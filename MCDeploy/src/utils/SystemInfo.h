#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace MCDeploy {

struct SystemMetrics {
    double cpu_usage_pct;
    double ram_total_gb;
    double ram_used_gb;
    double ram_free_gb;
    double ram_avail_gb;
    double disk_total_gb;
    double disk_used_gb;
    double disk_free_gb;
    double load_1m;
    double load_5m;
    double load_15m;
    double net_rx_kbps;
    double net_tx_kbps;
};

class SystemInfo {
public:
    static SystemInfo& getInstance();

    SystemMetrics getMetrics(const std::string& serverPath = ".");
    
    // Parse Minecraft logs and fetch JVM Heap details
    static double getWorldSizeGB(const std::string& serverDirectory);

private:
    SystemInfo();
    ~SystemInfo() = default;

#ifdef _WIN32
    // Windows CPU tracking variables
    uint64_t m_lastIdleTime = 0;
    uint64_t m_lastKernelTime = 0;
    uint64_t m_lastUserTime = 0;
#else
    // Linux CPU tracking variables
    uint64_t m_lastUser = 0;
    uint64_t m_lastNice = 0;
    uint64_t m_lastSystem = 0;
    uint64_t m_lastIdle = 0;
    uint64_t m_lastIoWait = 0;
    uint64_t m_lastIrq = 0;
    uint64_t m_lastSoftIrq = 0;
    uint64_t m_lastSteal = 0;
#endif

    double calculateCpuUsage();
};

} // namespace MCDeploy
