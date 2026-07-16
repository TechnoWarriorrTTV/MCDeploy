#include "MembersController.h"
#include "../models/Database.h"

#include <nlohmann/json.hpp>
#include <regex>
#include <algorithm>
#include <cctype>

namespace MCDeploy {

// ============================================================
// Permission catalog — the exhaustive list of granular actions
// a member can be granted for a specific server. Presets below
// map role names to sane defaults; the frontend can override any
// individual flag.
// ============================================================
struct PermGroup {
    std::string label;
    std::vector<std::pair<std::string, std::string>> perms; // { key, description }
};

static const std::vector<PermGroup>& permissionGroups() {
    static const std::vector<PermGroup> groups = {
        { "Server lifecycle", {
            { "server.view",          "See this server on the dashboard" },
            { "server.start",         "Start the server" },
            { "server.stop",          "Stop the server (graceful)" },
            { "server.restart",       "Restart the server" },
            { "server.kill",          "Force-kill the process" },
            { "server.delete",        "Delete the server entirely" },
            { "server.rename",        "Change the server name" },
        }},
        { "Configuration", {
            { "config.view",          "Read server.properties and related configs" },
            { "config.edit",          "Modify server.properties and configs" },
            { "config.ram_port",      "Change RAM allocation and port" },
            { "config.start_command", "Rewrite the JVM launch command" },
            { "config.dns",           "Change the Cloudflare subdomain" },
            { "config.performance",   "Adjust CPU priority + smart optimization" },
        }},
        { "Console", {
            { "console.view",         "Read live console output" },
            { "console.send",         "Send commands to the running server" },
            { "console.dangerous",    "Send dangerous commands (stop, op, ban)" },
        }},
        { "Files", {
            { "files.view",           "Browse the server directory" },
            { "files.read",           "Read file contents" },
            { "files.edit",           "Write / append to files" },
            { "files.delete",         "Delete files" },
            { "files.upload",         "Upload new files" },
            { "files.download",       "Download files" },
        }},
        { "Players", {
            { "players.view",         "See the player list" },
            { "players.kick",         "Kick or ban players" },
            { "players.edit_stats",   "Change player health, hunger, freeze state" },
            { "players.edit_inventory","Add/remove items in inventory or ender chest" },
            { "players.advancements", "Grant or revoke advancements" },
            { "players.reset",        "Wipe a player entirely" },
            { "players.backup",       "Snapshot & restore per-player data" },
        }},
        { "Analytics & scheduling", {
            { "analytics.view",       "View player activity analytics and reports" },
            { "schedule.view",        "View scheduled server tasks and run history" },
            { "schedule.manage",      "Create, edit, enable, or delete scheduled tasks" },
            { "schedule.run",         "Run scheduled tasks immediately" },
        }},
        { "Plugins & Mods", {
            { "plugins.view",         "See installed plugins/mods" },
            { "plugins.install",      "Install plugins/mods from Modrinth" },
            { "plugins.uninstall",    "Remove installed plugins/mods" },
        }},
        { "Backups", {
            { "backups.view",         "Browse the backup archive list" },
            { "backups.create",       "Create new backups" },
            { "backups.restore",      "Restore a backup" },
            { "backups.delete",       "Delete backup archives" },
            { "backups.download",     "Download a backup zip" },
        }},
        { "Automation", {
            { "automation.view",      "Review automation rules and their results" },
            { "automation.manage",    "Create, edit, enable, or delete rules" },
            { "automation.run",       "Run an automation rule immediately" },
        }},
        { "Maintenance", {
            { "maintenance.view",     "Review the current maintenance state" },
            { "maintenance.manage",   "Configure, enable, or disable maintenance" },
        }},
        { "AI Copilot", {
            { "ai.use",               "Chat with the AI in read-only mode" },
            { "ai.agent_mode",        "Enable Agent Mode (file writes, commands)" },
            { "ai.approve",           "Approve confirmation-gated AI actions" },
            { "ai.shell",             "Approve arbitrary shell command execution" },
            { "ai.undo",              "Undo previous AI-caused changes" },
        }},
        { "Team management", {
            { "team.view",            "See other members on this server" },
            { "team.invite",          "Invite new emails" },
            { "team.remove",          "Remove members" },
            { "team.edit_permissions","Change other members' permissions" },
            { "team.transfer",        "Transfer server ownership" },
        }},
        { "Monitoring & audit", {
            { "metrics.view",         "View live CPU/RAM/TPS metrics" },
            { "audit.view",           "Read the audit log" },
            { "tokens.view",          "See AI token usage totals" },
        }},
    };
    return groups;
}

// Default permission set per role.
static nlohmann::json defaultPermissionsForRole(const std::string& role) {
    nlohmann::json p = nlohmann::json::object();
    auto set = [&](const std::string& key, bool val) { p[key] = val; };

    // Start with everything false.
    for (const auto& g : permissionGroups())
        for (const auto& [key, _desc] : g.perms) set(key, false);

    if (role == "owner") {
        for (const auto& g : permissionGroups())
            for (const auto& [key, _desc] : g.perms) set(key, true);
    } else if (role == "admin") {
        for (const auto& g : permissionGroups())
            for (const auto& [key, _desc] : g.perms) set(key, true);
        set("server.delete", false);
        set("team.transfer", false);
        set("ai.shell", false);
    } else if (role == "moderator") {
        // Can look at everything, moderate players + console, no config or file edits.
        set("server.view", true);
        set("server.start", true); set("server.stop", true); set("server.restart", true);
        set("config.view", true);
        set("console.view", true); set("console.send", true);
        set("files.view", true); set("files.read", true);
        set("players.view", true); set("players.kick", true);
        set("analytics.view", true); set("schedule.view", true);
        set("plugins.view", true);
        set("backups.view", true); set("backups.create", true);
        set("automation.view", true);
        set("maintenance.view", true);
        set("ai.use", true);
        set("team.view", true);
        set("metrics.view", true);
    } else if (role == "viewer") {
        set("server.view", true);
        set("config.view", true);
        set("console.view", true);
        set("files.view", true); set("files.read", true);
        set("players.view", true);
        set("analytics.view", true); set("schedule.view", true);
        set("plugins.view", true);
        set("backups.view", true);
        set("automation.view", true);
        set("maintenance.view", true);
        set("ai.use", true);
        set("team.view", true);
        set("metrics.view", true);
    }
    return p;
}

// ============================================================
// Helpers
// ============================================================
static HttpResponsePtr jsonResp(const nlohmann::json& j, HttpStatusCode code = k200OK) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(code);
    resp->setBody(j.dump());
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    return resp;
}

