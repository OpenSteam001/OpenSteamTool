#include "CloudRedirectHost.h"

#include "OSTPlatform/include/DynamicLibrary.h"
#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"
#include "Utils/Logging/Log.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <vector>

namespace CloudRedirectHost {
namespace {

    // --- CloudRedirect third-party client ABI (CloudRedirect/src/common/cr_api.h)
    // Declared locally so OpenSteamTool does not need CloudRedirect's headers.
    using CR_NotifyFn        = void (*)(int level, const char* title, const char* message);
    using CR_InitCloudSave_t = bool (*)(const char* steamPath, CR_NotifyFn notify);
    using CR_HandleCloudRpc_t = bool (*)(const char* method, uint32_t appId, uint32_t accountId,
                                         const uint8_t* reqBody, uint32_t reqLen,
                                         uint8_t* respBuf, uint32_t respMaxLen,
                                         uint32_t* respLen, int32_t* eresult);
    using CR_AddApp_t   = void (*)(uint32_t appId);
    using CR_RemoveApp_t = void (*)(uint32_t appId);
    using CR_IsApp_t    = bool (*)(uint32_t appId);
    using CR_SetApps_t          = void (*)(const uint32_t* appIds, uint32_t count);
    using CR_Shutdown_t         = void (*)();
    using CR_EnableStatsSync_t   = void (*)(bool, bool);
    using CR_SetAccountId_t      = void (*)(uint32_t);
    using CR_NotifyAppRunning_t  = void (*)(uint32_t, bool);
    using CR_NotifyStatsStored_t = void (*)(uint32_t);
    using CR_GetAchievements_t    = uint32_t (*)(uint32_t, CloudRedirectHost::AchievementBlock*, uint32_t);
    using CR_InstallVtableHooks_t = bool (*)();

    std::mutex        g_mutex;
    std::atomic<bool> g_active{false};
    OSTPlatform::DynamicLibrary::ModuleHandle g_module = nullptr;

    CR_InitCloudSave_t     g_initCloudSave     = nullptr;
    CR_HandleCloudRpc_t    g_handleCloudRpc    = nullptr;
    CR_SetApps_t           g_setApps           = nullptr;
    CR_IsApp_t             g_isApp             = nullptr;
    CR_Shutdown_t          g_shutdownFn        = nullptr;
    CR_EnableStatsSync_t    g_enableStatsSync    = nullptr;
    CR_SetAccountId_t       g_setAccountId       = nullptr;
    CR_NotifyAppRunning_t   g_notifyAppRunning   = nullptr;
    CR_NotifyStatsStored_t  g_notifyStatsStored  = nullptr;
    CR_GetAchievements_t    g_getAchievements    = nullptr;
    CR_InstallVtableHooks_t g_installVtableHooks = nullptr;

    // Routes CloudRedirect's notifications into OpenSteamTool's log instead of
    // popping a MessageBox from inside Steam.
    void CloudNotify(int level, const char* title, const char* message) {
        const char* t = title ? title : "CloudRedirect";
        const char* m = message ? message : "";
        switch (level) {
        case 2:  LOG_ERROR("[CloudRedirect] {}: {}", t, m); break;
        case 1:  LOG_WARN("[CloudRedirect] {}: {}", t, m);  break;
        default: LOG_INFO("[CloudRedirect] {}: {}", t, m);  break;
        }
    }

    std::filesystem::path ResolveLibraryPath(const std::string& steamRoot,
                                             const std::string& configured) {
        if (configured.empty())
            return std::filesystem::path(steamRoot) / "cloud_redirect.dll";

        std::filesystem::path lib(configured);
        if (lib.is_absolute())
            return lib;
        return std::filesystem::path(steamRoot) / lib;
    }

