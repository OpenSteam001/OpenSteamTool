#pragma once

#include "Steam/Types.h"

#include <cstdint>

namespace StatsClient {

    bool FetchStatSteamId(AppId_t appId, uint64_t* outSteamId);

} // namespace StatsClient
