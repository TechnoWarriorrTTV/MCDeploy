#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

namespace MCDeploy {

struct ProcessConsoleLine {
    std::string text;
    std::string type; // "INFO", "WARN", "ERROR", "STDOUT", "STDERR"
    std::string timestamp;
    uint64_t sequence = 0;
};

struct MinecraftServerProcess {
    std::string uuid;
    std::string name;
    bool is_running = false;
    std::string start_cmd;
    std::string directory;
    bool auto_restart = true;
    int crash_count = 0;

#ifdef _WIN32
    HANDLE hProcess = NULL;
    HANDLE hThread = NULL;
    HANDLE hStdIn = NULL;
#else
    pid_t pid = -1;
    int std_in_fd = -1;
#endif

    std::vector<ProcessConsoleLine> logs;
    std::mutex log_mutex;
    std::thread reader_thread;
    std::atomic<bool> stop_reader{false};
    uint64_t total_logs_added = 0;

    std::string cpu_priority = "normal";
    bool smart_optimization = true;
};

class ProcessManager {
public:
    static ProcessManager& getInstance();

    bool startServer(const std::string& uuid, const std::string& name, const std::string& directory, const std::string& startCmd);
    bool stopServer(const std::string& uuid, bool force = false);
    bool sendCommand(const std::string& uuid, const std::string& command);
    bool isServerRunning(const std::string& uuid);
    
    std::vector<ProcessConsoleLine> getLogs(const std::string& uuid, size_t limit = 100);
    std::vector<std::string> getRunningServerUuids();

    // Priority and Performance tuning
    bool setServerCpuPriority(const std::string& uuid, const std::string& priority);
    bool setServerSmartOptimization(const std::string& uuid, bool enable);
    std::string getServerCpuPriority(const std::string& uuid);
    bool getServerSmartOptimization(const std::string& uuid);

    // Callback on process crash
    void setCrashCallback(std::function<void(const std::string&, const std::string&)> cb) {
        m_crashCallback = cb;
    }

    void shutdownAll();

private:
    ProcessManager() = default;
    ~ProcessManager();
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    std::unordered_map<std::string, MinecraftServerProcess*> m_processes;
    std::mutex m_mutex;
    std::function<void(const std::string&, const std::string&)> m_crashCallback;

    bool applyCpuPriority(MinecraftServerProcess* proc, const std::string& priority);

    void readStream(MinecraftServerProcess* proc, 
#ifdef _WIN32
                    HANDLE hRead
#else
                    int readFd
#endif
    );

    void addLogLine(MinecraftServerProcess* proc, const std::string& rawLine, const std::string& streamType);
    void handleProcessExit(MinecraftServerProcess* proc);
    std::string getTimestamp();
};

} // namespace MCDeploy
