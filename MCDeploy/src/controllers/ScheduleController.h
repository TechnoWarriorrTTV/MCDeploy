#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace MCDeploy {

// Scheduled tasks per server: create, edit, enable/disable, delete,
// run-now, and view execution history. The actual execution is driven
// by the background Scheduler thread — this controller is just the
// CRUD + on-demand invocation surface.
class ScheduleController : public drogon::HttpController<ScheduleController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(ScheduleController::listTasks,   "/api/servers/{uuid}/schedule",              Get);
        ADD_METHOD_TO(ScheduleController::createTask,  "/api/servers/{uuid}/schedule",              Post);
        ADD_METHOD_TO(ScheduleController::updateTask,  "/api/servers/{uuid}/schedule/{id}",         Put);
        ADD_METHOD_TO(ScheduleController::deleteTask,  "/api/servers/{uuid}/schedule/{id}",         Delete);
        ADD_METHOD_TO(ScheduleController::toggleTask,  "/api/servers/{uuid}/schedule/{id}/toggle",  Post);
        ADD_METHOD_TO(ScheduleController::runTaskNow,  "/api/servers/{uuid}/schedule/{id}/run",     Post);
        ADD_METHOD_TO(ScheduleController::taskRuns,    "/api/servers/{uuid}/schedule/{id}/runs",    Get);
    METHOD_LIST_END

    void listTasks  (const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void createTask (const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void updateTask (const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string id);
    void deleteTask (const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string id);
    void toggleTask (const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string id);
    void runTaskNow (const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string id);
    void taskRuns   (const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string id);

private:
    bool validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole);
};

} // namespace MCDeploy
