#include "ServerController.h"
#include "../models/Database.h"
#include "../utils/VersionFetcher.h"
#include "../utils/ProcessManager.h"
#include "../utils/DnsManager.h"
#include "../utils/TunnelManager.h"
#include "../utils/ServerLifecycle.h"
#include "../utils/Scheduler.h"
#include "../utils/SystemInfo.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <thread>
#include <random>
#include <iostream>
#include <algorithm>
#include <regex>

namespace MCDeploy {

namespace fs = std::filesystem;

static bool extractZip(const std::string& zipPath, const std::string& destPath);

static const std::string JWT_SECRET = "MCDEPLOY_SUPER_SECRET_TOKEN_CHANGE_ME_IN_PRODUCTION";
static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static HttpResponsePtr newJsonResponse(const nlohmann::json& j) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setBody(j.dump());
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    return resp;
}

static void killProcessOnPort(int port) {
    if (port <= 0) return;
#ifdef _WIN32
    std::string cmd = "powershell.exe -Command \"Get-NetTCPConnection -LocalPort " + std::to_string(port) + " -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue }\"";
    system(cmd.c_str());
#else
    std::string cmd = "fuser -k " + std::to_string(port) + "/tcp >/dev/null 2>&1 || true";
    system(cmd.c_str());
#endif
}

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
    return getHomeDirectory() + "/servers";
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
    return getHomeDirectory() + "/backups";
}

static std::string formatDateTime(std::time_t time_val) {
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

bool ServerController::validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole) {
    (void)req;
    outUsername = "admin";
    outRole = "admin";
    return true;
}

std::string ServerController::generateUuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4"; // UUID v4
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis(gen); // Variant
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);
    return ss.str();
}

void ServerController::listServers(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    auto servers = Database::getInstance().getAllServers();
    nlohmann::json res = nlohmann::json::array();
    
    for (auto& s : servers) {
        // Sync active status with process manager
        bool running = ProcessManager::getInstance().isServerRunning(s.uuid);
        if (running && s.status != "Online" && s.status != "Starting") {
            s.status = "Online";
            Database::getInstance().updateServerStatus(s.uuid, "Online");
        } else if (!running && s.status != "Offline" && s.status != "Installing" && s.status != "Crashed") {
            s.status = "Offline";
            Database::getInstance().updateServerStatus(s.uuid, "Offline");
        }

        nlohmann::json item;
        item["uuid"] = s.uuid;
        item["name"] = s.name;
        item["software_type"] = s.software_type;
        item["version"] = s.version;
        item["port"] = s.port;
        item["ram_min"] = s.ram_min;
        item["ram_max"] = s.ram_max;
        item["status"] = s.status;
        item["created_at"] = s.created_at;
        item["subdomain"] = s.subdomain;
        if (!s.subdomain.empty()) {
            item["address"] = DnsManager::getInstance().getFullAddress(s.subdomain);
            item["dns_active"] = !s.dns_a_record_id.empty();
        } else {
            item["address"] = "";
            item["dns_active"] = false;
        }
        res.push_back(item);
    }

    callback(newJsonResponse(res));
}

