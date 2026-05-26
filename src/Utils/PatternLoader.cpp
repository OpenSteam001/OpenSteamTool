#include "PatternLoader.h"
#include "Config.h"
#include "Hash.h"
#include "Log.h"
#include "WinHttp.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <psapi.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <toml++/toml.hpp>

// ---- compile-time sanity checks for FNV-1a table keys ----
// If the steam-monitor bot uses the same algorithm these must hold.
static_assert(Fnv1aHash("BBuildAndAsyncSendFrame") == 0x82428E37u,
              "FNV-1a mismatch for BBuildAndAsyncSendFrame");
static_assert(Fnv1aHash("BuildDepotDependency") == 0xC37F2D8Eu,
              "FNV-1a mismatch for BuildDepotDependency");

namespace {

// ---- per-function pattern record ----
struct PatternEntry {
    std::string name;
    uintptr_t   rva = 0;   // 0 = not present in file
    std::string sig;        // empty = not present in file
};

// key = Fnv1aHash(funcName)
using PatternMap = std::unordered_map<uint32_t, PatternEntry>;

// module → its pattern map
static std::unordered_map<HMODULE, PatternMap> g_moduleMaps;

// Modules whose Load() call failed (popup already shown). FindPattern
// silently returns nullptr for these — without re-logging or adding the
// function to g_missingFunctions — so we don't follow one "TOML missing"
// popup with a second popup listing every dependent hook.
static std::unordered_set<HMODULE> g_failedModules;

// functions whose names were not found during FindPattern
static std::vector<std::string> g_missingFunctions;

// Built-in fallback mirrors. Tried in this fixed order when [pattern]
// mirror is not configured: GitHub raw first (canonical source), jsDelivr
// (global CDN) on connection failure.
static constexpr const char* kGithubMirror =
    "https://raw.githubusercontent.com/OpenSteam001/steam-monitor/pattern";
static constexpr const char* kJsdelivrMirror =
    "https://cdn.jsdelivr.net/gh/OpenSteam001/steam-monitor@pattern";

// ---- byte-pattern scanner (independent of old ByteSearch) ----

static bool ParseSig(const std::string& str,
                     std::vector<uint8_t>& bytes,
                     std::vector<uint8_t>& mask)
{
    bytes.clear();
    mask.clear();
    for (const char* p = str.c_str(); *p; ) {
        if (*p == ' ' || *p == '\t' || *p == ',') { ++p; continue; }
        if (p[0] == '?' && p[1] == '?') {
            bytes.push_back(0); mask.push_back(0); p += 2; continue;
        }
        char hi = p[0], lo = p[1];
        if (!hi || !lo) return false;
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = nib(hi), l = nib(lo);
        if (h < 0 || l < 0) return false;
        bytes.push_back(static_cast<uint8_t>((h << 4) | l));
        mask.push_back(1);
        p += 2;
    }
    return !bytes.empty();
}

static void* ScanModule(HMODULE module,
                        const std::vector<uint8_t>& bytes,
                        const std::vector<uint8_t>& mask)
{
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), module, &mi, sizeof(mi)))
        return nullptr;

    auto* base   = static_cast<uint8_t*>(mi.lpBaseOfDll);
    SIZE_T size   = mi.SizeOfImage;
    SIZE_T patLen = bytes.size();
    if (size < patLen) return nullptr;

    for (SIZE_T i = 0; i <= size - patLen; ++i) {
        bool found = true;
        for (SIZE_T j = 0; j < patLen; ++j) {
            if (mask[j] && base[i + j] != bytes[j]) { found = false; break; }
        }
        if (found) return base + i;
    }
    return nullptr;
}

// ---- TOML pattern parser ----

