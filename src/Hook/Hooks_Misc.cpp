#include "Hooks_Misc.h"
#include "HookMacros.h"
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
    using SetAppIdForCurrentPipe_t       = uint32_t(__fastcall*)(void*, uint32_t, bool);

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

    // Assumes one game at a time.  Set by SpawnProcess VEH when a
    // lua-added game is launched; cleared on non-lua games.
    AppId_t   g_OnlineFixRealAppId;

    std::unordered_map<AppId_t, std::string> g_GameNameCache;

    static SetAppIdForCurrentPipe_t oSetAppIdForCurrentPipe = nullptr;

    static void*    g_bCanRemotePlayTogetherTarget = nullptr;
    static uint8_t  g_bCanRemotePlayTogetherOrig[5];

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

                if (LuaConfig::HasDepot(appId)) {
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

    // ── SetAppIdForCurrentPipe hook ────────────────────────────
    uint32_t __fastcall hkSetAppIdForCurrentPipe(void* pSelf, uint32_t appId, bool bUnknown) {
        if (appId != 0 && appId != kOnlineFixAppId && LuaConfig::HasDepot(appId)) {
            LOG_MISC_INFO("SetAppIdForCurrentPipe: {} -> {}", appId, kOnlineFixAppId);
            appId = kOnlineFixAppId;
        }
        return oSetAppIdForCurrentPipe(pSelf, appId, bUnknown);
    }

} // anonymous namespace

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

        // SetAppIdForCurrentPipe hook
        if (auto* p = FIND_SIG(diversion_hMdoule, SetAppIdForCurrentPipe)) {
            oSetAppIdForCurrentPipe = reinterpret_cast<SetAppIdForCurrentPipe_t>(p);
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(reinterpret_cast<PVOID*>(&oSetAppIdForCurrentPipe),
                         reinterpret_cast<PVOID>(hkSetAppIdForCurrentPipe));
            DetourTransactionCommit();
            LOG_MISC_INFO("SetAppIdForCurrentPipe hook installed at {}", (void*)p);
        }

        // BCanRemotePlayTogether patch — hides the "Invite Anyone to Play" banner
        if (auto* p = FIND_SIG(diversion_hMdoule, BCanRemotePlayTogether)) {
            g_bCanRemotePlayTogetherTarget = p;
            memcpy(g_bCanRemotePlayTogetherOrig, p, 5);
            static constexpr uint8_t kPatch[5] = { 0xB0, 0x00, 0xC3, 0x90, 0x90 };
            PatchMemoryBytes(p, kPatch, 5);
            LOG_MISC_INFO("BCanRemotePlayTogether patched at {}", p);
        }
    }

    void Uninstall() {
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

        if (oSetAppIdForCurrentPipe) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(reinterpret_cast<PVOID*>(&oSetAppIdForCurrentPipe),
                         reinterpret_cast<PVOID>(hkSetAppIdForCurrentPipe));
            DetourTransactionCommit();
            oSetAppIdForCurrentPipe = nullptr;
        }

        if (g_bCanRemotePlayTogetherTarget) {
            PatchMemoryBytes(g_bCanRemotePlayTogetherTarget, g_bCanRemotePlayTogetherOrig, 5);
            g_bCanRemotePlayTogetherTarget = nullptr;
        }
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

        // Collect AppIds not already present in the vector.
        std::vector<AppId_t> allIds = LuaConfig::GetAllDepotIds();
        std::vector<AppId_t> newIds;
        for (AppId_t id : allIds) {
            bool found = false;
            for (uint32_t i = 0; i < pPkg->AppIdVec.m_Size; ++i) {
                if (pPkg->AppIdVec.m_Memory.m_pMemory[i] == id) {
                    found = true;
                    break;
                }
            }
            if (!found) newIds.push_back(id);
        }

        if (newIds.empty()) {
            LOG_PACKAGE_DEBUG("NotifyLicenseChanged: no new AppIds to insert");
            return;
        } else {
            uint32_t oldSize = pPkg->AppIdVec.m_Size;
            oCUtlMemoryGrow(&pPkg->AppIdVec, static_cast<uint32>(newIds.size()));
            for (size_t i = 0; i < newIds.size(); ++i) {
                pPkg->AppIdVec.m_Memory.m_pMemory[oldSize + i] = newIds[i];
                LOG_PACKAGE_DEBUG("NotifyLicenseChanged: inserted AppId {} at [{}]", newIds[i], oldSize + i);
            }
        }
        // Mark package 0 as changed and trigger library refresh.
        oMarkLicenseAsChanged(g_pCUser, 0, true);
        oProcessPendingLicenseUpdates(g_pCUser);
        LOG_PACKAGE_INFO("NotifyLicenseChanged: triggered, {} new AppIds inserted", newIds.size());
    }
}
