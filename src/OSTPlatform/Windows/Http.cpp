#include "include/Http.h"

#include "include/Encoding.h"
#include "include/Log.h"
#include "include/Numbers.h"

#include <windows.h>
#include <winhttp.h>

#include <chrono>
#include <format>
#include <string>

namespace OSTPlatform::Http {
namespace {

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
    bool tls = false;
    bool valid = false;
};

ParsedUrl ParseUrl(const char* rawUrl) {
    ParsedUrl out;
    if (!rawUrl) return out;

    std::string url(rawUrl);
    if (url.starts_with("https://")) {
        out.tls = true;
        out.port = INTERNET_DEFAULT_HTTPS_PORT;
        url = url.substr(8);
    } else if (url.starts_with("http://")) {
        url = url.substr(7);
    } else {
        return out;
    }

    const size_t slash = url.find('/');
    const std::string hostPart = url.substr(0, slash);
    out.path = (slash != std::string::npos)
        ? L"/" + std::wstring(url.begin() + slash + 1, url.end())
        : L"/";

    const size_t colon = hostPart.find(':');
    if (colon != std::string::npos) {
        out.host = std::wstring(hostPart.begin(), hostPart.begin() + colon);
        const auto port = Numbers::ParseUInt32(hostPart, {colon + 1});
        if (!port || *port == 0 || *port > 65535) return out;
        out.port = static_cast<INTERNET_PORT>(*port);
    } else {
        out.host = std::wstring(hostPart.begin(), hostPart.end());
    }

    out.valid = !out.host.empty();
    return out;
}

} // namespace

Result Execute(const wchar_t* method,
               const char* url,
               const void* reqBody,
               uint32_t reqBodyLen,
               const wchar_t* headers,
               uint32_t timeoutResolve,
               uint32_t timeoutConnect,
               uint32_t timeoutSend,
               uint32_t timeoutRecv,
               uint32_t maxBodyBytes) {
    Result r;

    ParsedUrl pu = ParseUrl(url);
    if (!pu.valid) {
        OSTP_LOG_WARN("Invalid URL: {}", url ? url : "");
        return r;
    }

    auto t0 = std::chrono::steady_clock::now();

    HINTERNET hSession = WinHttpOpen(L"OpenSteamTool/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) {
        OSTP_LOG_WARN("{} - WinHttpOpen failed (error={})", url ? url : "", GetLastError());
        return r;
    }

    WinHttpSetTimeouts(hSession, timeoutResolve, timeoutConnect, timeoutSend, timeoutRecv);

    HINTERNET hConnect = WinHttpConnect(hSession, pu.host.c_str(), pu.port, 0);
    if (!hConnect) {
        OSTP_LOG_WARN("{} - WinHttpConnect(host='{}', port={}) failed (error={})",
                      url ? url : "",
                      Encoding::WideToUtf8(pu.host),
                      pu.port,
                      GetLastError());
        WinHttpCloseHandle(hSession);
        return r;
    }

    const DWORD flags = pu.tls ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        method,
        pu.path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!hRequest) {
        OSTP_LOG_WARN("{} - WinHttpOpenRequest failed (error={})", url ? url : "", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return r;
    }

    if (headers && headers[0]) {
        if (!WinHttpAddRequestHeaders(
            hRequest,
            headers,
            static_cast<DWORD>(wcslen(headers)),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
            OSTP_LOG_WARN("{} - WinHttpAddRequestHeaders failed (error={})", url ? url : "", GetLastError());
        }
    }

    const DWORD totalLen = reqBodyLen;
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            const_cast<void*>(reqBody), reqBodyLen, totalLen, 0)) {
        OSTP_LOG_WARN("{} - WinHttpSendRequest failed (error={})", url, GetLastError());
    } else if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        OSTP_LOG_WARN("{} - WinHttpReceiveResponse failed (error={})", url, GetLastError());
    } else {
        DWORD sz = sizeof(r.status);
        if (!WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &r.status,
            &sz,
            WINHTTP_NO_HEADER_INDEX)) {
            OSTP_LOG_WARN("{} - WinHttpQueryHeaders(status) failed (error={})", url ? url : "", GetLastError());
        }

        DWORD avail = 0;
        while (true) {
            if (!WinHttpQueryDataAvailable(hRequest, &avail)) {
                OSTP_LOG_WARN("{} - WinHttpQueryDataAvailable failed (error={})", url ? url : "", GetLastError());
                break;
            }
            if (!avail) break;

            const size_t off = r.body.size();
            r.body.resize(off + avail);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, r.body.data() + off, avail, &read)) {
                OSTP_LOG_WARN("{} - WinHttpReadData(size={}) failed (error={})",
                              url ? url : "", avail, GetLastError());
                r.body.resize(off);
                break;
            }
            r.body.resize(off + read);
            if (r.body.size() >= maxBodyBytes) break;
        }

        if (r.status < 200 || r.status >= 300) {
            OSTP_LOG_WARN("{} - unexpected HTTP {}  body={}",
                             url,
                             r.status,
                             r.body.size() > 512 ? r.body.substr(0, 512) + "..." : r.body);
        } else {
            OSTP_LOG_TRACE("{} - response body={} ({}bytes)",
                           url ? url : "", r.body, r.body.size());
        }
        r.ok = true;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    OSTP_LOG_DEBUG("{} - elapsed: {}ms status={} body_bytes={}",
                   url ? url : "", elapsed, r.status, r.body.size());

    return r;
}

} // namespace OSTPlatform::Http
