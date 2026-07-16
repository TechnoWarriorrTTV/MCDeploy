#pragma once

#include <drogon/HttpController.h>
#include <nlohmann/json.hpp>

using namespace drogon;

namespace MCDeploy {

// AI Editor Controller — powers the /api/servers/{uuid}/ai/* endpoints.
// The heavy lifting (tool-calling loop, tool implementations, DB persistence,
// undo, rate limiting, path safety) all lives in ai/AIAgent.{h,cpp}. This
// controller just handles HTTP concerns and defers to the agent.
class AIEditorController : public drogon::HttpController<AIEditorController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(AIEditorController::chat,             "/api/servers/{uuid}/ai/chat",         Post);
        ADD_METHOD_TO(AIEditorController::getConversation,  "/api/servers/{uuid}/ai/conversation", Get);
        ADD_METHOD_TO(AIEditorController::clearConversation,"/api/servers/{uuid}/ai/conversation", Delete);
        ADD_METHOD_TO(AIEditorController::approveAction,    "/api/servers/{uuid}/ai/approve",      Post);
        ADD_METHOD_TO(AIEditorController::undoLast,         "/api/servers/{uuid}/ai/undo",         Post);
        ADD_METHOD_TO(AIEditorController::previewDiff,      "/api/servers/{uuid}/ai/diff",         Post);
        ADD_METHOD_TO(AIEditorController::getUsage,         "/api/servers/{uuid}/ai/usage",        Get);
        ADD_METHOD_TO(AIEditorController::getToolCallLog,   "/api/servers/{uuid}/ai/tool-log",     Get);
    METHOD_LIST_END

    void chat(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void getConversation(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void clearConversation(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void approveAction(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void undoLast(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void previewDiff(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void getUsage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void getToolCallLog(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);

private:
    bool validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole);
};

} // namespace MCDeploy
