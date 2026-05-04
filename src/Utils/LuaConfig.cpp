#include "dllmain.h"
#include "LuaConfig.h"
#include "WinHttp.h"
#include "Utils/AppTicket.h"
#include <lua.hpp>
#include <cctype>
#include <fstream>
#include <string>

extern "C" {
    struct lua_State;
}

namespace LuaConfig{
    static lua_State* g_lua_state = nullptr;
    static bool g_hasManifestCodeFunc = false;
    std::unordered_map<AppId_t, std::string>DepotKeySet{};
    std::unordered_map<AppId_t, uint64_t>AccessTokenSet{};
    std::unordered_set<AppId_t> PinnedApps{};
    std::unordered_map<uint64_t, ManifestOverride> ManifestOverrides{};
    std::unordered_map<AppId_t, uint64_t> StatSteamIdSet{};
    std::unordered_set<AppId_t> OwnedAppIdSet{};
    constexpr uint64_t kDefaultStatSteamId = 76561198028121353ULL;

    // Case-insensitive function registry: lowercase name → C function
    static std::unordered_map<std::string, lua_CFunction> g_func_registry;

    // ── Lua HTTP helpers ──────────────────────────────────────────
    //   http_get(url [, headers]) → body, status_code
    //     headers: optional table, e.g. {["Accept"]="application/json"}
    //
    //   http_post(url, post_body [, headers]) → body, status_code
    //     post_body: string payload
    // ────────────────────────────────────────────────────────────────

