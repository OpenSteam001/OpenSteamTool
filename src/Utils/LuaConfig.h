#ifndef LUACONFIG_H
#define LUACONFIG_H

#include <cstdint>

namespace LuaConfig{
    bool HasDepot(AppId_t appId);
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
    // depotId → {gid, size}
    const std::unordered_map<uint64_t, ManifestOverride>& GetManifestOverrides();

    void ParseDirectory(const std::string& directory);

    // manifest.lua — fetch_manifest_code(gid) -> uint64 | nil
    // Returns true if manifest.lua defined a fetch_manifest_code function.
    bool HasManifestCodeFunc();
    // Call the Lua fetch_manifest_code(gid). Returns false if no function or
    // it returned nil.
    bool CallManifestFetchCode(uint64_t gid, uint64_t* outCode);
}

#endif // LUACONFIG_H
