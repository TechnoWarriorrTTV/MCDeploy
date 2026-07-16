#include "WebPanelController.h"
#include "ServerController.h"
#include "../ai/AIAgent.h"
#include "../models/Database.h"
#include "../utils/ProcessManager.h"
#include "../utils/ServerLifecycle.h"
#include "../utils/SystemInfo.h"
#include "../utils/VersionFetcher.h"

#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <thread>

namespace MCDeploy {
namespace {

std::string& tokenSecret() {
    static std::string secret = [] {
        const char* configured = std::getenv("MCDEPLOY_WEBPANEL_SECRET");
        if (configured && std::char_traits<char>::length(configured) >= 32)
            return std::string(configured);
        unsigned char bytes[32] = {};
        if (RAND_bytes(bytes, sizeof(bytes)) != 1)
            return std::string("mcdeploy-ephemeral-webpanel-secret");
        std::ostringstream value;
        for (const auto byte : bytes)
            value << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        return value.str();
    }();
    return secret;
}

HttpResponsePtr jsonResponse(const nlohmann::json& body, HttpStatusCode code = k200OK) {
    auto response = HttpResponse::newHttpResponse();
    response->setStatusCode(code);
    response->setContentTypeCode(CT_APPLICATION_JSON);
    response->setBody(body.dump());
    return response;
}

HttpResponsePtr errorResponse(const std::string& message, HttpStatusCode code) {
    return jsonResponse({{"status", "error"}, {"message", message}}, code);
}

std::string hmacSha256(const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    const auto& secret = tokenSecret();
    HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash, &length);
    std::ostringstream output;
    for (unsigned int i = 0; i < length; ++i)
        output << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    return output.str();
}

std::string base64Encode(const std::string& input) {
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    int value = 0;
    int bits = -6;
    for (const unsigned char c : input) {
        value = (value << 8) + c;
        bits += 8;
        while (bits >= 0) {
            output.push_back(alphabet[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) output.push_back(alphabet[((value << 8) >> (bits + 8)) & 0x3F]);
    while (output.size() % 4) output.push_back('=');
    return output;
}

std::string generateToken(const std::string& email) {
    const auto expiry = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() + std::chrono::hours(24));
    const std::string encoded = base64Encode(email + "|webpanel|" + std::to_string(expiry));
    return encoded + "." + hmacSha256(encoded);
}

std::string base64Decode(const std::string& input) {
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    int value = 0;
    int bits = -8;
    for (const unsigned char c : input) {
        if (c == '=') break;
        const auto position = alphabet.find(static_cast<char>(c));
        if (position == std::string::npos) return {};
        value = (value << 6) + static_cast<int>(position);
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return output;
}

struct OAuthProviderConfig {
    std::string name;
    std::string clientId;
    std::string clientSecret;
    std::string authorizationUrl;
    std::string tokenUrl;
    std::string userUrl;
    std::string scope;
};

struct OAuthProfile {
    std::string subject;
    std::string email;
    std::string displayName;
    std::string avatarUrl;
};

std::string environmentValue(const char* name, const std::string& fallback = {}) {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : fallback;
}

const nlohmann::json& applicationConfig() {
    static const nlohmann::json config = [] {
        std::ifstream input("config.json");
        if (!input.is_open()) return nlohmann::json::object();
        try { return nlohmann::json::parse(input); }
        catch (...) { return nlohmann::json::object(); }
    }();
    return config;
}

std::string configuredOAuthValue(const std::string& provider, const std::string& key,
                                 const char* environmentName) {
    const std::string fromEnvironment = environmentValue(environmentName);
    if (!fromEnvironment.empty()) return fromEnvironment;
    const auto& root = applicationConfig();
    if (!root.contains("mcdeploy") || !root["mcdeploy"].contains("oauth")) return {};
    const auto& oauth = root["mcdeploy"]["oauth"];
    if (!oauth.contains(provider) || !oauth[provider].is_object()) return {};
    return oauth[provider].value(key, "");
}

std::string configuredEmailValue(const std::string& key, const char* environmentName,
                                 const std::string& fallback = {}) {
    const std::string fromEnvironment = environmentValue(environmentName);
    if (!fromEnvironment.empty()) return fromEnvironment;
    const auto& root = applicationConfig();
    if (!root.contains("mcdeploy") || !root["mcdeploy"].contains("email") ||
        !root["mcdeploy"]["email"].is_object()) return fallback;
    return root["mcdeploy"]["email"].value(key, fallback);
}

std::string webpanelUrl() {
    std::string fallback = "http://localhost:5174";
    const auto& root = applicationConfig();
    if (root.contains("mcdeploy")) fallback = root["mcdeploy"].value("webpanel_url", fallback);
    std::string value = environmentValue("MCDEPLOY_WEBPANEL_URL", fallback);
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

std::string publicApiUrl() {
    std::string fallback = "http://localhost:8082";
    const auto& root = applicationConfig();
    if (root.contains("mcdeploy")) fallback = root["mcdeploy"].value("public_url", fallback);
    std::string value = environmentValue("MCDEPLOY_PUBLIC_URL", fallback);
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

OAuthProviderConfig providerConfig(const std::string& provider) {
    if (provider == "google") return {
        provider,
        configuredOAuthValue("google", "client_id", "MCDEPLOY_GOOGLE_CLIENT_ID"),
        configuredOAuthValue("google", "client_secret", "MCDEPLOY_GOOGLE_CLIENT_SECRET"),
        "https://accounts.google.com/o/oauth2/v2/auth",
        "https://oauth2.googleapis.com/token",
        "https://openidconnect.googleapis.com/v1/userinfo",
        "openid email profile"
    };
    if (provider == "github") return {
        provider,
        configuredOAuthValue("github", "client_id", "MCDEPLOY_GITHUB_CLIENT_ID"),
        configuredOAuthValue("github", "client_secret", "MCDEPLOY_GITHUB_CLIENT_SECRET"),
        "https://github.com/login/oauth/authorize",
        "https://github.com/login/oauth/access_token",
        "https://api.github.com/user",
        "read:user user:email"
    };
    if (provider == "discord") return {
        provider,
        configuredOAuthValue("discord", "client_id", "MCDEPLOY_DISCORD_CLIENT_ID"),
        configuredOAuthValue("discord", "client_secret", "MCDEPLOY_DISCORD_CLIENT_SECRET"),
        "https://discord.com/oauth2/authorize",
        "https://discord.com/api/oauth2/token",
        "https://discord.com/api/users/@me",
        "identify email"
    };
    return {};
}

bool providerConfigured(const OAuthProviderConfig& provider) {
    return !provider.name.empty() && !provider.clientId.empty() && !provider.clientSecret.empty();
}

std::string callbackUrl(const std::string& provider) {
    return publicApiUrl() + "/api/webpanel/oauth/" + provider + "/callback";
}

std::string urlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') encoded << c;
        else encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return encoded.str();
}

std::string randomToken(std::size_t byteCount = 32) {
    std::vector<unsigned char> bytes(byteCount);
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) return {};
    std::ostringstream output;
    for (const auto byte : bytes)
        output << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    return output.str();
}

std::string sha256Hex(const std::string& value) {
    unsigned char hash[EVP_MAX_MD_SIZE] = {};
    unsigned int length = 0;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) return {};
    EVP_DigestInit_ex(context, EVP_sha256(), nullptr);
    EVP_DigestUpdate(context, value.data(), value.size());
    EVP_DigestFinal_ex(context, hash, &length);
    EVP_MD_CTX_free(context);
    std::ostringstream output;
    for (unsigned int i = 0; i < length; ++i)
        output << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    return output.str();
}

std::string makeOAuthState(const std::string& provider) {
    const auto expiry = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() + std::chrono::minutes(10));
    const std::string payload = base64Encode(provider + "|" + std::to_string(expiry) + "|" + randomToken(16));
    return payload + "." + hmacSha256(payload);
}

bool validateOAuthState(const std::string& state, const std::string& provider) {
    const auto separator = state.find('.');
    if (separator == std::string::npos) return false;
    const std::string payload = state.substr(0, separator);
    const std::string signature = state.substr(separator + 1);
    const std::string expected = hmacSha256(payload);
    if (signature.size() != expected.size() ||
        CRYPTO_memcmp(signature.data(), expected.data(), expected.size()) != 0) return false;
    const std::string decoded = base64Decode(payload);
    const auto first = decoded.find('|');
    const auto second = decoded.find('|', first == std::string::npos ? first : first + 1);
    if (first == std::string::npos || second == std::string::npos || decoded.substr(0, first) != provider)
        return false;
    try {
        const auto expiry = std::stoll(decoded.substr(first + 1, second - first - 1));
        return expiry > std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    } catch (...) {
        return false;
    }
}

size_t curlWrite(void* data, size_t size, size_t count, void* output) {
    static_cast<std::string*>(output)->append(static_cast<char*>(data), size * count);
    return size * count;
}

bool httpRequest(const std::string& url, const std::string& postBody,
                 const std::string& bearer, std::string& output, std::string& error) {
    static const bool curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!curlReady) { error = "OAuth networking is unavailable."; return false; }
    CURL* curl = curl_easy_init();
    if (!curl) { error = "OAuth networking is unavailable."; return false; }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: MCDeploy-Webpanel");
    if (!postBody.empty()) headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    if (!bearer.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + bearer).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    if (!postBody.empty()) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody.c_str());
    }
    const auto result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || status < 200 || status >= 300) {
        error = "The identity provider rejected the OAuth request.";
        return false;
    }
    return true;
}

