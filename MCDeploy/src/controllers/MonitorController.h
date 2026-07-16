#pragma once

#include <drogon/WebSocketController.h>
#include "../utils/SystemInfo.h"
#include "../utils/ProcessManager.h"
#include "../models/Database.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <iostream>
#include <random>

using namespace drogon;

namespace MCDeploy {

struct MonitorContext {
    std::string server_uuid;
    bool active = true;
    std::thread stream_thread;
};

class MonitorController : public drogon::WebSocketController<MonitorController> {
public:
    void handleNewMessage(const WebSocketConnectionPtr& conn, std::string&& message, const WebSocketMessageType& type) override {
        (void)type;
        try {
            auto j = nlohmann::json::parse(message);
            if (j.contains("action") && j["action"] == "subscribe") {
                std::string uuid = j.value("uuid", "");
                
                auto ctx = std::make_shared<MonitorContext>();
                ctx->server_uuid = uuid;
                ctx->active = true;
                conn->setContext(ctx);

                ctx->stream_thread = std::thread([conn, ctx]() {
                    // Seed random for simulating minor details like TPS / MSPT fluctuations
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_real_distribution<> tpsDis(19.8, 20.0);
                    std::uniform_real_distribution<> msptDis(8.0, 15.0);

                    while (ctx->active) {
                        if (conn->disconnected()) break;

                        SystemMetrics sys = SystemInfo::getInstance().getMetrics();
                        bool isRunning = false;
                        double worldSize = 0.05; // Mock/default size GB
                        
                        if (!ctx->server_uuid.empty()) {
                            isRunning = ProcessManager::getInstance().isServerRunning(ctx->server_uuid);
                            ServerRecord s;
                            if (Database::getInstance().getServer(ctx->server_uuid, s)) {
                                worldSize = SystemInfo::getWorldSizeGB(s.directory_path);
                            } else {
                                worldSize = SystemInfo::getWorldSizeGB("./servers/" + ctx->server_uuid);
                            }
                        }

                        nlohmann::json metrics;
                        metrics["cpu_usage"] = sys.cpu_usage_pct;
                        metrics["ram_used"] = sys.ram_used_gb;
                        metrics["ram_total"] = sys.ram_total_gb;
                        metrics["disk_used"] = sys.disk_used_gb;
                        metrics["disk_total"] = sys.disk_total_gb;
                        
                        // Minecraft server specific telemetry
                        if (isRunning) {
                            metrics["tps"] = tpsDis(gen);
                            metrics["mspt"] = msptDis(gen);
                            metrics["players_online"] = 1; // Simulated active player
                            metrics["players_max"] = 20;
                            metrics["status"] = "Online";
                        } else {
                            metrics["tps"] = 0.0;
                            metrics["mspt"] = 0.0;
                            metrics["players_online"] = 0;
                            metrics["players_max"] = 0;
                            metrics["status"] = "Offline";
                        }
                        metrics["world_size_gb"] = worldSize;

                        nlohmann::json msg;
                        msg["type"] = "metrics";
                        msg["data"] = metrics;

                        conn->send(msg.dump());
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                    }
                });

                nlohmann::json reply;
                reply["type"] = "status";
                reply["data"] = "Subscribed to live telemetry streaming.";
                conn->send(reply.dump());
            }
        } catch (...) {
            nlohmann::json err;
            err["type"] = "error";
            err["data"] = "Failed to process monitor subscription.";
            conn->send(err.dump());
        }
    }

    void handleNewConnection(const HttpRequestPtr& req, const WebSocketConnectionPtr& conn) override {
        (void)req;
        (void)conn;
    }

    void handleConnectionClosed(const WebSocketConnectionPtr& conn) override {
        auto ctx = conn->getContext<MonitorContext>();
        if (ctx) {
            ctx->active = false;
            if (ctx->stream_thread.joinable()) {
                ctx->stream_thread.join();
            }
        }
    }

    WS_PATH_LIST_BEGIN
        WS_PATH_ADD("/api/servers/monitor");
    WS_PATH_LIST_END
};

} // namespace MCDeploy
