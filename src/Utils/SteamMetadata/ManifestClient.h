#pragma once
#include "Steam/Types.h"
#include <string_view>

// ─────────────────────────────────────────────────────────────────
//  ManifestClient — HTTP client for depot manifest request codes.
//  Built-in providers live in kProviders (ManifestClient.cpp); any
//  other [manifest] url is used as a custom URL template as-is.
//
//  Thread-safe — serialises access to the underlying WinHTTP connection.
// ─────────────────────────────────────────────────────────────────
namespace ManifestClient {

    inline constexpr std::string_view kDefaultProviderName = "opensteamtool";

    // Select the active provider by its string name (matches kProviders[i].name).
    // Returns false if no provider matches; the previous selection is kept.
    bool SetProvider(std::string_view name);

    // Use a custom URL template containing one {gid} placeholder, e.g.
    // "https://my.server/manifest/{gid}". Format selects the response
    // parser: "plain" (bare digits) or "steamrun" ({"content":"..."}).
    // Returns false if the template is invalid; the previous selection is kept.
    bool SetCustomProvider(std::string_view urlTemplate, std::string_view format);

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
