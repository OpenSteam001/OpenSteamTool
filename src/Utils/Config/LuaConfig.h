#ifndef LUACONFIG_H
#define LUACONFIG_H

#include "Steam/Types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace LuaConfig{
    bool HasDepot(AppId_t appId, bool checkOwned=true);
    bool IsOwned(AppId_t appId);
    void MarkOwned(AppId_t appId);
    std::vector<AppId_t> GetAllDepotIds();
    std::vector<uint8> GetDecryptionKey(AppId_t appId);
    uint64_t GetAccessToken(AppId_t appId);
    // Legacy third-party CD key override for an app, if the user set one via
    // setlegacycdkey(appid, "KEY"); nullopt when no override is configured.
    std::optional<std::string> GetLegacyCDKey(AppId_t appId);
    uint64_t GetStatSteamId(AppId_t appId);
    bool pinApp(AppId_t appId);
    uint32_t GetPurchaseTime(AppId_t appId);

    struct ManifestOverride {
          uint64_t gid;
          uint64_t size;
    };
    const std::unordered_map<uint64_t, ManifestOverride>& GetManifestOverrides();

    void ParseFile(const std::string& filePath);
    void UnloadFile(const std::string& filePath);
    // Returns and clears the list of depot IDs removed/added since last call.
    std::vector<AppId_t> TakePendingRemovals();
    std::vector<AppId_t> TakePendingAdditions();
    void ParseDirectory(const std::string& directory);
    void ReloadDirectories(const std::vector<std::string>& directories, bool clearPendingAdditions = false);

    // Merge the user-configured Lua directories with the built-in default, dropping any
    // entry that resolves to the same filesystem location (so a relative config entry
    // like "config/stplug-in" that equals the absolute default folder is not loaded
    // twice). Configured paths keep their order; the default is appended only if new.
    std::vector<std::string> MergeWatchDirs(const std::vector<std::string>& configured,
                                            const std::string& defaultDir);

    bool HasManifestCodeFunc();
    bool CallManifestFetchCode(uint64_t gid, uint64_t* outCode);

    bool HasManifestCodeFuncEx();
    bool CallManifestFetchCodeEx(uint64_t app_id, uint64_t depot_id, uint64_t gid, uint64_t* outCode);
}

#endif // LUACONFIG_H
