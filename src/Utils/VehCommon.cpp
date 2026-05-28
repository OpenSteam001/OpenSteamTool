#include "VehCommon.h"

namespace {
    std::vector<VehCommon::Int3Site> g_sites;
    PVOID g_vehHandle = nullptr;
}

namespace VehCommon {

static LONG CALLBACK VehHandler(PEXCEPTION_POINTERS pExInfo) {
    auto code = pExInfo->ExceptionRecord->ExceptionCode;
    PCONTEXT ctx = pExInfo->ContextRecord;

    if (code == EXCEPTION_BREAKPOINT && OnBreakpoint(ctx))
        return EXCEPTION_CONTINUE_EXECUTION;
    if (code == EXCEPTION_SINGLE_STEP && OnSingleStep(ctx))
        return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

static void EnsureHandlerInstalled() {
    if (!g_vehHandle)
        g_vehHandle = AddVectoredExceptionHandler(1, VehHandler);
}

static void ArmInt3(void* target) {
    DWORD oldProtect = 0;
    VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
    *static_cast<uint8_t*>(target) = 0xCC;
}

static void RestoreByte(void* target, uint8_t original) {
    DWORD oldProtect = 0;
    VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
    *static_cast<uint8_t*>(target) = original;
}

void Arm(Int3Site site) {
    EnsureHandlerInstalled();
    g_sites.push_back(site);
    ArmInt3(site.target);
}

bool HasSites() {
    return !g_sites.empty();
}

bool OnBreakpoint(PCONTEXT ctx) {
    for (auto& site : g_sites) {
        if (!site.target || !IsAt(ctx->Rip, site.target)) continue;

        // Restore the original byte so the CPU can execute the real
        // first instruction when we resume.
        *site.target = site.originalByte;

        if (site.onHit) site.onHit(ctx, site);

        if (site.persistent) {
            // Set TF: CPU executes one instruction then raises SINGLE_STEP,
            // where we re-arm the int3.
            ctx->EFlags |= 0x100;
        }
        // For one-shot sites, leaving the byte restored permanently is the
        // desired behavior -- no further action needed.
        return true;
    }
    return false;
}

bool OnSingleStep(PCONTEXT ctx) {
    for (auto& site : g_sites) {
        if (!site.persistent || !site.target) continue;
        if (!IsPostInt3Step(ctx->Rip, site.target)) continue;
        *site.target = 0xCC;  // re-arm
        return true;
    }
    return false;
}

void DisarmAll() {
    for (auto& site : g_sites) {
        if (site.target && *site.target == 0xCC) {
            RestoreByte(site.target, site.originalByte);
        }
    }
    g_sites.clear();
}

void RemoveHandler() {
    if (g_vehHandle) {
        RemoveVectoredExceptionHandler(g_vehHandle);
        g_vehHandle = nullptr;
    }
}

} // namespace VehCommon
