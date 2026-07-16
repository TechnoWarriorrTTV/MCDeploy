#include "ProcessManager.h"
#include "../models/Database.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <regex>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#endif

namespace MCDeploy {

ProcessManager& ProcessManager::getInstance() {
    static ProcessManager instance;
    return instance;
}

ProcessManager::~ProcessManager() {
    shutdownAll();
}

std::string ProcessManager::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm bt{};
#ifdef _WIN32
    localtime_s(&bt, &time_t_now);
#else
    localtime_r(&time_t_now, &bt);
#endif
    std::stringstream ss;
    ss << std::put_time(&bt, "%H:%M:%S");
    return ss.str();
}

void ProcessManager::addLogLine(MinecraftServerProcess* proc, const std::string& rawLine, const std::string& streamType) {
    if (rawLine.empty()) return;

    ProcessConsoleLine line;
    line.text = rawLine;
    line.timestamp = getTimestamp();

    // Simple parsing of Minecraft log formats
    if (rawLine.find("[WARN]") != std::string::npos || rawLine.find("/WARN]") != std::string::npos) {
        line.type = "WARN";
    } else if (rawLine.find("[ERROR]") != std::string::npos || rawLine.find("/ERROR]") != std::string::npos) {
        line.type = "ERROR";
    } else if (rawLine.find("[INFO]") != std::string::npos || rawLine.find("/INFO]") != std::string::npos) {
        line.type = "INFO";
    } else {
        line.type = streamType;
    }

    // Detect when server is fully started
    if (rawLine.find("Done (") != std::string::npos || rawLine.find("For help, type") != std::string::npos) {
        Database::getInstance().updateServerStatus(proc->uuid, "Online");
        if (proc->smart_optimization) {
            std::vector<PlayerRecord> players = Database::getInstance().getServerPlayers(proc->uuid);
            int onlineCount = 0;
            for (const auto& p : players) {
                if (p.is_online) onlineCount++;
            }
            proc->cpu_priority = (onlineCount > 0) ? "above_normal" : "normal";
            applyCpuPriority(proc, proc->cpu_priority);
        }
    }

    // Parse Player logon: "Username[/127.0.0.1:12345] logged in with entity id 123 at (10.0, 64.0, 20.0)" or with world prefix like "at ([world]10.0, 64.0, 20.0)"
    if (rawLine.find("logged in with entity id") != std::string::npos) {
        std::regex joinRegex("([a-zA-Z0-9_]+)\\[/[0-9\\.:]+\\] logged in with entity id \\d+ at \\((?:\\[[^\\]]+\\])?([-0-9\\.]+),\\s*([-0-9\\.]+),\\s*([-0-9\\.]+)\\)");
        std::smatch matches;
        if (std::regex_search(rawLine, matches, joinRegex)) {
            try {
                std::string username = matches[1].str();
                double x = std::stod(matches[2].str());
                double y = std::stod(matches[3].str());
                double z = std::stod(matches[4].str());

                Database::getInstance().logPlayerCoordinates(proc->uuid, username, "login", x, y, z);
                Database::getInstance().logAction("SYSTEM", "PLAYER_JOIN", proc->uuid, "Player " + username + " logged in at coords: (" + matches[2].str() + ", " + matches[3].str() + ", " + matches[4].str() + ")");

                // Analytics: open a session row + record the join event.
                PlayerRecord pr;
                Database::getInstance().getOrCreatePlayer(proc->uuid, username, pr);
                Database::getInstance().startPlayerSession(proc->uuid, pr.uuid, username, x, y, z);
                Database::getInstance().logPlayerEvent({
                    /*id*/0, proc->uuid, pr.uuid, username, "join", "", ""
                });

                if (proc->smart_optimization) {
                    proc->cpu_priority = "above_normal";
                    applyCpuPriority(proc, "above_normal");
                }
            } catch (...) {}
        }
    }

    // Parse Player logoff: "Username left the game"
    if (rawLine.find("left the game") != std::string::npos) {
        std::regex leaveRegex("([a-zA-Z0-9_]+)\\s+left the game");
        std::smatch matches;
        if (std::regex_search(rawLine, matches, leaveRegex)) {
            try {
                std::string username = matches[1].str();
                double x = 0.0, y = 64.0, z = 0.0;
                PlayerRecord pr;
                if (Database::getInstance().getPlayer(proc->uuid, username, pr)) {
                    x = pr.last_login_x;
                    y = pr.last_login_y;
                    z = pr.last_login_z;
                }
                Database::getInstance().logPlayerCoordinates(proc->uuid, username, "logoff", x, y, z);
                Database::getInstance().logAction("SYSTEM", "PLAYER_LEAVE", proc->uuid, "Player " + username + " left the game");

                // Analytics: close the session + record the leave event.
                Database::getInstance().endPlayerSession(proc->uuid, username);
                Database::getInstance().logPlayerEvent({
                    /*id*/0, proc->uuid, pr.uuid, username, "leave", "", ""
                });

                if (proc->smart_optimization) {
                    std::vector<PlayerRecord> players = Database::getInstance().getServerPlayers(proc->uuid);
                    int onlineCount = 0;
                    for (const auto& p : players) {
                        if (p.is_online) onlineCount++;
                    }
                    proc->cpu_priority = (onlineCount > 0) ? "above_normal" : "normal";
                    applyCpuPriority(proc, proc->cpu_priority);
                }
            } catch (...) {}
        }
    }

    // Chat detection. Vanilla + Paper formats:
    //   [INFO]: <PlayerName> Hello world
    //   [INFO]: [Not Secure] <PlayerName> Hello world
    // We only run this when the line contains "<...>" to keep the regex cheap
    // for non-chat lines. Skip any bracket-tag chats like "[Server]" or
    // teamspeak-style prefixes to avoid double-counting system messages.
    if (rawLine.find('<') != std::string::npos && rawLine.find('>') != std::string::npos) {
        std::regex chatRegex("(?:\\[Not Secure\\]\\s*)?<([a-zA-Z0-9_]+)>\\s*(.*)$");
        std::smatch matches;
        if (std::regex_search(rawLine, matches, chatRegex)) {
            try {
                std::string username = matches[1].str();
                std::string message  = matches[2].str();
                PlayerRecord pr;
                Database::getInstance().getPlayer(proc->uuid, username, pr);
                Database::getInstance().logPlayerEvent({
                    /*id*/0, proc->uuid, pr.uuid, username, "chat", message, ""
                });
            } catch (...) {}
        }
    }

    // Death detection. Vanilla death messages start with the player name and
    // contain phrases like "was slain by", "fell from", "was blown up by",
    // "drowned", "burned to death", etc. Rather than enumerating all 100+
    // possible causes we detect a handful of common indicators.
    {
        static const std::regex deathRegex(
            "^([a-zA-Z0-9_]+) (?:was |went |drowned|fell |hit the ground|burned|died|starved|"
            "suffocated|blew up|withered|froze|got finished off|walked into|didn'?t want|"
            "tried to swim|discovered the floor|experienced kinetic energy|left the confines)");
        std::smatch matches;
        if (std::regex_search(rawLine, matches, deathRegex)) {
            try {
                std::string username = matches[1].str();
                // Filter false positives — "SYSTEM was ..." shouldn't count.
                if (username != "SYSTEM" && username.size() >= 3) {
                    PlayerRecord pr;
                    Database::getInstance().getPlayer(proc->uuid, username, pr);
                    Database::getInstance().logPlayerEvent({
                        /*id*/0, proc->uuid, pr.uuid, username, "death", rawLine, ""
                    });
                }
            } catch (...) {}
        }
    }

    std::lock_guard<std::mutex> lock(proc->log_mutex);
    line.sequence = proc->total_logs_added++;
    proc->logs.push_back(line);
    if (proc->logs.size() > 500) {
        proc->logs.erase(proc->logs.begin());
    }
}

