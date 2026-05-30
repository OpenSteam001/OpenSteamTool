#include "DllDirectory.h"
#include <windows.h>

namespace Utils {

    std::filesystem::path GetDllDirectory() {
        HMODULE hSelf = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetDllDirectory),
            &hSelf
        );
        char dllPath[MAX_PATH] = { 0 };
        GetModuleFileNameA(hSelf, dllPath, MAX_PATH);
        return std::filesystem::path(dllPath).parent_path();
    }

}