#include "Hooks_SteamUI.h"
#include "HookManager.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "steam_messages.pb.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>
#include <windows.h>

namespace
{
    using namespace std::chrono_literals;
    constexpr int  kMaxRetry      = 500;
    constexpr auto kRetryInterval = 10ms;

    static std::string_view ExtractFileName(std::string_view path) {
        const size_t pos = path.find_last_of("\\/");
        return (pos == std::string_view::npos) ? path : path.substr(pos + 1);
    }

    static std::wstring_view ExtractFileNameW(std::wstring_view path) {
        const size_t pos = path.find_last_of(L"\\/");
        return (pos == std::wstring_view::npos) ? path : path.substr(pos + 1);
    }

    static bool IsSteamClientPath(const char* path) {
        if (!path) return false;
        std::string_view fn = ExtractFileName(path);
        switch (fn.size()) {
        case 17: return _strnicmp(fn.data(), "steamclient64.dll", 17) == 0;
        case 15: return _strnicmp(fn.data(), "steamclient.dll", 15) == 0;
        case 13: return _strnicmp(fn.data(), "steamclient64", 13) == 0;
        case 11: return _strnicmp(fn.data(), "steamclient", 11) == 0;
        default: return false;
        }
    }

    static bool IsSteamClientPathW(const wchar_t* path) {
        if (!path) return false;
        std::wstring_view fn = ExtractFileNameW(path);
        switch (fn.size()) {
        case 17: return _wcsnicmp(fn.data(), L"steamclient64.dll", 17) == 0;
        case 15: return _wcsnicmp(fn.data(), L"steamclient.dll", 15) == 0;
        case 13: return _wcsnicmp(fn.data(), L"steamclient64", 13) == 0;
        case 11: return _wcsnicmp(fn.data(), L"steamclient", 11) == 0;
        default: return false;
        }
    }

    // Original pointers for system module lookup APIs
    static decltype(&GetModuleHandleA)   oGetModuleHandleA   = &GetModuleHandleA;
    static decltype(&GetModuleHandleW)   oGetModuleHandleW   = &GetModuleHandleW;
    static decltype(&GetModuleHandleExA) oGetModuleHandleExA = &GetModuleHandleExA;
    static decltype(&GetModuleHandleExW) oGetModuleHandleExW = &GetModuleHandleExW;

    HMODULE WINAPI hkGetModuleHandleA(LPCSTR lpModuleName)
    {
        if (client_hModule && IsSteamClientPath(lpModuleName)) {
            return reinterpret_cast<HMODULE>(client_hModule);
        }
        return oGetModuleHandleA(lpModuleName);
    }

    HMODULE WINAPI hkGetModuleHandleW(LPCWSTR lpModuleName)
    {
        if (client_hModule && IsSteamClientPathW(lpModuleName)) {
            return reinterpret_cast<HMODULE>(client_hModule);
        }
        return oGetModuleHandleW(lpModuleName);
    }

