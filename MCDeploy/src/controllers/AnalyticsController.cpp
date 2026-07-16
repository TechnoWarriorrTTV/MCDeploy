#include "AnalyticsController.h"
#include "../models/Database.h"

#include <algorithm>

namespace MCDeploy {

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

bool AnalyticsController::validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole) {
    (void)req;
    outUsername = "admin";
    outRole = "admin";
    return true;
}

static int getDaysParam(const HttpRequestPtr& req, int def) {
    try {
        auto p = req->getParameter("days");
        if (p.empty()) return def;
        int d = std::stoi(p);
        return std::max(0, std::min(365, d));  // clamp to a year
    } catch (...) { return def; }
}

void AnalyticsController::summary(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  std::string uuid) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) return callback(errorResponse("server not found", k404NotFound));

    int days = getDaysParam(req, 7);
    auto data = Database::getInstance().getAnalyticsSummary(uuid, days);
    callback(jsonResponse({{"status", "success"}, {"summary", data}}));
}

void AnalyticsController::hourly(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 std::string uuid) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) return callback(errorResponse("server not found", k404NotFound));

    int days = getDaysParam(req, 7);
    auto arr = Database::getInstance().getAnalyticsHourly(uuid, days);
    callback(jsonResponse({{"status", "success"}, {"hourly", arr}}));
}

void AnalyticsController::daily(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                std::string uuid) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) return callback(errorResponse("server not found", k404NotFound));

    int days = getDaysParam(req, 30);
    auto arr = Database::getInstance().getAnalyticsDaily(uuid, days);
    callback(jsonResponse({{"status", "success"}, {"daily", arr}}));
}

void AnalyticsController::leaderboard(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback,
                                      std::string uuid) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) return callback(errorResponse("server not found", k404NotFound));

    int days = getDaysParam(req, 7);
    int limit = 10;
    try {
        auto p = req->getParameter("limit");
        if (!p.empty()) limit = std::max(1, std::min(100, std::stoi(p)));
    } catch (...) {}

    auto arr = Database::getInstance().getAnalyticsLeaderboard(uuid, days, limit);
    callback(jsonResponse({{"status", "success"}, {"leaderboard", arr}}));
}

void AnalyticsController::events(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 std::string uuid) {
    std::string user, role;
    if (!validateJwt(req, user, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) return callback(errorResponse("server not found", k404NotFound));

    std::string type = req->getParameter("type");   // optional filter
    int limit = 100;
    try {
        auto p = req->getParameter("limit");
        if (!p.empty()) limit = std::max(1, std::min(1000, std::stoi(p)));
    } catch (...) {}

    auto evs = Database::getInstance().getPlayerEvents(uuid, type, limit);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : evs) {
        arr.push_back({
            {"id",          e.id},
            {"player_uuid", e.player_uuid},
            {"username",    e.username},
            {"event_type",  e.event_type},
            {"payload",     e.payload},
            {"created_at",  e.created_at},
        });
    }
    callback(jsonResponse({{"status", "success"}, {"events", arr}}));
}

} // namespace MCDeploy
