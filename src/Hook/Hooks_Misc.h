#pragma once

#include "dllmain.h"

// Catch-all for the lightweight info-capture int3 traps that don't fit a
// dedicated category — currently:
//   * GetAppIDForCurrentPipe  -> captures the SteamEngine pointer
//   * SpawnProcess            -> auto-spoof lua-added games to 480
//   * GetAppDataFromAppInfo   -> captures the CAppInfoCache pointer
//   * MarkLicenseAsChanged    -> captures pCUser; resolved for NotifyLicenseChanged
//   * GetPackageInfo          -> captures pCPackageInfo; used by NotifyLicenseChanged to append AppIds
//   * ProcessPendingLicenseUpdates -> resolved for NotifyLicenseChanged
namespace Hooks_Misc {
    void Install();
    void Uninstall();

    // Returns the AppId for the current Steam pipe via the captured engine
    // pointer, or 0 if we haven't yet observed the host calling
    // GetAppIDForCurrentPipe.
    AppId_t GetAppIDForCurrentPipe();

    // Grow a CUtlBuffer to at least 'size' bytes and set m_Put = size.
    // Uses CUtlBuffer::EnsureCapacity from steamclient, resolved on first call.
    void EnsureBufferSize(CUtlBuffer* pWrite, int32 size);

    // Resolve the real appid: if SpawnProcess rewrote to 480 return real appid,
    // otherwise fall back to GetAppIDForCurrentPipe().
    AppId_t ResolveAppId();

    // Get localized game name via GetAppDataFromAppInfo (cached).
    std::string GetGameNameByAppID(AppId_t appId);

    // Mark package 0 as changed and trigger CClientAppManager_ProcessPendingLicenseUpdates
    // Requires pCUser to have been captured (happens on first natural call to
    // MarkLicenseAsChanged, which Steam makes during license load on startup).
    void NotifyLicenseChanged();
}
