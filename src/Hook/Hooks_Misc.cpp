#include "Hooks_Misc.h"
#include "HookMacros.h"
#include "Hooks_SteamUI.h"
#include "Utils/VehCommon.h"
#include "dllmain.h"

namespace {
    // ── function type aliases (alphabetical) ─────────────────────────────────
    using CUtlBufferEnsureCapacity_t     = void*(*)(CUtlBuffer*, int);
    using CUtlMemoryGrow_t               = void*(*)(CUtlVector<AppId_t>*, int);
    using GetAppDataFromAppInfo_t        = int64(*)(void*, AppId_t, const char*, uint8*, int32);
    using GetAppIDForCurrentPipe_t       = AppId_t(*)(void*);
    using GetPackageInfo_t               = PackageInfo*(*)(void*, uint32, int64);
    using MarkLicenseAsChanged_t         = int64(*)(void*, uint32, bool);
    using ProcessPendingLicenseUpdates_t = bool(*)(void*);

    // ── X-macro lists ────────────────────────────────────────────────────────
    // One-shot int3: on hit, ctx->Rcx stored to the named output variable.
    #define CAPTURE_LIST(X)                          \
        X(GetAppIDForCurrentPipe, g_steamEngine)     \
        X(GetAppDataFromAppInfo,  g_pCAppInfoCache)  \
        X(MarkLicenseAsChanged,   g_pCUser)          \
        X(GetPackageInfo,         g_pCPackageInfo)

    // Resolve-only (no int3).
    #define LOCATE_LIST(X)               \
        X(CUtlBufferEnsureCapacity)      \
        X(CUtlMemoryGrow)               \
        X(ProcessPendingLicenseUpdates)

    // ── generated declarations ───────────────────────────────────────────────
    CAPTURE_LIST(VEH_DECL_CAPTURE)
    LOCATE_LIST(VEH_DECL_RESOLVE)

    uint8_t*  g_spawnProcessTarget;
    PVOID     g_vehHandle;

    // Assumes one game at a time.  Set by SpawnProcess VEH when -onlinefix
    // is detected; cleared when a non-onlinefix game launches.
    AppId_t   g_OnlineFixRealAppId;

    std::unordered_map<AppId_t, std::string> g_GameNameCache;

    static std::vector<CaptureEntry> g_captures;