bool sendVerificationEmail(const std::string& email, const std::string& code, std::string& error) {
    const std::string apiKey = configuredEmailValue("resend_api_key", "MCDEPLOY_RESEND_API_KEY");
    const std::string from = configuredEmailValue("from", "MCDEPLOY_EMAIL_FROM",
                                                   "MCDeploy <onboarding@resend.dev>");
    if (apiKey.empty()) {
        error = "Email verification is not configured.";
        return false;
    }
    static const bool curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!curlReady) { error = "Email delivery is unavailable."; return false; }

    const nlohmann::json body = {
        {"from", from}, {"to", nlohmann::json::array({email})},
        {"subject", "Your MCDeploy verification code"},
        {"text", "An MCDeploy account creation was attempted with this email. Your verification code is " +
                 code + ". It expires in 10 minutes. If this was not you, you can ignore this email."},
        {"html", "<div style=\"font-family:Arial,sans-serif;max-width:520px;margin:auto;padding:24px\">"
                 "<h2>Verify your MCDeploy account</h2>"
                 "<p>An account creation was attempted with this email address.</p>"
                 "<p>Your six-digit verification code is:</p>"
                 "<div style=\"font-size:32px;font-weight:700;letter-spacing:8px;padding:16px 0\">" + code + "</div>"
                 "<p>This code expires in 10 minutes. If this was not you, ignore this email.</p></div>"}
    };
    std::string responseBody;
    CURL* curl = curl_easy_init();
    if (!curl) { error = "Email delivery is unavailable."; return false; }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + apiKey).c_str());
    const std::string payload = body.dump();
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.resend.com/emails");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    long status = 0;
    const auto result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || status < 200 || status >= 300) {
        error = "The verification email could not be sent. Check the Resend sender configuration.";
        return false;
    }
    return true;
}

std::string secureVerificationCode() {
    unsigned int value = 0;
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&value), sizeof(value)) != 1) return {};
    value %= 1000000;
    std::ostringstream code;
    code << std::setw(6) << std::setfill('0') << value;
    return code.str();
}

std::string verificationCodeHash(const std::string& email, const std::string& code) {
    return hmacSha256("verify|" + email + "|" + code);
}

std::string jsonIdentifier(const nlohmann::json& value, const std::string& key) {
    if (!value.contains(key)) return {};
    if (value[key].is_string()) return value[key].get<std::string>();
    if (value[key].is_number_integer()) return std::to_string(value[key].get<long long>());
    return {};
}

std::string jsonText(const nlohmann::json& value, const std::string& key) {
    return value.contains(key) && value[key].is_string() ? value[key].get<std::string>() : std::string{};
}