    template <typename T>
    bool ResolveSymbol(OSTPlatform::DynamicLibrary::ModuleHandle module,
                       const char* name, T& out) {
        out = reinterpret_cast<T>(OSTPlatform::DynamicLibrary::GetSymbol(module, name));
        if (!out) {
            LOG_WARN("CloudRedirect: export {} not found in cloud_redirect.dll", name);
            return false;
        }
        return true;
    }

} // namespace

void Initialize(const char* steamInstallPath) {
    const Config::CloudSettings cloud = Config::GetCloudSettings();
    if (!cloud.enabled) {
        LOG_INFO("CloudRedirect: [cloud].enabled is false, cloud save redirection disabled");
        return;
    }
    if (!steamInstallPath || steamInstallPath[0] == '\0') {
        LOG_WARN("CloudRedirect: empty Steam install path, cannot initialise");
        return;
    }

    std::lock_guard lock(g_mutex);
    if (g_active.load(std::memory_order_acquire)) return;

    const std::filesystem::path libPath = ResolveLibraryPath(steamInstallPath, cloud.library);
    if (!std::filesystem::exists(libPath)) {
        LOG_WARN("CloudRedirect: cloud_redirect.dll not found at {}", libPath.string());
        return;
    }

    g_module = OSTPlatform::DynamicLibrary::Load(libPath);
    if (!g_module) {
        LOG_WARN("CloudRedirect: failed to load {} (err={})",
                 libPath.string(), OSTPlatform::DynamicLibrary::GetLastErrorCode());
        return;
    }

    bool ok = true;
    ok &= ResolveSymbol(g_module, "CR_InitCloudSave",  g_initCloudSave);
    ok &= ResolveSymbol(g_module, "CR_HandleCloudRpc", g_handleCloudRpc);
    ok &= ResolveSymbol(g_module, "CR_SetApps",        g_setApps);
    ok &= ResolveSymbol(g_module, "CR_IsApp",          g_isApp);
    ok &= ResolveSymbol(g_module, "CR_Shutdown",       g_shutdownFn);
    if (!ok) {
        LOG_WARN("CloudRedirect: cloud_redirect.dll is missing required exports, disabling");
        g_module = nullptr;
        return;
    }

    // Optional (CR 2.2.5+)
    ResolveSymbol(g_module, "CR_EnableStatsSync",    g_enableStatsSync);
    ResolveSymbol(g_module, "CR_SetAccountId",       g_setAccountId);
    ResolveSymbol(g_module, "CR_NotifyAppRunning",   g_notifyAppRunning);
    ResolveSymbol(g_module, "CR_NotifyStatsStored",  g_notifyStatsStored);
    ResolveSymbol(g_module, "CR_GetAchievements",    g_getAchievements);
    ResolveSymbol(g_module, "CR_InstallVtableHooks", g_installVtableHooks);

    if (!g_initCloudSave(steamInstallPath, &CloudNotify)) {
        LOG_WARN("CloudRedirect: CR_InitCloudSave failed, disabling cloud save redirection");
        g_module = nullptr;
        return;
    }

    g_active.store(true, std::memory_order_release);
    LOG_INFO("CloudRedirect: loaded {} and initialised cloud save redirection",
             libPath.string());

    if (g_enableStatsSync) {
        g_enableStatsSync(true, true);
        LOG_INFO("CloudRedirect: stats sync registered");
    }

    // Push the current unlocked-app set without re-locking g_mutex.
    std::vector<AppId_t> depots = LuaConfig::GetAllDepotIds();
    std::vector<uint32_t> appIds(depots.begin(), depots.end());
    g_setApps(appIds.empty() ? nullptr : appIds.data(),
              static_cast<uint32_t>(appIds.size()));
    LOG_INFO("CloudRedirect: registered {} redirected app(s)", appIds.size());

    // Vtable hooks let CR handle Cloud RPCs synchronously (slot4 semantics).
    if (g_installVtableHooks) {
        if (g_installVtableHooks())
            LOG_INFO("CloudRedirect: vtable hooks installed");
        else
            LOG_WARN("CloudRedirect: vtable hook install failed, using packet-layer path");
    }
}

void SyncAppSet() {
    if (!g_active.load(std::memory_order_acquire) || !g_setApps) return;

    std::vector<AppId_t> depots = LuaConfig::GetAllDepotIds();
    std::vector<uint32_t> appIds(depots.begin(), depots.end());
    g_setApps(appIds.empty() ? nullptr : appIds.data(),
              static_cast<uint32_t>(appIds.size()));
    LOG_DEBUG("CloudRedirect: re-synced redirected app set ({} app(s))", appIds.size());
}

bool IsActive() {
    return g_active.load(std::memory_order_acquire);
}

bool IsApp(uint32_t appId) {
    if (!g_active.load(std::memory_order_acquire) || !g_isApp) return false;
    return g_isApp(appId);
}

bool HandleCloudRpc(const char* method, uint32_t appId, uint32_t accountId,
                    const uint8_t* reqBody, uint32_t reqLen,
                    uint8_t* respBuf, uint32_t respMaxLen,
                    uint32_t* respLen, int32_t* eresult) {
    if (!g_active.load(std::memory_order_acquire) || !g_handleCloudRpc) return false;
    return g_handleCloudRpc(method, appId, accountId, reqBody, reqLen,
                            respBuf, respMaxLen, respLen, eresult);
}

void SetAccountId(uint32_t accountId) {
    if (!g_active.load(std::memory_order_acquire) || !g_setAccountId) return;
    g_setAccountId(accountId);
}

void NotifyAppRunning(uint32_t appId, bool running) {
    if (!g_active.load(std::memory_order_acquire) || !g_notifyAppRunning) return;
    g_notifyAppRunning(appId, running);
}

void NotifyStatsStored(uint32_t appId) {
    if (!g_active.load(std::memory_order_acquire) || !g_notifyStatsStored) return;
    g_notifyStatsStored(appId);
}

uint32_t GetAchievements(uint32_t appId, AchievementBlock* out, uint32_t maxBlocks) {
    if (!g_active.load(std::memory_order_acquire) || !g_getAchievements) return 0;
    return g_getAchievements(appId, out, maxBlocks);
}

void Shutdown() {
    std::lock_guard lock(g_mutex);
    if (!g_active.exchange(false)) return;
    if (g_shutdownFn) g_shutdownFn();
    LOG_INFO("CloudRedirect: shut down");
}

} // namespace CloudRedirectHost
