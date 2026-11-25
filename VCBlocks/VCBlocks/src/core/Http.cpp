// Http.cpp
// 轻量 HTTP 下载（WinHTTP）
#include "Http.h"
#include "util.h"
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace vcb {

bool HttpGetBytes(const std::string& url, std::vector<unsigned char>& out, std::string& err) {
    out.clear();

    std::wstring wurl = Utf8ToWide(url);

    // 解析 URL
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[512] = {0}, path[4096] = {0}, extra[4096] = {0};
    uc.lpszHostName = host; uc.dwHostNameLength = 512;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        err = "无法解析 URL";
        return false;
    }
    bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    // 拼接 path + query
    std::wstring fullPath = path;
    if (extra[0]) fullPath += extra;

    HINTERNET session = WinHttpOpen(L"VCBlocks/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { err = "WinHttpOpen 失败"; return false; }

    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(session); err = "连接失败"; return false; }

    HINTERNET req = WinHttpOpenRequest(conn, L"GET", fullPath.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); err = "创建请求失败"; return false; }

    BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);

    if (ok) {
        DWORD available = 0;
        do {
            available = 0;
            WinHttpQueryDataAvailable(req, &available);
            if (available == 0) break;
            std::vector<unsigned char> buf(available);
            DWORD read = 0;
            if (WinHttpReadData(req, buf.data(), available, &read) && read > 0) {
                out.insert(out.end(), buf.begin(), buf.begin() + read);
            } else {
                break;
            }
        } while (available > 0);
    } else {
        err = "请求失败";
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    if (!ok) {
        if (err.empty()) err = "HTTP 请求失败";
        return false;
    }
    return true;
}

bool HttpGet(const std::string& url, std::string& outBody, std::string& err) {
    std::vector<unsigned char> bytes;
    if (!HttpGetBytes(url, bytes, err)) return false;
    outBody.assign(bytes.empty() ? "" : (const char*)bytes.data(), bytes.size());
    return true;
}

} // namespace vcb
