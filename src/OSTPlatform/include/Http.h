#pragma once

#include <cstdint>
#include <string>

namespace OSTPlatform::Http {

    struct Result {
        std::string body;
        uint32_t status = 0;
        // True once WinHTTP produced a response; callers must still check status for 2xx.
        bool ok = false;
    };

    Result Execute(const wchar_t* method,
                   const char* url,
                   const void* reqBody = nullptr,
                   uint32_t reqBodyLen = 0,
                   const wchar_t* headers = nullptr,
                   uint32_t timeoutResolve = 5000,
                   uint32_t timeoutConnect = 5000,
                   uint32_t timeoutSend = 10000,
                   uint32_t timeoutRecv = 10000,
                   // Upper bound on the response body read into memory. The default
                   // suits small metadata (TOML/JSON); callers fetching larger payloads
                   // (e.g. a DLL) must raise it. Reading stops once the cap is reached.
                   uint32_t maxBodyBytes = 256 * 1024);

} // namespace OSTPlatform::Http