void ServerController::createServer(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    if (role != "admin") {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k403Forbidden);
        resp->setBody("Only administrators can create servers");
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string name = json->get("name", "").asString();
    std::string softwareType = json->get("software_type", "").asString();
    std::string version = json->get("version", "").asString();
    int port = json->get("port", 25565).asInt();
    int ramMin = json->get("ram_min", 1024).asInt();
    int ramMax = json->get("ram_max", 2048).asInt();
    std::string subdomain = json->get("subdomain", "").asString();

    std::string modpackSource = json->get("modpack_source", "").asString();
    std::string modpackId = json->get("modpack_id", "").asString();
    std::string modpackVersionId = json->get("modpack_version_id", "").asString();
    std::string serverPackUrl = json->get("server_pack_url", "").asString();
    std::string serverPackFileId = json->get("server_pack_file_id", "").asString();
    std::string gameVersion = json->get("game_version", "").asString();
    std::string loaderType = json->get("loader_type", "").asString();
    std::string apiKey = json->get("apiKey", "").asString();

    if (name.empty() || softwareType.empty() || version.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("Missing required server details");
        callback(resp);
        return;
    }

    // Validate and sanitize subdomain
    if (!subdomain.empty()) {
        // Convert to lowercase
        std::transform(subdomain.begin(), subdomain.end(), subdomain.begin(), ::tolower);
        
        // Validate: 3-63 chars, lowercase alphanumeric + hyphens, no leading/trailing hyphens
        std::regex subdomainRegex("^[a-z0-9]([a-z0-9-]{1,61}[a-z0-9])?$");
        if (!std::regex_match(subdomain, subdomainRegex)) {
            nlohmann::json errorRes;
            errorRes["status"] = "error";
            errorRes["message"] = "Invalid subdomain. Use 3-63 lowercase letters, numbers, and hyphens. Must start and end with a letter or number.";
            auto resp = newJsonResponse(errorRes);
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        static const std::vector<std::string> reserved = {
            "www", "mail", "ftp", "admin", "api", "app", "panel",
            "dashboard", "status", "blog", "docs", "help", "support",
            "ns1", "ns2", "mx", "smtp", "imap", "pop", "mcdeploy", "offline"
        };
        if (std::find(reserved.begin(), reserved.end(), subdomain) != reserved.end()) {
            nlohmann::json errorRes;
            errorRes["status"] = "error";
            errorRes["message"] = "The requested subdomain is reserved.";
            auto resp = newJsonResponse(errorRes);
            resp->setStatusCode(k409Conflict);
            callback(resp);
            return;
        }

        // Local defense-in-depth; global ownership is reserved in authoritative DNS below.
        if (Database::getInstance().isSubdomainTaken(subdomain)) {
            nlohmann::json errorRes;
            errorRes["status"] = "error";
            errorRes["message"] = "The subdomain '" + subdomain + ".mcdeploy.online' is already taken. Please choose a different one.";
            auto resp = newJsonResponse(errorRes);
            resp->setStatusCode(k409Conflict);
            callback(resp);
            return;
        }
    }

    // Port check
    auto allServers = Database::getInstance().getAllServers();
    for (const auto& s : allServers) {
        if (s.port == port) {
            nlohmann::json errorRes;
            errorRes["status"] = "error";
            errorRes["message"] = "Port conflict detected. Port " + std::to_string(port) + " is already in use.";
            auto resp = newJsonResponse(errorRes);
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }
    }

    DnsRecordResult reservation;
    if (!subdomain.empty()) {
        if (!DnsManager::getInstance().isEnabled()) {
            nlohmann::json errorRes;
            errorRes["status"] = "error";
            errorRes["message"] = "Global subdomain reservations are temporarily unavailable. No server was created.";
            auto resp = newJsonResponse(errorRes);
            resp->setStatusCode(k503ServiceUnavailable);
            callback(resp);
            return;
        }

        reservation = DnsManager::getInstance().reserveSubdomain(subdomain);
        if (!reservation.success) {
            const auto availability = DnsManager::getInstance().checkSubdomainAvailability(subdomain);
            nlohmann::json errorRes;
            errorRes["status"] = "error";
            const bool taken = availability.status == DnsAvailabilityStatus::Taken;
            errorRes["message"] = taken
                ? "The subdomain '" + subdomain + ".mcdeploy.online' was reserved by another MCDeploy server. Please choose a different one."
                : "The global subdomain reservation service could not confirm ownership. No server was created.";
            auto resp = newJsonResponse(errorRes);
            resp->setStatusCode(taken ? k409Conflict : k503ServiceUnavailable);
            callback(resp);
            return;
        }
    }

    std::string uuid = generateUuid();
    std::string serversDir = getServersDir();
    std::string serverPath = serversDir + "/" + uuid;

    ServerRecord record;
    record.uuid = uuid;
    record.name = name;
    record.software_type = softwareType;
    record.version = version;
    record.port = port;
    record.ram_min = ramMin;
    record.ram_max = ramMax;
    record.status = "Installing";
    record.directory_path = serverPath;
    record.subdomain = subdomain;
    record.dns_a_record_id = reservation.record_id;
    record.dns_srv_record_id.clear();
    
    // Windows/Linux specific startup wrapper
#ifdef _WIN32
    record.start_command = "java -Xms" + std::to_string(ramMin) + "M -Xmx" + std::to_string(ramMax) + "M -XX:+UseG1GC -jar server.jar nogui";
#else
    record.start_command = "java -Xms" + std::to_string(ramMin) + "M -Xmx" + std::to_string(ramMax) + "M -XX:+UseG1GC -jar server.jar nogui";
#endif

    auto now = std::chrono::system_clock::now();
    record.created_at = formatDateTime(std::chrono::system_clock::to_time_t(now));

    if (!Database::getInstance().createServer(record)) {
        if (!reservation.record_id.empty()) {
            DnsManager::getInstance().deleteRecord(reservation.record_id);
        }
        nlohmann::json errorRes;
        errorRes["status"] = "error";
        errorRes["message"] = "The server could not be saved. Its subdomain reservation was released.";
        auto resp = newJsonResponse(errorRes);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }

    // Launch installation background thread
    std::thread([uuid, serverPath, softwareType, version, port, username, ramMin, ramMax,
                 modpackSource, modpackId, modpackVersionId, serverPackUrl, serverPackFileId,
                 gameVersion, loaderType, apiKey]() {
        try {
            fs::create_directories(serverPath);
            bool success = false;
            std::string actualLoader = loaderType;
            std::string actualGameVersion = gameVersion;

            if (softwareType == "modpack") {
                std::string downloadUrl = serverPackUrl;
                if (modpackSource == "curseforge") {
                    std::string currentApiKey = apiKey;
                    if (currentApiKey.empty()) {
                        currentApiKey = "$2a$10$bLva59BlqnwDpxbDBAEG7.tD.R3kv71qa9CZLa9TWDWNsccecFoFi";
                    }
                    std::string fileUrl = "https://api.curseforge.com/v1/mods/" + modpackId + "/files/" + serverPackFileId;
                    std::vector<std::string> headers = {
                        "x-api-key: " + currentApiKey,
                        "Accept: application/json"
                    };
                    std::string fileRes = VersionFetcher::getInstance().httpGetWithHeaders(fileUrl, headers);
                    if (!fileRes.empty()) {
                        try {
                            auto fileJson = nlohmann::json::parse(fileRes);
                            if (fileJson.contains("data") && fileJson["data"].contains("downloadUrl")) {
                                downloadUrl = fileJson["data"]["downloadUrl"].get<std::string>();
                            }
                        } catch (...) {}
                    }
                }

                if (downloadUrl.empty()) {
                    std::cerr << "[MCDeploy Installer] Failed to resolve modpack server pack URL" << std::endl;
                    Database::getInstance().updateServerStatus(uuid, "Crashed");
                    Database::getInstance().logAction(username, "CREATE_SERVER_FAILED", uuid, "Failed to resolve modpack download URL.");
                    return;
                }

                std::string zipPath = serverPath + "/serverpack.zip";
                std::cout << "[MCDeploy Installer] Downloading modpack server pack from: " << downloadUrl << std::endl;
                success = VersionFetcher::getInstance().downloadFile(downloadUrl, zipPath);
                if (!success) {
                    std::cerr << "[MCDeploy Installer] Failed to download server pack zip." << std::endl;
                    Database::getInstance().updateServerStatus(uuid, "Crashed");
                    Database::getInstance().logAction(username, "CREATE_SERVER_FAILED", uuid, "Failed to download server pack ZIP.");
                    return;
                }

                std::cout << "[MCDeploy Installer] Extracting server pack..." << std::endl;
                success = extractZip(zipPath, serverPath);
                try {
                    fs::remove(zipPath);
                } catch (...) {}

                if (!success) {
                    std::cerr << "[MCDeploy Installer] Failed to extract server pack zip." << std::endl;
                    Database::getInstance().updateServerStatus(uuid, "Crashed");
                    Database::getInstance().logAction(username, "CREATE_SERVER_FAILED", uuid, "Failed to extract server pack ZIP.");
                    return;
                }

                // Check if extracted pack is a client-only CurseForge or Modrinth pack
                bool isCurseforgeClient = fs::exists(serverPath + "/manifest.json");
                bool isModrinthClient = fs::exists(serverPath + "/modrinth.index.json");

                if (isCurseforgeClient) {
                    std::cout << "[MCDeploy Installer] Processing CurseForge client modpack manifest..." << std::endl;
                    try {
                        std::ifstream manifestFile(serverPath + "/manifest.json");
                        nlohmann::json manifestJson = nlohmann::json::parse(manifestFile);
                        manifestFile.close();

                        if (manifestJson.contains("files") && manifestJson["files"].is_array()) {
                            fs::create_directories(serverPath + "/mods");
                            std::string currentApiKey = apiKey;
                            if (currentApiKey.empty()) {
                                currentApiKey = "$2a$10$bLva59BlqnwDpxbDBAEG7.tD.R3kv71qa9CZLa9TWDWNsccecFoFi";
                            }

                            for (const auto& fileItem : manifestJson["files"]) {
                                int projectID = fileItem.value("projectID", 0);
                                int fileID = fileItem.value("fileID", 0);
                                if (projectID > 0 && fileID > 0) {
                                    std::string fileUrl = "https://api.curseforge.com/v1/mods/" + std::to_string(projectID) + "/files/" + std::to_string(fileID);
                                    std::vector<std::string> headers = {
                                        "x-api-key: " + currentApiKey,
                                        "Accept: application/json"
                                    };
                                    std::string fileRes = VersionFetcher::getInstance().httpGetWithHeaders(fileUrl, headers);
                                    if (!fileRes.empty()) {
                                        auto fileJson = nlohmann::json::parse(fileRes);
                                        if (fileJson.contains("data") && fileJson["data"].contains("downloadUrl")) {
                                            std::string modDownloadUrl = fileJson["data"]["downloadUrl"].get<std::string>();
                                            std::string fileName = fileJson["data"].value("fileName", "mod_" + std::to_string(fileID) + ".jar");
                                            std::cout << "[MCDeploy Installer] Downloading mod: " << fileName << std::endl;
                                            VersionFetcher::getInstance().downloadFile(modDownloadUrl, serverPath + "/mods/" + fileName);
                                        }
                                    }
                                }
                            }
                        }

                        // Copy overrides
                        std::string overridesPath = serverPath + "/overrides";
                        if (fs::exists(overridesPath) && fs::is_directory(overridesPath)) {
                            std::cout << "[MCDeploy Installer] Copying overrides to server root..." << std::endl;
                            for (const auto& entry : fs::recursive_directory_iterator(overridesPath)) {
                                const auto& path = entry.path();
                                auto relativePath = fs::relative(path, overridesPath);
                                auto destPath = fs::path(serverPath) / relativePath;
                                if (entry.is_directory()) {
                                    fs::create_directories(destPath);
                                } else if (entry.is_regular_file()) {
                                    fs::create_directories(destPath.parent_path());
                                    fs::copy_file(path, destPath, fs::copy_options::overwrite_existing);
                                }
                            }
                            fs::remove_all(overridesPath);
                        }

                        fs::remove(serverPath + "/manifest.json");
                    } catch (const std::exception& e) {
                        std::cerr << "[MCDeploy Installer] Error parsing CurseForge client manifest: " << e.what() << std::endl;
                    }
                } else if (isModrinthClient) {
                    std::cout << "[MCDeploy Installer] Processing Modrinth client modpack manifest..." << std::endl;
                    try {
                        std::ifstream indexFile(serverPath + "/modrinth.index.json");
                        nlohmann::json indexJson = nlohmann::json::parse(indexFile);
                        indexFile.close();

                        if (indexJson.contains("files") && indexJson["files"].is_array()) {
                            for (const auto& fileItem : indexJson["files"]) {
                                std::string pathStr = fileItem.value("path", "");
                                if (!pathStr.empty()) {
                                    bool isServerSide = true;
                                    if (fileItem.contains("env")) {
                                        std::string serverEnv = fileItem["env"].value("server", "required");
                                        if (serverEnv == "unsupported") {
                                            isServerSide = false;
                                        }
                                    }

                                    if (isServerSide && fileItem.contains("downloads") && fileItem["downloads"].is_array() && !fileItem["downloads"].empty()) {
                                        std::string fileDownloadUrl = fileItem["downloads"][0].get<std::string>();
                                        fs::path fullDestPath = fs::path(serverPath) / pathStr;
                                        fs::create_directories(fullDestPath.parent_path());
                                        std::cout << "[MCDeploy Installer] Downloading: " << pathStr << std::endl;
                                        VersionFetcher::getInstance().downloadFile(fileDownloadUrl, fullDestPath.string());
                                    }
                                }
                            }
                        }

                        // Copy overrides
                        std::string overridesPath = serverPath + "/overrides";
                        if (fs::exists(overridesPath) && fs::is_directory(overridesPath)) {
                            std::cout << "[MCDeploy Installer] Copying overrides to server root..." << std::endl;
                            for (const auto& entry : fs::recursive_directory_iterator(overridesPath)) {
                                const auto& path = entry.path();
                                auto relativePath = fs::relative(path, overridesPath);
                                auto destPath = fs::path(serverPath) / relativePath;
                                if (entry.is_directory()) {
                                    fs::create_directories(destPath);
                                } else if (entry.is_regular_file()) {
                                    fs::create_directories(destPath.parent_path());
                                    fs::copy_file(path, destPath, fs::copy_options::overwrite_existing);
                                }
                            }
                            fs::remove_all(overridesPath);
                        }

                        fs::remove(serverPath + "/modrinth.index.json");
                    } catch (const std::exception& e) {
                        std::cerr << "[MCDeploy Installer] Error parsing Modrinth client manifest: " << e.what() << std::endl;
                    }
                }

                // Scan for installer jars inside the folder
                std::string forgeInstallerPath = "";
                std::string fabricInstallerPath = "";
                std::string neoforgeInstallerPath = "";
                for (const auto& entry : fs::directory_iterator(serverPath)) {
                    if (entry.is_regular_file()) {
                        std::string fname = entry.path().filename().string();
                        std::string fnameLower = fname;
                        std::transform(fnameLower.begin(), fnameLower.end(), fnameLower.begin(), ::tolower);
                        if (fnameLower.find("neoforge") != std::string::npos && fnameLower.find("installer") != std::string::npos && fnameLower.substr(fnameLower.length() - 4) == ".jar") {
                            neoforgeInstallerPath = entry.path().string();
                        } else if (fnameLower.find("forge") != std::string::npos && fnameLower.find("installer") != std::string::npos && fnameLower.substr(fnameLower.length() - 4) == ".jar") {
                            forgeInstallerPath = entry.path().string();
                        } else if (fnameLower.find("fabric") != std::string::npos && fnameLower.find("installer") != std::string::npos && fnameLower.substr(fnameLower.length() - 4) == ".jar") {
                            fabricInstallerPath = entry.path().string();
                        }
                    }
                }

                if (!neoforgeInstallerPath.empty()) {
                    actualLoader = "neoforge";
                } else if (!forgeInstallerPath.empty()) {
                    actualLoader = "forge";
                } else if (!fabricInstallerPath.empty()) {
                    actualLoader = "fabric";
                }

                // Run installers if found
                if (!neoforgeInstallerPath.empty()) {
                    std::cout << "[MCDeploy Installer] Running embedded NeoForge installer: " << neoforgeInstallerPath << std::endl;
                    std::string cmd;
#ifdef _WIN32
                    cmd = "powershell.exe -Command \"cd '" + serverPath + "'; java -jar '" + neoforgeInstallerPath + "' --installServer\"";
#else
                    cmd = "cd \"" + serverPath + "\" && java -jar \"" + neoforgeInstallerPath + "\" --installServer";
#endif
                    system(cmd.c_str());
                    try {
                        fs::remove(neoforgeInstallerPath);
                        if (fs::exists(neoforgeInstallerPath + ".log")) {
                            fs::remove(neoforgeInstallerPath + ".log");
                        }
                    } catch (...) {}
                } else if (!forgeInstallerPath.empty()) {
                    std::cout << "[MCDeploy Installer] Running embedded Forge installer: " << forgeInstallerPath << std::endl;
                    std::string cmd;
#ifdef _WIN32
                    cmd = "powershell.exe -Command \"cd '" + serverPath + "'; java -jar '" + forgeInstallerPath + "' --installServer\"";
#else
                    cmd = "cd \"" + serverPath + "\" && java -jar \"" + forgeInstallerPath + "\" --installServer";
#endif
                    system(cmd.c_str());
                    try {
                        fs::remove(forgeInstallerPath);
                        if (fs::exists(forgeInstallerPath + ".log")) {
                            fs::remove(forgeInstallerPath + ".log");
                        }
                    } catch (...) {}
                } else if (!fabricInstallerPath.empty()) {
                    std::cout << "[MCDeploy Installer] Running embedded Fabric installer: " << fabricInstallerPath << std::endl;
                    std::string cmd;
#ifdef _WIN32
                    cmd = "powershell.exe -Command \"cd '" + serverPath + "'; java -jar '" + fabricInstallerPath + "' server -downloadMinecraft\"";
#else
                    cmd = "cd \"" + serverPath + "\" && java -jar \"" + fabricInstallerPath + "\" server -downloadMinecraft";
#endif
                    system(cmd.c_str());
                    try {
                        fs::remove(fabricInstallerPath);
                    } catch (...) {}
                } else {
                    // No embedded installer, install the loader specified by actualLoader/loaderType
                    if (actualLoader == "neoforge") {
                        std::string installerUrl = VersionFetcher::getInstance().getDownloadUrl("neoforge", actualGameVersion);
                        if (!installerUrl.empty()) {
                            std::string localInstaller = serverPath + "/neoforge_installer.jar";
                            success = VersionFetcher::getInstance().downloadFile(installerUrl, localInstaller);
                            if (success) {
                                std::string cmd;
#ifdef _WIN32
                                cmd = "powershell.exe -Command \"cd '" + serverPath + "'; java -jar neoforge_installer.jar --installServer\"";
#else
                                cmd = "cd \"" + serverPath + "\" && java -jar neoforge_installer.jar --installServer";
#endif
                                system(cmd.c_str());
                                try {
                                    fs::remove(localInstaller);
                                    if (fs::exists(localInstaller + ".log")) {
                                        fs::remove(localInstaller + ".log");
                                    }
                                } catch (...) {}
                            }
                        }
                    } else if (actualLoader == "forge") {
                        std::string installerUrl = VersionFetcher::getInstance().getDownloadUrl("forge", actualGameVersion);
                        if (!installerUrl.empty()) {
                            std::string localInstaller = serverPath + "/forge_installer.jar";
                            success = VersionFetcher::getInstance().downloadFile(installerUrl, localInstaller);
                            if (success) {
                                std::string cmd;
#ifdef _WIN32
                                cmd = "powershell.exe -Command \"cd '" + serverPath + "'; java -jar forge_installer.jar --installServer\"";
#else
                                cmd = "cd \"" + serverPath + "\" && java -jar forge_installer.jar --installServer";
#endif
                                system(cmd.c_str());
                                try {
                                    fs::remove(localInstaller);
                                    if (fs::exists(localInstaller + ".log")) {
                                        fs::remove(localInstaller + ".log");
                                    }
                                } catch (...) {}
                            }
                        }
                    } else if (actualLoader == "fabric") {
                        std::string loaderDownloadUrl = VersionFetcher::getInstance().getDownloadUrl("fabric", actualGameVersion);
                        std::string jarPath = serverPath + "/server.jar";
                        VersionFetcher::getInstance().downloadFile(loaderDownloadUrl, jarPath);
                    }
                }

                // Startup properties setup
                if (actualLoader == "neoforge") {
                    std::string neoforgeLibPath = serverPath + "/libraries/net/neoforged/neoforge";
                    std::string neoforgeVersion = "";
                    if (fs::exists(neoforgeLibPath)) {
                        for (const auto& entry : fs::directory_iterator(neoforgeLibPath)) {
                            if (entry.is_directory()) {
                                neoforgeVersion = entry.path().filename().string();
                                break;
                            }
                        }
                    }

                    if (!neoforgeVersion.empty()) {
                        std::string jvmArgsPath = serverPath + "/user_jvm_args.txt";
                        std::ofstream jvmArgsFile(jvmArgsPath);
                        jvmArgsFile << "# Generated by MCDeploy\n";
                        jvmArgsFile << "-Xms" << ramMin << "M\n";
                        jvmArgsFile << "-Xmx" << ramMax << "M\n";
                        jvmArgsFile.close();

                        std::string startCommand;
#ifdef _WIN32
                        startCommand = "java @user_jvm_args.txt @libraries/net/neoforged/neoforge/" + neoforgeVersion + "/win_args.txt nogui";
#else
                        startCommand = "java @user_jvm_args.txt @libraries/net/neoforged/neoforge/" + neoforgeVersion + "/unix_args.txt nogui";
#endif
                        Database::getInstance().updateServerStartCommand(uuid, startCommand);
                    }
                } else if (actualLoader == "forge") {
                    std::string forgeLibPath = serverPath + "/libraries/net/minecraftforge/forge";
                    std::string forgeVersion = "";
                    if (fs::exists(forgeLibPath)) {
                        for (const auto& entry : fs::directory_iterator(forgeLibPath)) {
                            if (entry.is_directory()) {
                                forgeVersion = entry.path().filename().string();
                                break;
                            }
                        }
                    }

                    if (!forgeVersion.empty()) {
                        std::string jvmArgsPath = serverPath + "/user_jvm_args.txt";
                        std::ofstream jvmArgsFile(jvmArgsPath);
                        jvmArgsFile << "# Generated by MCDeploy\n";
                        jvmArgsFile << "-Xms" << ramMin << "M\n";
                        jvmArgsFile << "-Xmx" << ramMax << "M\n";
                        jvmArgsFile.close();

                        std::string startCommand;
#ifdef _WIN32
                        startCommand = "java @user_jvm_args.txt @libraries/net/minecraftforge/forge/" + forgeVersion + "/win_args.txt nogui";
#else
                        startCommand = "java @user_jvm_args.txt @libraries/net/minecraftforge/forge/" + forgeVersion + "/unix_args.txt nogui";
#endif
                        Database::getInstance().updateServerStartCommand(uuid, startCommand);
                    } else {
                        std::string legacyJar = "";
                        for (const auto& entry : fs::directory_iterator(serverPath)) {
                            if (entry.is_regular_file()) {
                                std::string fname = entry.path().filename().string();
                                if (fname.find("forge") != std::string::npos && fname.substr(fname.length() - 4) == ".jar") {
                                    legacyJar = fname;
                                    break;
                                }
                            }
                        }
                        if (!legacyJar.empty()) {
                            try {
                                fs::rename(serverPath + "/" + legacyJar, serverPath + "/server.jar");
                            } catch (...) {}
                        }
                    }
                } else if (actualLoader == "fabric") {
                    if (fs::exists(serverPath + "/fabric-server-launch.jar")) {
                        try {
                            if (fs::exists(serverPath + "/server.jar")) {
                                fs::remove(serverPath + "/server.jar");
                            }
                            fs::rename(serverPath + "/fabric-server-launch.jar", serverPath + "/server.jar");
                        } catch (...) {}
                    }
                }
            } else if (softwareType == "forge") {
                std::string downloadUrl = VersionFetcher::getInstance().getDownloadUrl("forge", version);
                std::string installerPath = serverPath + "/installer.jar";
                success = VersionFetcher::getInstance().downloadFile(downloadUrl, installerPath);
                if (!success) {
                    std::cerr << "[MCDeploy Installer] Forge installer download failed." << std::endl;
                    Database::getInstance().updateServerStatus(uuid, "Crashed");
                    Database::getInstance().logAction(username, "CREATE_SERVER_FAILED", uuid, "Failed to download Forge installer.");
                    return;
                }

                std::string installCmd;
#ifdef _WIN32
                installCmd = "powershell.exe -Command \"cd '" + serverPath + "'; java -jar installer.jar --installServer\"";
#else
                installCmd = "cd \"" + serverPath + "\" && java -jar installer.jar --installServer";
#endif
                system(installCmd.c_str());
                try {
                    fs::remove(installerPath);
                    if (fs::exists(installerPath + ".log")) {
                        fs::remove(installerPath + ".log");
                    }
                } catch (...) {}

                std::string forgeLibPath = serverPath + "/libraries/net/minecraftforge/forge";
                std::string forgeVersion = "";
                if (fs::exists(forgeLibPath)) {
                    for (const auto& entry : fs::directory_iterator(forgeLibPath)) {
                        if (entry.is_directory()) {
                            forgeVersion = entry.path().filename().string();
                            break;
                        }
                    }
                }

                if (!forgeVersion.empty()) {
                    std::string jvmArgsPath = serverPath + "/user_jvm_args.txt";
                    std::ofstream jvmArgsFile(jvmArgsPath);
                    jvmArgsFile << "# Generated by MCDeploy\n";
                    jvmArgsFile << "-Xms" << ramMin << "M\n";
                    jvmArgsFile << "-Xmx" << ramMax << "M\n";
                    jvmArgsFile.close();

                    std::string startCommand;
#ifdef _WIN32
                    startCommand = "java @user_jvm_args.txt @libraries/net/minecraftforge/forge/" + forgeVersion + "/win_args.txt nogui";
#else
                    startCommand = "java @user_jvm_args.txt @libraries/net/minecraftforge/forge/" + forgeVersion + "/unix_args.txt nogui";
#endif
                    Database::getInstance().updateServerStartCommand(uuid, startCommand);
                } else {
                    std::string legacyJar = "";
                    for (const auto& entry : fs::directory_iterator(serverPath)) {
                        if (entry.is_regular_file()) {
                            std::string fname = entry.path().filename().string();
                            if (fname.find("forge") != std::string::npos && fname.substr(fname.length() - 4) == ".jar") {
                                legacyJar = fname;
                                break;
                            }
                        }
                    }
                    if (!legacyJar.empty()) {
                        try {
                            fs::rename(serverPath + "/" + legacyJar, serverPath + "/server.jar");
                        } catch (...) {}
                    }
                }
            } else if (softwareType == "neoforge") {
                std::string downloadUrl = VersionFetcher::getInstance().getDownloadUrl("neoforge", version);
                std::string installerPath = serverPath + "/installer.jar";
                success = VersionFetcher::getInstance().downloadFile(downloadUrl, installerPath);
                if (!success) {
                    std::cerr << "[MCDeploy Installer] NeoForge installer download failed." << std::endl;
                    Database::getInstance().updateServerStatus(uuid, "Crashed");
                    Database::getInstance().logAction(username, "CREATE_SERVER_FAILED", uuid, "Failed to download NeoForge installer.");
                    return;
                }

                std::string installCmd;
#ifdef _WIN32
                installCmd = "powershell.exe -Command \"cd '" + serverPath + "'; java -jar installer.jar --installServer\"";
#else
                installCmd = "cd \"" + serverPath + "\" && java -jar installer.jar --installServer";
#endif
                system(installCmd.c_str());
                try {
                    fs::remove(installerPath);
                    if (fs::exists(installerPath + ".log")) {
                        fs::remove(installerPath + ".log");
                    }
                } catch (...) {}

                std::string neoforgeLibPath = serverPath + "/libraries/net/neoforged/neoforge";
                std::string neoforgeVersion = "";
                if (fs::exists(neoforgeLibPath)) {
                    for (const auto& entry : fs::directory_iterator(neoforgeLibPath)) {
                        if (entry.is_directory()) {
                            neoforgeVersion = entry.path().filename().string();
                            break;
                        }
                    }
                }

                if (!neoforgeVersion.empty()) {
                    std::string jvmArgsPath = serverPath + "/user_jvm_args.txt";
                    std::ofstream jvmArgsFile(jvmArgsPath);
                    jvmArgsFile << "# Generated by MCDeploy\n";
                    jvmArgsFile << "-Xms" << ramMin << "M\n";
                    jvmArgsFile << "-Xmx" << ramMax << "M\n";
                    jvmArgsFile.close();

                    std::string startCommand;
#ifdef _WIN32
                    startCommand = "java @user_jvm_args.txt @libraries/net/neoforged/neoforge/" + neoforgeVersion + "/win_args.txt nogui";
#else
                    startCommand = "java @user_jvm_args.txt @libraries/net/neoforged/neoforge/" + neoforgeVersion + "/unix_args.txt nogui";
#endif
                    Database::getInstance().updateServerStartCommand(uuid, startCommand);
                }
            } else {
                std::string downloadUrl = VersionFetcher::getInstance().getDownloadUrl(softwareType, version);
                std::string jarPath = serverPath + "/server.jar";
                success = VersionFetcher::getInstance().downloadFile(downloadUrl, jarPath);
                if (!success) {
                    std::cerr << "[MCDeploy Installer] Download failed for " << downloadUrl << std::endl;
                    Database::getInstance().updateServerStatus(uuid, "Crashed");
                    Database::getInstance().logAction(username, "CREATE_SERVER_FAILED", uuid, "Failed to download JAR file.");
                    return;
                }
            }

            // 3. Agree to EULA
            std::ofstream eulaFile(serverPath + "/eula.txt");
            eulaFile << "eula=true\n";
            eulaFile.close();

            // 4. Create default server.properties
            std::ofstream propertiesFile(serverPath + "/server.properties");
            propertiesFile << "server-port=" << port << "\n";
            propertiesFile << "query.port=" << port << "\n";
            propertiesFile << "online-mode=true\n";
            propertiesFile << "motd=Powered by MCDeploy\n";
            propertiesFile << "difficulty=easy\n";
            propertiesFile.close();

            // 5. Complete Installation
            Database::getInstance().updateServerStatus(uuid, "Offline");
            Database::getInstance().logAction(username, "CREATE_SERVER_SUCCESS", uuid, "Server installed successfully.");
            std::cout << "[MCDeploy Installer] Installation complete for server UUID: " << uuid << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[MCDeploy Installer] Error during install: " << e.what() << std::endl;
            Database::getInstance().updateServerStatus(uuid, "Crashed");
        }
    }).detach();

    nlohmann::json successJson;
    successJson["status"] = "success";
    successJson["uuid"] = uuid;
    if (!subdomain.empty()) {
        successJson["subdomain"] = subdomain;
        successJson["address"] = DnsManager::getInstance().getFullAddress(subdomain);
    }
    successJson["message"] = "Server creation started under MCDeploy wizard.";
    callback(newJsonResponse(successJson));
}

void ServerController::deleteServer(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    if (role != "admin") {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k403Forbidden);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    // Clean up TCP tunnel before deleting
    TunnelManager::getInstance().stopTunnel(uuid);

    // Release only DNS records owned by this server. Never delete by hostname.
    if (!s.dns_a_record_id.empty() || !s.dns_srv_record_id.empty()) {
        if (!DnsManager::getInstance().removeServerDnsRecords(s.dns_a_record_id, s.dns_srv_record_id)) {
            nlohmann::json errorRes;
            errorRes["status"] = "error";
            errorRes["message"] = "The server was not deleted because its global subdomain reservation could not be released.";
            auto resp = newJsonResponse(errorRes);
            resp->setStatusCode(k502BadGateway);
            callback(resp);
            return;
        }
        Database::getInstance().logAction(username, "DNS_RESERVATION_RELEASED", uuid,
            "Released the owned DNS reservation for " + s.subdomain + " during server deletion");
    }

    // Force stop if running
    ProcessManager::getInstance().stopServer(uuid, true);

    // Release the port by killing any process listening on it (in case handle was lost after daemon restart)
    killProcessOnPort(s.port);

    // Give the operating system a moment to release file handles
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Remove files with retries
    for (int retry = 0; retry < 5; ++retry) {
        try {
            if (fs::exists(s.directory_path)) {
                fs::remove_all(s.directory_path);
            }
            break;
        } catch (...) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }

    Database::getInstance().deleteServer(uuid);
    Database::getInstance().logAction(username, "DELETE_SERVER", uuid, "Deleted server and removed files");

    nlohmann::json res;
    res["status"] = "success";
    callback(newJsonResponse(res));
}

void ServerController::controlServer(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string action = json->get("action", "").asString();
    
    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    // All the tunnel + DNS + status juggling lives in ServerLifecycle so the
    // background scheduler and other automation paths behave identically.
    LifecycleResult lr;
    if (action == "start") {
        lr = ServerLifecycle::start(uuid, username);
    } else if (action == "stop" || action == "kill") {
        lr = ServerLifecycle::stop(uuid, username, action == "kill");
    } else if (action == "restart") {
        lr = ServerLifecycle::restart(uuid, username);
    } else {
        lr.ok = false;
        lr.message = "unknown action: " + action;
    }

    nlohmann::json res;
    res["status"]  = lr.ok ? "success" : "error";
    res["message"] = lr.message;
    callback(newJsonResponse(res));
}

void ServerController::getConfig(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    std::string configPath = s.directory_path + "/server.properties";
    nlohmann::json configJson = nlohmann::json::object();

    std::ifstream file(configPath);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = line.substr(0, eqPos);
                std::string val = line.substr(eqPos + 1);
                configJson[key] = val;
            }
        }
        file.close();
    }

    callback(newJsonResponse(configJson));
}

