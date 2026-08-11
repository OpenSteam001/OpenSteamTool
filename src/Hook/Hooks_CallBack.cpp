#include "Hooks_CallBack.h"
#include "HookMacros.h"
#include "dllmain.h"
namespace {
    
    HOOK_FUNC(SendCallbackToPipe, bool, void* pSteamEngine, HSteamPipe hSteamPipe,
            HSteamUser iClientUser, int iCallback, void* pCallbackData, int cubCallbackData) {
        // ── Callback modifier dispatch ─────────────────────────────────────────
        // Intercept callbacks before they reach the pipe and modify data in-place.
        // To add a new callback: add an else-if branch here.
        
        return oSendCallbackToPipe(pSteamEngine, hSteamPipe, iClientUser,
                                    iCallback, pCallbackData, cubCallbackData);
    }
}

namespace Hooks_CallBack {
    void Install() {
        HOOK_BEGIN();
        INSTALL_HOOK_C(SendCallbackToPipe);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK_C(SendCallbackToPipe);
        UNHOOK_END();
    }
}
