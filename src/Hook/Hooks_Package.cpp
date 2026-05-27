#include "Hooks_Package.h"
#include "HookMacros.h"
#include "dllmain.h"

namespace {
    using CUtlMemoryGrow_t = void* (*)(CUtlVector<AppId_t>* pVec, int grow_size);
    CUtlMemoryGrow_t oCUtlMemoryGrow = nullptr;

    HOOK_FUNC(LoadPackage, bool, PackageInfo* pInfo, uint8* sha1, int32 cn, void* p4) {
        bool result = oLoadPackage(pInfo, sha1, cn, p4);

        if (pInfo->PackageId == 0) {
            std::vector<AppId_t> appIds = LuaConfig::GetAllDepotIds();
            if (!appIds.empty()) {
                uint32 oldSize = pInfo->AppIdVec.m_Size;
                uint32 numToAdd = static_cast<uint32>(appIds.size());
                LOG_PACKAGE_INFO("LoadPackage(PackageId=0): adding {} apps, oldSize={}", numToAdd, oldSize);
                oCUtlMemoryGrow(&pInfo->AppIdVec, numToAdd);
                for (uint32 i = 0; i < numToAdd; i++)
                    pInfo->AppIdVec.m_Memory.m_pMemory[oldSize + i] = appIds[i];
            }
        }

        return result;
    }

    HOOK_FUNC(CheckAppOwnership, bool, void* pObj, AppId_t appId, AppOwnership* pOwn) {
        bool result = oCheckAppOwnership(pObj, appId, pOwn);
        // LOG_PACKAGE_TRACE("CheckAppOwnership: AppId={} result={} {}", appId, result, pOwn->DebugString());
        if (LuaConfig::HasDepot(appId)) {
            if (result && pOwn->ExistInPackageNums > 1) {
                // Actually owned — record so HasDepot excludes it going forward
                LuaConfig::MarkOwned(appId);
            } else {
                pOwn->PackageId    = 0;
                pOwn->ReleaseState = EAppReleaseState::Released;
                // Setting this free flag to false will hide it from the library UI.
                pOwn->bFreeLicense = false;
                return true;
            }
        }
        return result;
    }

    HOOK_FUNC(SendCallbackToPipe, bool, void* pSteamEngine, HSteamPipe hSteamPipe,
              HSteamUser iClientUser, int iCallback, void* pCallbackData, int cubCallbackData) {
        // ── Callback modifier dispatch ─────────────────────────────────────────
        // Intercept callbacks before they reach the pipe and modify data in-place.
        // To add a new callback: add an else-if branch here.
        if (iCallback == AppLicensesChanged_t::k_iCallback) {
            auto* p = static_cast<AppLicensesChanged_t*>(pCallbackData);
            LOG_PACKAGE_DEBUG("SendCallbackToPipe: AppLicensesChanged m_bReloadAll={}",
                           p->m_bReloadAll);
            // p->m_bReloadAll = true; we don't need it anymore
        }

        return oSendCallbackToPipe(pSteamEngine, hSteamPipe, iClientUser,
                                   iCallback, pCallbackData, cubCallbackData);
    }
}

namespace Hooks_Package {
    void Install() {
        RESOLVE_D(CUtlMemoryGrow);

        HOOK_BEGIN();
        INSTALL_HOOK_D(LoadPackage);
        INSTALL_HOOK_D(CheckAppOwnership);
        INSTALL_HOOK_D(SendCallbackToPipe);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(LoadPackage);
        UNINSTALL_HOOK(CheckAppOwnership);
        UNINSTALL_HOOK(SendCallbackToPipe);
        UNHOOK_END();
        oCUtlMemoryGrow = nullptr;
    }
}