static HttpResponsePtr err(const std::string& msg, HttpStatusCode code = k400BadRequest) {
    return jsonResp({{"status", "error"}, {"message", msg}}, code);
}

static bool looksLikeEmail(const std::string& s) {
    static const std::regex rx(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)");
    return std::regex_match(s, rx);
}

static nlohmann::json memberToJson(const ServerMemberRecord& r) {
    nlohmann::json j;
    j["id"]           = r.id;
    j["server_uuid"]  = r.server_uuid;
    j["email"]        = r.email;
    j["role"]         = r.role;
    j["added_by"]     = r.added_by;
    j["display_name"] = r.display_name;
    j["status"]       = r.status;
    j["created_at"]   = r.created_at;
    j["last_seen_at"] = r.last_seen_at;
    try { j["permissions"] = nlohmann::json::parse(r.permissions_json); }
    catch (...) { j["permissions"] = nlohmann::json::object(); }
    return j;
}

bool MembersController::validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole) {
    (void)req;
    outUsername = "admin";
    outRole = "admin";
    return true;
}

// ============================================================
// GET /api/servers/{uuid}/members
// ============================================================
void MembersController::listMembers(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback,
                                    std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(err("unauthorized", k401Unauthorized));

    ServerRecord srv;
    if (!Database::getInstance().getServer(uuid, srv) || srv.uuid.empty())
        return callback(err("server not found", k404NotFound));

    auto rows = Database::getInstance().listServerMembers(uuid);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) arr.push_back(memberToJson(r));

    callback(jsonResp({{"status", "success"}, {"members", arr}}));
}

