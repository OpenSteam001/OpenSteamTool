#include "Mirror.h"

#include "OSTPlatform/include/Http.h"
#include "Utils/Logging/Log.h"

#include <iterator>
#include <string>

namespace Mirror {

namespace {
    // Delivery mirror chain for the `updates` branch of madoiscool/BetterSteamTools.
    // Independent hosts (not just CDN copies of one repo), tried in order until one
    // returns HTTP 200. github-raw first for pointer freshness; jsDelivr and lua.tools
    // cover regions where raw.githubusercontent is throttled/blocked.
    constexpr const char* kBaseTemplates[] = {
        "https://raw.githubusercontent.com/madoiscool/BetterSteamTools/updates/{path}",
        "https://cdn.jsdelivr.net/gh/madoiscool/BetterSteamTools@updates/{path}",
        "https://git.lua.tools/luatools/BetterSteamTools/raw/branch/updates/{path}",
    };

    std::string Expand(std::string tmpl, std::string_view relPath)
    {
        const std::string_view token = "{path}";
        if (const size_t pos = tmpl.find(token); pos != std::string::npos)
            tmpl.replace(pos, token.size(), relPath);
        return tmpl;
    }
} // namespace

std::optional<std::string> Fetch(std::string_view relPath)
{
    for (size_t i = 0; i < std::size(kBaseTemplates); ++i) {
        const std::string url = Expand(kBaseTemplates[i], relPath);
        LOG_INFO("Mirror: fetching {}", url);

        // Raise the body cap well above the default: updates carry a multi-MB DLL,
        // not just a small TOML pointer. (AppUpdater still range-checks the DLL size.)
        OSTPlatform::Http::Result http =
            OSTPlatform::Http::Execute(L"GET", url.c_str(), nullptr, 0, nullptr,
                                       5000, 5000, 10000, 10000, 16u * 1024 * 1024);

        if (http.ok && http.status == 200 && !http.body.empty())
            return std::move(http.body);

        // Mirrors are independent, so a failure on one says nothing about the others —
        // always fall through to the next.
        LOG_WARN("Mirror: {} failed (ok={} HTTP={}){}",
                 url, http.ok, http.status,
                 i + 1 < std::size(kBaseTemplates) ? ", trying next" : ", no more mirrors");
    }
    return std::nullopt;
}

} // namespace Mirror