void ServerController::updateConfig(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    if (role != "admin" && role != "moderator") {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k403Forbidden);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string configPath = s.directory_path + "/server.properties";
    std::ofstream file(configPath);
    if (!file.is_open()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }

    file << "# Generated by MCDeploy\n";
    
    // Convert rapidjson to std::string key-value pairs
    auto rawJson = req->getBody();
    try {
        auto j = nlohmann::json::parse(rawJson);
        for (auto& [key, value] : j.items()) {
            std::string valStr;
            if (value.is_string()) valStr = value.get<std::string>();
            else if (value.is_boolean()) valStr = value.get<bool>() ? "true" : "false";
            else if (value.is_number()) valStr = std::to_string(value.get<double>());
            file << key << "=" << valStr << "\n";
        }
    } catch (...) {}

    file.close();
    Database::getInstance().logAction(username, "UPDATE_CONFIG", uuid, "Updated server.properties");

    nlohmann::json res;
    res["status"] = "success";
    callback(newJsonResponse(res));
}

void ServerController::getBackups(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    auto backups = Database::getInstance().getServerBackups(uuid);
    nlohmann::json list = nlohmann::json::array();
    
    for (const auto& b : backups) {
        nlohmann::json item;
        item["backup_uuid"] = b.backup_uuid;
        item["file_name"] = b.file_name;
        item["file_size"] = b.file_size;
        item["created_at"] = b.created_at;
        list.push_back(item);
    }
    callback(newJsonResponse(list));
}

