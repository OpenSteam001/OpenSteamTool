#include "ManifestClient.h"
#include "OSTPlatform/include/Http.h"
#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"
#include "Utils/Logging/Log.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string_view>

namespace ManifestClient {

    // ── parsers ────────────────────────────────────────────────────
    using Parser = bool (*)(std::string_view body, uint64_t* out);

    static bool ParsePlainUint(std::string_view body, uint64_t* out) {
        uint64_t code = 0;
        auto [_, ec] = std::from_chars(body.data(), body.data() + body.size(), code);
        if (ec != std::errc{}) return false;
        *out = code;
        return true;
    }

    static bool ParseSteamRunJson(std::string_view body, uint64_t* out) {
        size_t key = body.find("\"content\"");
        if (key == std::string_view::npos) return false;
        size_t q1 = body.find('"', key + 9);
        if (q1 == std::string_view::npos) return false;
        size_t q2 = body.find('"', q1 + 1);
        if (q2 == std::string_view::npos) return false;
        return ParsePlainUint(body.substr(q1 + 1, q2 - q1 - 1), out);
    }

    // ── provider table ────────────────────────────────────────────
    //
    // Adding a new provider: add one row to kProviders below.
    // host / port / tls / path are all derived from the URL template
    // by Make() at compile time.
    //
    // Order matters only as the tie-break for a cold start: the first
    // entry is the initial active provider, and failures walk forward
    // from there.

    struct Provider {
        std::string_view name;          // matches [manifest] url = "..."
        const char*      urlTemplate;   // full literal with one %llu — for log & path
        Parser           parse;
    };

    consteval Provider Make(std::string_view name, const char* url, Parser parse) {
        return {name, url, parse};
    }

    static constexpr Provider kProviders[] = {
        Make("opensteamtool", "https://manifest.opensteamtool.com/%llu",       ParsePlainUint),
        Make("wudrm",         "http://gmrc.wudrm.com/manifest/%llu",           ParsePlainUint),
        Make("steamrun",      "https://manifest.steam.run/api/manifest/%llu",  ParseSteamRunJson),
    };

    static constexpr size_t kProviderCount = std::size(kProviders);

    // ── provider state ────────────────────────────────────────────
    //
    // g_stateMutex guards g_active and g_deadUntil, and nothing else. It is
    // deliberately NOT held across HTTP or Lua calls: Http::Execute opens and
    // closes its own WinHTTP session per request, so there is no shared
    // connection to serialise, while holding a lock over a dead provider's
    // timeout would queue every concurrent depot behind it until they all
    // overran the hook's kMaxWaitMs window and lost their injection.
    static std::mutex g_stateMutex;

    // Index of the last provider that answered, so the healthy one is tried
    // first on the next fetch.
    static size_t g_active = 0;                         // opensteamtool

    // Per-provider deadline; while it is in the future the provider is
    // skipped rather than paying its timeout again.
    static std::chrono::steady_clock::time_point g_deadUntil[kProviderCount];

    // Per-provider success counter, bumped on every answer.  Fetches run
    // concurrently, so a failure can land after a success for the same
    // provider -- Steam starts many depots at once, and an upstream under
    // that burst may well 429 one request while answering another.  An
    // attempt reads this before it starts and hands it back on failure; a
    // mismatch means a success has landed since, so the failure is reporting
    // state that has already been disproved and must not set a cooldown.
    static uint64_t g_okSeq[kProviderCount] = {};

    static size_t ActiveIndex() {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        return g_active;
    }

