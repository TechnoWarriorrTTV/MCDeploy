#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

namespace MCDeploy {

struct ActiveTunnel {
    std::string uuid;
    int local_port = 0;
    int public_port = -1;
    bool is_running = false;
    
#ifdef _WIN32
    HANDLE hProcess = NULL;
    HANDLE hThread = NULL;
#else
    pid_t pid = -1;
#endif

    std::thread reader_thread;
    std::atomic<bool> stop_reader{false};
};

class TunnelManager {
public:
    static TunnelManager& getInstance();

    // Prevent copying
    TunnelManager(const TunnelManager&) = delete;
    TunnelManager& operator=(const TunnelManager&) = delete;

    /**
     * Download and verify the bore binary if it doesn't exist.
     * @return True if bore is available, false otherwise.
     */
    bool init();

    /**
     * Start a TCP tunnel from localhost:<localPort> to bore.pub.
     * @param uuid The server UUID to associate with this tunnel.
     * @param localPort The local port to expose (e.g. 25565).
     * @return The assigned public port number, or -1 on failure.
     */
    int startTunnel(const std::string& uuid, int localPort);

    /**
     * Stop and clean up the tunnel for a server.
     * @param uuid The server UUID.
     */
    void stopTunnel(const std::string& uuid);

    /**
     * Check if a tunnel is active for a server.
     */
    bool isTunnelActive(const std::string& uuid);

    /**
     * Shutdown all active tunnels.
     */
    void shutdownAll();

private:
    TunnelManager() = default;
    ~TunnelManager();

    std::unordered_map<std::string, ActiveTunnel*> m_tunnels;
    std::mutex m_mutex;

    void readTunnelStream(ActiveTunnel* tunnel,
#ifdef _WIN32
                          HANDLE hRead
#else
                          int readFd
#endif
    );
};

} // namespace MCDeploy
