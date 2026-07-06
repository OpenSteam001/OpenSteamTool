#include "Pipe/Features/DenuvoAuth/ProtectionScan.h"

#include "OSTPlatform/include/ByteSearch.h"
#include "OSTPlatform/include/PE.h"
#include "OSTPlatform/include/Process.h"
#include "Utils/Logging/Log.h"
#include "Utils/Support/Stopwatch.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PipeManager::DenuvoAuth {
namespace {

    struct ModuleCandidate {
        std::string path;
        std::filesystem::path nativePath;
        uint32 size = 0;
        bool executable = false;
        bool inGameTree = false;
        size_t order = 0;

        std::string DebugString() const {
            return std::format("path={} size={} executable={} inGameTree={} order={}",
                path, size, executable, inGameTree, order);
        }
    };

    struct DetectionMatch {
        DetectionMethod method = DetectionMethod::None;
        std::string sectionName;
        uint32 entryPointRva = 0;
        uint32 matchRva = 0;
        size_t matchRawOffset = 0;
    };

    constexpr std::array<std::string_view, 5> kLegacyDenuvoSections = {
        ".arch",
        ".srdata",
        ".xpdata",
        ".xdata",
        ".xtls",
    };

    constexpr std::array<uint8_t, 6> kLegacyDenuvoString = {
        'D', 'E', 'N', 'U', 'V', 'O',
    };

    // High-version quick path: only scan the section containing the entry point.
    constexpr std::array<uint8_t, 10> kDenuvoOepPattern = {
        0x48, 0xB9, 0x44, 0x4F, 0x44, 0x45, 0x4E, 0x55, 0x56, 0x4F,
    };

    // Packed Denuvo game modules are expected to be large; this avoids most
    // small runtime DLLs before any PE parsing or disk reads.
    constexpr uint32 kMinPackedModuleBytes = 80u * 1024u * 1024u;
    constexpr size_t kLegacyScanChunkBytes = 8ull * 1024ull * 1024ull;
    constexpr size_t kOepScanChunkBytes = 8ull * 1024ull * 1024ull;

    double BytesToMiB(uint64 bytes) {
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }

    constexpr std::array<std::string_view, 10> kSteamRuntimeModuleNames = {
        "steamclient.dll",
        "steamclient64.dll",
        "steam_api.dll",
        "steam_api64.dll",
        "tier0_s.dll",
        "tier0_s64.dll",
        "vstdlib_s.dll",
        "vstdlib_s64.dll",
        "gameoverlayrenderer.dll",
        "gameoverlayrenderer64.dll",
    };

    std::string Lower(std::string value) {
        for (char& ch : value) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    }

