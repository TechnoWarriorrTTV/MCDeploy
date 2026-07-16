#pragma once

// ============================================================
// MCDeploy - Scheduler
// ------------------------------------------------------------
// Long-lived background thread that polls the scheduled_tasks
// table every ~10 seconds, executes any tasks whose next_run_at
// has arrived, and writes the outcome plus the recomputed
// next_run_at back to the DB.
//
// The scheduler is a singleton started once from main() after
// database init. Task execution is dispatched via ServerLifecycle
// (start/stop/restart/backup) or ProcessManager (send console
// command) or AIAgent (run prompt), so it uses the same code
// paths as the interactive UI.
// ============================================================

#include <atomic>
#include <string>
#include <thread>

#include "../models/Database.h"

namespace MCDeploy {

class Scheduler {
public:
    static Scheduler& getInstance();

    // Start the background poller. Idempotent — a second call is a no-op.
    // `backupsRoot` is passed through to ServerLifecycle::createBackup so
    // scheduled backups match the interactive backup path.
    void start(const std::string& backupsRoot);

    // Stop the poller and join the thread.
    void stop();

    // Compute the next_run_at value for a given task, based on its
    // schedule_kind + schedule_value. Public so the REST controller can
    // reuse the same clock math when creating/updating tasks.
    static std::string computeNextRun(const std::string& kind, const std::string& value);

    // Execute one task synchronously in the calling thread. Public so the
    // "run now" endpoint can share the exact same execution path.
    struct ExecOutcome {
        std::string status; // "success" | "error" | "skipped"
        std::string output;
    };
    ExecOutcome executeTask(const ScheduledTaskRecord& t) const;

    // Evaluate a persisted automation rule and execute its action when the
    // trigger matches. `force` bypasses the trigger for the Run Now endpoint.
    ExecOutcome evaluateAutomationRule(const AutomationRuleRecord& rule, bool force = false) const;

private:
    Scheduler() = default;
    ~Scheduler();
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    std::atomic<bool> m_running{false};
    std::thread m_thread;
    std::string m_backupsRoot;

    void loop();
};

} // namespace MCDeploy
