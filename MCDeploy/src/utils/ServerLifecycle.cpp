#include "ServerLifecycle.h"

#include "ProcessManager.h"
#include "TunnelManager.h"
#include "DnsManager.h"
#include "../models/Database.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace MCDeploy {

namespace fs = std::filesystem;

// Internal helper — mirrors the UUID generator in ServerController.
static std::string generateBackupUuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis(gen);
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);
    return ss.str();
}

static std::string formatDateTime(std::time_t t) {
    std::tm bt;
#ifdef _WIN32
    localtime_s(&bt, &t);
#else
    localtime_r(&t, &bt);
#endif
    std::stringstream ss;
    ss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// ============================================================
// start
// ============================================================
LifecycleResult ServerLifecycle::start(const std::string& uuid, const std::string& actor) {
    LifecycleResult r;

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        r.message = "server not found";
        return r;
    }

    Database::getInstance().updateServerStatus(uuid, "Starting");
    r.ok = ProcessManager::getInstance().startServer(uuid, s.name, s.directory_path, s.start_command);
    if (!r.ok) {
        Database::getInstance().updateServerStatus(uuid, "Offline");
        r.message = "process failed to start";
        Database::getInstance().logAction(actor, "SERVER_CONTROL_start", uuid, r.message);
        return r;
    }

    // Bring up the Bore tunnel and activate this server's persistent hostname reservation.
    int tunnelPort = TunnelManager::getInstance().startTunnel(uuid, s.port);
    if (!s.subdomain.empty() && DnsManager::getInstance().isEnabled()) {
        std::string reservationId = s.dns_a_record_id;
        if (reservationId.empty()) {
            const auto reservation = DnsManager::getInstance().reserveSubdomain(s.subdomain);
            if (reservation.success) {
                reservationId = reservation.record_id;
                Database::getInstance().updateDnsRecordIds(uuid, reservationId, "");
            } else {
                Database::getInstance().logAction(actor, "DNS_RESERVATION_FAILED", uuid,
                    "Could not claim the globally reserved hostname " + s.subdomain);
            }
        }

        if (!reservationId.empty() && !s.dns_srv_record_id.empty()) {
            DnsManager::getInstance().deactivateReservedSubdomain(
                reservationId, s.subdomain, s.dns_srv_record_id);
            Database::getInstance().updateDnsRecordIds(uuid, reservationId, "");
        }

        if (tunnelPort != -1 && !reservationId.empty()) {
            std::string srvId;
            if (DnsManager::getInstance().activateReservedSubdomain(
                    reservationId, s.subdomain, "bore.pub", tunnelPort, srvId)) {
                Database::getInstance().updateDnsRecordIds(uuid, reservationId, srvId);
                Database::getInstance().logAction(actor, "DNS_RECORDS_CREATED", uuid,
                    "Activated reserved hostname → bore.pub:" + std::to_string(tunnelPort));
            } else {
                Database::getInstance().logAction(actor, "DNS_RECORDS_FAILED", uuid,
                    "Failed to activate reserved hostname " + s.subdomain);
            }
        } else if (tunnelPort == -1) {
            Database::getInstance().logAction(actor, "TUNNEL_FAILED", uuid,
                "Failed to start TCP tunnel via bore.pub");
        }
    }

    r.message = "started";
    Database::getInstance().logAction(actor, "SERVER_CONTROL_start", uuid, "success");
    return r;
}

// ============================================================
// stop
// ============================================================
LifecycleResult ServerLifecycle::stop(const std::string& uuid, const std::string& actor, bool force) {
    LifecycleResult r;

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        r.message = "server not found";
        return r;
    }

    // Tunnel down first so external clients disconnect cleanly.
    TunnelManager::getInstance().stopTunnel(uuid);

    // Remove only the transient SRV record and keep the CNAME reservation owned by this server.
    if (!s.subdomain.empty() && !s.dns_a_record_id.empty() && DnsManager::getInstance().isEnabled()) {
        const bool deactivated = DnsManager::getInstance().deactivateReservedSubdomain(
            s.dns_a_record_id, s.subdomain, s.dns_srv_record_id);
        if (deactivated) {
            Database::getInstance().updateDnsRecordIds(uuid, s.dns_a_record_id, "");
            Database::getInstance().logAction(actor, "DNS_RESERVATION_OFFLINE", uuid,
                "Kept the global hostname reservation while the server is offline");
        } else {
            Database::getInstance().logAction(actor, "DNS_DEACTIVATION_FAILED", uuid,
                "Could not fully deactivate the owned DNS reservation");
        }
    }

    Database::getInstance().updateServerStatus(uuid, "Offline");
    r.ok = ProcessManager::getInstance().stopServer(uuid, force);
    r.message = r.ok ? (force ? "killed" : "stopped") : "process stop failed";
    Database::getInstance().logAction(actor, force ? "SERVER_CONTROL_kill" : "SERVER_CONTROL_stop",
                                      uuid, r.message);
    return r;
}

