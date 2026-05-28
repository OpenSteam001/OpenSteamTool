#include "HookManager.h"
#include "Hooks_CallBack.h"
#include "Hooks_Decryption.h"
#include "Hooks_IPC.h"
#include "Hooks_KeyValues.h"
#include "Hooks_Manifest.h"
#include "Hooks_Misc.h"
#include "Hooks_NetPacket.h"
#include "Hooks_Package.h"
#include "Hooks_SteamUI.h"
#include "Utils/VehCommon.h"


namespace SteamUI {

    void CoreHook()   { 
        Hooks_SteamUI::Install(); 
    }
    void CoreUnhook() { 
        Hooks_SteamUI::Uninstall(); 
    }
}

namespace SteamClient {

    void CoreHook() {
        Hooks_CallBack::Install();
        Hooks_Decryption::Install();
        Hooks_IPC::Install();
        // Hooks_KeyValues::Install();
        Hooks_Manifest::Install();
        Hooks_Misc::Install();
        Hooks_NetPacket::Install();
        Hooks_Package::Install();
    }

    void CoreUnhook() {
        Hooks_CallBack::Uninstall();
        Hooks_Decryption::Uninstall();
        Hooks_IPC::Uninstall();
        // Hooks_KeyValues::Uninstall();
        Hooks_Manifest::Uninstall();
        Hooks_Misc::Uninstall();
        Hooks_NetPacket::Uninstall();
        Hooks_Package::Uninstall();
        VehCommon::DisarmAll();
        VehCommon::RemoveHandler();
    }
}