#ifdef _WIN32
void ProcessManager::readStream(MinecraftServerProcess* proc, HANDLE hRead) {
    char buffer[4096];
    DWORD bytesRead;
    std::string incompleteLine = "";

    while (!proc->stop_reader) {
        DWORD bytesAvail = 0;
        if (PeekNamedPipe(hRead, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
            if (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string chunk(buffer);
                std::string combined = incompleteLine + chunk;
                size_t pos = 0;
                size_t prev = 0;

                while ((pos = combined.find('\n', prev)) != std::string::npos) {
                    std::string line = combined.substr(prev, pos - prev);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    addLogLine(proc, line, "STDOUT");
                    prev = pos + 1;
                }
                incompleteLine = combined.substr(prev);
            }
        } else {
            // Check if process has exited
            DWORD exitCode;
            if (GetExitCodeProcess(proc->hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    if (!incompleteLine.empty()) {
        addLogLine(proc, incompleteLine, "STDOUT");
    }
    CloseHandle(hRead);
    handleProcessExit(proc);
}
#else
void ProcessManager::readStream(MinecraftServerProcess* proc, int readFd) {
    char buffer[4096];
    std::string incompleteLine = "";

    // Set non-blocking read
    int flags = fcntl(readFd, F_GETFL, 0);
    fcntl(readFd, F_SETFL, flags | O_NONBLOCK);

    while (!proc->stop_reader) {
        ssize_t bytesRead = read(readFd, buffer, sizeof(buffer) - 1);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string chunk(buffer);
            std::string combined = incompleteLine + chunk;
            size_t pos = 0;
            size_t prev = 0;

            while ((pos = combined.find('\n', prev)) != std::string::npos) {
                std::string line = combined.substr(prev, pos - prev);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                addLogLine(proc, line, "STDOUT");
                prev = pos + 1;
            }
            incompleteLine = combined.substr(prev);
        } else if (bytesRead < 0 && errno == EAGAIN) {
            // No data currently, check if process died
            int status;
            pid_t result = waitpid(proc->pid, &status, WNOHANG);
            if (result != 0) {
                // Process finished or error
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } else {
            // EOF or error
            break;
        }
    }
    if (!incompleteLine.empty()) {
        addLogLine(proc, incompleteLine, "STDOUT");
    }
    close(readFd);
    handleProcessExit(proc);
}
#endif

void ProcessManager::handleProcessExit(MinecraftServerProcess* proc) {
    std::lock_guard<std::mutex> lock(m_mutex);
    proc->is_running = false;
    // Analytics: close any open sessions for this server so aggregate metrics
    // stay correct. Best-effort — do not fail process teardown if the DB is closed.
    try {
        Database::getInstance().closeOpenSessionsForServer(proc->uuid);
    } catch (...) {}
    
    // Read exit status if possible
    int exitCode = 0;
#ifdef _WIN32
    DWORD winExitCode;
    if (GetExitCodeProcess(proc->hProcess, &winExitCode)) {
        exitCode = static_cast<int>(winExitCode);
        CloseHandle(proc->hProcess);
        CloseHandle(proc->hThread);
        CloseHandle(proc->hStdIn);
        proc->hProcess = NULL;
        proc->hThread = NULL;
        proc->hStdIn = NULL;
    }
#else
    int status;
    waitpid(proc->pid, &status, 0);
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exitCode = -WTERMSIG(status);
    }
    if (proc->std_in_fd != -1) {
        close(proc->std_in_fd);
        proc->std_in_fd = -1;
    }
#endif

    addLogLine(proc, "[MCDeploy] Process exited with code " + std::to_string(exitCode), "WARN");
    
    // Check for crash
    if (exitCode != 0 && proc->auto_restart) {
        proc->crash_count++;
        if (proc->crash_count < 5) {
            addLogLine(proc, "[MCDeploy] Server crashed! Auto-restarting (attempt " + std::to_string(proc->crash_count) + "/5)...", "ERROR");
            if (m_crashCallback) {
                m_crashCallback(proc->uuid, "Crash detected. Restarting...");
            }
            // Trigger start asynchronously after brief sleep
            std::string uuid = proc->uuid;
            std::string name = proc->name;
            std::string directory = proc->directory;
            std::string startCmd = proc->start_cmd;
            std::thread([this, uuid, name, directory, startCmd]() {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                startServer(uuid, name, directory, startCmd);
            }).detach();
        } else {
            addLogLine(proc, "[MCDeploy] Server crashed repeatedly. Auto-restart disabled.", "ERROR");
        }
    }
}

bool ProcessManager::startServer(const std::string& uuid, const std::string& name, const std::string& directory, const std::string& startCmd) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_processes.find(uuid);
    MinecraftServerProcess* proc = nullptr;
    if (it != m_processes.end()) {
        proc = it->second;
        if (proc->is_running) {
            return false; // Already running
        }
    } else {
        proc = new MinecraftServerProcess();
        proc->uuid = uuid;
        proc->name = name;
        proc->directory = directory;
        proc->start_cmd = startCmd;
        m_processes[uuid] = proc;
    }

    proc->is_running = true;
    proc->stop_reader = false;
    proc->logs.clear();
    // Analytics: any sessions still marked active belong to a prior boot and
    // will never see a real "left the game" line. Close them as abandoned so
    // they don't skew concurrent-user calculations.
    Database::getInstance().closeOpenSessionsForServer(uuid);
    addLogLine(proc, "[MCDeploy] Starting Minecraft server: " + name, "INFO");
    addLogLine(proc, "[MCDeploy] Executing: " + startCmd + " in working directory: " + directory, "INFO");

#ifdef _WIN32
    // Windows process spawning
    HANDLE hStdInRead, hStdInWrite;
    HANDLE hStdOutRead, hStdOutWrite;
    
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hStdInRead, &hStdInWrite, &sa, 0) ||
        !CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) {
        proc->is_running = false;
        return false;
    }

    SetHandleInformation(hStdInWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdError = hStdOutWrite;
    si.hStdOutput = hStdOutWrite;
    si.hStdInput = hStdInRead;
    si.dwFlags |= STARTF_USESTDHANDLES;

    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    // Prepare mutable command line copy
    std::vector<char> cmdBuffer(startCmd.begin(), startCmd.end());
    cmdBuffer.push_back('\0');

    BOOL success = CreateProcessA(
        NULL,
        cmdBuffer.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        directory.c_str(),
        &si,
        &pi
    );

    CloseHandle(hStdInRead);
    CloseHandle(hStdOutWrite);

    if (!success) {
        CloseHandle(hStdInWrite);
        CloseHandle(hStdOutRead);
        proc->is_running = false;
        return false;
    }

    proc->hProcess = pi.hProcess;
    proc->hThread = pi.hThread;
    proc->hStdIn = hStdInWrite;

    // Launch reader thread
    if (proc->reader_thread.joinable()) {
        proc->reader_thread.join();
    }
    proc->reader_thread = std::thread(&ProcessManager::readStream, this, proc, hStdOutRead);

#else
    // Linux process spawning
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        proc->is_running = false;
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        proc->is_running = false;
        return false;
    }

    if (pid == 0) {
        // Child process
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);

        // Change directory
        if (chdir(directory.c_str()) != 0) {
            std::cerr << "Failed to chdir to " << directory << std::endl;
            exit(1);
        }

        // Exec command using shell wrapper for argument processing and environment setup
        // Uses exec to prevent spawning another shell subprocess
        std::string execCmd = "exec " + startCmd;
        execl("/bin/sh", "sh", "-c", execCmd.c_str(), (char*)NULL);
        exit(1); // Should never reach
    } else {
        // Parent process
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        proc->pid = pid;
        proc->std_in_fd = stdin_pipe[1];

        // Launch reader thread
        if (proc->reader_thread.joinable()) {
            proc->reader_thread.join();
        }
        proc->reader_thread = std::thread(&ProcessManager::readStream, this, proc, stdout_pipe[0]);
    }
#endif

    if (proc->smart_optimization) {
        proc->cpu_priority = "high";
        applyCpuPriority(proc, "high");
    } else {
        applyCpuPriority(proc, proc->cpu_priority);
    }

    return true;
}

