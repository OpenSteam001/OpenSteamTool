#include "HookManager.h"
#include "Hooks_AccessToken.h"
#include "Hooks_AppState.h"
#include "Hooks_AppTicket.h"
#include "Hooks_Decryption.h"
#include "Hooks_IPC.h"
#include "Hooks_KeyValues.h"
#include "Hooks_Manifest.h"
#include "Hooks_Misc.h"
#include "Hooks_NetPacket.h"
#include "Hooks_Package.h"


namespace SteamClient {

    void CoreHook() {
        // Hooks_AccessToken::Install();  // migrated to Hooks_NetPacket (PICS)
        Hooks_AppState::Install();
        // Hooks_AppTicket::Install();
        Hooks_Decryption::Install();
        Hooks_IPC::Install();
        Hooks_KeyValues::Install();
        Hooks_Manifest::Install();
        Hooks_Misc::Install();
        Hooks_NetPacket::Install();
        Hooks_Package::Install();
    }

    void CoreUnhook() {
        // Hooks_AccessToken::Uninstall();  // migrated to Hooks_NetPacket (PICS)
        Hooks_AppState::Uninstall();
        // Hooks_AppTicket::Uninstall();
        Hooks_Decryption::Uninstall();
        Hooks_IPC::Uninstall();
        Hooks_KeyValues::Uninstall();
        Hooks_Manifest::Uninstall();
        Hooks_Misc::Uninstall();
        Hooks_NetPacket::Uninstall();
        Hooks_Package::Uninstall();
    }
}
