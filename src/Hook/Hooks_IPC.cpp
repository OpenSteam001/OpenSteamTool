#include "Hooks_IPC.h"
#include "Hooks_IPC_ISteamUser.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "Utils/Hash.h"
#include "Hooks_Misc.h"

namespace {

    // ════════════════════════════════════════════════════════════════
    //  CServerPipe layout
    //
    //  +32  clientPID    (uint32)
    //  +40  processName  (char*)
    // ════════════════════════════════════════════════════════════════
    constexpr int PIPE_OFFSET_CLIENT_PID   = 32;
    constexpr int PIPE_OFFSET_PROCESS_NAME = 40;

    // ════════════════════════════════════════════════════════════════
    //  Pipe helpers
    // ════════════════════════════════════════════════════════════════

    using GetPipeClient_t = void*(__fastcall*)(void* pEngine, uint32 hSteamPipe);
    GetPipeClient_t oGetPipeClient = nullptr;

    uint32 g_SteamPID = 0;

    // Pre-computed FNV-1a hashes for known steam internal processes.
    constexpr uint32 HASH_PROC_steamwebhelper   = Fnv1aHash("steamwebhelper.exe");
    constexpr uint32 HASH_PROC_gameoverlayui64  = Fnv1aHash("gameoverlayui64.exe");
    constexpr uint32 HASH_PROC_gameoverlayui    = Fnv1aHash("gameoverlayui.exe");

    // Cache IsSteamInternal results keyed by process name hash.
    std::unordered_map<uint32, bool> g_IsSteamInternalCache;

    static void* GetPipe(void* pServer, uint32 hSteamPipe) {
        return oGetPipeClient ? oGetPipeClient(pServer, hSteamPipe) : nullptr;
    }

    static uint32 GetPipeClientPID(void* pipe) {
        if (!pipe) return 0;
        return *reinterpret_cast<uint32*>(
            reinterpret_cast<uint8*>(pipe) + PIPE_OFFSET_CLIENT_PID);
    }

    // May return nullptr — Steam leaves the processName slot null for pipes
    // whose owner hasn't been resolved yet. Callers MUST null-check.
    static const char* GetPipeProcessName(void* pipe) {
        if (!pipe) return nullptr;
        return *reinterpret_cast<const char**>(
            reinterpret_cast<uint8*>(pipe) + PIPE_OFFSET_PROCESS_NAME);
    }

    // True when the request originated from steam internals — never spoof these.
    static bool IsSteamInternal(void* pipe, uint32 pid) {
        if (pid == 0 || pid == g_SteamPID) return true;
        if (!pipe) return false;

        const char* name = GetPipeProcessName(pipe);
        if (!name) return false;  // unresolved, check again later

        const uint32 nameHash = Fnv1aHash(name);
        auto it = g_IsSteamInternalCache.find(nameHash);
        if (it != g_IsSteamInternalCache.end()) return it->second;

        bool isInternal = nameHash == HASH_PROC_steamwebhelper
                       || nameHash == HASH_PROC_gameoverlayui64
                       || nameHash == HASH_PROC_gameoverlayui;

        g_IsSteamInternalCache[nameHash] = isInternal;
        return isInternal;
    }

    // ════════════════════════════════════════════════════════════════
    //  Handler registry
    // ════════════════════════════════════════════════════════════════

    std::vector<Hooks_IPC::IpcHandlerEntry> g_Handlers;

    static const Hooks_IPC::IpcHandlerEntry* FindHandler(EIPCInterface iface, uint32 funcHash) {
        for (auto& e : g_Handlers) {
            if (e.interfaceID == iface && e.funcHash == funcHash) return &e;
        }
        return nullptr;
    }

