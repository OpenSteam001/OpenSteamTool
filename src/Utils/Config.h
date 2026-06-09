#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace Config {

    enum class LogLevel { Trace, Debug, Info, Warn, Error };

    void Load(const std::string& configPath);

    // [manifest] — provider selection lives in ManifestClient (table-driven).
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

    // [remote]
    inline std::string remoteUrlTemplate;

}
