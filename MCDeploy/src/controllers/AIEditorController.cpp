#include "AIEditorController.h"
#include "../ai/AIAgent.h"
#include "../models/Database.h"

#include <fstream>
#include <sstream>
#include <thread>
#include <memory>
#include <filesystem>
#include <iostream>

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

// Consistent with the rest of the app (project-wide TODO: real JWT).
bool AIEditorController::validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole) {
    (void)req;
    outUsername = "admin";
    outRole = "admin";
    return true;
}

static nlohmann::json stepToJson(const AiStep& s) {
    nlohmann::json j;
    j["kind"] = s.kind;
    if (!s.tool_name.empty()) j["tool"] = s.tool_name;
    if (!s.arguments.is_null()) j["arguments"] = s.arguments;
    if (!s.result.empty()) j["result"] = s.result;
    if (!s.content.empty()) j["content"] = s.content;
    if (s.latency_ms > 0) j["latency_ms"] = s.latency_ms;
    if (s.needs_confirmation) j["needs_confirmation"] = true;
    if (!s.pending_tool_id.empty()) j["tool_call_id"] = s.pending_tool_id;
    return j;
}

// ============================================================
// POST /api/servers/{uuid}/ai/chat
// ============================================================
void AIEditorController::chat(const HttpRequestPtr& req,
                              std::function<void(const HttpResponsePtr&)>&& callback,
                              std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    auto json = req->getJsonObject();
    if (!json) return callback(errorResponse("request body must be JSON"));

    std::string message = json->get("message", "").asString();
    bool agentMode = json->get("agent_mode", false).asBool();
    if (message.empty()) return callback(errorResponse("'message' is required"));

    // Rate limit (60/min, 500/day) — generous for a single-user tool but bounds abuse.
    if (!Database::getInstance().aiRateLimitAllow(username, 60, 500))
        return callback(errorResponse("rate limit exceeded (60/min or 500/day)", k429TooManyRequests));

    ServerRecord server;
    if (!Database::getInstance().getServer(uuid, server) || server.uuid.empty())
        return callback(errorResponse("server not found", k404NotFound));

    // Kick the heavy work off the request thread — Drogon reuses the callback later.
    auto cb = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    std::thread([uuid, username, agentMode, message, serverPath = server.directory_path, cb]() {
        try {
            AIAgent agent(uuid, serverPath, username, agentMode, loadAiConfig());
            auto turn = agent.runTurn(message);

            nlohmann::json steps = nlohmann::json::array();
            for (const auto& s : turn.steps) steps.push_back(stepToJson(s));

            nlohmann::json resp;
            resp["status"] = turn.ok ? "success" : "error";
            resp["message"] = turn.final_message;
            resp["steps"] = steps;
            resp["suggestions"] = turn.suggestions;
            resp["agent_mode"] = agentMode;
            resp["tokens"] = { {"prompt", turn.tokens_prompt}, {"completion", turn.tokens_completion} };
            if (!turn.error.empty()) resp["error"] = turn.error;
            if (!turn.pending_actions.empty()) resp["pending_actions"] = turn.pending_actions;

            Database::getInstance().logAction(username, "AI_CHAT", uuid,
                std::string("agent=") + (agentMode ? "on" : "off") + " msg=" + message.substr(0, 100));

            (*cb)(jsonResponse(resp));
        } catch (const std::exception& e) {
            (*cb)(errorResponse(std::string("internal error: ") + e.what(), k500InternalServerError));
        }
    }).detach();
}

// ============================================================
// GET /api/servers/{uuid}/ai/conversation
// ============================================================
void AIEditorController::getConversation(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback,
                                         std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    auto rows = Database::getInstance().getAiConversation(uuid, username, 300);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) {
        nlohmann::json j;
        j["id"] = r.id;
        j["role"] = r.role;
        j["content"] = r.content;
        if (!r.tool_calls.empty()) {
            try { j["tool_calls"] = nlohmann::json::parse(r.tool_calls); } catch (...) {}
        }
        if (!r.tool_call_id.empty()) j["tool_call_id"] = r.tool_call_id;
        if (!r.tool_name.empty()) j["tool_name"] = r.tool_name;
        j["created_at"] = r.created_at;
        arr.push_back(j);
    }
    callback(jsonResponse({{"status", "success"}, {"conversation", arr}}));
}

// ============================================================
// DELETE /api/servers/{uuid}/ai/conversation
// ============================================================
void AIEditorController::clearConversation(const HttpRequestPtr& req,
                                           std::function<void(const HttpResponsePtr&)>&& callback,
                                           std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(errorResponse("unauthorized", k401Unauthorized));
    bool ok = Database::getInstance().clearAiConversation(uuid, username);
    Database::getInstance().logAction(username, "AI_CLEAR", uuid, "");
    callback(jsonResponse({{"status", ok ? "success" : "error"}, {"message", ok ? "conversation cleared" : "failed to clear"}}));
}