    // Serialise a Lua {key=val, ...} table at stack index idx into a
    // wstring of "Key: Val\r\n" lines. Returns L"" if no table.
    static std::wstring LuaHeadersToWstr(lua_State* L, int idx) {
        std::wstring headers;
        if (idx < 1 || !lua_istable(L, idx)) return headers;
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            std::string key(lua_tostring(L, -2));
            std::string val(lua_tostring(L, -1));
            headers += std::wstring(key.begin(), key.end())
                    + L": " + std::wstring(val.begin(), val.end()) + L"\r\n";
            lua_pop(L, 1);
        }
        return headers;
    }

    static int lua_http_get(lua_State* L) {
        // http_get(url [, headers]) → body, status_code
        auto hdrs = LuaHeadersToWstr(L, lua_gettop(L) >= 2 ? 2 : -1);
        auto r = WinHttp::Execute(L"GET", luaL_checkstring(L, 1),
                                  nullptr, 0, hdrs.empty() ? nullptr : hdrs.c_str());
        if (r.ok) {
            lua_pushstring(L, r.body.c_str());
            lua_pushinteger(L, r.status);
        } else {
            lua_pushnil(L);
            lua_pushstring(L, "HTTP request failed");
        }
        return 2;
    }

    static int lua_http_post(lua_State* L) {
        // http_post(url, post_body [, headers]) → body, status_code
        size_t bodyLen = 0;
        const char* body = luaL_checklstring(L, 2, &bodyLen);
        auto hdrs = LuaHeadersToWstr(L, lua_gettop(L) >= 3 ? 3 : -1);
        auto r = WinHttp::Execute(L"POST", luaL_checkstring(L, 1),
                                  body, static_cast<DWORD>(bodyLen),
                                  hdrs.empty() ? nullptr : hdrs.c_str());
        if (r.ok) {
            lua_pushstring(L, r.body.c_str());
            lua_pushinteger(L, r.status);
        } else {
            lua_pushnil(L);
            lua_pushstring(L, "HTTP request failed");
        }
        return 2;
    }

    // ── Case-insensitive global function lookup ─────────────────
    // __index metamethod on _G: when a global name isn't found,
    // lower-case the name and look it up in g_func_registry.
    static int case_insensitive_global_index(lua_State* L) {
        const char* name = lua_tostring(L, 2);
        if (!name) {
            lua_pushnil(L);
            return 1;
        }
        std::string lower;
        for (const char* p = name; *p; ++p) {
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
        }
        auto it = g_func_registry.find(lower);
        if (it != g_func_registry.end()) {
            lua_pushcfunction(L, it->second);
            return 1;
        }
        lua_pushnil(L);
        return 1;
    }

    // Register a C function both in _G (lowercase name) and in the
    // case-insensitive lookup table so any case variant resolves.
    static void register_func(lua_State* L, const char* lowercase_name, lua_CFunction fn) {
        g_func_registry[lowercase_name] = fn;
        lua_pushcfunction(L, fn);
        lua_setglobal(L, lowercase_name);
    }

    // ── Lua: addappid / addtoken / pinApp / setManifestid ────────
    static int lua_addappid(lua_State* L) {
        // addappid(integer, integer, string)
        int argc = lua_gettop(L);
        // Validate argument count and required argument types.
        if (argc == 0) {
            return luaL_error(L, "");
        }
        if (!lua_isinteger(L, 1)) {
            return luaL_error(L, "");
        }

        // Read the first argument as app/depot id.
        lua_Integer value = lua_tointeger(L, 1);
        // Ensure the value fits into uint32_t range.
        if (value < 0 || value > UINT32_MAX)
            return luaL_error(L, "");
        AppId_t DepotId = (uint32_t)value;
        // Read the optional third argument as a key.
        std::string Key = "";
        if (argc > 2) {
            if (!lua_isstring(L, 3))
                return luaL_error(L, "");
            const char* key = lua_tostring(L, 3);
            // Keep only keys with exactly 64 characters.
            if (strlen(key) == 64) {
                Key = std::string(key);
            }
        }
        // Non-empty keys have priority over existing empty keys.
        if (!Key.empty() || !DepotKeySet.count(DepotId)) {
            DepotKeySet[DepotId] = Key;
        }

        return 0;
    }

    static int lua_addtoken(lua_State* L) {
        // addtoken(integer, string(uint64_t))
        int argc = lua_gettop(L);
        // Validate argument count and required argument types.
        if (argc == 0) {
            return luaL_error(L, "");
        }
        if (!lua_isinteger(L, 1)) {
            return luaL_error(L, "");
        }

        // Read the first argument as app/depot id.
        lua_Integer value = lua_tointeger(L, 1);
        // Ensure the value fits into uint32_t range.
        if (value < 0 || value > UINT32_MAX)
            return luaL_error(L, "");
        AppId_t AppId = (uint32_t)value;
        // Read the second argument as a token.
        if (argc > 1) {
            if (!lua_isstring(L, 2))
                return luaL_error(L, "");
            const char* token = lua_tostring(L, 2);
            // Convert the string token to a uint64_t value.
            if(!std::all_of(token, token + strlen(token), ::isdigit)) {
                return luaL_error(L, "");
            }
            AccessTokenSet[AppId] = std::stoull(token);
        }

        return 0;
    }

    static int lua_pinApp(lua_State* L) {
        // pinApp(integer)
        int argc = lua_gettop(L);
        // Validate argument count and required argument types.
        if (argc == 0) {
            return luaL_error(L, "");
        }
        if (!lua_isinteger(L, 1)) {
            return luaL_error(L, "");
        }

        // Read the first argument as appid.
        lua_Integer value = lua_tointeger(L, 1);
        // Ensure the value fits into uint32_t range.
        if (value < 0 || value > UINT32_MAX)
            return luaL_error(L, "");
        AppId_t AppId = (uint32_t)value;

        PinnedApps.insert(AppId);

        return 0;
    }

    static int lua_setManifestid(lua_State* L)
    {
        // setManifestid(depotId, gid_string [, size])
        // size is always forced to 0 to prevent incorrect size from breaking Steam.
        int argc = lua_gettop(L);
        if (argc < 2)
            return luaL_error(L, "setManifestid: need depotId, gid");

        if (!lua_isinteger(L, 1))
            return luaL_error(L, "setManifestid: depotId must be integer");
        if (!lua_isstring(L, 2))
            return luaL_error(L, "setManifestid: gid must be decimal string");

        lua_Integer val = lua_tointeger(L, 1);
        if (val < 0 || val > UINT32_MAX)
            return luaL_error(L, "setManifestid: depotId out of range");

        uint64_t depotId = (uint64_t)(uint32_t)val;
        const char* gidStr = lua_tostring(L, 2);

        if (!std::all_of(gidStr, gidStr + strlen(gidStr), ::isdigit))
            return luaL_error(L, "setManifestid: gid must be all digits");

        ManifestOverrides[depotId] = { std::stoull(gidStr), 0 };
        return 0;
    }

    // ── Lua: setAppTicket / setETicket ──────────────────────────
    static int lua_setAppticket(lua_State* L) {
        int argc = lua_gettop(L);
        if (argc < 2)
            return luaL_error(L, "setAppTicket: need appId and hex string");
        if (!lua_isinteger(L, 1))
            return luaL_error(L, "setAppTicket: appId must be integer");
        if (!lua_isstring(L, 2))
            return luaL_error(L, "setAppTicket: ticket must be hex string");

        lua_Integer val = lua_tointeger(L, 1);
        if (val < 0 || val > UINT32_MAX)
            return luaL_error(L, "setAppTicket: appId out of range");
        AppId_t appId = static_cast<uint32_t>(val);

        size_t hexLen;
        const char* hex = lua_tolstring(L, 2, &hexLen);

        std::vector<uint8_t> binary;
        binary.reserve((hexLen + 1) / 2);
        for (size_t i = 0; i < hexLen; i += 2) {
            char byteStr[3] = {
                hex[i],
                i + 1 < hexLen ? hex[i + 1] : '0',
                '\0'
            };
            binary.push_back(static_cast<uint8_t>(strtoul(byteStr, nullptr, 16)));
        }

        if (!AppTicket::WriteAppOwnershipTicket(appId, binary))
            return luaL_error(L, "setAppTicket: failed to write to registry");

        return 0;
    }

    static int lua_setEticket(lua_State* L) {
        int argc = lua_gettop(L);
        if (argc < 2)
            return luaL_error(L, "setETicket: need appId and hex string");
        if (!lua_isinteger(L, 1))
            return luaL_error(L, "setETicket: appId must be integer");
        if (!lua_isstring(L, 2))
            return luaL_error(L, "setETicket: ticket must be hex string");

        lua_Integer val = lua_tointeger(L, 1);
        if (val < 0 || val > UINT32_MAX)
            return luaL_error(L, "setETicket: appId out of range");
        AppId_t appId = static_cast<uint32_t>(val);

        size_t hexLen;
        const char* hex = lua_tolstring(L, 2, &hexLen);

        std::vector<uint8_t> binary;
        binary.reserve((hexLen + 1) / 2);
        for (size_t i = 0; i < hexLen; i += 2) {
            char byteStr[3] = {
                hex[i],
                i + 1 < hexLen ? hex[i + 1] : '0',
                '\0'
            };
            binary.push_back(static_cast<uint8_t>(strtoul(byteStr, nullptr, 16)));
        }

        if (!AppTicket::WriteEncryptedTicket(appId, binary))
            return luaL_error(L, "setETicket: failed to write to registry");

        return 0;
    }

    
    // ── Lua: setStat ────────────────────────────────────────────
    static int lua_setStat(lua_State* L) {
        // setStat(appid, "steamid")
        int argc = lua_gettop(L);
        if (argc < 2)
            return luaL_error(L, "setStat: need appId and steamId string");
        if (!lua_isinteger(L, 1))
            return luaL_error(L, "setStat: appId must be integer");
        if (!lua_isstring(L, 2))
            return luaL_error(L, "setStat: steamId must be string");

        lua_Integer val = lua_tointeger(L, 1);
        if (val < 0 || val > UINT32_MAX)
            return luaL_error(L, "setStat: appId out of range");
        AppId_t appId = static_cast<uint32_t>(val);

        const char* sidStr = lua_tostring(L, 2);
        if (!std::all_of(sidStr, sidStr + strlen(sidStr), ::isdigit))
            return luaL_error(L, "setStat: steamId must be all digits");

        StatSteamIdSet[appId] = std::stoull(sidStr);
        return 0;
    }

    // ── init / cleanup ───────────────────────────────────────────
    static bool Initialize() {
        if (g_lua_state)
            return true;
        g_lua_state = luaL_newstate();
        if (!g_lua_state)
            return false;
        // Load standard Lua libraries.
        luaL_openlibs(g_lua_state);

        // Set up case-insensitive global lookup via __index on _G's metatable.
        lua_getglobal(g_lua_state, "_G");
        if (!lua_getmetatable(g_lua_state, -1)) {
            lua_newtable(g_lua_state);
        }
        lua_pushcfunction(g_lua_state, case_insensitive_global_index);
        lua_setfield(g_lua_state, -2, "__index");
        lua_setmetatable(g_lua_state, -2);
        lua_pop(g_lua_state, 1);  // pop _G

        // Register custom helper functions for scripts.
        // All functions use register_func() so any case variant works
        // (e.g. setAppTICKET, addAppId, SETManifestid, etc.).
        register_func(g_lua_state, "addappid", lua_addappid);
        register_func(g_lua_state, "addtoken", lua_addtoken);
        // we don't need it?
        // register_func(g_lua_state, "pinapp", lua_pinApp);
        register_func(g_lua_state, "setmanifestid", lua_setManifestid);
        register_func(g_lua_state, "http_get", lua_http_get);
        register_func(g_lua_state, "http_post", lua_http_post);
        register_func(g_lua_state, "setappticket", lua_setAppticket);
        register_func(g_lua_state, "seteticket", lua_setEticket);
        register_func(g_lua_state, "setstat", lua_setStat);
        return true;
    }

    static void Cleanup() {
        if (g_lua_state) {
            lua_close(g_lua_state);
            g_lua_state = nullptr;
        }
        g_hasManifestCodeFunc = false;
    }

    // ── public query API ─────────────────────────────────────────
    bool HasDepot(AppId_t DepotId) {
        return DepotKeySet.count(DepotId) && !OwnedAppIdSet.count(DepotId);
    }

    void MarkOwned(AppId_t AppId) {
        if(!OwnedAppIdSet.count(AppId)) {
            LOG_INFO("Marking app {} as owned", AppId);
            OwnedAppIdSet.insert(AppId);
        }
    }

    std::vector<AppId_t> GetAllDepotIds() {
        std::vector<AppId_t> DepotIds;
        for (const auto& pair : DepotKeySet) {
            DepotIds.push_back(pair.first);
        }
        return DepotIds;
    }

    std::vector<uint8> GetDecryptionKey(AppId_t DepotId) {
        std::vector<uint8> keyBytes;
        if (DepotKeySet.count(DepotId)) {
            const std::string& keyStr = DepotKeySet[DepotId];
            // Convert hex string to byte vector.
            for (size_t i = 0; i < keyStr.length(); i += 2) {
                std::string byteString = keyStr.substr(i, 2);
                uint8 byte = (uint8)strtoul(byteString.c_str(), nullptr, 16);
                keyBytes.push_back(byte);
            }
        }
        return keyBytes;
    }

    uint64_t GetAccessToken(AppId_t AppId) {
        if (AccessTokenSet.count(AppId)) {
            return AccessTokenSet[AppId];
        }
        return 0;
    }

    bool pinApp(AppId_t AppId) {
        return PinnedApps.count(AppId);
    }

    uint64_t GetStatSteamId(AppId_t AppId) {
        if (StatSteamIdSet.count(AppId))
            return StatSteamIdSet[AppId];
        return kDefaultStatSteamId;
    }

    const std::unordered_map<uint64_t, ManifestOverride>& GetManifestOverrides() {
      return ManifestOverrides;
    }

    bool HasManifestCodeFunc() {
        return g_hasManifestCodeFunc;
    }

    bool CallManifestFetchCode(uint64_t gid, uint64_t* outCode) {
        if (!g_hasManifestCodeFunc || !g_lua_state)
            return false;

        lua_getglobal(g_lua_state, "fetch_manifest_code");
        lua_pushinteger(g_lua_state, static_cast<lua_Integer>(gid));

        if (lua_pcall(g_lua_state, 1, 1, 0) != LUA_OK) {
            LOG_MANIFEST_WARN("fetch_manifest_code({}) error: {}", gid,
                             lua_tostring(g_lua_state, -1));
            lua_pop(g_lua_state, 1);
            return false;
        }

        // nil → fallback to config url (normal, not an error)
        if (lua_isnil(g_lua_state, -1)) {
            LOG_MANIFEST_WARN("fetch_manifest_code({}) returned nil", gid);
            lua_pop(g_lua_state, 1);
            return false;
        }

        // The function must return a digit string (uint64 as decimal).
        // tonumber() loses precision for values > 2^53; string is safe.
        if (lua_isinteger(g_lua_state, -1)) {
            *outCode = static_cast<uint64_t>(lua_tointeger(g_lua_state, -1));
        } else if (lua_isstring(g_lua_state, -1)) {
            const char* s = lua_tostring(g_lua_state, -1);
            if (!std::all_of(s, s + strlen(s), ::isdigit)) {
                LOG_MANIFEST_WARN("fetch_manifest_code({}) returned non-numeric string '{}'",
                                 gid, s);
                lua_pop(g_lua_state, 1);
                return false;
            }
            *outCode = std::stoull(s);
        } else {
            LOG_MANIFEST_WARN("fetch_manifest_code({}) unexpected type (expected digit-string)",
                             gid);
            lua_pop(g_lua_state, 1);
            return false;
        }

        LOG_MANIFEST_INFO("fetch_manifest_code({}) = {}", gid, *outCode);
        lua_pop(g_lua_state, 1);
        return true;
    }

    // ── directory scanner ────────────────────────────────────────
    void ParseDirectory(const std::string& directory) {
        if (!Initialize()) return;

        std::error_code ec;
        if (!std::filesystem::exists(directory, ec))
            std::filesystem::create_directories(directory, ec);
        if (!std::filesystem::exists(directory, ec) || !std::filesystem::is_directory(directory, ec))
            return;

        for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            const auto& path = entry.path();
            if (path.extension() != ".lua") continue;

            std::ifstream file(path);
            if (!file) continue;

            std::string chunk, line;
            int lineNo = 0;
            while (std::getline(file, line)) {
                ++lineNo;
                if (!chunk.empty()) chunk += '\n';
                chunk += line;

                // try to compile the accumulated chunk; if it fails with
                // a syntax error the statement is likely incomplete —
                // keep accumulating lines
                lua_settop(g_lua_state, 0);
                int rc = luaL_loadstring(g_lua_state, chunk.c_str());
                if (rc == LUA_OK) {
                    // valid chunk — execute it
                    if (lua_pcall(g_lua_state, 0, 0, 0) != LUA_OK) {
                        const char* err = lua_tostring(g_lua_state, -1);
                        LOG_WARN("{}:{}: {}", path.filename().string(), lineNo,
                                 err ? err : "unknown");
                    }
                    chunk.clear();
                } else if (rc == LUA_ERRSYNTAX) {
                    // incomplete — keep accumulating
                    lua_pop(g_lua_state, 1);
                } else {
                    // memory error etc. — give up on this chunk
                    const char* err = lua_tostring(g_lua_state, -1);
                    LOG_WARN("{}:{}: {}", path.filename().string(), lineNo, err ? err : "unknown");
                    lua_pop(g_lua_state, 1);
                    chunk.clear();
                }
            }
            // trailing incomplete chunk (shouldn't happen normally)
            if (!chunk.empty()) {
                LOG_WARN("{}: incomplete statement at end of file", path.filename().string());
            }
        }

        // After parsing all .lua files, check for manifest.lua's
        // fetch_manifest_code function. Lua state stays alive so we
        // can call it lazily from the manifest hook.
        g_hasManifestCodeFunc = false;
        lua_getglobal(g_lua_state, "fetch_manifest_code");
        if (lua_isfunction(g_lua_state, -1)) {
            g_hasManifestCodeFunc = true;
            LOG_INFO("manifest.lua: fetch_manifest_code found");
        }
        lua_pop(g_lua_state, 1);

        // Intentionally do NOT call Cleanup() — Lua state must stay
        // alive for CallManifestFetchCode queries during runtime.
    }

}
