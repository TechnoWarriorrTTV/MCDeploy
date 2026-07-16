#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace MCDeploy {

// Read-only analytics endpoints backed by the player_events and
// player_sessions tables. Session rows are opened / closed by the log
// parser in ProcessManager::addLogLine.
class AnalyticsController : public drogon::HttpController<AnalyticsController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(AnalyticsController::summary,     "/api/servers/{uuid}/analytics/summary",     Get);
        ADD_METHOD_TO(AnalyticsController::hourly,      "/api/servers/{uuid}/analytics/hourly",      Get);
        ADD_METHOD_TO(AnalyticsController::daily,       "/api/servers/{uuid}/analytics/daily",       Get);
        ADD_METHOD_TO(AnalyticsController::leaderboard, "/api/servers/{uuid}/analytics/leaderboard", Get);
        ADD_METHOD_TO(AnalyticsController::events,      "/api/servers/{uuid}/analytics/events",      Get);
    METHOD_LIST_END

    void summary    (const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void hourly     (const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void daily      (const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void leaderboard(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void events     (const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);

private:
    bool validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole);
};

} // namespace MCDeploy
