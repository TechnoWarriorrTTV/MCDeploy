#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <sqlite3.h>
#include <nlohmann/json.hpp>

namespace MCDeploy {

struct ServerRecord {
    std::string uuid;
    std::string name;
    std::string software_type;
    std::string version;
    int port;
    int ram_min;
    int ram_max;
    std::string status; // "Online", "Offline", "Starting", "Crashed", "Maintenance"
    std::string directory_path;
    std::string start_command;
    std::string created_at;
    std::string subdomain;         // DNS subdomain (e.g. "minto" -> minto.mcdeploy.online)
    std::string dns_a_record_id;   // Cloudflare A record ID (for cleanup)
    std::string dns_srv_record_id; // Cloudflare SRV record ID (for cleanup)
};

struct UserRecord {
    int id;
    std::string username; // Email address for webpanel users; local name for native admins
    std::string password_hash;
    std::string role; // "admin", "moderator", "viewer", "webpanel"
    std::string secret_2fa;
};

struct BackupRecord {
    std::string backup_uuid;
    std::string server_uuid;
    std::string file_name;
    std::string file_path;
    long long file_size;
    std::string created_at;
};

struct PlayerRecord {
    std::string uuid;
    std::string server_uuid;
    std::string username;
    int is_online = 0;
    double health = 20.0;
    int hunger = 20;
    int frozen = 0;
    double last_login_x = 0.0;
    double last_login_y = 64.0;
    double last_login_z = 0.0;
    double last_logoff_x = 0.0;
    double last_logoff_y = 64.0;
    double last_logoff_z = 0.0;
};

struct PlayerItemRecord {
    int id = 0;
    std::string player_uuid;
    std::string type; // "inventory" or "ender_chest"
    int slot = 0;
    std::string item_id;
    int count = 1;
    std::string display_name;
    int unbreakable = 0;
    std::string custom_aura;
    std::string potion_effect;
    std::string enchants = "{}"; // JSON string
};

struct PlayerBackupRecord {
    std::string backup_id;
    std::string player_uuid;
    std::string backup_name;
    std::string inventory_data;
    std::string ender_chest_data;
    std::string stats_data;
    std::string created_at;
};

struct PlayerCoordinateLog {
    int id = 0;
    std::string player_uuid;
    std::string username;
    std::string type; // "login" or "logoff"
    double x = 0.0;
    double y = 64.0;
    double z = 0.0;
    std::string timestamp;
};

// ============================================================
// AI Editor Records
// ============================================================
struct AiConversationRecord {
    long long id = 0;
    std::string server_uuid;
    std::string username;
    std::string role;          // 'user' | 'assistant' | 'tool' | 'system'
    std::string content;       // main text (may be empty for tool_call rows)
    std::string tool_calls;    // JSON array (assistant messages that request tools)
    std::string tool_call_id;  // links tool response back to a call
    std::string tool_name;     // convenience for tool responses
    int tokens_prompt = 0;
    int tokens_completion = 0;
    std::string created_at;
};

struct AiToolCallRecord {
    long long id = 0;
    std::string server_uuid;
    std::string username;
    std::string tool_name;
    std::string arguments;   // JSON
    std::string result;      // JSON or plain text
    std::string status;      // 'success' | 'error' | 'denied'
    int latency_ms = 0;
    std::string created_at;
};

struct AiUndoRecord {
    long long id = 0;
    std::string server_uuid;
    std::string username;
    std::string operation;       // 'write' | 'delete'
    std::string file_path;
    std::string previous_content;// empty if file did not exist
    bool previous_existed = false;
    std::string new_content;
    std::string created_at;
};

struct AiUsageRecord {
    std::string server_uuid;
    std::string username;
    std::string model;
    long long tokens_prompt = 0;
    long long tokens_completion = 0;
    long long request_count = 0;
};

// ============================================================
// Scheduled Tasks
// ============================================================
struct ScheduledTaskRecord {
    long long id = 0;
    std::string server_uuid;
    std::string name;
    // action: "console" | "start" | "stop" | "restart" | "backup" | "ai_prompt"
    std::string action_type;
    // Payload is action-specific: console command text, or AI prompt text.
    // For start/stop/restart/backup it's ignored.
    std::string payload;
    // schedule_kind: "interval" | "daily" | "cron"
    std::string schedule_kind;
    // schedule_value:
    //   interval -> integer seconds ("300" = every 5 min)
    //   daily    -> "HH:MM"     (local time)
    //   cron     -> 5-field cron "min hour dom month dow"
    std::string schedule_value;
    int enabled = 1;
    std::string next_run_at;   // ISO datetime (UTC via SQLite datetime())
    std::string last_run_at;
    std::string last_status;   // "success" | "error" | "skipped" | ""
    std::string last_output;   // short human-readable
    std::string created_by;
    std::string created_at;
};

struct ScheduledTaskRunRecord {
    long long id = 0;
    long long task_id = 0;
    std::string started_at;
    std::string finished_at;
    std::string status;        // "success" | "error" | "skipped"
    std::string output;        // truncated
};

// ============================================================
// AI Automation Rules
// ============================================================
struct AutomationRuleRecord {
    long long id = 0;
    std::string server_uuid;
    std::string name;
    // cpu_above | ram_above | disk_below | server_offline | log_contains
    std::string trigger_type;
    double threshold = 0.0;
    std::string condition_value; // text used by log_contains
    // console | start | stop | restart | backup | ai_prompt
    std::string action_type;
    std::string action_payload;
    int enabled = 1;
    int cooldown_seconds = 300;
    std::string last_evaluated_at;
    std::string last_triggered_at;
    std::string last_status;
    std::string last_output;
    std::string created_by;
    std::string created_at;
};

// ============================================================
// Maintenance Mode
// ============================================================
struct MaintenanceRecord {
    std::string server_uuid;
    int enabled = 0;
    std::string message = "Server maintenance is in progress.";
    int prevent_joins = 1;
    int backup_on_enable = 1;
    int whitelist_was_enabled = 0;
    std::string enabled_by;
    std::string enabled_at;
    std::string updated_at;
};

// ============================================================
// Player Analytics
// ============================================================
struct PlayerEventRecord {
    long long id = 0;
    std::string server_uuid;
    std::string player_uuid;   // may be empty when unknown
    std::string username;
    // "join" | "leave" | "chat" | "death"
    std::string event_type;
    std::string payload;       // chat text, death cause, etc.
    std::string created_at;    // UTC
};

struct PlayerSessionRecord {
    long long id = 0;
    std::string server_uuid;
    std::string player_uuid;
    std::string username;
    std::string joined_at;
    std::string left_at;       // empty = session still open
    long long duration_seconds = 0;
    double join_x = 0.0, join_y = 64.0, join_z = 0.0;
    std::string status;        // "active" | "ended" | "abandoned"
};

// ============================================================
// Team / Membership
// ============================================================
struct ServerMemberRecord {
    long long id = 0;
    std::string server_uuid;
    std::string email;
    std::string role;              // "owner" | "admin" | "moderator" | "viewer" | "custom"
    std::string permissions_json;  // JSON object with granular booleans
    std::string added_by;
    std::string display_name;      // optional pretty name shown in the members list
    std::string status;            // "active" | "pending" | "suspended"
    std::string created_at;
    std::string last_seen_at;
};

class Database {
public:
    static Database& getInstance();

