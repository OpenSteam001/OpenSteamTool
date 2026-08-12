#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Mirror {

    // Fetch a file committed to the `updates` branch of the delivery repo, trying the
    // built-in mirror chain (github-raw → jsDelivr → lua.tools) in order and returning
    // the first 200 response body. `relPath` is branch-relative, e.g.
    // "opensteamtool/latest.toml". Returns nullopt when every mirror fails.
    std::optional<std::string> Fetch(std::string_view relPath);

} // namespace Mirror