// ============================================================
// restart
// ============================================================
LifecycleResult ServerLifecycle::restart(const std::string& uuid, const std::string& actor) {
    LifecycleResult r;

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        r.message = "server not found";
        return r;
    }

    TunnelManager::getInstance().stopTunnel(uuid);
    if (!s.subdomain.empty() && !s.dns_a_record_id.empty() && DnsManager::getInstance().isEnabled()) {
        DnsManager::getInstance().deactivateReservedSubdomain(
            s.dns_a_record_id, s.subdomain, s.dns_srv_record_id);
        Database::getInstance().updateDnsRecordIds(uuid, s.dns_a_record_id, "");
    }

    Database::getInstance().updateServerStatus(uuid, "Starting");
    ProcessManager::getInstance().stopServer(uuid, false);

    r.ok = ProcessManager::getInstance().startServer(uuid, s.name, s.directory_path, s.start_command);
    if (!r.ok) {
        r.message = "process failed to start on restart";
        Database::getInstance().updateServerStatus(uuid, "Offline");
        Database::getInstance().logAction(actor, "SERVER_CONTROL_restart", uuid, r.message);
        return r;
    }

    int tunnelPort = TunnelManager::getInstance().startTunnel(uuid, s.port);
    if (tunnelPort != -1 && !s.subdomain.empty() && !s.dns_a_record_id.empty() &&
        DnsManager::getInstance().isEnabled()) {
        std::string srvId;
        if (DnsManager::getInstance().activateReservedSubdomain(
                s.dns_a_record_id, s.subdomain, "bore.pub", tunnelPort, srvId)) {
            Database::getInstance().updateDnsRecordIds(uuid, s.dns_a_record_id, srvId);
        }
    }

    r.message = "restarted";
    Database::getInstance().logAction(actor, "SERVER_CONTROL_restart", uuid, "success");
    return r;
}

// ============================================================
// createBackup
// ============================================================
LifecycleResult ServerLifecycle::createBackup(const std::string& uuid,
                                              const std::string& actor,
                                              const std::string& backupsRoot) {
    LifecycleResult r;

    ServerRecord s;
    if (!Database::getInstance().getServer(uuid, s)) {
        r.message = "server not found";
        return r;
    }

    try {
        std::string backupsDir = backupsRoot + "/" + uuid;
        fs::create_directories(backupsDir);

        std::string backupUuid = generateBackupUuid();
        std::string fileName = "backup-" + backupUuid + ".zip";
        std::string filePath = backupsDir + "/" + fileName;

        // Matches the current controller behavior — placeholder file, real
        // archival TBD. Kept identical so scheduler and UI produce equivalent
        // records.
        std::ofstream mockZip(filePath);
        mockZip << "Mock zip contents for backup from MCDeploy (via " << actor << ")\n";
        mockZip.close();

        BackupRecord b;
        b.backup_uuid = backupUuid;
        b.server_uuid = uuid;
        b.file_name = fileName;
        b.file_path = filePath;
        b.file_size = 512;
        b.created_at = formatDateTime(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

        if (!Database::getInstance().addBackup(b)) {
            r.message = "backup DB insert failed";
            return r;
        }

        Database::getInstance().logAction(actor, "CREATE_BACKUP", uuid, "Created backup " + fileName);
        r.ok = true;
        r.message = "backup " + fileName + " created";
    } catch (const std::exception& e) {
        r.message = std::string("backup exception: ") + e.what();
    }
    return r;
}

} // namespace MCDeploy