// Section keys are hex literals like "0x82428E37"; each section is a table
// with optional `name`, `rva` (hex string), and `sig` (IDA-style bytes).
static PatternMap TableToPatternMap(const toml::table& tbl)
{
    PatternMap map;
    map.reserve(tbl.size());
    for (auto& [rawKey, val] : tbl) {
        if (!val.is_table()) continue;
        auto& sub = *val.as_table();

        uint32_t hashKey = 0;
        try {
            hashKey = static_cast<uint32_t>(
                std::stoull(std::string(rawKey), nullptr, 16));
        } catch (...) { continue; }

        PatternEntry entry;
        if (auto v = sub["name"].value<std::string>()) entry.name = *v;
        if (auto v = sub["rva"].value<std::string>()) {
            try { entry.rva = static_cast<uintptr_t>(std::stoull(*v, nullptr, 16)); }
            catch (...) {}
        }
        if (auto v = sub["sig"].value<std::string>()) entry.sig = *v;

        map[hashKey] = std::move(entry);
    }
    return map;
}

static PatternMap ParsePatternFile(const std::filesystem::path& filePath)
{
    try {
        return TableToPatternMap(toml::parse_file(filePath.string()));
    } catch (const toml::parse_error& e) {
        LOG_WARN("PatternLoader: TOML parse error in {}: {}",
                 filePath.string(), e.description());
        return {};
    }
}

static PatternMap ParsePatternString(std::string_view body,
                                     std::string* outError = nullptr)
{
    try {
        return TableToPatternMap(toml::parse(body));
    } catch (const toml::parse_error& e) {
        if (outError) *outError = e.description();
        return {};
    }
}

// ---- popup helpers (detached threads so we never block Steam) ----

// Surface a missing pattern file to the user, with enough detail to either
// (a) drop a file in manually, (b) check the upstream repo, or (c) file
// an actionable bug report.  We deliberately only disable hooks for the
// failing module — the rest of OpenSteamTool keeps working.
static void ShowDownloadFailedPopup(const std::string& dllName,
                                    const std::string& sha256,
                                    const std::string& ghSubdir)
{
    std::thread([dllName, sha256, ghSubdir]() {
        std::string msg =
            "OpenSteamTool: signature file not found for " + dllName + ".\n\n"
            "  Steam DLL: " + dllName + "\n"
            "  SHA-256:   " + sha256 + "\n\n"
            "Steam was likely just updated and the matching pattern file is "
            "not yet published on the steam-monitor server. Hooks that depend "
            "on " + dllName + " are disabled for this session; other modules "
            "are unaffected.\n\n"
            "You can:\n"
            "  1. Wait for the next signature update (usually within hours of "
            "a new Steam build), then restart Steam.\n"
            "  2. Drop a matching TOML at:\n"
            "       <Steam>\\opensteamtool\\pattern\\" + ghSubdir + "\\" + sha256 + ".toml\n"
            "  3. Check upstream:\n"
            "       https://github.com/OpenSteam001/steam-monitor/tree/pattern/" + ghSubdir + "\n"
            "  4. Report this hash so it gets prioritized:\n"
            "       https://github.com/OpenSteam001/OpenSteamTool/issues";
        MessageBoxA(nullptr, msg.c_str(),
                    "OpenSteamTool - Unsupported Steam Version",
                    MB_OK | MB_ICONWARNING | MB_TOPMOST);
    }).detach();
}

} // namespace

// ---- public API ----

namespace PatternLoader {

bool Load(HMODULE module, const std::string& dllPath, const std::string& ghSubdir)
{
    namespace fs = std::filesystem;

    // 1. Compute SHA-256 of the DLL file on disk.
    //    Timed so we can see the cost in main.log — useful when triaging
    //    "Steam takes ages to start" reports from HDD users.
    const auto hashStart = std::chrono::steady_clock::now();
    const std::string sha256 = Sha256OfFile(dllPath);
    const auto hashMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - hashStart).count();

    if (sha256.empty()) {
        LOG_WARN("PatternLoader: Sha256OfFile failed for {} ({} ms)", dllPath, hashMs);
        ShowDownloadFailedPopup(fs::path(dllPath).filename().string(),
                                "(hash failed)", ghSubdir);
        g_failedModules.insert(module);
        return false;
    }
    LOG_INFO("PatternLoader: {} sha256 = {} ({} ms)", ghSubdir, sha256, hashMs);

