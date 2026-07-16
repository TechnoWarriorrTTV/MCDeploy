#include "DnsManager.h"
#include "ObfuscatedStrings.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <fstream>

namespace MCDeploy {

// CURL write callback
static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

DnsManager& DnsManager::getInstance() {
    static DnsManager instance;
    return instance;
}

DnsManager::DnsManager() {
    // 1. Try reading from config.json
    std::ifstream configFile("config.json");
    if (configFile.is_open()) {
        try {
            nlohmann::json j;
            configFile >> j;
            if (j.contains("mcdeploy") && j["mcdeploy"].contains("dns")) {
                auto dns = j["mcdeploy"]["dns"];
                m_apiToken = dns.value("cloudflare_api_token", "");
                m_zoneId = dns.value("cloudflare_zone_id", "");
                m_domain = dns.value("cloudflare_domain", "");
            }
        } catch (...) {}
        configFile.close();
    }

    // 2. Fallback to environment variables
    if (m_apiToken.empty()) {
        const char* envVal = std::getenv("CLOUDFLARE_API_TOKEN");
        if (envVal) m_apiToken = envVal;
    }
    if (m_zoneId.empty()) {
        const char* envVal = std::getenv("CLOUDFLARE_ZONE_ID");
        if (envVal) m_zoneId = envVal;
    }
    if (m_domain.empty()) {
        const char* envVal = std::getenv("CLOUDFLARE_DOMAIN");
        if (envVal) m_domain = envVal;
    }

    // 3. Fallback to compile-time obfuscated credentials (now empty placeholders)
    if (m_apiToken.empty()) {
        m_apiToken = DnsCredentials::getApiToken();
    }
    if (m_zoneId.empty()) {
        m_zoneId = DnsCredentials::getZoneId();
    }
    if (m_domain.empty()) {
        m_domain = DnsCredentials::getDomain();
    }

    m_enabled = !m_apiToken.empty() && !m_zoneId.empty() && !m_domain.empty();
    
    if (m_enabled) {
        std::cout << "[MCDeploy DNS] Cloudflare DNS integration initialized for domain: " << m_domain << std::endl;
    } else {
        std::cerr << "[MCDeploy DNS] Warning: DNS credentials not available. Subdomain features disabled." << std::endl;
    }
}

std::string DnsManager::getPublicIp() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Return cached IP if we have it
    if (!m_cachedPublicIp.empty()) {
        return m_cachedPublicIp;
    }

    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.ipify.org");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && !response.empty()) {
        m_cachedPublicIp = response;
        std::cout << "[MCDeploy DNS] Detected public IP: " << m_cachedPublicIp << std::endl;
    } else {
        std::cerr << "[MCDeploy DNS] Failed to detect public IP from ipify.org" << std::endl;
    }

    return m_cachedPublicIp;
}

std::string DnsManager::cloudflareApiRequest(const std::string& method,
                                               const std::string& endpoint,
                                               const std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string url = "https://api.cloudflare.com/client/v4" + endpoint;
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + m_apiToken).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (method == "GET") {
        // Default is GET
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[MCDeploy DNS] Cloudflare API request failed: " << curl_easy_strerror(res) << std::endl;
        return "";
    }

    return response;
}

DnsRecordResult DnsManager::createARecord(const std::string& subdomain, const std::string& ip) {
    DnsRecordResult result;

    std::string fullName = subdomain + "." + m_domain;

    nlohmann::json payload;
    payload["type"] = "A";
    payload["name"] = fullName;
    payload["content"] = ip;
    payload["ttl"] = 60;
    payload["proxied"] = false;

    std::string endpoint = "/zones/" + m_zoneId + "/dns_records";
    std::string response = cloudflareApiRequest("POST", endpoint, payload.dump());

    if (response.empty()) {
        result.error_message = "No response from Cloudflare API";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(response);
        if (j["success"].get<bool>()) {
            result.success = true;
            result.record_id = j["result"]["id"].get<std::string>();
            std::cout << "[MCDeploy DNS] Created A record: " << fullName << " -> " << ip 
                      << " (ID: " << result.record_id << ")" << std::endl;
        } else {
            auto errors = j["errors"];
            if (!errors.empty()) {
                result.error_message = errors[0]["message"].get<std::string>();
            }
            std::cerr << "[MCDeploy DNS] Failed to create A record: " << result.error_message << std::endl;
        }
    } catch (const std::exception& e) {
        result.error_message = std::string("JSON parse error: ") + e.what();
        std::cerr << "[MCDeploy DNS] " << result.error_message << std::endl;
    }

    return result;
}