void ServerController::createBackup(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    std::string backupsDir = getBackupsDir() + "/" + uuid;
    fs::create_directories(backupsDir);

    std::string backupUuid = generateUuid();
    std::string fileName = "backup-" + backupUuid + ".zip";
    std::string filePath = backupsDir + "/" + fileName;

    // Simulate writing a dummy backup file representing zip archive
    std::ofstream mockZip(filePath);
    mockZip << "Mock zip contents for backup from MCDeploy\n";
    mockZip.close();

    BackupRecord b;
    b.backup_uuid = backupUuid;
    b.server_uuid = uuid;
    b.file_name = fileName;
    b.file_path = filePath;
    b.file_size = 512; // Bytes
    
    auto now = std::chrono::system_clock::now();
    b.created_at = formatDateTime(std::chrono::system_clock::to_time_t(now));

    Database::getInstance().addBackup(b);
    Database::getInstance().logAction(username, "CREATE_BACKUP", uuid, "Created backup zip archive");

    nlohmann::json res;
    res["status"] = "success";
    res["backup_uuid"] = backupUuid;
    callback(newJsonResponse(res));
}

void ServerController::getVersions(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string software = req->getParameter("software");
    if (software.empty()) software = "paper";

    auto list = VersionFetcher::getInstance().fetchVersions(software);
    nlohmann::json res = nlohmann::json::array();
    for (const auto& v : list) {
        res.push_back(v);
    }
    callback(newJsonResponse(res));
}

void ServerController::listFiles(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    nlohmann::json list = nlohmann::json::array();
    try {
        if (fs::exists(s.directory_path) && fs::is_directory(s.directory_path)) {
            for (const auto& entry : fs::directory_iterator(s.directory_path)) {
                nlohmann::json item;
                item["name"] = entry.path().filename().string();
                item["is_directory"] = entry.is_directory();
                item["size"] = entry.is_regular_file() ? entry.file_size() : 0;
                list.push_back(item);
            }
        }
    } catch (...) {}

    callback(newJsonResponse(list));
}

void ServerController::viewFile(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    std::string fileName = req->getParameter("file");
    if (fileName.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    // Path traversal protection
    std::string fullPath = s.directory_path + "/" + fileName;
    if (fullPath.find("..") != std::string::npos) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k403Forbidden);
        callback(resp);
        return;
    }

    // Fast-path check for common binary files to prevent reading massive files
    std::string ext = std::filesystem::path(fileName).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".jar" || ext == ".zip" || ext == ".tar" || ext == ".gz" || ext == ".png" || 
        ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".exe" || ext == ".dll" || ext == ".so") {
        nlohmann::json res;
        res["content"] = "[Binary File - Content Hidden]";
        res["is_binary"] = true;
        callback(newJsonResponse(res));
        return;
    }

    try {
        std::ifstream file(fullPath, std::ios::binary);
        std::string content;
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            content = ss.str();
            file.close();
        }

        nlohmann::json res;
        try {
            res["content"] = content;
        } catch (...) {
            res["content"] = "[Binary File - Content Hidden]";
            res["is_binary"] = true;
        }
        callback(newJsonResponse(res));
    } catch (const std::exception& e) {
        nlohmann::json res;
        res["content"] = "[Error reading file]";
        res["error"] = e.what();
        callback(newJsonResponse(res));
    } catch (...) {
        nlohmann::json res;
        res["content"] = "[Unknown error reading file]";
        callback(newJsonResponse(res));
    }
}


void ServerController::saveFile(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    if (role != "admin" && role != "moderator") {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k403Forbidden);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string fileName = json->get("file", "").asString();
    std::string content = json->get("content", "").asString();

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    // Path traversal protection
    std::string fullPath = s.directory_path + "/" + fileName;
    if (fullPath.find("..") != std::string::npos) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k403Forbidden);
        callback(resp);
        return;
    }

    std::ofstream file(fullPath);
    if (!file.is_open()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }

    file << content;
    file.close();

    Database::getInstance().logAction(username, "SAVE_FILE", uuid, "Edited file: " + fileName);

    nlohmann::json res;
    res["status"] = "success";
    callback(newJsonResponse(res));
}

void ServerController::checkSubdomain(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    std::string name = req->getParameter("name");
    if (name.empty()) {
        nlohmann::json res;
        res["available"] = false;
        res["message"] = "No subdomain name provided.";
        callback(newJsonResponse(res));
        return;
    }

    // Lowercase it
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    // Validate format
    std::regex subdomainRegex("^[a-z0-9]([a-z0-9-]{1,61}[a-z0-9])?$");
    if (!std::regex_match(name, subdomainRegex)) {
        nlohmann::json res;
        res["available"] = false;
        res["message"] = "Invalid format. Use 3-63 lowercase letters, numbers, and hyphens.";
        callback(newJsonResponse(res));
        return;
    }

    // Reserved subdomains
    static const std::vector<std::string> reserved = {
        "www", "mail", "ftp", "admin", "api", "app", "panel", 
        "dashboard", "status", "blog", "docs", "help", "support",
        "ns1", "ns2", "mx", "smtp", "imap", "pop", "mcdeploy", "offline"
    };
    for (const auto& r : reserved) {
        if (name == r) {
            nlohmann::json res;
            res["available"] = false;
            res["message"] = "'" + name + "' is a reserved subdomain name.";
            callback(newJsonResponse(res));
            return;
        }
    }

    if (Database::getInstance().isSubdomainTaken(name)) {
        nlohmann::json res;
        res["available"] = false;
        res["message"] = "'" + name + ".mcdeploy.online' is already reserved on this installation.";
        callback(newJsonResponse(res));
        return;
    }

    const auto availability = DnsManager::getInstance().checkSubdomainAvailability(name);
    if (availability.status == DnsAvailabilityStatus::Error) {
        nlohmann::json res;
        res["available"] = false;
        res["message"] = "Global availability could not be confirmed. Try again shortly.";
        auto resp = newJsonResponse(res);
        resp->setStatusCode(k503ServiceUnavailable);
        callback(resp);
        return;
    }
    if (availability.status == DnsAvailabilityStatus::Taken) {
        nlohmann::json res;
        res["available"] = false;
        res["message"] = "'" + name + ".mcdeploy.online' is already reserved by another server.";
        callback(newJsonResponse(res));
        return;
    }

    nlohmann::json res;
    res["available"] = true;
    res["message"] = "'" + name + ".mcdeploy.online' is globally available!";
    res["address"] = name + ".mcdeploy.online";
    callback(newJsonResponse(res));
}

static bool extractZip(const std::string& zipPath, const std::string& destPath) {
#ifdef _WIN32
    // Windows PowerShell extraction
    std::string cmd = "powershell.exe -Command \"Expand-Archive -Path '" + zipPath + "' -DestinationPath '" + destPath + "' -Force\"";
    int status = system(cmd.c_str());
    return status == 0;
#else
    // Linux unzip extraction
    std::string cmd = "unzip -o \"" + zipPath + "\" -d \"" + destPath + "\" > /dev/null 2>&1";
    int status = system(cmd.c_str());
    return status == 0;
#endif
}

