#include "Hooks_SteamUI.h"
#include "HookManager.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "steam_messages.pb.h"
#include <thread>
#include <chrono>

namespace {
    using namespace std::chrono_literals;
    constexpr int  MAX_RETRY      = 20;
    constexpr auto RETRY_INTERVAL = 300ms;

    // ── function type aliases (alphabetical) ─────────────────────────────────
    using AddProtobufAsBinary_t   = void*(__fastcall*)(void* /*args*/, void* /*proto*/);
    using GetAppByID_t            = void*(__fastcall*)(void* /*controller*/, AppId_t, bool /*create*/);
    using GetTopManager_t         = void*(__fastcall*)();

    // ── resolved function pointers ───────────────────────────────────────────
    AddProtobufAsBinary_t   oAddProtobufAsBinary = nullptr;
    GetAppByID_t            oGetAppByID          = nullptr;
    GetTopManager_t         oGetTopManager       = nullptr;

    // CSteamUIAppController offsets (see its Validate() method):
    //   +0xAB8 from top-manager -> CSteamUIAppController*
    //   +848   m_mapAppIdToCApp
    //   +1744  m_vecAppOverviewChanged
    constexpr size_t kControllerInTopManager     = 0xAB8;
    constexpr size_t kSubscriberVecOffset        = 1744;
    constexpr size_t kSubscriberVecSizeOffset    = 1760;

    constexpr size_t kArgsSize                   = 64;
    constexpr size_t kSubscriberInvokeVtableSlot = 4;

    // Cleared so BuildCompleteAppOverviewChange's filter (BIsOwned via
    // vtable[22]) also excludes the app on the next full snapshot.
    constexpr size_t kCSteamAppOwnedFlagOffset   = 28;

    HOOK_FUNC(LoadModuleWithPath, HMODULE, const char* path, bool flags) {
        LOG_INFO("LoadModuleWithPath called with path: {} , flags: {}", path, flags);
        // wait for hooks to be installed
        for (int i = 0; i < MAX_RETRY && !g_HooksInstalled.load(); ++i){
            LOG_DEBUG("LoadModuleWithPath: waiting for hooks to be installed... (attempt {}/{},interval: {})", i + 1, MAX_RETRY, RETRY_INTERVAL.count());
            std::this_thread::sleep_for(RETRY_INTERVAL);
        }
        HMODULE h = oLoadModuleWithPath(path, flags);
        if (!strcmp(path, "steamclient64.dll"))
            h = diversion_hMdoule;
        return h;
    }

    // Fetch the CSteamUIAppController via the captured getter.  Returns null
    // if the singleton chain isn't ready yet.
    void* ResolveController() {
        if (!oGetTopManager) return nullptr;
        void* topMgr = oGetTopManager();
        if (!topMgr) return nullptr;
        return *reinterpret_cast<void**>(static_cast<uint8_t*>(topMgr) + kControllerInTopManager);
    }

    // Synthesize a CAppOverview_Change proto with removed_appid=[appId] and
    // dispatch to every registered webhelper subscriber.  Leaves the host-side
    // CSteamApp alive so async holders' cached pointers stay valid.
    bool EmitRemovedAppId(void* pController, AppId_t appId) {
        alignas(8) uint8_t argsBuf[kArgsSize] = {};

        ::CAppOverview_Change msg;
        msg.add_removed_appid(appId);
        msg.set_update_complete(true);
        oAddProtobufAsBinary(argsBuf, &msg);

        void** vecData = *reinterpret_cast<void***>(
            static_cast<uint8_t*>(pController) + kSubscriberVecOffset);
        uint32_t subCount = *reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(pController) + kSubscriberVecSizeOffset);

        if (!vecData || subCount == 0) {
            LOG_STEAMUI_WARN("EmitRemovedAppId: no subscribers; appId={}", appId);
            return false;
        }

        for (uint32_t i = 0; i < subCount; ++i) {
            void* subscriber = vecData[i];
            if (!subscriber) continue;
            void** vtable = *reinterpret_cast<void***>(subscriber);
            auto invoke = reinterpret_cast<void(__fastcall*)(void*, void*)>(
                vtable[kSubscriberInvokeVtableSlot]);
            invoke(subscriber, argsBuf);
        }

        return true;
    }
}

namespace Hooks_SteamUI {
    void Install() {
        HMODULE hSteamUI = GetModuleHandleA("steamui.dll");
        if (!hSteamUI) {
            LOG_STEAMUI_WARN("steamui.dll not loaded; SteamUI hooks disabled");
            return;
        }

        HOOK_BEGIN();
        INSTALL_HOOK(hSteamUI, LoadModuleWithPath);
        HOOK_END();

        RESOLVE(hSteamUI, GetAppByID);
        RESOLVE(hSteamUI, AddProtobufAsBinary);
        RESOLVE(hSteamUI, GetTopManager);

        LOG_STEAMUI_INFO("Install: GetAppByID={}, AddProtobufAsBinary={}, GetTopManager={}",
                         reinterpret_cast<void*>(oGetAppByID),
                         reinterpret_cast<void*>(oAddProtobufAsBinary),
                         reinterpret_cast<void*>(oGetTopManager));
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(LoadModuleWithPath);
        UNHOOK_END();

        oAddProtobufAsBinary = nullptr;
        oGetAppByID = nullptr;
        oGetTopManager = nullptr;
    }

    void RemoveAppOverview(AppId_t appId) {
        if (!oAddProtobufAsBinary || !oGetTopManager || !oGetAppByID) {
            LOG_STEAMUI_WARN("RemoveAppOverview: primitives unresolved; appId={}", appId);
            return;
        }

        void* pController = ResolveController();
        if (!pController) {
            LOG_STEAMUI_WARN("RemoveAppOverview: controller singleton not initialized; appId={}", appId);
            return;
        }

        if (void* pApp = oGetAppByID(pController, appId, /*create=*/false)) {
            *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pApp) + kCSteamAppOwnedFlagOffset) &= ~1u;
        }

        if (!EmitRemovedAppId(pController, appId)) return;

        LOG_STEAMUI_INFO("RemoveAppOverview: appId={} done", appId);
    }
}
