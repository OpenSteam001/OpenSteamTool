#include "Hooks_Manifest.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "Utils/WinHttp.h"
#include <charconv>
#include <format>
#include <mutex>

// ═══════════════════════════════════════════════════════════════════
//  Manifest override hooks:
//    BuildDepotDependency — patches depot entries' gid/size directly
//      in the output vector (replaces the old KV-tree approach).
//
//    GetManifestRequestCode — migrated to Hooks_NetPacket_Manifest
//      (NetPacket layer, async HTTP via ContentServerDirectory#1).
// ═══════════════════════════════════════════════════════════════════
namespace {

    // ── helper ─────────────────────────────────────────────────────

    std::string DepotEntryDebug(const DepotEntry& e) {
        return std::format("DepotId={} AppId={} Gid={} Size={} Dlc={} Lcs={} Carry={} Shared={}",
            e.DepotId, e.AppId, e.ManifestGid, e.ManifestSize, e.DlcAppId,
            (int)e.LcsRequired, (int)e.bNotNewTarget, (int)e.SharedInstall);
    }

    // ── BuildDepotDependency hook ──────────────────────────────────
    // After Steam builds the depot list for an app, patch ManifestGid
    // and ManifestSize for any depots we have overrides for.

    HOOK_FUNC(BuildDepotDependency, bool, void* pUserAppMgr, AppId_t AppId,
              void* pUserConfig, CUtlVector<DepotEntry>* pDepotInfo,
              CUtlVector<DepotEntry>* pSharedDepotInfo, void* pSteamApp,
              uint32* pBuildId, bool* pbBetaFallback)
    {
        bool result = oBuildDepotDependency(pUserAppMgr, AppId, pUserConfig,
            pDepotInfo, pSharedDepotInfo, pSteamApp, pBuildId, pbBetaFallback);

        LOG_MANIFEST_TRACE("BuildDepotDependency: AppId={} pUserConfig=0x{:X} result={} pSteamApp=0x{:X} pBuildId={} pbBetaFallback={}",
            AppId, (uintptr_t)pUserConfig, result, (uintptr_t)pSteamApp,
            pBuildId ? *pBuildId : 0, pbBetaFallback ? *pbBetaFallback : false);
        if (pDepotInfo) {
            LOG_MANIFEST_TRACE("pDepotInfo->nCount={}", pDepotInfo->m_Size);
            for (uint32 i = 0; i < pDepotInfo->m_Size; ++i) {
                LOG_MANIFEST_TRACE("  [{}] {}", i, DepotEntryDebug(pDepotInfo->m_Memory.m_pMemory[i]));
            }
        }
        if (pSharedDepotInfo) {
            LOG_MANIFEST_TRACE("pSharedDepotInfo->nCount={}", pSharedDepotInfo->m_Size);
            for (uint32 i = 0; i < pSharedDepotInfo->m_Size; ++i) {
                LOG_MANIFEST_TRACE("  shared[{}] {}", i, DepotEntryDebug(pSharedDepotInfo->m_Memory.m_pMemory[i]));
            }
        }

        if (!result) return result;

        const auto& overrides = LuaConfig::GetManifestOverrides();
        if (overrides.empty()) return result;

        if (pDepotInfo && pDepotInfo->m_Size) {
            for (uint32 i = 0; i < pDepotInfo->m_Size; ++i) {
                DepotEntry& e = pDepotInfo->m_Memory.m_pMemory[i];
                auto it = overrides.find(e.DepotId);
                if (it != overrides.end()) {
                    // if size=0 in the override, keep the original size(affects download display but not the actual download)
                    uint64_t newSize = it->second.size ? it->second.size : e.ManifestSize;
                    LOG_MANIFEST_INFO("BuildDepotDependency: patching depot {} gid={}->{} size={}->{}",
                        e.DepotId, e.ManifestGid, it->second.gid,
                        e.ManifestSize, newSize);
                    e.ManifestGid  = it->second.gid;
                    e.ManifestSize = newSize;
                }
            }
        }
        return result;
    }

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════
//  Manifest HTTP providers (thread-safe via g_ConnectionMutex)
// ═══════════════════════════════════════════════════════════════════
namespace Hooks_Manifest {

    std::mutex  g_ConnectionMutex;
    HINTERNET   g_hSession = nullptr;
    HINTERNET   g_hConnect = nullptr;
    bool        g_tls      = false;

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
    bool FetchSteamRun(uint64_t manifest_gid, uint64_t* outRequestCode) {
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
                    uint64_t code = 0;
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
    // Response: plain-text uint64_t, e.g. "10570517747114638659"
    bool FetchWudrm(uint64_t manifest_gid, uint64_t* outRequestCode) {
        EnsureConnection(L"gmrc.wudrm.com", INTERNET_DEFAULT_HTTP_PORT, false);
        if (!g_hConnect) return false;

        wchar_t path[80];
        swprintf_s(path, L"/manifest/%llu", manifest_gid);

        char urlForLog[128];
        snprintf(urlForLog, sizeof(urlForLog), "http://gmrc.wudrm.com/manifest/%llu", manifest_gid);

        auto r = DoGet(path, urlForLog);
        LOG_MANIFEST_INFO("Manifest wudrm status={} gid={}", r.status, manifest_gid);

        if (!r.ok || r.status != 200) return false;

        uint64_t code = 0;
        auto [_, ec] = std::from_chars(r.body.data(), r.body.data() + r.body.size(), code);
        if (ec == std::errc{}) {
            *outRequestCode = code;
            return true;
        }
        return false;
    }

    // ── resolve (single-provider, no fallback) ────────────────────
    bool FetchManifestRequestCode(uint64_t manifestGid, uint64_t* outRequestCode, AppId_t AppId, AppId_t DepotId) {
        std::lock_guard<std::mutex> lock(g_ConnectionMutex);

        // Try extended Lua function first (receives app_id, depot_id, gid)
        if (AppId && DepotId && LuaConfig::HasManifestCodeFuncEx()) {
            if (LuaConfig::CallManifestFetchCodeEx(AppId, DepotId, manifestGid, outRequestCode)) {
                LOG_MANIFEST_INFO("Manifest gid={} resolved via fetch_manifest_code_ex", manifestGid);
                return true;
            }
            LOG_MANIFEST_WARN("Manifest gid={} fetch_manifest_code_ex returned nil, trying fetch_manifest_code", manifestGid);
        }

        // Fall back to original Lua function (receives gid only)
        if (LuaConfig::HasManifestCodeFunc()) {
            if (LuaConfig::CallManifestFetchCode(manifestGid, outRequestCode)) {
                LOG_MANIFEST_INFO("Manifest gid={} resolved via manifest.lua", manifestGid);
                return true;
            }
            LOG_MANIFEST_WARN("Manifest gid={} lua returned nil, falling back to config", manifestGid);
        }

        switch (Config::manifestUrl) {
        case Config::ManifestUrl::Wudrm:
            return FetchWudrm(manifestGid, outRequestCode);
        case Config::ManifestUrl::SteamRun:
        default:
            return FetchSteamRun(manifestGid, outRequestCode);
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  Install / Uninstall
    // ═══════════════════════════════════════════════════════════════

    void Install() {
        HOOK_BEGIN();
        INSTALL_HOOK_C(BuildDepotDependency);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(BuildDepotDependency);
        UNHOOK_END();
        CloseConnection();
    }
}
