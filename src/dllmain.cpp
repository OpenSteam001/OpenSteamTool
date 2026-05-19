#include "dllmain.h"
#include "Hook/HookManager.h"
#include "Utils/FileWatcher.h"

// Load diversion.dll and prepare key runtime paths.
bool LoadDiversion()
{
    if (!GetCurrentDirectoryA(MAX_PATH, SteamInstallPath)) {
        return false;
    }
    sprintf_s(SteamclientPath, MAX_PATH, "%s\\steamclient64.dll",  SteamInstallPath);
    sprintf_s(DiversionPath,   MAX_PATH, "%s\\bin\\diversion.dll", SteamInstallPath);
    sprintf_s(LuaDir,          MAX_PATH, "%s\\config\\lua",        SteamInstallPath);
    sprintf_s(ConfigPath,      MAX_PATH, "%s\\opensteamtool.toml", SteamInstallPath);
    // ensure bin\ directory exists before copying
    char binDir[MAX_PATH];
    sprintf_s(binDir, MAX_PATH, "%s\\bin", SteamInstallPath);
    CreateDirectoryA(binDir, nullptr);  // no-op if already exists
    if (!CopyFileA(SteamclientPath, DiversionPath, FALSE)) {
        LOG_ERROR("CopyFileA failed: {} -> {} (err={})",
                  SteamclientPath, DiversionPath, GetLastError());
        return false;
    }
    diversion_hMdoule = LoadLibraryA(DiversionPath);
    if (!diversion_hMdoule) {
        LOG_ERROR("LoadLibraryA failed: {} (err={})", DiversionPath, GetLastError());
        return false;
    }
    LOG_INFO("Loaded diversion.dll from {}", DiversionPath);
    return true;
}

// Resolve the current Steam build id from steam.exe!GetBootstrapperVersion
// once at startup. ByteSearch uses this string as the preferred-match
// label so it picks the Sigs[] entry built for the running build before
// falling back to try-all order (see Utils/ByteSearch.cpp).
static void DetectSteamBuildId() {
    using GetBootstrapperVersion_t = int64_t (*)();
    HMODULE hSteam = GetModuleHandleA("steam.exe");
    if (!hSteam) {
        LOG_WARN("SteamVersion: steam.exe module not loaded; build id unavailable");
        return;
    }
    auto fn = reinterpret_cast<GetBootstrapperVersion_t>(
        GetProcAddress(hSteam, "GetBootstrapperVersion"));
    if (!fn) {
        LOG_WARN("SteamVersion: steam.exe!GetBootstrapperVersion not exported; "
                 "ByteSearch will fall back to try-all order");
        return;
    }
    g_steamBuildId = std::to_string(fn());
    LOG_INFO("SteamVersion: build id = {}", g_steamBuildId);
}

// All initialisation that touches the filesystem, calls LoadLibrary, scans
// memory, or installs detours runs here on a worker thread — we MUST NOT do
// any of that from inside DllMain (loader lock).
static DWORD WINAPI InitThread(LPVOID param) {
    HMODULE selfModule = static_cast<HMODULE>(param);
    Log::Init(selfModule);
    LOG_INFO("OpenSteamTool init thread started");

    DetectSteamBuildId();

    if (!LoadDiversion()) {
        LOG_ERROR("LoadDiversion failed");
        return 1;
    }

    Config::Load(ConfigPath);
    Log::InitModules();

    std::vector<std::string> watchDirs = Config::luaPaths;
    watchDirs.push_back(std::string(LuaDir));
    for (const auto& dir : watchDirs)
        LuaConfig::ParseDirectory(dir);

    FileWatcher::Start(watchDirs);

    SteamUI::CoreHook();
    SteamClient::CoreHook();
    g_HooksInstalled.store(true);
    LOG_INFO("OpenSteamTool init complete");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, PVOID pvReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        // Hand off all real work to a worker thread to avoid running file I/O,
        // LoadLibrary, and detour transactions under the loader lock.
        HANDLE h = CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);
        if (h) CloseHandle(h);
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        FileWatcher::Stop();
        SteamUI::CoreUnhook();
        SteamClient::CoreUnhook();
    }

    return TRUE;
}
