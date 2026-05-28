#include "Hooks_IPC.h"
#include "Hooks_IPC_ISteamUser.h"
#include "Utils/AppTicket.h"
#include "Utils/Log.h"
#include "Hooks_Misc.h"

namespace {
    // ── eticket: hAsyncCall → appId mapping ────────────────────────
    std::unordered_map<uint64, AppId_t> g_EticketAsyncCalls;

    // ── Handler: IClientUser::GetSteamID ──────────────────────────
    //  Request:  no args
    //  Response: [uint8 prefix=0x0B][uint64 SteamID]   (9 bytes)
    void Handler_IClientUser_GetSteamID(CSteamPipeClient* pipe,
                                         CUtlBuffer*, CUtlBuffer* pWrite)
    {
        AppId_t appId = Hooks_Misc::ResolveAppId();
        const uint64 spoofed = AppTicket::GetSpoofSteamID(appId);
        if (!spoofed) {
            LOG_IPC_WARN("IClientUser::GetSteamID: AppId={} no valid steamid - cannot spoof", appId);
            return;
        }
        uint8* base = pWrite->Base();
        base[0] = RESPONSE_PREFIX;
        memcpy(base + 1, &spoofed, sizeof(spoofed));
        LOG_IPC_DEBUG("IClientUser::GetSteamID: AppId={} -> Spoofed: 0x{:X}({})", appId, spoofed, spoofed);
    }

    // ── Handler: IClientUser::GetAppOwnershipTicketExtendedData ───
    void Handler_IClientUser_GetAppOwnershipTicketExtendedData(
        CSteamPipeClient* pipe, CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        const uint8* reqData = pRead->Base();
        const int32  reqSize = pRead->m_Put;
        if (reqSize < OFFSET_ARGS + 8) return;
        const uint8* args = reqData + OFFSET_ARGS;
        const uint32 reqAppID   = *reinterpret_cast<const uint32*>(args);
        const int32  reqBufSize = *reinterpret_cast<const int32*>(args + 4);

        LOG_IPC_DEBUG("IClientUser::GetAppOwnershipTicketExtendedData: req AppID={} bufSize={}",
                  reqAppID, reqBufSize);

        std::vector<uint8_t> ticket = AppTicket::GetAppOwnershipTicketFromRegistry(reqAppID);
        if (ticket.empty() || ticket.size() < 4) return;

        const uint32 ticketSize = static_cast<uint32>(ticket.size());
        const uint32 sigOffset  = *reinterpret_cast<const uint32*>(ticket.data());

        const uint32 totalSize = 1 + 4 + reqBufSize + 16;
        if (static_cast<uint32>(pWrite->m_Put) < totalSize) return;

        uint8* base = pWrite->Base();

        base[0] = RESPONSE_PREFIX;
        memcpy(base + 1, &ticketSize, 4);
        const uint32 copySize = (ticketSize < static_cast<uint32>(reqBufSize))
                              ? ticketSize : static_cast<uint32>(reqBufSize);
        memcpy(base + 5, ticket.data(), copySize);
        if (copySize < static_cast<uint32>(reqBufSize))
            memset(base + 5 + copySize, 0, reqBufSize - copySize);

        const uint32 piAppId      = 16;
        const uint32 piSteamId    = 8;
        const uint32 piSignature  = sigOffset;
        const uint32 pcbSignature = 128;
        const uint32 outOff = 5 + reqBufSize;
        memcpy(base + outOff,      &piAppId,      4);
        memcpy(base + outOff + 4,  &piSteamId,    4);
        memcpy(base + outOff + 8,  &piSignature,  4);
        memcpy(base + outOff + 12, &pcbSignature, 4);

        AppId_t appId = Hooks_Misc::ResolveAppId();
        LOG_IPC_DEBUG("IClientUser::GetAppOwnershipTicketExtendedData: AppId={} -> {} bytes "
                  "(sigOffset={})", appId, ticketSize, sigOffset);
    }

    // ── Handler: IClientUser::RequestEncryptedAppTicket ──────────
    void Handler_IClientUser_RequestEncryptedAppTicket(
        CSteamPipeClient* pipe, CUtlBuffer*, CUtlBuffer* pWrite)
    {
        if (pWrite->m_Put < 9) return;

        AppId_t appId = Hooks_Misc::ResolveAppId();
        auto ticket = AppTicket::GetEncryptedTicketFromRegistry(appId);
        if (ticket.empty()) {
            LOG_IPC_DEBUG("RequestEncryptedAppTicket: AppId={} - no cached eticket, skip", appId);
            return;
        }

        uint8* base = pWrite->Base();
        uint64 hAsyncCall;
        memcpy(&hAsyncCall, base + 1, sizeof(hAsyncCall));

        g_EticketAsyncCalls[hAsyncCall] = appId;
        LOG_IPC_DEBUG("RequestEncryptedAppTicket: AppId={} hAsyncCall=0x{:016X} - recorded", appId, hAsyncCall);
    }

    // ── Handler: IClientUser::GetEncryptedAppTicket ───────────────
    void Handler_IClientUser_GetEncryptedAppTicket(
        CSteamPipeClient* pipe, CUtlBuffer*, CUtlBuffer* pWrite)
    {
        AppId_t appId = Hooks_Misc::ResolveAppId();
        auto ticket = AppTicket::GetEncryptedTicketFromRegistry(appId);
        if (ticket.empty()) {
            LOG_IPC_DEBUG("GetEncryptedAppTicket: AppId={} - no cached eticket, skip", appId);
            return;
        }

        const uint32 ticketSize = static_cast<uint32>(ticket.size());
        const int32 totalSize = 1 + 1 + 4 + ticketSize;
        if (!Hooks_Misc::EnsureBufferSize(pWrite, totalSize)) {
            LOG_IPC_DEBUG("GetEncryptedAppTicket: AppId={} - failed to ensure buffer size", appId);
            return;
        }
        pWrite->m_Put = totalSize;

        uint8* base = pWrite->Base();
        base[0] = RESPONSE_PREFIX;
        base[1] = 1;
        memcpy(base + 2, &ticketSize, sizeof(ticketSize));
        memcpy(base + 6, ticket.data(), ticketSize);

        LOG_IPC_DEBUG("GetEncryptedAppTicket: AppId={} -> {} bytes", appId, ticketSize);
    }

    const Hooks_IPC::IpcHandlerEntry g_Entries[] = {
        ADD_IPC_HANDLER(IClientUser, GetSteamID),
        ADD_IPC_HANDLER(IClientUser, GetAppOwnershipTicketExtendedData),
        ADD_IPC_HANDLER(IClientUser, RequestEncryptedAppTicket),
        ADD_IPC_HANDLER(IClientUser, GetEncryptedAppTicket),
    };

} // namespace

namespace Hooks_IPC_ISteamUser {
    void Register() {
        Hooks_IPC::RegisterHandlers(g_Entries, std::size(g_Entries));
    }

    AppId_t LookupEticketAsyncCall(uint64 hAsyncCall) {
        auto it = g_EticketAsyncCalls.find(hAsyncCall);
        return it != g_EticketAsyncCalls.end() ? it->second : 0;
    }
    void EraseEticketAsyncCall(uint64 hAsyncCall) {
        g_EticketAsyncCalls.erase(hAsyncCall);
    }
}
