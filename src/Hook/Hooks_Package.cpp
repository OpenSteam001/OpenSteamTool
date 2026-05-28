#include "Hooks_Package.h"
#include "HookMacros.h"
#include "Hooks_SteamUI.h"
#include "dllmain.h"

namespace {
    RESOLVE_FUNC(CUtlMemoryGrow,               void*, CUtlVector<AppId_t>*, int);
    RESOLVE_FUNC(MarkLicenseAsChanged,         int64, void*, uint32, bool);
    RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool,  void*);

    void* g_pCUser = nullptr;
    void* g_pCPackageInfo = nullptr;
    PackageInfo* g_pInjectedPackageInfo = nullptr;
    bool  g_licenseInitialized = false;
    bool  g_licenseRefreshPending = false;

    constexpr PackageId_t kInjectedPackageId = 0;

    bool MarkLicenseAsChangedAndProcessUpdates() {
        if (!g_pCUser || !oMarkLicenseAsChanged || !oProcessPendingLicenseUpdates) {
            LOG_PACKAGE_WARN("MarkLicenseAsChangedAndProcessUpdates: dependencies not ready, skipping");
            return false;
        }
        oMarkLicenseAsChanged(g_pCUser, kInjectedPackageId, true);
        oProcessPendingLicenseUpdates(g_pCUser);
        LOG_PACKAGE_DEBUG("MarkLicenseAsChangedAndProcessUpdates: marked package {} as changed and processed updates", kInjectedPackageId);
        return true;
    }

    void TryProcessPendingLicenseRefresh() {
        if (!g_licenseRefreshPending)
            return;
        if (MarkLicenseAsChangedAndProcessUpdates())
            g_licenseRefreshPending = false;
    }

    bool CUtlMemoryGrowWrap(CUtlVector<AppId_t>* pVec, int grow_size) {
        if (!oCUtlMemoryGrow) {
            LOG_PACKAGE_WARN("CUtlMemoryGrow: oCUtlMemoryGrow not ready, cannot grow");
            return false;
        }
        return oCUtlMemoryGrow(pVec, grow_size);
    }

    bool InitFakeLicenseOnce(PackageInfo* pPkg) {
        if (!pPkg) {
            LOG_PACKAGE_WARN("InitFakeLicense: null PackageInfo pointer");
            return false;
        }
        // Inject all depots from config into the fake license. 
        std::vector<AppId_t> appIds = LuaConfig::GetAllDepotIds();
        if (!appIds.empty()) {
            uint32 oldSize = pPkg->AppIdVec.m_Size;
            uint32 numToAdd = static_cast<uint32>(appIds.size());
            LOG_PACKAGE_INFO("InitFakeLicense(PackageId={}): adding {} apps, oldSize={}", kInjectedPackageId, numToAdd, oldSize);
            if (!CUtlMemoryGrowWrap(&pPkg->AppIdVec, numToAdd)) {
                LOG_PACKAGE_WARN("InitFakeLicense(PackageId={}): failed to grow AppId vector", kInjectedPackageId);
                return false;
            }
            for (uint32 i = 0; i < numToAdd; i++)
                pPkg->AppIdVec.m_Memory.m_pMemory[oldSize + i] = appIds[i];
        }

        g_licenseInitialized = true;
        g_licenseRefreshPending = true;
        TryProcessPendingLicenseRefresh();
        return true;
    }

    HOOK_FUNC(LoadPackage, bool, PackageInfo* pInfo, uint8* sha1, int32 cn, void* p4) {
        bool result = oLoadPackage(pInfo, sha1, cn, p4);
        
        if (pInfo->PackageId == 0) {
            std::vector<AppId_t> appIds = LuaConfig::GetAllDepotIds();
            if (!appIds.empty()) {
                uint32 oldSize = pInfo->AppIdVec.m_Size;
                uint32 numToAdd = static_cast<uint32>(appIds.size());
                LOG_PACKAGE_INFO("LoadPackage(PackageId={}): adding {} apps, oldSize={}", kInjectedPackageId, numToAdd, oldSize);
                if (!CUtlMemoryGrowWrap(&pInfo->AppIdVec, numToAdd)) {
                    LOG_PACKAGE_WARN("LoadPackage(PackageId={}): failed to grow AppId vector", kInjectedPackageId);
                    return result;
                }
                for (uint32 i = 0; i < numToAdd; i++)
                pInfo->AppIdVec.m_Memory.m_pMemory[oldSize + i] = appIds[i];
            }
        }
        
        return result;
    }
    
    HOOK_FUNC(GetPackageInfo, PackageInfo*, void* pPackageInfoCache, uint32 packageId, uint64 accessToken) {
        if (!g_pCPackageInfo) {
            g_pCPackageInfo = pPackageInfoCache;
            LOG_PACKAGE_DEBUG("GetPackageInfo: captured CPackageInfo {}", g_pCPackageInfo);
        }
        // LOG_PACKAGE_TRACE("GetPackageInfo: package cache {}, packageId {}, accessToken {}", pPackageInfoCache, packageId, accessToken);
        PackageInfo* pPkg = oGetPackageInfo(pPackageInfoCache, packageId, accessToken);
        if (packageId == 0 && pPkg) {
            if(!g_pInjectedPackageInfo) {
                g_pInjectedPackageInfo = pPkg;
                LOG_PACKAGE_INFO("GetPackageInfo: g_pInjectedPackageInfo set to {}", (void*)g_pInjectedPackageInfo);
            }
            if(!g_licenseInitialized) {
                InitFakeLicenseOnce(pPkg);
            }
        }
        return pPkg;
    }

    HOOK_FUNC(CheckAppOwnership, bool, void* pObj, AppId_t appId, AppOwnership* pOwn) {
        if (!g_pCUser) {
            g_pCUser = pObj;
            LOG_PACKAGE_DEBUG("CheckAppOwnership: captured CUser {}", g_pCUser);
        }

        bool result = oCheckAppOwnership(pObj, appId, pOwn);
        TryProcessPendingLicenseRefresh();

        // LOG_PACKAGE_TRACE("CheckAppOwnership: AppId={} result={} {}", appId, result, pOwn->DebugString());
        if (LuaConfig::HasDepot(appId)) {
            if (result && pOwn->ExistInPackageNums > 1) {
                // Actually owned — record so HasDepot excludes it going forward
                LuaConfig::MarkOwned(appId);
            } else {
                pOwn->PackageId    = kInjectedPackageId;
                pOwn->ReleaseState = EAppReleaseState::Released;
                // Setting this free flag to false will hide it from the library UI.
                pOwn->bFreeLicense = false;
                return true;
            }
        }
        return result;
    }
}

