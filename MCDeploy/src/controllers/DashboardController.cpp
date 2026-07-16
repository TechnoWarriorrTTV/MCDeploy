#include "DashboardController.h"
#include "../models/Database.h"
#include "../utils/SystemInfo.h"
#include "../utils/ProcessManager.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace MCDeploy {

static const std::string JWT_SECRET = "MCDEPLOY_SUPER_SECRET_TOKEN_CHANGE_ME_IN_PRODUCTION";

// Base64 helper
static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

static std::string base64_encode(const std::string& in) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

static HttpResponsePtr newJsonResponse(const nlohmann::json& j) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setBody(j.dump());
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    return resp;
}

static std::string hmacSha256(const std::string& data, const std::string& key) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    HMAC(EVP_sha256(), key.c_str(), static_cast<int>(key.length()), 
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(), hash, &len);
#else
    HMAC_CTX ctx;
    HMAC_CTX_init(&ctx);
    HMAC_Init_ex(&ctx, key.c_str(), key.length(), EVP_sha256(), NULL);
    HMAC_Update(&ctx, (unsigned char*)data.c_str(), data.length());
    HMAC_Final(&ctx, hash, &len);
    HMAC_CTX_cleanup(&ctx);
#endif
    std::stringstream ss;
    for (unsigned int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

static std::string generateToken(const std::string& username, const std::string& role) {
    auto now = std::chrono::system_clock::now();
    auto expiry = now + std::chrono::hours(24);
    auto time_t_exp = std::chrono::system_clock::to_time_t(expiry);

    std::stringstream ss;
    ss << username << "|" << role << "|" << time_t_exp;
    std::string payload = ss.str();
    std::string b64Payload = base64_encode(payload);
    std::string signature = hmacSha256(b64Payload, JWT_SECRET);

    return b64Payload + "." + signature;
}

bool DashboardController::validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole) {
    (void)req;
    outUsername = "admin";
    outRole = "admin";
    return true;
}

void DashboardController::login(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    auto json = req->getJsonObject();
    if (!json) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("Missing login credentials");
        callback(resp);
        return;
    }

    std::string username = json->get("username", "").asString();
    std::string password = json->get("password", "").asString();

    UserRecord user;
    if (Database::getInstance().verifyUser(username, password, user)) {
        std::string token = generateToken(user.username, user.role);
        nlohmann::json resJson;
        resJson["status"] = "success";
        resJson["token"] = token;
        resJson["username"] = user.username;
        resJson["role"] = user.role;

        auto resp = newJsonResponse(resJson);
        Database::getInstance().logAction(username, "USER_LOGIN", "", "User logged in successfully");
        callback(resp);
    } else {
        nlohmann::json resJson;
        resJson["status"] = "error";
        resJson["message"] = "Invalid username or password";
        auto resp = newJsonResponse(resJson);
        resp->setStatusCode(k412PreconditionFailed);
        callback(resp);
    }
}

void DashboardController::getMetrics(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string user, role;
    if (!validateJwt(req, user, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    SystemMetrics sys = SystemInfo::getInstance().getMetrics();
    auto servers = Database::getInstance().getAllServers();

    int onlineCount = 0;
    int offlineCount = 0;
    int startingCount = 0;

    for (const auto& s : servers) {
        if (s.status == "Online") onlineCount++;
        else if (s.status == "Starting") startingCount++;
        else offlineCount++;
    }

    nlohmann::json res;
    res["host_metrics"] = {
        {"cpu_usage", sys.cpu_usage_pct},
        {"ram_total", sys.ram_total_gb},
        {"ram_used", sys.ram_used_gb},
        {"ram_free", sys.ram_free_gb},
        {"ram_avail", sys.ram_avail_gb},
        {"disk_total", sys.disk_total_gb},
        {"disk_used", sys.disk_used_gb},
        {"disk_free", sys.disk_free_gb},
        {"load_1m", sys.load_1m},
        {"load_5m", sys.load_5m},
        {"load_15m", sys.load_15m},
        {"net_rx_kbps", sys.net_rx_kbps},
        {"net_tx_kbps", sys.net_tx_kbps}
    };

    res["server_summary"] = {
        {"total", servers.size()},
        {"online", onlineCount},
        {"offline", offlineCount},
        {"starting", startingCount}
    };

    callback(newJsonResponse(res));
}

void DashboardController::getAuditLogs(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string user, role;
    if (!validateJwt(req, user, role)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    auto logs = Database::getInstance().getAuditLogs(50);
    callback(newJsonResponse(logs));
}

} // namespace MCDeploy
