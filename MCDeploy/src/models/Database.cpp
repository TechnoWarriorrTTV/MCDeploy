#include "Database.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace MCDeploy {

Database& Database::getInstance() {
    static Database instance;
    return instance;
}

Database::~Database() {
    close();
}

void Database::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool Database::executeQuery(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "[MCDeploy DB Error]: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

void seedMockPlayersInternal(sqlite3* db, const std::string& serverUuid);

bool Database::initialize(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dbPath = dbPath;
    
    int rc = sqlite3_open(m_dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        std::cerr << "[MCDeploy Database] Cannot open database: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    // Enable foreign keys
    executeQuery("PRAGMA foreign_keys = ON;");

    // Create Tables
    std::string createUsersTable = 
        "CREATE TABLE IF NOT EXISTS mcdeploy_users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL,"
        "password_hash TEXT NOT NULL,"
        "role TEXT NOT NULL,"
        "secret_2fa TEXT"
        ");";

    std::string createOAuthIdentitiesTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_oauth_identities ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "provider TEXT NOT NULL,"
        "provider_subject TEXT NOT NULL,"
        "email TEXT NOT NULL,"
        "display_name TEXT DEFAULT '',"
        "avatar_url TEXT DEFAULT '',"
        "linked_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "last_login_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "UNIQUE(provider, provider_subject)"
        ");";

    std::string createOAuthCodesTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_oauth_login_codes ("
        "code_hash TEXT PRIMARY KEY,"
        "email TEXT NOT NULL,"
        "expires_at TEXT NOT NULL,"
        "used_at TEXT DEFAULT ''"
        ");";

    std::string createPendingRegistrationsTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_pending_registrations ("
        "email TEXT PRIMARY KEY,"
        "password_hash TEXT NOT NULL,"
        "display_name TEXT DEFAULT '',"
        "code_hash TEXT NOT NULL,"
        "expires_at TEXT NOT NULL,"
        "attempts INTEGER NOT NULL DEFAULT 0,"
        "last_sent_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "created_at TEXT NOT NULL DEFAULT (datetime('now'))"
        ");";

    std::string createServersTable = 
        "CREATE TABLE IF NOT EXISTS mcdeploy_servers ("
        "uuid TEXT PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "software_type TEXT NOT NULL,"
        "version TEXT NOT NULL,"
        "port INTEGER NOT NULL,"
        "ram_min INTEGER NOT NULL,"
        "ram_max INTEGER NOT NULL,"
        "status TEXT NOT NULL,"
        "directory_path TEXT NOT NULL,"
        "start_command TEXT NOT NULL,"
        "created_at TEXT NOT NULL,"
        "subdomain TEXT DEFAULT '',"
        "dns_a_record_id TEXT DEFAULT '',"
        "dns_srv_record_id TEXT DEFAULT ''"
        ");";

    std::string createBackupsTable = 
        "CREATE TABLE IF NOT EXISTS mcdeploy_backups ("
        "backup_uuid TEXT PRIMARY KEY,"
        "server_uuid TEXT NOT NULL,"
        "file_name TEXT NOT NULL,"
        "file_path TEXT NOT NULL,"
        "file_size INTEGER NOT NULL,"
        "created_at TEXT NOT NULL,"
        "FOREIGN KEY (server_uuid) REFERENCES mcdeploy_servers(uuid) ON DELETE CASCADE"
        ");";

    std::string createAuditLogTable = 
        "CREATE TABLE IF NOT EXISTS mcdeploy_audit_log ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT NOT NULL,"
        "action TEXT NOT NULL,"
        "server_uuid TEXT,"
        "details TEXT,"
        "created_at TEXT NOT NULL"
        ");";

    std::string createPlayersTable = 
        "CREATE TABLE IF NOT EXISTS mcdeploy_players ("
        "uuid TEXT PRIMARY KEY,"
        "server_uuid TEXT NOT NULL,"
        "username TEXT NOT NULL,"
        "is_online INTEGER NOT NULL DEFAULT 0,"
        "health REAL NOT NULL DEFAULT 20.0,"
        "hunger INTEGER NOT NULL DEFAULT 20,"
        "frozen INTEGER NOT NULL DEFAULT 0,"
        "last_login_x REAL NOT NULL DEFAULT 0.0,"
        "last_login_y REAL NOT NULL DEFAULT 64.0,"
        "last_login_z REAL NOT NULL DEFAULT 0.0,"
        "last_logoff_x REAL NOT NULL DEFAULT 0.0,"
        "last_logoff_y REAL NOT NULL DEFAULT 64.0,"
        "last_logoff_z REAL NOT NULL DEFAULT 0.0,"
        "FOREIGN KEY(server_uuid) REFERENCES mcdeploy_servers(uuid) ON DELETE CASCADE"
        ");";

    std::string createPlayerInventoryTable = 
        "CREATE TABLE IF NOT EXISTS mcdeploy_player_inventory ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "player_uuid TEXT NOT NULL,"
        "type TEXT NOT NULL,"
        "slot INTEGER NOT NULL,"
        "item_id TEXT NOT NULL,"
        "count INTEGER NOT NULL DEFAULT 1,"
        "display_name TEXT,"
        "unbreakable INTEGER NOT NULL DEFAULT 0,"
        "custom_aura TEXT,"
        "potion_effect TEXT,"
        "enchants TEXT NOT NULL DEFAULT '{}',"
        "FOREIGN KEY(player_uuid) REFERENCES mcdeploy_players(uuid) ON DELETE CASCADE"
        ");";

    std::string createPlayerBackupsTable = 
        "CREATE TABLE IF NOT EXISTS mcdeploy_player_backups ("
        "backup_id TEXT PRIMARY KEY,"
        "player_uuid TEXT NOT NULL,"
        "backup_name TEXT NOT NULL,"
        "inventory_data TEXT NOT NULL,"
        "ender_chest_data TEXT NOT NULL,"
        "stats_data TEXT NOT NULL,"
        "created_at TEXT NOT NULL,"
        "FOREIGN KEY(player_uuid) REFERENCES mcdeploy_players(uuid) ON DELETE CASCADE"
        ");";

    std::string createPlayerAdvancementsTable = 
        "CREATE TABLE IF NOT EXISTS mcdeploy_player_advancements ("
        "player_uuid TEXT NOT NULL,"
        "advancement_id TEXT NOT NULL,"
        "granted INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(player_uuid, advancement_id),"
        "FOREIGN KEY(player_uuid) REFERENCES mcdeploy_players(uuid) ON DELETE CASCADE"
        ");";

    std::string createPlayerCoordinateLogsTable = 
        "CREATE TABLE IF NOT EXISTS mcdeploy_player_coordinate_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "player_uuid TEXT NOT NULL,"
        "username TEXT NOT NULL,"
        "type TEXT NOT NULL,"
        "x REAL NOT NULL,"
        "y REAL NOT NULL,"
        "z REAL NOT NULL,"
        "timestamp TEXT NOT NULL,"
        "FOREIGN KEY(player_uuid) REFERENCES mcdeploy_players(uuid) ON DELETE CASCADE"
        ");";

    // AI Editor tables
    std::string createAiConvTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_ai_conversations ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_uuid TEXT NOT NULL,"
        "username TEXT NOT NULL,"
        "role TEXT NOT NULL,"
        "content TEXT,"
        "tool_calls TEXT,"
        "tool_call_id TEXT,"
        "tool_name TEXT,"
        "tokens_prompt INTEGER DEFAULT 0,"
        "tokens_completion INTEGER DEFAULT 0,"
        "created_at TEXT NOT NULL"
        ");";

    std::string createAiToolCallsTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_ai_tool_calls ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_uuid TEXT NOT NULL,"
        "username TEXT NOT NULL,"
        "tool_name TEXT NOT NULL,"
        "arguments TEXT,"
        "result TEXT,"
        "status TEXT NOT NULL,"
        "latency_ms INTEGER DEFAULT 0,"
        "created_at TEXT NOT NULL"
        ");";

    std::string createAiUndoTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_ai_undo ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_uuid TEXT NOT NULL,"
        "username TEXT NOT NULL,"
        "operation TEXT NOT NULL,"
        "file_path TEXT NOT NULL,"
        "previous_content TEXT,"
        "previous_existed INTEGER DEFAULT 0,"
        "new_content TEXT,"
        "created_at TEXT NOT NULL"
        ");";

    std::string createAiUsageTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_ai_usage ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_uuid TEXT NOT NULL,"
        "username TEXT NOT NULL,"
        "model TEXT NOT NULL,"
        "tokens_prompt INTEGER DEFAULT 0,"
        "tokens_completion INTEGER DEFAULT 0,"
        "created_at TEXT NOT NULL"
        ");";

    std::string createAiRateTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_ai_rate_events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT NOT NULL,"
        "created_at TEXT NOT NULL"
        ");";

    std::string createScheduledTasksTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_scheduled_tasks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_uuid TEXT NOT NULL,"
        "name TEXT NOT NULL,"
        "action_type TEXT NOT NULL,"
        "payload TEXT DEFAULT '',"
        "schedule_kind TEXT NOT NULL,"
        "schedule_value TEXT NOT NULL,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "next_run_at TEXT,"
        "last_run_at TEXT DEFAULT '',"
        "last_status TEXT DEFAULT '',"
        "last_output TEXT DEFAULT '',"
        "created_by TEXT DEFAULT '',"
        "created_at TEXT NOT NULL,"
        "FOREIGN KEY(server_uuid) REFERENCES mcdeploy_servers(uuid) ON DELETE CASCADE"
        ");";

    std::string createScheduledTaskRunsTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_scheduled_task_runs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "task_id INTEGER NOT NULL,"
        "started_at TEXT NOT NULL,"
        "finished_at TEXT DEFAULT '',"
        "status TEXT NOT NULL,"
        "output TEXT DEFAULT '',"
        "FOREIGN KEY(task_id) REFERENCES mcdeploy_scheduled_tasks(id) ON DELETE CASCADE"
        ");";

    std::string createAutomationRulesTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_automation_rules ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_uuid TEXT NOT NULL,"
        "name TEXT NOT NULL,"
        "trigger_type TEXT NOT NULL,"
        "threshold REAL NOT NULL DEFAULT 0,"
        "condition_value TEXT DEFAULT '',"
        "action_type TEXT NOT NULL,"
        "action_payload TEXT DEFAULT '',"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "cooldown_seconds INTEGER NOT NULL DEFAULT 300,"
        "last_evaluated_at TEXT DEFAULT '',"
        "last_triggered_at TEXT DEFAULT '',"
        "last_status TEXT DEFAULT '',"
        "last_output TEXT DEFAULT '',"
        "created_by TEXT DEFAULT '',"
        "created_at TEXT NOT NULL,"
        "FOREIGN KEY(server_uuid) REFERENCES mcdeploy_servers(uuid) ON DELETE CASCADE"
        ");";

    std::string createMaintenanceTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_maintenance ("
        "server_uuid TEXT PRIMARY KEY,"
        "enabled INTEGER NOT NULL DEFAULT 0,"
        "message TEXT NOT NULL DEFAULT 'Server maintenance is in progress.',"
        "prevent_joins INTEGER NOT NULL DEFAULT 1,"
        "backup_on_enable INTEGER NOT NULL DEFAULT 1,"
        "whitelist_was_enabled INTEGER NOT NULL DEFAULT 0,"
        "enabled_by TEXT DEFAULT '',"
        "enabled_at TEXT DEFAULT '',"
        "updated_at TEXT NOT NULL,"
        "FOREIGN KEY(server_uuid) REFERENCES mcdeploy_servers(uuid) ON DELETE CASCADE"
        ");";

    std::string createPlayerEventsTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_player_events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_uuid TEXT NOT NULL,"
        "player_uuid TEXT DEFAULT '',"
        "username TEXT NOT NULL,"
        "event_type TEXT NOT NULL,"
        "payload TEXT DEFAULT '',"
        "created_at TEXT NOT NULL"
        ");";

    std::string createPlayerSessionsTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_player_sessions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_uuid TEXT NOT NULL,"
        "player_uuid TEXT DEFAULT '',"
        "username TEXT NOT NULL,"
        "joined_at TEXT NOT NULL,"
        "left_at TEXT DEFAULT '',"
        "duration_seconds INTEGER DEFAULT 0,"
        "join_x REAL DEFAULT 0.0,"
        "join_y REAL DEFAULT 64.0,"
        "join_z REAL DEFAULT 0.0,"
        "status TEXT NOT NULL DEFAULT 'active'"
        ");";

    std::string createMembersTable =
        "CREATE TABLE IF NOT EXISTS mcdeploy_server_members ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_uuid TEXT NOT NULL,"
        "email TEXT NOT NULL,"
        "role TEXT NOT NULL DEFAULT 'viewer',"
        "permissions_json TEXT NOT NULL DEFAULT '{}',"
        "added_by TEXT DEFAULT '',"
        "display_name TEXT DEFAULT '',"
        "status TEXT DEFAULT 'active',"
        "created_at TEXT NOT NULL,"
        "last_seen_at TEXT DEFAULT '',"
        "UNIQUE(server_uuid, email),"
        "FOREIGN KEY(server_uuid) REFERENCES mcdeploy_servers(uuid) ON DELETE CASCADE"
        ");";

    if (!executeQuery(createUsersTable) ||
        !executeQuery(createOAuthIdentitiesTable) ||
        !executeQuery(createOAuthCodesTable) ||
        !executeQuery(createPendingRegistrationsTable) || 
        !executeQuery(createServersTable) || 
        !executeQuery(createBackupsTable) || 
        !executeQuery(createAuditLogTable) ||
        !executeQuery(createPlayersTable) ||
        !executeQuery(createPlayerInventoryTable) ||
        !executeQuery(createPlayerBackupsTable) ||
        !executeQuery(createPlayerAdvancementsTable) ||
        !executeQuery(createPlayerCoordinateLogsTable) ||
        !executeQuery(createAiConvTable) ||
        !executeQuery(createAiToolCallsTable) ||
        !executeQuery(createAiUndoTable) ||
        !executeQuery(createAiUsageTable) ||
        !executeQuery(createAiRateTable) ||
        !executeQuery(createScheduledTasksTable) ||
        !executeQuery(createScheduledTaskRunsTable) ||
        !executeQuery(createAutomationRulesTable) ||
        !executeQuery(createMaintenanceTable) ||
        !executeQuery(createPlayerEventsTable) ||
        !executeQuery(createPlayerSessionsTable) ||
        !executeQuery(createMembersTable)) {
        return false;
    }

    // Helpful indexes for AI queries
    executeQuery("CREATE INDEX IF NOT EXISTS idx_ai_conv_lookup ON mcdeploy_ai_conversations(server_uuid, username, id);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_ai_undo_lookup ON mcdeploy_ai_undo(server_uuid, username, id);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_ai_rate_user ON mcdeploy_ai_rate_events(username, created_at);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_ai_usage_lookup ON mcdeploy_ai_usage(server_uuid, username);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_oauth_email ON mcdeploy_oauth_identities(email);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_oauth_codes_expiry ON mcdeploy_oauth_login_codes(expires_at);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_pending_registration_expiry ON mcdeploy_pending_registrations(expires_at);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_members_server ON mcdeploy_server_members(server_uuid);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_members_email ON mcdeploy_server_members(email);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_sched_server ON mcdeploy_scheduled_tasks(server_uuid);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_sched_due    ON mcdeploy_scheduled_tasks(enabled, next_run_at);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_sched_runs   ON mcdeploy_scheduled_task_runs(task_id, id);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_auto_server  ON mcdeploy_automation_rules(server_uuid, id);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_auto_due     ON mcdeploy_automation_rules(enabled, last_triggered_at);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_evt_server   ON mcdeploy_player_events(server_uuid, created_at);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_evt_type     ON mcdeploy_player_events(server_uuid, event_type, created_at);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_sess_server  ON mcdeploy_player_sessions(server_uuid, joined_at);");
    executeQuery("CREATE INDEX IF NOT EXISTS idx_sess_open    ON mcdeploy_player_sessions(server_uuid, username, status);");

    // Migration: Add DNS columns to existing servers table if missing
    executeQuery("ALTER TABLE mcdeploy_servers ADD COLUMN subdomain TEXT DEFAULT '';");
    executeQuery("ALTER TABLE mcdeploy_servers ADD COLUMN dns_a_record_id TEXT DEFAULT '';");
    executeQuery("ALTER TABLE mcdeploy_servers ADD COLUMN dns_srv_record_id TEXT DEFAULT '';");
    executeQuery("CREATE UNIQUE INDEX IF NOT EXISTS idx_mcdeploy_servers_subdomain "
                 "ON mcdeploy_servers(subdomain COLLATE NOCASE) WHERE subdomain <> '';");

    // Seed mock players
    seedMockPlayersInternal(m_db, "04ba9235-8d1f-498d-e05d-ad172fd72ff8");

    // Check if an admin user exists; if not, create default admin: admin/admin123
    std::string checkAdmin = "SELECT COUNT(*) FROM mcdeploy_users WHERE username='admin';";
    sqlite3_stmt* stmt = nullptr;
    bool adminExists = false;
    if (sqlite3_prepare_v2(m_db, checkAdmin.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            adminExists = sqlite3_column_int(stmt, 0) > 0;
        }
        sqlite3_finalize(stmt);
    }

    if (!adminExists) {
        std::cout << "[MCDeploy Database] Default admin user not found. Creating default admin (admin/admin123)..." << std::endl;
        createUserInternal("admin", "admin123", "admin");
    }

    return true;
}

static std::string hexEncode(const unsigned char* bytes, std::size_t length) {
    std::ostringstream output;
    for (std::size_t i = 0; i < length; ++i)
        output << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]);
    return output.str();
}

static bool hexDecode(const std::string& value, std::vector<unsigned char>& output) {
    if (value.empty() || value.size() % 2 != 0) return false;
    output.clear();
    output.reserve(value.size() / 2);
    try {
        for (std::size_t i = 0; i < value.size(); i += 2)
            output.push_back(static_cast<unsigned char>(std::stoul(value.substr(i, 2), nullptr, 16)));
    } catch (...) {
        output.clear();
        return false;
    }
    return true;
}

static std::string legacySha256(const std::string& password) {
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int length = 0;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) return {};
    EVP_DigestInit_ex(context, EVP_sha256(), nullptr);
    EVP_DigestUpdate(context, password.data(), password.size());
    EVP_DigestFinal_ex(context, digest, &length);
    EVP_MD_CTX_free(context);
    return hexEncode(digest, length);
}

std::string Database::hashPassword(const std::string& password) {
    constexpr int iterations = 210000;
    unsigned char salt[16] = {};
    unsigned char derived[32] = {};
    if (RAND_bytes(salt, sizeof(salt)) != 1 ||
        PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt, sizeof(salt),
                          iterations, EVP_sha256(), sizeof(derived), derived) != 1)
        return {};
    return "pbkdf2_sha256$" + std::to_string(iterations) + "$" + hexEncode(salt, sizeof(salt)) +
           "$" + hexEncode(derived, sizeof(derived));
}

bool Database::verifyPassword(const std::string& password, const std::string& hash) {
    constexpr const char* prefix = "pbkdf2_sha256$";
    if (hash.rfind(prefix, 0) != 0) {
        const std::string legacy = legacySha256(password);
        return legacy.size() == hash.size() && !legacy.empty() &&
               CRYPTO_memcmp(legacy.data(), hash.data(), hash.size()) == 0;
    }

    const auto iterationsEnd = hash.find('$', std::char_traits<char>::length(prefix));
    const auto saltEnd = hash.find('$', iterationsEnd == std::string::npos ? iterationsEnd : iterationsEnd + 1);
    if (iterationsEnd == std::string::npos || saltEnd == std::string::npos) return false;
    int iterations = 0;
    try { iterations = std::stoi(hash.substr(std::char_traits<char>::length(prefix),
                                              iterationsEnd - std::char_traits<char>::length(prefix))); }
    catch (...) { return false; }
    if (iterations < 100000 || iterations > 1000000) return false;

    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected;
    if (!hexDecode(hash.substr(iterationsEnd + 1, saltEnd - iterationsEnd - 1), salt) ||
        !hexDecode(hash.substr(saltEnd + 1), expected) || salt.size() < 16 || expected.size() != 32)
        return false;
    std::vector<unsigned char> actual(expected.size());
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                          static_cast<int>(salt.size()), iterations, EVP_sha256(),
                          static_cast<int>(actual.size()), actual.data()) != 1)
        return false;
    return CRYPTO_memcmp(actual.data(), expected.data(), expected.size()) == 0;
}

