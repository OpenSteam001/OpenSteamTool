#pragma once

#include <windows.h>

namespace RemoteInject {

    // Load a DLL into another process by running kernel32!LoadLibraryW on a
    // remote thread inside it. Windows loads kernel32 at the same address in
    // every process until the next reboot, so a LoadLibraryW pointer resolved
    // here is valid in the target. Header-only because the host DLL and the
    // payload run in separate processes and can't share compiled code.
    inline bool LoadDll(HANDLE proc, LPCWSTR dllPath) {
        auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        if (!loadLib) return false;

        const SIZE_T bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
        void* mem = VirtualAllocEx(proc, nullptr, bytes,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!mem) return false;

        bool ok = false;
        if (WriteProcessMemory(proc, mem, dllPath, bytes, nullptr)) {
            if (HANDLE t = CreateRemoteThread(proc, nullptr, 0, loadLib, mem, 0, nullptr)) {
                ok = (WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
                CloseHandle(t);
            }
        }
        VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
        return ok;
    }

} // namespace RemoteInject
