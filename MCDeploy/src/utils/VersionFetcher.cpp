#include "VersionFetcher.h"
#include <iostream>
#include <fstream>
#include <curl/curl.h>

namespace MCDeploy {

VersionFetcher& VersionFetcher::getInstance() {
    static VersionFetcher instance;
    return instance;
}

size_t VersionFetcher::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

size_t VersionFetcher::fileWriteCallback(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::ofstream* out = (std::ofstream*)stream;
    out->write((const char*)ptr, size * nmemb);
    return size * nmemb;
}

struct ProgData {
    std::function<void(double, double)> cb;
};

int VersionFetcher::progressCallback(void* clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
    (void)ultotal; (void)ulnow;
    if (clientp) {
        ProgData* data = (ProgData*)clientp;
        if (data->cb) {
            data->cb(dltotal, dlnow);
        }
    }
    return 0;
}

std::string VersionFetcher::httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    // User agent is helpful for some APIs
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MCDeploy/1.0 (Minecraft Server Dashboard)");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[MCDeploy curl GET error]: " << curl_easy_strerror(res) << " for URL: " << url << std::endl;
        return "";
    }
    return readBuffer;
}

std::string VersionFetcher::httpGetWithHeaders(const std::string& url, const std::vector<std::string>& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string readBuffer;
    struct curl_slist* chunk = nullptr;
    for (const auto& h : headers) {
        chunk = curl_slist_append(chunk, h.c_str());
    }
    if (chunk) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MCDeploy/1.0 (Minecraft Server Dashboard)");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (chunk) {
        curl_slist_free_all(chunk);
    }

    if (res != CURLE_OK) {
        std::cerr << "[MCDeploy curl GET error]: " << curl_easy_strerror(res) << " for URL: " << url << std::endl;
        return "";
    }
    return readBuffer;
}

