#include <drogon/drogon.h>
#include "models/Database.h"
#include "utils/ProcessManager.h"
#include "utils/BrowserWindow.h"
#include "utils/AppPaths.h"
#include "utils/DnsManager.h"
#include "utils/Scheduler.h"
#include "controllers/ConsoleController.h"
#include "controllers/MonitorController.h"
#include "controllers/PlayerController.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <csignal>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

// Probe whether we can actually bind to a port. Uses SO_EXCLUSIVEADDRUSE so
// we get a proper failure if the port is held by a zombie socket left over
// from a crashed prior instance — which does happen on Windows if mcdeploy.exe
// was killed uncleanly.
static bool canBindPort(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return true;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { WSACleanup(); return true; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char*)&one, sizeof(one));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    int rc = bind(s, (sockaddr*)&addr, sizeof(addr));
    closesocket(s);
    WSACleanup();
    return rc == 0;
#else
    (void)port;
    return true; // trust the OS on non-Windows
#endif
}

using namespace MCDeploy;
using namespace drogon;

static std::string getHomeDirectory() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return home ? std::string(home) : ".";
}

static std::string getServersDir() {
    std::ifstream configFile("config.json");
    if (configFile.is_open()) {
        try {
            nlohmann::json j;
            configFile >> j;
            if (j.contains("mcdeploy") && j["mcdeploy"].contains("servers_dir")) {
                return j["mcdeploy"]["servers_dir"].get<std::string>();
            }
        } catch (...) {}
    }
    return getHomeDirectory() + "/MCDeploy/Servers";
}

static std::string getBackupsDir() {
    std::ifstream configFile("config.json");
    if (configFile.is_open()) {
        try {
            nlohmann::json j;
            configFile >> j;
            if (j.contains("mcdeploy") && j["mcdeploy"].contains("backups_dir")) {
                return j["mcdeploy"]["backups_dir"].get<std::string>();
            }
        } catch (...) {}
    }
    return getHomeDirectory() + "/MCDeploy/Backups";
}

static std::string resolveDocumentRoot() {
    const auto executableDirectory = MCDeployPaths::executableDirectory();
    const std::vector<std::filesystem::path> candidates = {
        executableDirectory / "dist",
        executableDirectory.parent_path() / "dist",
        executableDirectory.parent_path().parent_path() / "dist",
        executableDirectory.parent_path().parent_path().parent_path() / "dist"
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path / "index.html")) {
            std::cout << "[MCDeploy] Found static document root at: " << path.string() << std::endl;
            return path.string();
        }
    }
    return (executableDirectory / "dist").string();
}

// Global signal handler helper
static void signalHandler(int signum) {
    std::cout << "\n[MCDeploy] Intercepted shutdown signal (" << signum << "). Gracefully stopping all Minecraft servers..." << std::endl;
    Scheduler::getInstance().stop();
    ProcessManager::getInstance().shutdownAll();
    Database::getInstance().close();
    exit(signum);
}

