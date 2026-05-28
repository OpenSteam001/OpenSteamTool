#ifndef LUACONFIG_H
#define LUACONFIG_H

#include <cstdint>
#include <unordered_map>
#include <string>
#include <vector>

namespace LuaConfig{
    bool HasDepot(AppId_t appId, bool checkOwned=true);
    void MarkOwned(AppId_t appId);
    std::vector<AppId_t> GetAllDepotIds();
    std::vector<uint8> GetDecryptionKey(AppId_t appId);
    uint64_t GetAccessToken(AppId_t appId);
    uint64_t GetStatSteamId(AppId_t appId);
    bool pinApp(AppId_t appId);

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

    bool HasManifestCodeFunc();
    bool CallManifestFetchCode(uint64_t gid, uint64_t* outCode);

    bool HasManifestCodeFuncEx();
    bool CallManifestFetchCodeEx(uint64_t app_id, uint64_t depot_id, uint64_t gid, uint64_t* outCode);
}

#endif // LUACONFIG_H
