#pragma once

// ============================================================
// MCDeploy - Server Lifecycle
// ------------------------------------------------------------
// Single source of truth for starting, stopping, restarting and
// backing up a Minecraft server, including the surrounding
// Bore TCP tunnel and Cloudflare DNS record reconciliation.
//
// Both the interactive REST controller (ServerController) and
// the background scheduler drive lifecycle through this helper,
// so scheduled restarts don't leave stale DNS records.
// ============================================================

#include <string>

namespace MCDeploy {

struct LifecycleResult {
    bool ok = false;
    std::string message; // human-readable summary for audit log / task history
};

class ServerLifecycle {
public:
    // Start the server, boot the Bore tunnel, and (re)create DNS records.
    // `actor` is stored in the audit log so we know who initiated the action
    // (e.g. "admin" for a UI click, "scheduler" for a scheduled task).
    static LifecycleResult start(const std::string& uuid, const std::string& actor);

    // Stop the server (graceful stop unless `force`), unregister the tunnel,
    // and remove DNS records.
    static LifecycleResult stop(const std::string& uuid, const std::string& actor, bool force);

    // Full stop → start cycle with DNS reconciliation.
    static LifecycleResult restart(const std::string& uuid, const std::string& actor);

    // Create a backup archive (currently placeholder-content, matching the
    // existing controller behavior) and record it in the DB.
    // `backupsRoot` is the base backups directory; per-server dir is created
    // beneath it.
    static LifecycleResult createBackup(const std::string& uuid,
                                        const std::string& actor,
                                        const std::string& backupsRoot);
};

} // namespace MCDeploy
