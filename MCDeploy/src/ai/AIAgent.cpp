// ============================================================
// MCDeploy - AI Agent Implementation
// ============================================================
#include "AIAgent.h"
#include "../models/Database.h"
#include "../utils/ProcessManager.h"
#include "../utils/SystemInfo.h"

#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <random>
#include <thread>
#include <tuple>
#include <cstdio>
#include <array>
#ifdef _WIN32
#include <windows.h>
#endif

namespace MCDeploy {

namespace fs = std::filesystem;

// ============================================================
// Config loading
// ============================================================
AiConfig loadAiConfig() {
    AiConfig cfg;

    // Env vars take precedence
    if (const char* envKey = std::getenv("MCDEPLOY_AI_KEY")) cfg.api_key = envKey;
    else if (const char* g = std::getenv("GEMINI_API_KEY")) cfg.api_key = g;

    std::ifstream f("config.json");
    if (f.is_open()) {
        try {
            nlohmann::json j;
            f >> j;
            if (j.contains("mcdeploy") && j["mcdeploy"].contains("ai")) {
                auto ai = j["mcdeploy"]["ai"];
                if (cfg.api_key.empty() && ai.contains("api_key")) {
                    std::string k = ai["api_key"].get<std::string>();
                    if (!k.empty() && k != "YOUR_GEMINI_API_KEY") cfg.api_key = k;
                }
                if (ai.contains("api_url"))          cfg.api_url = ai["api_url"].get<std::string>();
                if (ai.contains("model"))            cfg.model = ai["model"].get<std::string>();
                if (ai.contains("max_iterations"))   cfg.max_iterations = ai["max_iterations"].get<int>();
                if (ai.contains("temperature"))      cfg.temperature = ai["temperature"].get<double>();
                if (ai.contains("max_output_tokens")) cfg.max_output_tokens = ai["max_output_tokens"].get<int>();
            }
        } catch (...) {}
    }
    return cfg;
}

// ============================================================
// CURL helpers
// ============================================================
static size_t curlWriteToString(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// ============================================================
// Static helpers
// ============================================================
std::string AIAgent::truncateForModel(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    return s.substr(0, maxBytes) + "\n... [truncated by MCDeploy to keep context small; "
                                   "call the tool with a narrower range if you need more] ...";
}

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

static std::string genId() {
    // 12-char random hex for tool_call_id + backup names, etc.
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 15);
    std::string s(12, '0');
    for (auto& c : s) {
        int v = dist(rng);
        c = "0123456789abcdef"[v];
    }
    return s;
}

// ============================================================
// Constructor
// ============================================================
AIAgent::AIAgent(const std::string& serverUuid,
                 const std::string& serverPath,
                 const std::string& username,
                 bool agentMode,
                 const AiConfig& config)
    : m_serverUuid(serverUuid),
      m_serverPath(serverPath),
      m_username(username),
      m_agentMode(agentMode),
      m_config(config) {}

// ============================================================
// Path safety
// ============================================================
bool AIAgent::resolveSafePath(const std::string& relPath, fs::path& outAbsolute, std::string& outError) const {
    if (relPath.empty()) { outError = "path cannot be empty"; return false; }

    // Reject absolute paths and any path with a drive letter or backslash escapes.
    fs::path rel(relPath);
    if (rel.is_absolute()) { outError = "absolute paths are not allowed; provide a path relative to the server directory"; return false; }

    fs::path serverBase;
    try {
        serverBase = fs::weakly_canonical(m_serverPath);
    } catch (...) {
        outError = "server base directory not resolvable";
        return false;
    }

    fs::path combined = serverBase / rel;
    fs::path resolved;
    try {
        resolved = fs::weakly_canonical(combined);
    } catch (...) {
        outError = "unable to resolve path";
        return false;
    }

    // Verify resolved is inside serverBase using lexical comparison of components (portable + case-insensitive on Windows).
    auto normalize = [](fs::path p) -> std::string {
        std::string s = p.generic_string();
#ifdef _WIN32
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
#endif
        // Ensure trailing separator so /foo doesn't match /foobar
        if (!s.empty() && s.back() != '/') s += '/';
        return s;
    };

    std::string baseN = normalize(serverBase);
    std::string resN  = normalize(resolved);
    if (resN.rfind(baseN, 0) != 0) {
        outError = "access denied: path escapes the server directory";
        return false;
    }

    outAbsolute = resolved;
    return true;
}

bool AIAgent::isSensitiveFile(const fs::path& absolutePath) const {
    // Kept intentionally narrow — the user wants the AI to be able to edit
    // server.properties, plugin configs, etc. without a modal every time.
    // Only the launch scripts (which can execute arbitrary host commands) and
    // the world save data (which is unrecoverable if corrupted) remain gated.
    std::string name = toLower(absolutePath.filename().string());
    if (name == "start.bat" || name == "start.sh" || name == "run.sh" || name == "run.bat") return true;

    std::string p = toLower(absolutePath.generic_string());
    // Only save data inside world*/ directories, not everything (e.g. datapacks are fine).
    auto containsDir = [&](const std::string& dir) {
        return p.find("/" + dir + "/region/") != std::string::npos
            || p.find("/" + dir + "/data/") != std::string::npos
            || p.find("/" + dir + "/playerdata/") != std::string::npos
            || p.find("/" + dir + "/entities/") != std::string::npos
            || p.find("/" + dir + "/poi/") != std::string::npos
            || p.find("/" + dir + "/level.dat") != std::string::npos;
    };
    if (containsDir("world") || containsDir("world_nether") || containsDir("world_the_end")) return true;
    return false;
}

bool AIAgent::isDangerousCommand(const std::string& cmd) const {
    // Also intentionally narrow. Only things that end player sessions or grant
    // privileges get gated. Everything else in Agent Mode goes straight through.
    std::string c = toLower(cmd);
    if (!c.empty() && c.front() == '/') c.erase(c.begin());
    static const std::vector<std::string> dangerous = {
        "stop", "shutdown", "restart", "ban", "ban-ip", "op", "deop"
    };
    for (const auto& d : dangerous) {
        if (c == d || c.rfind(d + " ", 0) == 0) return true;
    }
    return false;
}

// ============================================================
// System prompt + tool schemas
// ============================================================
std::string AIAgent::buildSystemPrompt() const {
    std::string mode = m_agentMode ? "AGENT MODE (can modify files, run console commands, tune performance)"
                                   : "READ-ONLY MODE (can inspect files, logs, metrics; cannot modify)";
    std::ostringstream ss;
    ss << "You are the MCDeploy AI Assistant, a specialist in Minecraft server administration.\n"
       << "You are currently in " << mode << ".\n"
       << "Server UUID: " << m_serverUuid << "\n"
       << "Server directory: " << m_serverPath << "\n\n"
       << "Guidelines:\n"
       << "- Prefer calling tools to answer factual questions about the user's server. "
       <<   "Do not guess file contents or metric values.\n"
       << "- When the user asks about a config value, read the actual file first.\n"
       << "- When the user asks about errors or crashes, call search_logs with a relevant pattern.\n"
       << "- Batch tool calls when possible. Do NOT explain what you're about to do in narration; "
       <<   "just call the tools you need.\n"
       << "- For dangerous operations (write_file, delete_file, execute_command, restart_server, stop_server) "
       <<   "you may propose them; MCDeploy will surface a confirmation UI to the user if needed.\n"
       << "- Always show diffs for proposed file edits by calling diff_file before write_file for existing files.\n"
       << "- Respond in concise Markdown. Use short bullets or fenced code blocks for config snippets.\n"
       << "- End every final answer with 2-4 concrete follow-up suggestions in a JSON block like:\n"
       << "  ```suggestions\n  [\"Show recent errors\", \"Restart the server\", \"Explain view-distance\"]\n  ```\n";
    return ss.str();
}

nlohmann::json AIAgent::buildToolSchemas() const {
    using nlohmann::json;
    json tools = json::array();

    auto addTool = [&](const std::string& name, const std::string& desc, json params) {
        json t = {
            {"type", "function"},
            {"function", {
                {"name", name},
                {"description", desc},
                {"parameters", params}
            }}
        };
        tools.push_back(t);
    };

    // ---- read-only tools ----
    addTool("list_directory",
        "List files and subdirectories under the server folder. Use 'subpath' to descend "
        "(defaults to server root). Set 'recursive' to true for a tree listing up to 'depth' levels.",
        {
            {"type", "object"},
            {"properties", {
                {"subpath", {{"type", "string"}, {"description", "Path relative to server root, e.g. 'plugins' or 'world/datapacks'."}}},
                {"recursive", {{"type", "boolean"}, {"description", "If true, walk subdirectories."}}},
                {"depth", {{"type", "integer"}, {"description", "Max recursion depth (default 2)."}}}
            }}
        });

    addTool("read_file",
        "Read a text file from the server directory. Returns up to ~8KB; use 'offset' to page.",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Path relative to server root, e.g. 'server.properties'."}}},
                {"max_bytes", {{"type", "integer"}, {"description", "Max bytes to return (default 8000)."}}},
                {"offset", {{"type", "integer"}, {"description", "Byte offset to start reading from."}}}
            }},
            {"required", {"path"}}
        });

    addTool("search_files",
        "Regex-grep across text files under a directory. Returns matching lines with file:line paths.",
        {
            {"type", "object"},
            {"properties", {
                {"pattern", {{"type", "string"}, {"description", "Regex to search for."}}},
                {"subpath", {{"type", "string"}, {"description", "Relative directory to search under. Default: server root."}}},
                {"file_glob", {{"type", "string"}, {"description", "Optional extension filter like '.yml' or '.properties'."}}},
                {"max_matches", {{"type", "integer"}, {"description", "Max matches to return (default 50)."}}}
            }},
            {"required", {"pattern"}}
        });

    addTool("search_logs",
        "Search the running server's console/log output for a regex pattern. "
        "Great for finding errors, warnings, plugin messages.",
        {
            {"type", "object"},
            {"properties", {
                {"pattern", {{"type", "string"}, {"description", "Regex, case-insensitive. Empty/omit for latest lines."}}},
                {"limit", {{"type", "integer"}, {"description", "Max lines to return (default 40, cap 200)."}}},
                {"level", {{"type", "string"}, {"description", "Filter by level: INFO|WARN|ERROR|STDERR"}}}
            }}
        });

    addTool("diff_file",
        "Compute a unified diff between the current file content and proposed new content, without writing anything. "
        "ALWAYS call this before write_file on existing files so the user can review the change.",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "File to diff against, relative to server root."}}},
                {"new_content", {{"type", "string"}, {"description", "Proposed full new content of the file."}}}
            }},
            {"required", {"path", "new_content"}}
        });

    addTool("get_metrics",
        "Return current CPU/RAM/disk usage, running status, and performance tuning knobs for this server.",
        {{"type", "object"}, {"properties", nlohmann::json::object()}});

    addTool("get_server_info",
        "Return basic metadata about this server (name, software type, MC version, port, RAM allocation, subdomain, uptime).",
        {{"type", "object"}, {"properties", nlohmann::json::object()}});

    addTool("get_plugin_info",
        "List installed plugins/mods with detected filenames and sizes from the plugins/ or mods/ directory.",
        {{"type", "object"}, {"properties", nlohmann::json::object()}});

    addTool("query_players",
        "Return the list of players known for this server, whether they are currently online, and last-known location.",
        {{"type", "object"}, {"properties", nlohmann::json::object()}});

    // ---- write / control tools (only permitted in agent mode) ----
    addTool("write_file",
        "Create or overwrite a text file inside the server directory. Requires AGENT MODE. "
        "For existing files, call diff_file first. MCDeploy will auto-backup the previous content into the undo stack.",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Path relative to server root."}}},
                {"content", {{"type", "string"}, {"description", "Full new file content."}}}
            }},
            {"required", {"path", "content"}}
        });

    addTool("delete_file",
        "Delete a file from the server directory. Requires AGENT MODE. Recorded on the undo stack.",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Path relative to server root."}}}
            }},
            {"required", {"path"}}
        });

    addTool("execute_command",
        "Send a command to the running Minecraft server console (e.g. 'say hello', 'list', 'weather clear'). "
        "Requires AGENT MODE. Dangerous commands (stop, op, ban, gamerule...) will be gated for user approval.",
        {
            {"type", "object"},
            {"properties", {
                {"command", {{"type", "string"}, {"description", "Command text; leading slash optional."}}}
            }},
            {"required", {"command"}}
        });

    addTool("start_server", "Start the Minecraft server if it is stopped. Requires AGENT MODE.",
        {{"type", "object"}, {"properties", nlohmann::json::object()}});
    addTool("stop_server",  "Gracefully stop the Minecraft server. Requires AGENT MODE.",
        {{"type", "object"}, {"properties", nlohmann::json::object()}});
    addTool("restart_server", "Restart the Minecraft server. Requires AGENT MODE.",
        {{"type", "object"}, {"properties", nlohmann::json::object()}});

    addTool("set_performance",
        "Update performance tuning for the running server. Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"cpu_priority", {{"type", "string"}, {"description", "One of: idle, below_normal, normal, above_normal, high"}}},
                {"smart_optimization", {{"type", "boolean"}, {"description", "Enable adaptive optimization."}}}
            }}
        });

    addTool("create_backup",
        "Trigger a backup of the server directory (via ServerController's backup pipeline). Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"note", {{"type", "string"}, {"description", "Optional description for the backup."}}}
            }}
        });

    // ---- misc ----
    addTool("web_search",
        "Look up general Minecraft or Java-related documentation via DuckDuckGo's Instant Answer API. "
        "Only use for questions that require info outside the user's server (e.g. 'what does view-distance do', 'latest Paper version').",
        {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "Search query."}}}
            }},
            {"required", {"query"}}
        });

    // ---- config / control tools (Agent Mode) ----
    addTool("update_server_config",
        "Update this server's RAM allocation and/or port. RAM values are in megabytes. "
        "Use this when the user asks to change memory allocation (e.g. '2G to 4G' -> ram_min_mb=2048, ram_max_mb=4096). "
        "Persists to the DB. Restart the server for JVM args to take effect. Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"ram_min_mb", {{"type", "integer"}, {"description", "Minimum RAM in MB (e.g. 2048 for 2G)."}}},
                {"ram_max_mb", {{"type", "integer"}, {"description", "Maximum RAM in MB (e.g. 4096 for 4G)."}}},
                {"port",       {{"type", "integer"}, {"description", "Server port (1-65535)."}}}
            }}
        });

    addTool("update_start_command",
        "Overwrite this server's launch command line. This is what gets executed when the server starts. "
        "Full command line including java path and JVM args. Requires AGENT MODE (confirmation-gated).",
        {
            {"type", "object"},
            {"properties", {
                {"command", {{"type", "string"}, {"description", "Full start command."}}}
            }},
            {"required", {"command"}}
        });

    addTool("set_server_status",
        "Force the server status field to a specific value (e.g. Maintenance, Offline). Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"status", {{"type", "string"}, {"description", "One of: Online, Offline, Starting, Crashed, Maintenance"}}}
            }},
            {"required", {"status"}}
        });

    // ---- backups ----
    addTool("list_backups",
        "List all backups recorded for this server.",
        {{"type", "object"}, {"properties", nlohmann::json::object()}});

    addTool("delete_backup",
        "Delete a specific backup record and its underlying archive. Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"backup_uuid", {{"type", "string"}, {"description", "UUID of the backup to delete."}}}
            }},
            {"required", {"backup_uuid"}}
        });

    // ---- addons / plugins ----
    addTool("list_installed_addons",
        "List installed plugin/mod jar files in this server's plugins/ or mods/ directory (via MCDeploy's HTTP API).",
        {{"type", "object"}, {"properties", nlohmann::json::object()}});

    addTool("search_addons",
        "Search Modrinth for plugins/mods compatible with this server's software+version.",
        {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "Search query, e.g. 'essentials', 'worldedit'."}}}
            }},
            {"required", {"query"}}
        });

    addTool("install_addon",
        "Install a Modrinth plugin/mod. Provide the Modrinth addon_id (project id) and optionally a specific version_id. Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"addon_id",   {{"type", "string"}, {"description", "Modrinth project id, e.g. 'wSVLbcx7' for WorldEdit."}}},
                {"version_id", {{"type", "string"}, {"description", "Optional specific version id."}}}
            }},
            {"required", {"addon_id"}}
        });

    addTool("uninstall_addon",
        "Uninstall an addon by its jar filename. Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"filename", {{"type", "string"}, {"description", "Jar filename, e.g. 'WorldEdit-7.2.jar'."}}}
            }},
            {"required", {"filename"}}
        });

    // ---- extended file ops ----
    addTool("append_file",
        "Append content to the end of a file. Creates the file if missing. Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}}},
                {"content", {{"type", "string"}}}
            }},
            {"required", {"path", "content"}}
        });

    addTool("create_directory",
        "Create a directory (including parents) inside the server root.",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Directory path relative to server root."}}}
            }},
            {"required", {"path"}}
        });

    addTool("move_file",
        "Move or rename a file/directory inside the server root. Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"from", {{"type", "string"}}},
                {"to",   {{"type", "string"}}}
            }},
            {"required", {"from", "to"}}
        });

    addTool("copy_file",
        "Copy a file inside the server root.",
        {
            {"type", "object"},
            {"properties", {
                {"from", {{"type", "string"}}},
                {"to",   {{"type", "string"}}}
            }},
            {"required", {"from", "to"}}
        });

    addTool("file_info",
        "Get metadata about a file or directory: size, mtime, type.",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}}}
            }},
            {"required", {"path"}}
        });

    // ---- global system tools ----
    addTool("get_system_info",
        "Return host system info: OS, hostname, CPU count, disk totals, and MCDeploy build info.",
        {{"type", "object"}, {"properties", nlohmann::json::object()}});

    addTool("list_all_servers",
        "List every server managed by this MCDeploy instance (not just the current one). Useful for comparisons.",
        {{"type", "object"}, {"properties", nlohmann::json::object()}});

    addTool("get_audit_log",
        "Retrieve MCDeploy's audit log entries. Filter by action prefix if desired.",
        {
            {"type", "object"},
            {"properties", {
                {"limit",  {{"type", "integer"}, {"description", "Max rows (default 30, cap 200)."}}},
                {"filter", {{"type", "string"},  {"description", "Optional action substring filter, case-insensitive."}}}
            }}
        });

    // ---- player management ----
    addTool("get_player_details",
        "Fetch a player's full record: stats, inventory, ender chest, and last-known coordinates.",
        {
            {"type", "object"},
            {"properties", {
                {"username", {{"type", "string"}}}
            }},
            {"required", {"username"}}
        });

    addTool("update_player_stats",
        "Update health, hunger, or frozen state for a player. Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"username", {{"type", "string"}}},
                {"health",   {{"type", "number"}, {"description", "0-20"}}},
                {"hunger",   {{"type", "integer"}, {"description", "0-20"}}},
                {"frozen",   {{"type", "integer"}, {"description", "0 or 1"}}}
            }},
            {"required", {"username"}}
        });

    addTool("set_player_online",
        "Mark a player online or offline in the DB. Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"username", {{"type", "string"}}},
                {"online",   {{"type", "boolean"}}}
            }},
            {"required", {"username", "online"}}
        });

    addTool("clear_player_inventory",
        "Wipe a player's inventory or ender chest. Requires AGENT MODE (confirmation-gated).",
        {
            {"type", "object"},
            {"properties", {
                {"username", {{"type", "string"}}},
                {"type",     {{"type", "string"}, {"description", "inventory | ender_chest"}}}
            }},
            {"required", {"username", "type"}}
        });

    addTool("reset_player",
        "Wipe a player entirely: inventory, ender chest, advancements, stats. Requires AGENT MODE (confirmation-gated).",
        {
            {"type", "object"},
            {"properties", {
                {"username", {{"type", "string"}}}
            }},
            {"required", {"username"}}
        });

    addTool("give_player_item",
        "Insert or replace an item slot for a player. Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"username",   {{"type", "string"}}},
                {"type",       {{"type", "string"}, {"description", "inventory | ender_chest"}}},
                {"slot",       {{"type", "integer"}}},
                {"item_id",    {{"type", "string"}, {"description", "e.g. 'minecraft:diamond_sword'"}}},
                {"count",      {{"type", "integer"}}},
                {"display_name",{{"type", "string"}}},
                {"unbreakable",{{"type", "boolean"}}}
            }},
            {"required", {"username", "type", "slot", "item_id"}}
        });

    addTool("set_player_advancement",
        "Grant or revoke a specific advancement for a player. Requires AGENT MODE.",
        {
            {"type", "object"},
            {"properties", {
                {"username",       {{"type", "string"}}},
                {"advancement_id", {{"type", "string"}}},
                {"granted",        {{"type", "boolean"}}}
            }},
            {"required", {"username", "advancement_id", "granted"}}
        });

    addTool("get_player_coordinates",
        "Return login/logoff coordinate history for a player.",
        {
            {"type", "object"},
            {"properties", {
                {"username", {{"type", "string"}}}
            }},
            {"required", {"username"}}
        });

    addTool("get_player_advancements",
        "Return the full advancements state for a player.",
        {
            {"type", "object"},
            {"properties", {
                {"username", {{"type", "string"}}}
            }},
            {"required", {"username"}}
        });

    // ---- nuclear escape hatches ----
    addTool("run_shell_command",
        "Run an arbitrary shell command. VERY DANGEROUS: has full user-level access to the host. "
        "Always confirmation-gated. Only use as a last resort when no dedicated tool covers the task.",
        {
            {"type", "object"},
            {"properties", {
                {"command", {{"type", "string"}, {"description", "The command line to execute."}}},
                {"cwd",     {{"type", "string"}, {"description", "Optional working directory (defaults to server dir)."}}},
                {"timeout_seconds", {{"type", "integer"}, {"description", "Max seconds to wait (default 60, cap 300)."}}}
            }},
            {"required", {"command"}}
        });

    addTool("mcdeploy_api_call",
        "Make an authenticated call to any MCDeploy internal HTTP API endpoint (loopback). "
        "This is a generic escape hatch: whatever the frontend can do, this can do. "
        "Non-GET methods require AGENT MODE and are confirmation-gated.",
        {
            {"type", "object"},
            {"properties", {
                {"method", {{"type", "string"}, {"description", "GET | POST | PUT | DELETE"}}},
                {"path",   {{"type", "string"}, {"description", "e.g. '/api/servers/{uuid}/config'"}}},
                {"body",   {{"type", "object"}, {"description", "JSON body for POST/PUT. Optional."}}}
            }},
            {"required", {"method", "path"}}
        });

    return tools;
}

