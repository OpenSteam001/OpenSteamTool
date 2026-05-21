#pragma once

#include "dllmain.h"

// Hooks targeting steamui.dll:
//   * LoadModuleWithPath      -> redirect steamclient64.dll loads to the diversion copy
//   * RemoveAppOverview       -> evict a card from the live library UI by
//                                emitting a synthesized CAppOverview_Change to
//                                every registered webhelper subscriber.
namespace Hooks_SteamUI {
    void Install();
    void Uninstall();

    // Drop appId from the webhelper's m_mapApps and clear the host-side
    // CSteamApp owned flag so the next full snapshot also excludes it.
    void RemoveAppOverview(AppId_t appId);
}
