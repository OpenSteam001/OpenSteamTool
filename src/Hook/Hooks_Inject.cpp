#include "Hooks_Inject.h"
#include "HookMacros.h"
#include "Utils/RemoteInject.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace {

    // Lowercased exe basename → real AppId: filled by QueueInjection (from the
    // SpawnProcess hook), consumed by the CreateProcessW hook. Keyed by basename
    // because Steam runs the real spawn on a different thread than the hook that
    // sees the launch, so there's nothing else to correlate the two by.
    std::mutex g_pendingMutex;
    std::unordered_map<std::wstring, AppId_t> g_pending;

    std::wstring LowerBasename(LPCWSTR path) {
        if (!path || !*path) return {};
        std::wstring name = std::filesystem::path(path).filename().wstring();
        std::transform(name.begin(), name.end(), name.begin(),
            [](wchar_t c){ return static_cast<wchar_t>(towlower(c)); });
        return name;
    }

    // lpApplicationName is often null and the exe is the first token of
    // lpCommandLine instead. Peel it off (quoted or unquoted) so we can
    // still derive a basename in that case.
    std::wstring ExeFromCmd(LPCWSTR cmd) {
        if (!cmd) return {};
        while (*cmd == L' ' || *cmd == L'\t') ++cmd;
        std::wstring out;
        if (*cmd == L'"') {
            for (++cmd; *cmd && *cmd != L'"'; ++cmd) out.push_back(*cmd);
        } else {
            for (; *cmd && *cmd != L' ' && *cmd != L'\t'; ++cmd) out.push_back(*cmd);
        }
        return out;
    }

    AppId_t ClaimPending(LPCWSTR app, LPCWSTR cmd) {
        std::wstring key = LowerBasename(app);
        if (key.empty()) key = LowerBasename(ExeFromCmd(cmd).c_str());
        if (key.empty()) return 0;

        std::lock_guard lk(g_pendingMutex);
        auto it = g_pending.find(key);
        if (it == g_pending.end()) return 0;
        AppId_t id = it->second;
        g_pending.erase(it);
        return id;
    }

    // ── CreateProcessW / CreateProcessAsUserW hooks ────────────────────
    using CreateProcessW_t = BOOL(WINAPI*)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
        LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR,
        LPSTARTUPINFOW, LPPROCESS_INFORMATION);
    using CreateProcessAsUserW_t = BOOL(WINAPI*)(HANDLE, LPCWSTR, LPWSTR,
        LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
        LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);

    CreateProcessW_t       oCreateProcessW       = nullptr;
    CreateProcessAsUserW_t oCreateProcessAsUserW = nullptr;

    BOOL Spawn(HANDLE token, LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
               LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env,
               LPCWSTR cwd, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi)
    {
        auto fwd = [&](DWORD f) {
            return token
                ? oCreateProcessAsUserW(token, app, cmd, pa, ta, inherit, f, env, cwd, si, pi)
                : oCreateProcessW(app, cmd, pa, ta, inherit, f, env, cwd, si, pi);
        };

        AppId_t appId = ClaimPending(app, cmd);
        if (!appId || PayloadPath[0] == 0) return fwd(flags);

        // Start suspended so the payload loads before the game's first
        // instruction, then resume unless the caller already wanted it held.
        BOOL ok = fwd(flags | CREATE_SUSPENDED);
        if (!ok) {
            LOG_INJECT_WARN("appid={} spawn failed err={}", appId, GetLastError());
            return ok;
        }

        wchar_t wpayload[MAX_PATH] = {};
        MultiByteToWideChar(CP_ACP, 0, PayloadPath, -1, wpayload, MAX_PATH);
        const bool injected = RemoteInject::LoadDll(pi->hProcess, wpayload);
        LOG_INJECT_INFO("appid={} pid={} payload {}", appId, pi->dwProcessId,
                        injected ? "loaded" : "FAILED");

        // Injection failure must never bring the game down with us.
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

#ifdef OPENSTEAMTOOL_LOGGING_ENABLED
    // Each injected game process writes its own <pid>.log here. Wipe the
    // folder once per Steam session so the files don't pile up forever.
    void ResetPayloadLogs() {
        std::error_code ec;
        auto dir = std::filesystem::path(Config::logDir) / "payload";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
    }
#endif
}

namespace Hooks_Inject {
    void Install() {
        if (!Config::injectEnabled) {
            LOG_INJECT_INFO("payload injection disabled via config");
            return;
        }
#ifdef OPENSTEAMTOOL_LOGGING_ENABLED
        ResetPayloadLogs();
#endif
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (!k32) return;
        oCreateProcessW       = reinterpret_cast<CreateProcessW_t>      (GetProcAddress(k32, "CreateProcessW"));
        oCreateProcessAsUserW = reinterpret_cast<CreateProcessAsUserW_t>(GetProcAddress(k32, "CreateProcessAsUserW"));

        HOOK_BEGIN();
        if (oCreateProcessW)
            DetourAttach(reinterpret_cast<PVOID*>(&oCreateProcessW),
                         reinterpret_cast<PVOID>(hkCreateProcessW));
        if (oCreateProcessAsUserW)
            DetourAttach(reinterpret_cast<PVOID*>(&oCreateProcessAsUserW),
                         reinterpret_cast<PVOID>(hkCreateProcessAsUserW));
        HOOK_END();
        LOG_INJECT_INFO("spawn hooks installed dll=\"{}\"", PayloadPath);
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        if (oCreateProcessW) {
            DetourDetach(reinterpret_cast<PVOID*>(&oCreateProcessW),
                         reinterpret_cast<PVOID>(hkCreateProcessW));
            oCreateProcessW = nullptr;
        }
        if (oCreateProcessAsUserW) {
            DetourDetach(reinterpret_cast<PVOID*>(&oCreateProcessAsUserW),
                         reinterpret_cast<PVOID>(hkCreateProcessAsUserW));
            oCreateProcessAsUserW = nullptr;
        }
        UNHOOK_END();

        std::lock_guard lk(g_pendingMutex);
        g_pending.clear();
    }

    void QueueInjection(const char* exePath, AppId_t realAppId) {
        if (!Config::injectEnabled || !realAppId || !exePath || !*exePath) return;

        wchar_t wexe[MAX_PATH] = {};
        MultiByteToWideChar(CP_UTF8, 0, exePath, -1, wexe, MAX_PATH);
        std::wstring key = LowerBasename(wexe);
        if (key.empty()) return;

        std::lock_guard lk(g_pendingMutex);
        g_pending[key] = realAppId;
    }
}
