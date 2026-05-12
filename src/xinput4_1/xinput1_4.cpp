// xinput1_4.dll HiJack Project - True Dynamic Wrapper (With Undocumented Ordinals)
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <detours.h>
#include <string>

// ─── 1. Real XInput Function Pointers ───────────────────────────
static HMODULE g_hRealXInput = nullptr;

// Standard
typedef DWORD(WINAPI* XInputGetState_t)(DWORD, void*);
typedef DWORD(WINAPI* XInputSetState_t)(DWORD, void*);
typedef DWORD(WINAPI* XInputGetCapabilities_t)(DWORD, DWORD, void*);
typedef void(WINAPI* XInputEnable_t)(BOOL);
typedef DWORD(WINAPI* XInputGetAudioDeviceIds_t)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);
typedef DWORD(WINAPI* XInputGetBatteryInformation_t)(DWORD, BYTE, void*);
typedef DWORD(WINAPI* XInputGetKeystroke_t)(DWORD, DWORD, void*);

static XInputGetState_t o_XInputGetState = nullptr;
static XInputSetState_t o_XInputSetState = nullptr;
static XInputGetCapabilities_t o_XInputGetCapabilities = nullptr;
static XInputEnable_t o_XInputEnable = nullptr;
static XInputGetAudioDeviceIds_t o_XInputGetAudioDeviceIds = nullptr;
static XInputGetBatteryInformation_t o_XInputGetBatteryInformation = nullptr;
static XInputGetKeystroke_t o_XInputGetKeystroke = nullptr;

// Undocumented Ordinals (Required for Steam Big Picture / Guide Button)
static FARPROC o_100 = nullptr; // XInputGetStateEx
static FARPROC o_101 = nullptr; // XInputWaitForGuideButton
static FARPROC o_102 = nullptr; // XInputCancelGuideButtonWait
static FARPROC o_103 = nullptr; // XInputPowerOffController
static FARPROC o_104 = nullptr; // XInputGetBaseBusInformation
static FARPROC o_108 = nullptr; // XInputGetAudioDeviceIdsEx

// ─── 2. Core Initialization (Binding to the real System32 file) ───
void LoadRealXInput() {
    if (g_hRealXInput) return;

    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);
    std::string realPath = std::string(sysDir) + "\\xinput1_4.dll";

    g_hRealXInput = LoadLibraryA(realPath.c_str());
    if (g_hRealXInput) {
        // Load Standard API
        o_XInputGetState = (XInputGetState_t)GetProcAddress(g_hRealXInput, "XInputGetState");
        o_XInputSetState = (XInputSetState_t)GetProcAddress(g_hRealXInput, "XInputSetState");
        o_XInputGetCapabilities = (XInputGetCapabilities_t)GetProcAddress(g_hRealXInput, "XInputGetCapabilities");
        o_XInputEnable = (XInputEnable_t)GetProcAddress(g_hRealXInput, "XInputEnable");
        o_XInputGetAudioDeviceIds = (XInputGetAudioDeviceIds_t)GetProcAddress(g_hRealXInput, "XInputGetAudioDeviceIds");
        o_XInputGetBatteryInformation = (XInputGetBatteryInformation_t)GetProcAddress(g_hRealXInput, "XInputGetBatteryInformation");
        o_XInputGetKeystroke = (XInputGetKeystroke_t)GetProcAddress(g_hRealXInput, "XInputGetKeystroke");

        // Load Undocumented Ordinals
        o_100 = GetProcAddress(g_hRealXInput, (LPCSTR)100);
        o_101 = GetProcAddress(g_hRealXInput, (LPCSTR)101);
        o_102 = GetProcAddress(g_hRealXInput, (LPCSTR)102);
        o_103 = GetProcAddress(g_hRealXInput, (LPCSTR)103);
        o_104 = GetProcAddress(g_hRealXInput, (LPCSTR)104);
        o_108 = GetProcAddress(g_hRealXInput, (LPCSTR)108);
    }
}