    // Decode the request header and return the dispatch entry, or nullptr if
    // the packet is not an InterfaceCall we know how to spoof.
    static const Hooks_IPC::IpcHandlerEntry* DecodeRequest(const CUtlBuffer* pRead) {
        const int32 size = pRead->m_Put;
        if (size < IPC_HEADER_SIZE) return nullptr;

        const uint8* data = pRead->m_Memory.m_pMemory;
        const auto cmd = static_cast<EIPCCommand>(data[OFFSET_CMD]);
        if (cmd != EIPCCommand::InterfaceCall) {
            LOG_IPC_TRACE("ipc: cmd={} size={} (skipped)", static_cast<int>(cmd), size);
            return nullptr;
        }

        const auto iface = static_cast<EIPCInterface>(data[OFFSET_INTERFACE_ID]);
        const uint32 funcHash = *reinterpret_cast<const uint32*>(data + OFFSET_FUNC_HASH);
        const auto* entry = FindHandler(iface, funcHash);
        if (!entry) {
            LOG_IPC_TRACE("ipc: unhandled iface={} hash=0x{:08X}",
                      static_cast<int>(iface), funcHash);
            return nullptr;
        }

        LOG_IPC_TRACE("ipc: dispatch -> {}", entry->name);
        return entry;
    }

    // ════════════════════════════════════════════════════════════════
    //  Main hook
    // ════════════════════════════════════════════════════════════════
    HOOK_FUNC(IPCProcessMessage, bool,
              void* pServer, uint32 hSteamPipe,
              CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        const int32  reqSize = pRead->m_Put;
        const uint8* reqData = pRead->m_Memory.m_pMemory;

        const auto* entry = DecodeRequest(pRead);

        // ── Log every call (before IsSteamInternal, so Handshake's pid=0 isn't filtered) ─
        void* pipe = GetPipe(pServer, hSteamPipe);
        const uint32 clientPID = GetPipeClientPID(pipe);
        const char* procName = GetPipeProcessName(pipe);
        const auto cmd = static_cast<EIPCCommand>(reqData[OFFSET_CMD]);

        if (cmd == EIPCCommand::Handshake) {
            LOG_IPC_INFO("IPC Handshake: pipe=0x{:08X} pid={} proc={}", hSteamPipe, clientPID, procName ? procName : "?");
        } 
        else if (!IsSteamInternal(pipe, clientPID)) {
            if (cmd == EIPCCommand::InterfaceCall) {
                const auto iface = static_cast<EIPCInterface>(reqData[OFFSET_INTERFACE_ID]);
                const uint32 funcHash = *reinterpret_cast<const uint32*>(reqData + OFFSET_FUNC_HASH);
                LOG_IPC_DEBUG("IPC {}::0x{:08X}  pipe=0x{:08X} pid={} proc={}",
                            EIPCInterfaceName(iface), funcHash,
                            hSteamPipe, clientPID, procName ? procName : "?");
            } 
            else {
                LOG_IPC_DEBUG("IPC {}  pipe=0x{:08X} pid={} proc={}",
                            EIPCCommandName(cmd), hSteamPipe, clientPID,
                            procName ? procName : "?");
            }
        }
        

        const bool result = oIPCProcessMessage(pServer, hSteamPipe, pRead, pWrite);
        if (!result || !entry) return result;

        // ── Spoofing ──────────────────────────────────────────────
        if (IsSteamInternal(pipe, clientPID)) {
            LOG_IPC_TRACE("ipc: {} from steam — passthrough", entry->name);
            return result;
        }

        AppId_t appId = Hooks_Misc::GetAppIDForCurrentPipe();
        if (appId == 0)
            appId = Hooks_Misc::GetAppIDFromInitialRunningGame();
        if (appId == 0 ) {
            LOG_IPC_WARN("ipc: {} pid={} can't get appid",entry->name, clientPID);
            return result;
        }

        entry->handler(pWrite, reqData, reqSize, appId);
        return result;
    }

} // namespace


namespace Hooks_IPC {

    void RegisterHandlers(const IpcHandlerEntry* entries, size_t count) {
        g_Handlers.insert(g_Handlers.end(), entries, entries + count);
    }

    void Install() {
        g_SteamPID = GetCurrentProcessId();
        RESOLVE_D(GetPipeClient);

        // Interface modules register their handlers here.
        Hooks_IPC_ISteamUser::Register();
        // Hooks_IPC_ISteamUtils::Register();  // future

        HOOK_BEGIN();
        INSTALL_HOOK_D(IPCProcessMessage);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(IPCProcessMessage);
        UNHOOK_END();
        oGetPipeClient            = nullptr;
        g_SteamPID                = 0;
        g_Handlers.clear();
        g_IsSteamInternalCache.clear();
    }

}
