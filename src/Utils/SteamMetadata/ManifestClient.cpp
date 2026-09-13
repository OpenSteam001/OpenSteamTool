#include "ManifestClient.h"
#include "OSTPlatform/include/Http.h"
#include "OSTPlatform/include/Numbers.h"
#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"
#include "Utils/Logging/Log.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

namespace ManifestClient {

    // ── parsers ────────────────────────────────────────────────────
    using Parser = bool (*)(std::string_view body, uint64_t* out);

    static bool ParsePlainUint(std::string_view body, uint64_t* out) {
        const size_t end = body.find_last_not_of(" \t\r\n");
        if (end == std::string_view::npos) return false;
        const auto code = OSTPlatform::Numbers::ParseUInt64(body.substr(0, end + 1));
        if (!code) return false;
        *out = *code;
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
    // Built-in providers below; anything else in [manifest] url is used
    // as a custom URL template via SetCustomProvider, no code change needed.

    struct Provider {
        std::string_view name;          // matches [manifest] url = "..."
        const char*      urlTemplate;   // %llu (built-in) or {gid} (custom)
        Parser           parse;
    };

    consteval Provider Make(std::string_view name, const char* url, Parser parse) {
        return {name, url, parse};
    }

    static constexpr Provider kProviders[] = {
        Make("opensteamtool", "https://manifest.opensteamtool.com/%llu",       ParsePlainUint),
        Make("wudrm",         "http://gmrc.wudrm.com/manifest/%llu",           ParsePlainUint),
        Make("steamrun",      "https://manifest.steam.run/api/manifest/%llu",  ParseSteamRunJson),
    };

    static const Provider* g_active = &kProviders[0];
    static_assert(kProviders[0].name == kDefaultProviderName);
    static std::string     g_customUrl;
    static Provider        g_custom = {"custom", nullptr, ParsePlainUint};
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

    static bool IsCustomTemplate(std::string_view url) {
        if (url.empty() || url.size() > 512) return false;
        std::string_view rest;
        if (url.starts_with("https://")) rest = url.substr(8);
        else if (url.starts_with("http://")) rest = url.substr(7);
        else return false;
        if (rest.find("{gid}") == std::string_view::npos) return false;
        // Same authority rules Http::Execute enforces: expand the placeholder,
        // then require a non-empty host and a valid port.
        std::string expanded(rest);
        for (size_t pos = 0; (pos = expanded.find("{gid}", pos)) != std::string::npos;)
            expanded.replace(pos, 5, "0");
        const size_t slash = expanded.find('/');
        const std::string_view hostPart(expanded.data(), slash == std::string::npos ? expanded.size() : slash);
        const size_t colon = hostPart.find(':');
        if (hostPart.substr(0, colon).empty()) return false;
        for (const char c : hostPart.substr(0, colon))
            if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f) return false;
        if (colon != std::string_view::npos) {
            const auto port = OSTPlatform::Numbers::ParseUInt32(hostPart.substr(colon + 1));
            if (!port || *port == 0 || *port > 65535) return false;
        }
        return true;
    }

    static Parser ParserFor(std::string_view format) {
        if (format == "steamrun") return ParseSteamRunJson;
        return ParsePlainUint;
    }

    bool SetCustomProvider(std::string_view urlTemplate, std::string_view format) {
        if (!IsCustomTemplate(urlTemplate)) return false;
        if (format != "plain" && format != "steamrun")
            LOG_WARN("Unknown manifest.format \"{}\", using plain", format);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_customUrl.assign(urlTemplate);
        g_custom = {"custom", g_customUrl.c_str(), ParserFor(format)};
        g_active = &g_custom;
        return true;
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

        std::string url;
        if (g_active == &g_custom) {
            url.assign(p.urlTemplate);
            const std::string id = std::to_string(gid);
            for (size_t pos = 0; (pos = url.find("{gid}", pos)) != std::string::npos;)
                url.replace(pos, 5, id);
        } else {
            char urlLog[256];
            std::snprintf(urlLog, sizeof(urlLog), p.urlTemplate, gid);
            url.assign(urlLog);
        }

        auto r = OSTPlatform::Http::Execute(
            L"GET",
            url.c_str(),
            nullptr,
            0,
            nullptr,
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
