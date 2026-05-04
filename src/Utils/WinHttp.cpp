#include "WinHttp.h"
#include "dllmain.h"
#include <chrono>

#pragma comment(lib, "winhttp.lib")

namespace WinHttp {

    ParsedUrl ParseUrl(const char* rawUrl) {
        ParsedUrl out;
        std::string url(rawUrl);

        bool isHttps = false;
        if (url.starts_with("https://")) {
            isHttps = true;
            url = url.substr(8);
        } else if (url.starts_with("http://")) {
            url = url.substr(7);
        } else {
            return out;
        }
        out.tls = isHttps;
        out.port = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

        size_t slash = url.find('/');
        std::string hostPart = url.substr(0, slash);
        out.path = (slash != std::string::npos)
            ? L"/" + std::wstring(url.begin() + slash + 1, url.end())
            : L"/";

        size_t colon = hostPart.find(':');
        if (colon != std::string::npos) {
            out.host = std::wstring(hostPart.begin(), hostPart.begin() + colon);
            out.port = static_cast<INTERNET_PORT>(std::stoi(hostPart.substr(colon + 1)));
        } else {
            out.host = std::wstring(hostPart.begin(), hostPart.end());
        }

        out.valid = !out.host.empty();
        return out;
    }

    Result Execute(LPCWSTR method, const char* url,
                   const void* reqBody, DWORD reqBodyLen,
                   const wchar_t* headers,
                   DWORD timeoutResolve, DWORD timeoutConnect,
                   DWORD timeoutSend,    DWORD timeoutRecv) {
        Result r;

        ParsedUrl pu = ParseUrl(url);
        if (!pu.valid) {
            LOG_WINHTTP_WARN("Invalid URL: {}", url);
            return r;
        }

        LOG_WINHTTP_DEBUG("{}  (timeout: resolve={} connect={} send={} recv={})",
                         url, timeoutResolve, timeoutConnect, timeoutSend, timeoutRecv);

        auto t0 = std::chrono::steady_clock::now();

        HINTERNET hSession = WinHttpOpen(L"OpenSteamTool/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            LOG_WINHTTP_WARN("{} - WinHttpOpen failed", url);
            return r;
        }

        WinHttpSetTimeouts(hSession, timeoutResolve, timeoutConnect, timeoutSend, timeoutRecv);

        HINTERNET hConnect = WinHttpConnect(hSession, pu.host.c_str(), pu.port, 0);
        if (!hConnect) {
            LOG_WINHTTP_WARN("{} - WinHttpConnect failed (port={})", url, pu.port);
            WinHttpCloseHandle(hSession);
            return r;
        }

        DWORD flags = pu.tls ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, pu.path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            LOG_WINHTTP_WARN("{} - WinHttpOpenRequest failed", url);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return r;
        }

        if (headers && headers[0]) {
            WinHttpAddRequestHeaders(hRequest, headers,
                static_cast<DWORD>(wcslen(headers)),
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        DWORD totalLen = reqBodyLen;
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                const_cast<void*>(reqBody), reqBodyLen, totalLen, 0)) {
            LOG_WINHTTP_WARN("{} - WinHttpSendRequest failed (error={})",
                             url, GetLastError());
        } else if (!WinHttpReceiveResponse(hRequest, nullptr)) {
            LOG_WINHTTP_WARN("{} - WinHttpReceiveResponse failed (error={})",
                             url, GetLastError());
        } else {
            DWORD sz = sizeof(r.status);
            WinHttpQueryHeaders(hRequest,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &r.status, &sz,
                WINHTTP_NO_HEADER_INDEX);

            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &avail) && avail) {
                size_t off = r.body.size();
                r.body.resize(off + avail);
                DWORD read = 0;
                if (!WinHttpReadData(hRequest, r.body.data() + off, avail, &read)) break;
                r.body.resize(off + read);
                if (r.body.size() > 256 * 1024) break;
            }

            if (r.status < 200 || r.status >= 300) {
                LOG_WINHTTP_WARN("{} - unexpected HTTP {}  body={}", url, r.status,
                                r.body.size() > 512 ? r.body.substr(0, 512) + "..." : r.body);
            }
            r.ok = true;
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        LOG_WINHTTP_INFO("{} - elapsed: {}ms  (status={}, body={} ({}bytes))",
                        url, elapsed, r.status,r.body, r.body.size());

        return r;
    }

    Result ExecuteEx(HINTERNET hSession, HINTERNET hConnect, bool tls,
                     LPCWSTR method, const wchar_t* path,
                     const void* reqBody, DWORD reqBodyLen,
                     const wchar_t* headers,
                     const char* urlForLog) {
        Result r;
        if (!hSession || !hConnect) return r;

        auto t0 = std::chrono::steady_clock::now();

        DWORD flags = tls ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, path,
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            LOG_WINHTTP_WARN("{} - WinHttpOpenRequest failed", urlForLog);
            return r;
        }

        if (headers && headers[0]) {
            WinHttpAddRequestHeaders(hRequest, headers,
                static_cast<DWORD>(wcslen(headers)),
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        DWORD totalLen = reqBodyLen;
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                const_cast<void*>(reqBody), reqBodyLen, totalLen, 0)) {
            LOG_WINHTTP_WARN("{} - WinHttpSendRequest failed (error={})", urlForLog, GetLastError());
        } else if (!WinHttpReceiveResponse(hRequest, nullptr)) {
            LOG_WINHTTP_WARN("{} - WinHttpReceiveResponse failed (error={})", urlForLog, GetLastError());
        } else {
            DWORD sz = sizeof(r.status);
            WinHttpQueryHeaders(hRequest,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &r.status, &sz,
                WINHTTP_NO_HEADER_INDEX);

            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &avail) && avail) {
                size_t off = r.body.size();
                r.body.resize(off + avail);
                DWORD read = 0;
                if (!WinHttpReadData(hRequest, r.body.data() + off, avail, &read)) break;
                r.body.resize(off + read);
                if (r.body.size() > 256 * 1024) break;
            }

            if (r.status < 200 || r.status >= 300) {
                LOG_WINHTTP_WARN("{} - unexpected HTTP {}  body={}", urlForLog, r.status,
                                r.body.size() > 512 ? r.body.substr(0, 512) + "..." : r.body);
            }
            r.ok = true;
        }

        WinHttpCloseHandle(hRequest);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        LOG_WINHTTP_INFO("{} - elapsed: {}ms  (status={}, body={} ({}bytes))",
                        urlForLog, elapsed, r.status, r.body, r.body.size());

        return r;
    }

}