bool exchangeOAuthCode(const OAuthProviderConfig& provider, const std::string& code,
                       OAuthProfile& profile, std::string& error) {
    const std::string redirectUri = callbackUrl(provider.name);
    std::string tokenBody = "client_id=" + urlEncode(provider.clientId) +
        "&client_secret=" + urlEncode(provider.clientSecret) +
        "&code=" + urlEncode(code) + "&redirect_uri=" + urlEncode(redirectUri) +
        "&grant_type=authorization_code";
    std::string tokenResponse;
    if (!httpRequest(provider.tokenUrl, tokenBody, {}, tokenResponse, error)) return false;

    nlohmann::json tokenJson;
    try { tokenJson = nlohmann::json::parse(tokenResponse); }
    catch (...) { error = "The identity provider returned an invalid token response."; return false; }
    const std::string accessToken = tokenJson.value("access_token", "");
    if (accessToken.empty()) { error = "The identity provider did not return an access token."; return false; }

    std::string userResponse;
    if (!httpRequest(provider.userUrl, {}, accessToken, userResponse, error)) return false;
    nlohmann::json user;
    try { user = nlohmann::json::parse(userResponse); }
    catch (...) { error = "The identity provider returned an invalid profile."; return false; }

    profile.subject = provider.name == "google" ? jsonText(user, "sub") : jsonIdentifier(user, "id");
    profile.displayName = jsonText(user, "name");
    if (profile.displayName.empty()) profile.displayName = jsonText(user, "login");
    if (profile.displayName.empty()) profile.displayName = jsonText(user, "username");
    profile.avatarUrl = jsonText(user, "picture");
    if (profile.avatarUrl.empty()) profile.avatarUrl = jsonText(user, "avatar_url");

    if (provider.name == "google") {
        if (!user.value("email_verified", false)) { error = "Google did not provide a verified email."; return false; }
        profile.email = jsonText(user, "email");
    } else if (provider.name == "discord") {
        if (!user.value("verified", false)) { error = "Discord did not provide a verified email."; return false; }
        profile.email = jsonText(user, "email");
        if (profile.avatarUrl.empty() && user.contains("avatar") && user["avatar"].is_string())
            profile.avatarUrl = "https://cdn.discordapp.com/avatars/" + profile.subject + "/" + user["avatar"].get<std::string>() + ".png";
    } else if (provider.name == "github") {
        std::string emailsResponse;
        if (!httpRequest("https://api.github.com/user/emails", {}, accessToken, emailsResponse, error)) return false;
        try {
            const auto emails = nlohmann::json::parse(emailsResponse);
            for (const auto& candidate : emails) {
                if (candidate.value("verified", false) && candidate.value("primary", false)) {
                    profile.email = candidate.value("email", "");
                    break;
                }
            }
            if (profile.email.empty()) {
                for (const auto& candidate : emails) {
                    if (candidate.value("verified", false)) { profile.email = candidate.value("email", ""); break; }
                }
            }
        } catch (...) { error = "GitHub returned an invalid email list."; return false; }
    }

    std::transform(profile.email.begin(), profile.email.end(), profile.email.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (profile.subject.empty() || profile.email.find('@') == std::string::npos) {
        error = "The identity provider did not return a usable verified email.";
        return false;
    }
    return true;
}

HttpResponsePtr redirectResponse(const std::string& location) {
    auto response = HttpResponse::newHttpResponse();
    response->setStatusCode(k302Found);
    response->addHeader("Location", location);
    return response;
}

bool authenticate(const HttpRequestPtr& request, std::string& email) {
    std::string authorization = request->getHeader("Authorization");
    constexpr const char* prefix = "Bearer ";
    if (authorization.rfind(prefix, 0) != 0) return false;
    const std::string token = authorization.substr(std::char_traits<char>::length(prefix));
    const auto separator = token.find('.');
    if (separator == std::string::npos) return false;
    const std::string encodedPayload = token.substr(0, separator);
    const std::string suppliedSignature = token.substr(separator + 1);
    const std::string expectedSignature = hmacSha256(encodedPayload);
    if (suppliedSignature.size() != expectedSignature.size() ||
        CRYPTO_memcmp(suppliedSignature.data(), expectedSignature.data(), expectedSignature.size()) != 0)
        return false;

    const std::string payload = base64Decode(encodedPayload);
    const auto first = payload.find('|');
    const auto second = payload.find('|', first == std::string::npos ? first : first + 1);
    if (first == std::string::npos || second == std::string::npos) return false;
    email = payload.substr(0, first);
    const std::string role = payload.substr(first + 1, second - first - 1);
    if (role != "webpanel") return false;
    try {
        const auto expiry = std::stoll(payload.substr(second + 1));
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        if (expiry <= now) return false;
    } catch (...) {
        return false;
    }
    std::transform(email.begin(), email.end(), email.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return email.find('@') != std::string::npos;
}

bool permissionEnabled(const ServerMemberRecord& member, const std::string& permission) {
    try {
        const auto permissions = nlohmann::json::parse(member.permissions_json);
        return permissions.value(permission, false);
    } catch (...) {
        return false;
    }
}

bool authorizedMember(const HttpRequestPtr& request, const std::string& uuid,
                      const std::string& permission, std::string& email,
                      ServerMemberRecord& member, HttpResponsePtr& failure) {
    if (!authenticate(request, email)) {
        failure = errorResponse("Your webpanel session is invalid or expired.", k401Unauthorized);
        return false;
    }
    if (!Database::getInstance().getServerMember(uuid, email, member) || member.status != "active") {
        failure = errorResponse("You do not have active access to this server.", k403Forbidden);
        return false;
    }
    if (!permissionEnabled(member, permission)) {
        failure = errorResponse("Your server permissions do not allow this action.", k403Forbidden);
        return false;
    }
    Database::getInstance().touchMemberLastSeen(uuid, email);
    return true;
}

nlohmann::json publicServerJson(const ServerRecord& server, const ServerMemberRecord& member) {
    const bool running = ProcessManager::getInstance().isServerRunning(server.uuid);
    nlohmann::json output = {
        {"uuid", server.uuid}, {"name", server.name},
        {"software_type", server.software_type}, {"version", server.version},
        {"port", server.port}, {"ram_min", server.ram_min}, {"ram_max", server.ram_max},
        {"status", running ? "Online" : "Offline"}, {"subdomain", server.subdomain},
        {"public_url", server.subdomain.empty() ? "" : "https://" + server.subdomain + ".mcdeploy.online"},
        {"role", member.role}, {"app_connected", true}
    };
    try { output["permissions"] = nlohmann::json::parse(member.permissions_json); }
    catch (...) { output["permissions"] = nlohmann::json::object(); }
    return output;
}

bool dangerousCommand(std::string command) {
    std::transform(command.begin(), command.end(), command.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    while (!command.empty() && (command.front() == '/' || std::isspace(static_cast<unsigned char>(command.front()))))
        command.erase(command.begin());
    const auto space = command.find(' ');
    const std::string verb = command.substr(0, space);
    static const std::vector<std::string> dangerous = {
        "stop", "restart", "op", "deop", "ban", "ban-ip", "pardon", "whitelist", "save-off"
    };
    return std::find(dangerous.begin(), dangerous.end(), verb) != dangerous.end();
}

} // namespace

void WebPanelController::status(const HttpRequestPtr&,
                                std::function<void(const HttpResponsePtr&)>&& callback) {
    nlohmann::json providers = nlohmann::json::array();
    for (const std::string& name : {"google", "github", "discord"}) {
        if (providerConfigured(providerConfig(name))) providers.push_back(name);
    }
    callback(jsonResponse({{"status", "success"}, {"app_connected", true},
                           {"api_version", 3}, {"oauth_providers", providers},
                           {"email_verification", !configuredEmailValue("resend_api_key", "MCDEPLOY_RESEND_API_KEY").empty()}}));
}

void WebPanelController::registerAccount(const HttpRequestPtr& request,
                                         std::function<void(const HttpResponsePtr&)>&& callback) {
    const auto body = request->getJsonObject();
    if (!body) return callback(errorResponse("Request body must be JSON.", k400BadRequest));
    std::string email = body->get("email", "").asString();
    const std::string password = body->get("password", "").asString();
    const std::string displayName = body->get("display_name", "").asString().substr(0, 80);
    std::transform(email.begin(), email.end(), email.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool invalidEmail = email.size() > 254 || email.find('@') == std::string::npos ||
        email.find('.') == std::string::npos ||
        std::any_of(email.begin(), email.end(), [](unsigned char c) { return std::isspace(c); });
    if (invalidEmail) return callback(errorResponse("Enter a valid email address.", k400BadRequest));
    if (password.size() < 10 || password.size() > 128)
        return callback(errorResponse("Password must be between 10 and 128 characters.", k400BadRequest));
    if (Database::getInstance().userExists(email))
        return callback(errorResponse("An account already exists for this email.", k409Conflict));

    const std::string code = secureVerificationCode();
    if (code.empty()) return callback(errorResponse("A verification code could not be generated.", k500InternalServerError));
    if (!Database::getInstance().createPendingRegistration(
            email, password, displayName, verificationCodeHash(email, code)))
        return callback(errorResponse("Please wait before requesting another verification email.", k429TooManyRequests));

    std::string sendError;
    if (!sendVerificationEmail(email, code, sendError)) {
        Database::getInstance().deletePendingRegistration(email);
        return callback(errorResponse(sendError, k502BadGateway));
    }
    Database::getInstance().logAction(email, "WEBPANEL_VERIFICATION_SENT", "", "Manual signup verification requested");
    callback(jsonResponse({{"status", "pending_verification"}, {"email", email},
                           {"message", "A six-digit verification code was sent to your email."},
                           {"expires_in", 600}}, k202Accepted));
}

void WebPanelController::verifyRegistration(const HttpRequestPtr& request,
                                            std::function<void(const HttpResponsePtr&)>&& callback) {
    const auto body = request->getJsonObject();
    if (!body) return callback(errorResponse("Request body must be JSON.", k400BadRequest));
    std::string email = body->get("email", "").asString();
    const std::string code = body->get("code", "").asString();
    std::transform(email.begin(), email.end(), email.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (code.size() != 6 || !std::all_of(code.begin(), code.end(), [](unsigned char c) { return std::isdigit(c); }))
        return callback(errorResponse("Enter the six-digit verification code.", k400BadRequest));
    if (!Database::getInstance().completePendingRegistration(email, verificationCodeHash(email, code)))
        return callback(errorResponse("The verification code is invalid, expired, or has too many attempts.", k400BadRequest));
    Database::getInstance().logAction(email, "WEBPANEL_REGISTER", "", "Email verified and account created");
    callback(jsonResponse({{"status", "success"}, {"token", generateToken(email)},
                           {"email", email}, {"expires_in", 86400}}, k201Created));
}

void WebPanelController::resendVerification(const HttpRequestPtr& request,
                                            std::function<void(const HttpResponsePtr&)>&& callback) {
    const auto body = request->getJsonObject();
    if (!body) return callback(errorResponse("Request body must be JSON.", k400BadRequest));
    std::string email = body->get("email", "").asString();
    std::transform(email.begin(), email.end(), email.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string code = secureVerificationCode();
    if (Database::getInstance().pendingRegistrationExists(email) && !code.empty() &&
        Database::getInstance().rotatePendingVerification(email, verificationCodeHash(email, code))) {
        std::string ignored;
        sendVerificationEmail(email, code, ignored);
    }
    callback(jsonResponse({{"status", "success"},
                           {"message", "If a pending signup exists, a new code has been sent."}}, k202Accepted));
}

void WebPanelController::login(const HttpRequestPtr& request,
                               std::function<void(const HttpResponsePtr&)>&& callback) {
    const auto body = request->getJsonObject();
    if (!body) return callback(errorResponse("Request body must be JSON.", k400BadRequest));
    std::string email = body->get("email", "").asString();
    const std::string password = body->get("password", "").asString();
    std::transform(email.begin(), email.end(), email.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    UserRecord user;
    if (email.find('@') == std::string::npos ||
        !Database::getInstance().verifyUser(email, password, user) || user.role != "webpanel")
        return callback(errorResponse("Invalid email or password.", k401Unauthorized));
    callback(jsonResponse({{"status", "success"}, {"token", generateToken(email)},
                           {"email", email}, {"expires_in", 86400}}));
}

void WebPanelController::oauthStart(const HttpRequestPtr&,
                                    std::function<void(const HttpResponsePtr&)>&& callback,
                                    std::string provider) {
    std::transform(provider.begin(), provider.end(), provider.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto config = providerConfig(provider);
    if (config.name.empty())
        return callback(errorResponse("Unsupported OAuth provider.", k404NotFound));
    if (!providerConfigured(config))
        return callback(errorResponse("This OAuth provider is not configured.", k503ServiceUnavailable));
    const std::string authorization = config.authorizationUrl +
        "?client_id=" + urlEncode(config.clientId) +
        "&redirect_uri=" + urlEncode(callbackUrl(provider)) +
        "&response_type=code&scope=" + urlEncode(config.scope) +
        "&state=" + urlEncode(makeOAuthState(provider));
    callback(redirectResponse(authorization));
}

void WebPanelController::oauthCallback(const HttpRequestPtr& request,
                                       std::function<void(const HttpResponsePtr&)>&& callback,
                                       std::string provider) {
    std::transform(provider.begin(), provider.end(), provider.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto fail = [&](const std::string& message) {
        callback(redirectResponse(webpanelUrl() + "/#/login?oauth_error=" + urlEncode(message)));
    };
    const auto config = providerConfig(provider);
    if (!providerConfigured(config)) return fail("This OAuth provider is not configured.");
    const std::string state = request->getParameter("state");
    const std::string code = request->getParameter("code");
    if (!validateOAuthState(state, provider)) return fail("The OAuth request expired or was invalid.");
    if (code.empty()) return fail("The identity provider did not authorize sign-in.");

    OAuthProfile profile;
    std::string error;
    if (!exchangeOAuthCode(config, code, profile, error)) return fail(error);

    std::string linkedEmail;
    if (Database::getInstance().getOAuthIdentityEmail(provider, profile.subject, linkedEmail) &&
        linkedEmail != profile.email)
        return fail("This provider identity is already linked to another email.");

    UserRecord existing;
    if (Database::getInstance().getUser(profile.email, existing)) {
        if (existing.role != "webpanel") return fail("This email belongs to a privileged local account.");
    } else {
        const std::string unusablePassword = randomToken(48);
        if (unusablePassword.empty() ||
            !Database::getInstance().createUser(profile.email, unusablePassword, "webpanel"))
            return fail("The MCDeploy account could not be created.");
    }
    if (!Database::getInstance().linkOAuthIdentity(provider, profile.subject, profile.email,
                                                     profile.displayName, profile.avatarUrl))
        return fail("The provider identity could not be linked safely.");

    const std::string exchangeCode = randomToken(32);
    if (exchangeCode.empty() ||
        !Database::getInstance().createOAuthLoginCode(sha256Hex(exchangeCode), profile.email))
        return fail("A sign-in exchange could not be created.");
    Database::getInstance().logAction(profile.email, "WEBPANEL_OAUTH_LOGIN", "", provider);
    callback(redirectResponse(webpanelUrl() + "/#/login?oauth_code=" + urlEncode(exchangeCode) +
                              "&provider=" + urlEncode(provider)));
}

void WebPanelController::oauthExchange(const HttpRequestPtr& request,
                                       std::function<void(const HttpResponsePtr&)>&& callback) {
    const auto body = request->getJsonObject();
    if (!body) return callback(errorResponse("Request body must be JSON.", k400BadRequest));
    const std::string code = body->get("code", "").asString();
    std::string email;
    if (code.size() < 32 || !Database::getInstance().consumeOAuthLoginCode(sha256Hex(code), email))
        return callback(errorResponse("This OAuth sign-in code is invalid or expired.", k401Unauthorized));
    callback(jsonResponse({{"status", "success"}, {"token", generateToken(email)},
                           {"email", email}, {"expires_in", 86400}}));
}

void WebPanelController::session(const HttpRequestPtr& request,
                                 std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string email;
    if (!authenticate(request, email))
        return callback(errorResponse("Your webpanel session is invalid or expired.", k401Unauthorized));
    const auto memberships = Database::getInstance().listServersForEmail(email);
    const auto activeCount = std::count_if(memberships.begin(), memberships.end(), [](const auto& member) {
        return member.status == "active" && permissionEnabled(member, "server.view");
    });
    callback(jsonResponse({{"status", "success"}, {"email", email}, {"active_servers", activeCount}}));
}

void WebPanelController::servers(const HttpRequestPtr& request,
                                 std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string email;
    if (!authenticate(request, email))
        return callback(errorResponse("Your webpanel session is invalid or expired.", k401Unauthorized));
    nlohmann::json serversJson = nlohmann::json::array();
    for (const auto& member : Database::getInstance().listServersForEmail(email)) {
        if (member.status != "active" || !permissionEnabled(member, "server.view")) continue;
        ServerRecord server;
        if (Database::getInstance().getServer(member.server_uuid, server))
            serversJson.push_back(publicServerJson(server, member));
    }
    callback(jsonResponse({{"status", "success"}, {"email", email},
                           {"app_connected", true}, {"servers", serversJson}}));
}

void WebPanelController::overview(const HttpRequestPtr& request,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "server.view", email, member, failure))
        return callback(failure);
    ServerRecord server;
    if (!Database::getInstance().getServer(uuid, server))
        return callback(errorResponse("Server not found.", k404NotFound));
    nlohmann::json output = {{"status", "success"}, {"app_connected", true},
                             {"server", publicServerJson(server, member)}};
    if (permissionEnabled(member, "metrics.view")) {
        const auto metrics = SystemInfo::getInstance().getMetrics(server.directory_path);
        output["host_metrics"] = {
            {"cpu_usage", metrics.cpu_usage_pct}, {"ram_total_gb", metrics.ram_total_gb},
            {"ram_used_gb", metrics.ram_used_gb}, {"disk_total_gb", metrics.disk_total_gb},
            {"disk_used_gb", metrics.disk_used_gb},
            {"world_size_gb", SystemInfo::getWorldSizeGB(server.directory_path)}
        };
    }
    callback(jsonResponse(output));
}

void WebPanelController::control(const HttpRequestPtr& request,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 std::string uuid) {
    const auto body = request->getJsonObject();
    if (!body) return callback(errorResponse("Request body must be JSON.", k400BadRequest));
    const std::string action = body->get("action", "").asString();
    const std::string requiredPermission = action == "start" ? "server.start" :
        action == "stop" ? "server.stop" : action == "restart" ? "server.restart" :
        action == "kill" ? "server.kill" : "";
    if (requiredPermission.empty())
        return callback(errorResponse("Unknown server action.", k400BadRequest));
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, requiredPermission, email, member, failure))
        return callback(failure);

    LifecycleResult result;
    if (action == "start") result = ServerLifecycle::start(uuid, email);
    else if (action == "restart") result = ServerLifecycle::restart(uuid, email);
    else result = ServerLifecycle::stop(uuid, email, action == "kill");
    callback(jsonResponse({{"status", result.ok ? "success" : "error"},
                           {"message", result.message}},
                          result.ok ? k200OK : k409Conflict));
}

void WebPanelController::logs(const HttpRequestPtr& request,
                              std::function<void(const HttpResponsePtr&)>&& callback,
                              std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "console.view", email, member, failure))
        return callback(failure);
    nlohmann::json logsJson = nlohmann::json::array();
    for (const auto& line : ProcessManager::getInstance().getLogs(uuid, 200)) {
        logsJson.push_back({{"text", line.text}, {"type", line.type},
                            {"timestamp", line.timestamp}, {"sequence", line.sequence}});
    }
    callback(jsonResponse({{"status", "success"}, {"logs", logsJson}}));
}

void WebPanelController::command(const HttpRequestPtr& request,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "console.send", email, member, failure))
        return callback(failure);
    const auto body = request->getJsonObject();
    if (!body) return callback(errorResponse("Request body must be JSON.", k400BadRequest));
    const std::string value = body->get("command", "").asString();
    if (value.empty()) return callback(errorResponse("Command cannot be empty.", k400BadRequest));
    if (dangerousCommand(value) && !permissionEnabled(member, "console.dangerous"))
        return callback(errorResponse("This command requires the console.dangerous permission.", k403Forbidden));
    if (!ProcessManager::getInstance().sendCommand(uuid, value))
        return callback(errorResponse("The server process is not running.", k409Conflict));
    Database::getInstance().logAction(email, "WEBPANEL_COMMAND", uuid, value);
    callback(jsonResponse({{"status", "success"}, {"message", "Command sent."}}));
}