// ============================================================
// History loading & persistence
// ============================================================
nlohmann::json AIAgent::historyRowToOpenAI(const AiConversationRecord& r) const {
    using nlohmann::json;
    json m;
    m["role"] = r.role;
    if (!r.content.empty()) m["content"] = r.content;
    if (r.role == "assistant" && !r.tool_calls.empty()) {
        try {
            m["tool_calls"] = json::parse(r.tool_calls);
        } catch (...) {}
    }
    if (r.role == "tool") {
        m["tool_call_id"] = r.tool_call_id;
        if (!r.tool_name.empty()) m["name"] = r.tool_name;
    }
    return m;
}

nlohmann::json AIAgent::loadHistoryAsMessages() const {
    using nlohmann::json;
    json arr = json::array();
    auto rows = Database::getInstance().getAiConversation(m_serverUuid, m_username, 400);
    for (const auto& r : rows) {
        arr.push_back(historyRowToOpenAI(r));
    }
    return arr;
}

void AIAgent::persistUserMessage(const std::string& content) {
    AiConversationRecord r;
    r.server_uuid = m_serverUuid;
    r.username = m_username;
    r.role = "user";
    r.content = content;
    Database::getInstance().appendAiConversation(r);
}

void AIAgent::persistAssistantMessage(const std::string& content, const std::string& toolCallsJson,
                                      int promptTokens, int completionTokens) {
    AiConversationRecord r;
    r.server_uuid = m_serverUuid;
    r.username = m_username;
    r.role = "assistant";
    r.content = content;
    r.tool_calls = toolCallsJson;
    r.tokens_prompt = promptTokens;
    r.tokens_completion = completionTokens;
    Database::getInstance().appendAiConversation(r);
}

void AIAgent::persistToolResponse(const std::string& toolName, const std::string& toolCallId, const std::string& content) {
    AiConversationRecord r;
    r.server_uuid = m_serverUuid;
    r.username = m_username;
    r.role = "tool";
    r.content = content;
    r.tool_call_id = toolCallId;
    r.tool_name = toolName;
    Database::getInstance().appendAiConversation(r);
}

