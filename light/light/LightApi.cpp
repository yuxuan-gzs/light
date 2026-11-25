// LightApi.cpp - network bridge module attached to the light main program.
// Built into light.exe and exported via LightApi.def. Same-process plugin
// DLLs call these functions through GetModuleHandle(NULL)+GetProcAddress.
#include "LightApi.h"

#include <winhttp.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "winhttp.lib")

namespace {

// Default request timeout in milliseconds when timeoutMs <= 0.
constexpr DWORD kDefaultTimeoutMs = 30000;

// Global WinHTTP session handle, created lazily by LightApi_Init.
HINTERNET g_session = NULL;

// Convert UTF-8 C string to wide string. Returns empty on conversion failure.
std::wstring Utf8ToWide(const char* s) {
    std::wstring out;
    if (!s || !*s) return out;
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (len <= 0) return out;
    out.resize(static_cast<size_t>(len) - 1);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], len);
    return out;
}

// Parse a URL string into request target (path + query) and host.
// Returns false on malformed input.
bool ParseUrl(const std::wstring& url,
              std::wstring& outHost,
              std::wstring& outPath,
              INTERNET_PORT& outPort,
              bool& outSecure) {
    URL_COMPONENTSW comp = { sizeof(comp) };
    wchar_t hostBuf[256] = { 0 };
    wchar_t pathBuf[4096] = { 0 };
    comp.lpszHostName = hostBuf;
    comp.dwHostNameLength = (DWORD)(_countof(hostBuf) - 1);
    comp.lpszUrlPath = pathBuf;
    comp.dwUrlPathLength = (DWORD)(_countof(pathBuf) - 1);
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &comp)) return false;

    outSecure = (comp.nScheme == INTERNET_SCHEME_HTTPS);
    outPort = comp.nPort;
    outHost.assign(comp.lpszHostName, comp.dwHostNameLength);
    if (comp.dwUrlPathLength > 0 && comp.lpszUrlPath) {
        outPath.assign(comp.lpszUrlPath, comp.dwUrlPathLength);
    } else {
        outPath = L"/";
    }
    return !outHost.empty();
}

// Core request executor shared by sync and streaming entry points.
// Out body (memory allocated with malloc) is only produced when bodyOut is
// non-NULL; chunk mode uses chunkFn instead.
int DoRequest(const char* method,
              const char* url,
              const char* headers,
              const char* body,
              int         timeoutMs,
              int*        outStatus,
              char**      bodyOut,
              LightApiChunkFn chunkFn,
              void*           user) {
    if (outStatus) *outStatus = 0;
    if (bodyOut) *bodyOut = NULL;
    if (!url || !*url) return -1;

    std::wstring wUrl = Utf8ToWide(url);
    if (wUrl.empty()) return -2;

    std::wstring wHost, wPath;
    INTERNET_PORT port = 0;
    bool secure = false;
    if (!ParseUrl(wUrl, wHost, wPath, port, secure)) return -3;

    if (!g_session) {
        g_session = WinHttpOpen(L"LightApi/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
        if (!g_session) return -4;
    }

    HINTERNET hConnect = WinHttpConnect(g_session, wHost.c_str(), port, 0);
    if (!hConnect) return -5;

    const std::wstring wMethod = Utf8ToWide(method ? method : "GET");
    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConnect, wMethod.c_str(),
        wPath.c_str(), NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) {
        WinHttpCloseHandle(hConnect);
        return -6;
    }

    DWORD timeout = (timeoutMs > 0) ? (DWORD)timeoutMs : kDefaultTimeoutMs;
    WinHttpSetTimeouts(hReq, timeout, timeout, timeout, timeout);

    // Build header block: caller lines + Content-Type default for body posts.
    std::wstring wHeaders;
    if (headers && *headers) {
        wHeaders = Utf8ToWide(headers);
    }

    LPCWSTR headerPtr = wHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wHeaders.c_str();
    DWORD headerLen = wHeaders.empty() ? 0 : (DWORD)wHeaders.size();

    const char* bodyPtr = body ? body : "";
    DWORD bodyLen = body ? (DWORD)strlen(body) : 0;

    BOOL ok = WinHttpSendRequest(hReq, headerPtr, headerLen,
        (LPVOID)bodyPtr, bodyLen, bodyLen, 0);
    if (!ok) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        return -7;
    }

    if (!WinHttpReceiveResponse(hReq, NULL)) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        return -8;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE |
        WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &statusSize, NULL);

    // Read body in chunks.
    std::vector<char> fullBody;
    constexpr DWORD kChunkCap = 65536;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) break;
        if (avail == 0) break;

        const DWORD readSize = (avail < kChunkCap) ? avail : kChunkCap;
        std::vector<char> buf(readSize);
        DWORD read = 0;
        if (!WinHttpReadData(hReq, buf.data(), readSize, &read)) break;
        if (read == 0) break;

        if (chunkFn) {
            chunkFn(buf.data(), read, user);
        } else if (bodyOut) {
            fullBody.insert(fullBody.end(), buf.begin(), buf.begin() + read);
        }
    }

    if (outStatus) *outStatus = (int)status;

    if (bodyOut && !chunkFn) {
        char* p = (char*)malloc(fullBody.size() + 1);
        if (p) {
            if (!fullBody.empty()) memcpy(p, fullBody.data(), fullBody.size());
            p[fullBody.size()] = '\0';
            *bodyOut = p;
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    return 0;
}

} // namespace

extern "C" {

int LightApi_Init(void) {
    if (g_session) return 0;
    g_session = WinHttpOpen(L"LightApi/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    return g_session ? 0 : -1;
}

void LightApi_Shutdown(void) {
    if (g_session) {
        WinHttpCloseHandle(g_session);
        g_session = NULL;
    }
}

int LightApi_HttpRequest(const char* method,
                         const char* url,
                         const char* headers,
                         const char* body,
                         int         timeoutMs,
                         int*        outStatus,
                         char**      outBody) {
    return DoRequest(method, url, headers, body, timeoutMs,
        outStatus, outBody, NULL, NULL);
}

int LightApi_HttpRequestStream(const char* method,
                               const char* url,
                               const char* headers,
                               const char* body,
                               int         timeoutMs,
                               int*        outStatus,
                               LightApiChunkFn chunkFn,
                               void*           user) {
    return DoRequest(method, url, headers, body, timeoutMs,
        outStatus, NULL, chunkFn, user);
}

void LightApi_Free(void* ptr) {
    if (ptr) free(ptr);
}

} // extern "C"