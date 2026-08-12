#include "EticketClient.h"

#include "OSTPlatform/include/Http.h"
#include "Utils/Logging/Log.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace EticketClient {
namespace {

    // On-demand mint endpoint — the Tokeer backend's /eticket route. Hardcoded so
    // the feature is always active (no Lua opt-in). The backend POSTs
    // {app_id, nonce(hex), existing_steam_id} and returns {eticket, appticket,
    // steam_id}. Any failure falls back to the static credential store ticket.
    constexpr const char* kEticketUrl = "http://31.57.38.79:8080/eticket";

    // Short connect timeouts so a down/unreachable backend fails fast and the
    // caller falls back; generous recv because the backend mints via a live
    // Steam CM round-trip (~1-5s).
    constexpr uint32_t kResolveMs = 2000;
    constexpr uint32_t kConnectMs = 2000;
    constexpr uint32_t kSendMs    = 3000;
    constexpr uint32_t kRecvMs    = 8000;

    struct CachedTickets {
        std::vector<uint8_t> eticket;
        std::vector<uint8_t> ownership;
        // The pool account these tickets were minted under. If the registry's
        // current account later differs (user re-activated onto a different pool
        // account mid-session), the cache is evicted and re-minted so the served
        // ticket never disagrees with the account Denuvo now sees.
        uint64_t steamId = 0;
    };

    std::mutex g_mutex;
    std::unordered_map<AppId_t, CachedTickets> g_cache;  // only successful fetches are cached
    // Apps the backend has told us it has no owning pool account for. There's no
    // point hammering the backend (or logging) on every retry within a launch, so
    // we skip on-demand for the rest of the session once we learn this.
    std::unordered_set<AppId_t> g_noOwnerApps;

    std::string ToHex(std::span<const uint8_t> bytes) {
        static const char digits[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(bytes.size() * 2);
        for (uint8_t b : bytes) {
            out.push_back(digits[b >> 4]);
            out.push_back(digits[b & 0x0F]);
        }
        return out;
    }

    int HexNibble(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool FromHex(std::string_view hex, std::vector<uint8_t>& out) {
        if (hex.empty() || (hex.size() % 2) != 0) return false;
        out.clear();
        out.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2) {
            int hi = HexNibble(hex[i]);
            int lo = HexNibble(hex[i + 1]);
            if (hi < 0 || lo < 0) return false;
            out.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
        return true;
    }

    // Extract a string field ("key":"VALUE") from our own backend's JSON.
    // Returns false when the key is absent or its value is null/empty.
    bool ExtractStringField(std::string_view body, std::string_view key, std::string& out) {
        const std::string needle = std::string("\"") + std::string(key) + "\"";
        size_t k = body.find(needle);
        if (k == std::string_view::npos) return false;
        size_t colon = body.find(':', k + needle.size());
        if (colon == std::string_view::npos) return false;
        size_t q1 = body.find('"', colon + 1);
        if (q1 == std::string_view::npos) return false;
        // A null value (e.g. "appticket":null) has no opening quote before the
        // next delimiter — guard against grabbing a later field's quote.
        size_t delim = body.find_first_of(",}", colon + 1);
        if (delim != std::string_view::npos && q1 > delim) return false;
        size_t q2 = body.find('"', q1 + 1);
        if (q2 == std::string_view::npos) return false;
        out = std::string(body.substr(q1 + 1, q2 - q1 - 1));
        return !out.empty();
    }

    // Single backend mint → both tickets. Cached per app on success; failures are
    // not cached so the next call (the game retries ownership/eticket) re-attempts
    // — except a "no owning account" verdict, which is sticky for the session.
    // nonce and existingSteamId are only used on the first fetch for an app; once
    // an entry is cached, subsequent calls (ownership vs eticket, any order) share
    // it so both layers always align to the same account.
    bool EnsureFetched(AppId_t appId, std::span<const uint8_t> nonce, uint64_t existingSteamId, CachedTickets& out) {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_noOwnerApps.count(appId)) return false;  // already known: no pool owner
            auto it = g_cache.find(appId);
            if (it != g_cache.end()) {
                // Evict if the registry's current account differs from the one we
                // cached — a re-activation onto a different pool account must not
                // be served the previous account's ticket.
                const uint64_t cachedId = it->second.steamId;
                if (existingSteamId != 0 && cachedId != 0 && cachedId != existingSteamId) {
                    LOG_IPC_DEBUG("EticketClient: appid={} registry SteamID={} differs from cached SteamID={} — evicting stale cache entry and re-minting",
                                  appId, existingSteamId, cachedId);
                    g_cache.erase(it);
                } else {
                    out = it->second;
                    return true;
                }
            }
        }

        const std::string nonceHex = ToHex(nonce);
        std::string reqBody =
            "{\"app_id\":\"" + std::to_string(appId) + "\",\"nonce\":\"" + nonceHex + "\"";
        // Only send existing_steam_id when we actually have one — an empty/zero
        // value would make the backend refuse (it's read as "a ticket exists but
        // for account 0", i.e. foreign) instead of picking an owner itself.
        if (existingSteamId != 0) {
            reqBody += ",\"existing_steam_id\":\"" + std::to_string(existingSteamId) + "\"";
        }
        reqBody += "}";

        auto r = OSTPlatform::Http::Execute(
            L"POST", kEticketUrl,
            reqBody.data(), static_cast<uint32_t>(reqBody.size()),
            L"Content-Type: application/json\r\n",
            kResolveMs, kConnectMs, kSendMs, kRecvMs);

        if (!r.ok) {
            LOG_IPC_WARN("EticketClient: on-demand fetch failed appid={} status={} ok={} (fallback to credential store)",
                         appId, r.status, r.ok);
            return false;
        }

        // 409 = the backend deliberately refused. Two distinct reasons:
        //   - no owning account: nothing in the pool owns this app → skip for the
        //     rest of the session (sticky) so we stop retrying/logging.
        //   - foreign_account: the static ticket already in the registry belongs
        //     to an account the backend doesn't operate → it (correctly) won't
        //     mint a DIFFERENT account's ticket. Fall back to the static ticket,
        //     but DON'T make it sticky — a later re-activation could change it.
        if (r.status == 409) {
            if (r.body.find("\"foreign_account\":true") != std::string::npos) {
                LOG_IPC_DEBUG("EticketClient: appid={} existing ticket belongs to an account outside our pool — skipping on-demand override for this launch",
                              appId);
            } else {
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_noOwnerApps.insert(appId);
                }
                LOG_IPC_DEBUG("EticketClient: appid={} no owning account in pool — skipping on-demand for this session",
                              appId);
            }
            return false;
        }

