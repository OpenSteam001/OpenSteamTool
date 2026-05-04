#pragma once

#include <string>
#include <windows.h>
#include <winhttp.h>

namespace WinHttp {

    struct ParsedUrl {
        std::wstring host;
        std::wstring path;
        INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
        bool tls = false;
        bool valid = false;
    };

    ParsedUrl ParseUrl(const char* rawUrl);

    struct Result {
        std::string body;
        DWORD status = 0;
        bool ok = false;
    };

    // Perform an HTTP request.
    // method:         L"GET" or L"POST"
    // url:            full URL (http:// or https://)
    // reqBody:        raw POST body (nullptr for GET)
    // reqBodyLen:     byte length of reqBody
    // headers:        nullptr or wstring L"Key: Val\r\n..."
    // timeoutResolve: DNS timeout in ms (default 5000)
    // timeoutConnect: TCP connect timeout in ms (default 5000)
    // timeoutSend:    send timeout in ms (default 10000)
    // timeoutRecv:    receive timeout in ms (default 10000)
    Result Execute(LPCWSTR method, const char* url,
                   const void* reqBody, DWORD reqBodyLen,
                   const wchar_t* headers,
                   DWORD timeoutResolve = 5000,
                   DWORD timeoutConnect = 5000,
                   DWORD timeoutSend    = 10000,
                   DWORD timeoutRecv    = 10000);

    // Reuse caller-owned hSession + hConnect handles to skip DNS/TCP/TLS
    // setup on repeated requests to the same host.  The caller is
    // responsible for the lifetime of both handles.
    Result ExecuteEx(HINTERNET hSession, HINTERNET hConnect, bool tls,
                     LPCWSTR method, const wchar_t* path,
                     const void* reqBody, DWORD reqBodyLen,
                     const wchar_t* headers,
                     const char* urlForLog = "");

}
