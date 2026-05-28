#pragma once

#include "Log.h"

#include <windows.h>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace VehCommon {

    // ── x86-64 instruction length cap ────────────────────────────────────────
    constexpr uint64_t kMaxX64InsnLen = 15;

    // ── RIP comparisons ──────────────────────────────────────────────────────
    inline bool IsAt(uint64_t rip, const void* target) {
        return rip == reinterpret_cast<uint64_t>(target);
    }

    // True if rip is in (target, target + kMaxX64InsnLen], i.e. one
    // instruction past `target`. Used by the SINGLE_STEP branch to recognize
    // that the trap was caused by us setting TF after restoring `target`'s
    // first byte. Independent of the prologue's actual instruction length.
    inline bool IsPostInt3Step(uint64_t rip, const void* target) {
        auto base = reinterpret_cast<uint64_t>(target);
        return rip > base && rip <= base + kMaxX64InsnLen;
    }

    // ── x64 fastcall argument access ─────────────────────────────────────────
    // Read the Nth argument (1-based) of the function whose prologue was just
    // entered. Args 1-4 are in RCX/RDX/R8/R9; args 5+ live at [RSP + 8*N]
    // (return address at RSP+0, shadow space at +8..+0x20, arg5 at +0x28).
    // Only valid before RSP moves -- i.e. on an int3 hit at the first
    // instruction of the function.
    template<typename T>
    inline T GetArg(PCONTEXT ctx, int index) {
        static_assert(sizeof(T) <= sizeof(uint64_t),
                      "GetArg<T>: T must fit in a 64-bit register slot");
        uint64_t raw;
        switch (index) {
            case 1:  raw = ctx->Rcx; break;
            case 2:  raw = ctx->Rdx; break;
            case 3:  raw = ctx->R8;  break;
            case 4:  raw = ctx->R9;  break;
            default: raw = *reinterpret_cast<uint64_t*>(ctx->Rsp + 8 * index);
                     break;
        }
        if constexpr (std::is_pointer_v<T>) {
            return reinterpret_cast<T>(raw);
        } else {
            return static_cast<T>(raw);
        }
    }

    // ── Int3Site: managed soft-breakpoint site ───────────────────────────────
    // One soft-breakpoint location handled by the shared VEH dispatcher.
    //
    //   persistent = false   one-shot: on hit, restore byte and leave disarmed.
    //                        Typical use: capture a `this` pointer once on the
    //                        first call, then disappear.
    //
    //   persistent = true    on hit, restore byte and set TF.  The next
    //                        SINGLE_STEP exception re-arms the int3.  Typical
    //                        use: continuous interception that mutates args.
    //
    // onHit fires after the byte is restored but before control returns to the
    // original code. It may read/mutate the CPU context (ctx) freely and inspect
    // the site metadata for logging or site-specific callback data.
    struct Int3Site {
        uint8_t*    target;        // first byte of the hooked function
        uint8_t     originalByte;  // saved before arming
        bool        persistent;
        void      (*onHit)(PCONTEXT ctx, const Int3Site& site);
        void*       callbackData;  // site-specific data consumed by onHit
        const char* label;         // logging tag
    };

    // Append a site to the registry and write 0xCC to its first byte.
    // Caller must populate `originalByte` from `*target` before calling.
    void Arm(Int3Site site);

    // True if at least one site is currently registered.
    bool HasSites();

    // Drive these from a Vectored Exception Handler:
    //   - returns true if the exception matched a registered site and was
    //     handled (caller should return EXCEPTION_CONTINUE_EXECUTION)
    //   - returns false if nothing matched (caller should fall through to
    //     EXCEPTION_CONTINUE_SEARCH)
    bool OnBreakpoint(PCONTEXT ctx);
    bool OnSingleStep(PCONTEXT ctx);

    // Restore any still-armed bytes and clear the registry. Call during global
    // hook shutdown before removing the VEH handler.
    void DisarmAll();

    // Remove the shared VEH handler. Call only after all sites are disarmed.
    void RemoveHandler();

    // Convenience onHit for the "capture this" pattern.
    // callbackData must point to a `void*` slot that will receive RCX (the
    // implicit `this` argument in the Windows x64 fastcall convention).
    inline void CaptureRcxTo(PCONTEXT ctx, const Int3Site& site) {
        *static_cast<void**>(site.callbackData) = GetArg<void*>(ctx, 1);
        LOG_MISC_DEBUG("CaptureRcxTo {}: captured {}", site.label, GetArg<void*>(ctx, 1));
    }
}