    bool initialize(const std::string& dbPath);
    void close();

    // Server queries
    bool createServer(const ServerRecord& server);
    bool updateServerStatus(const std::string& uuid, const std::string& status);
    bool updateServerConfig(const std::string& uuid, int ramMin, int ramMax, int port);
    bool updateServerStartCommand(const std::string& uuid, const std::string& startCommand);
    bool deleteServer(const std::string& uuid);
    std::vector<ServerRecord> getAllServers();
    bool getServer(const std::string& uuid, ServerRecord& outServer);

    // DNS subdomain queries
    bool isSubdomainTaken(const std::string& subdomain);
    bool updateDnsRecordIds(const std::string& uuid, const std::string& aRecordId, const std::string& srvRecordId);
    bool clearDnsRecordIds(const std::string& uuid);

    // User authentication queries
    bool createUser(const std::string& username, const std::string& passwordPlain, const std::string& role);
    bool userExists(const std::string& username);
    bool getUser(const std::string& username, UserRecord& outUser);
    bool verifyUser(const std::string& username, const std::string& passwordPlain, UserRecord& outUser);
    bool updateUserRole(const std::string& username, const std::string& newRole);

    // External identities and short-lived OAuth callback exchanges.
    bool linkOAuthIdentity(const std::string& provider, const std::string& subject,
                           const std::string& email, const std::string& displayName,
                           const std::string& avatarUrl);
    bool getOAuthIdentityEmail(const std::string& provider, const std::string& subject,
                               std::string& outEmail);
    bool createOAuthLoginCode(const std::string& codeHash, const std::string& email);
    bool consumeOAuthLoginCode(const std::string& codeHash, std::string& outEmail);

