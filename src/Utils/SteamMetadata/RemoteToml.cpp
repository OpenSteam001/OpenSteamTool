#include "RemoteToml.h"
#include "OSTPlatform/include/Http.h"
#include "Utils/Config/Config.h"
#include "Utils/Logging/Log.h"
#include "Utils/SteamMetadata/SteamDiagnostics.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace RemoteToml {

namespace {
    // Built-in mirror chain, tried in order. Used when [remote] url_template is unset.
    // Mirrors are independent repos/hosts (not just CDN copies of one repo).
    constexpr const char* kDefaultTemplates[] = {
        "https://raw.githubusercontent.com/OpenSteam001/steam-monitor/{channel}/{component}/{sha256}.toml",
        "https://cdn.jsdelivr.net/gh/OpenSteam001/steam-monitor@{channel}/{component}/{sha256}.toml",
        "https://raw.githubusercontent.com/madoiscool/steam-monitor/{channel}/{component}/{sha256}.toml",
        "https://cdn.jsdelivr.net/gh/madoiscool/steam-monitor@{channel}/{component}/{sha256}.toml",
        "https://git.lua.tools/luatools/steam-monitor/raw/branch/{channel}/{component}/{sha256}.toml",
    };

    static bool HasPlaceholder(std::string_view text, std::string_view placeholder)
    {
        return text.find(placeholder) != std::string_view::npos;
    }

    static bool IsValidTemplate(std::string_view urlTemplate)
    {
        return HasPlaceholder(urlTemplate, "{channel}") &&
               HasPlaceholder(urlTemplate, "{component}") &&
               HasPlaceholder(urlTemplate, "{sha256}");
    }

    static void ReplaceAll(std::string& text,
                           std::string_view from,
                           std::string_view to)
    {
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    static std::string ExpandTemplate(std::string urlTemplate,
                                      const Request& request,
                                      std::string_view sha256)
    {
        ReplaceAll(urlTemplate, "{channel}", request.channel);
        ReplaceAll(urlTemplate, "{component}", request.component);
        ReplaceAll(urlTemplate, "{sha256}", sha256);
        return urlTemplate;
    }

    static std::vector<std::string> BuildUrlTemplates()
    {
        const std::vector<std::string> configured = Config::GetRemoteUrlTemplates();
        if (configured.empty())
            return { std::begin(kDefaultTemplates), std::end(kDefaultTemplates) };

        // A configured list REPLACES the built-ins; keep only valid entries.
        std::vector<std::string> out;
        for (const std::string& tmpl : configured) {
            if (IsValidTemplate(tmpl))
                out.push_back(tmpl);
            else
                LOG_WARN("RemoteToml: remote.url_template entry missing "
                         "{{channel}}/{{component}}/{{sha256}}, skipping: {}", tmpl);
        }
        if (out.empty())
            LOG_WARN("RemoteToml: no valid remote.url_template entries; remote fetch disabled");
        return out;
    }
} // namespace

Result Fetch(const Request& request)
{
    namespace fs = std::filesystem;
    Result out;

    // 1. SHA-256 of the DLL.
    const auto hashStart = std::chrono::steady_clock::now();
    out.sha256 = SteamDiagnostics::Sha256Of(request.dllPath);
    const auto hashMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - hashStart).count();

    if (out.sha256.empty()) {
        LOG_WARN("RemoteToml({}/{}): Sha256OfFile failed for {} ({} ms)",
                 request.channel, request.component, request.dllPath, hashMs);
        return out;
    }
    LOG_INFO("RemoteToml({}/{}): sha256 = {} ({} ms)",
             request.channel, request.component, out.sha256, hashMs);

    // 2. Cache path & dir.
    fs::path steamRoot = fs::path(request.dllPath).parent_path();
    fs::path cacheDir  = steamRoot / "opensteamtool" / request.channel / request.component;
    fs::path cachePath = cacheDir / (out.sha256 + ".toml");
    const std::string cachePathText = cachePath.string();

    std::error_code mkdirEc;
    fs::create_directories(cacheDir, mkdirEc);
    if (mkdirEc) {
        LOG_WARN("RemoteToml({}/{}): could not create cache dir {} ({})",
                 request.channel, request.component, cacheDir.string(), mkdirEc.message());
    }

    // 3. Try remote (mirror chain with early-out on 404).
    const std::vector<std::string> urlTemplates = BuildUrlTemplates();
    OSTPlatform::Http::Result http;
    std::string lastUrl;

    for (size_t i = 0; i < urlTemplates.size(); ++i) {
        lastUrl = ExpandTemplate(urlTemplates[i], request, out.sha256);
        LOG_INFO("RemoteToml({}/{}): downloading {}",
                 request.channel, request.component, lastUrl);

        http = OSTPlatform::Http::Execute(L"GET", lastUrl.c_str(),
                                          nullptr, 0, nullptr);

        if (http.ok && http.status == 200) break;

        // Mirrors are independent repos with possibly different coverage, so a 404
        // (or any non-200) on one does NOT imply the others lack the file — always
        // fall through to the next mirror.
        if (i + 1 < urlTemplates.size()) {
            LOG_WARN("RemoteToml({}/{}): mirror failed ({} ok={} HTTP={}), trying next",
                     request.channel, request.component, lastUrl, http.ok, http.status);
        } else {
            LOG_WARN("RemoteToml({}/{}): mirror failed ({} ok={} HTTP={}), no more mirrors",
                     request.channel, request.component, lastUrl, http.ok, http.status);
        }
    }

    // 4. Remote OK → write cache, return body.
    if (http.ok && http.status == 200 && !http.body.empty()) {
        std::ofstream ofs(cachePath, std::ios::binary);
        if (ofs) {
            ofs.write(http.body.data(),
                      static_cast<std::streamsize>(http.body.size()));
            LOG_INFO("RemoteToml({}/{}): cached to {}",
                     request.channel, request.component, cachePathText);
        } else {
            LOG_WARN("RemoteToml({}/{}): could not open {} for writing",
                     request.channel, request.component, cachePathText);
        }
        out.body = std::move(http.body);
        out.ok = true;
        return out;
    }

    // 5. Remote failed → fall back to whatever is cached for this exact SHA.
    if (fs::exists(cachePath)) {
        LOG_WARN("RemoteToml({}/{}): remote failed (last URL {} HTTP {}); "
                 "falling back to local cache {}",
                 request.channel, request.component,
                 lastUrl.empty() ? "<none>" : lastUrl, http.status, cachePathText);

        std::ifstream ifs(cachePath, std::ios::binary);
        if (ifs) {
            std::string buf((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
            if (!buf.empty()) {
                out.body = std::move(buf);
                out.ok = true;
                out.fromCache = true;
                return out;
            }
            LOG_WARN("RemoteToml({}/{}): cache file empty: {}",
                     request.channel, request.component, cachePathText);
        } else {
            LOG_WARN("RemoteToml({}/{}): could not open cache file: {}",
                     request.channel, request.component, cachePathText);
        }
    }

    // 6. Total failure — caller handles popup / degraded mode.
    LOG_WARN("RemoteToml({}/{}): no source available (last URL: {} HTTP {})",
             request.channel, request.component,
             lastUrl.empty() ? "<none>" : lastUrl, http.status);
    return out;
}

} // namespace RemoteToml
