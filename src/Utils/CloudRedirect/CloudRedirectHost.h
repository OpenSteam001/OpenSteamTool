#pragma once

#include <cstdint>

// Hosts CloudRedirect's prebuilt cloud_redirect.dll inside the Steam process and
// drives its third-party client API (see CloudRedirect/src/common/cr_api.h):
//   - loads the DLL and resolves the CR_* exports,
//   - calls CR_InitCloudSave in cloud-save-only mode,
//   - registers every addappid() game as a redirected ("namespace") app,
//   - forwards Cloud.* RPCs from the NetPacket hook to CR_HandleCloudRpc.
//
// All entry points are safe no-ops unless [cloud].enabled is set and the DLL
// loaded and initialised successfully.
namespace CloudRedirectHost {

    // Load + initialise. Called once from the init worker thread, after the
    // Steam hooks are installed. steamInstallPath is the Steam root directory.
    void Initialize(const char* steamInstallPath);

    // Re-push the current unlocked-app set to CloudRedirect. Called after a Lua
    // hot-reload so the redirected set tracks addappid() changes.
    void SyncAppSet();

    // True once the DLL is loaded and CR_InitCloudSave succeeded.
    bool IsActive();

    // Whether CloudRedirect is currently redirecting saves for this appid.
    bool IsApp(uint32_t appId);

    // Bridge from the NetPacket hook: forwards a single Cloud.* RPC to
    // CloudRedirect. Returns false when not handled (caller chains to original).
    bool HandleCloudRpc(const char* method, uint32_t appId, uint32_t accountId,
                        const uint8_t* reqBody, uint32_t reqLen,
                        uint8_t* respBuf, uint32_t respMaxLen,
                        uint32_t* respLen, int32_t* eresult);

    void SetAccountId(uint32_t accountId);
    void NotifyAppRunning(uint32_t appId, bool running);
    void NotifyStatsStored(uint32_t appId);

    struct AchievementBlock {
        uint32_t statId;
        uint32_t bits;
        uint32_t unlockTimes[32];
    };
    // Returns number of blocks written (0 if none/disabled).
    uint32_t GetAchievements(uint32_t appId, AchievementBlock* out, uint32_t maxBlocks);

    // Teardown. Called from DLL_PROCESS_DETACH.
    void Shutdown();

}
