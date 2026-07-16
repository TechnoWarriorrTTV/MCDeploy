#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace MCDeploy {

class DashboardController : public drogon::HttpController<DashboardController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(DashboardController::login, "/api/auth/login", Post);
        ADD_METHOD_TO(DashboardController::getMetrics, "/api/system/metrics", Get);
        ADD_METHOD_TO(DashboardController::getAuditLogs, "/api/system/audit-logs", Get);
    METHOD_LIST_END

    void login(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void getMetrics(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void getAuditLogs(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

private:
    bool validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole);
};

} // namespace MCDeploy
