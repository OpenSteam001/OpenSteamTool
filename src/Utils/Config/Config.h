#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "Steam/Types.h"

namespace Config {

    enum class LogLevel { Trace, Debug, Info, Warn, Error };

    struct ManifestTimeouts {
        uint32_t resolve = 5000;
        uint32_t connect = 5000;
        uint32_t send    = 10000;
        uint32_t recv    = 10000;
    };

    // [[inject]] entry: a DLL loaded into a matching game process at the IPC handshake.
    struct InjectDll {
        std::string                 path;        // resolved absolute path
        std::string                 whenCmdline; // substring required in the game command line
        std::unordered_set<AppId_t> whenAppids;  // appids this entry applies to
        bool                        allGames = false;  // false: only Lua-unlocked games
    };

    struct CloudSettings {
        bool enabled = false;
        std::string library;
    };

    struct LoadResult {
        bool applied = false;
        bool luaPathsChanged = false;
    };

    LoadResult Load(const std::string& configPath);

    ManifestTimeouts GetManifestTimeouts();
    LogLevel GetLogLevel();
    std::string GetLogDir();
    std::vector<std::string> GetLuaPaths();
    std::string GetRemoteUrlTemplate();
    CloudSettings GetCloudSettings();
    bool GetStatsEnableApi();

    // [manifest] — provider selection lives in ManifestClient (table-driven).
    inline uint32_t manifestTimeoutResolve = 5000;
    inline uint32_t manifestTimeoutConnect = 5000;
    inline uint32_t manifestTimeoutSend    = 10000;
    inline uint32_t manifestTimeoutRecv    = 10000;

    // [log]
    inline LogLevel logLevel = LogLevel::Debug;

    // derived from configPath: <steam>/opensteamtool/
    inline std::string logDir;

    // [lua]
    inline std::vector<std::string> luaPaths;

    // [remote]
    inline std::string remoteUrlTemplate;

    // [stats]
    inline bool statsEnableApi = true;

    // [[inject]] - optional DLL injection into matching game processes.
    inline std::vector<InjectDll> injectDlls;

    // [cloud] - optional Steam Cloud save redirection via CloudRedirect.
    inline bool cloudEnabled = false;
    inline std::string cloudLibrary;

}
