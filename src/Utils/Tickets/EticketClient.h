#pragma once

#include "Steam/Types.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace EticketClient {

    // On-demand encrypted-app-ticket mint.
    //
    // Strict Denuvo titles bind their encrypted app ticket to a nonce they pass
    // into RequestEncryptedAppTicket (pData) AT LAUNCH, and reject any pre-baked
    // / stale ticket with 88500012. A ticket written to the credential store
    // before launch can never carry that nonce, so for those titles we POST
    // {app_id, nonce} to the Tokeer backend, which mints a FRESH ticket from an
    // owning pool account with userdata=nonce — matching the exact challenge the
    // running game validates.
    //
    // existingSteamId is the SteamID already baked into whatever static
    // AppTicket is sitting in the credential store for this app (0 if none).
    // It lets the backend pin the mint to that SAME account when it's one of
    // its own pool accounts — so a refreshed/nonce-bound ticket never
    // disagrees with a ticket that's already in the registry. When the
    // existing ticket belongs to an account the backend doesn't control (a
    // real owner's own ticket, or one shared peer-to-peer from someone
    // else), the backend refuses outright rather than minting a DIFFERENT
    // account's ticket — that would otherwise leave half of Denuvo's
    // identity checks (GetSteamID, the IPC ownership ticket) pointing at the
    // original account while the eticket/network ownership ticket point at
    // an unrelated pool account, guaranteeing a mismatch (88500012).
    //
    // Returns the fresh ticket bytes, or nullopt on any failure (disabled,
    // backend down, bad response, or the existing ticket is a foreign
    // account). Callers fall back to the static credential store so titles
    // that don't need this keep working unchanged.
    std::optional<std::vector<uint8_t>> FetchFreshEticket(AppId_t appId, std::span<const uint8_t> nonce, uint64_t existingSteamId = 0);

    // Same backend mint, but returns the signed app-OWNERSHIP ticket instead of
    // the eticket. Both come from ONE /eticket call (one pool account) and are
    // cached per app, so the eticket served at the IPC layer and the ownership
    // ticket spoofed at the netpacket layer always match the same account —
    // required by Denuvo titles that verify ownership over the network
    // (k_EMsgClientGetAppOwnershipTicket, e.g. Suicide Squad: KTJL).
    // nonce and existingSteamId are only used on the first fetch for an app.
    std::optional<std::vector<uint8_t>> FetchOwnershipTicket(AppId_t appId, std::span<const uint8_t> nonce, uint64_t existingSteamId = 0);

} // namespace EticketClient
