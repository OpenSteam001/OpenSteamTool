#include "include/Memory.h"

#include "include/Log.h"

#include <windows.h>
#include <psapi.h>

namespace OSTPlatform::Memory {

std::optional<ModuleImage> GetModuleImage(DynamicLibrary::ModuleHandle module) {
    MODULEINFO info{};
    if (!module || !GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(module), &info, sizeof(info))) {
        OSTP_LOG_DEBUG("GetModuleImage(module={}) failed (error={})", module, GetLastError());
        return std::nullopt;
    }

    return ModuleImage{
        static_cast<uint8_t*>(info.lpBaseOfDll),
        static_cast<size_t>(info.SizeOfImage),
    };
}

bool IsReadable(const void* address, size_t size) {
    if (!address || size == 0) return false;

    const auto begin = reinterpret_cast<uintptr_t>(address);
    const auto end   = begin + size;
    if (end < begin) return false;

    // A range may span several regions; walk them all.
    for (uintptr_t cur = begin; cur < end; ) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cur), &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        cur = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

bool WriteExecutableByte(void* target, uint8_t value) {
    if (!target) {
        OSTP_LOG_WARN("WriteExecutableByte: target is null");
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        OSTP_LOG_WARN("WriteExecutableByte(target={}) VirtualProtect(RWX) failed (error={})",
                      target, GetLastError());
        return false;
    }
    *static_cast<uint8_t*>(target) = value;

    DWORD ignored = 0;
    if (!VirtualProtect(target, 1, oldProtect, &ignored)) {
        OSTP_LOG_WARN("WriteExecutableByte(target={}) VirtualProtect(restore=0x{:X}) failed (error={})",
                      target, oldProtect, GetLastError());
        return false;
    }
    if (!FlushInstructionCache(GetCurrentProcess(), target, 1)) {
        OSTP_LOG_WARN("WriteExecutableByte(target={}) FlushInstructionCache failed (error={})",
                      target, GetLastError());
        return false;
    }
    return true;
}

} // namespace OSTPlatform::Memory