bool ProcessManager::stopServer(const std::string& uuid, bool force) {
    MinecraftServerProcess* proc = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_processes.find(uuid);
        if (it == m_processes.end() || !it->second->is_running) {
            return false;
        }
        proc = it->second;
    }

    // Disable auto restart during manual stopping
    proc->auto_restart = false;

    if (!force) {
        addLogLine(proc, "[MCDeploy] Sending /stop command to server...", "INFO");
        sendCommand(uuid, "stop");

        // Wait up to 15 seconds for graceful exit
        for (int i = 0; i < 30; i++) {
            if (!isServerRunning(uuid)) {
                proc->auto_restart = true;
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        addLogLine(proc, "[MCDeploy] Server did not stop gracefully. Forcing termination...", "WARN");
    }

    // Kill process
#ifdef _WIN32
    if (proc->hProcess != NULL) {
        TerminateProcess(proc->hProcess, 1);
        // Wait for process handle to signal (up to 5 seconds)
        WaitForSingleObject(proc->hProcess, 5000);
    }
#else
    if (proc->pid > 0) {
        kill(proc->pid, SIGKILL);
    }
#endif

    // Wait for the reader thread to finish and is_running to become false (up to 5 seconds)
    for (int i = 0; i < 50; ++i) {
        if (!proc->is_running) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    proc->auto_restart = true;
    return true;
}

bool ProcessManager::sendCommand(const std::string& uuid, const std::string& command) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_processes.find(uuid);
    if (it == m_processes.end() || !it->second->is_running) {
        return false;
    }

    MinecraftServerProcess* proc = it->second;
    std::string formattedCmd = command + "\n";

#ifdef _WIN32
    DWORD bytesWritten;
    if (proc->hStdIn != NULL) {
        WriteFile(proc->hStdIn, formattedCmd.c_str(), static_cast<DWORD>(formattedCmd.length()), &bytesWritten, NULL);
        return bytesWritten > 0;
    }
#else
    if (proc->std_in_fd != -1) {
        ssize_t written = write(proc->std_in_fd, formattedCmd.c_str(), formattedCmd.length());
        return written > 0;
    }
#endif
    return false;
}

bool ProcessManager::isServerRunning(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_processes.find(uuid);
    if (it == m_processes.end()) {
        return false;
    }
    return it->second->is_running;
}

std::vector<ProcessConsoleLine> ProcessManager::getLogs(const std::string& uuid, size_t limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_processes.find(uuid);
    if (it == m_processes.end()) {
        return {};
    }

    MinecraftServerProcess* proc = it->second;
    std::lock_guard<std::mutex> logLock(proc->log_mutex);
    
    std::vector<ProcessConsoleLine> slice;
    size_t count = std::min(limit, proc->logs.size());
    if (count > 0) {
        slice.assign(proc->logs.end() - count, proc->logs.end());
    }
    return slice;
}

std::vector<std::string> ProcessManager::getRunningServerUuids() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> uuids;
    for (auto const& [uuid, proc] : m_processes) {
        if (proc->is_running) {
            uuids.push_back(uuid);
        }
    }
    return uuids;
}

