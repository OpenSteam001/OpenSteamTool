#include "Pipe/Features/Injection/Injection.h"

#include "OSTPlatform/include/Process.h"
#include "OSTPlatform/include/RemoteProcess.h"
#include "Utils/Config/Config.h"
#include "Utils/Logging/Log.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>

namespace PipeManager::Injection {
namespace {

    // Keyed on (process, path) so each DLL injects at most once per process
    // while several [[inject]] entries can still target the same game.
    struct InjectedKey {
        ProcessKey  process;
        std::string path;
        bool operator==(const InjectedKey&) const = default;
    };
    struct InjectedKeyHash {
        std::size_t operator()(const InjectedKey& key) const noexcept {
            return ProcessKeyHash{}(key.process) ^ std::hash<std::string>{}(key.path);
        }
    };

    std::mutex g_mutex;
    std::unordered_set<InjectedKey, InjectedKeyHash> g_injected;

    bool ClaimInjection(const InjectedKey& key) {
        std::scoped_lock lock(g_mutex);
        return g_injected.insert(key).second;
    }

    bool Matches(const Config::InjectDll& dll, const PipeContext& ctx,
                 const std::optional<std::string>& cmdLine) {
        if (!dll.allGames && !ctx.trackedApp) return false;
        if (!dll.whenAppids.empty() && !dll.whenAppids.count(ctx.appId)) return false;
        if (!dll.whenCmdline.empty() &&
            (!cmdLine || cmdLine->find(dll.whenCmdline) == std::string::npos)) {
            return false;
        }
        return true;
    }

} // namespace

void Apply(const PipeContext& ctx) {
    if (Config::injectDlls.empty()) return;
    if (!ctx.gameProcess) return;

    // Read the command line lazily: only if an injection entry uses it.
    std::optional<std::string> cmdLine;
    bool cmdLineResolved = false;
    auto commandLine = [&]() -> const std::optional<std::string>& {
        if (!cmdLineResolved) {
            cmdLine = OSTPlatform::Process::GetProcessCommandLine(ctx.process.pid);
            cmdLineResolved = true;
        }
        return cmdLine;
    };

    for (const auto& dll : Config::injectDlls) {
        const std::optional<std::string>& cmd = dll.whenCmdline.empty() ? cmdLine : commandLine();
        if (!Matches(dll, ctx, cmd)) continue;
        if (!ClaimInjection({ctx.process, dll.path})) continue;

        const std::filesystem::path path(dll.path);
        const auto status = OSTPlatform::RemoteProcess::InjectLibrary(ctx.process.pid, path);
        if (status == OSTPlatform::RemoteProcess::InjectStatus::Ok) {
            LOG_INJECT_INFO("injected pid={} appid={} dll=\"{}\"",
                            ctx.process.pid, ctx.appId, path.filename().string());
        } else {
            LOG_INJECT_WARN("inject failed pid={} appid={} status={} dll=\"{}\"",
                            ctx.process.pid, ctx.appId,
                            OSTPlatform::RemoteProcess::ToString(status),
                            path.filename().string());
        }
    }
}

} // namespace PipeManager::Injection