    // ── VEH handler ──────────────────────────────────────────────────────────
    // Scoped to this module's int3 sites only. Foreign RIP ->
    // EXCEPTION_CONTINUE_SEARCH so other VEH handlers still get their turn.
    LONG CALLBACK VehHandler(PEXCEPTION_POINTERS pExInfo) {
        PCONTEXT ctx = pExInfo->ContextRecord;

        if (pExInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
            for (auto& cap : g_captures) {
                if (*cap.funcPtr && ctx->Rip == reinterpret_cast<uint64_t>(*cap.funcPtr)) {
                    *cap.outPtr = reinterpret_cast<void*>(ctx->Rcx);
                    *reinterpret_cast<uint8_t*>(*cap.funcPtr) = cap.restoreByte;
                    LOG_MISC_INFO("Captured {}: 0x{:X}", cap.label,
                                  reinterpret_cast<uint64_t>(*cap.outPtr));
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }

            // CUser_SpawnProcess(pCUser, pExePath, pCommandLine, pWorkingDir,
            //                   pGameID, ...)
            // RCX=pCUser, RDX=pExePath, R8=pCommandLine, R9=pWorkingDir
            // [RSP+0x28]=pGameID (5th arg, pointer to CGameID, low 24 bits = AppId)
            if (g_spawnProcessTarget
                && ctx->Rip == reinterpret_cast<uint64_t>(g_spawnProcessTarget)) {
                auto* pGameID = reinterpret_cast<uint64_t*>(
                    *reinterpret_cast<uint64_t*>(ctx->Rsp + 0x28));
                AppId_t appId = static_cast<AppId_t>(*pGameID & 0xFFFFFF);

                *g_spawnProcessTarget = 0x48;
                ctx->EFlags |= 0x100;

                const char* cmdLine = reinterpret_cast<const char*>(ctx->R8);

                if (LuaConfig::HasDepot(appId) && cmdLine
                    && strstr(cmdLine, "-onlinefix")) {
                    g_OnlineFixRealAppId = appId;
                    *pGameID = kOnlineFixAppId;
                    LOG_MISC_INFO("SpawnProcess: appid {} -> {}, cmd=\"{}\"",
                                  appId, kOnlineFixAppId, cmdLine);
                } else {
                    g_OnlineFixRealAppId = 0;
                }
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }

        if (pExInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
            if (g_spawnProcessTarget
                && ctx->Rip == reinterpret_cast<uint64_t>(g_spawnProcessTarget + 5)) {
                *g_spawnProcessTarget = 0xCC;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    // ── SteamController_OptedInMask ──────────────────────────────────────────
    // Called by CUser_BuildSpawnEnvBlock with pGameID's appid to
    // compute EnableConfiguratorSupport and the SDL_* env vars.
    // With 480 the spawned game inherits Spacewar's Steam Input
    // opt-in and gameoverlayrenderer hijacks the XInput stream.
    HOOK_FUNC(OptedInMask, __int64,
              void* pThis, unsigned int appId)
    {
        if (appId == kOnlineFixAppId && g_OnlineFixRealAppId) {
            LOG_MISC_INFO("OptedInMask: appid {} -> {}",
                          appId, g_OnlineFixRealAppId);
            return oOptedInMask(pThis, g_OnlineFixRealAppId);
        }
        return oOptedInMask(pThis, appId);
    }

    // ── CUser_BuildSpawnEnvBlock ─────────────────────────────────────────────
    // pOverlayCGameID drives SteamOverlayGameId, which the in-game
    // overlay reads for screenshot tags, community URLs, and asset
    // selection.  pCGameID drives SteamGameId / SteamAppId; leave it
    // at 480 so the in-game ownership bypass holds.
    HOOK_FUNC(BuildSpawnEnvBlock, __int64,
              void* pThis, uint64_t* pCGameID, void* a3, void* env,
              uint64_t* pOverlayCGameID, void* a6, int a7,
              void* a8, void* a9, unsigned int a10, char a11)
    {
        if (g_OnlineFixRealAppId && pOverlayCGameID
            && (*pOverlayCGameID & 0xFFFFFF) == kOnlineFixAppId) {
            uint64_t prev = *pOverlayCGameID;
            *pOverlayCGameID = (prev & ~static_cast<uint64_t>(0xFFFFFF))
                             | g_OnlineFixRealAppId;
            LOG_MISC_INFO("BuildSpawnEnvBlock: overlay CGameID {:#x} -> {:#x}",
                          prev, *pOverlayCGameID);
        }
        return oBuildSpawnEnvBlock(pThis, pCGameID, a3, env,
                                    pOverlayCGameID, a6, a7,
                                    a8, a9, a10, a11);
    }
}

namespace Hooks_Misc {
    void Install() {
        if (g_vehHandle) return;

        LOCATE_LIST(VEH_LOCATE)
        CAPTURE_LIST(VEH_ARM)

        if (auto* p = FIND_SIG(diversion_hMdoule, SpawnProcess)) {
            g_spawnProcessTarget = static_cast<uint8_t*>(p);
            VehCommon::ArmInt3(p);
        }

        if (!g_captures.empty() || g_spawnProcessTarget)
            g_vehHandle = AddVectoredExceptionHandler(1, VehHandler);

        HOOK_BEGIN();
        INSTALL_HOOK_D(BuildSpawnEnvBlock);
        INSTALL_HOOK_D(OptedInMask);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(BuildSpawnEnvBlock);
        UNINSTALL_HOOK(OptedInMask);
        UNHOOK_END();

        if (g_vehHandle) {
            RemoveVectoredExceptionHandler(g_vehHandle);
            g_vehHandle = nullptr;
        }

        VEH_CLEANUP_CAPTURES(g_captures);

        if (g_spawnProcessTarget && *g_spawnProcessTarget == 0xCC)
            VehCommon::RestoreByte(g_spawnProcessTarget, 0x48);
        g_spawnProcessTarget = nullptr;

        LOCATE_LIST(VEH_ZERO_RESOLVE)
        g_OnlineFixRealAppId = 0;
        g_GameNameCache.clear();
    }

    AppId_t GetAppIDForCurrentPipe() {
        if (!g_steamEngine || !oGetAppIDForCurrentPipe) {
            LOG_MISC_WARN("GetAppIDForCurrentPipe called before capture — returning 0");
            return 0;
        }
        auto appid = oGetAppIDForCurrentPipe(g_steamEngine);
        if (!appid) {
            LOG_MISC_TRACE("GetAppIDForCurrentPipe: AppId=0(Not GamePipe)");
        } else {
            LOG_MISC_DEBUG("GetAppIDForCurrentPipe: AppId={}", appid);
        }
        return appid;
    }

    AppId_t ResolveAppId() {
        if (g_OnlineFixRealAppId) return g_OnlineFixRealAppId;
        return GetAppIDForCurrentPipe();
    }

    void EnsureBufferSize(CUtlBuffer* pWrite, int32 size)
    {
        if (oCUtlBufferEnsureCapacity) {
            LOG_MISC_DEBUG("Before ensuring CUtlBuffer capacity: {}", pWrite->DebugString());
            oCUtlBufferEnsureCapacity(pWrite, size);
            LOG_MISC_DEBUG("After ensuring CUtlBuffer capacity: {}", pWrite->DebugString());
        }
        pWrite->m_Put = size;
    }

    // ── Game name ────────────────────────────────────────────────
    std::string GetGameNameByAppID(AppId_t appId)
    {
        auto it = g_GameNameCache.find(appId);
        if (it != g_GameNameCache.end()) return it->second;

        std::string name;

        if (g_pCAppInfoCache && oGetAppDataFromAppInfo) {
            char buf[256] = {};
            // "common/name" triggers auto-localization: the function detects
            // prefix "common" (keyType=2) + key "name", then tries
            // "name_localized/<current_lang>" before falling back to "name".
            // Returns strlen+1 on success, -1 on failure.
            int64 len = oGetAppDataFromAppInfo(
                g_pCAppInfoCache, appId, "common/name",
                reinterpret_cast<uint8*>(buf), sizeof(buf));
            if (len > 1)
                name.assign(buf, static_cast<size_t>(len - 1));
        }

        LOG_MISC_DEBUG("GetGameNameByAppID({}): {}", appId, name);
        g_GameNameCache[appId] = name;
        return name;
    }

    // ── License refresh (no-restart) ────────────────────────────────
    void NotifyLicenseChanged() {
        if (!g_pCUser || !g_pCPackageInfo) {
            LOG_PACKAGE_WARN("NotifyLicenseChanged: pCUser or pCPackageInfo not captured yet, skipping");
            return;
        }
        if (!oGetPackageInfo || !oMarkLicenseAsChanged
            || !oProcessPendingLicenseUpdates || !oCUtlMemoryGrow) {
            LOG_PACKAGE_WARN("NotifyLicenseChanged: functions not resolved, skipping");
            return;
        }

        PackageInfo* pPkg = oGetPackageInfo(g_pCPackageInfo, 0, 0);
        if (!pPkg) {
            LOG_PACKAGE_WARN("NotifyLicenseChanged: GetPackageInfo returned null");
            return;
        }

        // ── Remove depots that were unloaded ──
        std::vector<AppId_t> removals = LuaConfig::TakePendingRemovals();
        uint32_t removedCount = 0;
        for (AppId_t id : removals) {
            if (pPkg->AppIdVec.FindAndFastRemove(id)) {
                ++removedCount;
                LOG_PACKAGE_DEBUG("NotifyLicenseChanged: removed AppId {}", id);
            }
        }

        // ── Add depots that are newly loaded ──
        std::vector<AppId_t> additions = LuaConfig::TakePendingAdditions();
        if (!additions.empty()) {
            uint32_t oldSize = pPkg->AppIdVec.m_Size;
            oCUtlMemoryGrow(&pPkg->AppIdVec, static_cast<uint32>(additions.size()));
            for (size_t i = 0; i < additions.size(); ++i) {
                pPkg->AppIdVec.m_Memory.m_pMemory[oldSize + i] = additions[i];
                LOG_PACKAGE_DEBUG("NotifyLicenseChanged: inserted AppId {} at [{}]", additions[i], oldSize + i);
            }
        }

        if (additions.empty() && removedCount == 0) {
            LOG_PACKAGE_DEBUG("NotifyLicenseChanged: no changes");
            return;
        }

        // Mark package 0 as changed and trigger library refresh.
        oMarkLicenseAsChanged(g_pCUser, 0, true);
        oProcessPendingLicenseUpdates(g_pCUser);
        LOG_PACKAGE_INFO("NotifyLicenseChanged: {} added, {} removed", additions.size(), removedCount);

        // CSteamUIAppController caches its own AppOverview entries and doesn't
        // re-query subscription state on package mutation; remove them
        // explicitly so the library UI reflects the dropped license.
        for (AppId_t id : removals) {
            Hooks_SteamUI::RemoveAppOverview(id);
        }
    }
}
