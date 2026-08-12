#pragma once

#include <string>

namespace AppUpdater {

    struct CheckResult {
        bool updateAvailable = false;
        std::string oldVersion;   // version baked into the running DLL
        std::string newVersion;   // version named by latest.toml
        std::string dllRelPath;   // branch-relative path to the new DLL
        std::string sha256;       // expected SHA-256 of the new DLL
    };

    // Read the mirror pointer (opensteamtool/latest.toml) and decide whether the
    // published build differs from the running one. Never throws; returns
    // updateAvailable = false on any failure.
    CheckResult Check();

    // Download the new DLL, validate it (size, MZ header, SHA-256), and stage it next
    // to Steam via rename-then-write. The current (loaded) DLL is renamed to
    // "<name>.old"; the new bytes are written at the canonical path and load on the
    // next Steam start. Returns false (leaving the live DLL untouched) on any failure.
    bool DownloadAndStage(const CheckResult& result, const std::string& selfDllPath);

    // Best-effort removal of a "<name>.old" left by a previous staged update. Succeeds
    // only once the old image is no longer loaded (i.e. after a restart), so it is safe
    // to call unconditionally on startup.
    void CleanupStagedBackup(const std::string& selfDllPath);

    // Launch a detached, hidden helper that gracefully restarts Steam so a staged
    // update takes effect immediately. No-op-safe: if the helper can't launch, the
    // update still applies on the user's next manual start.
    void RestartSteam();

} // namespace AppUpdater
