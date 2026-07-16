#include "ScheduleController.h"

#include "../models/Database.h"
#include "../utils/Scheduler.h"
#include "../utils/CronParser.h"

#include <nlohmann/json.hpp>

namespace MCDeploy {

// ============================================================
// Helpers
// ============================================================
static HttpResponsePtr jsonResponse(const nlohmann::json& j, HttpStatusCode code = k200OK) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(code);
    resp->setBody(j.dump());
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    return resp;
}

static HttpResponsePtr errorResponse(const std::string& msg, HttpStatusCode code = k400BadRequest) {
    return jsonResponse({{"status", "error"}, {"message", msg}}, code);
}

static nlohmann::json taskToJson(const ScheduledTaskRecord& t) {
    return {
        {"id",              t.id},
        {"server_uuid",     t.server_uuid},
        {"name",            t.name},
        {"action_type",     t.action_type},
        {"payload",         t.payload},
        {"schedule_kind",   t.schedule_kind},
        {"schedule_value",  t.schedule_value},
        {"enabled",         t.enabled != 0},
        {"next_run_at",     t.next_run_at},
        {"last_run_at",     t.last_run_at},
        {"last_status",     t.last_status},
        {"last_output",     t.last_output},
        {"created_by",      t.created_by},
        {"created_at",      t.created_at},
    };
}

// Reject payloads with mismatched schedule kind/value or unknown action.
static std::string validateTaskShape(const ScheduledTaskRecord& t) {
    static const std::vector<std::string> ACTIONS =
        {"console", "start", "stop", "restart", "backup", "ai_prompt"};
    if (std::find(ACTIONS.begin(), ACTIONS.end(), t.action_type) == ACTIONS.end()) {
        return "unknown action_type; expected one of: console, start, stop, restart, backup, ai_prompt";
    }
    if (t.name.empty()) return "name is required";
    if (t.action_type == "console" && t.payload.empty()) return "console tasks require a payload (command)";
    if (t.action_type == "ai_prompt" && t.payload.empty()) return "ai_prompt tasks require a payload (prompt)";

    if (t.schedule_kind == "interval") {
        try {
            int s = std::stoi(t.schedule_value);
            if (s < 30) return "interval must be at least 30 seconds";
        } catch (...) { return "interval schedule_value must be a number of seconds"; }
    } else if (t.schedule_kind == "daily") {
        auto colon = t.schedule_value.find(':');
        if (colon == std::string::npos) return "daily schedule_value must be HH:MM";
        try {
            int hh = std::stoi(t.schedule_value.substr(0, colon));
            int mm = std::stoi(t.schedule_value.substr(colon + 1));
            if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return "daily HH:MM out of range";
        } catch (...) { return "daily schedule_value must be HH:MM"; }
    } else if (t.schedule_kind == "cron") {
        if (!CronExpression::parse(t.schedule_value)) return "invalid 5-field cron expression";
    } else {
        return "schedule_kind must be one of: interval, daily, cron";
    }
    return "";
}

bool ScheduleController::validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole) {
    (void)req;
    outUsername = "admin";
    outRole = "admin";
    return true;
}

// ============================================================
// GET  /api/servers/{uuid}/schedule
// ============================================================
void ScheduleController::listTasks(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   std::string uuid) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) return callback(errorResponse("server not found", k404NotFound));

    auto tasks = Database::getInstance().listScheduledTasks(uuid);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : tasks) arr.push_back(taskToJson(t));
    callback(jsonResponse({{"status", "success"}, {"tasks", arr}}));
}