bool Database::createUser(const std::string& username, const std::string& passwordPlain, const std::string& role) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return createUserInternal(username, passwordPlain, role);
}

bool Database::userExists(const std::string& username) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "SELECT 1 FROM mcdeploy_users WHERE lower(username) = lower(?) LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

bool Database::getUser(const std::string& username, UserRecord& outUser) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "SELECT id, username, password_hash, role FROM mcdeploy_users WHERE lower(username)=lower(?) LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    if (found) {
        outUser.id = sqlite3_column_int(stmt, 0);
        outUser.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        outUser.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        outUser.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::linkOAuthIdentity(const std::string& provider, const std::string& subject,
                                 const std::string& email, const std::string& displayName,
                                 const std::string& avatarUrl) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* existingSql = "SELECT email FROM mcdeploy_oauth_identities WHERE provider=? AND provider_subject=?;";
    sqlite3_stmt* existing = nullptr;
    if (sqlite3_prepare_v2(m_db, existingSql, -1, &existing, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(existing, 1, provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(existing, 2, subject.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(existing) == SQLITE_ROW) {
        const auto stored = reinterpret_cast<const char*>(sqlite3_column_text(existing, 0));
        if (!stored || email != stored) {
            sqlite3_finalize(existing);
            return false;
        }
    }
    sqlite3_finalize(existing);

    const char* sql =
        "INSERT INTO mcdeploy_oauth_identities(provider,provider_subject,email,display_name,avatar_url) "
        "VALUES(?,?,?,?,?) ON CONFLICT(provider,provider_subject) DO UPDATE SET "
        "display_name=excluded.display_name,avatar_url=excluded.avatar_url,last_login_at=datetime('now');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, subject.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, avatarUrl.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::getOAuthIdentityEmail(const std::string& provider, const std::string& subject,
                                     std::string& outEmail) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "SELECT email FROM mcdeploy_oauth_identities WHERE provider=? AND provider_subject=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, subject.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    if (found) outEmail = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    return found;
}

bool Database::createOAuthLoginCode(const std::string& codeHash, const std::string& email) {
    std::lock_guard<std::mutex> lock(m_mutex);
    executeQuery("DELETE FROM mcdeploy_oauth_login_codes WHERE expires_at <= datetime('now') OR used_at <> ''; ");
    const char* sql = "INSERT INTO mcdeploy_oauth_login_codes(code_hash,email,expires_at) VALUES(?,?,datetime('now','+5 minutes'));";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, codeHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::consumeOAuthLoginCode(const std::string& codeHash, std::string& outEmail) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* selectSql =
        "SELECT email FROM mcdeploy_oauth_login_codes WHERE code_hash=? AND used_at='' AND expires_at>datetime('now');";
    sqlite3_stmt* select = nullptr;
    if (sqlite3_prepare_v2(m_db, selectSql, -1, &select, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(select, 1, codeHash.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(select) == SQLITE_ROW;
    if (found) outEmail = reinterpret_cast<const char*>(sqlite3_column_text(select, 0));
    sqlite3_finalize(select);
    if (!found) return false;

    const char* updateSql = "UPDATE mcdeploy_oauth_login_codes SET used_at=datetime('now') WHERE code_hash=? AND used_at='';";
    sqlite3_stmt* update = nullptr;
    if (sqlite3_prepare_v2(m_db, updateSql, -1, &update, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(update, 1, codeHash.c_str(), -1, SQLITE_TRANSIENT);
    const bool updated = sqlite3_step(update) == SQLITE_DONE && sqlite3_changes(m_db) == 1;
    sqlite3_finalize(update);
    return updated;
}

bool Database::createPendingRegistration(const std::string& email, const std::string& passwordPlain,
                                         const std::string& displayName, const std::string& codeHash) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string passwordHash = hashPassword(passwordPlain);
    if (passwordHash.empty()) return false;

    sqlite3_stmt* throttle = nullptr;
    const char* throttleSql =
        "SELECT 1 FROM mcdeploy_pending_registrations WHERE email=? AND last_sent_at>datetime('now','-60 seconds');";
    if (sqlite3_prepare_v2(m_db, throttleSql, -1, &throttle, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(throttle, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    const bool tooSoon = sqlite3_step(throttle) == SQLITE_ROW;
    sqlite3_finalize(throttle);
    if (tooSoon) return false;

    const char* sql =
        "INSERT INTO mcdeploy_pending_registrations(email,password_hash,display_name,code_hash,expires_at) "
        "VALUES(?,?,?,?,datetime('now','+10 minutes')) "
        "ON CONFLICT(email) DO UPDATE SET password_hash=excluded.password_hash,"
        "display_name=excluded.display_name,code_hash=excluded.code_hash,"
        "expires_at=excluded.expires_at,attempts=0,last_sent_at=datetime('now');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, passwordHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, codeHash.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::rotatePendingVerification(const std::string& email, const std::string& codeHash) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "UPDATE mcdeploy_pending_registrations SET code_hash=?,expires_at=datetime('now','+10 minutes'),"
        "attempts=0,last_sent_at=datetime('now') WHERE email=? "
        "AND last_sent_at<=datetime('now','-60 seconds');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, codeHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(m_db) == 1;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::pendingRegistrationExists(const std::string& email) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "SELECT 1 FROM mcdeploy_pending_registrations WHERE email=? AND expires_at>datetime('now') LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

bool Database::deletePendingRegistration(const std::string& email) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "DELETE FROM mcdeploy_pending_registrations WHERE email=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::completePendingRegistration(const std::string& email, const std::string& codeHash) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* selectSql =
        "SELECT password_hash FROM mcdeploy_pending_registrations WHERE email=? AND code_hash=? "
        "AND expires_at>datetime('now') AND attempts<6;";
    sqlite3_stmt* select = nullptr;
    if (sqlite3_prepare_v2(m_db, selectSql, -1, &select, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(select, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(select, 2, codeHash.c_str(), -1, SQLITE_TRANSIENT);
    const bool matched = sqlite3_step(select) == SQLITE_ROW;
    std::string passwordHash;
    if (matched) passwordHash = reinterpret_cast<const char*>(sqlite3_column_text(select, 0));
    sqlite3_finalize(select);
    if (!matched) {
        const char* attemptSql = "UPDATE mcdeploy_pending_registrations SET attempts=attempts+1 WHERE email=?;";
        sqlite3_stmt* attempt = nullptr;
        if (sqlite3_prepare_v2(m_db, attemptSql, -1, &attempt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(attempt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(attempt);
            sqlite3_finalize(attempt);
        }
        return false;
    }

    if (!executeQuery("BEGIN IMMEDIATE;")) return false;
    const char* insertSql = "INSERT INTO mcdeploy_users(username,password_hash,role) VALUES(?,?,'webpanel');";
    sqlite3_stmt* insert = nullptr;
    bool ok = sqlite3_prepare_v2(m_db, insertSql, -1, &insert, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_text(insert, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert, 2, passwordHash.c_str(), -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(insert) == SQLITE_DONE;
    }
    if (insert) sqlite3_finalize(insert);
    if (ok) {
        const char* deleteSql = "DELETE FROM mcdeploy_pending_registrations WHERE email=?;";
        sqlite3_stmt* remove = nullptr;
        ok = sqlite3_prepare_v2(m_db, deleteSql, -1, &remove, nullptr) == SQLITE_OK;
        if (ok) {
            sqlite3_bind_text(remove, 1, email.c_str(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(remove) == SQLITE_DONE;
        }
        if (remove) sqlite3_finalize(remove);
    }
    executeQuery(ok ? "COMMIT;" : "ROLLBACK;");
    return ok;
}

bool Database::createUserInternal(const std::string& username, const std::string& passwordPlain, const std::string& role) {
    std::string sql = "INSERT OR IGNORE INTO mcdeploy_users (username, password_hash, role) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    std::string pwdHash = hashPassword(passwordPlain);
    if (pwdHash.empty()) {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pwdHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, role.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    const bool inserted = rc == SQLITE_DONE && sqlite3_changes(m_db) == 1;
    sqlite3_finalize(stmt);
    return inserted;
}

bool Database::verifyUser(const std::string& username, const std::string& passwordPlain, UserRecord& outUser) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "SELECT id, username, password_hash, role FROM mcdeploy_users WHERE lower(username) = lower(?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    bool verified = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        std::string dbUsername = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string dbHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        std::string dbRole = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

        if (verifyPassword(passwordPlain, dbHash)) {
            outUser.id = id;
            outUser.username = dbUsername;
            outUser.password_hash = dbHash;
            outUser.role = dbRole;
            verified = true;
        }
    }
    sqlite3_finalize(stmt);
    return verified;
}

bool Database::createServer(const ServerRecord& server) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = 
        "INSERT INTO mcdeploy_servers (uuid, name, software_type, version, port, ram_min, ram_max, status, directory_path, start_command, created_at, subdomain, dns_a_record_id, dns_srv_record_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, server.uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, server.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, server.software_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, server.version.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, server.port);
    sqlite3_bind_int(stmt, 6, server.ram_min);
    sqlite3_bind_int(stmt, 7, server.ram_max);
    sqlite3_bind_text(stmt, 8, server.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, server.directory_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, server.start_command.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, server.created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, server.subdomain.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, server.dns_a_record_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, server.dns_srv_record_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::updateServerStatus(const std::string& uuid, const std::string& status) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "UPDATE mcdeploy_servers SET status = ? WHERE uuid = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, uuid.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::updateServerConfig(const std::string& uuid, int ramMin, int ramMax, int port) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "UPDATE mcdeploy_servers SET ram_min = ?, ram_max = ?, port = ? WHERE uuid = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, ramMin);
    sqlite3_bind_int(stmt, 2, ramMax);
    sqlite3_bind_int(stmt, 3, port);
    sqlite3_bind_text(stmt, 4, uuid.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::updateServerStartCommand(const std::string& uuid, const std::string& startCommand) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "UPDATE mcdeploy_servers SET start_command = ? WHERE uuid = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, startCommand.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, uuid.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::deleteServer(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "DELETE FROM mcdeploy_servers WHERE uuid = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<ServerRecord> Database::getAllServers() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ServerRecord> list;
    std::string sql = "SELECT uuid, name, software_type, version, port, ram_min, ram_max, status, directory_path, start_command, created_at, COALESCE(subdomain,''), COALESCE(dns_a_record_id,''), COALESCE(dns_srv_record_id,'') FROM mcdeploy_servers;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ServerRecord s;
            s.uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            s.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            s.software_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            s.version = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            s.port = sqlite3_column_int(stmt, 4);
            s.ram_min = sqlite3_column_int(stmt, 5);
            s.ram_max = sqlite3_column_int(stmt, 6);
            s.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            s.directory_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            s.start_command = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            s.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
            const char* sub = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
            s.subdomain = sub ? sub : "";
            const char* aId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
            s.dns_a_record_id = aId ? aId : "";
            const char* srvId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13));
            s.dns_srv_record_id = srvId ? srvId : "";
            list.push_back(s);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

bool Database::getServer(const std::string& uuid, ServerRecord& outServer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "SELECT uuid, name, software_type, version, port, ram_min, ram_max, status, directory_path, start_command, created_at, COALESCE(subdomain,''), COALESCE(dns_a_record_id,''), COALESCE(dns_srv_record_id,'') FROM mcdeploy_servers WHERE uuid = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        outServer.uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        outServer.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        outServer.software_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        outServer.version = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        outServer.port = sqlite3_column_int(stmt, 4);
        outServer.ram_min = sqlite3_column_int(stmt, 5);
        outServer.ram_max = sqlite3_column_int(stmt, 6);
        outServer.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        outServer.directory_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        outServer.start_command = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        outServer.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        const char* sub = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        outServer.subdomain = sub ? sub : "";
        const char* aId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
        outServer.dns_a_record_id = aId ? aId : "";
        const char* srvId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13));
        outServer.dns_srv_record_id = srvId ? srvId : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::isSubdomainTaken(const std::string& subdomain) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "SELECT COUNT(*) FROM mcdeploy_servers WHERE subdomain = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, subdomain.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count > 0;
}

bool Database::updateDnsRecordIds(const std::string& uuid, const std::string& aRecordId, const std::string& srvRecordId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "UPDATE mcdeploy_servers SET dns_a_record_id = ?, dns_srv_record_id = ? WHERE uuid = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, aRecordId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, srvRecordId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, uuid.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::clearDnsRecordIds(const std::string& uuid) {
    return updateDnsRecordIds(uuid, "", "");
}

bool Database::addBackup(const BackupRecord& backup) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "INSERT INTO mcdeploy_backups (backup_uuid, server_uuid, file_name, file_path, file_size, created_at) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, backup.backup_uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, backup.server_uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, backup.file_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, backup.file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, backup.file_size);
    sqlite3_bind_text(stmt, 6, backup.created_at.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<BackupRecord> Database::getServerBackups(const std::string& serverUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<BackupRecord> list;
    std::string sql = "SELECT backup_uuid, server_uuid, file_name, file_path, file_size, created_at FROM mcdeploy_backups WHERE server_uuid = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BackupRecord b;
            b.backup_uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            b.server_uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            b.file_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            b.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            b.file_size = sqlite3_column_int64(stmt, 4);
            b.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            list.push_back(b);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

bool Database::deleteBackup(const std::string& backupUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "DELETE FROM mcdeploy_backups WHERE backup_uuid = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, backupUuid.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::logAction(const std::string& username, const std::string& action, const std::string& serverUuid, const std::string& details) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "INSERT INTO mcdeploy_audit_log (username, action, server_uuid, details, created_at) VALUES (?, ?, ?, ?, datetime('now'));";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, action.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, details.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

nlohmann::json Database::getAuditLogs(int limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json logs = nlohmann::json::array();
    std::string sql = "SELECT username, action, server_uuid, details, created_at FROM mcdeploy_audit_log ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            nlohmann::json item;
            item["username"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            item["action"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* sUuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            item["server_uuid"] = sUuid ? sUuid : "";
            const char* details = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            item["details"] = details ? details : "";
            item["created_at"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            logs.push_back(item);
        }
        sqlite3_finalize(stmt);
    }
    return logs;
}

nlohmann::json Database::getServerAuditLogs(const std::string& serverUuid, int limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json logs = nlohmann::json::array();
    const char* sql =
        "SELECT username, action, server_uuid, details, created_at "
        "FROM mcdeploy_audit_log WHERE server_uuid=? ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return logs;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, std::max(1, std::min(limit, 200)));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto text = [&](int column) {
            const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
            return value ? std::string(value) : std::string();
        };
        logs.push_back({
            {"username", text(0)}, {"action", text(1)}, {"server_uuid", text(2)},
            {"details", text(3)}, {"created_at", text(4)}
        });
    }
    sqlite3_finalize(stmt);
    return logs;
}

// -----------------------------------------------------------------------------
// PLAYER MANAGER DATABASE QUERIES & MOCK SEEDING IMPLEMENTATIONS
// -----------------------------------------------------------------------------

static std::string getCurrentTimeStr() {
    auto now = std::chrono::system_clock::now();
    auto time_val = std::chrono::system_clock::to_time_t(now);
    std::tm bt;
#ifdef _WIN32
    localtime_s(&bt, &time_val);
#else
    localtime_r(&time_val, &bt);
#endif
    std::stringstream ss;
    ss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void seedMockPlayersInternal(sqlite3* db, const std::string& serverUuid) {
    // Check if mock players already exist for this server
    std::string checkSql = "SELECT COUNT(*) FROM mcdeploy_players WHERE server_uuid = '" + serverUuid + "' AND username = 'Steve';";
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db, checkSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    if (count > 0) return; // Already seeded

    std::cout << "[MCDeploy Database] Seeding mock players for server " << serverUuid << std::endl;

    std::string steveUuid = "steve-uuid-" + serverUuid;
    std::string alexUuid = "alex-uuid-" + serverUuid;
    std::string notchUuid = "notch-uuid-" + serverUuid;

    std::string sql1 = "INSERT INTO mcdeploy_players (uuid, server_uuid, username, is_online, health, hunger, frozen, last_login_x, last_login_y, last_login_z) "
                       "VALUES (?, ?, 'Steve', 1, 20.0, 20, 0, 100.5, 64.0, -250.2);";
    std::string sql2 = "INSERT INTO mcdeploy_players (uuid, server_uuid, username, is_online, health, hunger, frozen, last_login_x, last_login_y, last_login_z, last_logoff_x, last_logoff_y, last_logoff_z) "
                       "VALUES (?, ?, 'Alex', 0, 18.5, 15, 0, -45.2, 72.0, 89.1, -48.0, 72.0, 92.5);";
    std::string sql3 = "INSERT INTO mcdeploy_players (uuid, server_uuid, username, is_online, health, hunger, frozen, last_login_x, last_login_y, last_login_z, last_logoff_x, last_logoff_y, last_logoff_z) "
                       "VALUES (?, ?, 'Notch', 0, 20.0, 20, 0, 0.0, 80.0, 0.0, 5.0, 80.0, -5.0);";

    auto executeBind = [&](const std::string& sql, const std::string& pUuid) {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(s, 1, pUuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(s);
            sqlite3_finalize(s);
        }
    };
    executeBind(sql1, steveUuid);
    executeBind(sql2, alexUuid);
    executeBind(sql3, notchUuid);

    auto insertItem = [&](const std::string& playerUuid, const std::string& type, int slot, const std::string& itemId, int count, const std::string& name, int unbreakable, const std::string& aura, const std::string& potion, const std::string& enchants) {
        std::string isql = "INSERT INTO mcdeploy_player_inventory (player_uuid, type, slot, item_id, count, display_name, unbreakable, custom_aura, potion_effect, enchants) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db, isql.c_str(), -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(s, 1, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(s, 3, slot);
            sqlite3_bind_text(s, 4, itemId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(s, 5, count);
            if (name.empty()) sqlite3_bind_null(s, 6);
            else sqlite3_bind_text(s, 6, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(s, 7, unbreakable);
            if (aura.empty()) sqlite3_bind_null(s, 8);
            else sqlite3_bind_text(s, 8, aura.c_str(), -1, SQLITE_TRANSIENT);
            if (potion.empty()) sqlite3_bind_null(s, 9);
            else sqlite3_bind_text(s, 9, potion.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 10, enchants.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(s);
            sqlite3_finalize(s);
        }
    };

    insertItem(steveUuid, "inventory", 0, "minecraft:diamond_sword", 1, "Excalibur", 1, "Blue Glow", "", "{\"minecraft:sharpness\":5,\"minecraft:unbreaking\":3}");
    insertItem(steveUuid, "inventory", 1, "minecraft:bread", 64, "", 0, "", "", "{}");
    insertItem(steveUuid, "inventory", 2, "minecraft:potion", 1, "Super Healing Potion", 0, "Red Sparkles", "minecraft:instant_health:2", "{}");
    insertItem(steveUuid, "inventory", 36, "minecraft:iron_helmet", 1, "", 0, "", "", "{}");
    insertItem(steveUuid, "ender_chest", 0, "minecraft:notch_apple", 8, "God Apple", 0, "Golden Aura", "", "{}");

    insertItem(alexUuid, "inventory", 0, "minecraft:bow", 1, "Sniper Bow", 0, "", "", "{\"minecraft:power\":4,\"minecraft:infinity\":1}");
    insertItem(alexUuid, "inventory", 1, "minecraft:arrow", 64, "", 0, "", "", "{}");

    insertItem(notchUuid, "inventory", 0, "minecraft:netherite_axe", 1, "World Editor", 1, "Rainbow Aura", "", "{\"minecraft:efficiency\":5}");

    auto insertAdv = [&](const std::string& playerUuid, const std::string& advId, int granted) {
        std::string asql = "INSERT OR IGNORE INTO mcdeploy_player_advancements (player_uuid, advancement_id, granted) VALUES (?, ?, ?);";
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db, asql.c_str(), -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(s, 1, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, advId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(s, 3, granted);
            sqlite3_step(s);
            sqlite3_finalize(s);
        }
    };
    std::vector<std::string> advs = {
        "story/mine_stone", "story/upgrade_tools", "story/smelt_iron",
        "story/obtain_armor", "story/lava_bucket", "story/iron_tools",
        "story/defend_castle", "story/mine_diamond", "story/enchant_item"
    };
    for (const auto& a : advs) {
        insertAdv(steveUuid, a, 1);
        insertAdv(alexUuid, a, a == "story/mine_stone" ? 1 : 0);
        insertAdv(notchUuid, a, 1);
    }

    auto insertLog = [&](const std::string& playerUuid, const std::string& username, const std::string& type, double x, double y, double z, const std::string& time) {
        std::string lsql = "INSERT INTO mcdeploy_player_coordinate_logs (player_uuid, username, type, x, y, z, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db, lsql.c_str(), -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(s, 1, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, username.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 3, type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(s, 4, x);
            sqlite3_bind_double(s, 5, y);
            sqlite3_bind_double(s, 6, z);
            sqlite3_bind_text(s, 7, time.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(s);
            sqlite3_finalize(s);
        }
    };
    insertLog(steveUuid, "Steve", "login", 100.5, 64.0, -250.2, "2026-06-22 20:01:05");
    insertLog(alexUuid, "Alex", "login", -45.2, 72.0, 89.1, "2026-06-22 19:43:10");
    insertLog(alexUuid, "Alex", "logoff", -48.0, 72.0, 92.5, "2026-06-22 19:58:30");
}

std::vector<PlayerRecord> Database::getServerPlayers(const std::string& serverUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    seedMockPlayersInternal(m_db, serverUuid);
    std::vector<PlayerRecord> list;
    std::string sql = "SELECT uuid, server_uuid, username, is_online, health, hunger, frozen, last_login_x, last_login_y, last_login_z, last_logoff_x, last_logoff_y, last_logoff_z "
                      "FROM mcdeploy_players WHERE server_uuid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PlayerRecord p;
            p.uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            p.server_uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            p.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            p.is_online = sqlite3_column_int(stmt, 3);
            p.health = sqlite3_column_double(stmt, 4);
            p.hunger = sqlite3_column_int(stmt, 5);
            p.frozen = sqlite3_column_int(stmt, 6);
            p.last_login_x = sqlite3_column_double(stmt, 7);
            p.last_login_y = sqlite3_column_double(stmt, 8);
            p.last_login_z = sqlite3_column_double(stmt, 9);
            p.last_logoff_x = sqlite3_column_double(stmt, 10);
            p.last_logoff_y = sqlite3_column_double(stmt, 11);
            p.last_logoff_z = sqlite3_column_double(stmt, 12);
            list.push_back(p);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

bool Database::getPlayer(const std::string& serverUuid, const std::string& username, PlayerRecord& outPlayer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "SELECT uuid, server_uuid, username, is_online, health, hunger, frozen, last_login_x, last_login_y, last_login_z, last_logoff_x, last_logoff_y, last_logoff_z "
                      "FROM mcdeploy_players WHERE server_uuid = ? AND username = ?;";
    sqlite3_stmt* stmt = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            outPlayer.uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            outPlayer.server_uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            outPlayer.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            outPlayer.is_online = sqlite3_column_int(stmt, 3);
            outPlayer.health = sqlite3_column_double(stmt, 4);
            outPlayer.hunger = sqlite3_column_int(stmt, 5);
            outPlayer.frozen = sqlite3_column_int(stmt, 6);
            outPlayer.last_login_x = sqlite3_column_double(stmt, 7);
            outPlayer.last_login_y = sqlite3_column_double(stmt, 8);
            outPlayer.last_login_z = sqlite3_column_double(stmt, 9);
            outPlayer.last_logoff_x = sqlite3_column_double(stmt, 10);
            outPlayer.last_logoff_y = sqlite3_column_double(stmt, 11);
            outPlayer.last_logoff_z = sqlite3_column_double(stmt, 12);
            found = true;
        }
        sqlite3_finalize(stmt);
    }
    return found;
}

bool Database::getOrCreatePlayer(const std::string& serverUuid, const std::string& username, PlayerRecord& outPlayer) {
    // Try Select
    if (getPlayer(serverUuid, username, outPlayer)) {
        return true;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    std::string uuid = username + "-uuid-" + serverUuid;
    std::string sqlInsert = "INSERT INTO mcdeploy_players (uuid, server_uuid, username, is_online, health, hunger, frozen) VALUES (?, ?, ?, 0, 20.0, 20, 0);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sqlInsert.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, username.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc == SQLITE_DONE) {
            outPlayer.uuid = uuid;
            outPlayer.server_uuid = serverUuid;
            outPlayer.username = username;
            outPlayer.is_online = 0;
            outPlayer.health = 20.0;
            outPlayer.hunger = 20;
            outPlayer.frozen = 0;
            outPlayer.last_login_x = 0.0;
            outPlayer.last_login_y = 64.0;
            outPlayer.last_login_z = 0.0;
            outPlayer.last_logoff_x = 0.0;
            outPlayer.last_logoff_y = 64.0;
            outPlayer.last_logoff_z = 0.0;

            // Seed default advancements
            std::vector<std::string> advs = {
                "story/mine_stone", "story/upgrade_tools", "story/smelt_iron",
                "story/obtain_armor", "story/lava_bucket", "story/iron_tools",
                "story/defend_castle", "story/mine_diamond", "story/enchant_item"
            };
            for (const auto& a : advs) {
                std::string asql = "INSERT OR IGNORE INTO mcdeploy_player_advancements (player_uuid, advancement_id, granted) VALUES (?, ?, 0);";
                sqlite3_stmt* s = nullptr;
                if (sqlite3_prepare_v2(m_db, asql.c_str(), -1, &s, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(s, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(s, 2, a.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(s);
                    sqlite3_finalize(s);
                }
            }
            return true;
        }
    }
    return false;
}

bool Database::updatePlayerStats(const std::string& serverUuid, const std::string& username, double health, int hunger, int frozen) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "UPDATE mcdeploy_players SET health = ?, hunger = ?, frozen = ? WHERE server_uuid = ? AND username = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_double(stmt, 1, health);
        sqlite3_bind_int(stmt, 2, hunger);
        sqlite3_bind_int(stmt, 3, frozen);
        sqlite3_bind_text(stmt, 4, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, username.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
    return false;
}

bool Database::setPlayerOnline(const std::string& serverUuid, const std::string& username, int isOnline) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "UPDATE mcdeploy_players SET is_online = ? WHERE server_uuid = ? AND username = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, isOnline);
        sqlite3_bind_text(stmt, 2, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, username.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
    return false;
}

bool Database::logPlayerCoordinates(const std::string& serverUuid, const std::string& username, const std::string& type, double x, double y, double z) {
    PlayerRecord p;
    if (!getOrCreatePlayer(serverUuid, username, p)) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 1. Update player coordinates
    std::string updSql;
    if (type == "login") {
        updSql = "UPDATE mcdeploy_players SET is_online = 1, last_login_x = ?, last_login_y = ?, last_login_z = ? WHERE uuid = ?;";
    } else {
        updSql = "UPDATE mcdeploy_players SET is_online = 0, last_logoff_x = ?, last_logoff_y = ?, last_logoff_z = ? WHERE uuid = ?;";
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, updSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_double(stmt, 1, x);
        sqlite3_bind_double(stmt, 2, y);
        sqlite3_bind_double(stmt, 3, z);
        sqlite3_bind_text(stmt, 4, p.uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // 2. Add coordinates log entry
    std::string timestamp = getCurrentTimeStr();
    std::string logSql = "INSERT INTO mcdeploy_player_coordinate_logs (player_uuid, username, type, x, y, z, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(m_db, logSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, p.uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, x);
        sqlite3_bind_double(stmt, 5, y);
        sqlite3_bind_double(stmt, 6, z);
        sqlite3_bind_text(stmt, 7, timestamp.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
    return false;
}

std::vector<PlayerCoordinateLog> Database::getPlayerCoordinateLogs(const std::string& serverUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlayerCoordinateLog> list;
    std::string sql = "SELECT l.id, l.player_uuid, l.username, l.type, l.x, l.y, l.z, l.timestamp "
                      "FROM mcdeploy_player_coordinate_logs l "
                      "JOIN mcdeploy_players p ON p.uuid = l.player_uuid "
                      "WHERE p.server_uuid = ? ORDER BY l.id DESC LIMIT 50;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PlayerCoordinateLog l;
            l.id = sqlite3_column_int(stmt, 0);
            l.player_uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            l.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            l.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            l.x = sqlite3_column_double(stmt, 4);
            l.y = sqlite3_column_double(stmt, 5);
            l.z = sqlite3_column_double(stmt, 6);
            l.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            list.push_back(l);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

std::vector<PlayerItemRecord> Database::getPlayerItems(const std::string& playerUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlayerItemRecord> list;
    std::string sql = "SELECT id, player_uuid, type, slot, item_id, count, display_name, unbreakable, custom_aura, potion_effect, enchants "
                      "FROM mcdeploy_player_inventory WHERE player_uuid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PlayerItemRecord item;
            item.id = sqlite3_column_int(stmt, 0);
            item.player_uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            item.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            item.slot = sqlite3_column_int(stmt, 3);
            item.item_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            item.count = sqlite3_column_int(stmt, 5);
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            item.display_name = name ? name : "";
            item.unbreakable = sqlite3_column_int(stmt, 7);
            const char* aura = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            item.custom_aura = aura ? aura : "";
            const char* pot = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            item.potion_effect = pot ? pot : "";
            item.enchants = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
            list.push_back(item);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

bool Database::updatePlayerItem(const PlayerItemRecord& item) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Check if slot already occupied
    std::string checkSql = "SELECT id FROM mcdeploy_player_inventory WHERE player_uuid = ? AND type = ? AND slot = ?;";
    sqlite3_stmt* stmt = nullptr;
    int existingId = 0;
    if (sqlite3_prepare_v2(m_db, checkSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, item.player_uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, item.type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, item.slot);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            existingId = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    std::string sql;
    if (existingId > 0) {
        sql = "UPDATE mcdeploy_player_inventory SET item_id = ?, count = ?, display_name = ?, unbreakable = ?, custom_aura = ?, potion_effect = ?, enchants = ? WHERE id = ?;";
    } else {
        sql = "INSERT INTO mcdeploy_player_inventory (item_id, count, display_name, unbreakable, custom_aura, potion_effect, enchants, player_uuid, type, slot) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    }

    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, item.item_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, item.count);
        if (item.display_name.empty()) sqlite3_bind_null(stmt, 3);
        else sqlite3_bind_text(stmt, 3, item.display_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, item.unbreakable);
        if (item.custom_aura.empty()) sqlite3_bind_null(stmt, 5);
        else sqlite3_bind_text(stmt, 5, item.custom_aura.c_str(), -1, SQLITE_TRANSIENT);
        if (item.potion_effect.empty()) sqlite3_bind_null(stmt, 6);
        else sqlite3_bind_text(stmt, 6, item.potion_effect.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, item.enchants.c_str(), -1, SQLITE_TRANSIENT);

        if (existingId > 0) {
            sqlite3_bind_int(stmt, 8, existingId);
        } else {
            sqlite3_bind_text(stmt, 8, item.player_uuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 9, item.type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 10, item.slot);
        }

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
    return false;
}

bool Database::deletePlayerItem(int itemId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "DELETE FROM mcdeploy_player_inventory WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, itemId);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
    return false;
}

bool Database::clearPlayerItems(const std::string& playerUuid, const std::string& type) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "DELETE FROM mcdeploy_player_inventory WHERE player_uuid = ? AND type = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
    return false;
}

bool Database::resetPlayerEntirely(const std::string& serverUuid, const std::string& username) {
    PlayerRecord p;
    if (!getPlayer(serverUuid, username, p)) return false;

    clearPlayerItems(p.uuid, "inventory");
    clearPlayerItems(p.uuid, "ender_chest");

    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Reset stats
    std::string statsSql = "UPDATE mcdeploy_players SET health = 20.0, hunger = 20, frozen = 0 WHERE uuid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, statsSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, p.uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Reset advancements
    std::string advSql = "UPDATE mcdeploy_player_advancements SET granted = 0 WHERE player_uuid = ?;";
    if (sqlite3_prepare_v2(m_db, advSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, p.uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return true;
}

nlohmann::json Database::getPlayerAdvancements(const std::string& playerUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json list = nlohmann::json::array();
    std::string sql = "SELECT advancement_id, granted FROM mcdeploy_player_advancements WHERE player_uuid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            nlohmann::json item;
            item["advancement_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            item["granted"] = sqlite3_column_int(stmt, 1);
            list.push_back(item);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

bool Database::updatePlayerAdvancement(const std::string& playerUuid, const std::string& advancementId, int granted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "INSERT OR REPLACE INTO mcdeploy_player_advancements (player_uuid, advancement_id, granted) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, advancementId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, granted);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
    return false;
}

bool Database::createPlayerBackup(const std::string& playerUuid, const std::string& backupName) {
    // 1. Get stats
    std::string statsJson = "{}";
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string sql = "SELECT health, hunger, frozen, last_login_x, last_login_y, last_login_z FROM mcdeploy_players WHERE uuid = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                nlohmann::json j;
                j["health"] = sqlite3_column_double(stmt, 0);
                j["hunger"] = sqlite3_column_int(stmt, 1);
                j["frozen"] = sqlite3_column_int(stmt, 2);
                j["x"] = sqlite3_column_double(stmt, 3);
                j["y"] = sqlite3_column_double(stmt, 4);
                j["z"] = sqlite3_column_double(stmt, 5);
                statsJson = j.dump();
            }
            sqlite3_finalize(stmt);
        }
    }

    // 2. Get items
    auto items = getPlayerItems(playerUuid);
    nlohmann::json invArr = nlohmann::json::array();
    nlohmann::json endArr = nlohmann::json::array();
    for (const auto& item : items) {
        nlohmann::json j;
        j["slot"] = item.slot;
        j["item_id"] = item.item_id;
        j["count"] = item.count;
        j["display_name"] = item.display_name;
        j["unbreakable"] = item.unbreakable;
        j["custom_aura"] = item.custom_aura;
        j["potion_effect"] = item.potion_effect;
        j["enchants"] = nlohmann::json::parse(item.enchants);

        if (item.type == "inventory") {
            invArr.push_back(j);
        } else {
            endArr.push_back(j);
        }
    }

    // 3. Write backup
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string backupId = "pb-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::string timestamp = getCurrentTimeStr();
    std::string sql = "INSERT INTO mcdeploy_player_backups (backup_id, player_uuid, backup_name, inventory_data, ender_chest_data, stats_data, created_at) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, backupId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, backupName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, invArr.dump().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, endArr.dump().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, statsJson.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, timestamp.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
    return false;
}

std::vector<PlayerBackupRecord> Database::getPlayerBackups(const std::string& playerUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlayerBackupRecord> list;
    std::string sql = "SELECT backup_id, player_uuid, backup_name, inventory_data, ender_chest_data, stats_data, created_at FROM mcdeploy_player_backups WHERE player_uuid = ? ORDER BY created_at DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PlayerBackupRecord b;
            b.backup_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            b.player_uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            b.backup_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            b.inventory_data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            b.ender_chest_data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            b.stats_data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            b.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            list.push_back(b);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

bool Database::restorePlayerBackup(const std::string& playerUuid, const std::string& backupId) {
    // 1. Fetch backup details
    std::string invJson, endJson, statsJson;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string sql = "SELECT inventory_data, ender_chest_data, stats_data FROM mcdeploy_player_backups WHERE backup_id = ? AND player_uuid = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, backupId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                invJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                endJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                statsJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            }
            sqlite3_finalize(stmt);
        }
    }

    if (invJson.empty()) return false;

    // 2. Restore stats
    try {
        auto sj = nlohmann::json::parse(statsJson);
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string sql = "UPDATE mcdeploy_players SET health = ?, hunger = ?, frozen = ?, last_login_x = ?, last_login_y = ?, last_login_z = ? WHERE uuid = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_double(stmt, 1, sj.value("health", 20.0));
            sqlite3_bind_int(stmt, 2, sj.value("hunger", 20));
            sqlite3_bind_int(stmt, 3, sj.value("frozen", 0));
            sqlite3_bind_double(stmt, 4, sj.value("x", 0.0));
            sqlite3_bind_double(stmt, 5, sj.value("y", 64.0));
            sqlite3_bind_double(stmt, 6, sj.value("z", 0.0));
            sqlite3_bind_text(stmt, 7, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } catch (...) {}

    // 3. Restore inventory items
    clearPlayerItems(playerUuid, "inventory");
    clearPlayerItems(playerUuid, "ender_chest");

    auto restoreItemsArray = [&](const std::string& jsonText, const std::string& type) {
        try {
            auto arr = nlohmann::json::parse(jsonText);
            for (const auto& j : arr) {
                PlayerItemRecord item;
                item.player_uuid = playerUuid;
                item.type = type;
                item.slot = j.value("slot", 0);
                item.item_id = j.value("item_id", "minecraft:air");
                item.count = j.value("count", 1);
                item.display_name = j.value("display_name", "");
                item.unbreakable = j.value("unbreakable", 0);
                item.custom_aura = j.value("custom_aura", "");
                item.potion_effect = j.value("potion_effect", "");
                item.enchants = j.value("enchants", nlohmann::json::object()).dump();
                updatePlayerItem(item);
            }
        } catch (...) {}
    };

    restoreItemsArray(invJson, "inventory");
    restoreItemsArray(endJson, "ender_chest");

    return true;
}

// ============================================================
// AI Editor: conversation persistence
// ============================================================
bool Database::appendAiConversation(const AiConversationRecord& msg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "INSERT INTO mcdeploy_ai_conversations "
        "(server_uuid, username, role, content, tool_calls, tool_call_id, tool_name, tokens_prompt, tokens_completion, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'));";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, msg.server_uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, msg.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, msg.role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, msg.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, msg.tool_calls.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, msg.tool_call_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, msg.tool_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, msg.tokens_prompt);
    sqlite3_bind_int(stmt, 9, msg.tokens_completion);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<AiConversationRecord> Database::getAiConversation(const std::string& serverUuid,
                                                              const std::string& username,
                                                              int limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AiConversationRecord> out;
    const char* sql =
        "SELECT id, server_uuid, username, role, content, "
        "COALESCE(tool_calls,''), COALESCE(tool_call_id,''), COALESCE(tool_name,''), "
        "tokens_prompt, tokens_completion, created_at "
        "FROM mcdeploy_ai_conversations "
        "WHERE server_uuid = ? AND username = ? "
        "ORDER BY id ASC "
        "LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AiConversationRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.server_uuid    = (const char*)sqlite3_column_text(stmt, 1);
        r.username       = (const char*)sqlite3_column_text(stmt, 2);
        r.role           = (const char*)sqlite3_column_text(stmt, 3);
        auto c = sqlite3_column_text(stmt, 4);
        r.content        = c ? (const char*)c : "";
        r.tool_calls     = (const char*)sqlite3_column_text(stmt, 5);
        r.tool_call_id   = (const char*)sqlite3_column_text(stmt, 6);
        r.tool_name      = (const char*)sqlite3_column_text(stmt, 7);
        r.tokens_prompt      = sqlite3_column_int(stmt, 8);
        r.tokens_completion  = sqlite3_column_int(stmt, 9);
        r.created_at     = (const char*)sqlite3_column_text(stmt, 10);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool Database::clearAiConversation(const std::string& serverUuid, const std::string& username) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "DELETE FROM mcdeploy_ai_conversations WHERE server_uuid = ? AND username = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

// ============================================================
// AI Editor: tool-call audit trail
// ============================================================
bool Database::logAiToolCall(const AiToolCallRecord& call) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "INSERT INTO mcdeploy_ai_tool_calls "
        "(server_uuid, username, tool_name, arguments, result, status, latency_ms, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now'));";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, call.server_uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, call.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, call.tool_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, call.arguments.c_str(), -1, SQLITE_TRANSIENT);
    // Truncate result to 32KB to avoid ballooning the DB
    std::string trimmedResult = call.result.size() > 32768
        ? call.result.substr(0, 32768) + "... [truncated]"
        : call.result;
    sqlite3_bind_text(stmt, 5, trimmedResult.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, call.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, call.latency_ms);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<AiToolCallRecord> Database::getRecentAiToolCalls(const std::string& serverUuid, int limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AiToolCallRecord> out;
    const char* sql =
        "SELECT id, server_uuid, username, tool_name, arguments, result, status, latency_ms, created_at "
        "FROM mcdeploy_ai_tool_calls WHERE server_uuid = ? ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AiToolCallRecord r;
        r.id            = sqlite3_column_int64(stmt, 0);
        r.server_uuid   = (const char*)sqlite3_column_text(stmt, 1);
        r.username      = (const char*)sqlite3_column_text(stmt, 2);
        r.tool_name     = (const char*)sqlite3_column_text(stmt, 3);
        auto a = sqlite3_column_text(stmt, 4);
        r.arguments     = a ? (const char*)a : "";
        auto res = sqlite3_column_text(stmt, 5);
        r.result        = res ? (const char*)res : "";
        r.status        = (const char*)sqlite3_column_text(stmt, 6);
        r.latency_ms    = sqlite3_column_int(stmt, 7);
        r.created_at    = (const char*)sqlite3_column_text(stmt, 8);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

// ============================================================
// AI Editor: undo stack for file writes/deletes
// ============================================================
bool Database::pushAiUndo(const AiUndoRecord& u) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "INSERT INTO mcdeploy_ai_undo "
        "(server_uuid, username, operation, file_path, previous_content, previous_existed, new_content, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now'));";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, u.server_uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, u.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, u.operation.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, u.file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, u.previous_content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, u.previous_existed ? 1 : 0);
    sqlite3_bind_text(stmt, 7, u.new_content.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (ok) {
        // Trim to the most recent 100 undo entries per server+user to bound growth.
        const char* trimSql =
            "DELETE FROM mcdeploy_ai_undo WHERE id IN ("
            "  SELECT id FROM mcdeploy_ai_undo "
            "  WHERE server_uuid = ? AND username = ? "
            "  ORDER BY id DESC LIMIT -1 OFFSET 100"
            ");";
        sqlite3_stmt* ts = nullptr;
        if (sqlite3_prepare_v2(m_db, trimSql, -1, &ts, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(ts, 1, u.server_uuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ts, 2, u.username.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(ts);
            sqlite3_finalize(ts);
        }
    }
    return ok;
}

bool Database::popAiUndo(const std::string& serverUuid, const std::string& username, AiUndoRecord& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sel =
        "SELECT id, operation, file_path, COALESCE(previous_content,''), previous_existed, COALESCE(new_content,''), created_at "
        "FROM mcdeploy_ai_undo WHERE server_uuid = ? AND username = ? ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sel, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id                = sqlite3_column_int64(stmt, 0);
        out.operation         = (const char*)sqlite3_column_text(stmt, 1);
        out.file_path         = (const char*)sqlite3_column_text(stmt, 2);
        out.previous_content  = (const char*)sqlite3_column_text(stmt, 3);
        out.previous_existed  = sqlite3_column_int(stmt, 4) != 0;
        out.new_content       = (const char*)sqlite3_column_text(stmt, 5);
        out.created_at        = (const char*)sqlite3_column_text(stmt, 6);
        out.server_uuid       = serverUuid;
        out.username          = username;
        found = true;
    }
    sqlite3_finalize(stmt);
    if (!found) return false;

    const char* del = "DELETE FROM mcdeploy_ai_undo WHERE id = ?;";
    sqlite3_stmt* ds = nullptr;
    if (sqlite3_prepare_v2(m_db, del, -1, &ds, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(ds, 1, out.id);
        sqlite3_step(ds);
        sqlite3_finalize(ds);
    }
    return true;
}

int Database::countAiUndoStack(const std::string& serverUuid, const std::string& username) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "SELECT COUNT(*) FROM mcdeploy_ai_undo WHERE server_uuid = ? AND username = ?;";
    sqlite3_stmt* stmt = nullptr;
    int c = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) c = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return c;
}

// ============================================================
// AI Editor: usage/token accounting
// ============================================================
bool Database::recordAiUsage(const std::string& serverUuid, const std::string& username,
                             const std::string& model, long long promptTokens, long long completionTokens) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "INSERT INTO mcdeploy_ai_usage (server_uuid, username, model, tokens_prompt, tokens_completion, created_at) "
        "VALUES (?, ?, ?, ?, ?, datetime('now'));";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, promptTokens);
    sqlite3_bind_int64(stmt, 5, completionTokens);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

AiUsageRecord Database::getAiUsageTotals(const std::string& serverUuid, const std::string& username) {
    std::lock_guard<std::mutex> lock(m_mutex);
    AiUsageRecord out;
    out.server_uuid = serverUuid;
    out.username = username;
    const char* sql =
        "SELECT COALESCE(SUM(tokens_prompt),0), COALESCE(SUM(tokens_completion),0), COUNT(*), "
        "  COALESCE((SELECT model FROM mcdeploy_ai_usage WHERE server_uuid=? AND username=? ORDER BY id DESC LIMIT 1),'') "
        "FROM mcdeploy_ai_usage WHERE server_uuid = ? AND username = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, username.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.tokens_prompt     = sqlite3_column_int64(stmt, 0);
        out.tokens_completion = sqlite3_column_int64(stmt, 1);
        out.request_count     = sqlite3_column_int64(stmt, 2);
        out.model             = (const char*)sqlite3_column_text(stmt, 3);
    }
    sqlite3_finalize(stmt);
    return out;
}

// ============================================================
// AI Editor: simple sliding-window rate limit
// ============================================================
bool Database::aiRateLimitAllow(const std::string& username, int maxPerMinute, int maxPerDay) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Prune rows older than a day to keep table small.
    executeQuery("DELETE FROM mcdeploy_ai_rate_events WHERE created_at < datetime('now', '-1 day');");

    // Count last-60s and last-24h calls for this user
    auto countSince = [&](const char* interval) -> int {
        std::string sql =
            "SELECT COUNT(*) FROM mcdeploy_ai_rate_events WHERE username = ? AND created_at > datetime('now', ?);";
        sqlite3_stmt* stmt = nullptr;
        int c = 0;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, interval, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) c = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        return c;
    };

    int minuteCount = countSince("-60 seconds");
    if (minuteCount >= maxPerMinute) return false;
    int dayCount = countSince("-1 day");
    if (dayCount >= maxPerDay) return false;

    // Record the event
    const char* ins = "INSERT INTO mcdeploy_ai_rate_events (username, created_at) VALUES (?, datetime('now'));";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, ins, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    return true;
}

// ============================================================
// Team / Membership persistence
// ============================================================
static std::string lowerEmail(const std::string& e) {
    std::string s = e;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    // Trim whitespace
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    return s;
}

bool Database::addServerMember(const ServerMemberRecord& m) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "INSERT INTO mcdeploy_server_members "
        "(server_uuid, email, role, permissions_json, added_by, display_name, status, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now')) "
        "ON CONFLICT(server_uuid, email) DO UPDATE SET "
        "role=excluded.role, permissions_json=excluded.permissions_json, "
        "display_name=excluded.display_name, status=excluded.status;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string email = lowerEmail(m.email);
    std::string status = m.status.empty() ? "active" : m.status;
    sqlite3_bind_text(stmt, 1, m.server_uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, m.role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, m.permissions_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, m.added_by.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, m.display_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, status.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::updateServerMember(long long id, const std::string& role, const std::string& permissionsJson, const std::string& displayName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "UPDATE mcdeploy_server_members SET role=?, permissions_json=?, display_name=? WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, permissionsJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::updateServerMemberStatus(long long id, const std::string& status) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "UPDATE mcdeploy_server_members SET status=? WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::removeServerMember(long long id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "DELETE FROM mcdeploy_server_members WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::removeServerMemberByEmail(const std::string& serverUuid, const std::string& email) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "DELETE FROM mcdeploy_server_members WHERE server_uuid=? AND email=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string em = lowerEmail(email);
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, em.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

static void hydrateMember(sqlite3_stmt* stmt, ServerMemberRecord& r) {
    r.id             = sqlite3_column_int64(stmt, 0);
    r.server_uuid    = (const char*)sqlite3_column_text(stmt, 1);
    r.email          = (const char*)sqlite3_column_text(stmt, 2);
    r.role           = (const char*)sqlite3_column_text(stmt, 3);
    r.permissions_json = (const char*)sqlite3_column_text(stmt, 4);
    auto ab = sqlite3_column_text(stmt, 5);
    r.added_by       = ab ? (const char*)ab : "";
    auto dn = sqlite3_column_text(stmt, 6);
    r.display_name   = dn ? (const char*)dn : "";
    auto st = sqlite3_column_text(stmt, 7);
    r.status         = st ? (const char*)st : "active";
    r.created_at     = (const char*)sqlite3_column_text(stmt, 8);
    auto ls = sqlite3_column_text(stmt, 9);
    r.last_seen_at   = ls ? (const char*)ls : "";
}

std::vector<ServerMemberRecord> Database::listServerMembers(const std::string& serverUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ServerMemberRecord> out;
    const char* sql =
        "SELECT id, server_uuid, email, role, permissions_json, added_by, display_name, status, created_at, COALESCE(last_seen_at,'') "
        "FROM mcdeploy_server_members WHERE server_uuid=? ORDER BY created_at ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ServerMemberRecord r;
        hydrateMember(stmt, r);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool Database::getServerMember(const std::string& serverUuid, const std::string& email, ServerMemberRecord& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "SELECT id, server_uuid, email, role, permissions_json, added_by, display_name, status, created_at, COALESCE(last_seen_at,'') "
        "FROM mcdeploy_server_members WHERE server_uuid=? AND email=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string em = lowerEmail(email);
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, em.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) { hydrateMember(stmt, out); found = true; }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::getServerMemberById(long long id, ServerMemberRecord& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "SELECT id, server_uuid, email, role, permissions_json, added_by, display_name, status, created_at, COALESCE(last_seen_at,'') "
        "FROM mcdeploy_server_members WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) { hydrateMember(stmt, out); found = true; }
    sqlite3_finalize(stmt);
    return found;
}

std::vector<ServerMemberRecord> Database::listServersForEmail(const std::string& email) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ServerMemberRecord> out;
    const char* sql =
        "SELECT id, server_uuid, email, role, permissions_json, added_by, display_name, status, created_at, COALESCE(last_seen_at,'') "
        "FROM mcdeploy_server_members WHERE email=? ORDER BY created_at ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    std::string em = lowerEmail(email);
    sqlite3_bind_text(stmt, 1, em.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ServerMemberRecord r;
        hydrateMember(stmt, r);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

void Database::touchMemberLastSeen(const std::string& serverUuid, const std::string& email) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "UPDATE mcdeploy_server_members SET last_seen_at=datetime('now') WHERE server_uuid=? AND email=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string em = lowerEmail(email);
        sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, em.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// ============================================================
// Scheduled Tasks
// ============================================================
static void hydrateScheduledTask(sqlite3_stmt* stmt, ScheduledTaskRecord& r) {
    auto s = [&](int col) {
        const unsigned char* v = sqlite3_column_text(stmt, col);
        return v ? std::string(reinterpret_cast<const char*>(v)) : std::string();
    };
    r.id             = sqlite3_column_int64(stmt, 0);
    r.server_uuid    = s(1);
    r.name           = s(2);
    r.action_type    = s(3);
    r.payload        = s(4);
    r.schedule_kind  = s(5);
    r.schedule_value = s(6);
    r.enabled        = sqlite3_column_int(stmt, 7);
    r.next_run_at    = s(8);
    r.last_run_at    = s(9);
    r.last_status    = s(10);
    r.last_output    = s(11);
    r.created_by     = s(12);
    r.created_at     = s(13);
}

static const char* kScheduledSelectColumns =
    "id, server_uuid, name, action_type, payload, schedule_kind, schedule_value, "
    "enabled, COALESCE(next_run_at,''), COALESCE(last_run_at,''), COALESCE(last_status,''), "
    "COALESCE(last_output,''), COALESCE(created_by,''), created_at";

long long Database::createScheduledTask(const ScheduledTaskRecord& t) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "INSERT INTO mcdeploy_scheduled_tasks "
        "(server_uuid, name, action_type, payload, schedule_kind, schedule_value, enabled, "
        " next_run_at, last_run_at, last_status, last_output, created_by, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, '', '', '', ?, datetime('now'));";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_text (stmt, 1, t.server_uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 2, t.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 3, t.action_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 4, t.payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 5, t.schedule_kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 6, t.schedule_value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (stmt, 7, t.enabled ? 1 : 0);
    sqlite3_bind_text (stmt, 8, t.next_run_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 9, t.created_by.c_str(), -1, SQLITE_TRANSIENT);
    long long id = 0;
    if (sqlite3_step(stmt) == SQLITE_DONE) id = sqlite3_last_insert_rowid(m_db);
    sqlite3_finalize(stmt);
    return id;
}

bool Database::updateScheduledTask(long long id, const ScheduledTaskRecord& t) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "UPDATE mcdeploy_scheduled_tasks SET "
        "name = ?, action_type = ?, payload = ?, schedule_kind = ?, schedule_value = ?, "
        "enabled = ?, next_run_at = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text (stmt, 1, t.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 2, t.action_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 3, t.payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 4, t.schedule_kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 5, t.schedule_value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (stmt, 6, t.enabled ? 1 : 0);
    sqlite3_bind_text (stmt, 7, t.next_run_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 8, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deleteScheduledTask(long long id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM mcdeploy_scheduled_tasks WHERE id = ?;", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::setScheduledTaskEnabled(long long id, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE mcdeploy_scheduled_tasks SET enabled = ? WHERE id = ?;", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int  (stmt, 1, enabled ? 1 : 0);
    sqlite3_bind_int64(stmt, 2, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::setScheduledTaskNextRun(long long id, const std::string& nextRunAt) {
    std::lock_guard<std::mutex> lock(m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE mcdeploy_scheduled_tasks SET next_run_at = ? WHERE id = ?;", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text (stmt, 1, nextRunAt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::recordScheduledTaskResult(long long id, const std::string& status,
                                         const std::string& output, const std::string& nextRunAt) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "UPDATE mcdeploy_scheduled_tasks SET last_run_at = datetime('now'), "
        "last_status = ?, last_output = ?, next_run_at = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    // Trim output to something reasonable.
    std::string outTrim = output.size() > 2000 ? output.substr(0, 2000) + "…[truncated]" : output;
    sqlite3_bind_text (stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 2, outTrim.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 3, nextRunAt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::getScheduledTask(long long id, ScheduledTaskRecord& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = std::string("SELECT ") + kScheduledSelectColumns +
                      " FROM mcdeploy_scheduled_tasks WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        hydrateScheduledTask(stmt, out);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

std::vector<ScheduledTaskRecord> Database::listScheduledTasks(const std::string& serverUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ScheduledTaskRecord> out;
    std::string sql = std::string("SELECT ") + kScheduledSelectColumns +
                      " FROM mcdeploy_scheduled_tasks WHERE server_uuid = ? ORDER BY id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScheduledTaskRecord r;
        hydrateScheduledTask(stmt, r);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<ScheduledTaskRecord> Database::listDueScheduledTasks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ScheduledTaskRecord> out;
    // Only pick tasks whose server still exists (JOIN suppresses orphans).
    std::string sql =
        std::string("SELECT ") + kScheduledSelectColumns +
        " FROM mcdeploy_scheduled_tasks t "
        " INNER JOIN mcdeploy_servers s ON s.uuid = t.server_uuid "
        " WHERE t.enabled = 1 AND t.next_run_at IS NOT NULL AND t.next_run_at <> '' "
        "       AND t.next_run_at <= datetime('now') "
        " ORDER BY t.next_run_at ASC LIMIT 100;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScheduledTaskRecord r;
        hydrateScheduledTask(stmt, r);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<ScheduledTaskRunRecord> Database::listScheduledTaskRuns(long long taskId, int limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ScheduledTaskRunRecord> out;
    const char* sql =
        "SELECT id, task_id, started_at, COALESCE(finished_at,''), status, COALESCE(output,'') "
        "FROM mcdeploy_scheduled_task_runs WHERE task_id = ? ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int64(stmt, 1, taskId);
    sqlite3_bind_int  (stmt, 2, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScheduledTaskRunRecord r;
        r.id          = sqlite3_column_int64(stmt, 0);
        r.task_id     = sqlite3_column_int64(stmt, 1);
        r.started_at  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        r.finished_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        r.status      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        r.output      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

long long Database::insertScheduledTaskRun(const ScheduledTaskRunRecord& r) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "INSERT INTO mcdeploy_scheduled_task_runs (task_id, started_at, finished_at, status, output) "
        "VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    std::string outTrim = r.output.size() > 4000 ? r.output.substr(0, 4000) + "…[truncated]" : r.output;
    sqlite3_bind_int64(stmt, 1, r.task_id);
    sqlite3_bind_text (stmt, 2, r.started_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 3, r.finished_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 4, r.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 5, outTrim.c_str(), -1, SQLITE_TRANSIENT);
    long long id = 0;
    if (sqlite3_step(stmt) == SQLITE_DONE) id = sqlite3_last_insert_rowid(m_db);
    sqlite3_finalize(stmt);
    // Keep only the last 200 runs per task to prevent unbounded growth.
    sqlite3_stmt* prune = nullptr;
    const char* pruneSql =
        "DELETE FROM mcdeploy_scheduled_task_runs WHERE task_id = ? AND id NOT IN "
        "(SELECT id FROM mcdeploy_scheduled_task_runs WHERE task_id = ? ORDER BY id DESC LIMIT 200);";
    if (sqlite3_prepare_v2(m_db, pruneSql, -1, &prune, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(prune, 1, r.task_id);
        sqlite3_bind_int64(prune, 2, r.task_id);
        sqlite3_step(prune);
        sqlite3_finalize(prune);
    }
    return id;
}

// ============================================================
// Player Analytics
// ============================================================
bool Database::logPlayerEvent(const PlayerEventRecord& ev) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "INSERT INTO mcdeploy_player_events "
        "(server_uuid, player_uuid, username, event_type, payload, created_at) "
        "VALUES (?, ?, ?, ?, ?, datetime('now'));";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    // Cap payload size — chat can be long, death messages can be long.
    std::string p = ev.payload;
    if (p.size() > 512) p = p.substr(0, 512);
    sqlite3_bind_text(stmt, 1, ev.server_uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ev.player_uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ev.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, ev.event_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, p.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

long long Database::startPlayerSession(const std::string& serverUuid,
                                       const std::string& playerUuid,
                                       const std::string& username,
                                       double x, double y, double z) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Close any pre-existing open session for this player on this server as "abandoned".
    {
        const char* closeSql =
            "UPDATE mcdeploy_player_sessions SET status = 'abandoned', "
            "left_at = datetime('now'), "
            "duration_seconds = CAST((julianday('now') - julianday(joined_at)) * 86400 AS INTEGER) "
            "WHERE server_uuid = ? AND username = ? AND status = 'active';";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, closeSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    const char* sql =
        "INSERT INTO mcdeploy_player_sessions "
        "(server_uuid, player_uuid, username, joined_at, left_at, duration_seconds, "
        " join_x, join_y, join_z, status) "
        "VALUES (?, ?, ?, datetime('now'), '', 0, ?, ?, ?, 'active');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_text  (stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 2, playerUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 3, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, x);
    sqlite3_bind_double(stmt, 5, y);
    sqlite3_bind_double(stmt, 6, z);
    long long id = 0;
    if (sqlite3_step(stmt) == SQLITE_DONE) id = sqlite3_last_insert_rowid(m_db);
    sqlite3_finalize(stmt);
    return id;
}

bool Database::endPlayerSession(const std::string& serverUuid, const std::string& username) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "UPDATE mcdeploy_player_sessions SET "
        "left_at = datetime('now'), status = 'ended', "
        "duration_seconds = CAST((julianday('now') - julianday(joined_at)) * 86400 AS INTEGER) "
        "WHERE id = ("
        "  SELECT id FROM mcdeploy_player_sessions "
        "  WHERE server_uuid = ? AND username = ? AND status = 'active' "
        "  ORDER BY id DESC LIMIT 1);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

void Database::closeOpenSessionsForServer(const std::string& serverUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "UPDATE mcdeploy_player_sessions SET "
        "left_at = datetime('now'), status = 'abandoned', "
        "duration_seconds = CAST((julianday('now') - julianday(joined_at)) * 86400 AS INTEGER) "
        "WHERE server_uuid = ? AND status = 'active';";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// Helper: append a "AND created_at > datetime('now', '-N days')" clause when days>0.
static std::string daysClause(int days, const std::string& col) {
    if (days <= 0) return "";
    return " AND " + col + " > datetime('now', '-" + std::to_string(days) + " days')";
}

nlohmann::json Database::getAnalyticsSummary(const std::string& serverUuid, int days) {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json out;
    out["days"] = days;

    auto bindServer = [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    };

    // Unique players
    {
        std::string sql =
            "SELECT COUNT(DISTINCT username) FROM mcdeploy_player_sessions "
            "WHERE server_uuid = ?" + daysClause(days, "joined_at") + ";";
        sqlite3_stmt* stmt = nullptr;
        long long unique_players = 0;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            bindServer(stmt);
            if (sqlite3_step(stmt) == SQLITE_ROW) unique_players = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
        }
        out["unique_players"] = unique_players;
    }

    // Total ended playtime + total sessions + average session length
    {
        std::string sql =
            "SELECT COALESCE(SUM(duration_seconds),0), COUNT(*), COALESCE(AVG(duration_seconds),0) "
            "FROM mcdeploy_player_sessions WHERE server_uuid = ? "
            " AND status != 'active' AND duration_seconds > 0" +
            daysClause(days, "joined_at") + ";";
        sqlite3_stmt* stmt = nullptr;
        long long total = 0, count = 0;
        double avg = 0.0;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            bindServer(stmt);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                total = sqlite3_column_int64(stmt, 0);
                count = sqlite3_column_int64(stmt, 1);
                avg   = sqlite3_column_double(stmt, 2);
            }
            sqlite3_finalize(stmt);
        }
        out["total_playtime_seconds"]  = total;
        out["total_sessions"]          = count;
        out["avg_session_seconds"]     = (long long)avg;
    }

    // Peak concurrent — approximate: for every join event, count how many other sessions
    // were open at that moment (i.e. joined_at <= t AND (left_at = '' OR left_at >= t)).
    // For lookback windows this is a small enough set to compute in SQL.
    {
        std::string sql =
            "SELECT MAX(concurrent) FROM ("
            "  SELECT a.joined_at, COUNT(*) AS concurrent "
            "  FROM mcdeploy_player_sessions a "
            "  JOIN mcdeploy_player_sessions b "
            "    ON b.server_uuid = a.server_uuid "
            "   AND b.joined_at <= a.joined_at "
            "   AND (b.left_at = '' OR b.left_at >= a.joined_at) "
            "  WHERE a.server_uuid = ?" + daysClause(days, "a.joined_at") +
            "  GROUP BY a.id);";
        sqlite3_stmt* stmt = nullptr;
        long long peak = 0;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            bindServer(stmt);
            if (sqlite3_step(stmt) == SQLITE_ROW) peak = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
        }
        out["peak_concurrent"] = peak;
    }

    // Total chat / death event counts
    {
        std::string sql =
            "SELECT "
            "  SUM(CASE WHEN event_type='chat'  THEN 1 ELSE 0 END), "
            "  SUM(CASE WHEN event_type='death' THEN 1 ELSE 0 END), "
            "  SUM(CASE WHEN event_type='join'  THEN 1 ELSE 0 END), "
            "  SUM(CASE WHEN event_type='leave' THEN 1 ELSE 0 END)  "
            "FROM mcdeploy_player_events WHERE server_uuid = ?" + daysClause(days, "created_at") + ";";
        sqlite3_stmt* stmt = nullptr;
        long long c = 0, d = 0, j = 0, l = 0;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            bindServer(stmt);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                c = sqlite3_column_int64(stmt, 0);
                d = sqlite3_column_int64(stmt, 1);
                j = sqlite3_column_int64(stmt, 2);
                l = sqlite3_column_int64(stmt, 3);
            }
            sqlite3_finalize(stmt);
        }
        out["chat_count"]   = c;
        out["death_count"]  = d;
        out["join_count"]   = j;
        out["leave_count"]  = l;
    }

    // Currently online (active sessions).
    {
        const char* sql =
            "SELECT COUNT(*) FROM mcdeploy_player_sessions "
            "WHERE server_uuid = ? AND status = 'active';";
        sqlite3_stmt* stmt = nullptr;
        long long online = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            bindServer(stmt);
            if (sqlite3_step(stmt) == SQLITE_ROW) online = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
        }
        out["online_now"] = online;
    }

    return out;
}

nlohmann::json Database::getAnalyticsHourly(const std::string& serverUuid, int days) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Sum of join minutes bucketed by hour-of-day (local time).
    // We approximate by attributing the full session duration to the hour it started in.
    // Good enough for a "when are we busiest" heatmap.
    std::string sql =
        "SELECT CAST(strftime('%H', joined_at, 'localtime') AS INTEGER) AS hr, "
        "       COUNT(*), COALESCE(SUM(duration_seconds),0) "
        "FROM mcdeploy_player_sessions "
        "WHERE server_uuid = ?" + daysClause(days, "joined_at") +
        " GROUP BY hr ORDER BY hr ASC;";
    sqlite3_stmt* stmt = nullptr;
    // 24-slot dense array so the frontend renders a full day even when hours have no data.
    nlohmann::json arr = nlohmann::json::array();
    long long sessions[24] = {0};
    long long seconds [24] = {0};
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int hr = sqlite3_column_int(stmt, 0);
            if (hr < 0 || hr > 23) continue;
            sessions[hr] = sqlite3_column_int64(stmt, 1);
            seconds [hr] = sqlite3_column_int64(stmt, 2);
        }
        sqlite3_finalize(stmt);
    }
    for (int h = 0; h < 24; h++) {
        arr.push_back({{"hour", h}, {"sessions", sessions[h]}, {"seconds", seconds[h]}});
    }
    return arr;
}

nlohmann::json Database::getAnalyticsDaily(const std::string& serverUuid, int days) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // For each day in window: unique players, session count, total seconds.
    std::string sql =
        "SELECT strftime('%Y-%m-%d', joined_at, 'localtime') AS day, "
        "       COUNT(DISTINCT username), COUNT(*), COALESCE(SUM(duration_seconds),0) "
        "FROM mcdeploy_player_sessions "
        "WHERE server_uuid = ?" + daysClause(days, "joined_at") +
        " GROUP BY day ORDER BY day ASC;";
    sqlite3_stmt* stmt = nullptr;
    nlohmann::json arr = nlohmann::json::array();
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* day = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            arr.push_back({
                {"day",             day ? day : ""},
                {"unique_players",  sqlite3_column_int64(stmt, 1)},
                {"sessions",        sqlite3_column_int64(stmt, 2)},
                {"total_seconds",   sqlite3_column_int64(stmt, 3)}
            });
        }
        sqlite3_finalize(stmt);
    }
    return arr;
}

nlohmann::json Database::getAnalyticsLeaderboard(const std::string& serverUuid, int days, int limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql =
        "SELECT username, "
        "       COALESCE(SUM(duration_seconds),0) AS total_secs, "
        "       COUNT(*) AS sessions, "
        "       MAX(joined_at) AS last_seen "
        "FROM mcdeploy_player_sessions "
        "WHERE server_uuid = ?" + daysClause(days, "joined_at") +
        " GROUP BY username ORDER BY total_secs DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    nlohmann::json arr = nlohmann::json::array();
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (stmt, 2, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* u  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* ls = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            arr.push_back({
                {"username",       u ? u : ""},
                {"total_seconds",  sqlite3_column_int64(stmt, 1)},
                {"sessions",       sqlite3_column_int64(stmt, 2)},
                {"last_seen",      ls ? ls : ""}
            });
        }
        sqlite3_finalize(stmt);
    }
    return arr;
}

std::vector<PlayerEventRecord> Database::getPlayerEvents(const std::string& serverUuid,
                                                         const std::string& eventType,
                                                         int limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlayerEventRecord> out;
    std::string sql =
        "SELECT id, server_uuid, COALESCE(player_uuid,''), username, event_type, "
        "       COALESCE(payload,''), created_at "
        "FROM mcdeploy_player_events WHERE server_uuid = ?";
    if (!eventType.empty()) sql += " AND event_type = ?";
    sql += " ORDER BY id DESC LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return out;
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    if (!eventType.empty()) sqlite3_bind_text(stmt, idx++, eventType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlayerEventRecord r;
        r.id          = sqlite3_column_int64(stmt, 0);
        r.server_uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        r.player_uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        r.username    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        r.event_type  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        r.payload     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        r.created_at  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

// ============================================================
// AI Automation Rules
// ============================================================
static void hydrateAutomationRule(sqlite3_stmt* stmt, AutomationRuleRecord& r) {
    auto text = [&](int col) {
        const unsigned char* value = sqlite3_column_text(stmt, col);
        return value ? std::string(reinterpret_cast<const char*>(value)) : std::string();
    };
    r.id = sqlite3_column_int64(stmt, 0);
    r.server_uuid = text(1);
    r.name = text(2);
    r.trigger_type = text(3);
    r.threshold = sqlite3_column_double(stmt, 4);
    r.condition_value = text(5);
    r.action_type = text(6);
    r.action_payload = text(7);
    r.enabled = sqlite3_column_int(stmt, 8);
    r.cooldown_seconds = sqlite3_column_int(stmt, 9);
    r.last_evaluated_at = text(10);
    r.last_triggered_at = text(11);
    r.last_status = text(12);
    r.last_output = text(13);
    r.created_by = text(14);
    r.created_at = text(15);
}

static const char* kAutomationColumns =
    "id, server_uuid, name, trigger_type, threshold, COALESCE(condition_value,''), "
    "action_type, COALESCE(action_payload,''), enabled, cooldown_seconds, "
    "COALESCE(last_evaluated_at,''), COALESCE(last_triggered_at,''), "
    "COALESCE(last_status,''), COALESCE(last_output,''), COALESCE(created_by,''), created_at";

long long Database::createAutomationRule(const AutomationRuleRecord& r) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "INSERT INTO mcdeploy_automation_rules "
        "(server_uuid,name,trigger_type,threshold,condition_value,action_type,action_payload,enabled,cooldown_seconds,created_by,created_at) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,datetime('now'));";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, r.server_uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, r.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, r.trigger_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, r.threshold);
    sqlite3_bind_text(stmt, 5, r.condition_value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, r.action_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, r.action_payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, r.enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 9, r.cooldown_seconds);
    sqlite3_bind_text(stmt, 10, r.created_by.c_str(), -1, SQLITE_TRANSIENT);
    long long id = sqlite3_step(stmt) == SQLITE_DONE ? sqlite3_last_insert_rowid(m_db) : 0;
    sqlite3_finalize(stmt);
    return id;
}

bool Database::updateAutomationRule(long long id, const AutomationRuleRecord& r) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "UPDATE mcdeploy_automation_rules SET name=?,trigger_type=?,threshold=?,condition_value=?,"
        "action_type=?,action_payload=?,enabled=?,cooldown_seconds=? WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, r.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, r.trigger_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, r.threshold);
    sqlite3_bind_text(stmt, 4, r.condition_value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, r.action_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, r.action_payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, r.enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 8, r.cooldown_seconds);
    sqlite3_bind_int64(stmt, 9, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deleteAutomationRule(long long id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM mcdeploy_automation_rules WHERE id=?;", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::setAutomationRuleEnabled(long long id, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE mcdeploy_automation_rules SET enabled=? WHERE id=?;", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, enabled ? 1 : 0);
    sqlite3_bind_int64(stmt, 2, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::getAutomationRule(long long id, AutomationRuleRecord& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = std::string("SELECT ") + kAutomationColumns + " FROM mcdeploy_automation_rules WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    if (found) hydrateAutomationRule(stmt, out);
    sqlite3_finalize(stmt);
    return found;
}

std::vector<AutomationRuleRecord> Database::listAutomationRules(const std::string& serverUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AutomationRuleRecord> out;
    std::string sql = std::string("SELECT ") + kAutomationColumns +
                      " FROM mcdeploy_automation_rules WHERE server_uuid=? ORDER BY id DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AutomationRuleRecord r;
        hydrateAutomationRule(stmt, r);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<AutomationRuleRecord> Database::listRunnableAutomationRules() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AutomationRuleRecord> out;
    std::string sql = std::string("SELECT ") + kAutomationColumns +
        " FROM mcdeploy_automation_rules WHERE enabled=1 "
        "AND (last_triggered_at='' OR last_triggered_at IS NULL OR "
        "last_triggered_at <= datetime('now','-' || cooldown_seconds || ' seconds')) "
        "ORDER BY id ASC LIMIT 100;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AutomationRuleRecord r;
        hydrateAutomationRule(stmt, r);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool Database::recordAutomationEvaluation(long long id, bool triggered,
                                          const std::string& status, const std::string& output) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "UPDATE mcdeploy_automation_rules SET last_evaluated_at=datetime('now'), "
        "last_triggered_at=CASE WHEN ? THEN datetime('now') ELSE last_triggered_at END, "
        "last_status=?, last_output=? WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string trimmed = output.size() > 2000 ? output.substr(0, 2000) : output;
    sqlite3_bind_int(stmt, 1, triggered ? 1 : 0);
    sqlite3_bind_text(stmt, 2, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, trimmed.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

// ============================================================
// Maintenance Mode
// ============================================================
bool Database::getMaintenance(const std::string& serverUuid, MaintenanceRecord& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "SELECT server_uuid,enabled,message,prevent_joins,backup_on_enable,whitelist_was_enabled,"
        "COALESCE(enabled_by,''),COALESCE(enabled_at,''),updated_at "
        "FROM mcdeploy_maintenance WHERE server_uuid=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, serverUuid.c_str(), -1, SQLITE_TRANSIENT);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    if (found) {
        auto text = [&](int col) {
            const unsigned char* value = sqlite3_column_text(stmt, col);
            return value ? std::string(reinterpret_cast<const char*>(value)) : std::string();
        };
        out.server_uuid = text(0);
        out.enabled = sqlite3_column_int(stmt, 1);
        out.message = text(2);
        out.prevent_joins = sqlite3_column_int(stmt, 3);
        out.backup_on_enable = sqlite3_column_int(stmt, 4);
        out.whitelist_was_enabled = sqlite3_column_int(stmt, 5);
        out.enabled_by = text(6);
        out.enabled_at = text(7);
        out.updated_at = text(8);
    }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::upsertMaintenance(const MaintenanceRecord& r) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "INSERT INTO mcdeploy_maintenance "
        "(server_uuid,enabled,message,prevent_joins,backup_on_enable,whitelist_was_enabled,enabled_by,enabled_at,updated_at) "
        "VALUES (?,?,?,?,?,?,?,CASE WHEN ? THEN datetime('now') ELSE '' END,datetime('now')) "
        "ON CONFLICT(server_uuid) DO UPDATE SET enabled=excluded.enabled,message=excluded.message,"
        "prevent_joins=excluded.prevent_joins,backup_on_enable=excluded.backup_on_enable,"
        "whitelist_was_enabled=excluded.whitelist_was_enabled,enabled_by=excluded.enabled_by,"
        "enabled_at=CASE WHEN excluded.enabled=1 AND mcdeploy_maintenance.enabled=0 THEN datetime('now') "
        "WHEN excluded.enabled=0 THEN '' ELSE mcdeploy_maintenance.enabled_at END,updated_at=datetime('now');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, r.server_uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, r.enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 3, r.message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, r.prevent_joins ? 1 : 0);
    sqlite3_bind_int(stmt, 5, r.backup_on_enable ? 1 : 0);
    sqlite3_bind_int(stmt, 6, r.whitelist_was_enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 7, r.enabled_by.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, r.enabled ? 1 : 0);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace MCDeploy