int main() {
    if (!MCDeployPaths::initializeRuntimeDirectory()) {
        std::cerr << "[MCDeploy] Critical Error: could not initialize the per-user data directory at "
                  << MCDeployPaths::dataDirectory().string() << std::endl;
        return 1;
    }

    std::cout << "[MCDeploy] Runtime data directory: "
              << MCDeployPaths::dataDirectory().string() << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "          MCDeploy Minecraft Server Panel         " << std::endl;
    std::cout << "==================================================" << std::endl;

    // Load configurations from config.json
    std::string host = "0.0.0.0";
    uint16_t port = 8082;
    std::string dbPath = "mcdeploy.db";
    std::string documentRoot = resolveDocumentRoot();

    std::ifstream configFile("config.json");
    if (configFile.is_open()) {
        try {
            nlohmann::json j;
            configFile >> j;
            if (j.contains("mcdeploy")) {
                auto mc = j["mcdeploy"];
                host = mc.value("host", "0.0.0.0");
                port = mc.value<uint16_t>("port", 8082);
                if (mc.contains("database")) {
                    dbPath = mc["database"].value("path", "mcdeploy.db");
                }
                std::cout << "[MCDeploy] Config file loaded successfully." << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[MCDeploy] Warning: Error parsing config.json (" << e.what() << "). Using defaults." << std::endl;
        }
        configFile.close();
    } else {
        std::cout << "[MCDeploy] Warning: config.json not found. Creating default configuration." << std::endl;
        // Write default configuration file
        nlohmann::json defaultConfig;
        defaultConfig["mcdeploy"] = {
            {"host", "0.0.0.0"},
            {"port", 8082},
            {"database", {{"path", "mcdeploy.db"}}},
            {"servers_dir", getServersDir()},
            {"backups_dir", getBackupsDir()}
        };
        std::ofstream defaultFile("config.json");
        defaultFile << defaultConfig.dump(2);
        defaultFile.close();
    }

    // Keep relative database paths in the per-user Local AppData runtime directory.
    if (std::filesystem::path(dbPath).is_relative()) {
        dbPath = (MCDeployPaths::dataDirectory() / dbPath).string();
    }

    // Initialize Database
    std::cout << "[MCDeploy] Initializing SQLite database at: " << dbPath << "..." << std::endl;
    if (!Database::getInstance().initialize(dbPath)) {
        std::cerr << "[MCDeploy] Critical Error: Failed to initialize SQLite database. Exiting." << std::endl;
        return 1;
    }

    // Upgrade reconciliation: old versions removed DNS records when a server stopped.
    // Claim persistent authoritative reservations for every existing offline hostname.
    if (MCDeploy::DnsManager::getInstance().isEnabled()) {
        for (const auto& server : Database::getInstance().getAllServers()) {
            if (server.subdomain.empty() || !server.dns_a_record_id.empty()) continue;
            const auto reservation = MCDeploy::DnsManager::getInstance().reserveSubdomain(server.subdomain);
            if (reservation.success) {
                Database::getInstance().updateDnsRecordIds(server.uuid, reservation.record_id, "");
                Database::getInstance().logAction("SYSTEM", "DNS_RESERVATION_MIGRATED", server.uuid,
                    "Reserved existing hostname " + server.subdomain + ".mcdeploy.online");
            } else {
                Database::getInstance().logAction("SYSTEM", "DNS_RESERVATION_CONFLICT", server.uuid,
                    "Could not globally reserve existing hostname " + server.subdomain + ".mcdeploy.online");
                std::cerr << "[MCDeploy DNS] Existing hostname could not be reserved globally: "
                          << server.subdomain << ".mcdeploy.online" << std::endl;
            }
        }
    }

    // Create directories
    std::filesystem::create_directories(getServersDir());
    std::filesystem::create_directories(getBackupsDir());
    std::filesystem::create_directories(documentRoot);

    // Set up crash listener for process manager to auto-log in database
    ProcessManager::getInstance().setCrashCallback([](const std::string& uuid, const std::string& reason) {
        std::cout << "[MCDeploy Monitor] Server " << uuid << " crashed! Reason: " << reason << std::endl;
        Database::getInstance().updateServerStatus(uuid, "Crashed");
        Database::getInstance().logAction("SYSTEM", "CRASH_DETECTED", uuid, reason);
    });

    // If the desired port is held by a zombie socket (e.g. from a prior mcdeploy.exe
    // that was force-killed), scan forward to find one we can actually bind to. This
    // is a common issue on Windows where sockets from crashed processes linger
    // until reboot. Without this check drogon "succeeds" but nothing is served.
    uint16_t requestedPort = port;
    if (!canBindPort(port)) {
        std::cerr << "[MCDeploy] Warning: port " << port << " is unavailable "
                  << "(possibly held by an orphaned socket from a prior crash)."
                  << std::endl;
        for (uint16_t candidate = port + 1; candidate <= port + 20; candidate++) {
            if (canBindPort(candidate)) {
                port = candidate;
                std::cerr << "[MCDeploy] Falling back to port " << port
                          << ". Configure a different default in config.json to persist this." << std::endl;
                break;
            }
        }
        if (port == requestedPort) {
            std::cerr << "[MCDeploy] Critical: could not find an available port near "
                      << requestedPort << ". Reboot to release zombie sockets." << std::endl;
        }
    }

    // Configure Drogon web application
    std::cout << "[MCDeploy] Binding network listener on " << host << ":" << port << "..." << std::endl;
    drogon::app().addListener(host, port);
    drogon::app().setDocumentRoot(documentRoot);
    drogon::app().setClientMaxBodySize(1024 * 1024 * 100); // 100MB max uploads for plugins/backups
    
    // Explicit preflight response for a separately hosted React webpanel.
    drogon::app().registerHandler("/api/*", [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    }, {Options});

    // Serve index.html for SPA router fallback paths (so React Router works properly)
    drogon::app().registerHandler("/*", [documentRoot](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        if (req->path().rfind("/api/", 0) == 0) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k404NotFound);
            callback(resp);
        } else {
            auto resp = HttpResponse::newFileResponse(documentRoot + "/index.html");
            callback(resp);
        }
    }, {Get});

    // Global CORS advisor to ease react development
    drogon::app().registerPostHandlingAdvice([](const HttpRequestPtr& req, const HttpResponsePtr& resp) {
        (void)req;
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    });

    // Register signal handlers for clean process termination
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Boot the scheduled-tasks poller. Uses the same backups directory as
    // the interactive backup endpoint so scheduled backups end up alongside
    // manual ones.
    Scheduler::getInstance().start(getBackupsDir());

    std::cout << "[MCDeploy] Web Admin Dashboard initialized. Running server network loop." << std::endl;

    // The Windows installer always uses MCDeploy's native WebView2 window.
    // MCDEPLOY_NO_BROWSER remains available only for automated/headless operation.
    std::string dashboardUrl = "http://localhost:" + std::to_string(port);

    if (std::getenv("MCDEPLOY_NO_BROWSER") != nullptr) {
        std::cout << "[MCDeploy] Headless mode enabled. Dashboard available at "
                  << dashboardUrl << std::endl;
    } else {
        std::thread([dashboardUrl]() {
            // Brief pause so drogon has bound its socket before the window navigates.
            std::this_thread::sleep_for(std::chrono::milliseconds(600));

#ifdef _WIN32
            std::cout << "[MCDeploy] Opening native MCDeploy window at " << dashboardUrl << std::endl;
            const bool opened = MCDeploy::runMcdeployWindow(dashboardUrl, []() {
                std::cout << "[MCDeploy] Window closed - shutting down server..." << std::endl;
                drogon::app().quit();
            });
            if (!opened) {
                std::cerr << "[MCDeploy] Native window failed to initialize. Repair the WebView2 Runtime installation." << std::endl;
                drogon::app().quit();
            }
#else
            std::cerr << "[MCDeploy] The native MCDeploy window is only available on Windows." << std::endl;
            drogon::app().quit();
#endif
        }).detach();
    }

    drogon::app().run();

    return 0;
}