void ProcessManager::shutdownAll() {
    std::vector<std::string> activeUuids;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto const& [uuid, proc] : m_processes) {
            if (proc->is_running) {
                activeUuids.push_back(uuid);
            }
        }
    }

    for (const auto& uuid : activeUuids) {
        stopServer(uuid, false);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [uuid, proc] : m_processes) {
        proc->stop_reader = true;
        if (proc->reader_thread.joinable()) {
            proc->reader_thread.join();
        }
        delete proc;
    }
    m_processes.clear();
}

bool ProcessManager::applyCpuPriority(MinecraftServerProcess* proc, const std::string& priority) {
    if (!proc->is_running) return false;
#ifdef _WIN32
    if (proc->hProcess == NULL) return false;
    DWORD winPriority = NORMAL_PRIORITY_CLASS;
    if (priority == "idle") winPriority = IDLE_PRIORITY_CLASS;
    else if (priority == "below_normal") winPriority = BELOW_NORMAL_PRIORITY_CLASS;
    else if (priority == "normal") winPriority = NORMAL_PRIORITY_CLASS;
    else if (priority == "above_normal") winPriority = ABOVE_NORMAL_PRIORITY_CLASS;
    else if (priority == "high") winPriority = HIGH_PRIORITY_CLASS;
    
    BOOL res = SetPriorityClass(proc->hProcess, winPriority);
    return res != 0;
#else
    if (proc->pid <= 0) return false;
    int niceVal = 0;
    if (priority == "idle") niceVal = 15;
    else if (priority == "below_normal") niceVal = 5;
    else if (priority == "normal") niceVal = 0;
    else if (priority == "above_normal") niceVal = -5;
    else if (priority == "high") niceVal = -10;
    
    int res = setpriority(PRIO_PROCESS, proc->pid, niceVal);
    return res == 0;
#endif
}

