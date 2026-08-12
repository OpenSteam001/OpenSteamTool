#include "AppUpdater.h"

#include "OpenSteamToolBuildInfo.h"
#include "OSTPlatform/include/DynamicLibrary.h"
#include "OSTPlatform/include/Hash.h"
#include "OSTPlatform/include/Process.h"
#include "Utils/Logging/Log.h"
#include "Utils/SteamMetadata/Mirror.h"

#include <toml++/toml.hpp>

#include <windows.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace AppUpdater {

namespace {
    // Bounds for a sane framework DLL payload — guards against a truncated or hostile
    // response being written to disk. OpenSteamTool.dll is ~1 MB.
    constexpr size_t kMinDllBytes = 200 * 1024;
    constexpr size_t kMaxDllBytes = 8 * 1024 * 1024;

    constexpr const char* kPointerPath = "opensteamtool/latest.toml";

    bool EqualsIgnoreCase(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    std::string EscapeForPowerShellSingleQuoted(std::string s)
    {
        // Inside a PowerShell single-quoted string, ' is escaped by doubling it.
        for (size_t p = s.find('\''); p != std::string::npos; p = s.find('\'', p + 2))
            s.insert(p, 1, '\'');
        return s;
    }
} // namespace

CheckResult Check()
{
    CheckResult r;
    r.oldVersion = OPENSTEAMTOOL_VERSION;

    std::optional<std::string> body = Mirror::Fetch(kPointerPath);
    if (!body) {
        LOG_WARN("AppUpdater: {} unavailable from all mirrors", kPointerPath);
        return r;
    }

    toml::table tbl;
    try {
        tbl = toml::parse(*body);
    } catch (const toml::parse_error& e) {
        LOG_WARN("AppUpdater: latest.toml parse error: {}", e.description());
        return r;
    }

    const auto version = tbl["version"].value<std::string>();
    const auto path    = tbl["path"].value<std::string>();
    const auto sha     = tbl["sha256"].value<std::string>();
    if (!version || !path || !sha || version->empty() || path->empty() || sha->empty()) {
        LOG_WARN("AppUpdater: latest.toml missing version/path/sha256");
        return r;
    }

    r.newVersion = *version;
    r.dllRelPath = *path;
    r.sha256     = *sha;

    // latest.toml is authoritative for "newest"; build tags are not semver, so any
    // difference from the running version means there is something new to stage.
    if (r.newVersion == r.oldVersion) {
        LOG_INFO("AppUpdater: up to date (version {})", r.oldVersion);
        return r;
    }

    LOG_INFO("AppUpdater: update available {} -> {}", r.oldVersion, r.newVersion);
    r.updateAvailable = true;
    return r;
}

bool DownloadAndStage(const CheckResult& result, const std::string& selfDllPath)
{
    std::optional<std::string> download = Mirror::Fetch(result.dllRelPath);
    if (!download) {
        LOG_WARN("AppUpdater: DLL download failed for {}", result.dllRelPath);
        return false;
    }
    const std::string& body = *download;

    if (body.size() < kMinDllBytes || body.size() > kMaxDllBytes) {
        LOG_WARN("AppUpdater: rejected DLL (suspicious size {} bytes)", body.size());
        return false;
    }
    if (body.size() < 2 || body[0] != 'M' || body[1] != 'Z') {
        LOG_WARN("AppUpdater: rejected DLL (missing MZ header)");
        return false;
    }

    const std::string actual = OSTPlatform::Hash::Sha256OfBuffer(body.data(), body.size());
    if (actual.empty() || !EqualsIgnoreCase(actual, result.sha256)) {
        LOG_WARN("AppUpdater: SHA-256 mismatch (expected {}, got {})", result.sha256, actual);
        return false;
    }

    // The running DLL is write-locked while mapped, but NTFS allows renaming a loaded
    // image, so move it aside and write the new bytes at the canonical path.
    const std::string backup = selfDllPath + ".old";
    if (!MoveFileExA(selfDllPath.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        LOG_WARN("AppUpdater: could not rename current DLL to backup (error={})", GetLastError());
        return false;
    }

    {
        std::ofstream ofs(selfDllPath, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            LOG_WARN("AppUpdater: could not open {} for writing; restoring backup", selfDllPath);
            MoveFileExA(backup.c_str(), selfDllPath.c_str(), MOVEFILE_REPLACE_EXISTING);
            return false;
        }
        ofs.write(body.data(), static_cast<std::streamsize>(body.size()));
        ofs.flush();
        if (!ofs) {
            LOG_WARN("AppUpdater: write failed for {}; restoring backup", selfDllPath);
            ofs.close();
            MoveFileExA(backup.c_str(), selfDllPath.c_str(), MOVEFILE_REPLACE_EXISTING);
            return false;
        }
    }

    LOG_INFO("AppUpdater: staged {} ({} bytes); applies on next Steam start",
             selfDllPath, body.size());
    return true;
}

void CleanupStagedBackup(const std::string& selfDllPath)
{
    const std::string backup = selfDllPath + ".old";
    std::error_code ec;
    if (std::filesystem::exists(backup, ec) && DeleteFileA(backup.c_str()))
        LOG_INFO("AppUpdater: removed stale backup {}", backup);
}

void RestartSteam()
{
    const std::filesystem::path steamExe = OSTPlatform::DynamicLibrary::GetMainExecutablePath();
    if (steamExe.empty()) {
        LOG_WARN("AppUpdater: steam.exe path unknown; cannot auto-restart");
        return;
    }

    const std::string exe = EscapeForPowerShellSingleQuoted(steamExe.string());
    // Detached, hidden PowerShell: graceful -shutdown, wait for every steam process to
    // exit (covers steamwebhelper), then relaunch.
    const std::string command =
        "powershell -NoProfile -WindowStyle Hidden -Command "
        "\"& '" + exe + "' -shutdown; "
        "Wait-Process -Name steam -Timeout 30 -ErrorAction SilentlyContinue; "
        "Start-Process '" + exe + "'\"";

    if (OSTPlatform::Process::LaunchDetachedHidden(command))
        LOG_INFO("AppUpdater: restart helper launched");
    else
        LOG_WARN("AppUpdater: restart helper failed to launch; "
                 "update applies on next manual Steam start");
}

} // namespace AppUpdater