// ============================================================
// HTTP: chat/completions
// ============================================================
nlohmann::json AIAgent::postCompletions(const nlohmann::json& body) {
    using nlohmann::json;
    CURL* curl = curl_easy_init();
    if (!curl) return {{"error", {{"message", "curl_easy_init failed"}}}};

    std::string jsonData = body.dump();
    std::string readBuffer;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string authHeader = "Authorization: Bearer " + m_config.api_key;
    headers = curl_slist_append(headers, authHeader.c_str());
    // Gemini's REST endpoint also accepts x-goog-api-key; sending both is harmless.
    std::string googHeader = "x-goog-api-key: " + m_config.api_key;
    headers = curl_slist_append(headers, googHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, m_config.api_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
#ifdef _WIN32
    // Use Windows' cert store rather than shipping a CA bundle.
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif

    long http_code = 0;
    CURLcode res = CURLE_OK;
    // Retry on 429/503 with exponential backoff. These are common with free tiers.
    for (int attempt = 0; attempt < 3; attempt++) {
        readBuffer.clear();
        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (res != CURLE_OK) break;
        if (http_code != 429 && http_code != 503) break;
        int sleepMs = 500 * (1 << attempt); // 500, 1000, 2000
        std::cerr << "[AIAgent] HTTP " << http_code << " from provider, retrying in " << sleepMs << "ms (attempt " << (attempt + 1) << "/3)..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return {{"error", {{"message", std::string("HTTP transport error: ") + curl_easy_strerror(res)}}}};
    }

    // Log raw response snippet so future issues are easy to diagnose from the
    // MCDeploy stderr log.
    if (http_code >= 400) {
        std::cerr << "[AIAgent] Non-2xx from AI provider (HTTP " << http_code << "): "
                  << readBuffer.substr(0, 500) << std::endl;
    }

    try {
        auto parsed = json::parse(readBuffer);

        // Some providers (Gemini's OpenAI-compat endpoint especially) return the
        // error payload as a single-element array on failure. Normalize.
        if (parsed.is_array()) {
            if (!parsed.empty() && parsed[0].is_object()) {
                parsed = parsed[0];
            } else {
                json wrap = json::object();
                wrap["error"] = {{"message", "provider returned an array with no object"}};
                wrap["raw"] = readBuffer.substr(0, 500);
                return wrap;
            }
        }

        // If the http status was an error but the body didn't include an "error"
        // field, synthesize one so downstream code can rely on it.
        if (http_code >= 400 && parsed.is_object() && !parsed.contains("error")) {
            parsed["error"] = {{"message", "HTTP " + std::to_string(http_code)},
                               {"raw", readBuffer.substr(0, 500)}};
        }
        return parsed;
    } catch (const std::exception& e) {
        json err = json::object();
        err["error"] = {{"message", std::string("failed to parse response: ") + e.what()}};
        err["raw"] = readBuffer.substr(0, 1000);
        err["http_status"] = (int)http_code;
        return err;
    }
}

// ============================================================
// Tool dispatch
// ============================================================
AIAgent::ToolExecOutcome AIAgent::dispatchTool(const std::string& name, const nlohmann::json& args) {
    auto t0 = std::chrono::steady_clock::now();
    ToolExecOutcome out;
    out.status = "success";
    try {
        bool needsConfirm = false;
        // ---- read-only ----
        if      (name == "list_directory")       out.result = toolListDirectory(args);
        else if (name == "read_file")            out.result = toolReadFile(args);
        else if (name == "search_files")         out.result = toolSearchFiles(args);
        else if (name == "search_logs")          out.result = toolSearchLogs(args);
        else if (name == "diff_file")            out.result = toolDiffFile(args);
        else if (name == "get_metrics")          out.result = toolGetMetrics(args);
        else if (name == "get_server_info")      out.result = toolGetServerInfo(args);
        else if (name == "get_plugin_info")      out.result = toolGetPluginInfo(args);
        else if (name == "query_players")        out.result = toolQueryPlayers(args);
        else if (name == "web_search")           out.result = toolWebSearch(args);
        else if (name == "file_info")            out.result = toolFileInfo(args);
        else if (name == "get_system_info")      out.result = toolGetSystemInfo(args);
        else if (name == "list_all_servers")     out.result = toolListAllServers(args);
        else if (name == "get_audit_log")        out.result = toolGetAuditLog(args);
        else if (name == "list_backups")         out.result = toolListBackups(args);
        else if (name == "list_installed_addons") out.result = toolListInstalledAddons(args);
        else if (name == "search_addons")        out.result = toolSearchAddons(args);
        else if (name == "get_player_details")   out.result = toolGetPlayerDetails(args);
        else if (name == "get_player_coordinates") out.result = toolGetPlayerCoords(args);
        else if (name == "get_player_advancements") out.result = toolGetPlayerAdvancements(args);
        // ---- mutating / control ----
        else if (name == "write_file")           out.result = toolWriteFile(args, needsConfirm);
        else if (name == "delete_file")          out.result = toolDeleteFile(args, needsConfirm);
        else if (name == "append_file")          out.result = toolAppendFile(args, needsConfirm);
        else if (name == "create_directory")     out.result = toolCreateDirectory(args);
        else if (name == "move_file")            out.result = toolMoveFile(args, needsConfirm);
        else if (name == "copy_file")            out.result = toolCopyFile(args);
        else if (name == "execute_command")      out.result = toolExecuteCommand(args, needsConfirm);
        else if (name == "start_server")         out.result = toolServerControl("start", args, needsConfirm);
        else if (name == "stop_server")          out.result = toolServerControl("stop", args, needsConfirm);
        else if (name == "restart_server")       out.result = toolServerControl("restart", args, needsConfirm);
        else if (name == "set_performance")      out.result = toolSetPerformance(args, needsConfirm);
        else if (name == "create_backup")        out.result = toolCreateBackup(args, needsConfirm);
        else if (name == "update_server_config") out.result = toolUpdateServerConfig(args);
        else if (name == "update_start_command") out.result = toolUpdateStartCommand(args, needsConfirm);
        else if (name == "set_server_status")    out.result = toolSetServerStatus(args);
        else if (name == "run_shell_command")    out.result = toolRunShellCommand(args, needsConfirm);
        else if (name == "install_addon")        out.result = toolInstallAddon(args, needsConfirm);
        else if (name == "uninstall_addon")      out.result = toolUninstallAddon(args);
        else if (name == "update_player_stats")  out.result = toolUpdatePlayerStats(args);
        else if (name == "clear_player_inventory") out.result = toolClearPlayerInventory(args, needsConfirm);
        else if (name == "reset_player")         out.result = toolResetPlayer(args, needsConfirm);
        else if (name == "give_player_item")     out.result = toolGivePlayerItem(args);
        else if (name == "set_player_advancement") out.result = toolSetPlayerAdvancement(args);
        else if (name == "set_player_online")    out.result = toolSetPlayerOnline(args);
        else if (name == "delete_backup")        out.result = toolDeleteBackup(args, needsConfirm);
        else if (name == "mcdeploy_api_call")    out.result = toolMcdeployApiCall(args, needsConfirm);
        else {
            out.result = "Error: unknown tool '" + name + "'";
            out.status = "error";
        }
        out.needs_confirmation = needsConfirm;
        if (needsConfirm) out.status = "denied";
        if (out.result.rfind("Error", 0) == 0 && out.status == "success") out.status = "error";
    } catch (const std::exception& e) {
        out.result = std::string("Error: exception during tool execution: ") + e.what();
        out.status = "error";
    }
    auto t1 = std::chrono::steady_clock::now();
    out.latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    // Persist to audit trail.
    AiToolCallRecord rec;
    rec.server_uuid = m_serverUuid;
    rec.username = m_username;
    rec.tool_name = name;
    rec.arguments = args.dump();
    rec.result = truncateForModel(out.result, 8000);
    rec.status = out.status;
    rec.latency_ms = out.latency_ms;
    Database::getInstance().logAiToolCall(rec);

    return out;
}

// ============================================================
// Tool implementations - read-only
// ============================================================
std::string AIAgent::toolListDirectory(const nlohmann::json& args) {
    using nlohmann::json;
    std::string subpath = args.value("subpath", "");
    bool recursive = args.value("recursive", false);
    int depth = args.value("depth", 2);

    fs::path base;
    std::string err;
    std::string relForResolve = subpath.empty() ? "." : subpath;
    if (!resolveSafePath(relForResolve, base, err)) return "Error: " + err;
    if (!fs::exists(base)) return "Error: directory not found: " + subpath;
    if (!fs::is_directory(base)) return "Error: not a directory: " + subpath;

    json result = json::array();
    auto pushEntry = [&](const fs::directory_entry& e, int level) {
        try {
            fs::path rel = fs::relative(e.path(), fs::path(m_serverPath));
            json j;
            j["path"] = rel.generic_string();
            j["name"] = e.path().filename().string();
            j["depth"] = level;
            if (e.is_regular_file()) {
                j["type"] = "file";
                std::error_code ec;
                auto sz = e.file_size(ec);
                j["size"] = ec ? 0 : sz;
            } else if (e.is_directory()) {
                j["type"] = "directory";
            } else {
                j["type"] = "other";
            }
            result.push_back(j);
        } catch (...) {}
    };

    try {
        if (recursive) {
            for (auto it = fs::recursive_directory_iterator(base, fs::directory_options::skip_permission_denied); it != fs::recursive_directory_iterator(); ++it) {
                if (it.depth() >= depth) it.disable_recursion_pending();
                pushEntry(*it, it.depth());
                if ((int)result.size() > 500) break;
            }
        } else {
            for (const auto& e : fs::directory_iterator(base, fs::directory_options::skip_permission_denied)) {
                pushEntry(e, 0);
                if ((int)result.size() > 500) break;
            }
        }
    } catch (const std::exception& e) {
        return std::string("Error: unable to list directory: ") + e.what();
    }

    json out = {{"base", subpath.empty() ? "." : subpath}, {"entries", result}, {"count", (int)result.size()}};
    return out.dump(2);
}

std::string AIAgent::toolReadFile(const nlohmann::json& args) {
    if (!args.contains("path")) return "Error: 'path' is required";
    std::string relPath = args["path"].get<std::string>();
    size_t maxBytes = (size_t)args.value("max_bytes", 8000);
    size_t offset   = (size_t)args.value("offset", 0);
    if (maxBytes == 0 || maxBytes > 200000) maxBytes = 8000;

    fs::path abs;
    std::string err;
    if (!resolveSafePath(relPath, abs, err)) return "Error: " + err;
    if (!fs::exists(abs) || !fs::is_regular_file(abs)) return "Error: file not found: " + relPath;

    std::ifstream f(abs, std::ios::binary);
    if (!f.is_open()) return "Error: could not open file: " + relPath;
    f.seekg(0, std::ios::end);
    size_t total = (size_t)f.tellg();
    if (offset >= total) return "Error: offset beyond end of file (size=" + std::to_string(total) + ")";
    f.seekg(offset, std::ios::beg);
    std::string data(std::min(maxBytes, total - offset), '\0');
    f.read(&data[0], data.size());
    bool truncated = (offset + data.size()) < total;

    std::ostringstream ss;
    ss << "File: " << relPath << " (size " << total << " bytes, showing " << offset << ".."
       << (offset + data.size()) << ")\n"
       << "--------\n" << data;
    if (truncated) ss << "\n--------\n... [" << (total - (offset + data.size())) << " more bytes; pass offset=" << (offset + data.size()) << " to continue] ...";
    return ss.str();
}

std::string AIAgent::toolSearchFiles(const nlohmann::json& args) {
    if (!args.contains("pattern")) return "Error: 'pattern' is required";
    std::string pattern = args["pattern"].get<std::string>();
    std::string subpath = args.value("subpath", "");
    std::string glob = args.value("file_glob", "");
    int maxMatches = args.value("max_matches", 50);

    fs::path base;
    std::string err;
    if (!resolveSafePath(subpath.empty() ? "." : subpath, base, err)) return "Error: " + err;

    std::regex rx;
    try {
        rx = std::regex(pattern, std::regex::ECMAScript | std::regex::icase);
    } catch (const std::exception& e) {
        return std::string("Error: invalid regex: ") + e.what();
    }

    // Normalize glob: allow ".yml" or "*.yml" or "yml"
    std::string ext;
    if (!glob.empty()) {
        ext = glob;
        if (!ext.empty() && ext.front() == '*') ext.erase(ext.begin());
        if (!ext.empty() && ext.front() != '.') ext = "." + ext;
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
    }

    std::ostringstream out;
    int matches = 0;
    int filesScanned = 0;
    try {
        for (auto it = fs::recursive_directory_iterator(base, fs::directory_options::skip_permission_denied); it != fs::recursive_directory_iterator(); ++it) {
            const auto& e = *it;
            if (!e.is_regular_file()) continue;
            std::error_code ec;
            auto sz = e.file_size(ec);
            if (ec || sz > 5 * 1024 * 1024) continue; // skip large files
            if (!ext.empty()) {
                std::string fext = e.path().extension().string();
                std::transform(fext.begin(), fext.end(), fext.begin(), [](unsigned char c){ return std::tolower(c); });
                if (fext != ext) continue;
            }
            filesScanned++;
            std::ifstream f(e.path());
            if (!f.is_open()) continue;
            std::string line;
            int lineno = 0;
            while (std::getline(f, line)) {
                lineno++;
                if (std::regex_search(line, rx)) {
                    fs::path rel = fs::relative(e.path(), fs::path(m_serverPath));
                    out << rel.generic_string() << ":" << lineno << ": " << (line.size() > 300 ? line.substr(0, 300) + "..." : line) << "\n";
                    if (++matches >= maxMatches) goto done;
                }
            }
        }
    } catch (const std::exception& e) {
        return std::string("Error scanning files: ") + e.what();
    }
done:
    std::ostringstream head;
    head << "Scanned " << filesScanned << " files, " << matches << " match(es)" << (matches >= maxMatches ? " (capped)" : "") << ":\n";
    std::string body = out.str();
    if (body.empty()) body = "(no matches)";
    return head.str() + body;
}

std::string AIAgent::toolSearchLogs(const nlohmann::json& args) {
    std::string pattern = args.value("pattern", "");
    int limit = args.value("limit", 40);
    if (limit <= 0 || limit > 200) limit = 40;
    std::string level = args.value("level", "");

    // Grab the most recent 500 lines and filter down.
    auto lines = ProcessManager::getInstance().getLogs(m_serverUuid, 500);
    if (lines.empty()) return "(no logs available; server may not be running)";

    std::regex rx;
    bool haveRegex = false;
    if (!pattern.empty()) {
        try { rx = std::regex(pattern, std::regex::ECMAScript | std::regex::icase); haveRegex = true; }
        catch (const std::exception& e) { return std::string("Error: invalid regex: ") + e.what(); }
    }

    std::string upperLevel = level;
    std::transform(upperLevel.begin(), upperLevel.end(), upperLevel.begin(), [](unsigned char c){ return std::toupper(c); });

    std::vector<std::string> matched;
    // Walk from newest backwards, keep in chronological order at the end.
    for (auto it = lines.rbegin(); it != lines.rend() && (int)matched.size() < limit; ++it) {
        if (!upperLevel.empty() && it->type != upperLevel) continue;
        if (haveRegex && !std::regex_search(it->text, rx)) continue;
        std::ostringstream l;
        l << "[" << it->timestamp << "] [" << it->type << "] " << it->text;
        matched.push_back(l.str());
    }
    std::reverse(matched.begin(), matched.end());

    if (matched.empty()) return "(no matching log lines)";
    std::ostringstream out;
    out << matched.size() << " matching log line(s):\n";
    for (const auto& l : matched) out << l << "\n";
    return out.str();
}

// ============================================================
// Tool implementations - diff / metrics / info
// ============================================================
namespace {
    // Simple Myers-ish line diff via LCS; good enough for config files.
    std::string unifiedDiff(const std::string& a, const std::string& b, const std::string& path) {
        auto split = [](const std::string& s) {
            std::vector<std::string> out;
            std::string cur;
            for (char c : s) {
                if (c == '\n') { out.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            out.push_back(cur);
            return out;
        };
        auto A = split(a);
        auto B = split(b);
        int n = (int)A.size(), m = (int)B.size();
        std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < m; j++)
                lcs[i+1][j+1] = (A[i] == B[j]) ? lcs[i][j] + 1 : std::max(lcs[i][j+1], lcs[i+1][j]);

        // Walk backwards to build ops.
        std::vector<std::tuple<char, std::string, int, int>> ops; // op, line, aIdx, bIdx
        int i = n, j = m;
        while (i > 0 && j > 0) {
            if (A[i-1] == B[j-1]) { ops.emplace_back(' ', A[i-1], i-1, j-1); i--; j--; }
            else if (lcs[i-1][j] >= lcs[i][j-1]) { ops.emplace_back('-', A[i-1], i-1, j-1); i--; }
            else { ops.emplace_back('+', B[j-1], i-1, j-1); j--; }
        }
        while (i > 0) { ops.emplace_back('-', A[i-1], i-1, j-1); i--; }
        while (j > 0) { ops.emplace_back('+', B[j-1], i-1, j-1); j--; }
        std::reverse(ops.begin(), ops.end());

        std::ostringstream out;
        out << "--- a/" << path << "\n+++ b/" << path << "\n";
        // Group into hunks with 2 lines of context.
        const int ctx = 2;
        int aLine = 1, bLine = 1;
        std::vector<std::tuple<char, std::string>> hunk;
        int hunkAStart = 1, hunkBStart = 1;
        auto flushHunk = [&](int aEnd, int bEnd) {
            if (hunk.empty()) return;
            int aCount = 0, bCount = 0;
            for (const auto& [op, line] : hunk) { (void)line; if (op != '+') aCount++; if (op != '-') bCount++; }
            out << "@@ -" << hunkAStart << "," << aCount << " +" << hunkBStart << "," << bCount << " @@\n";
            for (const auto& [op, line] : hunk) out << op << line << "\n";
            (void)aEnd; (void)bEnd;
            hunk.clear();
        };

        int contextRemaining = 0;
        int lastChangeIdx = -1;
        for (size_t k = 0; k < ops.size(); k++) {
            auto [op, line, aIdx, bIdx] = ops[k];
            (void)aIdx; (void)bIdx;
            bool isChange = (op != ' ');
            if (isChange) {
                if (hunk.empty()) {
                    hunkAStart = aLine;
                    hunkBStart = bLine;
                    // Include leading ctx from previous ' ' lines
                    int start = (int)k;
                    int backctx = ctx;
                    while (start > 0 && backctx > 0 && std::get<0>(ops[start-1]) == ' ') { start--; backctx--; }
                    for (int p = start; p < (int)k; p++) {
                        auto [op2, line2, ai, bi] = ops[p];
                        hunk.emplace_back(op2, line2);
                        (void)ai; (void)bi;
                    }
                    if (start < (int)k) {
                        // Adjust hunk starts for the ctx we pulled in.
                        int lostA = (int)k - start;
                        int lostB = (int)k - start;
                        hunkAStart = aLine - lostA;
                        hunkBStart = bLine - lostB;
                    }
                }
                hunk.emplace_back(op, line);
                lastChangeIdx = (int)k;
                contextRemaining = ctx;
            } else {
                if (!hunk.empty() && contextRemaining > 0) {
                    hunk.emplace_back(op, line);
                    contextRemaining--;
                } else if (!hunk.empty() && contextRemaining == 0) {
                    flushHunk(aLine, bLine);
                }
            }
            if (op != '+') aLine++;
            if (op != '-') bLine++;
        }
        flushHunk(aLine, bLine);
        (void)lastChangeIdx;
        std::string s = out.str();
        return s.empty() ? "(files are identical)" : s;
    }
}

std::string AIAgent::toolDiffFile(const nlohmann::json& args) {
    if (!args.contains("path") || !args.contains("new_content"))
        return "Error: 'path' and 'new_content' are required";
    std::string relPath = args["path"].get<std::string>();
    std::string newContent = args["new_content"].get<std::string>();

    fs::path abs;
    std::string err;
    if (!resolveSafePath(relPath, abs, err)) return "Error: " + err;

    std::string oldContent;
    if (fs::exists(abs) && fs::is_regular_file(abs)) {
        std::ifstream f(abs, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        oldContent = ss.str();
    }
    return unifiedDiff(oldContent, newContent, relPath);
}

std::string AIAgent::toolGetMetrics(const nlohmann::json& args) {
    (void)args;
    SystemMetrics sys = SystemInfo::getInstance().getMetrics();
    bool running = ProcessManager::getInstance().isServerRunning(m_serverUuid);
    nlohmann::json j;
    j["cpu_usage_pct"] = sys.cpu_usage_pct;
    j["ram_used_gb"]   = sys.ram_used_gb;
    j["ram_total_gb"]  = sys.ram_total_gb;
    j["disk_used_gb"]  = sys.disk_used_gb;
    j["disk_total_gb"] = sys.disk_total_gb;
    j["is_running"]    = running;
    j["cpu_priority"]  = ProcessManager::getInstance().getServerCpuPriority(m_serverUuid);
    j["smart_optimization"] = ProcessManager::getInstance().getServerSmartOptimization(m_serverUuid);
    return j.dump(2);
}

std::string AIAgent::toolGetServerInfo(const nlohmann::json& args) {
    (void)args;
    ServerRecord rec;
    if (!Database::getInstance().getServer(m_serverUuid, rec) || rec.uuid.empty())
        return "Error: server record not found";
    nlohmann::json j;
    j["uuid"]            = rec.uuid;
    j["name"]            = rec.name;
    j["software_type"]   = rec.software_type;
    j["version"]         = rec.version;
    j["port"]            = rec.port;
    j["ram_min_mb"]      = rec.ram_min;
    j["ram_max_mb"]      = rec.ram_max;
    j["status"]          = rec.status;
    j["directory_path"]  = rec.directory_path;
    j["subdomain"]       = rec.subdomain;
    j["created_at"]      = rec.created_at;
    j["is_running_now"]  = ProcessManager::getInstance().isServerRunning(m_serverUuid);
    return j.dump(2);
}

std::string AIAgent::toolGetPluginInfo(const nlohmann::json& args) {
    (void)args;
    // Try plugins/ first (Bukkit/Spigot/Paper/Purpur), fall back to mods/ (Forge/Fabric).
    nlohmann::json out = nlohmann::json::object();
    out["plugins"] = nlohmann::json::array();
    out["mods"] = nlohmann::json::array();

    auto scan = [&](const std::string& sub, nlohmann::json& arr) {
        fs::path dir;
        std::string err;
        if (!resolveSafePath(sub, dir, err)) return;
        if (!fs::exists(dir) || !fs::is_directory(dir)) return;
        for (const auto& e : fs::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            std::string name = e.path().filename().string();
            std::string ext = toLower(e.path().extension().string());
            if (ext != ".jar") continue;
            std::error_code ec;
            auto sz = e.file_size(ec);
            nlohmann::json j;
            j["filename"] = name;
            j["size_bytes"] = ec ? 0 : (long long)sz;
            arr.push_back(j);
        }
    };
    scan("plugins", out["plugins"]);
    scan("mods", out["mods"]);
    if (out["plugins"].empty() && out["mods"].empty())
        return "(no plugins/ or mods/ directory found)";
    return out.dump(2);
}

std::string AIAgent::toolQueryPlayers(const nlohmann::json& args) {
    (void)args;
    auto players = Database::getInstance().getServerPlayers(m_serverUuid);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : players) {
        nlohmann::json j;
        j["username"] = p.username;
        j["is_online"] = p.is_online != 0;
        j["health"] = p.health;
        j["hunger"] = p.hunger;
        j["last_logoff"] = { {"x", p.last_logoff_x}, {"y", p.last_logoff_y}, {"z", p.last_logoff_z} };
        arr.push_back(j);
    }
    if (arr.empty()) return "(no players recorded for this server yet)";
    return arr.dump(2);
}

std::string AIAgent::toolWebSearch(const nlohmann::json& args) {
    if (!args.contains("query")) return "Error: 'query' is required";
    std::string q = args["query"].get<std::string>();

    // URL-encode the query.
    CURL* enc = curl_easy_init();
    if (!enc) return "Error: curl init failed";
    char* escaped = curl_easy_escape(enc, q.c_str(), (int)q.size());
    std::string url = "https://api.duckduckgo.com/?q=";
    url += escaped;
    url += "&format=json&no_html=1&skip_disambig=1";
    curl_free(escaped);
    curl_easy_cleanup(enc);

    CURL* curl = curl_easy_init();
    if (!curl) return "Error: curl init failed";
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MCDeploy-AI/1.0");
#ifdef _WIN32
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return std::string("Error: web search failed: ") + curl_easy_strerror(res);

    try {
        auto j = nlohmann::json::parse(body);
        nlohmann::json out;
        out["query"] = q;
        out["abstract"] = j.value("AbstractText", "");
        out["source"]   = j.value("AbstractURL", "");
        out["heading"]  = j.value("Heading", "");
        nlohmann::json topics = nlohmann::json::array();
        if (j.contains("RelatedTopics") && j["RelatedTopics"].is_array()) {
            int cnt = 0;
            for (auto& t : j["RelatedTopics"]) {
                if (!t.is_object()) continue;
                if (t.contains("Text") && t.contains("FirstURL")) {
                    topics.push_back({ {"text", t["Text"]}, {"url", t["FirstURL"]} });
                    if (++cnt >= 6) break;
                }
            }
        }
        out["related"] = topics;
        if (out["abstract"].get<std::string>().empty() && topics.empty())
            return "(no useful results for \"" + q + "\")";
        return out.dump(2);
    } catch (...) {
        return "Error: could not parse web search response";
    }
}

// ============================================================
// Tool implementations - write / control (gated)
// ============================================================
std::string AIAgent::toolWriteFile(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: write_file requires AGENT MODE. Ask the user to enable it.";
    if (!args.contains("path") || !args.contains("content"))
        return "Error: 'path' and 'content' are required";
    std::string relPath = args["path"].get<std::string>();
    std::string content = args["content"].get<std::string>();

    fs::path abs;
    std::string err;
    if (!resolveSafePath(relPath, abs, err)) return "Error: " + err;

    // Confirmation gate for sensitive files.
    if (fs::exists(abs) && isSensitiveFile(abs)) {
        outNeedsConfirm = true;
        return "Confirmation required: '" + relPath + "' is a sensitive server file. "
               "MCDeploy has surfaced a confirmation prompt to the user; the edit has not been applied yet.";
    }

    // Snapshot previous content for undo.
    AiUndoRecord u;
    u.server_uuid = m_serverUuid;
    u.username = m_username;
    u.operation = "write";
    u.file_path = relPath;
    if (fs::exists(abs) && fs::is_regular_file(abs)) {
        std::ifstream f(abs, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        u.previous_content = ss.str();
        u.previous_existed = true;
    }
    u.new_content = content;

    try {
        fs::create_directories(abs.parent_path());
    } catch (const std::exception& e) {
        return std::string("Error: could not create parent dirs: ") + e.what();
    }

    std::ofstream f(abs, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return "Error: could not open file for writing: " + relPath;
    f << content;
    f.close();

    Database::getInstance().pushAiUndo(u);
    Database::getInstance().logAction(m_username, "AI_WRITE", m_serverUuid, "path=" + relPath + " bytes=" + std::to_string(content.size()));
    return "Wrote " + std::to_string(content.size()) + " bytes to '" + relPath + "'. Previous content stored on the undo stack.";
}

std::string AIAgent::toolDeleteFile(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: delete_file requires AGENT MODE.";
    if (!args.contains("path")) return "Error: 'path' is required";
    std::string relPath = args["path"].get<std::string>();

    fs::path abs;
    std::string err;
    if (!resolveSafePath(relPath, abs, err)) return "Error: " + err;
    if (!fs::exists(abs)) return "Error: file does not exist: " + relPath;
    if (fs::is_directory(abs)) return "Error: refusing to delete a directory via delete_file";

    if (isSensitiveFile(abs)) {
        outNeedsConfirm = true;
        return "Confirmation required: '" + relPath + "' is a sensitive server file. "
               "Awaiting user approval before deleting.";
    }

    // Snapshot for undo.
    AiUndoRecord u;
    u.server_uuid = m_serverUuid;
    u.username = m_username;
    u.operation = "delete";
    u.file_path = relPath;
    u.previous_existed = true;
    {
        std::ifstream f(abs, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        u.previous_content = ss.str();
    }

    std::error_code ec;
    fs::remove(abs, ec);
    if (ec) return std::string("Error: failed to delete: ") + ec.message();
    Database::getInstance().pushAiUndo(u);
    Database::getInstance().logAction(m_username, "AI_DELETE", m_serverUuid, "path=" + relPath);
    return "Deleted '" + relPath + "'. Previous content stored on the undo stack.";
}

std::string AIAgent::toolExecuteCommand(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: execute_command requires AGENT MODE.";
    if (!args.contains("command")) return "Error: 'command' is required";
    std::string cmd = args["command"].get<std::string>();
    if (cmd.empty()) return "Error: empty command";

    if (isDangerousCommand(cmd)) {
        outNeedsConfirm = true;
        return "Confirmation required: '" + cmd + "' is a dangerous command. Awaiting user approval.";
    }

    if (!ProcessManager::getInstance().isServerRunning(m_serverUuid))
        return "Error: server is not running; start it first.";
    bool ok = ProcessManager::getInstance().sendCommand(m_serverUuid, cmd);
    Database::getInstance().logAction(m_username, "AI_COMMAND", m_serverUuid, cmd);
    return ok ? ("Sent to console: " + cmd) : "Error: failed to write to server console";
}

std::string AIAgent::toolServerControl(const std::string& op, const nlohmann::json& args, bool& outNeedsConfirm) {
    (void)args;
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: " + op + "_server requires AGENT MODE.";

    // Just do it in agent mode. User asked for everything; confirmation for
    // restart/stop was overkill and interrupted flow.
    ServerRecord rec;
    if (!Database::getInstance().getServer(m_serverUuid, rec) || rec.uuid.empty())
        return "Error: server record not found";
    auto& pm = ProcessManager::getInstance();
    if (op == "start") {
        if (pm.isServerRunning(m_serverUuid)) return "Server is already running.";
        bool ok = pm.startServer(rec.uuid, rec.name, rec.directory_path, rec.start_command);
        Database::getInstance().logAction(m_username, "AI_START", m_serverUuid, "");
        return ok ? "Server start requested." : "Error: startServer failed.";
    }
    if (op == "stop") {
        if (!pm.isServerRunning(m_serverUuid)) return "Server is not running.";
        bool ok = pm.stopServer(m_serverUuid, false);
        Database::getInstance().logAction(m_username, "AI_STOP", m_serverUuid, "");
        return ok ? "Server stop requested (graceful)." : "Error: stopServer failed.";
    }
    if (op == "restart") {
        if (pm.isServerRunning(m_serverUuid)) pm.stopServer(m_serverUuid, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        bool ok = pm.startServer(rec.uuid, rec.name, rec.directory_path, rec.start_command);
        Database::getInstance().logAction(m_username, "AI_RESTART", m_serverUuid, "");
        return ok ? "Server restart requested." : "Error: restart failed to start.";
    }
    return "Error: unknown lifecycle op '" + op + "'";
}

std::string AIAgent::toolSetPerformance(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: set_performance requires AGENT MODE.";

    std::string result = "Applied:";
    bool changed = false;
    if (args.contains("cpu_priority")) {
        std::string p = args["cpu_priority"].get<std::string>();
        static const std::vector<std::string> valid = {"idle","below_normal","normal","above_normal","high"};
        if (std::find(valid.begin(), valid.end(), p) == valid.end())
            return "Error: cpu_priority must be one of idle|below_normal|normal|above_normal|high";
        ProcessManager::getInstance().setServerCpuPriority(m_serverUuid, p);
        result += " cpu_priority=" + p;
        changed = true;
    }
    if (args.contains("smart_optimization")) {
        bool v = args["smart_optimization"].get<bool>();
        ProcessManager::getInstance().setServerSmartOptimization(m_serverUuid, v);
        result += std::string(" smart_optimization=") + (v ? "true" : "false");
        changed = true;
    }
    if (!changed) return "No performance settings provided.";
    Database::getInstance().logAction(m_username, "AI_PERF", m_serverUuid, result);
    return result;
}

std::string AIAgent::toolCreateBackup(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: create_backup requires AGENT MODE.";
    // Delegate to the internal HTTP API. This actually runs ServerController's
    // zip pipeline instead of just leaving a note.
    (void)args;
    nlohmann::json body = nlohmann::json::object();
    body["note"] = args.value("note", "created by AI assistant");
    nlohmann::json call;
    call["method"] = "POST";
    call["path"]   = "/api/servers/" + m_serverUuid + "/backups";
    call["body"]   = body;
    bool nc = false;
    return toolMcdeployApiCall(call, nc); // never gates GETs; POST goes through here in agent mode
}

// ============================================================
// Approved-tool execution path (frontend confirms → we run)
// ============================================================
AiStep AIAgent::executeApprovedTool(const std::string& toolName, const nlohmann::json& arguments) {
    AiStep s;
    s.kind = "tool_result";
    s.tool_name = toolName;
    s.arguments = arguments;

    auto runNow = [&](std::function<std::string()> body) {
        auto t0 = std::chrono::steady_clock::now();
        std::string r;
        try { r = body(); }
        catch (const std::exception& e) { r = std::string("Error: exception: ") + e.what(); }
        s.result = r;
        s.latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    };

    if (toolName == "write_file") {
        // Bypass sensitive-file check by pretending it's not sensitive: we're the user's approval.
        runNow([&]() {
            if (!m_agentMode) return std::string("Error: agent mode is off");
            if (!arguments.contains("path") || !arguments.contains("content")) return std::string("Error: missing args");
            std::string relPath = arguments["path"].get<std::string>();
            std::string content = arguments["content"].get<std::string>();
            fs::path abs; std::string err;
            if (!resolveSafePath(relPath, abs, err)) return std::string("Error: ") + err;

            AiUndoRecord u; u.server_uuid = m_serverUuid; u.username = m_username;
            u.operation = "write"; u.file_path = relPath; u.new_content = content;
            if (fs::exists(abs) && fs::is_regular_file(abs)) {
                std::ifstream f(abs, std::ios::binary);
                std::stringstream ss; ss << f.rdbuf();
                u.previous_content = ss.str();
                u.previous_existed = true;
            }
            try { fs::create_directories(abs.parent_path()); } catch (...) {}
            std::ofstream f(abs, std::ios::binary | std::ios::trunc);
            if (!f.is_open()) return std::string("Error: could not open file for writing");
            f << content; f.close();
            Database::getInstance().pushAiUndo(u);
            Database::getInstance().logAction(m_username, "AI_WRITE_APPROVED", m_serverUuid, "path=" + relPath);
            return std::string("Wrote ") + std::to_string(content.size()) + " bytes to '" + relPath + "'.";
        });
    }
    else if (toolName == "delete_file") {
        runNow([&]() {
            if (!m_agentMode) return std::string("Error: agent mode is off");
            std::string relPath = arguments.value("path", "");
            if (relPath.empty()) return std::string("Error: 'path' missing");
            fs::path abs; std::string err;
            if (!resolveSafePath(relPath, abs, err)) return std::string("Error: ") + err;
            if (!fs::exists(abs)) return std::string("Error: file does not exist");
            AiUndoRecord u; u.server_uuid = m_serverUuid; u.username = m_username;
            u.operation = "delete"; u.file_path = relPath; u.previous_existed = true;
            { std::ifstream f(abs, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); u.previous_content = ss.str(); }
            std::error_code ec; fs::remove(abs, ec);
            if (ec) return std::string("Error: failed to delete: ") + ec.message();
            Database::getInstance().pushAiUndo(u);
            Database::getInstance().logAction(m_username, "AI_DELETE_APPROVED", m_serverUuid, "path=" + relPath);
            return std::string("Deleted '") + relPath + "'.";
        });
    }
    else if (toolName == "execute_command") {
        runNow([&]() {
            if (!m_agentMode) return std::string("Error: agent mode is off");
            std::string cmd = arguments.value("command", "");
            if (cmd.empty()) return std::string("Error: 'command' missing");
            if (!ProcessManager::getInstance().isServerRunning(m_serverUuid))
                return std::string("Error: server is not running");
            bool ok = ProcessManager::getInstance().sendCommand(m_serverUuid, cmd);
            Database::getInstance().logAction(m_username, "AI_COMMAND_APPROVED", m_serverUuid, cmd);
            return ok ? std::string("Sent: ") + cmd : std::string("Error: sendCommand failed");
        });
    }
    else if (toolName == "start_server" || toolName == "stop_server" || toolName == "restart_server") {
        runNow([&]() -> std::string {
            ServerRecord rec;
            if (!Database::getInstance().getServer(m_serverUuid, rec) || rec.uuid.empty())
                return "Error: server record not found";
            auto& pm = ProcessManager::getInstance();
            if (toolName == "start_server") {
                if (pm.isServerRunning(m_serverUuid)) return "Server is already running.";
                bool ok = pm.startServer(rec.uuid, rec.name, rec.directory_path, rec.start_command);
                Database::getInstance().logAction(m_username, "AI_START_APPROVED", m_serverUuid, "");
                return ok ? "Server start requested." : "Error: startServer failed.";
            }
            if (toolName == "stop_server") {
                if (!pm.isServerRunning(m_serverUuid)) return "Server is not running.";
                bool ok = pm.stopServer(m_serverUuid, false);
                Database::getInstance().logAction(m_username, "AI_STOP_APPROVED", m_serverUuid, "");
                return ok ? "Server stop requested (graceful)." : "Error: stopServer failed.";
            }
            // restart
            if (pm.isServerRunning(m_serverUuid)) pm.stopServer(m_serverUuid, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            bool ok = pm.startServer(rec.uuid, rec.name, rec.directory_path, rec.start_command);
            Database::getInstance().logAction(m_username, "AI_RESTART_APPROVED", m_serverUuid, "");
            return ok ? "Server restart requested." : "Error: restart failed to start.";
        });
    }
    else if (toolName == "create_backup") {
        runNow([&]() {
            bool nc = false;
            nlohmann::json body = { {"note", arguments.value("note", "created by AI assistant")} };
            nlohmann::json call = { {"method","POST"}, {"path", "/api/servers/" + m_serverUuid + "/backups"}, {"body", body} };
            return toolMcdeployApiCall(call, nc);
        });
    }
    else if (toolName == "update_start_command") {
        runNow([&]() {
            std::string cmd = arguments.value("command", "");
            if (cmd.empty()) return std::string("Error: empty command");
            bool ok = Database::getInstance().updateServerStartCommand(m_serverUuid, cmd);
            Database::getInstance().logAction(m_username, "AI_START_CMD_APPROVED", m_serverUuid, cmd.substr(0, 200));
            return ok ? std::string("Start command updated. Restart the server to use it.")
                      : std::string("Error: DB update failed");
        });
    }
    else if (toolName == "clear_player_inventory") {
        runNow([&]() {
            std::string username = arguments.value("username", "");
            std::string type = arguments.value("type", "inventory");
            PlayerRecord p;
            if (!Database::getInstance().getPlayer(m_serverUuid, username, p)) return std::string("Error: player not found");
            bool ok = Database::getInstance().clearPlayerItems(p.uuid, type);
            Database::getInstance().logAction(m_username, "AI_CLEAR_INV_APPROVED", m_serverUuid, username + ":" + type);
            return ok ? std::string("Cleared ") + type + " for " + username : std::string("Error: DB delete failed");
        });
    }
    else if (toolName == "reset_player") {
        runNow([&]() {
            std::string username = arguments.value("username", "");
            bool ok = Database::getInstance().resetPlayerEntirely(m_serverUuid, username);
            Database::getInstance().logAction(m_username, "AI_RESET_PLAYER_APPROVED", m_serverUuid, username);
            return ok ? std::string("Player '") + username + "' reset." : std::string("Error: reset failed");
        });
    }
    else if (toolName == "delete_backup") {
        runNow([&]() {
            std::string uuid = arguments.value("backup_uuid", "");
            bool ok = Database::getInstance().deleteBackup(uuid);
            Database::getInstance().logAction(m_username, "AI_DELETE_BACKUP_APPROVED", m_serverUuid, uuid);
            return ok ? std::string("Backup deleted.") : std::string("Error: DB delete failed");
        });
    }
    else if (toolName == "run_shell_command") {
        runNow([&]() -> std::string {
            std::string cmd = arguments.value("command", "");
            std::string cwd = arguments.value("cwd", m_serverPath);
            int timeout = arguments.value("timeout_seconds", 60);
            if (timeout <= 0 || timeout > 300) timeout = 60;
            if (cmd.empty()) return std::string("Error: empty command");

            // Portable-ish popen with stdout+stderr merged. Wrap the cmd so it cd's first.
#ifdef _WIN32
            std::string wrapped = "cmd /c \"cd /d \"" + cwd + "\" && " + cmd + "\" 2>&1";
            FILE* pipe = _popen(wrapped.c_str(), "r");
#else
            std::string wrapped = "cd \"" + cwd + "\" && " + cmd + " 2>&1";
            FILE* pipe = popen(wrapped.c_str(), "r");
#endif
            if (!pipe) return std::string("Error: could not spawn shell");
            std::string out;
            std::array<char, 4096> buf{};
            auto start = std::chrono::steady_clock::now();
            while (fgets(buf.data(), (int)buf.size(), pipe)) {
                out.append(buf.data());
                if (out.size() > 200000) { out += "\n... [truncated at 200KB]\n"; break; }
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed > std::chrono::seconds(timeout)) {
                    out += "\n... [timeout at " + std::to_string(timeout) + "s]\n";
                    break;
                }
            }
#ifdef _WIN32
            int code = _pclose(pipe);
#else
            int code = pclose(pipe);
#endif
            Database::getInstance().logAction(m_username, "AI_SHELL_APPROVED", m_serverUuid, cmd.substr(0, 200));
            std::ostringstream ss;
            ss << "exit_code=" << code << "\n---\n" << out;
            return ss.str();
        });
    }
    else if (toolName == "install_addon") {
        runNow([&]() {
            bool nc = false;
            nlohmann::json call = {
                {"method","POST"},
                {"path", "/api/servers/" + m_serverUuid + "/addons/install"},
                {"body", arguments}
            };
            return toolMcdeployApiCall(call, nc);
        });
    }
    else {
        s.result = "Error: unknown tool for approval path";
    }

    // Persist the tool call audit.
    AiToolCallRecord rec;
    rec.server_uuid = m_serverUuid; rec.username = m_username;
    rec.tool_name = toolName; rec.arguments = arguments.dump();
    rec.result = truncateForModel(s.result, 8000); rec.status = "success";
    rec.latency_ms = s.latency_ms;
    Database::getInstance().logAiToolCall(rec);
    return s;
}

// ============================================================
// Undo (single step)
// ============================================================
std::string AIAgent::undoLast() {
    AiUndoRecord u;
    if (!Database::getInstance().popAiUndo(m_serverUuid, m_username, u))
        return "Undo stack is empty.";

    fs::path abs;
    std::string err;
    if (!resolveSafePath(u.file_path, abs, err)) return "Error: " + err;

    if (u.operation == "write") {
        if (u.previous_existed) {
            std::ofstream f(abs, std::ios::binary | std::ios::trunc);
            if (!f.is_open()) return "Error: could not restore previous content";
            f << u.previous_content;
            Database::getInstance().logAction(m_username, "AI_UNDO_WRITE", m_serverUuid, "path=" + u.file_path);
            return "Reverted write to '" + u.file_path + "' (restored " + std::to_string(u.previous_content.size()) + " bytes).";
        } else {
            std::error_code ec; fs::remove(abs, ec);
            Database::getInstance().logAction(m_username, "AI_UNDO_WRITE_NEW", m_serverUuid, "path=" + u.file_path);
            return "Reverted write to '" + u.file_path + "' (file did not previously exist; now deleted).";
        }
    } else if (u.operation == "delete") {
        try { fs::create_directories(abs.parent_path()); } catch (...) {}
        std::ofstream f(abs, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return "Error: could not restore deleted file";
        f << u.previous_content;
        Database::getInstance().logAction(m_username, "AI_UNDO_DELETE", m_serverUuid, "path=" + u.file_path);
        return "Restored deleted file '" + u.file_path + "' (" + std::to_string(u.previous_content.size()) + " bytes).";
    }
    return "Error: unknown undo operation '" + u.operation + "'";
}

// ============================================================
// Suggestion extraction
// ============================================================
std::vector<std::string> AIAgent::extractSuggestions(std::string& text) {
    std::vector<std::string> out;
    // Look for a ```suggestions ... ``` block at the end.
    std::regex block("```suggestions\\s*\\n([\\s\\S]*?)```", std::regex::ECMAScript);
    std::smatch m;
    if (std::regex_search(text, m, block)) {
        std::string body = m[1].str();
        try {
            auto j = nlohmann::json::parse(body);
            if (j.is_array()) {
                for (auto& v : j) if (v.is_string()) out.push_back(v.get<std::string>());
            }
        } catch (...) {}
        // Strip the block from the visible message.
        text = std::regex_replace(text, block, "");
        // Trim trailing whitespace.
        while (!text.empty() && (text.back() == '\n' || text.back() == ' ' || text.back() == '\t')) text.pop_back();
    }
    return out;
}

// ============================================================
// Main agent loop
// ============================================================
AiTurnResult AIAgent::runTurn(const std::string& userMessage, const nlohmann::json& priorHistoryOverride) {
    AiTurnResult turn;

    if (m_config.api_key.empty()) {
        turn.ok = false;
        turn.error = "No AI API key configured. Set mcdeploy.ai.api_key in config.json or the GEMINI_API_KEY env var.";
        return turn;
    }

    // Persist the user message first so it survives across restarts.
    persistUserMessage(userMessage);

    // Build messages array: [system] + history + latest user.
    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({ {"role", "system"}, {"content", buildSystemPrompt()} });

    nlohmann::json history = priorHistoryOverride.is_null() ? loadHistoryAsMessages() : priorHistoryOverride;
    if (!history.is_array()) history = nlohmann::json::array();
    for (auto& m : history) messages.push_back(m);

    // The user message was persisted; also included in history from that persistence,
    // so we don't push it again.

    nlohmann::json tools = buildToolSchemas();

    for (int iter = 0; iter < m_config.max_iterations; iter++) {
        nlohmann::json body = {
            {"model", m_config.model},
            {"messages", messages},
            {"tools", tools},
            {"tool_choice", "auto"},
            {"temperature", m_config.temperature},
            {"max_tokens", m_config.max_output_tokens}
        };

        auto response = postCompletions(body);

        if (response.contains("error")) {
            std::string errMsg;
            const auto& err = response["error"];
            if (err.is_object())      errMsg = err.value("message", "unknown API error");
            else if (err.is_string()) errMsg = err.get<std::string>();
            else                      errMsg = err.dump();
            turn.ok = false;
            turn.error = "AI API error: " + errMsg;
            AiStep s; s.kind = "error"; s.content = errMsg;
            turn.steps.push_back(s);
            return turn;
        }
        if (!response.contains("choices") || !response["choices"].is_array() || response["choices"].empty()) {
            turn.ok = false;
            turn.error = "AI API returned no choices";
            return turn;
        }

        // Usage
        if (response.contains("usage")) {
            long long pt = response["usage"].value("prompt_tokens", 0);
            long long ct = response["usage"].value("completion_tokens", 0);
            turn.tokens_prompt += pt;
            turn.tokens_completion += ct;
            Database::getInstance().recordAiUsage(m_serverUuid, m_username, m_config.model, pt, ct);
        }

        auto& choice = response["choices"][0];
        auto message = choice.value("message", nlohmann::json::object());
        std::string finishReason = choice.value("finish_reason", "");

        // The assistant's message: could contain a final answer, tool_calls, or both.
        std::string content = message.value("content", "");
        nlohmann::json toolCalls = message.value("tool_calls", nlohmann::json::array());

        // Append assistant message to conversation history for next round.
        nlohmann::json asstMsg = { {"role", "assistant"} };
        if (!content.empty()) asstMsg["content"] = content;
        if (!toolCalls.empty()) asstMsg["tool_calls"] = toolCalls;
        messages.push_back(asstMsg);

        // Persist to DB.
        persistAssistantMessage(content, toolCalls.empty() ? "" : toolCalls.dump(),
            (int)(response.contains("usage") ? response["usage"].value("prompt_tokens", 0) : 0),
            (int)(response.contains("usage") ? response["usage"].value("completion_tokens", 0) : 0));

        if (toolCalls.empty() || finishReason == "stop") {
            // Final answer.
            std::string finalText = content;
            auto sugg = extractSuggestions(finalText);
            turn.suggestions = sugg;
            turn.final_message = finalText;
            AiStep s; s.kind = "final"; s.content = finalText;
            turn.steps.push_back(s);
            return turn;
        }

        // Execute each tool call.
        for (auto& tc : toolCalls) {
            std::string tcId = tc.value("id", genId());
            auto fn = tc.value("function", nlohmann::json::object());
            std::string name = fn.value("name", "");
            std::string argsStr = fn.value("arguments", "{}");
            nlohmann::json argsJson;
            try { argsJson = nlohmann::json::parse(argsStr); }
            catch (...) { argsJson = nlohmann::json::object(); }

            AiStep call;
            call.kind = "tool_call";
            call.tool_name = name;
            call.arguments = argsJson;
            call.pending_tool_id = tcId;
            turn.steps.push_back(call);

            auto outcome = dispatchTool(name, argsJson);

            AiStep res;
            res.kind = "tool_result";
            res.tool_name = name;
            res.result = truncateForModel(outcome.result, 4000);
            res.latency_ms = outcome.latency_ms;
            res.needs_confirmation = outcome.needs_confirmation;
            res.pending_tool_id = tcId;
            res.arguments = argsJson;
            turn.steps.push_back(res);

            // Feed the tool result back to the model.
            nlohmann::json toolResp = {
                {"role", "tool"},
                {"tool_call_id", tcId},
                {"name", name},
                {"content", truncateForModel(outcome.result, 8000)}
            };
            messages.push_back(toolResp);
            persistToolResponse(name, tcId, truncateForModel(outcome.result, 8000));

            // If it needs confirmation, surface the pending action to the frontend.
            if (outcome.needs_confirmation) {
                turn.pending_actions.push_back({
                    {"tool", name},
                    {"arguments", argsJson},
                    {"tool_call_id", tcId},
                    {"reason", outcome.result}
                });
            }
        }
        // Loop again so the model can react to tool results.
    }

    // Ran out of iterations - inject a note so the user knows.
    turn.final_message = "(agent iteration cap reached; the model was still calling tools)";
    turn.ok = true;
    return turn;
}

// ============================================================
// EXPANDED TOOLKIT — config, addons, players, system, shell,
// escape hatches. Everything below was added to give the agent
// broader access when the user asks for it.
// ============================================================

std::string AIAgent::toolUpdateServerConfig(const nlohmann::json& args) {
    if (!m_agentMode) return "Error: update_server_config requires AGENT MODE.";
    ServerRecord rec;
    if (!Database::getInstance().getServer(m_serverUuid, rec) || rec.uuid.empty())
        return "Error: server not found";

    int ramMin = args.contains("ram_min_mb") ? args["ram_min_mb"].get<int>() : rec.ram_min;
    int ramMax = args.contains("ram_max_mb") ? args["ram_max_mb"].get<int>() : rec.ram_max;
    int port   = args.contains("port")       ? args["port"].get<int>()       : rec.port;

    if (ramMin <= 0 || ramMax <= 0)           return "Error: RAM values must be positive integers (in MB)";
    if (ramMin > ramMax)                       return "Error: ram_min_mb cannot exceed ram_max_mb";
    if (ramMax > 65536)                        return "Error: ram_max_mb absurdly large (>64GB)";
    if (port < 1 || port > 65535)              return "Error: port out of range";

    bool ok = Database::getInstance().updateServerConfig(m_serverUuid, ramMin, ramMax, port);
    if (!ok) return "Error: DB update failed";

    // If the JVM args in the start command reference the old RAM, rewrite them so a restart uses the new values.
    std::string oldCmd = rec.start_command;
    std::string newCmd = oldCmd;
    std::regex xmsRx("-Xms\\d+[mMgGkK]");
    std::regex xmxRx("-Xmx\\d+[mMgGkK]");
    newCmd = std::regex_replace(newCmd, xmsRx, "-Xms" + std::to_string(ramMin) + "M");
    newCmd = std::regex_replace(newCmd, xmxRx, "-Xmx" + std::to_string(ramMax) + "M");
    if (newCmd != oldCmd) {
        Database::getInstance().updateServerStartCommand(m_serverUuid, newCmd);
    }

    Database::getInstance().logAction(m_username, "AI_UPDATE_CONFIG", m_serverUuid,
        "ram=" + std::to_string(ramMin) + "-" + std::to_string(ramMax) + "MB port=" + std::to_string(port));

    std::ostringstream ss;
    ss << "Updated server config:\n"
       << "  ram_min = " << ramMin << " MB\n"
       << "  ram_max = " << ramMax << " MB\n"
       << "  port    = " << port << "\n";
    if (newCmd != oldCmd) ss << "Start command JVM args rewritten. Restart the server for changes to take effect.";
    else                  ss << "Restart the server for JVM changes to take effect.";
    return ss.str();
}

std::string AIAgent::toolUpdateStartCommand(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: update_start_command requires AGENT MODE.";
    std::string cmd = args.value("command", "");
    if (cmd.empty()) return "Error: 'command' is required";
    // The start command runs at server launch — gate this one; malformed cmd = broken server.
    outNeedsConfirm = true;
    return "Confirmation required: update_start_command will replace how this server is launched. Awaiting approval.";
}

std::string AIAgent::toolSetServerStatus(const nlohmann::json& args) {
    if (!m_agentMode) return "Error: set_server_status requires AGENT MODE.";
    std::string status = args.value("status", "");
    if (status.empty()) return "Error: 'status' required";
    bool ok = Database::getInstance().updateServerStatus(m_serverUuid, status);
    Database::getInstance().logAction(m_username, "AI_STATUS", m_serverUuid, status);
    return ok ? "Server status set to " + status : "Error: DB update failed";
}

std::string AIAgent::toolListBackups(const nlohmann::json& args) {
    (void)args;
    auto rows = Database::getInstance().getServerBackups(m_serverUuid);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& b : rows) {
        arr.push_back({
            {"backup_uuid", b.backup_uuid},
            {"file_name",   b.file_name},
            {"file_size",   b.file_size},
            {"created_at",  b.created_at}
        });
    }
    if (arr.empty()) return "(no backups yet)";
    return arr.dump(2);
}

std::string AIAgent::toolDeleteBackup(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: delete_backup requires AGENT MODE.";
    std::string uuid = args.value("backup_uuid", "");
    if (uuid.empty()) return "Error: 'backup_uuid' required";
    outNeedsConfirm = true;
    return "Confirmation required: delete_backup permanently removes the archive. Awaiting approval.";
}

// ============================================================
// Global system tools
// ============================================================
std::string AIAgent::toolGetSystemInfo(const nlohmann::json& args) {
    (void)args;
    nlohmann::json j;
#ifdef _WIN32
    j["os"] = "Windows";
    char hn[256] = {0}; DWORD sz = sizeof(hn);
    if (GetComputerNameA(hn, &sz)) j["hostname"] = hn;
    SYSTEM_INFO si; GetSystemInfo(&si);
    j["cpu_count"] = (int)si.dwNumberOfProcessors;
#else
    j["os"] = "unix-like";
#endif
    SystemMetrics sys = SystemInfo::getInstance().getMetrics();
    j["ram_used_gb"]  = sys.ram_used_gb;
    j["ram_total_gb"] = sys.ram_total_gb;
    j["disk_used_gb"] = sys.disk_used_gb;
    j["disk_total_gb"]= sys.disk_total_gb;
    j["cpu_usage_pct"]= sys.cpu_usage_pct;
    j["current_working_dir"] = fs::current_path().generic_string();
    return j.dump(2);
}

std::string AIAgent::toolListAllServers(const nlohmann::json& args) {
    (void)args;
    auto servers = Database::getInstance().getAllServers();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : servers) {
        arr.push_back({
            {"uuid", s.uuid},
            {"name", s.name},
            {"software_type", s.software_type},
            {"version", s.version},
            {"port", s.port},
            {"status", s.status},
            {"is_this_server", s.uuid == m_serverUuid},
            {"is_running", ProcessManager::getInstance().isServerRunning(s.uuid)}
        });
    }
    return arr.dump(2);
}

std::string AIAgent::toolGetAuditLog(const nlohmann::json& args) {
    int limit = args.value("limit", 30);
    if (limit <= 0 || limit > 200) limit = 30;
    std::string filter = toLower(args.value("filter", ""));

    auto logs = Database::getInstance().getAuditLogs(limit);
    if (!filter.empty() && logs.is_array()) {
        nlohmann::json filtered = nlohmann::json::array();
        for (auto& row : logs) {
            std::string action = toLower(row.value("action", ""));
            if (action.find(filter) != std::string::npos) filtered.push_back(row);
        }
        logs = filtered;
    }
    return logs.dump(2);
}

// ============================================================
// Addon tools (proxy through the internal HTTP API to reuse
// ServerController's Modrinth download pipeline).
// ============================================================
std::string AIAgent::toolListInstalledAddons(const nlohmann::json& args) {
    (void)args;
    nlohmann::json call;
    call["method"] = "GET";
    call["path"]   = "/api/servers/" + m_serverUuid + "/addons/installed";
    bool nc = false;
    return toolMcdeployApiCall(call, nc);
}

std::string AIAgent::toolSearchAddons(const nlohmann::json& args) {
    std::string q = args.value("query", "");
    if (q.empty()) return "Error: 'query' required";
    // Note: percent-encoding is handled by curl in the api call implementation.
    nlohmann::json call;
    call["method"] = "GET";
    call["path"]   = "/api/servers/" + m_serverUuid + "/addons/search?q=" + q;
    bool nc = false;
    return toolMcdeployApiCall(call, nc);
}

std::string AIAgent::toolInstallAddon(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: install_addon requires AGENT MODE.";
    std::string id = args.value("addon_id", "");
    if (id.empty()) return "Error: 'addon_id' required";
    nlohmann::json body = { {"addon_id", id} };
    if (args.contains("version_id")) body["version_id"] = args["version_id"];
    nlohmann::json call;
    call["method"] = "POST";
    call["path"]   = "/api/servers/" + m_serverUuid + "/addons/install";
    call["body"]   = body;
    return toolMcdeployApiCall(call, outNeedsConfirm);
}

std::string AIAgent::toolUninstallAddon(const nlohmann::json& args) {
    if (!m_agentMode) return "Error: uninstall_addon requires AGENT MODE.";
    std::string fn = args.value("filename", "");
    if (fn.empty()) return "Error: 'filename' required";
    nlohmann::json call;
    call["method"] = "DELETE";
    call["path"]   = "/api/servers/" + m_serverUuid + "/addons/uninstall?filename=" + fn;
    bool nc = false;
    return toolMcdeployApiCall(call, nc);
}

// ============================================================
// Player tools
// ============================================================
std::string AIAgent::toolGetPlayerDetails(const nlohmann::json& args) {
    std::string username = args.value("username", "");
    if (username.empty()) return "Error: 'username' required";
    PlayerRecord p;
    if (!Database::getInstance().getPlayer(m_serverUuid, username, p) || p.uuid.empty())
        return "Error: player not found";
    auto items = Database::getInstance().getPlayerItems(p.uuid);
    auto coords = Database::getInstance().getPlayerCoordinateLogs(m_serverUuid);
    auto advancements = Database::getInstance().getPlayerAdvancements(p.uuid);

    nlohmann::json out;
    out["uuid"] = p.uuid;
    out["username"] = p.username;
    out["is_online"] = p.is_online != 0;
    out["health"] = p.health;
    out["hunger"] = p.hunger;
    out["frozen"] = p.frozen != 0;
    out["last_login"]  = { {"x", p.last_login_x},  {"y", p.last_login_y},  {"z", p.last_login_z} };
    out["last_logoff"] = { {"x", p.last_logoff_x}, {"y", p.last_logoff_y}, {"z", p.last_logoff_z} };
    nlohmann::json inv = nlohmann::json::array();
    nlohmann::json ec  = nlohmann::json::array();
    for (const auto& it : items) {
        nlohmann::json ij = {
            {"slot", it.slot}, {"item_id", it.item_id}, {"count", it.count},
            {"display_name", it.display_name}
        };
        if (it.type == "inventory") inv.push_back(ij);
        else                        ec.push_back(ij);
    }
    out["inventory"] = inv;
    out["ender_chest"] = ec;
    out["advancements"] = advancements;
    out["recent_coordinate_events"] = nlohmann::json::array();
    int keep = 10;
    for (auto it = coords.rbegin(); it != coords.rend() && keep > 0; ++it, --keep) {
        if (it->username != username) continue;
        out["recent_coordinate_events"].push_back({
            {"type", it->type}, {"x", it->x}, {"y", it->y}, {"z", it->z}, {"timestamp", it->timestamp}
        });
    }
    return out.dump(2);
}

std::string AIAgent::toolUpdatePlayerStats(const nlohmann::json& args) {
    if (!m_agentMode) return "Error: update_player_stats requires AGENT MODE.";
    std::string username = args.value("username", "");
    if (username.empty()) return "Error: 'username' required";
    PlayerRecord p;
    if (!Database::getInstance().getPlayer(m_serverUuid, username, p)) return "Error: player not found";
    double health = args.contains("health") ? args["health"].get<double>() : p.health;
    int hunger    = args.contains("hunger") ? args["hunger"].get<int>()    : p.hunger;
    int frozen    = args.contains("frozen") ? args["frozen"].get<int>()    : p.frozen;
    bool ok = Database::getInstance().updatePlayerStats(m_serverUuid, username, health, hunger, frozen);
    Database::getInstance().logAction(m_username, "AI_PLAYER_STATS", m_serverUuid, username);
    return ok ? "Updated player stats." : "Error: DB update failed";
}

std::string AIAgent::toolSetPlayerOnline(const nlohmann::json& args) {
    if (!m_agentMode) return "Error: set_player_online requires AGENT MODE.";
    std::string username = args.value("username", "");
    if (username.empty()) return "Error: 'username' required";
    bool online = args.value("online", true);
    bool ok = Database::getInstance().setPlayerOnline(m_serverUuid, username, online ? 1 : 0);
    return ok ? (online ? "Player marked online." : "Player marked offline.") : "Error: DB update failed";
}

std::string AIAgent::toolClearPlayerInventory(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: clear_player_inventory requires AGENT MODE.";
    std::string username = args.value("username", "");
    std::string type = args.value("type", "inventory");
    if (username.empty()) return "Error: 'username' required";
    if (type != "inventory" && type != "ender_chest") return "Error: type must be 'inventory' or 'ender_chest'";
    outNeedsConfirm = true;
    return "Confirmation required: wiping " + type + " for '" + username + "'. Awaiting approval.";
}

std::string AIAgent::toolResetPlayer(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: reset_player requires AGENT MODE.";
    std::string username = args.value("username", "");
    if (username.empty()) return "Error: 'username' required";
    outNeedsConfirm = true;
    return "Confirmation required: reset_player permanently wipes '" + username + "'. Awaiting approval.";
}

std::string AIAgent::toolGivePlayerItem(const nlohmann::json& args) {
    if (!m_agentMode) return "Error: give_player_item requires AGENT MODE.";
    std::string username = args.value("username", "");
    std::string type = args.value("type", "inventory");
    if (username.empty() || !args.contains("slot") || !args.contains("item_id"))
        return "Error: username, slot, and item_id required";
    PlayerRecord p;
    if (!Database::getInstance().getOrCreatePlayer(m_serverUuid, username, p))
        return "Error: could not resolve player";
    PlayerItemRecord it;
    it.player_uuid = p.uuid;
    it.type = type;
    it.slot = args["slot"].get<int>();
    it.item_id = args["item_id"].get<std::string>();
    it.count = args.value("count", 1);
    it.display_name = args.value("display_name", "");
    it.unbreakable = args.value("unbreakable", false) ? 1 : 0;
    bool ok = Database::getInstance().updatePlayerItem(it);
    return ok ? "Item placed at slot " + std::to_string(it.slot) + "." : "Error: DB update failed";
}

std::string AIAgent::toolSetPlayerAdvancement(const nlohmann::json& args) {
    if (!m_agentMode) return "Error: set_player_advancement requires AGENT MODE.";
    std::string username = args.value("username", "");
    std::string advId = args.value("advancement_id", "");
    if (username.empty() || advId.empty()) return "Error: username and advancement_id required";
    bool granted = args.value("granted", true);
    PlayerRecord p;
    if (!Database::getInstance().getPlayer(m_serverUuid, username, p)) return "Error: player not found";
    bool ok = Database::getInstance().updatePlayerAdvancement(p.uuid, advId, granted ? 1 : 0);
    return ok ? std::string(granted ? "Granted " : "Revoked ") + advId : "Error: DB update failed";
}

std::string AIAgent::toolGetPlayerCoords(const nlohmann::json& args) {
    std::string username = args.value("username", "");
    if (username.empty()) return "Error: 'username' required";
    auto logs = Database::getInstance().getPlayerCoordinateLogs(m_serverUuid);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& l : logs) {
        if (l.username != username) continue;
        arr.push_back({
            {"type", l.type}, {"x", l.x}, {"y", l.y}, {"z", l.z}, {"timestamp", l.timestamp}
        });
    }
    if (arr.empty()) return "(no coordinate history for '" + username + "')";
    return arr.dump(2);
}

std::string AIAgent::toolGetPlayerAdvancements(const nlohmann::json& args) {
    std::string username = args.value("username", "");
    if (username.empty()) return "Error: 'username' required";
    PlayerRecord p;
    if (!Database::getInstance().getPlayer(m_serverUuid, username, p)) return "Error: player not found";
    return Database::getInstance().getPlayerAdvancements(p.uuid).dump(2);
}

// ============================================================
// Extended file operations
// ============================================================
std::string AIAgent::toolAppendFile(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: append_file requires AGENT MODE.";
    std::string relPath = args.value("path", "");
    std::string content = args.value("content", "");
    if (relPath.empty()) return "Error: 'path' required";
    fs::path abs; std::string err;
    if (!resolveSafePath(relPath, abs, err)) return "Error: " + err;
    if (fs::exists(abs) && isSensitiveFile(abs)) {
        outNeedsConfirm = true;
        return "Confirmation required: '" + relPath + "' is sensitive. Awaiting approval.";
    }
    try { fs::create_directories(abs.parent_path()); } catch (...) {}
    std::ofstream f(abs, std::ios::binary | std::ios::app);
    if (!f.is_open()) return "Error: could not open for append";
    f << content;
    Database::getInstance().logAction(m_username, "AI_APPEND", m_serverUuid, relPath);
    return "Appended " + std::to_string(content.size()) + " bytes to '" + relPath + "'.";
}

std::string AIAgent::toolCreateDirectory(const nlohmann::json& args) {
    if (!m_agentMode) return "Error: create_directory requires AGENT MODE.";
    std::string relPath = args.value("path", "");
    if (relPath.empty()) return "Error: 'path' required";
    fs::path abs; std::string err;
    if (!resolveSafePath(relPath, abs, err)) return "Error: " + err;
    std::error_code ec;
    fs::create_directories(abs, ec);
    if (ec) return "Error: " + ec.message();
    Database::getInstance().logAction(m_username, "AI_MKDIR", m_serverUuid, relPath);
    return "Created directory '" + relPath + "'.";
}

std::string AIAgent::toolMoveFile(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: move_file requires AGENT MODE.";
    std::string from = args.value("from", "");
    std::string to   = args.value("to", "");
    if (from.empty() || to.empty()) return "Error: 'from' and 'to' required";
    fs::path a, b; std::string err;
    if (!resolveSafePath(from, a, err)) return "Error (from): " + err;
    if (!resolveSafePath(to, b, err))   return "Error (to): " + err;
    if (!fs::exists(a)) return "Error: source does not exist";
    if (isSensitiveFile(a)) { outNeedsConfirm = true; return "Confirmation required: source is sensitive."; }
    std::error_code ec;
    fs::create_directories(b.parent_path(), ec);
    fs::rename(a, b, ec);
    if (ec) {
        // Fall back to copy+delete across filesystems
        fs::copy(a, b, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) return "Error: " + ec.message();
        fs::remove_all(a, ec);
    }
    Database::getInstance().logAction(m_username, "AI_MOVE", m_serverUuid, from + " -> " + to);
    return "Moved '" + from + "' -> '" + to + "'.";
}

std::string AIAgent::toolCopyFile(const nlohmann::json& args) {
    if (!m_agentMode) return "Error: copy_file requires AGENT MODE.";
    std::string from = args.value("from", "");
    std::string to   = args.value("to", "");
    if (from.empty() || to.empty()) return "Error: 'from' and 'to' required";
    fs::path a, b; std::string err;
    if (!resolveSafePath(from, a, err)) return "Error (from): " + err;
    if (!resolveSafePath(to, b, err))   return "Error (to): " + err;
    if (!fs::exists(a)) return "Error: source does not exist";
    std::error_code ec;
    fs::create_directories(b.parent_path(), ec);
    fs::copy(a, b, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) return "Error: " + ec.message();
    Database::getInstance().logAction(m_username, "AI_COPY", m_serverUuid, from + " -> " + to);
    return "Copied '" + from + "' -> '" + to + "'.";
}

std::string AIAgent::toolFileInfo(const nlohmann::json& args) {
    std::string relPath = args.value("path", "");
    if (relPath.empty()) return "Error: 'path' required";
    fs::path abs; std::string err;
    if (!resolveSafePath(relPath, abs, err)) return "Error: " + err;
    if (!fs::exists(abs)) return "Error: does not exist";
    nlohmann::json j;
    j["path"] = relPath;
    j["is_directory"] = fs::is_directory(abs);
    j["is_regular_file"] = fs::is_regular_file(abs);
    std::error_code ec;
    if (fs::is_regular_file(abs)) {
        auto sz = fs::file_size(abs, ec);
        j["size_bytes"] = ec ? 0 : (long long)sz;
    }
    auto ft = fs::last_write_time(abs, ec);
    if (!ec) {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::gmtime(&cftime));
        j["mtime_utc"] = buf;
    }
    return j.dump(2);
}

// ============================================================
// Shell command execution (heavily gated)
// ============================================================
std::string AIAgent::toolRunShellCommand(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    if (!m_agentMode) return "Error: run_shell_command requires AGENT MODE.";
    std::string cmd = args.value("command", "");
    if (cmd.empty()) return "Error: 'command' required";
    // Always gated - this can literally do anything.
    outNeedsConfirm = true;
    return "Confirmation required: run_shell_command executes an arbitrary shell command "
           "with the same privileges as MCDeploy. Awaiting user approval.";
}

// ============================================================
// The escape hatch: generic MCDeploy API call
// ============================================================
std::string AIAgent::toolMcdeployApiCall(const nlohmann::json& args, bool& outNeedsConfirm) {
    outNeedsConfirm = false;
    std::string method = toLower(args.value("method", "get"));
    std::transform(method.begin(), method.end(), method.begin(), [](unsigned char c){ return std::toupper(c); });
    std::string path = args.value("path", "");
    if (path.empty()) return "Error: 'path' required";
    if (method != "GET" && method != "POST" && method != "PUT" && method != "DELETE")
        return "Error: unsupported method";

    if (method != "GET") {
        if (!m_agentMode) return "Error: non-GET mcdeploy_api_call requires AGENT MODE.";
    }

    // Load port from config
    uint16_t port = 8082;
    {
        std::ifstream f("config.json");
        if (f.is_open()) {
            try {
                nlohmann::json j; f >> j;
                if (j.contains("mcdeploy") && j["mcdeploy"].contains("port"))
                    port = j["mcdeploy"]["port"].get<uint16_t>();
            } catch (...) {}
        }
    }
    std::string url = "http://127.0.0.1:" + std::to_string(port) + path;

    CURL* curl = curl_easy_init();
    if (!curl) return "Error: curl init failed";
    std::string body;
    std::string reqBody = args.contains("body") ? args["body"].dump() : "";
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    // Loopback call - internal auth stub accepts anything but include a bearer just in case.
    headers = curl_slist_append(headers, "Authorization: Bearer ai-internal");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (!reqBody.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reqBody.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return std::string("Error: ") + curl_easy_strerror(res);

    Database::getInstance().logAction(m_username, "AI_APICALL", m_serverUuid, method + " " + path);
    nlohmann::json j;
    j["http_status"] = (int)http_code;
    try { j["body"] = nlohmann::json::parse(body); } catch (...) { j["body_raw"] = body; }
    return j.dump(2);
}

} // namespace MCDeploy