void WebPanelController::backups(const HttpRequestPtr& request,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "backups.view", email, member, failure))
        return callback(failure);
    nlohmann::json backupsJson = nlohmann::json::array();
    for (const auto& backup : Database::getInstance().getServerBackups(uuid)) {
        backupsJson.push_back({{"uuid", backup.backup_uuid}, {"file_name", backup.file_name},
                               {"file_size", backup.file_size}, {"created_at", backup.created_at}});
    }
    callback(jsonResponse({{"status", "success"}, {"backups", backupsJson}}));
}

void WebPanelController::createBackup(const HttpRequestPtr& request,
                                      std::function<void(const HttpResponsePtr&)>&& callback,
                                      std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "backups.create", email, member, failure))
        return callback(failure);
    std::string backupsRoot = environmentValue("MCDEPLOY_BACKUPS_DIR");
    if (backupsRoot.empty()) {
        const auto& root = applicationConfig();
        backupsRoot = root.contains("mcdeploy") ? root["mcdeploy"].value("backups_dir", "backups") : "backups";
    }
    const auto result = ServerLifecycle::createBackup(uuid, email, backupsRoot);
    callback(jsonResponse({{"status", result.ok ? "success" : "error"},
                           {"message", result.message}},
                          result.ok ? k200OK : k500InternalServerError));
}

