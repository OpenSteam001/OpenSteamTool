#include "LegacyCDKey.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Support/FnvHash.h"
#include "Utils/Logging/Log.h"

#include <cstdio>

namespace LegacyCDKey {
    namespace {
        // Unambiguous 32-char alphabet (Crockford base32: no I, L, O, U).
        constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

        // Deterministic XXXX-XXXX-XXXX-XXXX (16 chars + 3 dashes = 19), seeded by
        // appId+accountId so relaunches on the same account produce the same key.
        std::string Synthesize(AppId_t appId, uint32 accountId) {
            char seedA[32];
            std::snprintf(seedA, sizeof(seedA), "%u:%u", appId, accountId);
            char seedB[40];
            std::snprintf(seedB, sizeof(seedB), "OST:%u:%u", accountId, appId);

            uint64_t bits = (static_cast<uint64_t>(Fnv1aHash(seedA)) << 32) | Fnv1aHash(seedB);

            std::string key;
            key.reserve(19);
            for (int i = 0; i < 16; ++i) {
                if (i && (i % 4) == 0) key.push_back('-');
                key.push_back(kAlphabet[bits & 31]);
                bits = (bits >> 4) | (bits << 60);   // rotate so every char draws fresh bits
            }
            return key;
        }
    }

    std::optional<std::string> Resolve(AppId_t appId, uint32 accountId) {
        // Never touch games the user actually manages elsewhere / really owns —
        // same guard AppTicket.cpp uses.
        if (!LuaConfig::HasDepot(appId)) {
            LOG_TRACE("LegacyCDKey: appId={} not managed, passing through", appId);
            return std::nullopt;
        }

        if (auto override_ = LuaConfig::GetLegacyCDKey(appId)) {
            LOG_INFO("LegacyCDKey: appId={} -> override", appId);
            return override_;
        }

        std::string synthesised = Synthesize(appId, accountId);
        LOG_INFO("LegacyCDKey: appId={} -> synthesised {}", appId, synthesised);
        return synthesised;
    }
}
