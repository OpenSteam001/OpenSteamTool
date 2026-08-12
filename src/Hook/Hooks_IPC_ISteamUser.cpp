#include "Hooks_IPC.h"
#include "Hooks_IPC_ISteamUser.h"
#include "PendingAPICalls.h"
#include "Utils/Tickets/AppTicket.h"
#include "Utils/Tickets/EticketClient.h"
#include "Pipe/PipeManager.h"
#include "Pipe/Features/DenuvoAuth/DenuvoAuth.h"
#include "Utils/Logging/Log.h"
#include "Hooks_Misc.h"
#include "Utils/Config/LuaConfig.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace {
    using namespace IPCMessages::IClientUser;

    // Fresh, nonce-bound etickets minted on-demand in RequestEncryptedAppTicket
    // (keyed by appId) and consumed by GetEncryptedAppTicket on the same launch.
    // Lets the strict-Denuvo path serve a ticket matching the launch nonce while
    // keeping GetEncryptedAppTicket's credential-store serve as the fallback.
    std::mutex g_freshEticketMutex;
    std::unordered_map<AppId_t, std::vector<uint8_t>> g_freshEticket;

    // [Post-Handler]: IClientUser::GetSteamID
    void HandlerPost_IClientUser_GetSteamID(CPipeClient* pipe,CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        AppId_t appId = Hooks_Misc::ResolveAppId();
        GetSteamIDResp resp{pWrite};
        if (!resp.ok()) return;

        // Spoof whenever we have a pool-account ticket for this app, not just
        // inside the Denuvo auth window. Denuvo reads its cached offline
        // license on second launch and calls GetSteamID BEFORE or AFTER the
        // auth window to verify it — if we only spoof inside the window the
        // real SteamID leaks out and mismatches the license → 012.
        // GetSpoofSteamID returns 0 for apps with no credential-store ticket
        // (real owners, non-tracked apps) so the spoof is naturally scoped.
        const uint64 spoofed = AppTicket::GetSpoofSteamID(appId);
        if (!spoofed) {
            return;
        }

        LOG_IPC_DEBUG("IClientUser::GetSteamID: AppId={} Original: {} -> Spoofed: 0x{:X}({})", 
                        appId,resp.DebugString(),spoofed, spoofed);
        resp.set_returnValue(spoofed);
    }

    // [Post-Handler]: IClientUser::GetAppOwnershipTicketExtendedData
    void HandlerPost_IClientUser_GetAppOwnershipTicketExtendedData(CPipeClient* pipe, CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        GetAppOwnershipTicketExtendedDataReq req{pRead};
        if (!req.ok()) return;

        LOG_IPC_DEBUG("IClientUser::GetAppOwnershipTicketExtendedData:{}", req.DebugString());
        if (req.cbMaxTicket() < 0) return;

        AppTicket::AppOwnershipTicket ticket{};
        AppId_t appId = req.unAppID() == kOnlineFixAppId ? Hooks_Misc::ResolveAppId() : req.unAppID();
        
        AppTicket::AppTicketSource ticketSource;
        if (PipeManager::DenuvoAuth::IsAuthorizedPipe(pipe)) {
            ticketSource = AppTicket::AppTicketSource::CredentialStoreOnly;
        } else {
            // Outside the auth window: prefer credential-store ticket (pool SteamID)
            // over ForgeOnly (which uses app 7's ticket and carries the real SteamID).
            // When the 858 network spoof is also active, both paths must agree on the
            // same SteamID or Denuvo cross-checks them and rejects (error 54).
            LOG_IPC_DEBUG("IClientUser::GetAppOwnershipTicketExtendedData: AppId={} not in authorization window, credential store preferred", appId);
            ticketSource = AppTicket::AppTicketSource::CredentialStoreThenForge;
        }        
        if (!AppTicket::GetAppOwnershipTicket(appId, ticket, ticketSource)) return;

        if (ticket.data.size() > static_cast<size_t>(req.cbMaxTicket())) {
            LOG_IPC_WARN("IClientUser::GetAppOwnershipTicketExtendedData: AppId={} ticket too large ({} bytes) for buffer ({} bytes)",
                         appId, ticket.data.size(), req.cbMaxTicket());
            return;
        }

        GetAppOwnershipTicketExtendedDataResp resp{pWrite, static_cast<size_t>(req.cbMaxTicket())};
        if (!resp.ok()) return;

        resp.set_returnValue(ticket.totalSize);
        if (!resp.set_pTicket(ticket.data)) return;
        resp.set_piAppId(ticket.appIdOffset);
        resp.set_piSteamId(ticket.steamIdOffset);
        resp.set_piSignature(ticket.signatureOffset);
        resp.set_pcbSignature(ticket.signatureSize);

        LOG_IPC_DEBUG("IClientUser::GetAppOwnershipTicketExtendedData: AppId={} {}", 
                        appId,resp.DebugString());
    }

    // [Post-Handler]: IClientUser::RequestEncryptedAppTicket
    // Reads the hAsyncCall steamclient already wrote into the response,
    // so we know which AppId to mint an eticket for in GetAPICallResult.
    void HandlerPost_IClientUser_RequestEncryptedAppTicket(CPipeClient* pipe, CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        RequestEncryptedAppTicketResp resp{pWrite};
        if (!resp.ok()) return;

        AppId_t appId = Hooks_Misc::ResolveAppId();

        // Strict Denuvo passes a per-launch nonce (pData) here and rejects a
        // stale/cached ticket (88500012). Try an on-demand mint bound to that
        // exact nonce; cache it for GetEncryptedAppTicket. Any failure falls
        // through to the static credential store below.
        {
            RequestEncryptedAppTicketReq req{pRead};
            std::span<const uint8_t> nonce;
            if (req.ok()) nonce = req.pData();
            // Whatever account the registry's current static ticket already
            // belongs to (0 if none) — lets the backend pin the mint to that
            // SAME account instead of risking a different pool pick.
            const uint64_t existingSteamId = AppTicket::ExtractSteamIdFromTicketBytes(
                AppTicket::GetAppOwnershipTicketFromCredentialStore(appId));
            // Mint a fresh eticket whenever the credential store already has a ticket
            // for this app (existingSteamId != 0). The minted eticket is pinned to
            // the same pool account via existingSteamId, which matches GetSteamID's
            // spoof (also sourced from the credential store via CredentialStoreThenForge)
            // — no error-54 risk. This fixes error 05 for games launched more than
            // 30 min after activation (stored ticket expired, fresh mint is current).
            if (existingSteamId != 0) {
                if (auto fresh = EticketClient::FetchFreshEticket(appId, nonce, existingSteamId)) {
                    std::lock_guard<std::mutex> lock(g_freshEticketMutex);
                    g_freshEticket[appId] = std::move(*fresh);
                }
            }
        }

        bool haveFresh;
        {
            std::lock_guard<std::mutex> lock(g_freshEticketMutex);
            haveFresh = g_freshEticket.find(appId) != g_freshEticket.end();
        }

        std::vector<uint8_t> ticket = AppTicket::GetEncryptedTicketFromCredentialStore(appId);
        if (ticket.empty() && !haveFresh) {
            LOG_IPC_DEBUG("RequestEncryptedAppTicket: AppId={} - no cached eticket, skip", appId);
            return;
        }

        const SteamAPICall_t hAsyncCall = resp.returnValue();
        PendingAPICalls::RecordEncryptedTicket(hAsyncCall, appId);
        LOG_IPC_DEBUG("RequestEncryptedAppTicket: AppId={} hAsyncCall=0x{:X} - recorded (fresh={})",
                      appId, hAsyncCall, haveFresh);
    }

    // [Post-Handler]: IClientUser::GetEncryptedAppTicket
    void HandlerPost_IClientUser_GetEncryptedAppTicket(CPipeClient* pipe, CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        AppId_t appId = Hooks_Misc::ResolveAppId();

        // Prefer a fresh nonce-bound ticket minted in RequestEncryptedAppTicket;
        // fall back to the static credential-store ticket (titles that don't
        // need the on-demand path keep working unchanged).
        std::vector<uint8_t> ticket;
        {
            std::lock_guard<std::mutex> lock(g_freshEticketMutex);
            auto it = g_freshEticket.find(appId);
            if (it != g_freshEticket.end()) ticket = it->second;
        }
        const bool fromFresh = !ticket.empty();
        if (ticket.empty()) {
            ticket = AppTicket::GetEncryptedTicketFromCredentialStore(appId);
        }
        if (ticket.empty()) {
            LOG_IPC_DEBUG("GetEncryptedAppTicket: AppId={} - no cached eticket, skip", appId);
            return;
        }
        LOG_IPC_DEBUG("GetEncryptedAppTicket: AppId={} serving source={}", appId, fromFresh ? "fresh" : "store");

        uint32 ticketSize = static_cast<uint32>(ticket.size());
        uint32 newCapacity = pWrite->Capacity() + ticketSize;
        if (!Hooks_Misc::EnsureBufferCapacity(pWrite, newCapacity,true)) {
            LOG_IPC_DEBUG("GetEncryptedAppTicket: AppId={} - failed to ensure buffer size", appId);
            return;
        }

        GetEncryptedAppTicketResp resp{pWrite};
        if (!resp.ok()) return;

        resp.set_returnValue(true);
        resp.set_pcbTicket(ticketSize);
        if (!resp.set_pTicket(ticket)) return;

        LOG_IPC_DEBUG("GetEncryptedAppTicket: AppId={} {}", appId, resp.DebugString());
    }

} // namespace

namespace Hooks_IPC_ISteamUser {
    void Register() {
        IPCHandlerEntry UserEntries[] = {
            ADD_IPC_POST_HANDLER(IClientUser, GetSteamID),
            ADD_IPC_POST_HANDLER(IClientUser, GetAppOwnershipTicketExtendedData),
            ADD_IPC_POST_HANDLER(IClientUser, RequestEncryptedAppTicket),
            ADD_IPC_POST_HANDLER(IClientUser, GetEncryptedAppTicket),
        };
        Hooks_IPC::RegisterHandlers(UserEntries);
    }
}
