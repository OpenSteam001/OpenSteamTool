// The real game is often launched by a launcher as a separate process, and the
// EOS SDK loads in that child rather than the launcher. The payload has to follow the
// process tree down: hooking into all process-creation calls in its own host process and
// loading the same DLL into each child before that child starts running.

#include "SelfPropagate.h"
#include "PayloadLog.h"
#include "Utils/RemoteInject.h"

#include <detours.h>

namespace {
    // Full path to this payload DLL, so children load the exact same file.
    wchar_t g_selfPath[MAX_PATH] = {};

    using CreateProcessW_t = BOOL(WINAPI*)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
        LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR,
        LPSTARTUPINFOW, LPPROCESS_INFORMATION);
    using CreateProcessAsUserW_t = BOOL(WINAPI*)(HANDLE, LPCWSTR, LPWSTR,
        LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
        LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);

    CreateProcessW_t       oCreateProcessW       = nullptr;
    CreateProcessAsUserW_t oCreateProcessAsUserW = nullptr;

    // CreateProcessW and CreateProcessAsUserW are separate spawn entry points,
    // so both are hooked and funneled here. token is null on the plain path while
    // the user token is on the AsUser path; every other argument passes through.
    BOOL Spawn(HANDLE token, LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
               LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env,
               LPCWSTR cwd, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi)
    {
        // Create the child suspended so we can load the payload before its
        // first instruction runs.
        const DWORD spawnFlags = flags | CREATE_SUSPENDED;
        BOOL ok = token
            ? oCreateProcessAsUserW(token, app, cmd, pa, ta, inherit, spawnFlags, env, cwd, si, pi)
            : oCreateProcessW(app, cmd, pa, ta, inherit, spawnFlags, env, cwd, si, pi);
        if (!ok) return ok;

        const bool injected = RemoteInject::LoadDll(pi->hProcess, g_selfPath);
        PayloadLog::Write("propagate pid=" + std::to_string(pi->dwProcessId) +
                          (injected ? " ok" : " FAILED"));

        // We forced the suspend; let the child run unless the caller had
        // already asked for it.
        if (!(flags & CREATE_SUSPENDED)) ResumeThread(pi->hThread);
        return ok;
    }

    BOOL WINAPI hkCreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
        LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env,
        LPCWSTR cwd, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi)
    {
        return Spawn(nullptr, app, cmd, pa, ta, inherit, flags, env, cwd, si, pi);
    }

    BOOL WINAPI hkCreateProcessAsUserW(HANDLE token, LPCWSTR app, LPWSTR cmd,
        LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags,
        LPVOID env, LPCWSTR cwd, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi)
    {
        return Spawn(token, app, cmd, pa, ta, inherit, flags, env, cwd, si, pi);
    }
}

namespace SelfPropagate {
    void Install(HMODULE hSelf) {
        // Remember our own path; children load this exact DLL.
        if (!GetModuleFileNameW(hSelf, g_selfPath, MAX_PATH)) return;
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (!k32) return;
        oCreateProcessW       = reinterpret_cast<CreateProcessW_t>      (GetProcAddress(k32, "CreateProcessW"));
        oCreateProcessAsUserW = reinterpret_cast<CreateProcessAsUserW_t>(GetProcAddress(k32, "CreateProcessAsUserW"));

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (oCreateProcessW)
            DetourAttach(reinterpret_cast<PVOID*>(&oCreateProcessW),
                         reinterpret_cast<PVOID>(hkCreateProcessW));
        if (oCreateProcessAsUserW)
            DetourAttach(reinterpret_cast<PVOID*>(&oCreateProcessAsUserW),
                         reinterpret_cast<PVOID>(hkCreateProcessAsUserW));
        DetourTransactionCommit();
    }
}
