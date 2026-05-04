#include "Hooks_Package.h"
#include "HookMacros.h"
#include "dllmain.h"

namespace {
    using CUtlMemoryGrow_t = void* (*)(CUtlVector<uint32>* pVec, int grow_size);
    CUtlMemoryGrow_t oCUtlMemoryGrow = nullptr;

    HOOK_FUNC(LoadPackage, bool, PackageInfo* pInfo, uint8* sha1, int32 cn, void* p4) {
        bool result = oLoadPackage(pInfo, sha1, cn, p4);
        if (pInfo->PackageId == 0) {
            std::vector<AppId_t> appIds = LuaConfig::GetAllDepotIds();
            if (!appIds.empty()) {
                uint32 oldSize = pInfo->AppIdVec.m_Size;
                uint32 numToAdd = static_cast<uint32>(appIds.size());
                oCUtlMemoryGrow(&pInfo->AppIdVec, numToAdd);
                for (uint32 i = 0; i < numToAdd; i++)
                    pInfo->AppIdVec.m_Memory.m_pMemory[oldSize + i] = appIds[i];
            }
        }
        return result;
    }

    HOOK_FUNC(CheckAppOwnership, bool, void* pObj, AppId_t appId, AppOwnership* pOwn) {
        bool result = oCheckAppOwnership(pObj, appId, pOwn);
        if (LuaConfig::HasDepot(appId)) {
            if (result && pOwn->ExistInPackageNums > 1) {
                // Actually owned — record so HasDepot excludes it going forward
                LuaConfig::MarkOwned(appId);
            } else {
                pOwn->PackageId    = 0;
                pOwn->ReleaseState = EAppReleaseState::Released;
                pOwn->GameIDType   = EGameIDType::k_EGameIDTypeApp;
                return true;
            }
        }
        return result;
    }
}

namespace Hooks_Package {
    void Install() {
        RESOLVE_D(CUtlMemoryGrow);

        HOOK_BEGIN();
        INSTALL_HOOK_D(LoadPackage);
        INSTALL_HOOK_D(CheckAppOwnership);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(LoadPackage);
        UNINSTALL_HOOK(CheckAppOwnership);
        UNHOOK_END();
        oCUtlMemoryGrow = nullptr;
    }
}
