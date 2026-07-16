#pragma once

// ============================================================
// MCDeploy - AI Agent
// ------------------------------------------------------------
// A proper tool-calling agent for the AI Editor. Uses the OpenAI
// chat/completions "tools" schema (which Google's Gemini API and
// most others speak natively) so the model can plan, call tools,
// observe results, and answer multi-step questions in one loop.
// ============================================================

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace MCDeploy {

// Config used per-request. Loaded from config.json.
struct AiConfig {
    std::string api_key;
    std::string api_url = "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions";
    std::string model   = "gemini-2.5-flash";
    int max_iterations  = 10;   // safety cap on agent tool-call loop
    int max_output_tokens = 4096;
    double temperature = 0.4;   // slightly lower - we want reliable tool calls
};

// Represents a single agent step surfaced to the frontend.
struct AiStep {
    std::string kind;          // "thought" | "tool_call" | "tool_result" | "final" | "error"
    std::string tool_name;
    nlohmann::json arguments;
    std::string result;        // truncated summary for display
    std::string content;       // for final assistant text
    int latency_ms = 0;
    bool needs_confirmation = false;   // set when a dangerous tool was refused pending user approval
    std::string pending_tool_id;
};

// The full result of a single chat turn.
struct AiTurnResult {
    std::string final_message;        // final assistant text (markdown-friendly)
    std::vector<AiStep> steps;        // full trace for UI display
    std::vector<std::string> suggestions; // 2-4 follow-up prompts extracted from the model
    long long tokens_prompt = 0;
    long long tokens_completion = 0;
    std::string error;
    bool ok = true;
    std::vector<nlohmann::json> pending_actions; // dangerous ops awaiting explicit approval
};

class AIAgent {
public:
    AIAgent(const std::string& serverUuid,
            const std::string& serverPath,
            const std::string& username,
            bool agentMode,
            const AiConfig& config);

    // Load persisted history from DB and turn it into OpenAI messages format.
    nlohmann::json loadHistoryAsMessages() const;

    // Run a full agent turn: send user's message, loop tool calls until the
    // model produces a final answer or we hit the iteration cap.
    AiTurnResult runTurn(const std::string& userMessage,
                         const nlohmann::json& priorHistoryOverride = nullptr);

    // Execute a specific dangerous tool that was previously flagged as needing
    // confirmation. The frontend passes back the exact args verbatim.
    AiStep executeApprovedTool(const std::string& toolName, const nlohmann::json& arguments);

    // Undo the most recent AI-caused file write/delete for this server+user.
    // Returns human-readable status.
    std::string undoLast();

private:
    // --------- Configuration ---------
    std::string m_serverUuid;
    std::string m_serverPath;
    std::string m_username;
    bool        m_agentMode;
    AiConfig    m_config;

    // --------- HTTP ---------
    // Calls the chat/completions endpoint. Returns raw parsed JSON.
    nlohmann::json postCompletions(const nlohmann::json& body);

    // --------- Prompt construction ---------
    std::string buildSystemPrompt() const;
    nlohmann::json buildToolSchemas() const;

    // Serialize a persisted conversation row for the model.
    nlohmann::json historyRowToOpenAI(const struct AiConversationRecord& r) const;

    // --------- Tool dispatch ---------
    // Runs a tool, returns the result text and populates latency.
    struct ToolExecOutcome {
        std::string result;
        std::string status;              // "success" | "error" | "denied"
        int latency_ms = 0;
        bool needs_confirmation = false; // true if the tool refused pending approval
    };
    ToolExecOutcome dispatchTool(const std::string& name, const nlohmann::json& args);

    // --------- Individual tool implementations ---------
    // Read-only / inspection
    std::string toolListDirectory(const nlohmann::json& args);
    std::string toolReadFile(const nlohmann::json& args);
    std::string toolSearchFiles(const nlohmann::json& args);
    std::string toolSearchLogs(const nlohmann::json& args);
    std::string toolDiffFile(const nlohmann::json& args);
    std::string toolGetMetrics(const nlohmann::json& args);
    std::string toolGetPluginInfo(const nlohmann::json& args);
    std::string toolQueryPlayers(const nlohmann::json& args);
    std::string toolGetServerInfo(const nlohmann::json& args);
    std::string toolWebSearch(const nlohmann::json& args);
    std::string toolFileInfo(const nlohmann::json& args);
    std::string toolGetSystemInfo(const nlohmann::json& args);
    std::string toolListAllServers(const nlohmann::json& args);
    std::string toolGetAuditLog(const nlohmann::json& args);
    std::string toolListBackups(const nlohmann::json& args);
    std::string toolListInstalledAddons(const nlohmann::json& args);
    std::string toolSearchAddons(const nlohmann::json& args);
    std::string toolGetPlayerDetails(const nlohmann::json& args);
    std::string toolGetPlayerCoords(const nlohmann::json& args);
    std::string toolGetPlayerAdvancements(const nlohmann::json& args);

    // Mutating / control (agent mode required)
    std::string toolWriteFile(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolDeleteFile(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolAppendFile(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolCreateDirectory(const nlohmann::json& args);
    std::string toolMoveFile(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolCopyFile(const nlohmann::json& args);
    std::string toolExecuteCommand(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolServerControl(const std::string& op, const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolSetPerformance(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolCreateBackup(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolUpdateServerConfig(const nlohmann::json& args);
    std::string toolUpdateStartCommand(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolSetServerStatus(const nlohmann::json& args);
    std::string toolRunShellCommand(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolInstallAddon(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolUninstallAddon(const nlohmann::json& args);
    std::string toolUpdatePlayerStats(const nlohmann::json& args);
    std::string toolClearPlayerInventory(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolResetPlayer(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolGivePlayerItem(const nlohmann::json& args);
    std::string toolSetPlayerAdvancement(const nlohmann::json& args);
    std::string toolSetPlayerOnline(const nlohmann::json& args);
    std::string toolDeleteBackup(const nlohmann::json& args, bool& outNeedsConfirm);
    std::string toolMcdeployApiCall(const nlohmann::json& args, bool& outNeedsConfirm);

    // --------- Security ---------
    // Resolves a user-supplied relative path within the server directory.
    // Returns true if safe. Populates outAbsolute (may not exist yet).
    bool resolveSafePath(const std::string& relPath, std::filesystem::path& outAbsolute, std::string& outError) const;

    // Whether an operation on this file is dangerous (needs confirmation
    // even in agent mode). Right now that's writes/deletes on server.properties,
    // world files, and eula.txt.
    bool isSensitiveFile(const std::filesystem::path& absolutePath) const;

    // Whether a Minecraft console command needs confirmation.
    bool isDangerousCommand(const std::string& cmd) const;

    // --------- Helpers ---------
    // Truncate a string for feeding back to the model to keep tokens in check.
    static std::string truncateForModel(const std::string& s, size_t maxBytes = 8000);
    // Persist to conversation history table.
    void persistUserMessage(const std::string& content);
    void persistAssistantMessage(const std::string& content, const std::string& toolCallsJson,
                                 int promptTokens, int completionTokens);
    void persistToolResponse(const std::string& toolName, const std::string& toolCallId, const std::string& content);

    // Extract suggested follow-ups from the model's final message.
    static std::vector<std::string> extractSuggestions(std::string& text);
};

// Load AI config from config.json + env vars.
AiConfig loadAiConfig();

} // namespace MCDeploy
