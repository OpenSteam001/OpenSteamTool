#include "Hooks_IPC.h"
#include "Hooks_IPC_ISteamUtils.h"
#include "Hooks_IPC_ISteamUser.h"
#include "Hooks_Misc.h"
#include "Steam/Callback.h"
#include "Utils/Log.h"

namespace {

    // ── IClientUtils::GetAPICallResult request args ──────────────
    struct GetAPICallResultRequest {
        uint64  hSteamAPICall;     // +0
        uint32  cubCallback;       // +8
        uint32  iCallbackExpected; // +12
    };

    // ── Helper: write the GetAPICallResult response boilerplate ───
    template<typename CallbackT, typename F>
    bool WriteCallbackResponse(CUtlBuffer* pWrite, F&& fill)
    {
        constexpr int32 total = 1 + 1 + sizeof(CallbackT) + 1;
        if (pWrite->m_Put < total) return false;

        uint8* base = pWrite->m_Memory.m_pMemory;
        base[0] = RESPONSE_PREFIX;
        base[1] = 1;
        base[2 + sizeof(CallbackT)] = 0;

        auto* cb = reinterpret_cast<CallbackT*>(base + 2);
        fill(*cb);
        return true;
    }

    // ── Handler: IClientUtils::GetAppID ──────────────────────────
    //  SpawnProcess rewrites pGameID to 480 for OnlineFix games,
    //  so steamclient returns 480.  Restore the real app_id.
    void Handler_IClientUtils_GetAppID(
        CSteamPipeClient* pipe, CUtlBuffer*, CUtlBuffer* pWrite)
    {
        AppId_t realAppId = Hooks_Misc::ResolveAppId();
        if (!realAppId || pWrite->m_Put < 5) return;

        AppId_t current = *reinterpret_cast<const AppId_t*>(pWrite->Base() + 1);
        if (current == realAppId) return;

        *reinterpret_cast<AppId_t*>(pWrite->Base() + 1) = realAppId;
        LOG_IPC_INFO("GetAppID: spoof response {} -> {}", current, realAppId);
    }

    // ════════════════════════════════════════════════════════════════
    //  GetAPICallResult per-callback handlers
    // ════════════════════════════════════════════════════════════════

    bool HandleCallback_EncryptedAppTicketResponse(
        CUtlBuffer* pWrite, uint64 hAsyncCall, uint32 cubCallback)
    {
        AppId_t appId = Hooks_IPC_ISteamUser::LookupEticketAsyncCall(hAsyncCall);
        if (!appId) return false;

        LOG_IPC_DEBUG("GetAPICallResult: EncryptedAppTicketResponse hAsyncCall=0x{:016X} "
                  "AppId={} - injecting k_EResultOK", hAsyncCall, appId);

        if (!WriteCallbackResponse<EncryptedAppTicketResponse_t>(pWrite, [](auto& cb) {
            cb.m_eResult = k_EResultOK;
        })) return false;

        Hooks_IPC_ISteamUser::EraseEticketAsyncCall(hAsyncCall);
        return true;
    }

    struct GacrDispatchEntry {
        uint32  callbackId;
        bool  (*handler)(CUtlBuffer* pWrite, uint64 hAsyncCall, uint32 cubCallback);
    };

    constexpr GacrDispatchEntry g_GacrDispatch[] = {
        { EncryptedAppTicketResponse_t::k_iCallback, HandleCallback_EncryptedAppTicketResponse },
    };

    // ── Handler: IClientUtils::GetAPICallResult ──────────────────
    void Handler_IClientUtils_GetAPICallResult(
        CSteamPipeClient*, CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        if (pRead->m_Put < OFFSET_ARGS + sizeof(GetAPICallResultRequest)) return;

        const auto* req = reinterpret_cast<const GetAPICallResultRequest*>(
            pRead->Base() + OFFSET_ARGS);

        AppId_t appId = Hooks_Misc::GetAppIDForCurrentPipeWrap();
        LOG_IPC_DEBUG("GetAPICallResult: hAsyncCall=0x{:016X} AppId={} iCallback={} cubCallback={}",
                  req->hSteamAPICall, appId, req->iCallbackExpected, req->cubCallback);
        for (auto& entry : g_GacrDispatch) {
            if (entry.callbackId == req->iCallbackExpected) {
                entry.handler(pWrite, req->hSteamAPICall, req->cubCallback);
                return;
            }
        }
    }

    const Hooks_IPC::IpcHandlerEntry g_Entries[] = {
        ADD_IPC_HANDLER(IClientUtils, GetAppID),
        ADD_IPC_HANDLER(IClientUtils, GetAPICallResult),
    };

} // namespace

namespace Hooks_IPC_ISteamUtils {
    void Register() {
        Hooks_IPC::RegisterHandlers(g_Entries, std::size(g_Entries));
    }
}
