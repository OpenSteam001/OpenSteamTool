#include "Pipe/Features/DenuvoAuth/DenuvoAuth.h"

#include "Pipe/Features/DenuvoAuth/ProtectionScan.h"
#include "Utils/Logging/Log.h"
#include "Utils/Tickets/AppTicket.h"
#include "Utils/Config/LuaConfig.h"
#include "OSTPlatform/include/SteamCredentialStore.h"

#include <cwctype>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace PipeManager::DenuvoAuth {
namespace {

    constexpr uint32 kEndDenuvoVerificationHandshake = 2;

    enum class Stage {
        None,
        Authorizing,
        EndAuthorization,
    };

    const char* ToString(Stage stage) {
        switch (stage) {
        case Stage::None:             return "None";
        case Stage::Authorizing:      return "Authorizing";
        case Stage::EndAuthorization: return "EndAuthorization";
        }
        return "?";
    }

    bool EqualsUniverseName(std::wstring_view lhs, std::wstring_view rhs) {
        if (lhs.size() != rhs.size()) return false;

        for (size_t i = 0; i < lhs.size(); ++i) {
            if (std::towlower(lhs[i]) != std::towlower(rhs[i])) return false;
        }

        return true;
    }

    EUniverse ParseUniverse(std::wstring_view universe) {
        if (EqualsUniverseName(universe, L"Public")) return k_EUniversePublic;
        if (EqualsUniverseName(universe, L"Beta")) return k_EUniverseBeta;
        if (EqualsUniverseName(universe, L"Internal")) return k_EUniverseInternal;
        if (EqualsUniverseName(universe, L"Dev")) return k_EUniverseDev;
        return k_EUniverseInvalid;
    }

    std::optional<uint64> GetCurrentSteamIdForDenuvoAuth() {
        uint32 accountId = 0;
        std::wstring universeName;
        const auto status = OSTPlatform::SteamCredentialStore::GetActiveUser(accountId, universeName);
        if (status != OSTPlatform::SteamCredentialStore::Status::Ok) {
            LOG_PIPE_WARN("DenuvoAuth: active Steam user unavailable ({})",
                           OSTPlatform::SteamCredentialStore::ToString(status));
            return std::nullopt;
        }

        const EUniverse universe = ParseUniverse(universeName);
        if (universe == k_EUniverseInvalid) {
            LOG_PIPE_WARN("DenuvoAuth: active Steam user has unsupported universe");
            return std::nullopt;
        }

        CSteamID steamId;
        steamId.Set(accountId, universe, k_EAccountTypeIndividual);
        return steamId.ConvertToUint64();
    }

    struct ProcessAuth {
        bool scanned = false;
        bool denuvo = false;
        Stage stage = Stage::None;
        uint32 pid = 0;
        uint32 handshakeCount = 0;

        std::optional<PipeKey> authorizationPipe;
        AppId_t authorizedAppId = k_uAppIdInvalid;

        std::string DebugString() const {
            return std::format("denuvo={} stage={} handshakeCount={} auth_appid={} pid={}",
                               denuvo, ToString(stage), handshakeCount, authorizedAppId, pid);
        }

        void OnHandshake(const PipeContext& ctx, const PipeKey& pipeKey) {
            ++handshakeCount;

            if (!denuvo) {
                stage = Stage::None;
                return;
            }

            if (!authorizationPipe.has_value()) {
                authorizationPipe = pipeKey;
                authorizedAppId = ctx.appId;
                pid = ctx.process.pid;
                stage = Stage::Authorizing;
                LOG_PIPE_INFO("DenuvoAuth: authorization pipe selected {}", this->DebugString());
            }

            if (stage == Stage::Authorizing &&
                handshakeCount >= kEndDenuvoVerificationHandshake) {
                stage = Stage::EndAuthorization;
                LOG_PIPE_INFO("DenuvoAuth: authorization window ended {}", this->DebugString());
                if(LuaConfig::IsOwned(authorizedAppId)){
                    WriteSteamIdOnEndAuthorization();
                }
            }
        }

        bool CanUseAuthorizedIdentity(const PipeKey& key) const {
            return denuvo && stage == Stage::Authorizing && authorizationPipe == key;
        }