DnsRecordResult DnsManager::createSrvRecord(const std::string& subdomain, int port) {
    DnsRecordResult result;

    // SRV record format for Minecraft: _minecraft._tcp.<subdomain>.<domain>
    // This tells Minecraft clients where to connect without specifying a port
    nlohmann::json payload;
    payload["type"] = "SRV";
    payload["name"] = "_minecraft._tcp." + subdomain + "." + m_domain;
    payload["data"] = {
        {"priority", 0},
        {"weight", 5},
        {"port", port},
        {"target", subdomain + "." + m_domain}
    };
    payload["ttl"] = 60;

    std::string endpoint = "/zones/" + m_zoneId + "/dns_records";
    std::string response = cloudflareApiRequest("POST", endpoint, payload.dump());

    if (response.empty()) {
        result.error_message = "No response from Cloudflare API";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(response);
        if (j["success"].get<bool>()) {
            result.success = true;
            result.record_id = j["result"]["id"].get<std::string>();
            std::cout << "[MCDeploy DNS] Created SRV record for " << subdomain << "." << m_domain 
                      << " port " << port << " (ID: " << result.record_id << ")" << std::endl;
        } else {
            auto errors = j["errors"];
            if (!errors.empty()) {
                result.error_message = errors[0]["message"].get<std::string>();
            }
            std::cerr << "[MCDeploy DNS] Failed to create SRV record: " << result.error_message << std::endl;
        }
    } catch (const std::exception& e) {
        result.error_message = std::string("JSON parse error: ") + e.what();
        std::cerr << "[MCDeploy DNS] " << result.error_message << std::endl;
    }

    return result;
}

DnsRecordResult DnsManager::createTunnelSrvRecord(const std::string& subdomain, const std::string& targetHost, int targetPort) {
    DnsRecordResult result;

    nlohmann::json payload;
    payload["type"] = "SRV";
    payload["name"] = "_minecraft._tcp." + subdomain + "." + m_domain;
    payload["data"] = {
        {"priority", 0},
        {"weight", 5},
        {"port", targetPort},
        {"target", targetHost}
    };
    payload["ttl"] = 60;

    std::string endpoint = "/zones/" + m_zoneId + "/dns_records";
    std::string response = cloudflareApiRequest("POST", endpoint, payload.dump());

    if (response.empty()) {
        result.error_message = "No response from Cloudflare API";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(response);
        if (j["success"].get<bool>()) {
            result.success = true;
            result.record_id = j["result"]["id"].get<std::string>();
            std::cout << "[MCDeploy DNS] Created Tunnel SRV record for " << subdomain << "." << m_domain 
                      << " -> " << targetHost << ":" << targetPort << " (ID: " << result.record_id << ")" << std::endl;
        } else {
            auto errors = j["errors"];
            if (!errors.empty()) {
                result.error_message = errors[0]["message"].get<std::string>();
            }
            std::cerr << "[MCDeploy DNS] Failed to create Tunnel SRV record: " << result.error_message << std::endl;
        }
    } catch (const std::exception& e) {
        result.error_message = std::string("JSON parse error: ") + e.what();
        std::cerr << "[MCDeploy DNS] " << result.error_message << std::endl;
    }

    return result;
}

