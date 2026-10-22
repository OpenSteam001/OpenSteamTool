#pragma once
#include <functional>
#include <string>

namespace RemoteToml {

    struct Request {
        std::string channel;    // "pattern" or "ipc"
        std::string component;  // "steamclient" or "steamui"
        std::string dllPath;
    };

    struct Result {
        bool        ok        = false;
        bool        fromCache = false;
        std::string body;
        std::string sha256;
    };

    // Use the exact local cache entry when the loader accepts it; otherwise fetch remote.
    // Only accepted remote content is written to the cache.
    Result Fetch(const Request& request,
                 const std::function<bool(const std::string&)>& load);

} // namespace RemoteToml