// ============================================================
// POST /api/servers/{uuid}/ai/approve
// Executes a previously flagged dangerous tool call.
// ============================================================
void AIEditorController::approveAction(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback,
                                       std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    auto j = req->getJsonObject();
    if (!j) return callback(errorResponse("request body must be JSON"));
    std::string toolName = j->get("tool", "").asString();
    if (toolName.empty()) return callback(errorResponse("'tool' is required"));

    nlohmann::json args;
    try {
        if (j->isMember("arguments")) args = nlohmann::json::parse((*j)["arguments"].toStyledString());
    } catch (...) {}
    if (args.is_null()) args = nlohmann::json::object();

    ServerRecord rec;
    if (!Database::getInstance().getServer(uuid, rec) || rec.uuid.empty())
        return callback(errorResponse("server not found", k404NotFound));

    AIAgent agent(uuid, rec.directory_path, username, /*agentMode*/ true, loadAiConfig());
    auto step = agent.executeApprovedTool(toolName, args);

    nlohmann::json resp;
    resp["status"] = "success";
    resp["step"] = stepToJson(step);
    callback(jsonResponse(resp));
}

// ============================================================
// POST /api/servers/{uuid}/ai/undo
// ============================================================
void AIEditorController::undoLast(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    ServerRecord rec;
    if (!Database::getInstance().getServer(uuid, rec) || rec.uuid.empty())
        return callback(errorResponse("server not found", k404NotFound));

    AIAgent agent(uuid, rec.directory_path, username, /*agentMode*/ true, loadAiConfig());
    std::string msg = agent.undoLast();
    int remaining = Database::getInstance().countAiUndoStack(uuid, username);

    callback(jsonResponse({
        {"status", msg.rfind("Error", 0) == 0 ? "error" : "success"},
        {"message", msg},
        {"undo_stack_size", remaining}
    }));
}

// ============================================================
// POST /api/servers/{uuid}/ai/diff
// Preview a diff between the current file contents and proposed new content.
// Used by the frontend's /diff slash command, independent of the agent loop.
// ============================================================
void AIEditorController::previewDiff(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(errorResponse("unauthorized", k401Unauthorized));
    auto j = req->getJsonObject();
    if (!j) return callback(errorResponse("request body must be JSON"));

    std::string path = j->get("path", "").asString();
    std::string content = j->get("new_content", "").asString();
    if (path.empty()) return callback(errorResponse("'path' is required"));

    ServerRecord rec;
    if (!Database::getInstance().getServer(uuid, rec) || rec.uuid.empty())
        return callback(errorResponse("server not found", k404NotFound));

    // Read the current file and return both sides. The frontend renders the
    // diff visually (accept / reject buttons); the AI-facing unified diff lives
    // inside AIAgent for LLM consumption.
    std::filesystem::path abs(rec.directory_path);
    abs /= path;
    std::string oldContent;
    bool existed = std::filesystem::exists(abs) && std::filesystem::is_regular_file(abs);
    if (existed) {
        std::ifstream f(abs, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        oldContent = ss.str();
    }
    callback(jsonResponse({
        {"status", "success"},
        {"path", path},
        {"old_content", oldContent},
        {"new_content", content},
        {"existed", existed}
    }));
}

// ============================================================
// GET /api/servers/{uuid}/ai/usage
// ============================================================
void AIEditorController::getUsage(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    auto totals = Database::getInstance().getAiUsageTotals(uuid, username);
    int undoSize = Database::getInstance().countAiUndoStack(uuid, username);

    nlohmann::json resp;
    resp["status"] = "success";
    resp["tokens_prompt"] = totals.tokens_prompt;
    resp["tokens_completion"] = totals.tokens_completion;
    resp["tokens_total"] = totals.tokens_prompt + totals.tokens_completion;
    resp["request_count"] = totals.request_count;
    resp["last_model"] = totals.model;
    resp["undo_stack_size"] = undoSize;
    callback(jsonResponse(resp));
}

// ============================================================
// GET /api/servers/{uuid}/ai/tool-log
// ============================================================
void AIEditorController::getToolCallLog(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(errorResponse("unauthorized", k401Unauthorized));

    auto rows = Database::getInstance().getRecentAiToolCalls(uuid, 50);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) {
        nlohmann::json j;
        j["id"] = r.id;
        j["tool_name"] = r.tool_name;
        j["status"] = r.status;
        j["latency_ms"] = r.latency_ms;
        j["created_at"] = r.created_at;
        try { j["arguments"] = nlohmann::json::parse(r.arguments); } catch (...) { j["arguments"] = r.arguments; }
        j["result_preview"] = r.result.size() > 400 ? r.result.substr(0, 400) + "…" : r.result;
        arr.push_back(j);
    }
    callback(jsonResponse({{"status", "success"}, {"tool_calls", arr}}));
}

} // namespace MCDeploy