    // Manual webpanel signup remains pending until a short-lived email code is verified.
    bool createPendingRegistration(const std::string& email, const std::string& passwordPlain,
                                   const std::string& displayName, const std::string& codeHash);
    bool rotatePendingVerification(const std::string& email, const std::string& codeHash);
    bool completePendingRegistration(const std::string& email, const std::string& codeHash);
    bool pendingRegistrationExists(const std::string& email);
    bool deletePendingRegistration(const std::string& email);

    // Backup log queries
    bool addBackup(const BackupRecord& backup);
    std::vector<BackupRecord> getServerBackups(const std::string& serverUuid);
    bool deleteBackup(const std::string& backupUuid);

    // Audit logs
    bool logAction(const std::string& username, const std::string& action, const std::string& serverUuid, const std::string& details);
    nlohmann::json getAuditLogs(int limit = 50);
    nlohmann::json getServerAuditLogs(const std::string& serverUuid, int limit = 50);

    // Player Manager queries
    std::vector<PlayerRecord> getServerPlayers(const std::string& serverUuid);
    bool getOrCreatePlayer(const std::string& serverUuid, const std::string& username, PlayerRecord& outPlayer);
    bool getPlayer(const std::string& serverUuid, const std::string& username, PlayerRecord& outPlayer);
    bool updatePlayerStats(const std::string& serverUuid, const std::string& username, double health, int hunger, int frozen);
    bool setPlayerOnline(const std::string& serverUuid, const std::string& username, int isOnline);
    bool logPlayerCoordinates(const std::string& serverUuid, const std::string& username, const std::string& type, double x, double y, double z);
    std::vector<PlayerCoordinateLog> getPlayerCoordinateLogs(const std::string& serverUuid);

    // Inventory queries
    std::vector<PlayerItemRecord> getPlayerItems(const std::string& playerUuid);
    bool updatePlayerItem(const PlayerItemRecord& item);
    bool deletePlayerItem(int itemId);
    bool clearPlayerItems(const std::string& playerUuid, const std::string& type); // Wipes inventory or ender_chest
    bool resetPlayerEntirely(const std::string& serverUuid, const std::string& username);

    // Advancements queries
    nlohmann::json getPlayerAdvancements(const std::string& playerUuid);
    bool updatePlayerAdvancement(const std::string& playerUuid, const std::string& advancementId, int granted);

    // Backup & Restore
    bool createPlayerBackup(const std::string& playerUuid, const std::string& backupName);
    std::vector<PlayerBackupRecord> getPlayerBackups(const std::string& playerUuid);
    bool restorePlayerBackup(const std::string& playerUuid, const std::string& backupId);

    // ============================================================
    // AI Editor
    // ============================================================
    bool appendAiConversation(const AiConversationRecord& msg);
    std::vector<AiConversationRecord> getAiConversation(const std::string& serverUuid,
                                                        const std::string& username,
                                                        int limit = 200);
    bool clearAiConversation(const std::string& serverUuid, const std::string& username);

    bool logAiToolCall(const AiToolCallRecord& call);
    std::vector<AiToolCallRecord> getRecentAiToolCalls(const std::string& serverUuid, int limit = 50);

    bool pushAiUndo(const AiUndoRecord& u);
    bool popAiUndo(const std::string& serverUuid, const std::string& username, AiUndoRecord& out);
    int  countAiUndoStack(const std::string& serverUuid, const std::string& username);

    bool recordAiUsage(const std::string& serverUuid, const std::string& username,
                       const std::string& model, long long promptTokens, long long completionTokens);
    AiUsageRecord getAiUsageTotals(const std::string& serverUuid, const std::string& username);

    // Simple sliding-window rate limit backed by DB; returns true if allowed.
    bool aiRateLimitAllow(const std::string& username, int maxPerMinute, int maxPerDay);