        void WriteSteamIdOnEndAuthorization() const {
            if (authorizedAppId == k_uAppIdInvalid) {
                LOG_PIPE_WARN("DenuvoAuth: end authorization skipped SteamID persist without auth app");
                return;
            }

            const std::optional<uint64> steamId = GetCurrentSteamIdForDenuvoAuth();
            if (!steamId || *steamId == 0) {
                LOG_PIPE_WARN("DenuvoAuth: end authorization no current SteamID source auth_appid={}",
                               authorizedAppId);
                return;
            }

            if (AppTicket::WriteSteamID(authorizedAppId, *steamId)) {
                LOG_PIPE_INFO("DenuvoAuth: persisted end-authorization SteamID auth_appid={} steamid={}",
                              authorizedAppId, *steamId);
                return;
            }

            LOG_PIPE_WARN("DenuvoAuth: failed to persist end-authorization SteamID auth_appid={} steamid={}",
                          authorizedAppId, *steamId);
        }
    };

    // All access runs on the single Steam IPC thread, so these need no lock.
    std::unordered_map<ProcessKey, ProcessAuth, ProcessKeyHash> g_processAuth;
    std::unordered_map<PipeKey, ProcessKey, PipeKeyHash> g_pipeProcess;

    ProcessAuth* FindAuthForPipe(const PipeKey& pipeKey) {
        const auto pipeIt = g_pipeProcess.find(pipeKey);
        if (pipeIt == g_pipeProcess.end()) return nullptr;

        const auto authIt = g_processAuth.find(pipeIt->second);
        return authIt == g_processAuth.end() ? nullptr : &authIt->second;
    }

    void EnsureScanned(ProcessAuth& auth, const ProcessKey& process, AppId_t appId) {
        if (auth.scanned) {
            LOG_PIPE_TRACE("DenuvoAuth: reusing cached protection result {} denuvo={}",
                           process.DebugString(), auth.denuvo);
            return;
        }

        auth.scanned = true;
        auth.denuvo = ScanProtection(process.pid).denuvoDetected;
        // Fallback when signature detection misses a real Denuvo title (some builds
        // slip both the OEP and legacy-string heuristics — see linked issue). Per the
        // README, setAppTicket/setETicket are only used for ticket-gated (Denuvo)
        // games, so an injected EncryptedAppTicket for this app is a strong "the user
        // intends Denuvo auth" signal; engage the auth path instead of giving up.
        if (!auth.denuvo &&
            !AppTicket::GetEncryptedTicketFromCredentialStore(appId).empty()) {
            LOG_PIPE_INFO("DenuvoAuth: scan missed but injected eticket present; treating as Denuvo appid={}", appId);
            auth.denuvo = true;
        }
        if (!auth.denuvo) auth.stage = Stage::None;
    }

} // namespace

void Apply(const PipeContext& ctx) {
    if (!ctx.gameProcess || !ctx.trackedApp) return;

    const PipeKey pipeKey = MakePipeKey(ctx.pipe);
    if (!pipeKey.IsValid()) return;

    ProcessAuth& auth = g_processAuth[ctx.process];
    g_pipeProcess[pipeKey] = ctx.process;

    EnsureScanned(auth, ctx.process, ctx.appId);
    auth.OnHandshake(ctx, pipeKey);
}

bool IsAuthorizedPipe(const CPipeClient* pipe) {
    if (!pipe) {
        LOG_PIPE_TRACE("DenuvoAuth: pipe not in authorization window: null pipe");
        return false;
    }

    const PipeKey pipeKey = MakePipeKey(pipe);
    const ProcessAuth* auth = FindAuthForPipe(pipeKey);
    if (!auth) {
        LOG_PIPE_TRACE("DenuvoAuth: pipe not tracked by DenuvoAuth {}", pipeKey.DebugString());
        return false;
    }

    if (!auth->CanUseAuthorizedIdentity(pipeKey)) {
        LOG_PIPE_TRACE("DenuvoAuth: pipe not in authorization window {} {}", pipeKey.DebugString(), auth->DebugString());
        return false;
    }
    LOG_PIPE_INFO("DenuvoAuth: pipe in authorization window {} {}", pipeKey.DebugString(), auth->DebugString());
    return true;
}

} // namespace PipeManager::DenuvoAuth