    // 2. Build local cache path and make sure the directory exists.
    //    Cache lives at: <steam>/opensteamtool/pattern/<subdir>/<sha256>.toml
    //    dllPath is always inside the Steam root directory.
    fs::path steamRoot = fs::path(dllPath).parent_path();
    fs::path cacheDir  = steamRoot / "opensteamtool" / "pattern" / ghSubdir;
    fs::path cachePath = cacheDir / (sha256 + ".toml");

    std::error_code mkdirEc;
    fs::create_directories(cacheDir, mkdirEc);
    if (mkdirEc) {
        // Non-fatal: we can still try to read an existing file or hold the
        // downloaded TOML in memory. Log it so disk-permission issues surface.
        LOG_WARN("PatternLoader: could not create cache dir {} ({})",
                 cacheDir.string(), mkdirEc.message());
    }

    // 3. Try remote first.  Rationale: the upstream bot can re-publish the
    //    TOML for the same SHA-256 (adding new function signatures, fixing
    //    stale ones, etc.).  Reading the local cache first would silently
    //    pin users to whatever version they downloaded on day 1.  The cache
    //    is kept purely as an offline fallback below.
    //
    //    Mirror selection:
    //    - If [pattern] mirror is configured, use only that URL.  Explicit
    //      user choice wins — no automatic fallback.
    //    - Otherwise try GitHub raw, then jsDelivr on connection failure
    //      (helps users where raw.githubusercontent.com is blocked).
    //    - HTTP 404 stops the loop early: all mirrors serve the same data,
    //      so 404 means the upstream bot hasn't published this SHA yet.
    std::vector<std::string> mirrors;
    if (!Config::patternMirror.empty()) {
        mirrors.push_back(Config::patternMirror);
    } else {
        mirrors.emplace_back(kGithubMirror);
        mirrors.emplace_back(kJsdelivrMirror);
    }

    WinHttp::Result result;
    std::string url;
    for (size_t i = 0; i < mirrors.size(); ++i) {
        url = mirrors[i] + "/" + ghSubdir + "/" + sha256 + ".toml";
        LOG_INFO("PatternLoader: downloading {}", url);

        result = WinHttp::Execute(L"GET", url.c_str(),
                                  nullptr, 0, nullptr,
                                  /*timeoutResolve=*/5000,
                                  /*timeoutConnect=*/5000,
                                  /*timeoutSend=*/10000,
                                  /*timeoutRecv=*/15000);

        if (result.ok && result.status == 200) break;

        if (result.ok && result.status == 404) {
            LOG_WARN("PatternLoader: mirror has no such file (HTTP 404): {}", url);
            break;  // all mirrors serve the same content — no point trying others
        }

        // Connection error or 5xx — try next mirror if any
        if (i + 1 < mirrors.size()) {
            LOG_WARN("PatternLoader: mirror failed ({} ok={} HTTP={}), falling back",
                     mirrors[i], result.ok, result.status);
        }
    }

    // 4. Remote succeeded → parse, then update cache on disk so the next
    //    launch has an up-to-date offline fallback.
    if (result.ok && result.status == 200) {
        std::string parseErr;
        PatternMap map = ParsePatternString(result.body, &parseErr);
        if (!map.empty()) {
            std::ofstream ofs(cachePath, std::ios::binary);
            if (ofs) {
                ofs.write(result.body.data(),
                          static_cast<std::streamsize>(result.body.size()));
                LOG_INFO("PatternLoader: cached to {}", cachePath.string());
            } else {
                LOG_WARN("PatternLoader: could not open {} for writing",
                         cachePath.string());
            }
            LOG_INFO("PatternLoader: loaded {} patterns for {} (remote)",
                     map.size(), ghSubdir);
            g_moduleMaps[module] = std::move(map);
            return true;
        }
        LOG_WARN("PatternLoader: downloaded body unparseable ({}); "
                 "trying local cache",
                 parseErr.empty() ? "empty or no entries" : parseErr);
    }