// ============================================================
// POST /api/servers/{uuid}/members
// Body: { email, role?, permissions?, display_name? }
// This assigns access only. The recipient owns account creation and sign-in
// through the public website; administrators never set member passwords.
// ============================================================
void MembersController::addMember(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(err("unauthorized", k401Unauthorized));

    ServerRecord srv;
    if (!Database::getInstance().getServer(uuid, srv) || srv.uuid.empty())
        return callback(err("server not found", k404NotFound));

    auto j = req->getJsonObject();
    if (!j) return callback(err("request body must be JSON"));

    std::string email = j->get("email", "").asString();
    if (!looksLikeEmail(email)) return callback(err("invalid email address"));
    std::transform(email.begin(), email.end(), email.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string newRole = j->get("role", "viewer").asString();
    static const std::vector<std::string> validRoles = {"owner","admin","moderator","viewer","custom"};
    if (std::find(validRoles.begin(), validRoles.end(), newRole) == validRoles.end())
        return callback(err("invalid role"));

    // Access assignment and account ownership are intentionally separate.
    // The recipient signs up or uses a verified OAuth email on the public website.

    // Permissions: use whatever the client provided, else derive from role default.
    nlohmann::json perms;
    try {
        if (j->isMember("permissions")) perms = nlohmann::json::parse((*j)["permissions"].toStyledString());
    } catch (...) {}
    if (perms.is_null() || !perms.is_object()) perms = defaultPermissionsForRole(newRole);

    ServerMemberRecord rec;
    rec.server_uuid       = uuid;
    rec.email             = email;
    rec.role              = newRole;
    rec.permissions_json  = perms.dump();
    rec.added_by          = username;
    rec.display_name      = j->get("display_name", "").asString();
    rec.status            = "active";

    if (!Database::getInstance().addServerMember(rec))
        return callback(err("failed to add member (already exists?)", k500InternalServerError));

    Database::getInstance().logAction(username, "MEMBER_ADD", uuid, email + " as " + newRole);

    ServerMemberRecord fresh;
    Database::getInstance().getServerMember(uuid, email, fresh);
    callback(jsonResp({{"status","success"}, {"member", memberToJson(fresh)}}));
}

// ============================================================
// PUT /api/servers/{uuid}/members/{id}
// Body: { role?, permissions?, display_name?, status? }
// ============================================================
void MembersController::updateMember(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     std::string uuid, std::string id) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(err("unauthorized", k401Unauthorized));

    long long memberId = 0;
    try { memberId = std::stoll(id); } catch (...) { return callback(err("invalid member id")); }

    ServerMemberRecord existing;
    if (!Database::getInstance().getServerMemberById(memberId, existing) || existing.server_uuid != uuid)
        return callback(err("member not found", k404NotFound));

    auto j = req->getJsonObject();
    if (!j) return callback(err("request body must be JSON"));

    std::string newRole = j->isMember("role") ? j->get("role", existing.role).asString() : existing.role;
    std::string displayName = j->isMember("display_name") ? j->get("display_name", existing.display_name).asString() : existing.display_name;

    nlohmann::json perms;
    try { perms = nlohmann::json::parse(existing.permissions_json); }
    catch (...) { perms = nlohmann::json::object(); }

    if (j->isMember("permissions")) {
        try {
            auto override_ = nlohmann::json::parse((*j)["permissions"].toStyledString());
            if (override_.is_object()) perms = override_;
        } catch (...) {}
    } else if (j->isMember("role") && newRole != existing.role) {
        // Role changed and no perms provided → use role defaults
        perms = defaultPermissionsForRole(newRole);
    }

    bool ok = Database::getInstance().updateServerMember(memberId, newRole, perms.dump(), displayName);
    if (!ok) return callback(err("update failed", k500InternalServerError));

    if (j->isMember("status")) {
        Database::getInstance().updateServerMemberStatus(memberId, j->get("status", existing.status).asString());
    }

    Database::getInstance().logAction(username, "MEMBER_UPDATE", uuid, existing.email + " role=" + newRole);

    ServerMemberRecord fresh;
    Database::getInstance().getServerMemberById(memberId, fresh);
    callback(jsonResp({{"status","success"}, {"member", memberToJson(fresh)}}));
}

