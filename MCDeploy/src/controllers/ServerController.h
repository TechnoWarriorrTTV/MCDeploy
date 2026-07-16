#pragma once

#include <drogon/HttpController.h>
#include <nlohmann/json.hpp>

using namespace drogon;

namespace MCDeploy {

class ServerController : public drogon::HttpController<ServerController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(ServerController::listServers, "/api/servers", Get);
        ADD_METHOD_TO(ServerController::createServer, "/api/servers", Post);
        ADD_METHOD_TO(ServerController::importServer, "/api/servers/import", Post);
        ADD_METHOD_TO(ServerController::deleteServer, "/api/servers/{uuid}", Delete);
        ADD_METHOD_TO(ServerController::controlServer, "/api/servers/{uuid}/control", Post);
        ADD_METHOD_TO(ServerController::getHealth, "/api/servers/{uuid}/health", Get);
        ADD_METHOD_TO(ServerController::listAutomationRules, "/api/servers/{uuid}/automation", Get);
        ADD_METHOD_TO(ServerController::createAutomationRule, "/api/servers/{uuid}/automation", Post);
        ADD_METHOD_TO(ServerController::updateAutomationRule, "/api/servers/{uuid}/automation/{id}", Put);
        ADD_METHOD_TO(ServerController::deleteAutomationRule, "/api/servers/{uuid}/automation/{id}", Delete);
        ADD_METHOD_TO(ServerController::toggleAutomationRule, "/api/servers/{uuid}/automation/{id}/toggle", Post);
        ADD_METHOD_TO(ServerController::runAutomationRule, "/api/servers/{uuid}/automation/{id}/run", Post);
        ADD_METHOD_TO(ServerController::getMaintenance, "/api/servers/{uuid}/maintenance", Get);
        ADD_METHOD_TO(ServerController::updateMaintenance, "/api/servers/{uuid}/maintenance", Put);
        ADD_METHOD_TO(ServerController::enableMaintenance, "/api/servers/{uuid}/maintenance/enable", Post);
        ADD_METHOD_TO(ServerController::disableMaintenance, "/api/servers/{uuid}/maintenance/disable", Post);
        ADD_METHOD_TO(ServerController::getConfig, "/api/servers/{uuid}/config", Get);
        ADD_METHOD_TO(ServerController::updateConfig, "/api/servers/{uuid}/config", Post);
        ADD_METHOD_TO(ServerController::getBackups, "/api/servers/{uuid}/backups", Get);
        ADD_METHOD_TO(ServerController::createBackup, "/api/servers/{uuid}/backups", Post);
        ADD_METHOD_TO(ServerController::getVersions, "/api/servers/software/versions", Get);
        ADD_METHOD_TO(ServerController::checkSubdomain, "/api/servers/check-subdomain", Get);
        ADD_METHOD_TO(ServerController::getPerformance, "/api/servers/{uuid}/performance", Get);
        ADD_METHOD_TO(ServerController::updatePerformance, "/api/servers/{uuid}/performance", Post);
        
        // Modpack endpoints
        ADD_METHOD_TO(ServerController::searchModpacks, "/api/modpacks/search", Get);
        ADD_METHOD_TO(ServerController::getModpackVersions, "/api/modpacks/{modId}/versions", Get);
        
        // File manager
        ADD_METHOD_TO(ServerController::listFiles, "/api/servers/{uuid}/files", Get);
        ADD_METHOD_TO(ServerController::viewFile, "/api/servers/{uuid}/files/view", Get);
        ADD_METHOD_TO(ServerController::saveFile, "/api/servers/{uuid}/files/save", Post);

        // Addons (Plugins / Mods)
        ADD_METHOD_TO(ServerController::searchAddons, "/api/servers/{uuid}/addons/search", Get);
        ADD_METHOD_TO(ServerController::getAddonVersions, "/api/servers/{uuid}/addons/{addonId}/versions", Get);
        ADD_METHOD_TO(ServerController::installAddon, "/api/servers/{uuid}/addons/install", Post);
        ADD_METHOD_TO(ServerController::getInstalledAddons, "/api/servers/{uuid}/addons/installed", Get);
        ADD_METHOD_TO(ServerController::uninstallAddon, "/api/servers/{uuid}/addons/uninstall", Delete);
    METHOD_LIST_END
 
    void listServers(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void createServer(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void importServer(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void deleteServer(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void controlServer(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void getHealth(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void listAutomationRules(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void createAutomationRule(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void updateAutomationRule(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string id);
    void deleteAutomationRule(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string id);
    void toggleAutomationRule(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string id);
    void runAutomationRule(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string id);
    void getMaintenance(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void updateMaintenance(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void enableMaintenance(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void disableMaintenance(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void getConfig(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void updateConfig(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void getPerformance(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void updatePerformance(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void getBackups(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void createBackup(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void getVersions(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void checkSubdomain(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    
    // Modpacks
    void searchModpacks(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void getModpackVersions(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string modId);
    
    // File endpoints
    void listFiles(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void viewFile(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void saveFile(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);

    // Addons (Plugins / Mods)
    void searchAddons(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void getAddonVersions(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string addonId);
    void installAddon(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void getInstalledAddons(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void uninstallAddon(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);

private:
    bool validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole);
    std::string generateUuid();
};

} // namespace MCDeploy
