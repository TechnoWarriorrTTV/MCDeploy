#include "AppPaths.h"

#include <cstdlib>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace MCDeployPaths {
namespace {
std::filesystem::path resolveExecutablePath() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        buffer.resize(length);
        return std::filesystem::path(buffer);
    }
#elif defined(__linux__)
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) return path;
#endif
    return std::filesystem::absolute("mcdeploy");
}
std::filesystem::path resolveDataDirectory() {
#ifdef _WIN32
    PWSTR value = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &value))) {
        const std::filesystem::path path(value);
        CoTaskMemFree(value);
        return path / "MCDeploy";
    }
#endif
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".local" / "share" / "mcdeploy";
    return std::filesystem::current_path() / ".mcdeploy";
}
} // namespace
const std::filesystem::path& executablePath() { static const auto path = resolveExecutablePath(); return path; }
const std::filesystem::path& executableDirectory() { static const auto path = executablePath().parent_path(); return path; }
const std::filesystem::path& dataDirectory() { static const auto path = resolveDataDirectory(); return path; }
std::filesystem::path boreExecutable() {
#ifdef _WIN32
    constexpr const char* name = "bore.exe";
#else
    constexpr const char* name = "bore";
#endif
    const std::vector<std::filesystem::path> candidates = {
        executableDirectory() / name,
        executableDirectory().parent_path() / name,
        executableDirectory().parent_path().parent_path() / name,
        dataDirectory() / name
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    return executableDirectory() / name;
}
bool initializeRuntimeDirectory() {
    std::error_code error;
    std::filesystem::create_directories(dataDirectory(), error);
    if (error) return false;
    std::filesystem::current_path(dataDirectory(), error);
    return !error;
}
} // namespace MCDeployPaths