void ServerController::searchModpacks(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string source = req->getParameter("source");
    std::string query = req->getParameter("query");
    std::string apiKey = req->getParameter("apiKey");
    std::string offset = req->getParameter("offset");
    std::string limit = req->getParameter("limit");

    if (source.empty()) source = "modrinth";
    if (offset.empty()) offset = "0";
    if (limit.empty()) limit = "20";

    nlohmann::json responseArray = nlohmann::json::array();

    if (source == "modrinth") {
        std::string url = "https://api.modrinth.com/v2/search?query=" + drogon::utils::urlEncode(query) + "&facets=%5B%5B%22project_type%3Amodpack%22%5D%5D&offset=" + offset + "&limit=" + limit;
        std::string res = VersionFetcher::getInstance().httpGet(url);
        if (!res.empty()) {
            try {
                auto json = nlohmann::json::parse(res);
                if (json.contains("hits")) {
                    for (const auto& hit : json["hits"]) {
                        nlohmann::json item;
                        item["id"] = hit.value("project_id", "");
                        item["name"] = hit.value("title", "");
                        item["summary"] = hit.value("description", "");
                        item["logoUrl"] = hit.value("icon_url", "");
                        item["downloads"] = hit.value("downloads", 0);
                        item["source"] = "modrinth";
                        responseArray.push_back(item);
                    }
                }
            } catch (...) {}
        }
    } else if (source == "curseforge") {
        if (apiKey.empty()) {
            apiKey = "$2a$10$bLva59BlqnwDpxbDBAEG7.tD.R3kv71qa9CZLa9TWDWNsccecFoFi"; // Default key
        }
        std::string url = "https://api.curseforge.com/v1/mods/search?gameId=432&classId=4471&searchFilter=" + drogon::utils::urlEncode(query) + "&index=" + offset + "&pageSize=" + limit;
        std::vector<std::string> headers = {
            "x-api-key: " + apiKey,
            "Accept: application/json"
        };
        std::string res = VersionFetcher::getInstance().httpGetWithHeaders(url, headers);
        if (!res.empty()) {
            try {
                auto json = nlohmann::json::parse(res);
                if (json.contains("data")) {
                    for (const auto& mod : json["data"]) {
                        nlohmann::json item;
                        item["id"] = std::to_string(mod.value("id", 0));
                        item["name"] = mod.value("name", "");
                        item["summary"] = mod.value("summary", "");
                        if (mod.contains("logo") && mod["logo"].contains("thumbnailUrl")) {
                            item["logoUrl"] = mod["logo"]["thumbnailUrl"].get<std::string>();
                        } else {
                            item["logoUrl"] = "";
                        }
                        item["downloads"] = mod.value("downloadCount", 0);
                        item["source"] = "curseforge";
                        responseArray.push_back(item);
                    }
                }
            } catch (...) {}
        }
    }

    callback(newJsonResponse(responseArray));
}

void ServerController::getModpackVersions(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string modId) {
    std::string source = req->getParameter("source");
    std::string apiKey = req->getParameter("apiKey");

    if (source.empty()) source = "modrinth";

    nlohmann::json responseArray = nlohmann::json::array();

    if (source == "modrinth") {
        std::string url = "https://api.modrinth.com/v2/project/" + modId + "/version";
        std::string res = VersionFetcher::getInstance().httpGet(url);
        if (!res.empty()) {
            try {
                auto json = nlohmann::json::parse(res);
                if (json.is_array()) {
                    for (const auto& v : json) {
                        bool hasServerPack = false;
                        std::string serverPackUrl = "";
                        
                        if (v.contains("files")) {
                            for (const auto& file : v["files"]) {
                                std::string fname = file.value("filename", "");
                                std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
                                if (fname.find(".mrpack") == std::string::npos && 
                                    (fname.find("server") != std::string::npos || fname.find(".zip") != std::string::npos)) {
                                    hasServerPack = true;
                                    serverPackUrl = file.value("url", "");
                                    break;
                                }
                            }
                            if (serverPackUrl.empty()) {
                                // Fallback: try to find .mrpack or any zip file
                                for (const auto& file : v["files"]) {
                                    std::string fname = file.value("filename", "");
                                    std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
                                    if (fname.find(".mrpack") != std::string::npos || fname.find(".zip") != std::string::npos) {
                                        serverPackUrl = file.value("url", "");
                                        break;
                                    }
                                }
                                if (serverPackUrl.empty() && !v["files"].empty()) {
                                    serverPackUrl = v["files"][0].value("url", "");
                                }
                            }
                        }

                        nlohmann::json item;
                        item["id"] = v.value("id", "");
                        item["name"] = v.value("name", "");
                        item["gameVersions"] = v.value("game_versions", nlohmann::json::array());
                        item["loaders"] = v.value("loaders", nlohmann::json::array());
                        item["hasServerPack"] = hasServerPack;
                        item["serverPackUrl"] = serverPackUrl;
                        responseArray.push_back(item);
                    }
                }
            } catch (...) {}
        }
    } else if (source == "curseforge") {
        if (apiKey.empty()) {
            apiKey = "$2a$10$bLva59BlqnwDpxbDBAEG7.tD.R3kv71qa9CZLa9TWDWNsccecFoFi"; // Default key
        }
        std::string url = "https://api.curseforge.com/v1/mods/" + modId + "/files";
        std::vector<std::string> headers = {
            "x-api-key: " + apiKey,
            "Accept: application/json"
        };
        std::string res = VersionFetcher::getInstance().httpGetWithHeaders(url, headers);
        if (!res.empty()) {
            try {
                auto json = nlohmann::json::parse(res);
                if (json.contains("data") && json["data"].is_array()) {
                    for (const auto& file : json["data"]) {
                        int serverPackFileId = file.value("serverPackFileId", 0);
                        bool hasServerPack = (serverPackFileId > 0);
                        int finalFileId = hasServerPack ? serverPackFileId : file.value("id", 0);

                        nlohmann::json gameVersions = nlohmann::json::array();
                        nlohmann::json loaders = nlohmann::json::array();
                        if (file.contains("gameVersions")) {
                            for (const auto& gv : file["gameVersions"]) {
                                std::string gvStr = gv.get<std::string>();
                                std::string gvLower = gvStr;
                                std::transform(gvLower.begin(), gvLower.end(), gvLower.begin(), ::tolower);
                                if (gvLower == "forge" || gvLower == "fabric" || gvLower == "quilt" || gvLower == "neoforge") {
                                    loaders.push_back(gvLower);
                                } else {
                                    gameVersions.push_back(gvStr);
                                }
                            }
                        }

                        nlohmann::json item;
                        item["id"] = std::to_string(file.value("id", 0));
                        item["name"] = file.value("displayName", "");
                        item["gameVersions"] = gameVersions;
                        item["loaders"] = loaders;
                        item["hasServerPack"] = hasServerPack;
                        item["serverPackFileId"] = std::to_string(finalFileId);
                        responseArray.push_back(item);
                    }
                }
            } catch (...) {}
        }
    }

    callback(newJsonResponse(responseArray));
}

void ServerController::searchAddons(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    std::string source = req->getParameter("source");
    std::string query = req->getParameter("query");
    std::string apiKey = req->getParameter("apiKey");
    std::string offset = req->getParameter("offset");
    std::string limit = req->getParameter("limit");

    if (source.empty()) source = "modrinth";
    if (offset.empty()) offset = "0";
    if (limit.empty()) limit = "20";

    // Detect software type compatibility: plugin vs mod
    std::string software = s.software_type;
    std::transform(software.begin(), software.end(), software.begin(), ::tolower);

    bool isPluginCompatible = (software == "paper" || software == "purpur" || software == "spigot");
    bool isModCompatible = (software == "fabric" || software == "forge" || software == "neoforge" || software == "modpack");

    if (!isPluginCompatible && !isModCompatible) {
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = "This server software type does not support plugins or mods.";
        auto resp = newJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    nlohmann::json responseArray = nlohmann::json::array();

    if (source == "modrinth") {
        std::string facetValue = isPluginCompatible ? "project_type:plugin" : "project_type:mod";
        std::string url = "https://api.modrinth.com/v2/search?query=" + drogon::utils::urlEncode(query) + 
                          "&facets=%5B%5B%22" + drogon::utils::urlEncode(facetValue) + "%22%5D%5D&offset=" + offset + "&limit=" + limit;
        std::string res = VersionFetcher::getInstance().httpGet(url);
        if (!res.empty()) {
            try {
                auto json = nlohmann::json::parse(res);
                if (json.contains("hits")) {
                    for (const auto& hit : json["hits"]) {
                        nlohmann::json item;
                        item["id"] = hit.value("project_id", "");
                        item["name"] = hit.value("title", "");
                        item["summary"] = hit.value("description", "");
                        item["logoUrl"] = hit.value("icon_url", "");
                        item["downloads"] = hit.value("downloads", 0);
                        item["source"] = "modrinth";
                        responseArray.push_back(item);
                    }
                }
            } catch (...) {}
        }
    } else if (source == "curseforge") {
        if (apiKey.empty()) {
            apiKey = "$2a$10$bLva59BlqnwDpxbDBAEG7.tD.R3kv71qa9CZLa9TWDWNsccecFoFi"; // Default key
        }
        std::string classId = isPluginCompatible ? "5" : "6"; // 5: bukkit plugins, 6: mods
        std::string url = "https://api.curseforge.com/v1/mods/search?gameId=432&classId=" + classId + 
                          "&searchFilter=" + drogon::utils::urlEncode(query) + "&index=" + offset + "&pageSize=" + limit;
        std::vector<std::string> headers = {
            "x-api-key: " + apiKey,
            "Accept: application/json"
        };
        std::string res = VersionFetcher::getInstance().httpGetWithHeaders(url, headers);
        if (!res.empty()) {
            try {
                auto json = nlohmann::json::parse(res);
                if (json.contains("data")) {
                    for (const auto& mod : json["data"]) {
                        nlohmann::json item;
                        item["id"] = std::to_string(mod.value("id", 0));
                        item["name"] = mod.value("name", "");
                        item["summary"] = mod.value("summary", "");
                        if (mod.contains("logo") && mod["logo"].contains("thumbnailUrl")) {
                            item["logoUrl"] = mod["logo"]["thumbnailUrl"].get<std::string>();
                        } else {
                            item["logoUrl"] = "";
                        }
                        item["downloads"] = mod.value("downloadCount", 0);
                        item["source"] = "curseforge";
                        responseArray.push_back(item);
                    }
                }
            } catch (...) {}
        }
    }

    callback(newJsonResponse(responseArray));
}

void ServerController::getAddonVersions(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string addonId) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    std::string source = req->getParameter("source");
    std::string apiKey = req->getParameter("apiKey");

    if (source.empty()) source = "modrinth";

    nlohmann::json responseArray = nlohmann::json::array();

    if (source == "modrinth") {
        std::string url = "https://api.modrinth.com/v2/project/" + addonId + "/version";
        std::string res = VersionFetcher::getInstance().httpGet(url);
        if (!res.empty()) {
            try {
                auto json = nlohmann::json::parse(res);
                if (json.is_array()) {
                    for (const auto& v : json) {
                        std::string downloadUrl = "";
                        std::string filename = "";
                        
                        if (v.contains("files") && v["files"].is_array() && !v["files"].empty()) {
                            // Try primary first
                            for (const auto& file : v["files"]) {
                                if (file.value("primary", false)) {
                                    downloadUrl = file.value("url", "");
                                    filename = file.value("filename", "");
                                    break;
                                }
                            }
                            // Fallback to first file
                            if (downloadUrl.empty()) {
                                downloadUrl = v["files"][0].value("url", "");
                                filename = v["files"][0].value("filename", "");
                            }
                        }

                        nlohmann::json item;
                        item["id"] = v.value("id", "");
                        item["name"] = v.value("name", "");
                        item["gameVersions"] = v.value("game_versions", nlohmann::json::array());
                        item["loaders"] = v.value("loaders", nlohmann::json::array());
                        item["downloadUrl"] = downloadUrl;
                        item["filename"] = filename;
                        responseArray.push_back(item);
                    }
                }
            } catch (...) {}
        }
    } else if (source == "curseforge") {
        if (apiKey.empty()) {
            apiKey = "$2a$10$bLva59BlqnwDpxbDBAEG7.tD.R3kv71qa9CZLa9TWDWNsccecFoFi"; // Default key
        }
        std::string url = "https://api.curseforge.com/v1/mods/" + addonId + "/files";
        std::vector<std::string> headers = {
            "x-api-key: " + apiKey,
            "Accept: application/json"
        };
        std::string res = VersionFetcher::getInstance().httpGetWithHeaders(url, headers);
        if (!res.empty()) {
            try {
                auto json = nlohmann::json::parse(res);
                if (json.contains("data") && json["data"].is_array()) {
                    for (const auto& file : json["data"]) {
                        nlohmann::json gameVersions = nlohmann::json::array();
                        nlohmann::json loaders = nlohmann::json::array();
                        if (file.contains("gameVersions")) {
                            for (const auto& gv : file["gameVersions"]) {
                                std::string gvStr = gv.get<std::string>();
                                std::string gvLower = gvStr;
                                std::transform(gvLower.begin(), gvLower.end(), gvLower.begin(), ::tolower);
                                if (gvLower == "forge" || gvLower == "fabric" || gvLower == "quilt" || gvLower == "neoforge") {
                                    loaders.push_back(gvLower);
                                } else {
                                    gameVersions.push_back(gvStr);
                                }
                            }
                        }

                        nlohmann::json item;
                        item["id"] = std::to_string(file.value("id", 0));
                        item["name"] = file.value("displayName", "");
                        item["filename"] = file.value("fileName", "");
                        item["downloadUrl"] = file.value("downloadUrl", "");
                        item["gameVersions"] = gameVersions;
                        item["loaders"] = loaders;
                        responseArray.push_back(item);
                    }
                }
            } catch (...) {}
        }
    }

    callback(newJsonResponse(responseArray));
}

