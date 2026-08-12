#ifndef LUACONFIG_H
#define LUACONFIG_H

#include "Steam/Types.h"

#include <cstdint>
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

    bool HasManifestCodeFunc();
    bool CallManifestFetchCode(uint64_t gid, uint64_t* outCode);

    bool HasManifestCodeFuncEx();
    bool CallManifestFetchCodeEx(uint64_t app_id, uint64_t depot_id, uint64_t gid, uint64_t* outCode);

    // Returns the appid configured for a process exe name via addprocess(), or
    // k_uAppIdInvalid if none. Used by PipeManager to identify games that don't
    // export SteamAppId (e.g. launcher-spawned child processes).
    AppId_t GetAppIdForProcess(const std::string& imageName);

    // Returns true if the appid was marked via forcedenuvo(), bypassing
    // ProtectionScan in DenuvoAuth (for games where the heuristic fails).
    bool IsForcedDenuvo(AppId_t appId);

    // On-demand eticket backend URL set via seteticketurl() in Lua config.
    // Empty string means the feature is disabled and EticketClient falls
    // back to the static credential-store ticket (original behaviour).
    const std::string& GetEticketUrl();
}

#endif // LUACONFIG_H
