#pragma once

#include <drogon/HttpController.h>
#include "../models/Database.h"
#include "../utils/ProcessManager.h"
#include <nlohmann/json.hpp>
#include <iostream>

using namespace drogon;

namespace MCDeploy {

class PlayerController : public drogon::HttpController<PlayerController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(PlayerController::listPlayers, "/api/servers/{uuid}/players", Get);
        ADD_METHOD_TO(PlayerController::playerAction, "/api/servers/{uuid}/players/{username}/action", Post);
        ADD_METHOD_TO(PlayerController::playerPotion, "/api/servers/{uuid}/players/{username}/potion", Post);
        ADD_METHOD_TO(PlayerController::getInventory, "/api/servers/{uuid}/players/{username}/inventory", Get);
        ADD_METHOD_TO(PlayerController::updateItem, "/api/servers/{uuid}/players/{username}/inventory/update", Post);
        ADD_METHOD_TO(PlayerController::duplicateItem, "/api/servers/{uuid}/players/{username}/inventory/duplicate", Post);
        ADD_METHOD_TO(PlayerController::deleteItem, "/api/servers/{uuid}/players/{username}/inventory/delete", Post);
        ADD_METHOD_TO(PlayerController::giveItem, "/api/servers/{uuid}/players/{username}/inventory/give", Post);
        ADD_METHOD_TO(PlayerController::repairItem, "/api/servers/{uuid}/players/{username}/inventory/repair", Post);
        ADD_METHOD_TO(PlayerController::enchantItem, "/api/servers/{uuid}/players/{username}/inventory/enchant", Post);
        ADD_METHOD_TO(PlayerController::createBackup, "/api/servers/{uuid}/players/{username}/backup", Post);
        ADD_METHOD_TO(PlayerController::getBackups, "/api/servers/{uuid}/players/{username}/backups", Get);
        ADD_METHOD_TO(PlayerController::restoreBackup, "/api/servers/{uuid}/players/{username}/backup/restore", Post);
        ADD_METHOD_TO(PlayerController::getAdvancements, "/api/servers/{uuid}/players/{username}/advancements", Get);
        ADD_METHOD_TO(PlayerController::updateAdvancement, "/api/servers/{uuid}/players/{username}/advancements/update", Post);
    METHOD_LIST_END