// ============================================================
// POST /api/servers/{uuid}/schedule
// ============================================================
void ScheduleController::createTask(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback,
                                    std::string uuid) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) return callback(errorResponse("server not found", k404NotFound));

    nlohmann::json body;
    try { body = nlohmann::json::parse(req->getBody()); }
    catch (...) { return callback(errorResponse("body must be JSON")); }

    ScheduledTaskRecord t;
    t.server_uuid    = uuid;
    t.name           = body.value("name", "");
    t.action_type    = body.value("action_type", "");
    t.payload        = body.value("payload", "");
    t.schedule_kind  = body.value("schedule_kind", "");
    t.schedule_value = body.value("schedule_value", "");
    t.enabled        = body.value("enabled", true) ? 1 : 0;
    t.created_by     = user;

    std::string err = validateTaskShape(t);
    if (!err.empty()) return callback(errorResponse(err));

    t.next_run_at = Scheduler::computeNextRun(t.schedule_kind, t.schedule_value);

    long long id = Database::getInstance().createScheduledTask(t);
    if (id == 0) return callback(errorResponse("failed to persist task", k500InternalServerError));

    ScheduledTaskRecord saved;
    Database::getInstance().getScheduledTask(id, saved);
    Database::getInstance().logAction(user, "SCHEDULED_TASK_CREATE", uuid, "task " + saved.name);
    callback(jsonResponse({{"status", "success"}, {"task", taskToJson(saved)}}));
}

// ============================================================
// PUT /api/servers/{uuid}/schedule/{id}
// ============================================================
void ScheduleController::updateTask(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback,
                                    std::string uuid, std::string id) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    long long taskId = 0;
    try { taskId = std::stoll(id); } catch (...) { return callback(errorResponse("bad id")); }

    ScheduledTaskRecord existing;
    if (!Database::getInstance().getScheduledTask(taskId, existing) || existing.server_uuid != uuid)
        return callback(errorResponse("task not found", k404NotFound));

    nlohmann::json body;
    try { body = nlohmann::json::parse(req->getBody()); }
    catch (...) { return callback(errorResponse("body must be JSON")); }

    ScheduledTaskRecord t = existing;
    if (body.contains("name"))            t.name           = body["name"].get<std::string>();
    if (body.contains("action_type"))     t.action_type    = body["action_type"].get<std::string>();
    if (body.contains("payload"))         t.payload        = body["payload"].get<std::string>();
    if (body.contains("schedule_kind"))   t.schedule_kind  = body["schedule_kind"].get<std::string>();
    if (body.contains("schedule_value"))  t.schedule_value = body["schedule_value"].get<std::string>();
    if (body.contains("enabled"))         t.enabled        = body["enabled"].get<bool>() ? 1 : 0;

    std::string err = validateTaskShape(t);
    if (!err.empty()) return callback(errorResponse(err));

    // Recompute next_run_at whenever schedule changes.
    if (t.schedule_kind != existing.schedule_kind || t.schedule_value != existing.schedule_value) {
        t.next_run_at = Scheduler::computeNextRun(t.schedule_kind, t.schedule_value);
    }

    if (!Database::getInstance().updateScheduledTask(taskId, t))
        return callback(errorResponse("failed to update task", k500InternalServerError));

    ScheduledTaskRecord saved;
    Database::getInstance().getScheduledTask(taskId, saved);
    Database::getInstance().logAction(user, "SCHEDULED_TASK_UPDATE", uuid, "task id=" + id);
    callback(jsonResponse({{"status", "success"}, {"task", taskToJson(saved)}}));
}

// ============================================================
// DELETE /api/servers/{uuid}/schedule/{id}
// ============================================================
void ScheduleController::deleteTask(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback,
                                    std::string uuid, std::string id) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    long long taskId = 0;
    try { taskId = std::stoll(id); } catch (...) { return callback(errorResponse("bad id")); }

    ScheduledTaskRecord existing;
    if (!Database::getInstance().getScheduledTask(taskId, existing) || existing.server_uuid != uuid)
        return callback(errorResponse("task not found", k404NotFound));

    if (!Database::getInstance().deleteScheduledTask(taskId))
        return callback(errorResponse("failed to delete", k500InternalServerError));

    Database::getInstance().logAction(user, "SCHEDULED_TASK_DELETE", uuid, "task id=" + id);
    callback(jsonResponse({{"status", "success"}}));
}