namespace {

bool loadAuthorizedServer(const HttpRequestPtr& request, const std::string& uuid,
                          const std::string& permission, std::string& email,
                          ServerMemberRecord& member, ServerRecord& server,
                          HttpResponsePtr& failure) {
    if (!authorizedMember(request, uuid, permission, email, member, failure)) return false;
    if (!Database::getInstance().getServer(uuid, server)) {
        failure = errorResponse("Server not found.", k404NotFound);
        return false;
    }
    return true;
}

bool resolveServerPath(const ServerRecord& server, const std::string& relative,
                       std::filesystem::path& resolved, std::string& error) {
    namespace fs = std::filesystem;
    fs::path requested(relative.empty() ? "." : relative);
    if (requested.is_absolute() || requested.has_root_name()) {
        error = "Absolute paths are not allowed.";
        return false;
    }
    try {
        const fs::path base = fs::weakly_canonical(server.directory_path);
        resolved = fs::weakly_canonical(base / requested);
        auto normalize = [](fs::path path) {
            std::string value = path.generic_string();
#ifdef _WIN32
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
            if (!value.empty() && value.back() != '/') value.push_back('/');
            return value;
        };
        if (normalize(resolved).rfind(normalize(base), 0) != 0) {
            error = "The requested path is outside the server directory.";
            return false;
        }
        return true;
    } catch (...) {
        error = "The requested path could not be resolved.";
        return false;
    }
}

bool sensitiveFile(const std::filesystem::path& path) {
    std::string name = path.filename().string();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == ".env" || name == "config.json" || name == "credentials.json") return true;
    return name.find("secret") != std::string::npos || name.find("credential") != std::string::npos ||
           name.find("private_key") != std::string::npos || name.find("token") != std::string::npos;
}

nlohmann::json automationJson(const AutomationRuleRecord& rule) {
    return {
        {"id", rule.id}, {"name", rule.name}, {"trigger_type", rule.trigger_type},
        {"threshold", rule.threshold}, {"condition_value", rule.condition_value},
        {"action_type", rule.action_type}, {"action_payload", rule.action_payload},
        {"enabled", rule.enabled != 0}, {"cooldown_seconds", rule.cooldown_seconds},
        {"last_evaluated_at", rule.last_evaluated_at},
        {"last_triggered_at", rule.last_triggered_at}, {"last_status", rule.last_status},
        {"last_output", rule.last_output}, {"created_by", rule.created_by},
        {"created_at", rule.created_at}
    };
}

nlohmann::json maintenanceJson(const MaintenanceRecord& state) {
    return {
        {"enabled", state.enabled != 0}, {"message", state.message},
        {"prevent_joins", state.prevent_joins != 0},
        {"backup_on_enable", state.backup_on_enable != 0},
        {"enabled_by", state.enabled_by}, {"enabled_at", state.enabled_at},
        {"updated_at", state.updated_at}
    };
}

nlohmann::json aiStepJson(const AiStep& step) {
    nlohmann::json value = {
        {"kind", step.kind}, {"tool", step.tool_name}, {"arguments", step.arguments},
        {"result", step.result}, {"content", step.content}, {"latency_ms", step.latency_ms}
    };
    if (step.needs_confirmation) value["needs_confirmation"] = true;
    if (!step.pending_tool_id.empty()) value["tool_call_id"] = step.pending_tool_id;
    return value;
}

bool trustedAddonDownload(const std::string& url) {
    constexpr const char* prefix = "https://";
    if (url.rfind(prefix, 0) != 0) return false;
    const auto hostStart = std::char_traits<char>::length(prefix);
    const auto hostEnd = url.find('/', hostStart);
    std::string host = url.substr(hostStart, hostEnd - hostStart);
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return host == "cdn.modrinth.com" || host == "edge.forgecdn.net" ||
           host == "mediafilez.forgecdn.net";
}

bool pluginReadAuthorized(const HttpRequestPtr& request, const std::string& uuid,
                          std::string& email, ServerMemberRecord& member,
                          HttpResponsePtr& failure) {
    if (!authenticate(request, email)) {
        failure = errorResponse("Your webpanel session is invalid or expired.", k401Unauthorized);
        return false;
    }
    if (!Database::getInstance().getServerMember(uuid, email, member) || member.status != "active") {
        failure = errorResponse("You do not have active access to this server.", k403Forbidden);
        return false;
    }
    if (!permissionEnabled(member, "plugins.view") && !permissionEnabled(member, "plugins.install")) {
        failure = errorResponse("Your server permissions do not allow this action.", k403Forbidden);
        return false;
    }
    Database::getInstance().touchMemberLastSeen(uuid, email);
    return true;
}

} // namespace

void WebPanelController::health(const HttpRequestPtr& request,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "metrics.view", email, member, server, failure))
        return callback(failure);

    const auto metrics = SystemInfo::getInstance().getMetrics(server.directory_path);
    const bool running = ProcessManager::getInstance().isServerRunning(uuid);
    nlohmann::json components = nlohmann::json::array();
    nlohmann::json recommendations = nlohmann::json::array();
    int score = 0;
    auto add = [&](const std::string& key, int value, int maximum, const std::string& evidence) {
        value = std::max(0, std::min(value, maximum));
        score += value;
        components.push_back({{"key", key}, {"score", value}, {"maximum", maximum},
            {"status", value >= maximum * 0.8 ? "good" : value >= maximum * 0.5 ? "warning" : "critical"},
            {"evidence", evidence}});
    };

    int availability = running ? 25 : 20;
    std::string availabilityEvidence = running ? "process is running" : "server is offline";
    if (server.status == "Crashed") {
        availability = 0;
        availabilityEvidence = "last known state is crashed";
        recommendations.push_back("Inspect recent crash logs before restarting.");
    } else if (!running && (server.status == "Online" || server.status == "Starting")) {
        availability = 8;
        availabilityEvidence = "database and process state disagree";
        recommendations.push_back("Refresh lifecycle state or restart the server.");
    }
    add("availability", availability, 25, availabilityEvidence);

    const double cpu = metrics.cpu_usage_pct;
    const int cpuScore = cpu < 70.0 ? 15 : cpu < 85.0 ? 10 : cpu < 95.0 ? 5 : 1;
    add("cpu", cpuScore, 15, std::to_string(static_cast<int>(cpu)) + "% host CPU used");
    if (cpuScore < 10) recommendations.push_back("Investigate sustained CPU pressure and expensive plugins or chunk generation.");

    const double ramPercent = metrics.ram_total_gb > 0.0
        ? metrics.ram_used_gb / metrics.ram_total_gb * 100.0 : 0.0;
    const int ramScore = ramPercent < 75.0 ? 15 : ramPercent < 88.0 ? 10 : ramPercent < 96.0 ? 5 : 1;
    add("memory", ramScore, 15, std::to_string(static_cast<int>(ramPercent)) + "% host RAM used");
    if (ramScore < 10) recommendations.push_back("Reduce memory pressure or adjust server heap allocation.");

    const double diskPercent = metrics.disk_total_gb > 0.0
        ? metrics.disk_free_gb / metrics.disk_total_gb * 100.0 : 100.0;
    const int diskScore = diskPercent > 20.0 ? 15 : diskPercent > 10.0 ? 10 : diskPercent > 5.0 ? 5 : 0;
    std::ostringstream diskEvidence;
    diskEvidence << std::fixed << std::setprecision(1) << metrics.disk_free_gb << " GB free";
    add("storage", diskScore, 15, diskEvidence.str());
    if (diskScore < 10) recommendations.push_back("Free disk space before creating backups or generating more chunks.");

    int errors = 0;
    int warnings = 0;
    for (const auto& line : ProcessManager::getInstance().getLogs(uuid, 150)) {
        std::string content = line.type + " " + line.text;
        std::transform(content.begin(), content.end(), content.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (content.find("error") != std::string::npos || content.find("exception") != std::string::npos) ++errors;
        else if (content.find("warn") != std::string::npos) ++warnings;
    }
    const int logScore = errors == 0 ? (warnings < 10 ? 15 : 12) : errors < 3 ? 9 : errors < 10 ? 4 : 0;
    add("logs", logScore, 15, std::to_string(errors) + " errors and " + std::to_string(warnings) + " warnings in recent output");
    if (errors > 0) recommendations.push_back("Review recent errors in the console.");

    const auto backups = Database::getInstance().getServerBackups(uuid);
    const int backupScore = backups.empty() ? 0 : 15;
    add("backups", backupScore, 15, backups.empty() ? "no backups recorded" : "latest backup: " + backups.back().created_at);
    if (backups.empty()) recommendations.push_back("Create a backup and schedule recurring backups.");

    const std::string grade = score >= 90 ? "Excellent" : score >= 75 ? "Good" :
                              score >= 55 ? "Needs attention" : "Critical";
    callback(jsonResponse({{"status", "success"}, {"score", score}, {"grade", grade},
        {"components", components}, {"recommendations", recommendations},
        {"world_size_gb", SystemInfo::getWorldSizeGB(server.directory_path)}}));
}