    static bool IsCoolingDown(size_t i, std::chrono::steady_clock::time_point now) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        return g_deadUntil[i] > now;
    }

    // Read before an attempt, passed back to MarkFailed afterwards.
    static uint64_t OkSeq(size_t i) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        return g_okSeq[i];
    }

    static void MarkFailed(size_t i, std::chrono::steady_clock::time_point now, uint64_t seq) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        // A success landed while this attempt was failing. Anything we could
        // say about the provider is now stale; the success is the better
        // evidence, so leave its cleared cooldown alone.
        if (g_okSeq[i] != seq) {
            LOG_MANIFEST_DEBUG("Manifest {} failure ignored (superseded by a success)",
                               kProviders[i].name);
            return;
        }
        g_deadUntil[i] = now + std::chrono::milliseconds(kProviderCooldownMs);
    }

    // Record a provider that just answered, and make it the starting point of
    // the next fetch.
    static void RecordSuccess(size_t i) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        ++g_okSeq[i];
        // Unconditionally, including when this provider is already active: a
        // 200 is proof it is alive, and a concurrent attempt may have left a
        // spurious cooldown behind.
        g_deadUntil[i] = {};
        if (i == g_active) return;
        LOG_MANIFEST_WARN("Manifest provider switched {} -> {}",
                          kProviders[g_active].name, kProviders[i].name);
        g_active = i;
    }

    bool SetProvider(std::string_view name) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        for (size_t i = 0; i < kProviderCount; ++i)
            if (kProviders[i].name == name) {
                // Re-selecting the provider that is already active must be a
                // no-op. Any write to opensteamtool.toml reloads the whole
                // config, so wiping the cooldowns here would make an unrelated
                // edit (say, log.level) send the next fetch straight back into
                // a host we already know is dead, costing a full timeout.
                if (i == g_active) return true;
                g_active = i;
                // An explicit switch is a fresh start for the chosen provider
                // only; what we learned about the others still holds. Bump the
                // sequence too, so an attempt already in flight against this
                // provider cannot immediately undo the clear.
                g_deadUntil[i] = {};
                ++g_okSeq[i];
                return true;
            }
        return false;
    }

    const char* ActiveProviderName() {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        return kProviders[g_active].name.data();
    }

    // ── request ───────────────────────────────────────────────────

    void Shutdown() {
        std::lock_guard<std::mutex> lock(g_stateMutex);
    }

    // ── fetch ─────────────────────────────────────────────────────

    // Tries the active provider first, then fails over through the rest of
    // the table.  A provider that just failed goes into cooldown so a dead
    // upstream costs one timeout instead of one per depot; when every
    // provider is cooling down this returns false without any network I/O.
    static bool FetchWithFailover(uint64_t gid, uint64_t* outCode) {
        const Config::ManifestTimeouts timeouts = Config::GetManifestTimeouts();
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(kFetchBudgetMs);

        // Snapshot the starting point once, so the walk order stays stable even
        // if a concurrent fetch promotes a different provider mid-loop.
        const size_t start = ActiveIndex();

        size_t tried = 0;
        for (size_t k = 0; k < kProviderCount; ++k) {
            const size_t i = (start + k) % kProviderCount;
            const Provider& p = kProviders[i];

            const auto now = std::chrono::steady_clock::now();
            if (IsCoolingDown(i, now)) {
                LOG_MANIFEST_DEBUG("Manifest {} skipped (cooldown)", p.name);
                continue;
            }

            // Read before the request goes out, so that any success landing
            // while we wait is guaranteed to change it. Checked by MarkFailed.
            const uint64_t seq = OkSeq(i);

            // Never let one attempt outlive the shared budget — the hook
            // stops waiting at kFetchBudgetMs, so a slower answer is useless
            // to Steam and would only delay the next depot's fetch.
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now).count();
            if (left <= 0) {
                LOG_MANIFEST_WARN("Manifest gid={}: {}ms budget exhausted after {} attempt(s)",
                                  gid, kFetchBudgetMs, tried);
                break;
            }
            // Clamped at both ends: never past the remaining budget, and never
            // below 1 ms.  WinHTTP reads a zero timeout as "no timeout at all",
            // so a 0 here would disable that phase and let a single attempt run
            // long past the budget the hook is waiting on.  Config::Load already
            // clamps 0 away, so this is defence in depth -- written as max/min rather
            // than std::clamp because clamp(v, 1, left) would be UB should the
            // left <= 0 guard above ever be moved or removed.
            const auto cap = [left](uint32_t ms) {
                return static_cast<uint32_t>(
                    std::max<int64_t>(1, std::min<int64_t>(ms, left)));
            };

            char urlLog[256];
            std::snprintf(urlLog, sizeof(urlLog), p.urlTemplate, gid);

            auto r = OSTPlatform::Http::Execute(
                L"GET",
                urlLog,
                nullptr,
                0,
                nullptr,
                cap(timeouts.resolve),
                cap(timeouts.connect),
                cap(timeouts.send),
                cap(timeouts.recv));

            LOG_MANIFEST_INFO("Manifest {} status={} gid={}", p.name, r.status, gid);
            ++tried;

            if (r.ok && r.status == 200 && p.parse(r.body, outCode)) {
                RecordSuccess(i);
                return true;
            }

            MarkFailed(i, std::chrono::steady_clock::now(), seq);
        }

        if (tried == 0)
            LOG_MANIFEST_WARN("Manifest gid={}: every provider is in cooldown", gid);
        return false;
    }

    // ── public ────────────────────────────────────────────────────

    // LuaConfig hands out one shared lua_State and does no locking of its own,
    // so two fetch threads calling fetch_manifest_code at once would push onto
    // the same stack and corrupt it.  Serialise the Lua attempts here, kept
    // separate from g_stateMutex so the HTTP path never waits on Lua.
    static std::mutex g_luaMutex;

    // Returns true when Lua produced a code.  Caller falls through to HTTP
    // otherwise.  Scoped as its own function so the lock is released before
    // FetchWithFailover starts any network I/O.
    static bool TryLua(uint64_t manifestGid, uint64_t* outRequestCode,
                       AppId_t appId, AppId_t depotId)
    {
        std::lock_guard<std::mutex> lock(g_luaMutex);

        if (appId && depotId && LuaConfig::HasManifestCodeFuncEx()) {
            if (LuaConfig::CallManifestFetchCodeEx(appId, depotId, manifestGid, outRequestCode)) {
                LOG_MANIFEST_INFO("Manifest gid={} resolved via fetch_manifest_code_ex", manifestGid);
                return true;
            }
            LOG_MANIFEST_WARN("Manifest gid={} fetch_manifest_code_ex returned nil, trying fetch_manifest_code", manifestGid);
        }

        if (LuaConfig::HasManifestCodeFunc()) {
            if (LuaConfig::CallManifestFetchCode(manifestGid, outRequestCode)) {
                LOG_MANIFEST_INFO("Manifest gid={} resolved via manifest.lua", manifestGid);
                return true;
            }
            LOG_MANIFEST_WARN("Manifest gid={} lua returned nil, falling back to config", manifestGid);
        }

        return false;
    }

    bool FetchManifestRequestCode(uint64_t manifestGid, uint64_t* outRequestCode,
                                  AppId_t appId, AppId_t depotId)
    {
        if (TryLua(manifestGid, outRequestCode, appId, depotId))
            return true;

        return FetchWithFailover(manifestGid, outRequestCode);
    }
}
