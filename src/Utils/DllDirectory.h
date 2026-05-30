#pragma once
#include <filesystem>

namespace Utils {
    // Returns the directory where the current DLL is located.
    std::filesystem::path GetDllDirectory();
}