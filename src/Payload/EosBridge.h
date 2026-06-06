#pragma once

#include <windows.h>

// Patches EOSSDK-Win64-Shipping.dll so EOS multiplayer works.
namespace EosBridge {
    void InstallOn(HMODULE eosModule); // InstallOn more than once is safe.
}