void ServerController::installAddon(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    auto rawBody = req->getBody();
    std::string downloadUrl = "";
    std::string filename = "";
    std::string addonName = "";
    
    try {
        auto json = nlohmann::json::parse(rawBody);
        downloadUrl = json.value("downloadUrl", "");
        filename = json.value("filename", "");
        addonName = json.value("addonName", "");
    } catch (...) {
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = "Invalid JSON body";
        auto resp = newJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    if (downloadUrl.empty() || filename.empty()) {
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = "Missing downloadUrl or filename";
        auto resp = newJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    // Determine target directory: plugins or mods
    std::string software = s.software_type;
    std::transform(software.begin(), software.end(), software.begin(), ::tolower);
    bool isPlugin = (software == "paper" || software == "purpur" || software == "spigot");

    std::string subDir = isPlugin ? "plugins" : "mods";
    std::string serverPath = s.directory_path;
    std::string targetDir = serverPath + "/" + subDir;

    // Security: sanitize filename to avoid directory traversal
    std::string safeFilename = fs::path(filename).filename().string();
    if (safeFilename.empty()) {
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = "Invalid filename";
        auto resp = newJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string targetPath = targetDir + "/" + safeFilename;

    try {
        fs::create_directories(targetDir);
    } catch (const std::exception& e) {
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = std::string("Failed to create directory: ") + e.what();
        auto resp = newJsonResponse(err);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }

    // Download the file
    bool success = VersionFetcher::getInstance().downloadFile(downloadUrl, targetPath);
    if (!success) {
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = "Download failed. Please check the URL or try again later.";
        auto resp = newJsonResponse(err);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }

    // Log to Audit Database
    Database::getInstance().logAction(username, "INSTALL_ADDON", uuid, 
        "Installed " + (addonName.empty() ? safeFilename : addonName) + " (" + safeFilename + ") to " + subDir + "/");

    nlohmann::json res;
    res["status"] = "success";
    res["message"] = "Successfully installed " + safeFilename;
    callback(newJsonResponse(res));
}

void ServerController::getInstalledAddons(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    std::string software = s.software_type;
    std::transform(software.begin(), software.end(), software.begin(), ::tolower);
    bool isPlugin = (software == "paper" || software == "purpur" || software == "spigot");

    std::string subDir = isPlugin ? "plugins" : "mods";
    std::string targetDir = s.directory_path + "/" + subDir;

    nlohmann::json filesArray = nlohmann::json::array();

    if (fs::exists(targetDir) && fs::is_directory(targetDir)) {
        for (const auto& entry : fs::directory_iterator(targetDir)) {
            if (entry.is_regular_file()) {
                std::string fname = entry.path().filename().string();
                nlohmann::json item;
                item["filename"] = fname;
                item["size"] = entry.file_size();
                filesArray.push_back(item);
            }
        }
    }

    callback(newJsonResponse(filesArray));
}

void ServerController::uninstallAddon(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    std::string filename = req->getParameter("filename");
    if (filename.empty()) {
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = "Missing filename parameter";
        auto resp = newJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string software = s.software_type;
    std::transform(software.begin(), software.end(), software.begin(), ::tolower);
    bool isPlugin = (software == "paper" || software == "purpur" || software == "spigot");

    std::string subDir = isPlugin ? "plugins" : "mods";
    
    // Security: sanitize filename to avoid directory traversal
    std::string safeFilename = fs::path(filename).filename().string();
    if (safeFilename.empty() || safeFilename == "." || safeFilename == "..") {
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = "Invalid filename";
        auto resp = newJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string filePath = s.directory_path + "/" + subDir + "/" + safeFilename;

    if (!fs::exists(filePath)) {
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = "File not found";
        auto resp = newJsonResponse(err);
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    try {
        fs::remove(filePath);
    } catch (const std::exception& e) {
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = std::string("Failed to delete file: ") + e.what();
        auto resp = newJsonResponse(err);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }

    // Log to Audit Database
    Database::getInstance().logAction(username, "UNINSTALL_ADDON", uuid, 
        "Deleted " + safeFilename + " from " + subDir + "/");

    nlohmann::json res;
    res["status"] = "success";
    res["message"] = "Successfully uninstalled " + safeFilename;
    callback(newJsonResponse(res));
}

void ServerController::getPerformance(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    nlohmann::json res;
    res["cpu_priority"] = ProcessManager::getInstance().getServerCpuPriority(uuid);
    res["smart_optimization"] = ProcessManager::getInstance().getServerSmartOptimization(uuid);
    res["is_running"] = ProcessManager::getInstance().isServerRunning(uuid);
    
    callback(newJsonResponse(res));
}

void ServerController::updatePerformance(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    if (role != "admin" && role != "moderator") {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k403Forbidden);
        callback(resp);
        return;
    }

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    // Convert rapidjson parameters manually
    if (json->isMember("smart_optimization")) {
        bool enable = json->get("smart_optimization", false).asBool();
        ProcessManager::getInstance().setServerSmartOptimization(uuid, enable);
    }

    if (json->isMember("cpu_priority")) {
        std::string priority = json->get("cpu_priority", "normal").asString();
        ProcessManager::getInstance().setServerCpuPriority(uuid, priority);
    }

    Database::getInstance().logAction(username, "UPDATE_PERFORMANCE", uuid, "Updated CPU priority / smart optimization settings");

    nlohmann::json res;
    res["status"] = "success";
    callback(newJsonResponse(res));
}

// ============================================================
// Operations features: import, health, automation, maintenance
// ============================================================
static std::string trimCopy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

static std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static std::string safeConsoleText(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
    if (value.size() > 240) value.resize(240);
    return value;
}

static std::string readProperty(const fs::path& path, const std::string& key, const std::string& fallback = "") {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (trimCopy(line.substr(0, eq)) == key) return trimCopy(line.substr(eq + 1));
    }
    return fallback;
}

static bool writeProperty(const fs::path& path, const std::string& key, const std::string& value) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    bool replaced = false;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq != std::string::npos && trimCopy(line.substr(0, eq)) == key) {
            lines.push_back(key + "=" + value);
            replaced = true;
        } else {
            lines.push_back(line);
        }
    }
    if (!replaced) lines.push_back(key + "=" + value);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (const auto& current : lines) out << current << '\n';
    return out.good();
}

static int parseMemoryMb(const std::string& command, const std::string& flag, int fallback) {
    std::smatch match;
    std::regex pattern("-" + flag + "([0-9]+)([mMgG])", std::regex::icase);
    if (!std::regex_search(command, match, pattern)) return fallback;
    int amount = std::stoi(match[1].str());
    return lowerCopy(match[2].str()) == "g" ? amount * 1024 : amount;
}

static nlohmann::json automationToJson(const AutomationRuleRecord& r) {
    return {
        {"id", r.id}, {"server_uuid", r.server_uuid}, {"name", r.name},
        {"trigger_type", r.trigger_type}, {"threshold", r.threshold},
        {"condition_value", r.condition_value}, {"action_type", r.action_type},
        {"action_payload", r.action_payload}, {"enabled", r.enabled != 0},
        {"cooldown_seconds", r.cooldown_seconds},
        {"last_evaluated_at", r.last_evaluated_at}, {"last_triggered_at", r.last_triggered_at},
        {"last_status", r.last_status}, {"last_output", r.last_output},
        {"created_by", r.created_by}, {"created_at", r.created_at}
    };
}

static nlohmann::json maintenanceToJson(const MaintenanceRecord& r) {
    return {
        {"server_uuid", r.server_uuid}, {"enabled", r.enabled != 0},
        {"message", r.message}, {"prevent_joins", r.prevent_joins != 0},
        {"backup_on_enable", r.backup_on_enable != 0},
        {"enabled_by", r.enabled_by}, {"enabled_at", r.enabled_at},
        {"updated_at", r.updated_at}
    };
}

static bool parseId(const std::string& raw, long long& id) {
    try { id = std::stoll(raw); return id > 0; } catch (...) { return false; }
}

static std::string validateAutomation(const AutomationRuleRecord& r) {
    static const std::vector<std::string> triggers =
        {"cpu_above", "ram_above", "disk_below", "server_offline", "log_contains"};
    static const std::vector<std::string> actions =
        {"console", "start", "stop", "restart", "backup", "ai_prompt"};
    if (r.name.empty()) return "name is required";
    if (std::find(triggers.begin(), triggers.end(), r.trigger_type) == triggers.end()) return "unknown trigger_type";
    if (std::find(actions.begin(), actions.end(), r.action_type) == actions.end()) return "unknown action_type";
    if ((r.trigger_type == "cpu_above" || r.trigger_type == "ram_above") &&
        (r.threshold < 1.0 || r.threshold > 100.0)) return "percentage threshold must be between 1 and 100";
    if (r.trigger_type == "disk_below" && r.threshold < 0.1) return "disk threshold must be at least 0.1 GB";
    if (r.trigger_type == "log_contains" && r.condition_value.empty()) return "log_contains requires condition_value";
    if ((r.action_type == "console" || r.action_type == "ai_prompt") && r.action_payload.empty()) return "this action requires a payload";
    if (r.cooldown_seconds < 30 || r.cooldown_seconds > 604800) return "cooldown_seconds must be between 30 and 604800";
    return "";
}

static void applyAutomationBody(const nlohmann::json& body, AutomationRuleRecord& r) {
    if (body.contains("name")) r.name = body.value("name", r.name);
    if (body.contains("trigger_type")) r.trigger_type = body.value("trigger_type", r.trigger_type);
    if (body.contains("threshold")) r.threshold = body.value("threshold", r.threshold);
    if (body.contains("condition_value")) r.condition_value = body.value("condition_value", r.condition_value);
    if (body.contains("action_type")) r.action_type = body.value("action_type", r.action_type);
    if (body.contains("action_payload")) r.action_payload = body.value("action_payload", r.action_payload);
    if (body.contains("enabled")) r.enabled = body.value("enabled", true) ? 1 : 0;
    if (body.contains("cooldown_seconds")) r.cooldown_seconds = body.value("cooldown_seconds", r.cooldown_seconds);
}

void ServerController::importServer(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp);
    }
    if (role != "admin") {
        auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k403Forbidden); return callback(resp);
    }

    nlohmann::json body;
    try { body = nlohmann::json::parse(req->getBody()); }
    catch (...) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "body must be JSON"}});
        resp->setStatusCode(k400BadRequest); return callback(resp);
    }

    const std::string rawPath = body.value("directory", "");
    if (rawPath.empty()) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "directory is required"}});
        resp->setStatusCode(k400BadRequest); return callback(resp);
    }

    fs::path directory;
    try { directory = fs::weakly_canonical(fs::path(rawPath)); }
    catch (...) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "directory path is invalid"}});
        resp->setStatusCode(k400BadRequest); return callback(resp);
    }
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "directory does not exist or is not a folder"}});
        resp->setStatusCode(k400BadRequest); return callback(resp);
    }

    const std::string normalized = lowerCopy(directory.lexically_normal().string());
    for (const auto& existing : Database::getInstance().getAllServers()) {
        try {
            if (lowerCopy(fs::weakly_canonical(existing.directory_path).lexically_normal().string()) == normalized) {
                auto resp = newJsonResponse({{"status", "error"}, {"message", "this directory is already registered"}});
                resp->setStatusCode(k409Conflict); return callback(resp);
            }
        } catch (...) {}
    }

    std::string startCommand;
    std::string jarName;
    std::vector<std::string> warnings;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const std::string extension = lowerCopy(entry.path().extension().string());
        if ((extension == ".bat" || extension == ".cmd" || extension == ".sh") && startCommand.empty()) {
            std::ifstream script(entry.path());
            std::string scriptLine;
            while (std::getline(script, scriptLine)) {
                const std::string lowered = lowerCopy(scriptLine);
                if (lowered.find("java") != std::string::npos && lowered.find("-jar") != std::string::npos) {
                    startCommand = trimCopy(scriptLine);
                    if (startCommand.rfind("call ", 0) == 0) startCommand = trimCopy(startCommand.substr(5));
                    break;
                }
            }
        }
        if (extension == ".jar") {
            const std::string lowerName = lowerCopy(entry.path().filename().string());
            if (lowerName.find("installer") == std::string::npos &&
                (jarName.empty() || lowerName.find("server") != std::string::npos ||
                 lowerName.find("paper") != std::string::npos || lowerName.find("purpur") != std::string::npos)) {
                jarName = entry.path().filename().string();
            }
        }
    }
    if (startCommand.empty() && !jarName.empty()) {
        startCommand = "java -Xms1024M -Xmx2048M -XX:+UseG1GC -jar \"" + jarName + "\" nogui";
        warnings.push_back("No launch script was detected; MCDeploy generated a Java command.");
    }
    if (startCommand.empty()) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "no runnable server JAR or Java launch script was found"}});
        resp->setStatusCode(k400BadRequest); return callback(resp);
    }

    const fs::path properties = directory / "server.properties";
    int port = 25565;
    try { port = std::stoi(readProperty(properties, "server-port", "25565")); } catch (...) {}
    for (const auto& existing : Database::getInstance().getAllServers()) {
        if (existing.port == port) {
            auto resp = newJsonResponse({{"status", "error"}, {"message", "detected port " + std::to_string(port) + " is already registered"}});
            resp->setStatusCode(k409Conflict); return callback(resp);
        }
    }

    std::string detectionText = lowerCopy(jarName + " " + startCommand);
    std::string software = "vanilla";
    if (detectionText.find("purpur") != std::string::npos) software = "purpur";
    else if (detectionText.find("paper") != std::string::npos) software = "paper";
    else if (detectionText.find("neoforge") != std::string::npos) software = "neoforge";
    else if (detectionText.find("forge") != std::string::npos) software = "forge";
    else if (detectionText.find("fabric") != std::string::npos) software = "fabric";

    std::string version = "unknown";
    std::smatch versionMatch;
    std::regex versionPattern("([0-9]+\\.[0-9]+(?:\\.[0-9]+)?)");
    if (std::regex_search(detectionText, versionMatch, versionPattern)) version = versionMatch[1].str();
    if (version == "unknown") warnings.push_back("Minecraft version could not be inferred from filenames.");
    if (!fs::exists(properties)) warnings.push_back("server.properties was not found; defaults were used.");

    ServerRecord record;
    record.uuid = generateUuid();
    record.name = body.value("name", directory.filename().string());
    if (record.name.empty()) record.name = "Imported Server";
    record.software_type = software;
    record.version = version;
    record.port = port;
    record.ram_min = parseMemoryMb(startCommand, "Xms", 1024);
    record.ram_max = parseMemoryMb(startCommand, "Xmx", 2048);
    record.status = "Offline";
    record.directory_path = directory.string();
    record.start_command = startCommand;
    record.created_at = formatDateTime(std::time(nullptr));
    record.subdomain = "";
    record.dns_a_record_id = "";
    record.dns_srv_record_id = "";

    if (!Database::getInstance().createServer(record)) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "failed to register imported server"}});
        resp->setStatusCode(k500InternalServerError); return callback(resp);
    }
    Database::getInstance().logAction(username, "SERVER_IMPORT", record.uuid, record.directory_path);
    callback(newJsonResponse({
        {"status", "success"}, {"uuid", record.uuid}, {"name", record.name},
        {"software_type", record.software_type}, {"version", record.version},
        {"port", record.port}, {"ram_min", record.ram_min}, {"ram_max", record.ram_max},
        {"start_command", record.start_command}, {"warnings", warnings}
    }));
}

