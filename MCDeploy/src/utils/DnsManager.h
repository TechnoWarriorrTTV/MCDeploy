#pragma once

#include <string>
#include <mutex>

namespace MCDeploy {

struct DnsRecordResult {
    bool success = false;
    std::string record_id;
    std::string error_message;
};

enum class DnsAvailabilityStatus {
    Available,
    Taken,
    Error
};

struct DnsAvailabilityResult {
    DnsAvailabilityStatus status = DnsAvailabilityStatus::Error;
    std::string error_message;
};

class DnsManager {
public:
    static DnsManager& getInstance();

    // Get the host machine's public IP address via ipify
    std::string getPublicIp();

    // Create an A record: <subdomain>.mcdeploy.online -> <ip>
    DnsRecordResult createARecord(const std::string& subdomain, const std::string& ip);

    // Create an SRV record: _minecraft._tcp.<subdomain>.mcdeploy.online -> port
    // This allows Minecraft clients to connect without specifying a port
    DnsRecordResult createSrvRecord(const std::string& subdomain, int port);

    // Create an SRV record pointing to an external target host (like bore.pub)
    DnsRecordResult createTunnelSrvRecord(const std::string& subdomain, const std::string& targetHost, int targetPort);

    // Create a CNAME record: <subdomain>.mcdeploy.online -> <target>
    DnsRecordResult createCnameRecord(const std::string& subdomain, const std::string& target);

    // Delete a DNS record by its Cloudflare record ID
    bool deleteRecord(const std::string& recordId);

    // Check the authoritative Cloudflare zone for any hostname or Minecraft SRV record.
    // Errors are distinct from "available" so callers can fail closed.
    DnsAvailabilityResult checkSubdomainAvailability(const std::string& subdomain);

    // Reserve a hostname globally by creating a persistent offline CNAME record.
    DnsRecordResult reserveSubdomain(const std::string& subdomain);

    // Switch an owned reservation between offline and an active Bore tunnel.
    bool activateReservedSubdomain(const std::string& reservationRecordId,
                                   const std::string& subdomain,
                                   const std::string& targetHost,
                                   int targetPort,
                                   std::string& outSrvRecordId);
    bool deactivateReservedSubdomain(const std::string& reservationRecordId,
                                     const std::string& subdomain,
                                     const std::string& srvRecordId);

    // Legacy boolean helper retained for existing callers.
    bool subdomainExistsInDns(const std::string& subdomain);

    // Full lifecycle: create both A + SRV records for a server
    bool createServerDnsRecords(const std::string& subdomain, int port,
                                 std::string& outARecordId, std::string& outSrvRecordId);

    // Full lifecycle: create tunnel CNAME + SRV records for a server
    bool createServerTunnelDnsRecords(const std::string& subdomain, const std::string& targetHost, int targetPort,
                                       std::string& outCnameRecordId, std::string& outSrvRecordId);

    // Full lifecycle: remove both A + SRV records for a server
    bool removeServerDnsRecords(const std::string& aRecordId, const std::string& srvRecordId);

    // Query Cloudflare and delete any records matching the subdomain name (fail-safe cleanup)
    bool forceCleanupSubdomain(const std::string& subdomain);

    // Get the full domain address for a subdomain
    std::string getFullAddress(const std::string& subdomain);

    // Check if DNS feature is available (credentials present)
    bool isEnabled() const { return m_enabled; }

private:
    DnsManager();
    ~DnsManager() = default;
    DnsManager(const DnsManager&) = delete;
    DnsManager& operator=(const DnsManager&) = delete;

    // HTTP helper for Cloudflare API calls
    std::string cloudflareApiRequest(const std::string& method, 
                                      const std::string& endpoint,
                                      const std::string& body = "");
    bool updateCnameRecord(const std::string& recordId,
                           const std::string& subdomain,
                           const std::string& target);
    std::string offlineReservationTarget() const;

    std::string m_apiToken;
    std::string m_zoneId;
    std::string m_domain;
    bool m_enabled = false;
    std::string m_cachedPublicIp;
    std::mutex m_mutex;
};

} // namespace MCDeploy
