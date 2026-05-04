#pragma once

#include "Steam/Types.h"
#include "Steam/Enums.h"
#include "Steam/Structs.h"

// ── IPC InterfaceCall packet layout ─────────────────────────────
//  offset 0:  cmd          (1 byte, EIPCCommand)
//  offset 1:  interfaceID  (1 byte, EIPCInterface)
//  offset 2:  hSteamUser   (4 bytes)
//  offset 6:  funcHash     (4 bytes)
//  offset 10: args[]       (variable)
// ─────────────────────────────────────────────────────────────────
constexpr int OFFSET_CMD          = 0;
constexpr int OFFSET_INTERFACE_ID = 1;
constexpr int OFFSET_FUNC_HASH    = 6;
constexpr int OFFSET_ARGS         = 10;
constexpr int IPC_HEADER_SIZE     = 10;
constexpr uint8 RESPONSE_PREFIX   = 0x0B;

namespace Hooks_IPC {

    void Install();
    void Uninstall();

    // ── Handler registry ────────────────────────────────────────
    // Each interface module (ISteamUser, ISteamUtils, …) defines
    // its own handlers and registers them via Register() so the
    // central dispatcher can find them at runtime.

    using IpcHandlerFn = void(*)(CUtlBuffer* pWrite,
                                 const uint8* reqData, int32 reqSize,
                                 AppId_t appId);

    struct IpcHandlerEntry {
        EIPCInterface interfaceID;
        uint32        funcHash;
        const char*   name;       // "Interface::Method" — logging
        IpcHandlerFn  handler;
    };

    void RegisterHandlers(const IpcHandlerEntry* entries, size_t count);

}
