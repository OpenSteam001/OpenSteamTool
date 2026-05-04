// xinput1_4.dll HiJack Project

#include <windows.h>
#include <cstring>

#pragma comment(linker, "/EXPORT:DllMain=XINPUT1_4.DllMain,@1")
#pragma comment(linker, "/EXPORT:XInputEnable=XINPUT1_4.XInputEnable,@5")
#pragma comment(linker, "/EXPORT:XInputGetAudioDeviceIds=XINPUT1_4.XInputGetAudioDeviceIds,@10")
#pragma comment(linker, "/EXPORT:XInputGetBatteryInformation=XINPUT1_4.XInputGetBatteryInformation,@7")
#pragma comment(linker, "/EXPORT:XInputGetCapabilities=XINPUT1_4.XInputGetCapabilities,@4")
#pragma comment(linker, "/EXPORT:XInputGetKeystroke=XINPUT1_4.XInputGetKeystroke,@8")
#pragma comment(linker, "/EXPORT:XInputGetState=XINPUT1_4.XInputGetState,@2")
#pragma comment(linker, "/EXPORT:XInputSetState=XINPUT1_4.XInputSetState,@3")
#pragma comment(linker, "/EXPORT:#100=XINPUT1_4.#100,@100,NONAME")
#pragma comment(linker, "/EXPORT:#101=XINPUT1_4.#101,@101,NONAME")
#pragma comment(linker, "/EXPORT:#102=XINPUT1_4.#102,@102,NONAME")
#pragma comment(linker, "/EXPORT:#103=XINPUT1_4.#103,@103,NONAME")
#pragma comment(linker, "/EXPORT:#104=XINPUT1_4.#104,@104,NONAME")
#pragma comment(linker, "/EXPORT:#108=XINPUT1_4.#108,@108,NONAME")

// Only inject when the host process is steam.exe (case-insensitive).
// LoadLibraryA itself guarantees that OpenSteamTool.dll's DllMain
// runs at most once per process, so multiple hijack DLLs can safely
// call this without additional synchronisation.
BOOL OpenSteamToolLoad()
{
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH))
    {
        const char* exeName = strrchr(exePath, '\\');
        exeName = exeName ? exeName + 1 : exePath;
        if (_stricmp(exeName, "steam.exe") != 0)
            return TRUE;   // not Steam — let the proxy load, but don't inject
    }
    return LoadLibraryA("OpenSteamTool.dll") != NULL;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, PVOID pvReserved)
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hModule);
            if ( !OpenSteamToolLoad() )
                return FALSE;
            break;
        }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