void WebPanelController::config(const HttpRequestPtr& request,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "config.view", email, member, server, failure))
        return callback(failure);

    nlohmann::json properties = nlohmann::json::object();
    std::ifstream input(std::filesystem::path(server.directory_path) / "server.properties");
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        std::string key = line.substr(0, separator);
        std::string value = line.substr(separator + 1);
        std::string lowered = key;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowered.find("password") != std::string::npos || lowered.find("secret") != std::string::npos ||
            lowered.find("token") != std::string::npos || lowered.rfind("rcon.", 0) == 0)
            value = "[REDACTED]";
        properties[key] = value;
    }
    callback(jsonResponse({{"status", "success"}, {"properties", properties},
        {"allocation", {{"ram_min", server.ram_min}, {"ram_max", server.ram_max}, {"port", server.port}}}}));
}

void WebPanelController::updateConfig(const HttpRequestPtr& request,
                                      std::function<void(const HttpResponsePtr&)>&& callback,
                                      std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "config.edit", email, member, server, failure))
        return callback(failure);
    nlohmann::json body;
    try { body = nlohmann::json::parse(request->getBody()); }
    catch (...) { return callback(errorResponse("Request body must be JSON.", k400BadRequest)); }
    const nlohmann::json updates = body.contains("properties") ? body["properties"] : body;
    if (!updates.is_object()) return callback(errorResponse("Properties must be a JSON object.", k400BadRequest));

    std::map<std::string, std::string> properties;
    const auto path = std::filesystem::path(server.directory_path) / "server.properties";
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        const auto separator = line.find('=');
        if (separator != std::string::npos) properties[line.substr(0, separator)] = line.substr(separator + 1);
    }
    for (const auto& [key, value] : updates.items()) {
        std::string lowered = key;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool sensitive = lowered.find("password") != std::string::npos ||
            lowered.find("secret") != std::string::npos || lowered.find("token") != std::string::npos ||
            lowered.rfind("rcon.", 0) == 0;
        if (sensitive || key.find('\n') != std::string::npos || key.find('=') != std::string::npos) continue;
        if (value.is_string()) properties[key] = value.get<std::string>();
        else if (value.is_boolean()) properties[key] = value.get<bool>() ? "true" : "false";
        else if (value.is_number()) properties[key] = value.dump();
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open()) return callback(errorResponse("server.properties could not be written.", k500InternalServerError));
    output << "# Updated by MCDeploy webpanel\n";
    for (const auto& [key, value] : properties) output << key << '=' << value << '\n';
    Database::getInstance().logAction(email, "WEBPANEL_CONFIG_UPDATE", uuid, "Updated server.properties");
    callback(jsonResponse({{"status", "success"}, {"message", "Configuration saved."}}));
}

void WebPanelController::files(const HttpRequestPtr& request,
                               std::function<void(const HttpResponsePtr&)>&& callback,
                               std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "files.view", email, member, server, failure))
        return callback(failure);

    const std::string relative = request->getParameter("path");
    std::filesystem::path directory;
    std::string pathError;
    if (!resolveServerPath(server, relative, directory, pathError))
        return callback(errorResponse(pathError, k403Forbidden));
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory))
        return callback(errorResponse("Directory not found.", k404NotFound));

    nlohmann::json entries = nlohmann::json::array();
    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            const auto relativePath = std::filesystem::relative(entry.path(), server.directory_path).generic_string();
            entries.push_back({{"name", entry.path().filename().string()}, {"path", relativePath},
                {"is_directory", entry.is_directory()},
                {"size", entry.is_regular_file() ? static_cast<long long>(entry.file_size()) : 0}});
        }
    } catch (...) {
        return callback(errorResponse("The directory could not be read.", k500InternalServerError));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        if (left["is_directory"] != right["is_directory"])
            return left["is_directory"].get<bool>() > right["is_directory"].get<bool>();
        return left["name"].get<std::string>() < right["name"].get<std::string>();
    });
    callback(jsonResponse({{"status", "success"}, {"path", relative}, {"entries", entries}}));
}

void WebPanelController::file(const HttpRequestPtr& request,
                              std::function<void(const HttpResponsePtr&)>&& callback,
                              std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "files.read", email, member, server, failure))
        return callback(failure);

    const std::string relative = request->getParameter("path");
    if (relative.empty()) return callback(errorResponse("A file path is required.", k400BadRequest));
    std::filesystem::path path;
    std::string pathError;
    if (!resolveServerPath(server, relative, path, pathError))
        return callback(errorResponse(pathError, k403Forbidden));
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path))
        return callback(errorResponse("File not found.", k404NotFound));
    if (sensitiveFile(path)) return callback(errorResponse("This sensitive file is not available in the public webpanel.", k403Forbidden));
    if (std::filesystem::file_size(path) > 512 * 1024)
        return callback(errorResponse("Files larger than 512 KB cannot be previewed.", k400BadRequest));

    std::ifstream input(path, std::ios::binary);
    std::ostringstream content;
    content << input.rdbuf();
    std::string text = content.str();
    if (text.find('\0') != std::string::npos)
        return callback(jsonResponse({{"status", "success"}, {"path", relative}, {"is_binary", true}, {"content", ""}}));
    callback(jsonResponse({{"status", "success"}, {"path", relative}, {"is_binary", false}, {"content", text}}));
}

void WebPanelController::saveFile(const HttpRequestPtr& request,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "files.edit", email, member, server, failure))
        return callback(failure);
    nlohmann::json body;
    try { body = nlohmann::json::parse(request->getBody()); }
    catch (...) { return callback(errorResponse("Request body must be JSON.", k400BadRequest)); }
    const std::string relative = body.value("path", "");
    const std::string content = body.value("content", "");
    if (relative.empty() || content.size() > 1024 * 1024)
        return callback(errorResponse("A path is required and content must be at most 1 MB.", k400BadRequest));
    std::filesystem::path path;
    std::string pathError;
    if (!resolveServerPath(server, relative, path, pathError))
        return callback(errorResponse(pathError, k403Forbidden));
    if (sensitiveFile(path))
        return callback(errorResponse("This sensitive file cannot be edited from the public webpanel.", k403Forbidden));
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) return callback(errorResponse("The file could not be written.", k500InternalServerError));
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    Database::getInstance().logAction(email, "WEBPANEL_FILE_SAVE", uuid, relative);
    callback(jsonResponse({{"status", "success"}, {"message", "File saved."}}));
}

void WebPanelController::players(const HttpRequestPtr& request,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "players.view", email, member, failure))
        return callback(failure);
    nlohmann::json playersJson = nlohmann::json::array();
    for (const auto& player : Database::getInstance().getServerPlayers(uuid)) {
        playersJson.push_back({{"uuid", player.uuid}, {"username", player.username},
            {"is_online", player.is_online != 0}, {"health", player.health},
            {"hunger", player.hunger}, {"frozen", player.frozen != 0},
            {"last_login", {{"x", player.last_login_x}, {"y", player.last_login_y}, {"z", player.last_login_z}}},
            {"last_logoff", {{"x", player.last_logoff_x}, {"y", player.last_logoff_y}, {"z", player.last_logoff_z}}}});
    }
    callback(jsonResponse({{"status", "success"}, {"players", playersJson}}));
}

