#pragma once

#include "dllmain.h"

// Hooks targeting steamui.dll:

namespace Hooks_SteamUI {
    void Install();
    void Uninstall();

    // Clears ownership flag for the given appId and
    // sends an app change notification to update the library UI.
    void RemoveAppAndSendChange(AppId_t appId);
}