    bool EndsWithInsensitive(std::string_view value, std::string_view suffix) {
        if (value.size() < suffix.size()) return false;

        const size_t offset = value.size() - suffix.size();
        for (size_t i = 0; i < suffix.size(); ++i) {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(value[offset + i])));
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
            if (a != b) return false;
        }
        return true;
    }

    std::string BaseNameFromPath(const std::string& path) {
        const size_t slash = path.find_last_of("\\/");
        if (slash == std::string::npos) return path;
        return path.substr(slash + 1);
    }

    std::string DirectoryFromPath(const std::string& path) {
        const size_t slash = path.find_last_of("\\/");
        if (slash == std::string::npos) return {};
        return path.substr(0, slash + 1);
    }

    std::string NormalizeDirectoryPrefix(std::string path) {
        path = Lower(std::move(path));
        std::replace(path.begin(), path.end(), '/', '\\');
        if (!path.empty() && path.back() != '\\') path += '\\';
        return path;
    }

    bool PathStartsWithInsensitive(const std::string& path, const std::string& prefix) {
        if (prefix.empty()) return false;
        std::string normalizedPath = Lower(path);
        std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');
        return normalizedPath.starts_with(prefix);
    }

    bool IsSteamRuntimeModuleName(std::string name) {
        name = Lower(std::move(name));
        return std::ranges::find(kSteamRuntimeModuleNames, name) != kSteamRuntimeModuleNames.end();
    }

    const OSTPlatform::PE::Section* FindLegacyDenuvoSection(const OSTPlatform::PE::Image& image) {
        for (std::string_view name : kLegacyDenuvoSections) {
            for (const auto& section : image.Sections()) {
                if (section.name == name) return &section;
            }
        }
        return nullptr;
    }

    std::optional<DetectionMatch> TryLegacySectionString(
        const ModuleCandidate& module,
        const OSTPlatform::PE::Image& image,
        const OSTPlatform::PE::Section& section) {
        const auto matchRaw = OSTPlatform::ByteSearch::FindInFile(
            module.nativePath,
            kLegacyDenuvoString,
            kLegacyScanChunkBytes);
        if (!matchRaw) {
            LOG_PIPE_DEBUG("DenuvoAuth: legacy section present but DENUVO string not found path={} section={}",
                           module.path, section.name);
            return std::nullopt;
        }

        DetectionMatch match{};
        match.method = DetectionMethod::LegacySectionString;
        match.sectionName = section.name;
        match.entryPointRva = image.EntryPointRva();
        match.matchRawOffset = static_cast<size_t>(*matchRaw);
        match.matchRva = image.RawOffsetToRva(match.matchRawOffset).value_or(0);
        return match;
    }

    std::optional<DetectionMatch> TryOepPattern(
        const ModuleCandidate& module,
        const OSTPlatform::PE::Image& image) {
        const uint32 oep = image.EntryPointRva();
        if (oep == 0) {
            LOG_PIPE_TRACE("DenuvoAuth: module skipped zero OEP path={}", module.path);
            return std::nullopt;
        }

        const OSTPlatform::PE::Section* section = image.SectionContainingRva(oep);
        if (!section) {
            LOG_PIPE_TRACE("DenuvoAuth: module skipped OEP not in section path={} oep=0x{:X}",
                           module.path, oep);
            return std::nullopt;
        }

        if (section->rawSize == 0) {
            LOG_PIPE_TRACE("DenuvoAuth: module skipped empty OEP section path={} section={} oep=0x{:X}",
                           module.path, section->name, oep);
            return std::nullopt;
        }

        // Streaming scan: read the section straight from disk in overlapped chunks
        // so the BMH pass runs concurrently with the next chunk's read, instead of
        // materializing the whole (~100s of MB) section in memory first. Returns the
        // absolute file offset of the match.
        const Utils::Stopwatch scanTimer;
        const auto matchRaw = OSTPlatform::ByteSearch::FindInFileRange(
            module.nativePath,
            section->rawOffset,
            section->rawSize,
            kDenuvoOepPattern,
            kOepScanChunkBytes);
        const double scanMs = scanTimer.ElapsedMs();
        LOG_PIPE_DEBUG("DenuvoAuth: OEP section timing path={} section={} raw_start=0x{:X} scan_size={} ({:.2f} MB) scan_ms={:.3f} matched={}",
                       module.path,
                       section->name,
                       section->rawOffset,
                       section->rawSize,
                       BytesToMiB(static_cast<uint64>(section->rawSize)),
                       scanMs,
                       matchRaw.has_value());
        if (!matchRaw) {
            LOG_PIPE_DEBUG("DenuvoAuth: OEP section scanned path={} section={} oep=0x{:X} raw_start=0x{:X} scan_size={} ({:.2f} MB) matched=false",
                           module.path,
                           section->name,
                           oep,
                           section->rawOffset,
                           section->rawSize,
                           BytesToMiB(static_cast<uint64>(section->rawSize)));
            return std::nullopt;
        }

        const size_t localMatch = static_cast<size_t>(*matchRaw) - section->rawOffset;
        DetectionMatch match{};
        match.method = DetectionMethod::OepPattern;
        match.sectionName = section->name;
        match.entryPointRva = oep;
        match.matchRawOffset = static_cast<size_t>(*matchRaw);
        match.matchRva = section->virtualAddress + static_cast<uint32>(localMatch);
        return match;
    }

    std::optional<DetectionMatch> DetectModule(
        const ModuleCandidate& module,
        const OSTPlatform::PE::Image& image) {
        // Prefer the OEP signature path; legacy section/string scan is broader.
        if (const auto match = TryOepPattern(module, image)) {
            return match;
        }

        if (const auto* legacySection = FindLegacyDenuvoSection(image)) {
            return TryLegacySectionString(module, image, *legacySection);
        }
        return std::nullopt;
    }

    std::vector<ModuleCandidate> EnumerateModules(PID_t pid) {
        const Utils::Stopwatch timer;
        std::vector<ModuleCandidate> modules;

        size_t order = 0;
        for (auto module : OSTPlatform::Process::EnumerateModules(pid)) {
            if (module.path.empty()) continue;

            const bool executable = EndsWithInsensitive(module.path, ".exe");
            const bool dll = EndsWithInsensitive(module.path, ".dll");
            if (!executable && !dll) continue;
            // [PATCH #117] Always scan the main game executable regardless of size.
            // Denuvo packs the .exe itself, and some titles (e.g. Sniper Elite 4
            // appid 312660, 71MB) fall under the 80MB floor and were silently
            // skipped -> denuvo never detected -> auth never engaged -> 8850000A.
            // Keep the size floor only for DLLs (the original perf optimization).
            if (!executable && module.size < kMinPackedModuleBytes) {
                LOG_PIPE_TRACE("DenuvoAuth: module skipped below packed size floor path={} size={} ({:.2f} MB) min={} ({:.2f} MB)",
                               module.path,
                               module.size,
                               BytesToMiB(module.size),
                               kMinPackedModuleBytes,
                               BytesToMiB(kMinPackedModuleBytes));
                continue;
            }
            if (!executable && OSTPlatform::Process::IsSystemModulePath(module.path)) continue;
            if (!executable && IsSteamRuntimeModuleName(BaseNameFromPath(module.path))) {
                LOG_PIPE_TRACE("DenuvoAuth: module skipped Steam runtime path={}", module.path);
                continue;
            }

            modules.push_back(ModuleCandidate{
                std::move(module.path),
                std::move(module.nativePath),
                module.size,
                executable,
                false,
                order++,
            });
        }

        std::string gameDirectory;
        for (const auto& module : modules) {
            if (!module.executable) continue;
            gameDirectory = NormalizeDirectoryPrefix(DirectoryFromPath(module.path));
            break;
        }

        for (auto& module : modules) {
            module.inGameTree = !module.executable &&
                PathStartsWithInsensitive(module.path, gameDirectory);
        }

        const size_t unsortedCount = modules.size();
        std::stable_sort(modules.begin(), modules.end(),
            [](const ModuleCandidate& lhs, const ModuleCandidate& rhs) {
                if (lhs.executable != rhs.executable) return lhs.executable;
                if (!lhs.executable && lhs.inGameTree != rhs.inGameTree) return lhs.inGameTree;
                if (!lhs.executable && lhs.size != rhs.size) return lhs.size > rhs.size;
                return lhs.order < rhs.order;
            });

        LOG_PIPE_DEBUG("DenuvoAuth: pid={} enumerated {} candidate module(s) elapsed_ms={:.3f}",
                       pid, unsortedCount, timer.ElapsedMs());
        return modules;
    }

    ProtectionScanReport RunProtectionScan(PID_t pid) {
        ProtectionScanReport report{};
        if (pid == 0) return report;

        const Utils::Stopwatch totalTimer;
        std::vector<ModuleCandidate> modules = EnumerateModules(pid);
        LOG_PIPE_DEBUG("DenuvoAuth: pid={} scanning {} candidate module(s)", pid, modules.size());

        for (const auto& module : modules) {
            ++report.scannedModules;
            LOG_PIPE_DEBUG("DenuvoAuth: module candidate {}: {}", report.scannedModules, module.DebugString());

            const OSTPlatform::PE::Image image(module.nativePath);
            if (!image) {
                LOG_PIPE_DEBUG("DenuvoAuth: module skipped invalid or unreadable PE path={} size={}",
                               module.path, module.size);
                continue;
            }

            const auto match = DetectModule(module, image);
            if (!match) continue;

            report.pid = pid;
            report.denuvoDetected = true;
            report.method = match->method;
            report.modulePath = module.path;
            report.sectionName = match->sectionName;
            report.moduleSize = module.size;
            report.entryPointRva = match->entryPointRva;
            report.matchRva = match->matchRva;
            report.matchRawOffset = match->matchRawOffset;
            report.elapsedMs = totalTimer.ElapsedMs();

            LOG_PIPE_INFO("DenuvoAuth: success to detect Denuvo in module {}", report.DebugString());
            return report;
        }

        report.elapsedMs = totalTimer.ElapsedMs();
        LOG_PIPE_DEBUG("DenuvoAuth: pid={} no Denuvo match scanned={} elapsed_ms={:.3f}",
                       pid, report.scannedModules, report.elapsedMs);
        return report;
    }

} // namespace

const char* ToString(DetectionMethod method) {
    switch (method) {
    case DetectionMethod::None: return "None";
    case DetectionMethod::LegacySectionString: return "LegacySectionString";
    case DetectionMethod::OepPattern: return "OepPattern";
    }
    return "Unknown";
}

ProtectionScanReport ScanProtection(PID_t pid) {
    return RunProtectionScan(pid);
}

} // namespace PipeManager::DenuvoAuth
