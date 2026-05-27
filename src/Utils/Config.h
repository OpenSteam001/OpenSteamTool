#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace Config {

    enum class ManifestUrl { SteamRun, Wudrm };

    enum class LogLevel { Trace, Debug, Info, Warn, Error };

    void Load(const std::string& configPath);

    // [manifest]
    inline ManifestUrl manifestUrl = ManifestUrl::Wudrm;
    inline DWORD manifestTimeoutResolve = 5000;
    inline DWORD manifestTimeoutConnect = 5000;
    inline DWORD manifestTimeoutSend    = 10000;
    inline DWORD manifestTimeoutRecv    = 10000;

    // [log]
    inline LogLevel logLevel = LogLevel::Debug;

    // derived from configPath: <steam>/opensteamtool/
    inline std::string logDir;

    // [lua]
    inline std::vector<std::string> luaPaths;

    // [pattern]
    // Base URL for the per-DLL pattern TOML files. Final URL =
    //   <patternMirror>/<ghSubdir>/<sha256>.toml
    // Empty → built-in default (raw.githubusercontent.com). Users in regions
    // where the default is blocked or slow can point this at a mirror.
    inline std::string patternMirror;

}
