#include "Hooks_Manifest.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "Utils/WinHttp.h"
#include <charconv>

// ═══════════════════════════════════════════════════════════════════
//  Manifest request-code resolution.
//  Codes rotate over time — cannot be cached.
//
//  For games with many DLCs (150+), Steam calls GetManifestRequestCode
//  serially for every depot.  We keep a persistent WinHTTP session +
//  connection so that repeated requests to the same manifest provider
//  skip DNS / TCP / TLS setup after the first call.
// ═══════════════════════════════════════════════════════════════════
namespace {

    // ── persistent connection state ───────────────────────────────
    HINTERNET g_hSession = nullptr;
    HINTERNET g_hConnect = nullptr;
    bool      g_tls      = false;

    void EnsureConnection(const wchar_t* host, INTERNET_PORT port, bool tls) {
        // Already connected to the right host — reuse
        if (g_hSession && g_hConnect)
            return;

        // Clean up stale handles
        if (g_hConnect) { WinHttpCloseHandle(g_hConnect); g_hConnect = nullptr; }
        if (g_hSession) { WinHttpCloseHandle(g_hSession); g_hSession = nullptr; }

        g_tls = tls;
        g_hSession = WinHttpOpen(L"OpenSteamTool/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g_hSession) return;

        WinHttpSetTimeouts(g_hSession,
            Config::manifestTimeoutResolve,
            Config::manifestTimeoutConnect,
            Config::manifestTimeoutSend,
            Config::manifestTimeoutRecv);

        g_hConnect = WinHttpConnect(g_hSession, host, port, 0);
        if (!g_hConnect) {
            WinHttpCloseHandle(g_hSession);
            g_hSession = nullptr;
        }
    }

    void CloseConnection() {
        if (g_hConnect) { WinHttpCloseHandle(g_hConnect); g_hConnect = nullptr; }
        if (g_hSession) { WinHttpCloseHandle(g_hSession); g_hSession = nullptr; }
    }

    // Try ExecuteEx on the persistent connection; on failure reset
    // the connection so the next call reconnects.
    WinHttp::Result DoGet(const wchar_t* path, const char* urlForLog) {
        auto r = WinHttp::ExecuteEx(g_hSession, g_hConnect, g_tls,
                                    L"GET", path, nullptr, 0, nullptr,
                                    urlForLog);
        if (!r.ok)
            CloseConnection();
        return r;
    }

    // ── HTTP providers ────────────────────────────────────────────

    // GET https://manifest.steam.run/api/manifest/{gid}
    // Response: {"content":"1666836470726104466"}
    bool FetchSteamRun(uint64 manifest_gid, uint64* outRequestCode) {
        EnsureConnection(L"manifest.steam.run", INTERNET_DEFAULT_HTTPS_PORT, true);
        if (!g_hConnect) return false;

        wchar_t path[80];
        swprintf_s(path, L"/api/manifest/%llu", manifest_gid);

        char urlForLog[128];
        snprintf(urlForLog, sizeof(urlForLog), "https://manifest.steam.run/api/manifest/%llu", manifest_gid);

        auto r = DoGet(path, urlForLog);
        LOG_MANIFEST_INFO("Manifest steamrun status={} gid={}", r.status, manifest_gid);

        if (!r.ok || r.status != 200) return false;

        if (size_t key = r.body.find("\"content\""); key != std::string::npos) {
            if (size_t q1 = r.body.find('"', key + 9); q1 != std::string::npos) {
                if (size_t q2 = r.body.find('"', q1 + 1); q2 != std::string::npos) {
                    uint64 code = 0;
                    auto [_, ec] = std::from_chars(
                        r.body.data() + q1 + 1, r.body.data() + q2, code);
                    if (ec == std::errc{}) {
                        *outRequestCode = code;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // ── provider: gmrc.wudrm.com ───────────────────────────────────
    // GET http://gmrc.wudrm.com/manifest/{gid}
    // Response: plain-text uint64, e.g. "10570517747114638659"
    bool FetchWudrm(uint64 manifest_gid, uint64* outRequestCode) {
        EnsureConnection(L"gmrc.wudrm.com", INTERNET_DEFAULT_HTTP_PORT, false);
        if (!g_hConnect) return false;

        wchar_t path[80];
        swprintf_s(path, L"/manifest/%llu", manifest_gid);

        char urlForLog[128];
        snprintf(urlForLog, sizeof(urlForLog), "http://gmrc.wudrm.com/manifest/%llu", manifest_gid);

        auto r = DoGet(path, urlForLog);
        LOG_MANIFEST_INFO("Manifest wudrm status={} gid={}", r.status, manifest_gid);

        if (!r.ok || r.status != 200) return false;

        uint64 code = 0;
        auto [_, ec] = std::from_chars(r.body.data(), r.body.data() + r.body.size(), code);
        if (ec == std::errc{}) {
            *outRequestCode = code;
            return true;
        }
        return false;
    }

    // ── resolve (single-provider, no fallback) ────────────────────
    bool FetchManifestRequestCode(uint64 manifest_gid, uint64* outRequestCode) {
        if (LuaConfig::HasManifestCodeFunc()) {
            if (LuaConfig::CallManifestFetchCode(manifest_gid, outRequestCode)) {
                LOG_MANIFEST_INFO("Manifest gid={} resolved via manifest.lua", manifest_gid);
                return true;
            }
            LOG_MANIFEST_WARN("Manifest gid={} lua returned nil, falling back to config", manifest_gid);
        }

        switch (Config::manifestUrl) {
        case Config::ManifestUrl::Wudrm:
            return FetchWudrm(manifest_gid, outRequestCode);
        case Config::ManifestUrl::SteamRun:
        default:
            return FetchSteamRun(manifest_gid, outRequestCode);
        }
    }

    HOOK_FUNC(GetManifestRequestCode, EResult, void* pObject, AppId_t AppId, AppId_t DepotId,
              uint64 manifest_gid, const char* branch, uint64* outRequestCode) {
        LOG_MANIFEST_DEBUG("GetManifestRequestCode: AppId={} DepotId={} gid={} branch={}",
                          AppId, DepotId, manifest_gid, branch);
        if (LuaConfig::HasDepot(DepotId)
            && FetchManifestRequestCode(manifest_gid, outRequestCode))
            return k_EResultOK;
        return oGetManifestRequestCode(pObject, AppId, DepotId, manifest_gid, branch, outRequestCode);
    }
}

namespace Hooks_Manifest {
    void Install() {
        HOOK_BEGIN();
        INSTALL_HOOK_D(GetManifestRequestCode);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(GetManifestRequestCode);
        UNHOOK_END();
        CloseConnection();
    }
}