std::vector<std::string> VersionFetcher::fetchVersions(const std::string& software) {
    std::vector<std::string> versions;
    
    try {
        if (software == "paper") {
            std::string res = httpGet("https://api.papermc.io/v2/projects/paper");
            if (!res.empty()) {
                auto json = nlohmann::json::parse(res);
                if (json.contains("versions")) {
                    for (const auto& v : json["versions"]) {
                        versions.push_back(v.get<std::string>());
                    }
                }
            }
        } else if (software == "purpur") {
            std::string res = httpGet("https://api.purpurmc.org/v2/purpur");
            if (!res.empty()) {
                auto json = nlohmann::json::parse(res);
                if (json.contains("versions")) {
                    for (const auto& v : json["versions"]) {
                        versions.push_back(v.get<std::string>());
                    }
                }
            }
        } else if (software == "vanilla" || software == "fabric" || software == "forge" || software == "neoforge") {
            std::string res = httpGet("https://launchermeta.mojang.com/mc/game/version_manifest.json");
            if (!res.empty()) {
                auto json = nlohmann::json::parse(res);
                if (json.contains("versions")) {
                    for (const auto& entry : json["versions"]) {
                        if (entry["type"] == "release") {
                            versions.push_back(entry["id"].get<std::string>());
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[MCDeploy VersionFetcher] Error parsing versions: " << e.what() << std::endl;
    }

    // Default offline/fallback versions to prevent breaking without internet
    if (versions.empty()) {
        std::cout << "[MCDeploy VersionFetcher] Using local fallback versions list due to connectivity issues." << std::endl;
        versions = {"1.20.4", "1.20.2", "1.20.1", "1.19.4", "1.18.2", "1.16.5", "1.12.2"};
    } else {
        // Reverse to show latest first
        std::reverse(versions.begin(), versions.end());
    }

    return versions;
}

std::string VersionFetcher::getDownloadUrl(const std::string& software, const std::string& version, const std::string& build) {
    try {
        if (software == "paper") {
            std::string buildsRes = httpGet("https://api.papermc.io/v2/projects/paper/versions/" + version);
            if (!buildsRes.empty()) {
                auto json = nlohmann::json::parse(buildsRes);
                if (json.contains("builds") && !json["builds"].empty()) {
                    std::string targetBuild = build;
                    if (build == "latest") {
                        targetBuild = std::to_string(json["builds"].back().get<int>());
                    }
                    return "https://api.papermc.io/v2/projects/paper/versions/" + version + 
                           "/builds/" + targetBuild + "/downloads/paper-" + version + "-" + targetBuild + ".jar";
                }
            }
        } else if (software == "purpur") {
            return "https://api.purpurmc.org/v2/purpur/" + version + "/" + build + "/download";
        } else if (software == "vanilla") {
            std::string res = httpGet("https://launchermeta.mojang.com/mc/game/version_manifest.json");
            if (!res.empty()) {
                auto json = nlohmann::json::parse(res);
                for (const auto& entry : json["versions"]) {
                    if (entry["id"] == version) {
                        std::string metaRes = httpGet(entry["url"].get<std::string>());
                        if (!metaRes.empty()) {
                            auto metaJson = nlohmann::json::parse(metaRes);
                            if (metaJson.contains("downloads") && metaJson["downloads"].contains("server")) {
                                return metaJson["downloads"]["server"]["url"].get<std::string>();
                            }
                        }
                    }
                }
            }
        } else if (software == "fabric") {
            std::string loaderVer = "0.15.11"; // Default fallback
            std::string installerVer = "0.11.2"; // Default fallback
            
            std::string loaderRes = httpGet("https://meta.fabricmc.net/v2/versions/loader/" + version);
            if (!loaderRes.empty()) {
                try {
                    auto json = nlohmann::json::parse(loaderRes);
                    if (json.is_array() && !json.empty() && json[0].contains("loader") && json[0]["loader"].contains("version")) {
                        loaderVer = json[0]["loader"]["version"].get<std::string>();
                    }
                } catch(...) {}
            }
            
            std::string installerRes = httpGet("https://meta.fabricmc.net/v2/versions/installer");
            if (!installerRes.empty()) {
                try {
                    auto json = nlohmann::json::parse(installerRes);
                    if (json.is_array() && !json.empty() && json[0].contains("version")) {
                        installerVer = json[0]["version"].get<std::string>();
                    }
                } catch(...) {}
            }
            
            return "https://meta.fabricmc.net/v2/versions/loader/" + version + "/" + loaderVer + "/" + installerVer + "/server/jar";
        } else if (software == "forge") {
            std::string res = httpGet("https://api.serverjars.in/v1.1/fetchJar/forge/" + version);
            if (!res.empty()) {
                try {
                    auto json = nlohmann::json::parse(res);
                    if (json.contains("response") && json["response"].contains("url")) {
                        return json["response"]["url"].get<std::string>();
                    }
                } catch(...) {}
            }
            // If API fails, fall back to standard URL construction if possible, or return empty.
            return "";
        } else if (software == "neoforge") {
            std::string prefix = "";
            std::vector<std::string> parts;
            size_t start = 0;
            while (true) {
                size_t end = version.find('.', start);
                if (end == std::string::npos) {
                    parts.push_back(version.substr(start));
                    break;
                }
                parts.push_back(version.substr(start, end - start));
                start = end + 1;
            }
            if (parts.size() >= 2 && parts[0] == "1") {
                std::string major = parts[1];
                std::string minor = (parts.size() >= 3) ? parts[2] : "0";
                prefix = major + "." + minor + ".";
            }
            
            if (!prefix.empty()) {
                std::string res = httpGet("https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml");
                if (!res.empty()) {
                    std::string bestVersion = "";
                    size_t pos = 0;
                    while (true) {
                        size_t startXml = res.find("<version>", pos);
                        if (startXml == std::string::npos) break;
                        size_t endXml = res.find("</version>", startXml);
                        if (endXml == std::string::npos) break;
                        std::string ver = res.substr(startXml + 9, endXml - (startXml + 9));
                        
                        if (ver.rfind(prefix, 0) == 0) {
                            bestVersion = ver;
                        }
                        pos = endXml + 10;
                    }
                    if (!bestVersion.empty()) {
                        return "https://maven.neoforged.net/releases/net/neoforged/neoforge/" + bestVersion + 
                               "/neoforge-" + bestVersion + "-installer.jar";
                    }
                }
            }
            return "";
        }
    } catch (...) {}

    // Fallbacks if APIs fail
    if (software == "paper") {
        return "https://api.papermc.io/v2/projects/paper/versions/1.20.4/builds/496/downloads/paper-1.20.4-496.jar";
    } else if (software == "purpur") {
        return "https://api.purpurmc.org/v2/purpur/1.20.4/latest/download";
    }
    return "https://piston-data.mojang.com/v1/objects/8dd1a33047f5c7b5a48e7b358572e1e9ac20a45f/server.jar"; // 1.20.4 vanilla
}

bool VersionFetcher::downloadFile(const std::string& url, const std::string& outputPath, 
                                  std::function<void(double, double)> progressCb) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile) {
        curl_easy_cleanup(curl);
        return false;
    }

    ProgData data{progressCb};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outFile);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MCDeploy/1.0 (Minecraft Server Dashboard)");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &data);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    outFile.close();

    if (res != CURLE_OK) {
        std::cerr << "[MCDeploy Download Error]: " << curl_easy_strerror(res) << std::endl;
        std::filesystem::remove(outputPath);
        return false;
    }
    return true;
}

} // namespace MCDeploy
