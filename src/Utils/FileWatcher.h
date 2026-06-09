#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace FileWatcher {
    void Start(const std::vector<std::string>& directories);
    void Stop();
}