// ─── 3. Native Exports (Safely passing data to the game) ──────────
extern "C" {
    // Standard Functions
    DWORD WINAPI XInputGetState(DWORD dwUserIndex, void* pState) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_XInputGetState ? o_XInputGetState(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
    }
    DWORD WINAPI XInputSetState(DWORD dwUserIndex, void* pVibration) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_XInputSetState ? o_XInputSetState(dwUserIndex, pVibration) : ERROR_DEVICE_NOT_CONNECTED;
    }
    DWORD WINAPI XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, void* pCapabilities) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_XInputGetCapabilities ? o_XInputGetCapabilities(dwUserIndex, dwFlags, pCapabilities) : ERROR_DEVICE_NOT_CONNECTED;
    }
    void WINAPI XInputEnable(BOOL enable) {
        if (!g_hRealXInput) LoadRealXInput();
        if (o_XInputEnable) o_XInputEnable(enable);
    }
    DWORD WINAPI XInputGetAudioDeviceIds(DWORD dwUserIndex, LPWSTR pRenderDeviceId, UINT* pRenderCount, LPWSTR pCaptureDeviceId, UINT* pCaptureCount) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_XInputGetAudioDeviceIds ? o_XInputGetAudioDeviceIds(dwUserIndex, pRenderDeviceId, pRenderCount, pCaptureDeviceId, pCaptureCount) : ERROR_DEVICE_NOT_CONNECTED;
    }
    DWORD WINAPI XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType, void* pBatteryInformation) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_XInputGetBatteryInformation ? o_XInputGetBatteryInformation(dwUserIndex, devType, pBatteryInformation) : ERROR_DEVICE_NOT_CONNECTED;
    }
    DWORD WINAPI XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, void* pKeystroke) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_XInputGetKeystroke ? o_XInputGetKeystroke(dwUserIndex, dwReserved, pKeystroke) : ERROR_DEVICE_NOT_CONNECTED;
    }

    // Undocumented Ordinal Wrappers
    DWORD WINAPI XInputOrdinal100(DWORD a1, void* a2) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_100 ? ((DWORD(WINAPI*)(DWORD, void*))o_100)(a1, a2) : ERROR_DEVICE_NOT_CONNECTED;
    }
    DWORD WINAPI XInputOrdinal101(DWORD a1, DWORD a2, void* a3) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_101 ? ((DWORD(WINAPI*)(DWORD, DWORD, void*))o_101)(a1, a2, a3) : ERROR_DEVICE_NOT_CONNECTED;
    }
    DWORD WINAPI XInputOrdinal102(DWORD a1) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_102 ? ((DWORD(WINAPI*)(DWORD))o_102)(a1) : ERROR_DEVICE_NOT_CONNECTED;
    }
    DWORD WINAPI XInputOrdinal103(DWORD a1) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_103 ? ((DWORD(WINAPI*)(DWORD))o_103)(a1) : ERROR_DEVICE_NOT_CONNECTED;
    }
    DWORD WINAPI XInputOrdinal104(DWORD a1, void* a2) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_104 ? ((DWORD(WINAPI*)(DWORD, void*))o_104)(a1, a2) : ERROR_DEVICE_NOT_CONNECTED;
    }
    DWORD WINAPI XInputOrdinal108(DWORD a1, void* a2, void* a3, void* a4, void* a5) {
        if (!g_hRealXInput) LoadRealXInput();
        return o_108 ? ((DWORD(WINAPI*)(DWORD, void*, void*, void*, void*))o_108)(a1, a2, a3, a4, a5) : ERROR_DEVICE_NOT_CONNECTED;
    }
}

// ─── 4. Original Developer's Detours & Injection Logic ────────────
static HMODULE g_OldModule = nullptr;

typedef HMODULE(WINAPI* LoadLibraryExW_t)(LPCWSTR, HANDLE, DWORD);
static LoadLibraryExW_t oLoadLibraryExW = nullptr;

HMODULE WINAPI hkLoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    if (lpLibFileName) {
        if (g_OldModule && _wcsicmp(lpLibFileName, L"xinput1_4.dll") == 0) {
            return g_OldModule;
        }
    }
    return oLoadLibraryExW(lpLibFileName, hFile, dwFlags);
}

static void InstallHook() {
    oLoadLibraryExW = reinterpret_cast<LoadLibraryExW_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryExW"));
    if (!oLoadLibraryExW) return;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(&oLoadLibraryExW), reinterpret_cast<PVOID>(hkLoadLibraryExW));
    DetourTransactionCommit();
}

static void UninstallHook() {
    if (!oLoadLibraryExW) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID*>(&oLoadLibraryExW), reinterpret_cast<PVOID>(hkLoadLibraryExW));
    DetourTransactionCommit();
    oLoadLibraryExW = nullptr;
}

BOOL OpenSteamToolLoad() {
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        const char* exeName = strrchr(exePath, '\\');
        exeName = exeName ? exeName + 1 : exePath;
        if (_stricmp(exeName, "steam.exe") != 0) return TRUE;
    }
    return LoadLibraryA("OpenSteamTool.dll") != NULL;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, PVOID pvReserved) {
    switch (dwReason) {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        g_OldModule = hModule;

        HMODULE pinned = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&hkLoadLibraryExW),
            &pinned);

        LoadRealXInput();
        InstallHook();

        if (!OpenSteamToolLoad()) return FALSE;
        break;
    }
    case DLL_PROCESS_DETACH:
        UninstallHook();
        break;
    }
    return TRUE;
}