namespace Hooks_Package {
    void Install() {
        RESOLVE_C(CUtlMemoryGrow);
        RESOLVE_C(MarkLicenseAsChanged);
        RESOLVE_C(ProcessPendingLicenseUpdates);

        HOOK_BEGIN();
        // INSTALL_HOOK_C(LoadPackage);
        INSTALL_HOOK_C(GetPackageInfo);
        INSTALL_HOOK_C(CheckAppOwnership);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        // UNINSTALL_HOOK_C(LoadPackage);
        UNINSTALL_HOOK_C(GetPackageInfo);
        UNINSTALL_HOOK_C(CheckAppOwnership);
        UNHOOK_END();
    }

    void NotifyLicenseChanged() {
        PackageInfo* pPkg = g_pInjectedPackageInfo;
        if (!pPkg) {
            LOG_PACKAGE_WARN("NotifyLicenseChanged: injected PackageInfo not ready, cannot notify");
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
            if (CUtlMemoryGrowWrap(&pPkg->AppIdVec, additions.size())) {
                for (size_t i = 0; i < additions.size(); ++i) {
                    pPkg->AppIdVec.m_Memory.m_pMemory[oldSize + i] = additions[i];
                    LOG_PACKAGE_DEBUG("NotifyLicenseChanged: inserted AppId {} at [{}]", additions[i], oldSize + i);
                }
            }
        }

        if (additions.empty() && removedCount == 0) {
            LOG_PACKAGE_DEBUG("NotifyLicenseChanged: no changes");
            return;
        }

        // Mark package 0 as changed and trigger library refresh.
        if (!MarkLicenseAsChangedAndProcessUpdates()) {
            LOG_PACKAGE_WARN("NotifyLicenseChanged: failed to mark license as changed");
            return;
        }
        LOG_PACKAGE_INFO("NotifyLicenseChanged: {} added, {} removed", additions.size(), removedCount);

        // CSteamUIAppController caches its own AppOverview entries and doesn't
        // re-query subscription state on package mutation; remove them
        // explicitly so the library UI reflects the dropped license.
        for (AppId_t id : removals) {
            Hooks_SteamUI::RemoveAppAndSendChange(id);
        }
    }
}
