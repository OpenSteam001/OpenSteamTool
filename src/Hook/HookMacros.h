#pragma once

// ─────────────────────────────────────────────────────────────────
// Hook boilerplate elimination macros.
//
// Convention: macros ending in _D use client_hModule as module.
// Standard macros (no _D suffix) take an explicit module argument.
// ─────────────────────────────────────────────────────────────────

#include <windows.h>
#include <detours.h>
#include "Utils/ByteSearch.h"

// ── transaction helpers ─────────────────────────────────────────
#define HOOK_BEGIN()                          \
    do {                                       \
        DetourTransactionBegin();              \
        DetourUpdateThread(GetCurrentThread())

#define HOOK_END()                            \
        DetourTransactionCommit();             \
    } while (0)

#define UNHOOK_BEGIN()                        \
    do {                                       \
        DetourTransactionBegin();              \
        DetourUpdateThread(GetCurrentThread())

#define UNHOOK_END()                          \
        DetourTransactionCommit();             \
    } while (0)

// ── hook function definition ────────────────────────────────────
//  HOOK_FUNC(LoadModuleWithPath, HMODULE, const char* path, bool f) {
//      return oLoadModuleWithPath(path, f);
//  }
//
// generates:
//   using LoadModuleWithPath_t = HMODULE(__fastcall*)(const char*, bool);
//   inline LoadModuleWithPath_t oLoadModuleWithPath = nullptr;
//   HMODULE __fastcall hkLoadModuleWithPath(const char* path, bool f)
#define HOOK_FUNC(name, ret, ...)                         \
    using name##_t = ret(__fastcall*)(__VA_ARGS__);        \
    inline name##_t o##name = nullptr;                      \
    ret __fastcall hk##name(__VA_ARGS__)

    
// ── install ─────────────────────────────────────────────────────
// Call between HOOK_BEGIN / HOOK_END.
#define INSTALL_HOOK(module, name)                                    \
do {                                                              \
    void* _p_ = FIND_SIG(module, name);                            \
    if (_p_) {                                                    \
        o##name = (name##_t)_p_;                                  \
        DetourAttach(reinterpret_cast<PVOID*>(&o##name),           \
        reinterpret_cast<PVOID>(hk##name));           \
    }                                                             \
} while (0)

#define INSTALL_HOOK_C(name)            INSTALL_HOOK(client_hModule, name)
#define INSTALL_HOOK_U(name)            INSTALL_HOOK(ui_hModule, name)

// ── uninstall ───────────────────────────────────────────────────
// Call between UNHOOK_BEGIN / UNHOOK_END.
#define UNINSTALL_HOOK(name)                                          \
    do {                                                              \
        if (o##name) {                                                \
            DetourDetach(reinterpret_cast<PVOID*>(&o##name),           \
                         reinterpret_cast<PVOID>(hk##name));           \
            o##name = nullptr;                                        \
        }                                                             \
    } while (0)
#define UNINSTALL_HOOK_C(name)        UNINSTALL_HOOK(name)
#define UNINSTALL_HOOK_U(name)        UNINSTALL_HOOK(name)

// ── resolve function definition ────────────────────────────────────
//   RESOLVE_FUNC(CUtlMemoryGrow, void*, CUtlVector<AppId_t>*, int);
// generates:
//   using CUtlMemoryGrow_t = void*(__fastcall*)(CUtlVector<AppId_t>*, int);
//   inline CUtlMemoryGrow_t oCUtlMemoryGrow = nullptr;
#define RESOLVE_FUNC(name, ret, ...)                       \
    using name##_t = ret(__fastcall*)(__VA_ARGS__);        \
    inline name##_t o##name = nullptr

// ── resolve ─────────────────────────────────────────────────────
// Find signature → cast to name##_t → assign to o##name.
#define RESOLVE(module, name) \
do { \
    void* _p_ = FIND_SIG(module, name); \
    if (_p_) { \
        o##name = reinterpret_cast<name##_t>(_p_); \
    } \
} while (0)

#define RESOLVE_C(name)       RESOLVE(client_hModule, name)
#define RESOLVE_U(name)       RESOLVE(ui_hModule, name)