void WebPanelController::analytics(const HttpRequestPtr& request,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "analytics.view", email, member, failure))
        return callback(failure);
    int days = 30;
    try { if (!request->getParameter("days").empty()) days = std::stoi(request->getParameter("days")); }
    catch (...) { return callback(errorResponse("Days must be a number.", k400BadRequest)); }
    days = std::clamp(days, 1, 365);
    callback(jsonResponse({{"status", "success"},
        {"summary", Database::getInstance().getAnalyticsSummary(uuid, days)},
        {"hourly", Database::getInstance().getAnalyticsHourly(uuid, days)},
        {"daily", Database::getInstance().getAnalyticsDaily(uuid, days)},
        {"leaderboard", Database::getInstance().getAnalyticsLeaderboard(uuid, days, 10)}}));
}

void WebPanelController::schedule(const HttpRequestPtr& request,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "schedule.view", email, member, failure))
        return callback(failure);
    nlohmann::json tasks = nlohmann::json::array();
    for (const auto& task : Database::getInstance().listScheduledTasks(uuid)) {
        tasks.push_back({{"id", task.id}, {"name", task.name}, {"action_type", task.action_type},
            {"payload", task.payload}, {"schedule_kind", task.schedule_kind},
            {"schedule_value", task.schedule_value}, {"enabled", task.enabled != 0},
            {"next_run_at", task.next_run_at}, {"last_run_at", task.last_run_at},
            {"last_status", task.last_status}, {"last_output", task.last_output}});
    }
    callback(jsonResponse({{"status", "success"}, {"tasks", tasks}}));
}

void WebPanelController::performance(const HttpRequestPtr& request,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "metrics.view", email, member, failure))
        return callback(failure);
    callback(jsonResponse({{"status", "success"},
        {"cpu_priority", ProcessManager::getInstance().getServerCpuPriority(uuid)},
        {"smart_optimization", ProcessManager::getInstance().getServerSmartOptimization(uuid)},
        {"is_running", ProcessManager::getInstance().isServerRunning(uuid)}}));
}

void WebPanelController::updatePerformance(const HttpRequestPtr& request,
                                           std::function<void(const HttpResponsePtr&)>&& callback,
                                           std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "config.performance", email, member, failure))
        return callback(failure);
    const auto body = request->getJsonObject();
    if (!body) return callback(errorResponse("Request body must be JSON.", k400BadRequest));
    if (body->isMember("smart_optimization"))
        ProcessManager::getInstance().setServerSmartOptimization(uuid, body->get("smart_optimization", false).asBool());
    if (body->isMember("cpu_priority")) {
        const std::string priority = body->get("cpu_priority", "normal").asString();
        static const std::vector<std::string> allowed = {"low", "below_normal", "normal", "above_normal", "high"};
        if (std::find(allowed.begin(), allowed.end(), priority) == allowed.end())
            return callback(errorResponse("Invalid CPU priority.", k400BadRequest));
        ProcessManager::getInstance().setServerCpuPriority(uuid, priority);
    }
    Database::getInstance().logAction(email, "WEBPANEL_PERFORMANCE_UPDATE", uuid, "Updated performance settings");
    callback(jsonResponse({{"status", "success"}, {"message", "Performance settings updated."}}));
}

void WebPanelController::automation(const HttpRequestPtr& request,
                                    std::function<void(const HttpResponsePtr&)>&& callback,
                                    std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "automation.view", email, member, failure))
        return callback(failure);
    nlohmann::json rules = nlohmann::json::array();
    for (const auto& rule : Database::getInstance().listAutomationRules(uuid)) rules.push_back(automationJson(rule));
    callback(jsonResponse({{"status", "success"}, {"rules", rules}}));
}

void WebPanelController::maintenance(const HttpRequestPtr& request,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "maintenance.view", email, member, failure))
        return callback(failure);
    MaintenanceRecord state;
    if (!Database::getInstance().getMaintenance(uuid, state)) state.server_uuid = uuid;
    callback(jsonResponse({{"status", "success"}, {"maintenance", maintenanceJson(state)}}));
}

void WebPanelController::plugins(const HttpRequestPtr& request,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "plugins.view", email, member, server, failure))
        return callback(failure);
    std::string software = server.software_type;
    std::transform(software.begin(), software.end(), software.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string kind = software == "paper" || software == "purpur" || software == "spigot" ? "plugins" : "mods";
    const auto directory = std::filesystem::path(server.directory_path) / kind;
    nlohmann::json installed = nlohmann::json::array();
    try {
        if (std::filesystem::is_directory(directory)) {
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (!entry.is_regular_file()) continue;
                installed.push_back({{"filename", entry.path().filename().string()},
                                     {"size", static_cast<long long>(entry.file_size())}});
            }
        }
    } catch (...) {
        return callback(errorResponse("The installed add-ons directory could not be read.", k500InternalServerError));
    }
    callback(jsonResponse({{"status", "success"}, {"kind", kind}, {"installed", installed}}));
}

void WebPanelController::updateMaintenance(const HttpRequestPtr& request,
                                           std::function<void(const HttpResponsePtr&)>&& callback,
                                           std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "maintenance.manage", email, member, server, failure))
        return callback(failure);
    const auto body = request->getJsonObject();
    if (!body) return callback(errorResponse("Request body must be JSON.", k400BadRequest));

    MaintenanceRecord current;
    if (!Database::getInstance().getMaintenance(uuid, current)) current.server_uuid = uuid;
    const bool desiredEnabled = body->isMember("enabled") ? body->get("enabled", false).asBool() : current.enabled != 0;
    ServerController delegate;
    auto auditedCallback = [email, uuid, callback = std::move(callback)](const HttpResponsePtr& response) mutable {
        if (response && response->statusCode() >= k200OK && response->statusCode() < k300MultipleChoices)
            Database::getInstance().logAction(email, "WEBPANEL_MAINTENANCE_UPDATE", uuid, "Maintenance state updated");
        callback(response);
    };
    if (desiredEnabled != (current.enabled != 0)) {
        if (desiredEnabled) delegate.enableMaintenance(request, std::move(auditedCallback), uuid);
        else delegate.disableMaintenance(request, std::move(auditedCallback), uuid);
    } else {
        delegate.updateMaintenance(request, std::move(auditedCallback), uuid);
    }
}

void WebPanelController::searchPlugins(const HttpRequestPtr& request,
                                       std::function<void(const HttpResponsePtr&)>&& callback,
                                       std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!pluginReadAuthorized(request, uuid, email, member, failure)) return callback(failure);
    ServerController delegate;
    delegate.searchAddons(request, std::move(callback), uuid);
}

void WebPanelController::pluginVersions(const HttpRequestPtr& request,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        std::string uuid, std::string addon) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!pluginReadAuthorized(request, uuid, email, member, failure)) return callback(failure);
    if (addon.empty()) return callback(errorResponse("An add-on ID is required.", k400BadRequest));
    ServerController delegate;
    delegate.getAddonVersions(request, std::move(callback), uuid, addon);
}

void WebPanelController::installPlugin(const HttpRequestPtr& request,
                                       std::function<void(const HttpResponsePtr&)>&& callback,
                                       std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "plugins.install", email, member, server, failure))
        return callback(failure);
    nlohmann::json body;
    try { body = nlohmann::json::parse(request->getBody()); }
    catch (...) { return callback(errorResponse("Request body must be JSON.", k400BadRequest)); }
    const std::string downloadUrl = body.value("downloadUrl", "");
    const std::string requestedName = body.value("filename", "");
    const std::string addonName = body.value("addonName", requestedName);
    if (!trustedAddonDownload(downloadUrl))
        return callback(errorResponse("Downloads must come from an approved Modrinth or CurseForge CDN.", k400BadRequest));
    const std::string filename = std::filesystem::path(requestedName).filename().string();
    if (filename.empty() || filename != requestedName || std::filesystem::path(filename).extension() != ".jar")
        return callback(errorResponse("A safe .jar filename is required.", k400BadRequest));
    std::string software = server.software_type;
    std::transform(software.begin(), software.end(), software.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool plugin = software == "paper" || software == "purpur" || software == "spigot";
    const auto directory = std::filesystem::path(server.directory_path) / (plugin ? "plugins" : "mods");
    try { std::filesystem::create_directories(directory); }
    catch (...) { return callback(errorResponse("The add-on directory could not be created.", k500InternalServerError)); }
    if (!VersionFetcher::getInstance().downloadFile(downloadUrl, (directory / filename).string()))
        return callback(errorResponse("The add-on download failed.", k502BadGateway));
    Database::getInstance().logAction(email, "WEBPANEL_ADDON_INSTALL", uuid, addonName + " (" + filename + ")");
    callback(jsonResponse({{"status", "success"}, {"message", "Installed " + filename + "."}}));
}