void ServerController::getHealth(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp);
    }
    ServerRecord server;
    if (!Database::getInstance().getServer(uuid, server)) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "server not found"}});
        resp->setStatusCode(k404NotFound); return callback(resp);
    }

    auto metrics = SystemInfo::getInstance().getMetrics(server.directory_path);
    const bool running = ProcessManager::getInstance().isServerRunning(uuid);
    nlohmann::json components = nlohmann::json::array();
    nlohmann::json recommendations = nlohmann::json::array();
    int total = 0;
    auto addComponent = [&](const std::string& key, int score, int maximum,
                            const std::string& evidence) {
        score = std::max(0, std::min(score, maximum));
        total += score;
        components.push_back({{"key", key}, {"score", score}, {"maximum", maximum},
                              {"status", score >= maximum * 0.8 ? "good" : score >= maximum * 0.5 ? "warning" : "critical"},
                              {"evidence", evidence}});
    };

    int availability = 20;
    std::string availabilityEvidence = running ? "process is running" : "server is intentionally offline";
    if (server.status == "Crashed") { availability = 0; availabilityEvidence = "last known state is crashed"; recommendations.push_back("Inspect recent crash logs before restarting."); }
    else if (running) availability = 25;
    else if (server.status == "Online" || server.status == "Starting") { availability = 8; availabilityEvidence = "database and process state disagree"; recommendations.push_back("Refresh lifecycle state or restart the server."); }
    addComponent("availability", availability, 25, availabilityEvidence);

    const double cpu = metrics.cpu_usage_pct;
    int cpuScore = cpu < 70.0 ? 15 : cpu < 85.0 ? 10 : cpu < 95.0 ? 5 : 1;
    addComponent("cpu", cpuScore, 15, std::to_string(static_cast<int>(cpu)) + "% host CPU used");
    if (cpuScore < 10) recommendations.push_back("Investigate sustained CPU pressure and expensive plugins or chunk generation.");

    const double ramPct = metrics.ram_total_gb > 0.0 ? metrics.ram_used_gb / metrics.ram_total_gb * 100.0 : 0.0;
    int ramScore = ramPct < 75.0 ? 15 : ramPct < 88.0 ? 10 : ramPct < 96.0 ? 5 : 1;
    addComponent("memory", ramScore, 15, std::to_string(static_cast<int>(ramPct)) + "% host RAM used");
    if (ramScore < 10) recommendations.push_back("Reduce memory pressure or adjust server heap allocation.");

    const double diskPct = metrics.disk_total_gb > 0.0 ? metrics.disk_free_gb / metrics.disk_total_gb * 100.0 : 100.0;
    int diskScore = diskPct > 20.0 ? 15 : diskPct > 10.0 ? 10 : diskPct > 5.0 ? 5 : 0;
    std::ostringstream diskEvidence;
    diskEvidence << std::fixed << std::setprecision(1) << metrics.disk_free_gb << " GB free";
    addComponent("storage", diskScore, 15, diskEvidence.str());
    if (diskScore < 10) recommendations.push_back("Free disk space before creating backups or generating more chunks.");

    auto logs = ProcessManager::getInstance().getLogs(uuid, 150);
    int errors = 0, warnings = 0;
    for (const auto& line : logs) {
        const std::string content = lowerCopy(line.type + " " + line.text);
        if (content.find("error") != std::string::npos || content.find("exception") != std::string::npos) ++errors;
        else if (content.find("warn") != std::string::npos) ++warnings;
    }
    int logScore = errors == 0 ? (warnings < 10 ? 15 : 12) : errors < 3 ? 9 : errors < 10 ? 4 : 0;
    addComponent("logs", logScore, 15, std::to_string(errors) + " errors and " + std::to_string(warnings) + " warnings in recent output");
    if (errors > 0) recommendations.push_back("Review recent errors in the console or ask the AI Server Doctor for a diagnosis.");

    auto backups = Database::getInstance().getServerBackups(uuid);
    int backupScore = 0;
    std::string backupEvidence = "no backups recorded";
    if (!backups.empty()) {
        auto latest = std::max_element(backups.begin(), backups.end(),
            [](const BackupRecord& a, const BackupRecord& b) { return a.created_at < b.created_at; });
        std::tm backupTime{};
        std::istringstream parsed(latest->created_at);
        parsed >> std::get_time(&backupTime, "%Y-%m-%d %H:%M:%S");
        const double ageHours = parsed.fail() ? 1e9 : std::difftime(std::time(nullptr), std::mktime(&backupTime)) / 3600.0;
        backupScore = ageHours <= 24.0 ? 15 : ageHours <= 168.0 ? 10 : 4;
        backupEvidence = "latest backup: " + latest->created_at;
    }
    addComponent("backups", backupScore, 15, backupEvidence);
    if (backups.empty()) recommendations.push_back("Create a backup and schedule recurring backups.");
    else if (backupScore < 15) recommendations.push_back("Create a fresh backup; the latest recovery point is more than 24 hours old.");

    std::string grade = total >= 90 ? "Excellent" : total >= 75 ? "Good" : total >= 55 ? "Needs attention" : "Critical";
    callback(newJsonResponse({
        {"status", "success"}, {"score", total}, {"grade", grade},
        {"components", components}, {"recommendations", recommendations},
        {"world_size_gb", SystemInfo::getWorldSizeGB(server.directory_path)},
        {"generated_at", formatDateTime(std::time(nullptr))}
    }));
}

void ServerController::listAutomationRules(const HttpRequestPtr& req,
                                           std::function<void(const HttpResponsePtr&)>&& callback,
                                           std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp);
    }
    ServerRecord server;
    if (!Database::getInstance().getServer(uuid, server)) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "server not found"}});
        resp->setStatusCode(k404NotFound); return callback(resp);
    }
    nlohmann::json rules = nlohmann::json::array();
    for (const auto& rule : Database::getInstance().listAutomationRules(uuid)) rules.push_back(automationToJson(rule));
    callback(newJsonResponse({{"status", "success"}, {"rules", rules}}));
}

void ServerController::createAutomationRule(const HttpRequestPtr& req,
                                             std::function<void(const HttpResponsePtr&)>&& callback,
                                             std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) {
        auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp);
    }
    if (role != "admin") { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k403Forbidden); return callback(resp); }
    ServerRecord server;
    if (!Database::getInstance().getServer(uuid, server)) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "server not found"}}); resp->setStatusCode(k404NotFound); return callback(resp);
    }
    nlohmann::json body;
    try { body = nlohmann::json::parse(req->getBody()); }
    catch (...) { auto resp = newJsonResponse({{"status", "error"}, {"message", "body must be JSON"}}); resp->setStatusCode(k400BadRequest); return callback(resp); }
    AutomationRuleRecord rule;
    rule.server_uuid = uuid;
    rule.cooldown_seconds = 300;
    rule.created_by = username;
    applyAutomationBody(body, rule);
    const std::string error = validateAutomation(rule);
    if (!error.empty()) { auto resp = newJsonResponse({{"status", "error"}, {"message", error}}); resp->setStatusCode(k400BadRequest); return callback(resp); }
    rule.id = Database::getInstance().createAutomationRule(rule);
    if (!rule.id || !Database::getInstance().getAutomationRule(rule.id, rule)) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "failed to persist automation rule"}}); resp->setStatusCode(k500InternalServerError); return callback(resp);
    }
    Database::getInstance().logAction(username, "AUTOMATION_RULE_CREATE", uuid, rule.name);
    callback(newJsonResponse({{"status", "success"}, {"rule", automationToJson(rule)}}));
}

