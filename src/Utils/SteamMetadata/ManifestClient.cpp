#include "ManifestClient.h"
#include "OSTPlatform/include/Http.h"
#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"
#include "Utils/Logging/Log.h"

#include <algorithm>
#include <charconv>
#include <mutex>
#include <string_view>

namespace ManifestClient {

    // ── parsers ────────────────────────────────────────────────────
    using Parser = bool (*)(std::string_view body, uint64_t* out);

    static bool ParsePlainUint(std::string_view body, uint64_t* out) {
        uint64_t code = 0;
        auto [_, ec] = std::from_chars(body.data(), body.data() + body.size(), code);
        if (ec != std::errc{}) return false;
        *out = code;
        return true;
    }

    static bool ParseSteamRunJson(std::string_view body, uint64_t* out) {
        size_t key = body.find("\"content\"");
        if (key == std::string_view::npos) return false;
        size_t q1 = body.find('"', key + 9);
        if (q1 == std::string_view::npos) return false;
        size_t q2 = body.find('"', q1 + 1);
        if (q2 == std::string_view::npos) return false;
        return ParsePlainUint(body.substr(q1 + 1, q2 - q1 - 1), out);
    }

    // ── provider table ────────────────────────────────────────────
    //
    // Adding a new provider: add one row to kProviders below.
    // host / port / tls / path are all derived from the URL template
    // by Make() at compile time.

    struct Provider {
        std::string_view name;          // matches [manifest] url = "..."
        const char*      urlTemplate;   // full literal with one %llu — for log & path
        Parser           parse;
        const wchar_t*   headers;
    };

    consteval Provider Make(std::string_view name, const char* url, Parser parse,
                            const wchar_t* headers = nullptr) {
        return {name, url, parse, headers};
    }

    static constexpr Provider kProviders[] = {
        Make("opensteamtool", "https://manifest.opensteamtool.com/%llu",       ParsePlainUint),
        Make("manifestdex",   "https://manifest.manifestdex.com/%llu",         ParsePlainUint,
             L"User-Agent: ManifestDeX/1.0\r\n"),
        Make("wudrm",         "http://gmrc.wudrm.com/manifest/%llu",           ParsePlainUint),
        Make("steamrun",      "https://manifest.steam.run/api/manifest/%llu",  ParseSteamRunJson),
    };

    static const Provider* g_active = &kProviders[0];   // opensteamtool
    static std::mutex      g_mutex;

    bool SetProvider(std::string_view name) {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& p : kProviders)
            if (p.name == name) { 
                g_active = &p; 
                return true; 
            }
        return false;
    }

    const char* ActiveProviderName() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_active->name.data(); 
    }

    // ── request ───────────────────────────────────────────────────

    void Shutdown() {
        std::lock_guard<std::mutex> lock(g_mutex);
    }

    // ── fetch ─────────────────────────────────────────────────────

    static bool FetchActive(uint64_t gid, uint64_t* outCode) {
        const Provider& p = *g_active;
        const Config::ManifestTimeouts timeouts = Config::GetManifestTimeouts();

        char urlLog[256];
        std::snprintf(urlLog, sizeof(urlLog), p.urlTemplate, gid);

        auto r = OSTPlatform::Http::Execute(
            L"GET",
            urlLog,
            nullptr,
            0,
            p.headers,
            timeouts.resolve,
            timeouts.connect,
            timeouts.send,
            timeouts.recv);

        LOG_MANIFEST_INFO("Manifest {} status={} gid={}", p.name, r.status, gid);

        if (!r.ok || r.status != 200) return false;
        return p.parse(r.body, outCode);
    }

    // ── public ────────────────────────────────────────────────────

    bool FetchManifestRequestCode(uint64_t manifestGid, uint64_t* outRequestCode,
                                  AppId_t appId, AppId_t depotId)
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (appId && depotId && LuaConfig::HasManifestCodeFuncEx()) {
            if (LuaConfig::CallManifestFetchCodeEx(appId, depotId, manifestGid, outRequestCode)) {
                LOG_MANIFEST_INFO("Manifest gid={} resolved via fetch_manifest_code_ex", manifestGid);
                return true;
            }
            LOG_MANIFEST_WARN("Manifest gid={} fetch_manifest_code_ex returned nil, trying fetch_manifest_code", manifestGid);
        }

        if (LuaConfig::HasManifestCodeFunc()) {
            if (LuaConfig::CallManifestFetchCode(manifestGid, outRequestCode)) {
                LOG_MANIFEST_INFO("Manifest gid={} resolved via manifest.lua", manifestGid);
                return true;
            }
            LOG_MANIFEST_WARN("Manifest gid={} lua returned nil, falling back to config", manifestGid);
        }

        return FetchActive(manifestGid, outRequestCode);
    }
}