    BOOL WINAPI hkGetModuleHandleExA(DWORD dwFlags, LPCSTR lpModuleName, HMODULE* phModule)
    {
        if (client_hModule && !(dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) &&
            IsSteamClientPath(lpModuleName))
        {
            if (!phModule) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            if ((dwFlags & GET_MODULE_HANDLE_EX_FLAG_PIN) &&
                (dwFlags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            if (!(dwFlags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT)) {
                HMODULE dummy = nullptr;
                if (!oGetModuleHandleExA((dwFlags & GET_MODULE_HANDLE_EX_FLAG_PIN) | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                         reinterpret_cast<LPCSTR>(client_hModule), &dummy))
                {
                    *phModule = nullptr;
                    return FALSE;
                }
            }
            *phModule = reinterpret_cast<HMODULE>(client_hModule);
            return TRUE;
        }
        return oGetModuleHandleExA(dwFlags, lpModuleName, phModule);
    }

    BOOL WINAPI hkGetModuleHandleExW(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE* phModule)
    {
        if (client_hModule && !(dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) &&
            IsSteamClientPathW(lpModuleName))
        {
            if (!phModule) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            if ((dwFlags & GET_MODULE_HANDLE_EX_FLAG_PIN) &&
                (dwFlags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            if (!(dwFlags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT)) {
                HMODULE dummy = nullptr;
                if (!oGetModuleHandleExW((dwFlags & GET_MODULE_HANDLE_EX_FLAG_PIN) | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                         reinterpret_cast<LPCWSTR>(client_hModule), &dummy))
                {
                    *phModule = nullptr;
                    return FALSE;
                }
            }
            *phModule = reinterpret_cast<HMODULE>(client_hModule);
            return TRUE;
        }
        return oGetModuleHandleExW(dwFlags, lpModuleName, phModule);
    }

    HOOK_FUNC(LoadModuleWithPath, void*, const char* path, bool flags)
    {
        LOG_STEAMUI_INFO("LoadModuleWithPath called with path: {}, flags: {}",
                         path ? path : "(null)", flags);

        const bool isSteamClient = IsSteamClientPath(path);

        if (isSteamClient) {
            bool logged = false;
            for (int i = 0; i < kMaxRetry && !g_HooksInstalled.load(std::memory_order_acquire); ++i) {
                if (!logged) {
                    LOG_STEAMUI_DEBUG("LoadModuleWithPath: waiting for hooks to be installed...");
                    logged = true;
                }
                std::this_thread::sleep_for(kRetryInterval);
            }
        }

        void* h = oLoadModuleWithPath(path, flags);

        if (isSteamClient) {
            if (!g_HooksInstalled.load(std::memory_order_acquire)) {
                LOG_STEAMUI_WARN("LoadModuleWithPath: hooks initialization timed out after {} ms, aborting diversion",
                                 kMaxRetry * static_cast<int>(kRetryInterval.count()));
                return h;
            }

            if (client_hModule) {
                if (g_IsDiversionActive.load(std::memory_order_acquire)) {
                    LOG_STEAMUI_INFO("LoadModuleWithPath: diverted {} (original {:p}) -> diversion {:p}",
                                     path ? path : "steamclient64.dll", h, static_cast<void*>(client_hModule));
                } else {
                    LOG_STEAMUI_INFO("LoadModuleWithPath: returned fallback steamclient64.dll ({:p})",
                                     static_cast<void*>(client_hModule));
                }
                return client_hModule;
            }
        }

        return h;
    }

    RESOLVE_FUNC(RepeatedFieldUint32_Add, void, void* field, const uint32* value);

    CAPTURE_THIS_FUNC(GetAppByID, CSteamApp*, g_pController,void* pThis, AppId_t appId, bool bCreate);
    CAPTURE_THIS_FUNC(MarkAppChange,void*,g_pAppChangeSource,void* pThis,AppId_t appId, EAppChangeFlags changeFlags);

    HOOK_FUNC(FillInAppOverview, void *, void *pThis, void *pAppOverview, CSteamApp *pApp)
    {
        if (pApp && LuaConfig::HasDepot(pApp->nAppID, false))
        {
            uint32_t t = LuaConfig::GetPurchaseTime(pApp->nAppID);
            if (t)
            {
                pApp->PurchasedTime = t;
                LOG_STEAMUI_TRACE("FillInAppOverview: set PurchasedTime={} for appId={}",
                                  pApp->PurchasedTime, pApp->nAppID);
            }
        }
        return oFillInAppOverview(pThis, pAppOverview, pApp);
    }

    // Apps to drop from the library: queued off-thread, marked on the UI thread.
    std::mutex g_removalMutex;
    std::atomic<bool> g_hasPendingRemovals{false};
    std::vector<AppId_t> g_pendingRemovals;
    std::unordered_set<AppId_t> g_removedAppIds;

    // A full rebuild never lists removed_appid for apps still in the map
    // so re-assert our set after the snapshot is built.
    HOOK_FUNC(BuildCompleteAppOverviewChange, void, void *pController,
              CAppOverview_Change *pChange, void *optionalCallbackSlot)
    {
        oBuildCompleteAppOverviewChange(pController, pChange, optionalCallbackSlot);
        if (pChange && oRepeatedFieldUint32_Add)
        {
            std::lock_guard<std::mutex> lock(g_removalMutex);
            if (!g_removedAppIds.empty())
            {
                auto* field = pChange->mutable_removed_appid();
                for (AppId_t appId : g_removedAppIds) {
                    oRepeatedFieldUint32_Add(field, &appId);
                }
                LOG_STEAMUI_DEBUG("BuildCompleteAppOverviewChange: appended {} removed_appid entries",
                                  g_removedAppIds.size());
            }
        }
    }


    // Clearing ownership makes ShouldShowAppInLibrary() false (delta drops it,
    // the full snapshot skips it); MarkAppChange triggers the flush.
    HOOK_FUNC(CSteamUIAppControllerRunFrame, void *, void *pController)
    {
        if (CAPTURE_READY(GetAppByID) && CAPTURE_READY(MarkAppChange))
        {
            if (g_hasPendingRemovals.load(std::memory_order_acquire))
            {
                std::vector<AppId_t> draining;
                {
                    std::lock_guard<std::mutex> lock(g_removalMutex);
                    draining.swap(g_pendingRemovals);
                    g_hasPendingRemovals.store(false, std::memory_order_release);
                }
                if (!draining.empty())
                {
                    std::vector<AppId_t> uninstalledAppIds;
                    for (AppId_t appId : draining)
                    {
                        if (LuaConfig::IsOwned(appId))
                        {
                            LOG_STEAMUI_DEBUG("RunFrame: appId {} is owned again, skipping removal", appId);
                            continue;
                        }
                        if (CSteamApp *pApp = oGetAppByID(g_pController, appId, false))
                        {
                            // Only remove from the library if it's not already uninstalled
                            pApp->OwnershipFlags = k_EAppOwnershipFlags_None;
                            if (pApp->AppStateFlags == k_EAppStateUninstalled) {
                                uninstalledAppIds.push_back(appId);
                            }
                        }

                        oMarkAppChange(g_pAppChangeSource, appId, EAppChangeFlags::AppInfoOrConfig);
                    }
                    if (!uninstalledAppIds.empty()) {
                        std::lock_guard<std::mutex> lock(g_removalMutex);
                        for (AppId_t appId : uninstalledAppIds) {
                            g_removedAppIds.insert(appId);
                        }
                    }
                }
            }
        }
        return oCSteamUIAppControllerRunFrame(pController);
    }
}

namespace Hooks_SteamUI
{
    static bool g_systemHooksAttached = false;

    void Install()
    {
        ARM_CAPTURE_U(GetAppByID);
        ARM_CAPTURE_U(MarkAppChange);

        RESOLVE_U(RepeatedFieldUint32_Add);

        bool sysAttached = false;
        HOOK_BEGIN();
        INSTALL_HOOK_U(LoadModuleWithPath);
        INSTALL_HOOK_U(FillInAppOverview);
        INSTALL_HOOK_U(BuildCompleteAppOverviewChange);
        INSTALL_HOOK_U(CSteamUIAppControllerRunFrame);

        // System module handle redirection for Diversion shadow memory isolation
        if (!g_systemHooksAttached) {
            bool ok = true;
            if (!OSTPlatform::Detour::Attach(reinterpret_cast<void**>(&oGetModuleHandleA), reinterpret_cast<void*>(hkGetModuleHandleA))) ok = false;
            if (!OSTPlatform::Detour::Attach(reinterpret_cast<void**>(&oGetModuleHandleW), reinterpret_cast<void*>(hkGetModuleHandleW))) ok = false;
            if (!OSTPlatform::Detour::Attach(reinterpret_cast<void**>(&oGetModuleHandleExA), reinterpret_cast<void*>(hkGetModuleHandleExA))) ok = false;
            if (!OSTPlatform::Detour::Attach(reinterpret_cast<void**>(&oGetModuleHandleExW), reinterpret_cast<void*>(hkGetModuleHandleExW))) ok = false;
            if (ok) {
                sysAttached = true;
            } else {
                _ost_detour_transaction_ok_ = false;
            }
        }

        HOOK_END();
        if (sysAttached) {
            g_systemHooksAttached = true;
        }
    }

    void Uninstall()
    {
        bool sysDetached = false;
        UNHOOK_BEGIN();
        if (g_systemHooksAttached) {
            bool ok = true;
            if (!OSTPlatform::Detour::Detach(reinterpret_cast<void**>(&oGetModuleHandleA), reinterpret_cast<void*>(hkGetModuleHandleA))) ok = false;
            if (!OSTPlatform::Detour::Detach(reinterpret_cast<void**>(&oGetModuleHandleW), reinterpret_cast<void*>(hkGetModuleHandleW))) ok = false;
            if (!OSTPlatform::Detour::Detach(reinterpret_cast<void**>(&oGetModuleHandleExA), reinterpret_cast<void*>(hkGetModuleHandleExA))) ok = false;
            if (!OSTPlatform::Detour::Detach(reinterpret_cast<void**>(&oGetModuleHandleExW), reinterpret_cast<void*>(hkGetModuleHandleExW))) ok = false;
            if (ok) {
                sysDetached = true;
            } else {
                _ost_detour_transaction_ok_ = false;
            }
        }

        UNINSTALL_HOOK(LoadModuleWithPath);
        UNINSTALL_HOOK(FillInAppOverview);
        UNINSTALL_HOOK(BuildCompleteAppOverviewChange);
        UNINSTALL_HOOK(CSteamUIAppControllerRunFrame);
        UNHOOK_END();
        if (sysDetached) {
            g_systemHooksAttached = false;
        }
    }

    void QueueRemoval(AppId_t appId)
    {
        std::lock_guard<std::mutex> lock(g_removalMutex);
        g_pendingRemovals.push_back(appId);
        g_hasPendingRemovals.store(true, std::memory_order_release);
    }

    void CancelRemoval(AppId_t appId)
    {
        std::lock_guard<std::mutex> lock(g_removalMutex);
        std::erase(g_pendingRemovals, appId);
        g_removedAppIds.erase(appId);
        if (g_pendingRemovals.empty()) {
            g_hasPendingRemovals.store(false, std::memory_order_release);
        }
    }
}
