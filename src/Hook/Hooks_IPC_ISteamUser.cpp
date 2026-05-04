#include "Hooks_IPC_ISteamUser.h"
#include "Hooks_IPC.h"
#include "Utils/AppTicket.h"
#include "Utils/Log.h"

namespace {

    constexpr uint32 HASH_IClientUser_GetSteamID                        = 0xD6FC3200;
    constexpr uint32 HASH_IClientUser_GetAppOwnershipTicketExtendedData = 0xC7E71245;

    // ── Handler: IClientUser::GetSteamID ──────────────────────────
    //  Request:  no args
    //  Response: [uint8 prefix=0x0B][uint64 SteamID]   (9 bytes)
    void Handler_IClientUser_GetSteamID(CUtlBuffer* pWrite,
                                        const uint8*, int32,
                                        AppId_t appId)
    {
        LOG_IPC_DEBUG(pWrite->DebugString());
        const uint64 spoofed = AppTicket::GetSpoofSteamID(appId);
        if (!spoofed) {
            LOG_IPC_WARN("IClientUser::GetSteamID: AppId={} no valid steamid - cannot spoof", appId);
            return;
        }
        uint8* base = pWrite->m_Memory.m_pMemory;
        base[0] = RESPONSE_PREFIX;
        memcpy(base + 1, &spoofed, sizeof(spoofed));
        LOG_IPC_DEBUG("IClientUser::GetSteamID: AppId={} -> Spoofed: 0x{:X}({})", appId, spoofed, spoofed);
    }

    // ── Handler: IClientUser::GetAppOwnershipTicketExtendedData ───
    //  Request:  [uint32 nAppID][int32 cbBufferLength]
    //  Response:
    //    [uint8  prefix     ]  0x0B
    //    [uint32 returnValue]  actual ticket bytes written
    //    [bufSize bytes     ]  ticket data padded with zeros
    //    [uint32 piAppId    ]  offset of AppID  in ticket (V4 = 16)
    //    [uint32 piSteamId  ]  offset of SteamID in ticket (V4 = 8)
    //    [uint32 piSignature]  offset of signature = ownershipTicketLength
    //    [uint32 pcbSignature] signature size in bytes (RSA-1024 = 128)
    void Handler_IClientUser_GetAppOwnershipTicketExtendedData(
        CUtlBuffer* pWrite, const uint8* reqData, int32 reqSize, AppId_t appId)
    {
        LOG_IPC_DEBUG(pWrite->DebugString());
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

        uint8* base = pWrite->m_Memory.m_pMemory;

        // prefix
        base[0] = RESPONSE_PREFIX;
        // returnValue = ticketSize
        memcpy(base + 1, &ticketSize, 4);
        // ticket buffer: copy ticket then zero-fill the rest
        const uint32 copySize = (ticketSize < static_cast<uint32>(reqBufSize))
                              ? ticketSize : static_cast<uint32>(reqBufSize);
        memcpy(base + 5, ticket.data(), copySize);
        if (copySize < static_cast<uint32>(reqBufSize))
            memset(base + 5 + copySize, 0, reqBufSize - copySize);
        // 4 output parameters
        const uint32 piAppId      = 16;
        const uint32 piSteamId    = 8;
        const uint32 piSignature  = sigOffset;
        const uint32 pcbSignature = 128;
        const uint32 outOff = 5 + reqBufSize;
        memcpy(base + outOff,      &piAppId,      4);
        memcpy(base + outOff + 4,  &piSteamId,    4);
        memcpy(base + outOff + 8,  &piSignature,  4);
        memcpy(base + outOff + 12, &pcbSignature,  4);

        LOG_IPC_DEBUG("IClientUser::GetAppOwnershipTicketExtendedData: AppId={} -> {} bytes "
                  "(sigOffset={})", appId, ticketSize, sigOffset);
    }

    const Hooks_IPC::IpcHandlerEntry g_Entries[] = {
        { EIPCInterface::IClientUser, HASH_IClientUser_GetSteamID,
          "IClientUser::GetSteamID",
          Handler_IClientUser_GetSteamID },
        { EIPCInterface::IClientUser, HASH_IClientUser_GetAppOwnershipTicketExtendedData,
          "IClientUser::GetAppOwnershipTicketExtendedData",
          Handler_IClientUser_GetAppOwnershipTicketExtendedData },
    };

} // namespace

namespace Hooks_IPC_ISteamUser {
    void Register() {
        Hooks_IPC::RegisterHandlers(g_Entries, std::size(g_Entries));
    }
}