    // 5. Remote unreachable (or returned garbage) → fall back to whatever
    //    we previously cached for this exact SHA-256.  Better stale-but-
    //    working than nothing at all.
    if (fs::exists(cachePath)) {
        LOG_WARN("PatternLoader: remote failed (last: {} HTTP {}); "
                 "falling back to local cache {}",
                 url, result.status, cachePath.string());
        PatternMap map = ParsePatternFile(cachePath);
        if (!map.empty()) {
            LOG_INFO("PatternLoader: loaded {} patterns for {} (cache fallback)",
                     map.size(), ghSubdir);
            g_moduleMaps[module] = std::move(map);
            return true;
        }
        LOG_WARN("PatternLoader: cache fallback also failed (file empty/invalid)");
    }

    // 6. Remote failed and no usable cache — give up.
    LOG_WARN("PatternLoader: no source available for {} (last URL: {} HTTP {})",
             ghSubdir, url, result.status);
    std::string dllName = fs::path(dllPath).filename().string();
    ShowDownloadFailedPopup(dllName, sha256, ghSubdir);
    g_failedModules.insert(module);
    return false;
}

void* FindPattern(HMODULE module, const char* funcName)
{
    // If the whole module's pattern file failed to load, stay quiet — the
    // user already saw one popup and the main.log already has the warning.
    // No point amplifying that into one log line per hook plus a second
    // "missing functions" popup later.
    if (g_failedModules.count(module)) {
        return nullptr;
    }

    uint32_t key = Fnv1aHash(funcName);

    auto mapIt = g_moduleMaps.find(module);
    if (mapIt == g_moduleMaps.end()) {
        // Load() was never called for this module.
        LOG_WARN("PatternLoader: FindPattern called for module that was never loaded "
                 "('{}')", funcName);
        g_missingFunctions.emplace_back(funcName);
        return nullptr;
    }

    auto& map = mapIt->second;
    auto entryIt = map.find(key);
    if (entryIt == map.end()) {
        LOG_WARN("PatternLoader: no entry for '{}' (key=0x{:08X})", funcName, key);
        g_missingFunctions.emplace_back(funcName);
        return nullptr;
    }

    const PatternEntry& entry = entryIt->second;

    // Priority 1: RVA direct offset
    if (entry.rva != 0) {
        void* addr = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(module) + entry.rva);
        LOG_DEBUG("PatternLoader: {} resolved via RVA 0x{:X}", funcName, entry.rva);
        return addr;
    }

    // Priority 2: byte-signature scan
    if (!entry.sig.empty()) {
        std::vector<uint8_t> bytes, mask;
        if (ParseSig(entry.sig, bytes, mask)) {
            void* addr = ScanModule(module, bytes, mask);
            if (addr) {
                uintptr_t rva = reinterpret_cast<uintptr_t>(addr) -
                                reinterpret_cast<uintptr_t>(module);
                LOG_DEBUG("PatternLoader: {} resolved via sig @ RVA 0x{:X}",
                          funcName, rva);
                return addr;
            }
            LOG_WARN("PatternLoader: sig scan miss for '{}' (pattern parsed OK, "
                     "no match in module image)", funcName);
        } else {
            LOG_WARN("PatternLoader: malformed sig for '{}': '{}'",
                     funcName, entry.sig);
        }
    } else {
        LOG_WARN("PatternLoader: entry for '{}' has neither rva nor sig", funcName);
    }

    g_missingFunctions.emplace_back(funcName);
    return nullptr;
}

void ReportMissingFunctions()
{
    if (g_missingFunctions.empty()) return;

    // Build the list
    std::string list;
    for (const auto& name : g_missingFunctions)
        list += "  - " + name + "\n";
    g_missingFunctions.clear();

    std::thread([list]() {
        std::string msg =
            "OpenSteamTool: some functions could not be located.\n\n"
            "The following functions were not found in the signature file:\n" +
            list +
            "\nHooks for these functions are disabled for this session.\n\n"
            "Please report this at:\n"
            "https://github.com/OpenSteam001/OpenSteamTool/issues";
        MessageBoxA(nullptr, msg.c_str(),
                    "OpenSteamTool - Missing Signatures",
                    MB_OK | MB_ICONWARNING | MB_TOPMOST);
    }).detach();
}

} // namespace PatternLoader
