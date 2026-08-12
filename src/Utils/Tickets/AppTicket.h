#pragma once

#include "Steam/Types.h"

#include <cstdint>
#include <vector>

namespace AppTicket {
    inline constexpr uint32 kAppTicketSteamIdOffset = 8;
    inline constexpr uint32 kAppTicketAppIdOffset = 16;
    inline constexpr uint32 kAppTicketSignatureSize = 128;

    enum class AppTicketSource {
        CredentialStoreOnly,
        ForgeOnly,
        CredentialStoreThenForge,
    };

    struct AppOwnershipTicket {
        std::vector<uint8_t> data;
        uint32 totalSize = 0;
        uint32 appIdOffset = kAppTicketAppIdOffset;
        uint32 steamIdOffset = kAppTicketSteamIdOffset;
        uint32 signatureOffset = 0;
        uint32 signatureSize = kAppTicketSignatureSize;
    };

    // Reads the app ownership ticket cached by Steam's local credential store.
    // Returns an empty vector when no ticket is available.
    std::vector<uint8_t> GetAppOwnershipTicketFromCredentialStore(AppId_t appId);

    bool GetAppOwnershipTicket(AppId_t appId, AppOwnershipTicket& ticket, AppTicketSource source);

    // Reads the encrypted app ticket cached by Steam's local credential store.
    // Returns an empty vector when no ticket is available.
    std::vector<uint8_t> GetEncryptedTicketFromCredentialStore(AppId_t appId);

    //Get spoof steamID From the cached AppOwnershipTicket for the given AppId.
    uint64_t GetSpoofSteamID(AppId_t appId);

    // Parses the SteamID baked into app-ownership-ticket bytes (offset
    // kAppTicketSteamIdOffset). Returns 0 if the ticket is too short to
    // contain one. Lets callers identify which account a ticket belongs to
    // without duplicating the layout knowledge.
    uint64_t ExtractSteamIdFromTicketBytes(const std::vector<uint8_t>& ticket);

    // Write AppTicket binary data to Steam's local credential store.
    bool WriteAppOwnershipTicket(AppId_t appId, const std::vector<uint8_t>& data);

    // Write ETicket binary data to Steam's local credential store.
    bool WriteEncryptedTicket(AppId_t appId, const std::vector<uint8_t>& data);

    // Write authorized SteamID to Steam's local credential store.
    bool WriteSteamID(AppId_t appId, uint64_t steamId);
}
