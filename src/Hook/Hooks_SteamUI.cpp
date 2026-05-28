#include "Hooks_SteamUI.h"
#include "HookManager.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "steam_messages.pb.h"
#include "Utils/VehCommon.h"

#include <chrono>
#include <thread>

namespace {

    CAPTURE_THIS_FUNC(GetAppByID, CSteamApp*, g_pController,void* pThis, AppId_t appId, bool bCreate);
    CAPTURE_THIS_FUNC(MarkAppChange,void*,g_pAppChangeSource,void* pThis,AppId_t appId, EAppChangeFlags changeFlags);

}

namespace Hooks_SteamUI {
    void Install() {

        ARM_CAPTURE_U(GetAppByID);
        ARM_CAPTURE_U(MarkAppChange);

        HOOK_BEGIN();
        HOOK_END();

    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNHOOK_END();
    }

    void RemoveAppAndSendChange(AppId_t appId) {
        if(CAPTURE_READY(GetAppByID) && CAPTURE_READY(MarkAppChange)) {
            CSteamApp* pApp = oGetAppByID(g_pController, appId, false);
            if(pApp) {
                pApp->OwnershipFlags = k_EAppOwnershipFlags_None;
                LOG_STEAMUI_DEBUG("RemoveAppAndSendChange: cleared owned flag for appId={}", appId);
                oMarkAppChange(g_pAppChangeSource, appId, EAppChangeFlags::AddedOrCreated);
            } else {
                LOG_STEAMUI_WARN("RemoveAppAndSendChange: appId={} not found in GetAppByID", appId);
            }
        }
    }

}
