#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OSTPlatform::Process {

    struct ModuleInfo {
        std::string path;
        std::filesystem::path nativePath;
        uint32_t size = 0;
        bool executable = false;
    };

    std::optional<uint64_t> GetCreationTime(uint32_t pid);
    std::string FormatCreationTime(uint64_t fileTime);
    std::optional<std::string> GetImagePath(uint32_t pid);
    std::optional<std::string> GetEnvironmentVariableValue(uint32_t pid, std::wstring_view name);
    std::optional<std::string> GetProcessCommandLine(uint32_t pid);
    std::vector<ModuleInfo> EnumerateModules(uint32_t pid);

    // True when `path` lives under the OS system directory tree (on Windows,
    // %WINDIR%). Used to filter out OS-supplied modules during scans. Case- and
    // separator-insensitive; returns false on platforms without the concept.
    bool IsSystemModulePath(const std::string& path);

} // namespace OSTPlatform::Process
