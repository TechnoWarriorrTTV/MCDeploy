#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace MCDeploy {

// Restricted API used by the separately deployed public React webpanel.
// Every server route derives the member email from the signed login token and
// checks that server's active membership and granular permissions.
class WebPanelController : public drogon::HttpController<WebPanelController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(WebPanelController::status,        "/api/webpanel/status", Get);
        ADD_METHOD_TO(WebPanelController::registerAccount,"/api/webpanel/register", Post);
        ADD_METHOD_TO(WebPanelController::verifyRegistration,"/api/webpanel/register/verify", Post);
        ADD_METHOD_TO(WebPanelController::resendVerification,"/api/webpanel/register/resend", Post);
        ADD_METHOD_TO(WebPanelController::login,         "/api/webpanel/login", Post);
        ADD_METHOD_TO(WebPanelController::oauthStart,    "/api/webpanel/oauth/{provider}/start", Get);
        ADD_METHOD_TO(WebPanelController::oauthCallback, "/api/webpanel/oauth/{provider}/callback", Get);
        ADD_METHOD_TO(WebPanelController::oauthExchange, "/api/webpanel/oauth/exchange", Post);
        ADD_METHOD_TO(WebPanelController::session,       "/api/webpanel/session", Get);
        ADD_METHOD_TO(WebPanelController::servers,     "/api/webpanel/servers", Get);
        ADD_METHOD_TO(WebPanelController::overview,    "/api/webpanel/servers/{uuid}", Get);
        ADD_METHOD_TO(WebPanelController::control,     "/api/webpanel/servers/{uuid}/control", Post);
        ADD_METHOD_TO(WebPanelController::logs,        "/api/webpanel/servers/{uuid}/logs", Get);
        ADD_METHOD_TO(WebPanelController::command,     "/api/webpanel/servers/{uuid}/command", Post);
        ADD_METHOD_TO(WebPanelController::backups,     "/api/webpanel/servers/{uuid}/backups", Get);
        ADD_METHOD_TO(WebPanelController::createBackup, "/api/webpanel/servers/{uuid}/backups", Post);
        ADD_METHOD_TO(WebPanelController::health,      "/api/webpanel/servers/{uuid}/health", Get);
        ADD_METHOD_TO(WebPanelController::config,      "/api/webpanel/servers/{uuid}/config", Get);
        ADD_METHOD_TO(WebPanelController::updateConfig,"/api/webpanel/servers/{uuid}/config", Put);
        ADD_METHOD_TO(WebPanelController::files,       "/api/webpanel/servers/{uuid}/files", Get);
        ADD_METHOD_TO(WebPanelController::file,        "/api/webpanel/servers/{uuid}/file", Get);
        ADD_METHOD_TO(WebPanelController::saveFile,    "/api/webpanel/servers/{uuid}/file", Put);
        ADD_METHOD_TO(WebPanelController::players,     "/api/webpanel/servers/{uuid}/players", Get);
        ADD_METHOD_TO(WebPanelController::analytics,   "/api/webpanel/servers/{uuid}/analytics", Get);
        ADD_METHOD_TO(WebPanelController::schedule,    "/api/webpanel/servers/{uuid}/schedule", Get);
        ADD_METHOD_TO(WebPanelController::performance, "/api/webpanel/servers/{uuid}/performance", Get);
        ADD_METHOD_TO(WebPanelController::updatePerformance,"/api/webpanel/servers/{uuid}/performance", Put);
        ADD_METHOD_TO(WebPanelController::automation,  "/api/webpanel/servers/{uuid}/automation", Get);
        ADD_METHOD_TO(WebPanelController::maintenance, "/api/webpanel/servers/{uuid}/maintenance", Get);
        ADD_METHOD_TO(WebPanelController::updateMaintenance,"/api/webpanel/servers/{uuid}/maintenance", Put);
        ADD_METHOD_TO(WebPanelController::plugins,     "/api/webpanel/servers/{uuid}/plugins", Get);
        ADD_METHOD_TO(WebPanelController::searchPlugins,"/api/webpanel/servers/{uuid}/plugins/search", Get);
        ADD_METHOD_TO(WebPanelController::pluginVersions,"/api/webpanel/servers/{uuid}/plugins/{addon}/versions", Get);
        ADD_METHOD_TO(WebPanelController::installPlugin,"/api/webpanel/servers/{uuid}/plugins/install", Post);
        ADD_METHOD_TO(WebPanelController::uninstallPlugin,"/api/webpanel/servers/{uuid}/plugins/uninstall", Delete);
        ADD_METHOD_TO(WebPanelController::aiConversation,"/api/webpanel/servers/{uuid}/ai/conversation", Get);
        ADD_METHOD_TO(WebPanelController::clearAiConversation,"/api/webpanel/servers/{uuid}/ai/conversation", Delete);
        ADD_METHOD_TO(WebPanelController::aiChat,      "/api/webpanel/servers/{uuid}/ai/chat", Post);
        ADD_METHOD_TO(WebPanelController::aiApprove,   "/api/webpanel/servers/{uuid}/ai/approve", Post);
        ADD_METHOD_TO(WebPanelController::aiUndo,      "/api/webpanel/servers/{uuid}/ai/undo", Post);
        ADD_METHOD_TO(WebPanelController::audit,       "/api/webpanel/servers/{uuid}/audit", Get);
    METHOD_LIST_END

    void status(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&);
    void registerAccount(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&);
    void verifyRegistration(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&);
    void resendVerification(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&);
    void login(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&);
    void oauthStart(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string provider);
    void oauthCallback(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string provider);
    void oauthExchange(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&);
    void session(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&);
    void servers(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&);
    void overview(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void control(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void logs(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void command(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void backups(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void createBackup(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void health(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void config(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void updateConfig(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void files(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void file(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void saveFile(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void players(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void analytics(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void schedule(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void performance(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void updatePerformance(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void automation(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void maintenance(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void updateMaintenance(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void plugins(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void searchPlugins(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void pluginVersions(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid, std::string addon);
    void installPlugin(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void uninstallPlugin(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void aiConversation(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void clearAiConversation(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void aiChat(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void aiApprove(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void aiUndo(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
    void audit(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&, std::string uuid);
};

} // namespace MCDeploy