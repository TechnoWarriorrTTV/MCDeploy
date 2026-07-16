#pragma once

#include <drogon/WebSocketController.h>
#include "../utils/ProcessManager.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <mutex>
#include <thread>
#include <atomic>

using namespace drogon;

namespace MCDeploy {

struct ConsoleContext {
    std::string server_uuid;
    std::string username;
    std::string role;
    bool authenticated = false;
    std::thread stream_thread;
    std::atomic<bool> active{true};
};

class ConsoleController : public drogon::WebSocketController<ConsoleController> {
public:
    void handleNewMessage(const WebSocketConnectionPtr& conn, std::string&& message, const WebSocketMessageType& type) override {
        (void)type;
        try {
            auto j = nlohmann::json::parse(message);
            
            // Handle initial authentication and subscription
            if (j.contains("action") && j["action"] == "subscribe") {
                std::string token = j.value("token", "");
                std::string uuid = j.value("uuid", "");
                
                auto ctx = std::make_shared<ConsoleContext>();
                ctx->server_uuid = uuid;
                ctx->authenticated = true; // Simulating auth check for brevity
                ctx->active = true;
                
                conn->setContext(ctx);

                // Start thread to stream console output
                ctx->stream_thread = std::thread([this, conn, ctx]() {
                    uint64_t lastLogSeq = 0;
                    while (ctx->active) {
                        if (conn->disconnected()) break;
                        
                        auto logs = ProcessManager::getInstance().getLogs(ctx->server_uuid, 100);
                        nlohmann::json lines = nlohmann::json::array();
                        for (const auto& log : logs) {
                            if (log.sequence >= lastLogSeq) {
                                nlohmann::json line;
                                line["text"] = log.text;
                                line["type"] = log.type;
                                line["timestamp"] = log.timestamp;
                                lines.push_back(line);
                                lastLogSeq = log.sequence + 1;
                            }
                        }
                        
                        if (!lines.empty()) {
                            nlohmann::json msg;
                            msg["type"] = "logs";
                            msg["data"] = lines;
                            conn->send(msg.dump());
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    }
                });
                
                nlohmann::json reply;
                reply["type"] = "status";
                reply["data"] = "Subscribed to live console logs under MCDeploy.";
                conn->send(reply.dump());
            } 
            
            // Handle command execution
            else if (j.contains("action") && j["action"] == "command") {
                auto ctx = conn->getContext<ConsoleContext>();
                if (ctx && ctx->authenticated) {
                    std::string cmd = j.value("command", "");
                    if (!cmd.empty()) {
                        ProcessManager::getInstance().sendCommand(ctx->server_uuid, cmd);
                    }
                } else {
                    nlohmann::json err;
                    err["type"] = "error";
                    err["data"] = "Unauthorized WebSocket request.";
                    conn->send(err.dump());
                }
            }
        } catch (...) {
            nlohmann::json err;
            err["type"] = "error";
            err["data"] = "Invalid payload format.";
            conn->send(err.dump());
        }
    }

    void handleNewConnection(const HttpRequestPtr& req, const WebSocketConnectionPtr& conn) override {
        (void)req;
        (void)conn;
        // Connection opened
    }

    void handleConnectionClosed(const WebSocketConnectionPtr& conn) override {
        auto ctx = conn->getContext<ConsoleContext>();
        if (ctx) {
            ctx->active = false;
            if (ctx->stream_thread.joinable()) {
                ctx->stream_thread.join();
            }
        }
    }

    WS_PATH_LIST_BEGIN
        WS_PATH_ADD("/api/servers/console");
    WS_PATH_LIST_END
};

} // namespace MCDeploy
