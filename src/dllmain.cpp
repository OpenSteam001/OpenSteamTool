#include "dllmain.h"
#include "Hook/HookManager.h"
#include "Utils/FileWatcher.h"
#include "Utils/IPCLoader.h"
#include "Utils/PatternLoader.h"
#include "Utils/SteamDiagnostics.h"

// prepare key runtime paths.
bool InitializeSteamComponents()
{
    if (!GetCurrentDirectoryA(MAX_PATH, SteamInstallPath)) {
        return false;
    }
    sprintf_s(SteamclientPath, MAX_PATH, "%s\\steamclient64.dll",  SteamInstallPath);
    sprintf_s(SteamUIPath,     MAX_PATH, "%s\\steamui.dll",        SteamInstallPath);
    sprintf_s(DiversionPath,   MAX_PATH, "%s\\bin\\diversion.dll", SteamInstallPath);
    sprintf_s(LuaDir,          MAX_PATH, "%s\\config\\lua",        SteamInstallPath);
    sprintf_s(ConfigPath,      MAX_PATH, "%s\\opensteamtool.toml", SteamInstallPath);
    
    client_hModule = LoadLibraryA(SteamclientPath);
    if (!client_hModule) {
        LOG_ERROR("LoadLibraryA failed: {} (err={})", SteamclientPath, GetLastError());
        return false;
    }
    LOG_INFO("Loaded diversion.dll from {}", SteamclientPath);
    
    ui_hModule = GetModuleHandleA("steamui.dll");
    if(!ui_hModule) {
        LOG_ERROR("GetModuleHandleA failed for steamui.dll: err={}", GetLastError());
        return false;
    }
    return true;
}

// All initialisation that touches the filesystem, calls LoadLibrary, scans
// memory, or installs detours runs here on a worker thread — we MUST NOT do
// any of that from inside DllMain (loader lock).
static DWORD WINAPI InitThread(LPVOID param) {
    HMODULE selfModule = static_cast<HMODULE>(param);
    Log::Init(selfModule);
    LOG_INFO("OpenSteamTool init thread started");

    if (!InitializeSteamComponents()) {
        LOG_ERROR("InitializeSteamComponents failed");
        return 1;
    }

    Config::Load(ConfigPath);
    Log::InitModules();
    SteamDiagnostics::Initialize(SteamclientPath, SteamUIPath);

    // Load pattern files for steamclient64.dll and steamui.dll.
    // Each call computes the SHA-256 of the DLL on disk, checks the local
    // cache, and downloads from GitHub if needed.  Both calls are synchronous
    // but run on this worker thread, never under the loader lock.
    PatternLoader::Load(ui_hModule, SteamUIPath, "steamui");
    PatternLoader::Load(client_hModule, SteamclientPath, "steamclient");

    // IPC method metadata (funcHash, fencepost, argc, ...)
    IPCLoader::Load(SteamclientPath);

    std::vector<std::string> watchDirs = Config::luaPaths;
    watchDirs.push_back(std::string(LuaDir));
    for (const auto& dir : watchDirs)
        LuaConfig::ParseDirectory(dir);

    FileWatcher::Start(watchDirs);

    SteamUI::CoreHook();
    SteamClient::CoreHook();

    // Surface any functions that FindPattern() could not locate.
    PatternLoader::ReportMissingFunctions();

    LOG_INFO("OpenSteamTool init complete");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, PVOID pvReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        // Keep this module pinned so explicit FreeLibrary cannot unload code
        // while hooks and worker threads may still reference it.
        HMODULE pinnedModule = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCSTR>(&DllMain), &pinnedModule);
        // Hand off all real work to a worker thread to avoid running file I/O,
        // LoadLibrary, and detour transactions under the loader lock.
        HANDLE h = CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);
        if (h) CloseHandle(h);
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        // During process termination, join watcher thread object so CRT static
        // teardown does not hit std::thread's joinable-guard terminate.
        if (pvReserved != nullptr) {
            FileWatcher::Stop();
        }
    }

    return TRUE;
}
