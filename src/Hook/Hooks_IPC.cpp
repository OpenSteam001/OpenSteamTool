#include "dllmain.h"
#include "Hooks_IPC.h"
#include "Hooks_IPC_ISteamUser.h"
#include "Hooks_IPC_ISteamUtils.h"
#include "HookMacros.h"
#include "Hooks_Misc.h"
#include "Pipe/PipeManager.h"
#include "Utils/Support/FnvHash.h"
#include "Utils/SteamMetadata/IPCLoader.h"

namespace {

    RESOLVE_FUNC(GetPipeClient, CPipeClient*, void* pEngine, HSteamPipe hSteamPipe);

    static CPipeClient* GetPipe(void* pServer, HSteamPipe hSteamPipe) {
        return oGetPipeClient ? oGetPipeClient(pServer, hSteamPipe) : nullptr;
    }

    //  Handler dispatch table
    struct ResolvedHandler {
        EIPCInterface         interfaceID;
        uint32                funcHash;
        std::string           name;       // "IClientUser::GetSteamID" — for logs
        uint32                fencepost;
        uint32                argc;
        IPCHandlerFn          pre;
        IPCHandlerFn          post;

        ResolvedHandler(const IPCHandlerEntry& entry, const IPCLoader::Method& method)
            : interfaceID(method.interfaceID),
              funcHash(method.funcHash),
              name(std::string(entry.interfaceName) + "::" + entry.methodName),
              fencepost(method.fencepost),
              argc(method.argc),
              pre(entry.pre),
              post(entry.post) {}

        std::string DebugString() const {
            return std::format("{} -> hash=0x{:08X} fencepost=0x{:08X} argc={}",
                name, funcHash, fencepost, argc);
        }
    };
    std::vector<ResolvedHandler> g_Handlers;

    static ResolvedHandler* FindHandler(EIPCInterface iface, uint32 funcHash) {
        for (auto& e : g_Handlers) {
            if (e.interfaceID == iface && e.funcHash == funcHash) return &e;
        }
        return nullptr;
    }

    struct IPCDispatch {
        CPipeClient*     pipe = nullptr;
        ResolvedHandler* handler = nullptr;

        bool enabled() const {
            return pipe && handler;
        }

        std::string DebugString() const {
            return std::format("{} {}",pipe ? pipe->DebugString() : "null",
                                handler ? handler->DebugString() : "null");
        }
    };

    static IPCDispatch ResolveDispatch(void* pServer,HSteamPipe hSteamPipe,CUtlBuffer* pRead)
    {
        IPCDispatch dispatch{};
        dispatch.pipe = GetPipe(pServer, hSteamPipe);
        if (!dispatch.pipe) return dispatch;

        // We only care about InterfaceCall messages
        IPCMessages::IPCRequest request{pRead};
        if (!request.ok()) return dispatch;
        if (request.command() != EIPCCommand::InterfaceCall) return dispatch;

        // Ignore calls when appId is not resolved or not in Lua config
        if (!LuaConfig::HasDepot(Hooks_Misc::ResolveAppId())) return dispatch;

        // Parse out the interface call header to find the handler
        IPCMessages::IPCInterfaceCall call{request.body()};
        if (!call.ok()) return dispatch;

        // Lookup handler by interface ID + method hash
        dispatch.handler = FindHandler(call.interfaceID(), call.funcHash());
        if (!dispatch.handler) return dispatch;

        LOG_IPC_TRACE("Resolved IPC handler: {}", dispatch.DebugString());
        return dispatch;
    }

    static void HandleHandshake(void* pServer, HSteamPipe hSteamPipe,CUtlBuffer* pRead)
    {
        IPCMessages::IPCRequest request{pRead};
        if (!request.ok()|| request.command() != EIPCCommand::Handshake) return;
        IPCMessages::IPCHandshakeReq handshake{request.body()};
        if(!handshake.ok()) return;

        CPipeClient* pipe = GetPipe(pServer, hSteamPipe);
        if (!pipe) return;
        // set client PID 
        pipe->m_clientPID = handshake.pid();

        LOG_IPC_DEBUG("Received handshake from {},{}", pipe->DebugString(), handshake.DebugString());
        PipeManager::OnHandshake(pipe);
    }

    // Detect the first SteamNetworkingSockets call (interface 46) so GetAppID can
    // flip to 480 for P2P games. Skipped once already seen or when not in onlinefix.
    static void DetectNetworkingSockets(CUtlBuffer* pRead) {
        if (!Hooks_Misc::IsOnlineFixActive() || Hooks_Misc::ShouldReportOnlineFixAppId()) return;
        IPCMessages::IPCRequest request{pRead};
        if (!request.ok() || request.command() != EIPCCommand::InterfaceCall) return;
        IPCMessages::IPCInterfaceCall call{request.body()};
        if (!call.ok()) return;
        if (call.interfaceID() == EIPCInterface::IClientNetworkingSocketsSerialized)
            Hooks_Misc::NotifyNetworkingSocketsUsed();
    }

    HOOK_FUNC(IPCProcessMessage, bool,void* pServer, HSteamPipe hSteamPipe,
              CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        // handle handshake messages
        HandleHandshake(pServer, hSteamPipe, pRead);
        DetectNetworkingSockets(pRead);

        IPCDispatch dispatch = ResolveDispatch(pServer, hSteamPipe, pRead);
        // If we didn't find a handler for this message, just pass through to the original function.
        if(!dispatch.enabled()) return oIPCProcessMessage(pServer, hSteamPipe, pRead, pWrite);

        // If we did find a handler, run the pre-handler
        if (dispatch.handler->pre)
            dispatch.handler->pre(dispatch.pipe, pRead, pWrite);

        // Then call the original function to let steamclient process the message as normal.
        bool result = oIPCProcessMessage(pServer, hSteamPipe, pRead, pWrite);

        // Ultimately the post-handler can choose to modify the response.
        if (result && dispatch.handler->post)
            dispatch.handler->post(dispatch.pipe, pRead, pWrite);

        return result;
    }

} // namespace


namespace Hooks_IPC {

    void Install() {
        RESOLVE_C(GetPipeClient);

        // Each module registers a static array. Hash lookup against the
        // IPCLoader metadata happens inside RegisterHandlers.
        Hooks_IPC_ISteamUser::Register();
        Hooks_IPC_ISteamUtils::Register();

        LOG_IPC_INFO("Hooks_IPC: {} handlers registered", g_Handlers.size());

        HOOK_BEGIN();
        INSTALL_HOOK_C(IPCProcessMessage);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK_C(IPCProcessMessage);
        UNHOOK_END();
    }

    void RegisterHandlers(std::span<const IPCHandlerEntry> entries) {
        for (const auto& e : entries) {
            const auto* m = IPCLoader::Find(e.interfaceName, e.methodName);
            if (!m) {
                LOG_IPC_WARN("[Handler Disabled] no IPC spec for {}",e.DebugString());
                continue;
            }
            auto& handler = g_Handlers.emplace_back(e,*m);
            LOG_IPC_DEBUG("Hooks_IPC: resolved {}", handler.DebugString());
        }
    }

}