bool ProcessManager::setServerCpuPriority(const std::string& uuid, const std::string& priority) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_processes.find(uuid);
    if (it == m_processes.end()) {
        return false;
    }
    
    MinecraftServerProcess* proc = it->second;
    proc->cpu_priority = priority;
    
    if (proc->is_running) {
        return applyCpuPriority(proc, priority);
    }
    return true;
}

bool ProcessManager::setServerSmartOptimization(const std::string& uuid, bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_processes.find(uuid);
    if (it == m_processes.end()) {
        return false;
    }
    
    MinecraftServerProcess* proc = it->second;
    proc->smart_optimization = enable;
    
    if (proc->is_running && enable) {
        ServerRecord s;
        std::string status = "Offline";
        if (Database::getInstance().getServer(uuid, s)) {
            status = s.status;
        }
        
        if (status == "Starting") {
            proc->cpu_priority = "high";
        } else if (status == "Online") {
            std::vector<PlayerRecord> players = Database::getInstance().getServerPlayers(uuid);
            int onlinePlayers = 0;
            for (const auto& p : players) {
                if (p.is_online) onlinePlayers++;
            }
            proc->cpu_priority = (onlinePlayers > 0) ? "above_normal" : "normal";
        }
        applyCpuPriority(proc, proc->cpu_priority);
    }
    return true;
}

std::string ProcessManager::getServerCpuPriority(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_processes.find(uuid);
    if (it == m_processes.end()) {
        return "normal";
    }
    return it->second->cpu_priority;
}

bool ProcessManager::getServerSmartOptimization(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_processes.find(uuid);
    if (it == m_processes.end()) {
        return true;
    }
    return it->second->smart_optimization;
}

} // namespace MCDeploy
