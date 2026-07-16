#include "TunnelManager.h"
#include "AppPaths.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <vector>
#include <cctype>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#endif

namespace fs = std::filesystem;

namespace MCDeploy {

TunnelManager& TunnelManager::getInstance() {
    static TunnelManager instance;
    return instance;
}

TunnelManager::~TunnelManager() {
    shutdownAll();
}

bool TunnelManager::init() {
#ifdef _WIN32
    const auto borePath = ::MCDeployPaths::boreExecutable();
    if (fs::is_regular_file(borePath)) return true;
    std::cerr << "[MCDeploy Tunnel] bore.exe is missing from the installation. Repair MCDeploy to restore it." << std::endl;
    return false;
#else
    if (fs::exists("./bore")) {
        return true;
    }
    std::cout << "[MCDeploy Tunnel] bore binary not found. Downloading..." << std::endl;
    // Download and extract tar.gz via curl and tar
    std::string cmd = "curl -L https://github.com/ekzhang/bore/releases/download/v0.5.2/bore-v0.5.2-x86_64-unknown-linux-musl.tar.gz -o bore.tar.gz && tar -xzf bore.tar.gz && rm bore.tar.gz && chmod +x bore";
    int res = std::system(cmd.c_str());
    return res == 0 && fs::exists("./bore");
#endif
}

int TunnelManager::startTunnel(const std::string& uuid, int localPort) {
    if (!init()) {
        std::cerr << "[MCDeploy Tunnel] Error: Failed to initialize bore binary." << std::endl;
        return -1;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Stop existing tunnel for this UUID if it exists
    auto it = m_tunnels.find(uuid);
    if (it != m_tunnels.end()) {
        ActiveTunnel* existing = it->second;
        if (existing->is_running) {
            std::cout << "[MCDeploy Tunnel] Tunnel already exists and is running for server: " << uuid << std::endl;
            return existing->public_port;
        }
    }

    std::cout << "[MCDeploy Tunnel] Creating TCP tunnel for local port " << localPort << "..." << std::endl;

    ActiveTunnel* tunnel = new ActiveTunnel();
    tunnel->uuid = uuid;
    tunnel->local_port = localPort;
    tunnel->is_running = true;
    tunnel->stop_reader = false;

#ifdef _WIN32
    // Windows process spawning with output redirection
    HANDLE hStdOutRead, hStdOutWrite;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) {
        delete tunnel;
        return -1;
    }
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdError = hStdOutWrite;
    si.hStdOutput = hStdOutWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    const std::string borePath = ::MCDeployPaths::boreExecutable().string();
    std::string cmdLine = "\"" + borePath + "\" local " + std::to_string(localPort) + " --to bore.pub";
    std::vector<char> cmdBuffer(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back('\0');

    BOOL success = CreateProcessA(
        borePath.c_str(),
        cmdBuffer.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    CloseHandle(hStdOutWrite); // Close write end in parent so ReadFile returns EOF when child exits

    if (!success) {
        CloseHandle(hStdOutRead);
        delete tunnel;
        return -1;
    }

    tunnel->hProcess = pi.hProcess;
    tunnel->hThread = pi.hThread;

    // Parse the output inline to capture the port before spinning off reader thread
    char buffer[1024];
    DWORD bytesRead;
    std::string output = "";
    int assignedPort = -1;
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count() > 10) {
            std::cerr << "[MCDeploy Tunnel] Timeout waiting for bore tunnel allocation." << std::endl;
            break;
        }

        DWORD bytesAvail = 0;
        if (PeekNamedPipe(hStdOutRead, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
            if (ReadFile(hStdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                output += buffer;

                size_t pos = output.find("listening at bore.pub:");
                if (pos != std::string::npos) {
                    size_t portPos = pos + strlen("listening at bore.pub:");
                    std::string portStr = "";
                    while (portPos < output.size() && std::isdigit(output[portPos])) {
                        portStr += output[portPos];
                        portPos++;
                    }
                    if (!portStr.empty()) {
                        assignedPort = std::stoi(portStr);
                        break;
                    }
                }
            }
        } else {
            // Check if exited
            DWORD exitCode;
            if (GetExitCodeProcess(tunnel->hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    if (assignedPort != -1) {
        tunnel->public_port = assignedPort;
        std::cout << "[MCDeploy Tunnel] Tunnel successfully mapped: localhost:" << localPort 
                  << " -> bore.pub:" << assignedPort << std::endl;
        
        // Start background reader thread to clear stdout
        tunnel->reader_thread = std::thread(&TunnelManager::readTunnelStream, this, tunnel, hStdOutRead);
    } else {
        std::cerr << "[MCDeploy Tunnel] Tunnel setup failed. Output: " << output << std::endl;
        TerminateProcess(tunnel->hProcess, 1);
        CloseHandle(tunnel->hProcess);
        CloseHandle(tunnel->hThread);
        CloseHandle(hStdOutRead);
        delete tunnel;
        return -1;
    }

#else
    // Linux process spawning with pipe
    int stdout_pipe[2];
    if (pipe(stdout_pipe) < 0) {
        delete tunnel;
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        delete tunnel;
        return -1;
    }

    if (pid == 0) {
        // Child
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);

        std::string localPortStr = std::to_string(localPort);
        execl("./bore", "bore", "local", localPortStr.c_str(), "--to", "bore.pub", (char*)NULL);
        exit(1);
    } else {
        // Parent
        close(stdout_pipe[1]);
        tunnel->pid = pid;

        char buffer[1024];
        std::string output = "";
        int assignedPort = -1;
        auto startTime = std::chrono::steady_clock::now();

        // Set non-blocking read
        int flags = fcntl(stdout_pipe[0], F_GETFL, 0);
        fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

        while (true) {
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count() > 10) {
                break;
            }

            ssize_t bytesRead = read(stdout_pipe[0], buffer, sizeof(buffer) - 1);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                output += buffer;

                size_t pos = output.find("listening at bore.pub:");
                if (pos != std::string::npos) {
                    size_t portPos = pos + strlen("listening at bore.pub:");
                    std::string portStr = "";
                    while (portPos < output.size() && std::isdigit(output[portPos])) {
                        portStr += output[portPos];
                        portPos++;
                    }
                    if (!portStr.empty()) {
                        assignedPort = std::stoi(portStr);
                        break;
                    }
                }
            } else if (bytesRead < 0 && errno == EAGAIN) {
                int status;
                if (waitpid(pid, &status, WNOHANG) != 0) {
                    break; // Child exited
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } else {
                break;
            }
        }

        if (assignedPort != -1) {
            tunnel->public_port = assignedPort;
            std::cout << "[MCDeploy Tunnel] Tunnel successfully mapped: localhost:" << localPort 
                      << " -> bore.pub:" << assignedPort << std::endl;
            tunnel->reader_thread = std::thread(&TunnelManager::readTunnelStream, this, tunnel, stdout_pipe[0]);
        } else {
            std::cerr << "[MCDeploy Tunnel] Tunnel setup failed. Output: " << output << std::endl;
            kill(pid, SIGKILL);
            int status;
            waitpid(pid, &status, 0);
            close(stdout_pipe[0]);
            delete tunnel;
            return -1;
        }
    }
#endif

    m_tunnels[uuid] = tunnel;
    return tunnel->public_port;
}

void TunnelManager::stopTunnel(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_tunnels.find(uuid);
    if (it == m_tunnels.end()) {
        return;
    }

    ActiveTunnel* tunnel = it->second;
    tunnel->stop_reader = true;

    std::cout << "[MCDeploy Tunnel] Closing TCP tunnel for server: " << uuid << std::endl;

#ifdef _WIN32
    if (tunnel->hProcess != NULL) {
        TerminateProcess(tunnel->hProcess, 1);
        WaitForSingleObject(tunnel->hProcess, 3000);
        CloseHandle(tunnel->hProcess);
        CloseHandle(tunnel->hThread);
        tunnel->hProcess = NULL;
        tunnel->hThread = NULL;
    }
#else
    if (tunnel->pid > 0) {
        kill(tunnel->pid, SIGKILL);
        int status;
        waitpid(tunnel->pid, &status, 0);
        tunnel->pid = -1;
    }
#endif

    if (tunnel->reader_thread.joinable()) {
        tunnel->reader_thread.join();
    }

    delete tunnel;
    m_tunnels.erase(it);
}

bool TunnelManager::isTunnelActive(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_tunnels.find(uuid);
    return it != m_tunnels.end() && it->second->is_running;
}

void TunnelManager::shutdownAll() {
    std::vector<std::string> activeUuids;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [uuid, tunnel] : m_tunnels) {
            activeUuids.push_back(uuid);
        }
    }

    for (const auto& uuid : activeUuids) {
        stopTunnel(uuid);
    }
}

void TunnelManager::readTunnelStream(ActiveTunnel* tunnel,
#ifdef _WIN32
                                    HANDLE hRead
#else
                                    int readFd
#endif
) {
    char buffer[1024];
#ifdef _WIN32
    DWORD bytesRead;
    while (!tunnel->stop_reader) {
        DWORD bytesAvail = 0;
        if (PeekNamedPipe(hRead, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
            if (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                // Just clear the pipe
            }
        } else {
            DWORD exitCode;
            if (GetExitCodeProcess(tunnel->hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    CloseHandle(hRead);
#else
    while (!tunnel->stop_reader) {
        ssize_t bytesRead = read(readFd, buffer, sizeof(buffer) - 1);
        if (bytesRead <= 0) {
            if (bytesRead < 0 && errno == EAGAIN) {
                int status;
                if (waitpid(tunnel->pid, &status, WNOHANG) != 0) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                break;
            }
        }
    }
    close(readFd);
#endif
    tunnel->is_running = false;
}

} // namespace MCDeploy