DnsRecordResult DnsManager::createCnameRecord(const std::string& subdomain, const std::string& target) {
    DnsRecordResult result;

    nlohmann::json payload;
    payload["type"] = "CNAME";
    payload["name"] = subdomain + "." + m_domain;
    payload["content"] = target;
    payload["ttl"] = 60;
    payload["proxied"] = false;

    std::string endpoint = "/zones/" + m_zoneId + "/dns_records";
    std::string response = cloudflareApiRequest("POST", endpoint, payload.dump());

    if (response.empty()) {
        result.error_message = "No response from Cloudflare API";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(response);
        if (j["success"].get<bool>()) {
            result.success = true;
            result.record_id = j["result"]["id"].get<std::string>();
            std::cout << "[MCDeploy DNS] Created CNAME record: " << subdomain << "." << m_domain 
                      << " -> " << target << " (ID: " << result.record_id << ")" << std::endl;
        } else {
            auto errors = j["errors"];
            if (!errors.empty()) {
                result.error_message = errors[0]["message"].get<std::string>();
            }
            std::cerr << "[MCDeploy DNS] Failed to create CNAME record: " << result.error_message << std::endl;
        }
    } catch (const std::exception& e) {
        result.error_message = std::string("JSON parse error: ") + e.what();
        std::cerr << "[MCDeploy DNS] " << result.error_message << std::endl;
    }

    return result;
}

bool DnsManager::createServerTunnelDnsRecords(const std::string& subdomain, const std::string& targetHost, int targetPort,
                                             std::string& outCnameRecordId, std::string& outSrvRecordId) {
    if (!m_enabled) {
        std::cerr << "[MCDeploy DNS] DNS is not enabled, skipping record creation." << std::endl;
        return false;
    }

    // 1. Create CNAME record pointing to target host (bore.pub) so A/CNAME queries resolve
    auto cnameResult = createCnameRecord(subdomain, targetHost);
    if (!cnameResult.success) {
        std::cerr << "[MCDeploy DNS] CNAME record creation failed: " << cnameResult.error_message << std::endl;
        return false;
    }
    outCnameRecordId = cnameResult.record_id;

    // 2. Create SRV record pointing to target host and port
    auto srvResult = createTunnelSrvRecord(subdomain, targetHost, targetPort);
    if (!srvResult.success) {
        std::cerr << "[MCDeploy DNS] Tunnel SRV record creation failed: " << srvResult.error_message << std::endl;
        // Rollback CNAME
        deleteRecord(outCnameRecordId);
        outCnameRecordId.clear();
        return false;
    }
    outSrvRecordId = srvResult.record_id;

    std::cout << "[MCDeploy DNS] Tunnel DNS CNAME + SRV records created successfully for " << subdomain << "." << m_domain << std::endl;
    return true;
}