// ============================================================
// DELETE /api/servers/{uuid}/members/{id}
// ============================================================
void MembersController::removeMember(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     std::string uuid, std::string id) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(err("unauthorized", k401Unauthorized));

    long long memberId = 0;
    try { memberId = std::stoll(id); } catch (...) { return callback(err("invalid member id")); }

    ServerMemberRecord existing;
    if (!Database::getInstance().getServerMemberById(memberId, existing) || existing.server_uuid != uuid)
        return callback(err("member not found", k404NotFound));

    if (!Database::getInstance().removeServerMember(memberId))
        return callback(err("delete failed", k500InternalServerError));

    Database::getInstance().logAction(username, "MEMBER_REMOVE", uuid, existing.email);
    callback(jsonResp({{"status","success"}, {"message", "member removed"}}));
}

// ============================================================
// GET /api/user/servers?email=xxx
// Returns all servers the given email has access to. This is the
// endpoint the central hub / web panel calls to build the user's
// "My Servers" list. Meant for hub-side aggregation.
// ============================================================
void MembersController::listMyServers(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string username, role;
    if (!validateJwt(req, username, role)) return callback(err("unauthorized", k401Unauthorized));

    std::string email = req->getParameter("email");
    if (email.empty()) email = username; // fall back to logged-in user

    auto memberships = Database::getInstance().listServersForEmail(email);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : memberships) {
        ServerRecord srv;
        if (!Database::getInstance().getServer(m.server_uuid, srv)) continue;
        nlohmann::json j;
        j["uuid"]           = srv.uuid;
        j["name"]           = srv.name;
        j["software_type"]  = srv.software_type;
        j["version"]        = srv.version;
        j["port"]           = srv.port;
        j["status"]         = srv.status;
        j["subdomain"]      = srv.subdomain;
        j["public_url"]     = srv.subdomain.empty() ? "" : ("https://" + srv.subdomain + ".mcdeploy.online");
        j["your_role"]      = m.role;
        j["your_status"]    = m.status;
        try { j["your_permissions"] = nlohmann::json::parse(m.permissions_json); }
        catch (...) { j["your_permissions"] = nlohmann::json::object(); }
        arr.push_back(j);
    }
    callback(jsonResp({{"status","success"}, {"email", email}, {"servers", arr}}));
}

// ============================================================
// GET /api/members/permission-catalog
// Returns the full permission taxonomy + role presets so the
// frontend can render a permission editor without hardcoding.
// ============================================================
void MembersController::permissionCatalog(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback) {
    (void)req;
    nlohmann::json groups = nlohmann::json::array();
    for (const auto& g : permissionGroups()) {
        nlohmann::json gj;
        gj["label"] = g.label;
        gj["permissions"] = nlohmann::json::array();
        for (const auto& [key, desc] : g.perms) {
            gj["permissions"].push_back({{"key", key}, {"description", desc}});
        }
        groups.push_back(gj);
    }

    nlohmann::json presets;
    for (const auto& r : {"owner","admin","moderator","viewer"}) {
        presets[r] = defaultPermissionsForRole(r);
    }

    callback(jsonResp({
        {"status","success"},
        {"groups", groups},
        {"role_presets", presets}
    }));
}

} // namespace MCDeploy
