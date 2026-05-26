#ifndef HOOKMANAGER_H
#define HOOKMANAGER_H

#include "dllmain.h"

namespace SteamUI {
    void CoreHook();
    void CoreUnhook();
}

namespace SteamClient {
    void CoreHook();
    void CoreUnhook();
}


#endif // HOOKMANAGER_H