bool DnsManager::deleteRecord(const std::string& recordId) {
    if (recordId.empty()) return true; // Nothing to delete

    std::string endpoint = "/zones/" + m_zoneId + "/dns_records/" + recordId;
    std::string response = cloudflareApiRequest("DELETE", endpoint);

    if (response.empty()) {
        std::cerr << "[MCDeploy DNS] Failed to delete record " << recordId << ": no response" << std::endl;
        return false;
    }

    try {
        auto j = nlohmann::json::parse(response);
        if (j["success"].get<bool>()) {
            std::cout << "[MCDeploy DNS] Deleted DNS record: " << recordId << std::endl;
            return true;
        } else {
            std::cerr << "[MCDeploy DNS] Failed to delete record " << recordId << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[MCDeploy DNS] Error parsing delete response: " << e.what() << std::endl;
        return false;
    }
}

std::string DnsManager::offlineReservationTarget() const {
    return "offline." + m_domain;
}

DnsAvailabilityResult DnsManager::checkSubdomainAvailability(const std::string& subdomain) {
    DnsAvailabilityResult result;
    if (!m_enabled) {
        result.error_message = "Authoritative DNS service is not configured.";
        return result;
    }

    const std::string fullName = subdomain + "." + m_domain;
    const std::string srvName = "_minecraft._tcp." + fullName;
    const std::string endpoints[] = {
        "/zones/" + m_zoneId + "/dns_records?name=" + fullName,
        "/zones/" + m_zoneId + "/dns_records?name=" + srvName
    };

    for (const auto& endpoint : endpoints) {
        const std::string response = cloudflareApiRequest("GET", endpoint);
        if (response.empty()) {
            result.error_message = "No response from the authoritative DNS service.";
            return result;
        }
        try {
            const auto json = nlohmann::json::parse(response);
            if (!json.value("success", false) || !json.contains("result") || !json["result"].is_array()) {
                result.error_message = "The authoritative DNS service rejected the availability lookup.";
                return result;
            }
            if (!json["result"].empty()) {
                result.status = DnsAvailabilityStatus::Taken;
                return result;
            }
        } catch (const std::exception& error) {
            result.error_message = std::string("Invalid authoritative DNS response: ") + error.what();
            return result;
        }
    }

    result.status = DnsAvailabilityStatus::Available;
    return result;
}

DnsRecordResult DnsManager::reserveSubdomain(const std::string& subdomain) {
    const auto availability = checkSubdomainAvailability(subdomain);
    if (availability.status != DnsAvailabilityStatus::Available) {
        DnsRecordResult result;
        result.error_message = availability.status == DnsAvailabilityStatus::Taken
            ? "The subdomain is already reserved."
            : availability.error_message;
        return result;
    }

    auto reservation = createCnameRecord(subdomain, offlineReservationTarget());
    if (!reservation.success) return reservation;

    // Verify ownership after creation. This closes the check/create race even if the
    // DNS provider accepts duplicate CNAME requests issued at nearly the same time.
    const std::string fullName = subdomain + "." + m_domain;
    const std::string endpoint = "/zones/" + m_zoneId + "/dns_records?name=" + fullName;
    const std::string response = cloudflareApiRequest("GET", endpoint);
    bool uniquelyOwned = false;
    if (!response.empty()) {
        try {
            const auto json = nlohmann::json::parse(response);
            if (json.value("success", false) && json.contains("result") &&
                json["result"].is_array() && json["result"].size() == 1) {
                uniquelyOwned = json["result"][0].value("id", "") == reservation.record_id;
            }
        } catch (...) {
            uniquelyOwned = false;
        }
    }

    if (!uniquelyOwned) {
        deleteRecord(reservation.record_id);
        reservation.success = false;
        reservation.record_id.clear();
        reservation.error_message = "The reservation could not be uniquely confirmed; another request may have claimed it.";
    }
    return reservation;
}

bool DnsManager::updateCnameRecord(const std::string& recordId,
                                   const std::string& subdomain,
                                   const std::string& target) {
    if (recordId.empty()) return false;
    nlohmann::json payload;
    payload["type"] = "CNAME";
    payload["name"] = subdomain + "." + m_domain;
    payload["content"] = target;
    payload["ttl"] = 60;
    payload["proxied"] = false;

    const std::string endpoint = "/zones/" + m_zoneId + "/dns_records/" + recordId;
    const std::string response = cloudflareApiRequest("PUT", endpoint, payload.dump());
    if (response.empty()) return false;
    try {
        return nlohmann::json::parse(response).value("success", false);
    } catch (...) {
        return false;
    }
}

bool DnsManager::activateReservedSubdomain(const std::string& reservationRecordId,
                                           const std::string& subdomain,
                                           const std::string& targetHost,
                                           int targetPort,
                                           std::string& outSrvRecordId) {
    outSrvRecordId.clear();
    if (!m_enabled || reservationRecordId.empty()) return false;
    if (!updateCnameRecord(reservationRecordId, subdomain, targetHost)) return false;

    const auto srv = createTunnelSrvRecord(subdomain, targetHost, targetPort);
    if (!srv.success) {
        updateCnameRecord(reservationRecordId, subdomain, offlineReservationTarget());
        return false;
    }
    outSrvRecordId = srv.record_id;
    return true;
}

bool DnsManager::deactivateReservedSubdomain(const std::string& reservationRecordId,
                                             const std::string& subdomain,
                                             const std::string& srvRecordId) {
    if (!m_enabled || reservationRecordId.empty()) return false;
    const bool srvRemoved = srvRecordId.empty() || deleteRecord(srvRecordId);
    const bool reservationOffline = updateCnameRecord(
        reservationRecordId, subdomain, offlineReservationTarget());
    return srvRemoved && reservationOffline;
}

bool DnsManager::subdomainExistsInDns(const std::string& subdomain) {
    return checkSubdomainAvailability(subdomain).status != DnsAvailabilityStatus::Available;
}

bool DnsManager::createServerDnsRecords(const std::string& subdomain, int port,
                                          std::string& outARecordId, std::string& outSrvRecordId) {
    if (!m_enabled) {
        std::cerr << "[MCDeploy DNS] DNS is not enabled, skipping record creation." << std::endl;
        return false;
    }

    // Get public IP
    std::string publicIp = getPublicIp();
    if (publicIp.empty()) {
        std::cerr << "[MCDeploy DNS] Cannot create DNS records: failed to detect public IP." << std::endl;
        return false;
    }

    // Create A record
    auto aResult = createARecord(subdomain, publicIp);
    if (!aResult.success) {
        std::cerr << "[MCDeploy DNS] A record creation failed: " << aResult.error_message << std::endl;
        return false;
    }
    outARecordId = aResult.record_id;

    // Create SRV record
    auto srvResult = createSrvRecord(subdomain, port);
    if (!srvResult.success) {
        std::cerr << "[MCDeploy DNS] SRV record creation failed: " << srvResult.error_message << std::endl;
        // Rollback the A record since SRV failed
        deleteRecord(outARecordId);
        outARecordId.clear();
        return false;
    }
    outSrvRecordId = srvResult.record_id;

    std::cout << "[MCDeploy DNS] DNS records created successfully for " << subdomain << "." << m_domain << std::endl;
    return true;
}

bool DnsManager::removeServerDnsRecords(const std::string& aRecordId, const std::string& srvRecordId) {
    bool aOk = deleteRecord(aRecordId);
    bool srvOk = deleteRecord(srvRecordId);

    if (aOk && srvOk) {
        std::cout << "[MCDeploy DNS] DNS records cleaned up successfully." << std::endl;
    }

    return aOk && srvOk;
}

bool DnsManager::forceCleanupSubdomain(const std::string& subdomain) {
    if (!m_enabled || subdomain.empty()) return false;

    std::string fullName = subdomain + "." + m_domain;
    std::string srvName = "_minecraft._tcp." + subdomain + "." + m_domain;
    bool success = true;

    // 1. Search and delete A records
    std::string aEndpoint = "/zones/" + m_zoneId + "/dns_records?name=" + fullName;
    std::string aResponse = cloudflareApiRequest("GET", aEndpoint);
    if (!aResponse.empty()) {
        try {
            auto j = nlohmann::json::parse(aResponse);
            if (j["success"].get<bool>() && j.contains("result")) {
                for (const auto& record : j["result"]) {
                    std::string id = record["id"].get<std::string>();
                    std::cout << "[MCDeploy DNS] Found orphaned A record for " << fullName << " (ID: " << id << "). Deleting..." << std::endl;
                    if (!deleteRecord(id)) {
                        success = false;
                    }
                }
            }
        } catch (...) {
            success = false;
        }
    }

    // 2. Search and delete SRV records
    std::string srvEndpoint = "/zones/" + m_zoneId + "/dns_records?name=" + srvName;
    std::string srvResponse = cloudflareApiRequest("GET", srvEndpoint);
    if (!srvResponse.empty()) {
        try {
            auto j = nlohmann::json::parse(srvResponse);
            if (j["success"].get<bool>() && j.contains("result")) {
                for (const auto& record : j["result"]) {
                    std::string id = record["id"].get<std::string>();
                    std::cout << "[MCDeploy DNS] Found orphaned SRV record for " << srvName << " (ID: " << id << "). Deleting..." << std::endl;
                    if (!deleteRecord(id)) {
                        success = false;
                    }
                }
            }
        } catch (...) {
            success = false;
        }
    }

    return success;
}

std::string DnsManager::getFullAddress(const std::string& subdomain) {
    return subdomain + "." + m_domain;
}

} // namespace MCDeploy