// ── CAPTURE_THIS_FUNC ────────────────────────────────────────────────────────
//   CAPTURE_THIS_FUNC(GetPackageInfo, PackageInfo*, g_pCPackageInfo,
//                     void*, uint32, int64);
// generates:
//   using GetPackageInfo_t = PackageInfo*(__fastcall*)(void*, uint32, int64);
//   inline GetPackageInfo_t oGetPackageInfo = nullptr;
//   inline void* g_pCPackageInfo = nullptr;
//   inline void** const _capture_out_GetPackageInfo = &g_pCPackageInfo;
//
// The trailing `_capture_out_*` slot lets ARM_CAPTURE_C(name) pick up the
// outVar binding without the caller having to repeat it.
#define CAPTURE_THIS_FUNC(name, ret, outVar, ...)          \
    using name##_t = ret(__fastcall*)(__VA_ARGS__);        \
    inline name##_t o##name = nullptr;                     \
    inline void* outVar = nullptr;                         \
    inline void** const _capture_out_##name = &outVar

// ── ARM_INT3 ─────────────────────────────────────────────────────────────────
// Generic int3 site registration: resolve `name` in `module`, save its first
// byte, register an Int3Site with the given persistence, onHit, and callbackData.
// Use this when you need full control (e.g. persistent intercepts whose
// callback mutates args). For the common "capture this" pattern see
// ARM_CAPTURE_C below.
#define ARM_INT3(module, name, persistent_, onHit_, callbackData_)        \
    do {                                                                  \
        if (auto* _p_ = FIND_SIG(module, name)) {                         \
            auto* _t_ = static_cast<uint8_t*>(_p_);                       \
            VehCommon::Arm(VehCommon::Int3Site{                           \
                _t_, *_t_, persistent_, onHit_, callbackData_, #name,     \
            });                                                           \
        }                                                                 \
    } while (0)

#define ARM_INT3_C(name, persistent_, onHit_, callbackData_)              \
    ARM_INT3(client_hModule, name, persistent_, onHit_, callbackData_)

#define ARM_INT3_U(name, persistent_, onHit_, callbackData_)              \
    ARM_INT3(ui_hModule, name, persistent_, onHit_, callbackData_)

// ── CAPTURE_READY ────────────────────────────────────────────────────────────
// True when both the captured `this` pointer and the resolved function pointer
// are populated. Use to guard call sites where either may not be set yet
// (capture hasn't fired, or symbol resolution failed).
//
//   if (CAPTURE_READY(GetAppDataFromAppInfo)) { ... }
// is equivalent to
//   if (g_pCAppInfoCache && oGetAppDataFromAppInfo) { ... }
#define CAPTURE_READY(name) (*_capture_out_##name && o##name)

// ── ARM_CAPTURE ────────────────────────────────────────────────────────────
// Pair to CAPTURE_THIS_FUNC.  Resolves the symbol, saves its first byte,
// registers a one-shot Int3Site that writes RCX into the outVar bound by
// CAPTURE_THIS_FUNC (via `_capture_out_##name`), and stashes the resolved
// address into `o##name` so callers can invoke the original after capture.
#define ARM_CAPTURE(module,name)                                          \
    do {                                                                  \
        if (auto* _p_ = FIND_SIG(module, name)) {                         \
            o##name = reinterpret_cast<name##_t>(_p_);                    \
            auto* _t_ = static_cast<uint8_t*>(_p_);                       \
            VehCommon::Arm(VehCommon::Int3Site{                           \
                _t_, *_t_, /*persistent=*/false,                          \
                &VehCommon::CaptureRcxTo,                                 \
                static_cast<void*>(_capture_out_##name),                  \
                #name,                                                    \
            });                                                           \
        }                                                                 \
    } while (0)

#define ARM_CAPTURE_C(name) ARM_CAPTURE(client_hModule, name)
#define ARM_CAPTURE_U(name) ARM_CAPTURE(ui_hModule, name)
