#pragma once

#include "Steam/Types.h"

#include <optional>
#include <string>

// Legacy third-party CD-key ("Updating product key") resolution.
//
// When Steam launches a title with a third-party retail key (Ubisoft Connect,
// Rockstar, ...) it asks the CM for the per-account key via
// k_EMsgClientGetLegacyGameKey. For a managed (added) app we answer that request
// locally instead: a user-supplied override if configured, otherwise a
// deterministic synthetic key.
namespace LegacyCDKey {
    // Resolve the legacy CD key for a managed app.
    //   nullopt => not an app we manage; the caller must let Steam's real flow run.
    // NOTE: a synthesised key satisfies Steam's local step only. Titles that
    // validate the key online (Ubisoft/Rockstar) require a real override key.
    std::optional<std::string> Resolve(AppId_t appId, uint32 accountId);
}
