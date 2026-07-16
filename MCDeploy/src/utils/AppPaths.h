#pragma once

#include <filesystem>

namespace MCDeployPaths {
const std::filesystem::path& executablePath();
const std::filesystem::path& executableDirectory();
const std::filesystem::path& dataDirectory();
std::filesystem::path boreExecutable();
bool initializeRuntimeDirectory();
} // namespace MCDeployPaths