        if (r.status != 200) {
            LOG_IPC_WARN("EticketClient: on-demand fetch failed appid={} status={} ok={} (fallback to credential store)",
                         appId, r.status, r.ok);
            return false;
        }

        CachedTickets fetched;
        std::string hex;
        if (ExtractStringField(r.body, "eticket", hex)) {
            if (!FromHex(hex, fetched.eticket)) fetched.eticket.clear();
        }
        if (ExtractStringField(r.body, "appticket", hex)) {
            if (!FromHex(hex, fetched.ownership)) fetched.ownership.clear();
        }

        if (fetched.eticket.empty() && fetched.ownership.empty()) {
            LOG_IPC_WARN("EticketClient: backend returned no usable tickets appid={} bytes={}", appId, r.body.size());
            return false;
        }

        // Remember which pool account the backend minted under, so a later call
        // whose registry account differs triggers the eviction above.
        if (ExtractStringField(r.body, "steam_id", hex)) {
            fetched.steamId = std::strtoull(hex.c_str(), nullptr, 10);
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_cache[appId] = fetched;
            out = fetched;
        }
        LOG_IPC_INFO("EticketClient: minted appid={} eticket_bytes={} ownership_bytes={} nonce_bytes={}",
                     appId, fetched.eticket.size(), fetched.ownership.size(), nonce.size());
        return true;
    }

} // namespace

std::optional<std::vector<uint8_t>> FetchFreshEticket(AppId_t appId, std::span<const uint8_t> nonce, uint64_t existingSteamId) {
    CachedTickets t;
    if (!EnsureFetched(appId, nonce, existingSteamId, t) || t.eticket.empty()) return std::nullopt;
    return t.eticket;
}

std::optional<std::vector<uint8_t>> FetchOwnershipTicket(AppId_t appId, std::span<const uint8_t> nonce, uint64_t existingSteamId) {
    CachedTickets t;
    if (!EnsureFetched(appId, nonce, existingSteamId, t) || t.ownership.empty()) return std::nullopt;
    return t.ownership;
}

} // namespace EticketClient
