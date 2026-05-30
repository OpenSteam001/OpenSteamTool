#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

namespace Utils {
    inline std::filesystem::path GetDllDirectory() {
        char dllPath[MAX_PATH];
        GetModuleFileNameA(g_hSelfModule, dllPath, MAX_PATH);
        return std::filesystem::path(dllPath).parent_path();
    }
}