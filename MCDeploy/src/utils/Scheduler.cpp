#include "Scheduler.h"

#include "CronParser.h"
#include "ProcessManager.h"
#include "ServerLifecycle.h"
#include "SystemInfo.h"
#include "../ai/AIAgent.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace MCDeploy {

Scheduler& Scheduler::getInstance() {
    static Scheduler instance;
    return instance;
}

Scheduler::~Scheduler() { stop(); }

// ============================================================
// Time helpers
// ============================================================
// The database stores times as UTC using SQLite's datetime('now'). All
// comparisons happen inside the DB against datetime('now'), so we always
// serialize next_run_at in the same "YYYY-MM-DD HH:MM:SS" UTC form.
static std::string toUtcSqlite(std::time_t t) {
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    std::stringstream ss;
    ss << std::put_time(&tmv, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// ============================================================
// computeNextRun
// ============================================================
std::string Scheduler::computeNextRun(const std::string& kind, const std::string& value) {
    std::time_t now = std::time(nullptr);

    if (kind == "interval") {
        int seconds = 0;
        try { seconds = std::stoi(value); } catch (...) { seconds = 0; }
        if (seconds < 30) seconds = 30;  // enforce a sane minimum
        return toUtcSqlite(now + seconds);
    }

    if (kind == "daily") {
        // value = "HH:MM" in the server's local time.
        if (value.size() < 4 || value.find(':') == std::string::npos) return toUtcSqlite(now + 60);
        int hh = 0, mm = 0;
        try {
            hh = std::stoi(value.substr(0, value.find(':')));
            mm = std::stoi(value.substr(value.find(':') + 1));
        } catch (...) { return toUtcSqlite(now + 60); }

        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        local.tm_hour = hh;
        local.tm_min  = mm;
        local.tm_sec  = 0;
        std::time_t candidate = std::mktime(&local);
        if (candidate <= now) candidate += 24 * 60 * 60;
        return toUtcSqlite(candidate);
    }

    if (kind == "cron") {
        auto parsed = CronExpression::parse(value);
        if (!parsed) return toUtcSqlite(now + 60);
        std::time_t next = parsed->nextAfter(now);
        if (next == 0) return toUtcSqlite(now + 60);
        return toUtcSqlite(next);
    }

    return toUtcSqlite(now + 60);
}

// ============================================================
// executeTask
// ============================================================
Scheduler::ExecOutcome Scheduler::executeTask(const ScheduledTaskRecord& t) const {
    ExecOutcome out;

    if (t.action_type == "console") {
        if (t.payload.empty()) {
            out.status = "error"; out.output = "empty console command";
            return out;
        }
        if (!ProcessManager::getInstance().isServerRunning(t.server_uuid)) {
            out.status = "skipped"; out.output = "server offline; command not sent";
            return out;
        }
        bool ok = ProcessManager::getInstance().sendCommand(t.server_uuid, t.payload);
        out.status = ok ? "success" : "error";
        out.output = ok ? ("sent: " + t.payload) : "sendCommand returned false";
        return out;
    }

    if (t.action_type == "start") {
        auto r = ServerLifecycle::start(t.server_uuid, "scheduler");
        out.status = r.ok ? "success" : "error";
        out.output = r.message;
        return out;
    }
    if (t.action_type == "stop") {
        auto r = ServerLifecycle::stop(t.server_uuid, "scheduler", /*force=*/false);
        out.status = r.ok ? "success" : "error";
        out.output = r.message;
        return out;
    }
    if (t.action_type == "restart") {
        auto r = ServerLifecycle::restart(t.server_uuid, "scheduler");
        out.status = r.ok ? "success" : "error";
        out.output = r.message;
        return out;
    }
    if (t.action_type == "backup") {
        auto r = ServerLifecycle::createBackup(t.server_uuid, "scheduler", m_backupsRoot);
        out.status = r.ok ? "success" : "error";
        out.output = r.message;
        return out;
    }
    if (t.action_type == "ai_prompt") {
        try {
            ServerRecord s;
            if (!Database::getInstance().getServer(t.server_uuid, s)) {
                out.status = "error"; out.output = "server not found for AI task";
                return out;
            }
            AIAgent agent(t.server_uuid, s.directory_path, "scheduler",
                          /*agentMode=*/false, loadAiConfig());
            auto turn = agent.runTurn(t.payload);
            out.status = turn.ok ? "success" : "error";
            out.output = turn.final_message.size() > 1500
                       ? turn.final_message.substr(0, 1500) + "…[truncated]"
                       : turn.final_message;
            if (!turn.error.empty()) {
                out.status = "error";
                out.output = turn.error;
            }
        } catch (const std::exception& e) {
            out.status = "error";
            out.output = std::string("ai_prompt exception: ") + e.what();
        }
        return out;
    }

    out.status = "error";
    out.output = "unknown action_type: " + t.action_type;
    return out;
}

// ============================================================
// evaluateAutomationRule
// ============================================================
Scheduler::ExecOutcome Scheduler::evaluateAutomationRule(const AutomationRuleRecord& rule, bool force) const {
    bool matched = force;
    std::string evidence = force ? "manual run" : "";

    if (!force) {
        if (rule.trigger_type == "server_offline") {
            matched = !ProcessManager::getInstance().isServerRunning(rule.server_uuid);
            evidence = matched ? "server is offline" : "server is online";
        } else if (rule.trigger_type == "cpu_above" || rule.trigger_type == "ram_above" ||
                   rule.trigger_type == "disk_below") {
            ServerRecord server;
            if (!Database::getInstance().getServer(rule.server_uuid, server)) {
                return {"error", "server not found"};
            }
            auto metrics = SystemInfo::getInstance().getMetrics(server.directory_path);
            double measured = 0.0;
            if (rule.trigger_type == "cpu_above") {
                measured = metrics.cpu_usage_pct;
                matched = measured > rule.threshold;
                evidence = "CPU " + std::to_string(static_cast<int>(measured)) + "%";
            } else if (rule.trigger_type == "ram_above") {
                measured = metrics.ram_total_gb > 0.0 ? (metrics.ram_used_gb / metrics.ram_total_gb) * 100.0 : 0.0;
                matched = measured > rule.threshold;
                evidence = "RAM " + std::to_string(static_cast<int>(measured)) + "%";
            } else {
                measured = metrics.disk_free_gb;
                matched = measured < rule.threshold;
                std::ostringstream value;
                value << std::fixed << std::setprecision(1) << measured;
                evidence = "disk free " + value.str() + " GB";
            }
        } else if (rule.trigger_type == "log_contains") {
            std::string needle = rule.condition_value;
            std::transform(needle.begin(), needle.end(), needle.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto logs = ProcessManager::getInstance().getLogs(rule.server_uuid, 200);
            for (const auto& line : logs) {
                std::string haystack = line.text;
                std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (!needle.empty() && haystack.find(needle) != std::string::npos) {
                    matched = true;
                    evidence = "matched recent log: " + line.text.substr(0, 180);
                    break;
                }
            }
            if (!matched) evidence = "text not found in recent logs";
        } else {
            return {"error", "unknown trigger_type: " + rule.trigger_type};
        }
    }

    if (!matched) return {"idle", evidence};

    ScheduledTaskRecord action;
    action.server_uuid = rule.server_uuid;
    action.name = rule.name;
    action.action_type = rule.action_type;
    action.payload = rule.action_payload;
    auto outcome = executeTask(action);
    if (!evidence.empty()) outcome.output = evidence + "; " + outcome.output;
    return outcome;
}

// ============================================================
// start / stop / loop
// ============================================================
void Scheduler::start(const std::string& backupsRoot) {
    if (m_running.exchange(true)) return; // already running
    m_backupsRoot = backupsRoot;
    m_thread = std::thread(&Scheduler::loop, this);
}

void Scheduler::stop() {
    if (!m_running.exchange(false)) return;
    if (m_thread.joinable()) m_thread.join();
}

void Scheduler::loop() {
    std::cout << "[MCDeploy Scheduler] Poller started." << std::endl;
    while (m_running) {
        try {
            auto due = Database::getInstance().listDueScheduledTasks();
            for (const auto& task : due) {
                if (!m_running) break;
                std::string startedAt = toUtcSqlite(std::time(nullptr));
                ExecOutcome outcome = executeTask(task);
                std::string finishedAt = toUtcSqlite(std::time(nullptr));

                // Persist run history + update the task's next_run_at.
                Database::getInstance().insertScheduledTaskRun({
                    /*id*/ 0, task.id, startedAt, finishedAt, outcome.status, outcome.output
                });
                std::string nextRun = computeNextRun(task.schedule_kind, task.schedule_value);
                Database::getInstance().recordScheduledTaskResult(task.id, outcome.status, outcome.output, nextRun);
            }

            // Automation rules share this poller and the same action executor.
            // Cooldowns are filtered in SQL so a sustained condition cannot fire
            // on every ten-second pass.
            auto rules = Database::getInstance().listRunnableAutomationRules();
            for (const auto& rule : rules) {
                if (!m_running) break;
                ExecOutcome outcome = evaluateAutomationRule(rule, false);
                const bool triggered = outcome.status != "idle";
                Database::getInstance().recordAutomationEvaluation(
                    rule.id, triggered, outcome.status, outcome.output);
                if (triggered) {
                    Database::getInstance().logAction(
                        "automation", "AUTOMATION_RULE_TRIGGERED", rule.server_uuid,
                        "rule id=" + std::to_string(rule.id) + " status=" + outcome.status);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[MCDeploy Scheduler] loop error: " << e.what() << std::endl;
        }

        // 10-second polling cadence — the minimum grain is 1 minute anyway.
        for (int i = 0; i < 20 && m_running; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    std::cout << "[MCDeploy Scheduler] Poller stopped." << std::endl;
}

} // namespace MCDeploy
