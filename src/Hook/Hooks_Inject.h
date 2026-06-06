#pragma once

#include "dllmain.h"

namespace Hooks_Inject {
    void Install();
    void Uninstall();

    void QueueInjection(const char* exePath, AppId_t realAppId);
}