    static HttpResponsePtr newJson(const nlohmann::json& j) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody(j.dump());
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        return resp;
    }

    void listPlayers(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
        (void)req;
        
        auto players = Database::getInstance().getServerPlayers(uuid);
        auto coords = Database::getInstance().getPlayerCoordinateLogs(uuid);

        nlohmann::json res = nlohmann::json::object();
        nlohmann::json plArr = nlohmann::json::array();
        for (const auto& p : players) {
            nlohmann::json pObj;
            pObj["uuid"] = p.uuid;
            pObj["server_uuid"] = p.server_uuid;
            pObj["username"] = p.username;
            pObj["is_online"] = p.is_online;
            pObj["health"] = p.health;
            pObj["hunger"] = p.hunger;
            pObj["frozen"] = p.frozen;
            pObj["last_login_x"] = p.last_login_x;
            pObj["last_login_y"] = p.last_login_y;
            pObj["last_login_z"] = p.last_login_z;
            pObj["last_logoff_x"] = p.last_logoff_x;
            pObj["last_logoff_y"] = p.last_logoff_y;
            pObj["last_logoff_z"] = p.last_logoff_z;
            plArr.push_back(pObj);
        }
        res["players"] = plArr;

        nlohmann::json cArr = nlohmann::json::array();
        for (const auto& c : coords) {
            nlohmann::json cObj;
            cObj["id"] = c.id;
            cObj["player_uuid"] = c.player_uuid;
            cObj["username"] = c.username;
            cObj["type"] = c.type;
            cObj["x"] = c.x;
            cObj["y"] = c.y;
            cObj["z"] = c.z;
            cObj["timestamp"] = c.timestamp;
            cArr.push_back(cObj);
        }
        res["coordinate_logs"] = cArr;

        callback(newJson(res));
    }

    void playerAction(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        std::string action = json->get("action", "").asString();
        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        bool success = false;
        if (action == "heal") {
            ProcessManager::getInstance().sendCommand(uuid, "effect give " + username + " minecraft:instant_health 1 255");
            success = Database::getInstance().updatePlayerStats(uuid, username, 20.0, p.hunger, p.frozen);
            Database::getInstance().logAction("admin", "PLAYER_HEAL", uuid, "Healed player: " + username);
        } 
        else if (action == "feed") {
            ProcessManager::getInstance().sendCommand(uuid, "effect give " + username + " minecraft:saturation 1 255");
            success = Database::getInstance().updatePlayerStats(uuid, username, p.health, 20, p.frozen);
            Database::getInstance().logAction("admin", "PLAYER_FEED", uuid, "Fed player: " + username);
        }
        else if (action == "freeze") {
            success = Database::getInstance().updatePlayerStats(uuid, username, p.health, p.hunger, 1);
            Database::getInstance().logAction("admin", "PLAYER_FREEZE", uuid, "Froze movement for player: " + username);
        }
        else if (action == "unfreeze") {
            success = Database::getInstance().updatePlayerStats(uuid, username, p.health, p.hunger, 0);
            Database::getInstance().logAction("admin", "PLAYER_UNFREEZE", uuid, "Unfroze movement for player: " + username);
        }
        else if (action == "kick") {
            std::string reason = json->get("reason", "Kicked by administrator").asString();
            ProcessManager::getInstance().sendCommand(uuid, "kick " + username + " " + reason);
            Database::getInstance().setPlayerOnline(uuid, username, 0);
            success = true;
            Database::getInstance().logAction("admin", "PLAYER_KICK", uuid, "Kicked player: " + username + ". Reason: " + reason);
        }
        else if (action == "ban") {
            std::string reason = json->get("reason", "Banned by administrator").asString();
            ProcessManager::getInstance().sendCommand(uuid, "ban " + username + " " + reason);
            Database::getInstance().setPlayerOnline(uuid, username, 0);
            success = true;
            Database::getInstance().logAction("admin", "PLAYER_BAN", uuid, "Banned player: " + username + ". Reason: " + reason);
        }
        else if (action == "tempban") {
            std::string reason = json->get("reason", "Temporarily banned by administrator").asString();
            std::string duration = json->get("duration", "1h").asString();
            ProcessManager::getInstance().sendCommand(uuid, "ban " + username + " Tempban duration: " + duration + ". Reason: " + reason);
            Database::getInstance().setPlayerOnline(uuid, username, 0);
            success = true;
            Database::getInstance().logAction("admin", "PLAYER_TEMPBAN", uuid, "Temporarily banned player: " + username + " for " + duration + ". Reason: " + reason);
        }
        else if (action == "timeout") {
            std::string duration = json->get("duration", "10m").asString();
            ProcessManager::getInstance().sendCommand(uuid, "mute " + username);
            success = true;
            Database::getInstance().logAction("admin", "PLAYER_TIMEOUT", uuid, "Muted/Timeout player: " + username + " for " + duration);
        }
        else if (action == "reset") {
            success = Database::getInstance().resetPlayerEntirely(uuid, username);
            Database::getInstance().logAction("admin", "PLAYER_RESET", uuid, "Reset entire player inventory, ender chests, and stats for: " + username);
        }

        nlohmann::json r;
        r["status"] = success ? "success" : "error";
        callback(newJson(r));
    }

    void playerPotion(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        std::string effect = json->get("effect", "").asString();
        int duration = json->get("duration", 30).asInt();
        int amplifier = json->get("amplifier", 1).asInt();

        if (effect.empty()) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        std::string cmd = "effect give " + username + " " + effect + " " + std::to_string(duration) + " " + std::to_string(amplifier);
        ProcessManager::getInstance().sendCommand(uuid, cmd);
        Database::getInstance().logAction("admin", "PLAYER_POTION", uuid, "Gave potion effect to player: " + username + " (" + effect + " amp:" + std::to_string(amplifier) + " dur:" + std::to_string(duration) + "s)");

        nlohmann::json r;
        r["status"] = "success";
        callback(newJson(r));
    }

    void getInventory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        (void)req;
        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        auto items = Database::getInstance().getPlayerItems(p.uuid);
        
        nlohmann::json res = nlohmann::json::object();
        nlohmann::json invArr = nlohmann::json::array();
        nlohmann::json endArr = nlohmann::json::array();

        for (const auto& item : items) {
            nlohmann::json j;
            j["id"] = item.id;
            j["player_uuid"] = item.player_uuid;
            j["type"] = item.type;
            j["slot"] = item.slot;
            j["item_id"] = item.item_id;
            j["count"] = item.count;
            j["display_name"] = item.display_name;
            j["unbreakable"] = item.unbreakable;
            j["custom_aura"] = item.custom_aura;
            j["potion_effect"] = item.potion_effect;
            
            try {
                j["enchants"] = nlohmann::json::parse(item.enchants);
            } catch (...) {
                j["enchants"] = nlohmann::json::object();
            }

            if (item.type == "inventory") {
                invArr.push_back(j);
            } else {
                endArr.push_back(j);
            }
        }
        res["inventory"] = invArr;
        res["ender_chest"] = endArr;

        callback(newJson(res));
    }

    void updateItem(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        PlayerItemRecord item;
        item.player_uuid = p.uuid;
        item.type = json->get("type", "inventory").asString();
        item.slot = json->get("slot", 0).asInt();
        item.item_id = json->get("item_id", "minecraft:air").asString();
        item.count = json->get("count", 1).asInt();
        item.display_name = json->get("display_name", "").asString();
        item.unbreakable = json->get("unbreakable", 0).asInt();
        item.custom_aura = json->get("custom_aura", "").asString();
        item.potion_effect = json->get("potion_effect", "").asString();
        
        auto enchantsJson = json->get("enchants", Json::Value());
        if (!enchantsJson.isNull() && enchantsJson.isObject()) {
            nlohmann::json n;
            // Transfer properties manually to nlohmann::json
            for (auto it = enchantsJson.begin(); it != enchantsJson.end(); ++it) {
                n[it.name()] = it->asInt();
            }
            item.enchants = n.dump();
        } else {
            item.enchants = "{}";
        }

        bool success = Database::getInstance().updatePlayerItem(item);
        Database::getInstance().logAction("admin", "PLAYER_ITEM_UPDATE", uuid, "Updated item in slot " + std::to_string(item.slot) + " (" + item.item_id + ") for player: " + username);

        nlohmann::json r;
        r["status"] = success ? "success" : "error";
        callback(newJson(r));
    }

    void duplicateItem(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        std::string type = json->get("type", "inventory").asString();
        int slot = json->get("slot", 0).asInt();
        int newSlot = json->get("new_slot", -1).asInt();

        auto items = Database::getInstance().getPlayerItems(p.uuid);
        PlayerItemRecord match;
        bool found = false;
        for (const auto& item : items) {
            if (item.type == type && item.slot == slot) {
                match = item;
                found = true;
                break;
            }
        }

        if (!found) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        match.slot = newSlot;
        bool success = Database::getInstance().updatePlayerItem(match);
        Database::getInstance().logAction("admin", "PLAYER_ITEM_DUPLICATE", uuid, "Duplicated item " + match.item_id + " from slot " + std::to_string(slot) + " to slot " + std::to_string(newSlot) + " for player: " + username);

        nlohmann::json r;
        r["status"] = success ? "success" : "error";
        callback(newJson(r));
    }

    void deleteItem(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        int itemId = json->get("id", 0).asInt();
        bool success = Database::getInstance().deletePlayerItem(itemId);
        Database::getInstance().logAction("admin", "PLAYER_ITEM_DELETE", uuid, "Deleted inventory item (ID: " + std::to_string(itemId) + ") for player: " + username);

        nlohmann::json r;
        r["status"] = success ? "success" : "error";
        callback(newJson(r));
    }

    void giveItem(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        std::string targetUsername = json->get("target_username", "").asString();
        std::string type = json->get("type", "inventory").asString();
        int slot = json->get("slot", 0).asInt();

        PlayerRecord target;
        if (!Database::getInstance().getOrCreatePlayer(uuid, targetUsername, target)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            resp->setBody("Target player not found.");
            callback(resp);
            return;
        }

        auto items = Database::getInstance().getPlayerItems(p.uuid);
        PlayerItemRecord match;
        bool found = false;
        for (const auto& item : items) {
            if (item.type == type && item.slot == slot) {
                match = item;
                found = true;
                break;
            }
        }

        if (!found) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        // Find first empty slot in target's inventory
        auto targetItems = Database::getInstance().getPlayerItems(target.uuid);
        std::unordered_map<int, bool> occupiedSlots;
        for (const auto& item : targetItems) {
            if (item.type == "inventory") {
                occupiedSlots[item.slot] = true;
            }
        }
        int emptySlot = 0;
        for (int i = 0; i < 36; i++) {
            if (occupiedSlots.find(i) == occupiedSlots.end()) {
                emptySlot = i;
                break;
            }
        }

        match.player_uuid = target.uuid;
        match.slot = emptySlot;
        match.type = "inventory";
        
        bool success = Database::getInstance().updatePlayerItem(match);
        if (success) {
            Database::getInstance().deletePlayerItem(match.id);
        }
        Database::getInstance().logAction("admin", "PLAYER_ITEM_TRANSFER", uuid, "Transferred item " + match.item_id + " from " + username + " to " + targetUsername);

        nlohmann::json r;
        r["status"] = success ? "success" : "error";
        callback(newJson(r));
    }

    void repairItem(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        std::string type = json->get("type", "inventory").asString();
        int slot = json->get("slot", 0).asInt();

        auto items = Database::getInstance().getPlayerItems(p.uuid);
        PlayerItemRecord match;
        bool found = false;
        for (const auto& item : items) {
            if (item.type == type && item.slot == slot) {
                match = item;
                found = true;
                break;
            }
        }

        if (!found) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        // Simulating repair (could trigger commands or NBT updates)
        Database::getInstance().logAction("admin", "PLAYER_ITEM_REPAIR", uuid, "Repaired item durability for " + match.item_id + " in slot " + std::to_string(slot) + " for player: " + username);

        nlohmann::json r;
        r["status"] = "success";
        callback(newJson(r));
    }

    void enchantItem(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        std::string type = json->get("type", "inventory").asString();
        int slot = json->get("slot", 0).asInt();
        
        auto enchantsJson = json->get("enchants", Json::Value());
        if (enchantsJson.isNull() || !enchantsJson.isObject()) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        auto items = Database::getInstance().getPlayerItems(p.uuid);
        PlayerItemRecord match;
        bool found = false;
        for (const auto& item : items) {
            if (item.type == type && item.slot == slot) {
                match = item;
                found = true;
                break;
            }
        }

        if (!found) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        nlohmann::json n;
        for (auto it = enchantsJson.begin(); it != enchantsJson.end(); ++it) {
            n[it.name()] = it->asInt();
        }
        match.enchants = n.dump();
        
        bool success = Database::getInstance().updatePlayerItem(match);
        Database::getInstance().logAction("admin", "PLAYER_ITEM_ENCHANT", uuid, "Modified enchants on item " + match.item_id + " in slot " + std::to_string(slot) + " for player: " + username);

        nlohmann::json r;
        r["status"] = success ? "success" : "error";
        callback(newJson(r));
    }

    void createBackup(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        std::string backupName = json->get("backup_name", "Manual Backup").asString();
        bool success = Database::getInstance().createPlayerBackup(p.uuid, backupName);
        Database::getInstance().logAction("admin", "PLAYER_BACKUP_CREATE", uuid, "Created backup snapshot (" + backupName + ") for player: " + username);

        nlohmann::json r;
        r["status"] = success ? "success" : "error";
        callback(newJson(r));
    }

    void getBackups(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        (void)req;
        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        auto list = Database::getInstance().getPlayerBackups(p.uuid);
        nlohmann::json r = nlohmann::json::array();
        for (const auto& b : list) {
            nlohmann::json item;
            item["backup_id"] = b.backup_id;
            item["player_uuid"] = b.player_uuid;
            item["backup_name"] = b.backup_name;
            item["created_at"] = b.created_at;
            r.push_back(item);
        }
        callback(newJson(r));
    }

    void restoreBackup(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        std::string backupId = json->get("backup_id", "").asString();
        bool success = Database::getInstance().restorePlayerBackup(p.uuid, backupId);
        Database::getInstance().logAction("admin", "PLAYER_BACKUP_RESTORE", uuid, "Restored player data snapshot " + backupId + " for player: " + username);

        nlohmann::json r;
        r["status"] = success ? "success" : "error";
        callback(newJson(r));
    }

    void getAdvancements(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        (void)req;
        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        auto list = Database::getInstance().getPlayerAdvancements(p.uuid);
        callback(newJson(list));
    }

    void updateAdvancement(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string username) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        PlayerRecord p;
        if (!Database::getInstance().getOrCreatePlayer(uuid, username, p)) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        std::string advancementId = json->get("advancement_id", "").asString();
        int granted = json->get("granted", 0).asInt();

        bool success = Database::getInstance().updatePlayerAdvancement(p.uuid, advancementId, granted);
        
        // Execute minecraft advancement command
        std::string cmd = std::string(granted ? "advancement grant " : "advancement revoke ") + username + " only minecraft:" + advancementId;
        ProcessManager::getInstance().sendCommand(uuid, cmd);
        
        Database::getInstance().logAction("admin", "PLAYER_ADVANCEMENT_UPDATE", uuid, std::string(granted ? "Granted" : "Revoked") + " advancement minecraft:" + advancementId + " for player: " + username);

        nlohmann::json r;
        r["status"] = success ? "success" : "error";
        callback(newJson(r));
    }
};

} // namespace MCDeploy