    // ============================================================
    // Team / Membership
    // ============================================================
    bool addServerMember(const ServerMemberRecord& m);
    bool updateServerMember(long long id, const std::string& role, const std::string& permissionsJson, const std::string& displayName);
    bool updateServerMemberStatus(long long id, const std::string& status);
    bool removeServerMember(long long id);
    bool removeServerMemberByEmail(const std::string& serverUuid, const std::string& email);
    std::vector<ServerMemberRecord> listServerMembers(const std::string& serverUuid);
    bool getServerMember(const std::string& serverUuid, const std::string& email, ServerMemberRecord& out);
    bool getServerMemberById(long long id, ServerMemberRecord& out);
    std::vector<ServerMemberRecord> listServersForEmail(const std::string& email);
    void touchMemberLastSeen(const std::string& serverUuid, const std::string& email);

    // ============================================================
    // Scheduled Tasks
    // ============================================================
    long long createScheduledTask(const ScheduledTaskRecord& t);
    bool      updateScheduledTask(long long id, const ScheduledTaskRecord& t);
    bool      deleteScheduledTask(long long id);
    bool      setScheduledTaskEnabled(long long id, bool enabled);
    bool      setScheduledTaskNextRun(long long id, const std::string& nextRunAt);
    bool      recordScheduledTaskResult(long long id, const std::string& status,
                                        const std::string& output, const std::string& nextRunAt);
    bool      getScheduledTask(long long id, ScheduledTaskRecord& out);
    std::vector<ScheduledTaskRecord> listScheduledTasks(const std::string& serverUuid);
    // Returns tasks whose next_run_at is <= now and are enabled and their server exists.
    std::vector<ScheduledTaskRecord> listDueScheduledTasks();
    // Run history for a single task.
    std::vector<ScheduledTaskRunRecord> listScheduledTaskRuns(long long taskId, int limit = 50);
    long long insertScheduledTaskRun(const ScheduledTaskRunRecord& r);

    // ============================================================
    // AI Automation Rules
    // ============================================================
    long long createAutomationRule(const AutomationRuleRecord& r);
    bool updateAutomationRule(long long id, const AutomationRuleRecord& r);
    bool deleteAutomationRule(long long id);
    bool setAutomationRuleEnabled(long long id, bool enabled);
    bool getAutomationRule(long long id, AutomationRuleRecord& out);
    std::vector<AutomationRuleRecord> listAutomationRules(const std::string& serverUuid);
    std::vector<AutomationRuleRecord> listRunnableAutomationRules();
    bool recordAutomationEvaluation(long long id, bool triggered,
                                    const std::string& status, const std::string& output);

    // ============================================================
    // Maintenance Mode
    // ============================================================
    bool getMaintenance(const std::string& serverUuid, MaintenanceRecord& out);
    bool upsertMaintenance(const MaintenanceRecord& r);

    // ============================================================
    // Player Analytics
    // ============================================================
    bool logPlayerEvent(const PlayerEventRecord& ev);
    // Opens a new session; if a prior session for this player is still open,
    // it gets closed with status = "abandoned".
    long long startPlayerSession(const std::string& serverUuid,
                                 const std::string& playerUuid,
                                 const std::string& username,
                                 double x, double y, double z);
    // Closes the open session for this player, computing duration_seconds.
    bool endPlayerSession(const std::string& serverUuid, const std::string& username);
    // Close every open session for a server (used on server (re)start).
    void closeOpenSessionsForServer(const std::string& serverUuid);
    // Analytics aggregations. `days` is a lookback window; use 0 for "all time".
    nlohmann::json getAnalyticsSummary   (const std::string& serverUuid, int days);
    nlohmann::json getAnalyticsHourly    (const std::string& serverUuid, int days);
    nlohmann::json getAnalyticsDaily     (const std::string& serverUuid, int days);
    nlohmann::json getAnalyticsLeaderboard(const std::string& serverUuid, int days, int limit);
    std::vector<PlayerEventRecord> getPlayerEvents(const std::string& serverUuid,
                                                   const std::string& eventType,
                                                   int limit);

private:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    sqlite3* m_db = nullptr;
    std::mutex m_mutex;
    std::string m_dbPath;

    bool executeQuery(const std::string& sql);
    std::string hashPassword(const std::string& password);
    bool verifyPassword(const std::string& password, const std::string& hash);
    bool createUserInternal(const std::string& username, const std::string& passwordPlain, const std::string& role);
};

} // namespace MCDeploy