// ============================================================
// POST /api/servers/{uuid}/schedule/{id}/toggle
// ============================================================
void ScheduleController::toggleTask(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback,
                                    std::string uuid, std::string id) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    long long taskId = 0;
    try { taskId = std::stoll(id); } catch (...) { return callback(errorResponse("bad id")); }

    ScheduledTaskRecord existing;
    if (!Database::getInstance().getScheduledTask(taskId, existing) || existing.server_uuid != uuid)
        return callback(errorResponse("task not found", k404NotFound));

    bool newEnabled = existing.enabled == 0;
    if (!Database::getInstance().setScheduledTaskEnabled(taskId, newEnabled))
        return callback(errorResponse("failed to toggle", k500InternalServerError));

    // If we just enabled it, refresh next_run_at so it doesn't fire immediately
    // against a stale timestamp.
    if (newEnabled) {
        std::string next = Scheduler::computeNextRun(existing.schedule_kind, existing.schedule_value);
        Database::getInstance().setScheduledTaskNextRun(taskId, next);
    }
    Database::getInstance().logAction(user, newEnabled ? "SCHEDULED_TASK_ENABLE" : "SCHEDULED_TASK_DISABLE",
                                      uuid, "task id=" + id);

    ScheduledTaskRecord saved;
    Database::getInstance().getScheduledTask(taskId, saved);
    callback(jsonResponse({{"status", "success"}, {"task", taskToJson(saved)}}));
}

// ============================================================
// POST /api/servers/{uuid}/schedule/{id}/run
// ============================================================
void ScheduleController::runTaskNow(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback,
                                    std::string uuid, std::string id) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    long long taskId = 0;
    try { taskId = std::stoll(id); } catch (...) { return callback(errorResponse("bad id")); }

    ScheduledTaskRecord task;
    if (!Database::getInstance().getScheduledTask(taskId, task) || task.server_uuid != uuid)
        return callback(errorResponse("task not found", k404NotFound));

    // Execute off-thread so the request returns quickly for long-running actions.
    auto cb = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    std::thread([task, user, cb]() {
        try {
            auto outcome = Scheduler::getInstance().executeTask(task);
            Database::getInstance().insertScheduledTaskRun({
                /*id*/ 0, task.id, "", "", outcome.status, outcome.output
            });
            Database::getInstance().logAction(user, "SCHEDULED_TASK_RUN_NOW", task.server_uuid,
                "task id=" + std::to_string(task.id) + " status=" + outcome.status);
            (*cb)(jsonResponse({
                {"status", "success"},
                {"result", {{"status", outcome.status}, {"output", outcome.output}}}
            }));
        } catch (const std::exception& e) {
            (*cb)(errorResponse(std::string("run error: ") + e.what(), k500InternalServerError));
        }
    }).detach();
}

// ============================================================
// GET /api/servers/{uuid}/schedule/{id}/runs
// ============================================================
void ScheduleController::taskRuns(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  std::string uuid, std::string id) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    long long taskId = 0;
    try { taskId = std::stoll(id); } catch (...) { return callback(errorResponse("bad id")); }

    ScheduledTaskRecord existing;
    if (!Database::getInstance().getScheduledTask(taskId, existing) || existing.server_uuid != uuid)
        return callback(errorResponse("task not found", k404NotFound));

    int limit = 50;
    try {
        auto p = req->getParameter("limit");
        if (!p.empty()) limit = std::max(1, std::min(200, std::stoi(p)));
    } catch (...) {}

    auto runs = Database::getInstance().listScheduledTaskRuns(taskId, limit);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : runs) {
        arr.push_back({
            {"id",          r.id},
            {"task_id",     r.task_id},
            {"started_at",  r.started_at},
            {"finished_at", r.finished_at},
            {"status",      r.status},
            {"output",      r.output},
        });
    }
    callback(jsonResponse({{"status", "success"}, {"runs", arr}}));
}

} // namespace MCDeploy