void ServerController::updateAutomationRule(const HttpRequestPtr& req,
                                             std::function<void(const HttpResponsePtr&)>&& callback,
                                             std::string uuid, std::string rawId) {
    std::string username, role; long long id = 0;
    if (!validateJwt(req, username, role)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp); }
    if (role != "admin") { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k403Forbidden); return callback(resp); }
    AutomationRuleRecord rule;
    if (!parseId(rawId, id) || !Database::getInstance().getAutomationRule(id, rule) || rule.server_uuid != uuid) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "rule not found"}}); resp->setStatusCode(k404NotFound); return callback(resp);
    }
    nlohmann::json body;
    try { body = nlohmann::json::parse(req->getBody()); }
    catch (...) { auto resp = newJsonResponse({{"status", "error"}, {"message", "body must be JSON"}}); resp->setStatusCode(k400BadRequest); return callback(resp); }
    applyAutomationBody(body, rule);
    const std::string error = validateAutomation(rule);
    if (!error.empty()) { auto resp = newJsonResponse({{"status", "error"}, {"message", error}}); resp->setStatusCode(k400BadRequest); return callback(resp); }
    if (!Database::getInstance().updateAutomationRule(id, rule)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k500InternalServerError); return callback(resp); }
    Database::getInstance().getAutomationRule(id, rule);
    Database::getInstance().logAction(username, "AUTOMATION_RULE_UPDATE", uuid, rule.name);
    callback(newJsonResponse({{"status", "success"}, {"rule", automationToJson(rule)}}));
}

void ServerController::deleteAutomationRule(const HttpRequestPtr& req,
                                             std::function<void(const HttpResponsePtr&)>&& callback,
                                             std::string uuid, std::string rawId) {
    std::string username, role; long long id = 0; AutomationRuleRecord rule;
    if (!validateJwt(req, username, role)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp); }
    if (!parseId(rawId, id) || !Database::getInstance().getAutomationRule(id, rule) || rule.server_uuid != uuid) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "rule not found"}}); resp->setStatusCode(k404NotFound); return callback(resp);
    }
    if (!Database::getInstance().deleteAutomationRule(id)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k500InternalServerError); return callback(resp); }
    Database::getInstance().logAction(username, "AUTOMATION_RULE_DELETE", uuid, rule.name);
    callback(newJsonResponse({{"status", "success"}}));
}

void ServerController::toggleAutomationRule(const HttpRequestPtr& req,
                                             std::function<void(const HttpResponsePtr&)>&& callback,
                                             std::string uuid, std::string rawId) {
    std::string username, role; long long id = 0; AutomationRuleRecord rule;
    if (!validateJwt(req, username, role)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp); }
    if (!parseId(rawId, id) || !Database::getInstance().getAutomationRule(id, rule) || rule.server_uuid != uuid) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "rule not found"}}); resp->setStatusCode(k404NotFound); return callback(resp);
    }
    Database::getInstance().setAutomationRuleEnabled(id, rule.enabled == 0);
    Database::getInstance().getAutomationRule(id, rule);
    Database::getInstance().logAction(username, rule.enabled ? "AUTOMATION_RULE_ENABLE" : "AUTOMATION_RULE_DISABLE", uuid, rule.name);
    callback(newJsonResponse({{"status", "success"}, {"rule", automationToJson(rule)}}));
}

void ServerController::runAutomationRule(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback,
                                          std::string uuid, std::string rawId) {
    std::string username, role; long long id = 0; AutomationRuleRecord rule;
    if (!validateJwt(req, username, role)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp); }
    if (!parseId(rawId, id) || !Database::getInstance().getAutomationRule(id, rule) || rule.server_uuid != uuid) {
        auto resp = newJsonResponse({{"status", "error"}, {"message", "rule not found"}}); resp->setStatusCode(k404NotFound); return callback(resp);
    }
    auto outcome = Scheduler::getInstance().evaluateAutomationRule(rule, true);
    Database::getInstance().recordAutomationEvaluation(id, true, outcome.status, outcome.output);
    Database::getInstance().logAction(username, "AUTOMATION_RULE_RUN_NOW", uuid, rule.name + ": " + outcome.status);
    callback(newJsonResponse({{"status", "success"}, {"result", {{"status", outcome.status}, {"output", outcome.output}}}}));
}

void ServerController::getMaintenance(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback,
                                      std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp); }
    ServerRecord server;
    if (!Database::getInstance().getServer(uuid, server)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k404NotFound); return callback(resp); }
    MaintenanceRecord state;
    if (!Database::getInstance().getMaintenance(uuid, state)) state.server_uuid = uuid;
    callback(newJsonResponse({{"status", "success"}, {"maintenance", maintenanceToJson(state)}}));
}

static void applyMaintenanceBody(const nlohmann::json& body, MaintenanceRecord& state) {
    if (body.contains("message")) state.message = safeConsoleText(body.value("message", state.message));
    if (body.contains("prevent_joins")) state.prevent_joins = body.value("prevent_joins", true) ? 1 : 0;
    if (body.contains("backup_on_enable")) state.backup_on_enable = body.value("backup_on_enable", true) ? 1 : 0;
    if (state.message.empty()) state.message = "Server maintenance is in progress.";
}

void ServerController::updateMaintenance(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback,
                                         std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp); }
    if (role != "admin") { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k403Forbidden); return callback(resp); }
    ServerRecord server;
    if (!Database::getInstance().getServer(uuid, server)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k404NotFound); return callback(resp); }
    nlohmann::json body;
    try { body = nlohmann::json::parse(req->getBody()); }
    catch (...) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k400BadRequest); return callback(resp); }
    MaintenanceRecord state;
    if (!Database::getInstance().getMaintenance(uuid, state)) state.server_uuid = uuid;
    applyMaintenanceBody(body, state);
    if (!Database::getInstance().upsertMaintenance(state)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k500InternalServerError); return callback(resp); }
    Database::getInstance().getMaintenance(uuid, state);
    Database::getInstance().logAction(username, "MAINTENANCE_CONFIG_UPDATE", uuid, state.message);
    callback(newJsonResponse({{"status", "success"}, {"maintenance", maintenanceToJson(state)}}));
}

void ServerController::enableMaintenance(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback,
                                         std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp); }
    if (role != "admin") { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k403Forbidden); return callback(resp); }
    ServerRecord server;
    if (!Database::getInstance().getServer(uuid, server)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k404NotFound); return callback(resp); }
    MaintenanceRecord state;
    const bool existed = Database::getInstance().getMaintenance(uuid, state);
    if (!existed) state.server_uuid = uuid;
    try {
        if (!req->getBody().empty()) applyMaintenanceBody(nlohmann::json::parse(req->getBody()), state);
    } catch (...) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k400BadRequest); return callback(resp); }
    if (state.enabled) return callback(newJsonResponse({{"status", "success"}, {"maintenance", maintenanceToJson(state)}, {"message", "maintenance mode is already enabled"}}));

    const fs::path properties = fs::path(server.directory_path) / "server.properties";
    state.whitelist_was_enabled = lowerCopy(readProperty(properties, "white-list", "false")) == "true" ? 1 : 0;
    state.enabled = 1;
    state.enabled_by = username;

    nlohmann::json steps = nlohmann::json::array();
    if (state.backup_on_enable) {
        auto result = ServerLifecycle::createBackup(uuid, username, getBackupsDir());
        steps.push_back({{"step", "backup"}, {"ok", result.ok}, {"message", result.message}});
        if (!result.ok) {
            auto resp = newJsonResponse({{"status", "error"}, {"message", "safety backup failed; maintenance mode was not enabled"}, {"steps", steps}});
            resp->setStatusCode(k500InternalServerError); return callback(resp);
        }
    }
    if (state.prevent_joins) {
        const bool whitelistWritten = writeProperty(properties, "white-list", "true");
        const bool enforcementWritten = writeProperty(properties, "enforce-whitelist", "true");
        if (!whitelistWritten || !enforcementWritten) {
            auto resp = newJsonResponse({{"status", "error"}, {"message", "could not update server.properties; maintenance mode was not enabled"}, {"steps", steps}});
            resp->setStatusCode(k500InternalServerError); return callback(resp);
        }
    }
    if (ProcessManager::getInstance().isServerRunning(uuid)) {
        ProcessManager::getInstance().sendCommand(uuid, "say [Maintenance] " + safeConsoleText(state.message));
        if (state.prevent_joins) {
            ProcessManager::getInstance().sendCommand(uuid, "whitelist on");
            ProcessManager::getInstance().sendCommand(uuid, "whitelist reload");
        }
    }
    if (!Database::getInstance().upsertMaintenance(state)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k500InternalServerError); return callback(resp); }
    Database::getInstance().getMaintenance(uuid, state);
    Database::getInstance().logAction(username, "MAINTENANCE_ENABLE", uuid, state.message);
    callback(newJsonResponse({{"status", "success"}, {"maintenance", maintenanceToJson(state)}, {"steps", steps}}));
}

void ServerController::disableMaintenance(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback,
                                          std::string uuid) {
    std::string username, role;
    if (!validateJwt(req, username, role)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k401Unauthorized); return callback(resp); }
    if (role != "admin") { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k403Forbidden); return callback(resp); }
    ServerRecord server;
    if (!Database::getInstance().getServer(uuid, server)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k404NotFound); return callback(resp); }
    MaintenanceRecord state;
    if (!Database::getInstance().getMaintenance(uuid, state) || !state.enabled) {
        if (state.server_uuid.empty()) state.server_uuid = uuid;
        return callback(newJsonResponse({{"status", "success"}, {"maintenance", maintenanceToJson(state)}, {"message", "maintenance mode is already disabled"}}));
    }

    const fs::path properties = fs::path(server.directory_path) / "server.properties";
    if (state.prevent_joins && !state.whitelist_was_enabled) {
        const bool whitelistWritten = writeProperty(properties, "white-list", "false");
        const bool enforcementWritten = writeProperty(properties, "enforce-whitelist", "false");
        if (!whitelistWritten || !enforcementWritten) {
            auto resp = newJsonResponse({{"status", "error"}, {"message", "could not restore server.properties; maintenance mode remains enabled"}});
            resp->setStatusCode(k500InternalServerError); return callback(resp);
        }
    }
    if (ProcessManager::getInstance().isServerRunning(uuid)) {
        ProcessManager::getInstance().sendCommand(uuid, "say [Maintenance] Maintenance complete. The server is open.");
        if (state.prevent_joins && !state.whitelist_was_enabled) {
            ProcessManager::getInstance().sendCommand(uuid, "whitelist off");
        }
    }
    state.enabled = 0;
    state.enabled_by = "";
    if (!Database::getInstance().upsertMaintenance(state)) { auto resp = HttpResponse::newHttpResponse(); resp->setStatusCode(k500InternalServerError); return callback(resp); }
    Database::getInstance().getMaintenance(uuid, state);
    Database::getInstance().logAction(username, "MAINTENANCE_DISABLE", uuid, "maintenance complete");
    callback(newJsonResponse({{"status", "success"}, {"maintenance", maintenanceToJson(state)}}));
}

} // namespace MCDeploy