void WebPanelController::uninstallPlugin(const HttpRequestPtr& request,
                                         std::function<void(const HttpResponsePtr&)>&& callback,
                                         std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "plugins.uninstall", email, member, server, failure))
        return callback(failure);
    const std::string requestedName = request->getParameter("filename");
    const std::string filename = std::filesystem::path(requestedName).filename().string();
    if (filename.empty() || filename != requestedName)
        return callback(errorResponse("A safe filename is required.", k400BadRequest));
    std::string software = server.software_type;
    std::transform(software.begin(), software.end(), software.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool plugin = software == "paper" || software == "purpur" || software == "spigot";
    const auto target = std::filesystem::path(server.directory_path) / (plugin ? "plugins" : "mods") / filename;
    try {
        if (!std::filesystem::is_regular_file(target))
            return callback(errorResponse("The installed add-on was not found.", k404NotFound));
        if (!std::filesystem::remove(target))
            return callback(errorResponse("The installed add-on could not be removed.", k500InternalServerError));
    } catch (...) {
        return callback(errorResponse("The installed add-on could not be removed.", k500InternalServerError));
    }
    Database::getInstance().logAction(email, "WEBPANEL_ADDON_UNINSTALL", uuid, filename);
    callback(jsonResponse({{"status", "success"}, {"message", "Removed " + filename + "."}}));
}

void WebPanelController::aiConversation(const HttpRequestPtr& request,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "ai.use", email, member, failure)) return callback(failure);
    nlohmann::json conversation = nlohmann::json::array();
    for (const auto& row : Database::getInstance().getAiConversation(uuid, email, 300)) {
        nlohmann::json value = {{"id", row.id}, {"role", row.role}, {"content", row.content},
                                {"created_at", row.created_at}};
        if (!row.tool_calls.empty()) {
            try { value["tool_calls"] = nlohmann::json::parse(row.tool_calls); } catch (...) {}
        }
        if (!row.tool_call_id.empty()) value["tool_call_id"] = row.tool_call_id;
        if (!row.tool_name.empty()) value["tool_name"] = row.tool_name;
        conversation.push_back(value);
    }
    callback(jsonResponse({{"status", "success"}, {"conversation", conversation}}));
}

void WebPanelController::clearAiConversation(const HttpRequestPtr& request,
                                             std::function<void(const HttpResponsePtr&)>&& callback,
                                             std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "ai.use", email, member, failure)) return callback(failure);
    const bool ok = Database::getInstance().clearAiConversation(uuid, email);
    Database::getInstance().logAction(email, "WEBPANEL_AI_CLEAR", uuid, "Conversation cleared");
    callback(jsonResponse({{"status", ok ? "success" : "error"},
                           {"message", ok ? "Conversation cleared." : "Conversation could not be cleared."}},
                          ok ? k200OK : k500InternalServerError));
}

void WebPanelController::aiChat(const HttpRequestPtr& request,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "ai.use", email, member, server, failure)) return callback(failure);
    const auto body = request->getJsonObject();
    if (!body) return callback(errorResponse("Request body must be JSON.", k400BadRequest));
    const std::string message = body->get("message", "").asString();
    const bool agentMode = body->get("agent_mode", false).asBool();
    if (message.empty() || message.size() > 12000)
        return callback(errorResponse("A message between 1 and 12000 characters is required.", k400BadRequest));
    if (agentMode && !permissionEnabled(member, "ai.agent_mode"))
        return callback(errorResponse("Agent Mode requires the ai.agent_mode permission.", k403Forbidden));
    if (!Database::getInstance().aiRateLimitAllow(email, 60, 500))
        return callback(errorResponse("AI rate limit exceeded (60/minute or 500/day).", k429TooManyRequests));

    auto responseCallback = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    std::thread([uuid, email, agentMode, message, serverPath = server.directory_path, responseCallback]() {
        try {
            AIAgent agent(uuid, serverPath, email, agentMode, loadAiConfig());
            const auto turn = agent.runTurn(message);
            nlohmann::json steps = nlohmann::json::array();
            for (const auto& step : turn.steps) steps.push_back(aiStepJson(step));
            nlohmann::json response = {{"status", turn.ok ? "success" : "error"},
                {"message", turn.final_message}, {"steps", steps}, {"suggestions", turn.suggestions},
                {"agent_mode", agentMode},
                {"tokens", {{"prompt", turn.tokens_prompt}, {"completion", turn.tokens_completion}}}};
            if (!turn.error.empty()) response["error"] = turn.error;
            if (!turn.pending_actions.empty()) response["pending_actions"] = turn.pending_actions;
            Database::getInstance().logAction(email, "WEBPANEL_AI_CHAT", uuid,
                std::string("agent=") + (agentMode ? "on" : "off") + " msg=" + message.substr(0, 100));
            (*responseCallback)(jsonResponse(response, turn.ok ? k200OK : k500InternalServerError));
        } catch (const std::exception& error) {
            (*responseCallback)(errorResponse(std::string("AI request failed: ") + error.what(), k500InternalServerError));
        }
    }).detach();
}

void WebPanelController::aiApprove(const HttpRequestPtr& request,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "ai.approve", email, member, server, failure)) return callback(failure);
    nlohmann::json body;
    try { body = nlohmann::json::parse(request->getBody()); }
    catch (...) { return callback(errorResponse("Request body must be JSON.", k400BadRequest)); }
    const std::string tool = body.value("tool", "");
    if (tool.empty()) return callback(errorResponse("A tool name is required.", k400BadRequest));
    if (tool == "run_shell_command" && !permissionEnabled(member, "ai.shell"))
        return callback(errorResponse("Shell approval requires the ai.shell permission.", k403Forbidden));
    const nlohmann::json arguments = body.value("arguments", nlohmann::json::object());
    AIAgent agent(uuid, server.directory_path, email, true, loadAiConfig());
    const auto step = agent.executeApprovedTool(tool, arguments);
    Database::getInstance().logAction(email, "WEBPANEL_AI_APPROVE", uuid, tool);
    callback(jsonResponse({{"status", step.kind == "error" ? "error" : "success"},
                           {"step", aiStepJson(step)}}));
}

void WebPanelController::aiUndo(const HttpRequestPtr& request,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    ServerRecord server;
    HttpResponsePtr failure;
    if (!loadAuthorizedServer(request, uuid, "ai.undo", email, member, server, failure)) return callback(failure);
    AIAgent agent(uuid, server.directory_path, email, true, loadAiConfig());
    const std::string message = agent.undoLast();
    const bool ok = message.rfind("Error", 0) != 0;
    Database::getInstance().logAction(email, "WEBPANEL_AI_UNDO", uuid, message);
    callback(jsonResponse({{"status", ok ? "success" : "error"}, {"message", message},
                           {"undo_stack_size", Database::getInstance().countAiUndoStack(uuid, email)}},
                          ok ? k200OK : k409Conflict));
}

void WebPanelController::audit(const HttpRequestPtr& request,
                               std::function<void(const HttpResponsePtr&)>&& callback,
                               std::string uuid) {
    std::string email;
    ServerMemberRecord member;
    HttpResponsePtr failure;
    if (!authorizedMember(request, uuid, "audit.view", email, member, failure))
        return callback(failure);
    int limit = 75;
    try {
        if (!request->getParameter("limit").empty()) limit = std::stoi(request->getParameter("limit"));
    } catch (...) {
        return callback(errorResponse("Audit limit must be a number.", k400BadRequest));
    }
    callback(jsonResponse({{"status", "success"},
                           {"entries", Database::getInstance().getServerAuditLogs(uuid, limit)}}));
}

} // namespace MCDeploy