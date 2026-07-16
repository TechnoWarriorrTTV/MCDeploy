#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace MCDeploy {

struct SoftwareVersion {
    std::string version;
    std::string build;
    std::string download_url;
    bool is_experimental = false;
};

class VersionFetcher {
public:
    static VersionFetcher& getInstance();

    // Fetch lists of versions for different softwares
    std::vector<std::string> fetchVersions(const std::string& software);

    // Get the download URL for a specific software, version, and build
    std::string getDownloadUrl(const std::string& software, const std::string& version, const std::string& build = "latest");

    // Download file with a progress callback
    bool downloadFile(const std::string& url, const std::string& outputPath, 
                      std::function<void(double totalToDownload, double nowDownloaded)> progressCb = nullptr);

    // HTTP helper methods
    std::string httpGet(const std::string& url);
    std::string httpGetWithHeaders(const std::string& url, const std::vector<std::string>& headers);

private:
    VersionFetcher() = default;
    ~VersionFetcher() = default;
    VersionFetcher(const VersionFetcher&) = delete;
    VersionFetcher& operator=(const VersionFetcher&) = delete;
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static size_t fileWriteCallback(void* ptr, size_t size, size_t nmemb, void* stream);
    static int progressCallback(void* clientp, double dltotal, double dlnow, double ultotal, double ulnow);
};

} // namespace MCDeploy
