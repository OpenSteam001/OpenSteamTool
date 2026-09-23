#pragma once
#include "Steam/Types.h"
#include <string_view>

// ─────────────────────────────────────────────────────────────────
//  ManifestClient — HTTP client for depot manifest request codes.
//  Provider table is internal (see kProviders in ManifestClient.cpp);
//  adding a new provider only requires one row there.
//
//  Fetches walk the provider table and fall back to the next entry
//  when one fails, so a single dead upstream no longer breaks depot
//  downloads.
//
//  Thread-safe, and concurrent fetches do run concurrently: each HTTP
//  request owns its own WinHTTP session, so nothing is serialised
//  except the provider state and the shared lua_State.  A dead upstream
//  therefore costs every waiting depot one timeout in parallel, not one
//  timeout each in sequence.
// ─────────────────────────────────────────────────────────────────
namespace ManifestClient {

    // Wall-clock budget for one whole fetch, i.e. every provider attempt
    // combined.  The net-packet hook waits slightly longer than this
    // before it gives up and lets Steam's original response through, so
    // raising it here also has to be reflected there (see kMaxWaitMs in
    // Hooks_NetPacket.cpp, which derives from this value).
    constexpr uint32_t kFetchBudgetMs = 11000;

    // How long a provider is skipped after a failed attempt.  Keeps a
    // dead upstream costing one timeout instead of one per depot.
    constexpr uint32_t kProviderCooldownMs = 60000;

    // Select the active provider by its string name (matches kProviders[i].name).
    // Returns false if no provider matches; the previous selection is kept.
    bool SetProvider(std::string_view name);

    // Name of the currently active provider (for logging / diagnostics).
    const char* ActiveProviderName();

    // Resolve a manifest GID to its request code. Tries Lua first
    // (fetch_manifest_code_ex, then fetch_manifest_code), then the
    // active provider. Returns true and sets *outRequestCode on success.
    bool FetchManifestRequestCode(uint64_t manifestGid, uint64_t* outRequestCode,
                                  AppId_t appId = 0, AppId_t depotId = 0);

    // Tear down the cached WinHTTP connection (call at unload).
    void Shutdown();
}
