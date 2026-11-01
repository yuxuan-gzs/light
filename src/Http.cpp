// Http.cpp — WinHTTP 实现
#include "Http.h"
#include "Crypto.h"

#include <windows.h>
#include <winhttp.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <mutex>
#include <cerrno>

#pragma comment(lib, "winhttp.lib")

namespace http {

const char* kUserAgent = "YXG-MinecraftLauncher/1.0 (Windows)";

namespace fs = std::filesystem;

// ---------------------------------------------------------------
// UTF-8 <-> UTF-16
// ---------------------------------------------------------------
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &u[0], n, nullptr, nullptr);
    return u;
}

// ---------------------------------------------------------------
// URL 解析
// ---------------------------------------------------------------
bool ParseUrl(const std::string& url, UrlParts& out) {
    std::string u = url;
    // 去掉 scheme
    std::string scheme = "https";
    size_t sp = u.find("://");
    if (sp != std::string::npos) {
        scheme = u.substr(0, sp);
        u = u.substr(sp + 3);
    }
    out.secure = (scheme == "https");
    size_t slash = u.find('/');
    std::string hostport = (slash == std::string::npos) ? u : u.substr(0, slash);
    out.path = (slash == std::string::npos) ? std::wstring(L"/") : Utf8ToWide(u.substr(slash));
    // 端口
    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
        out.host = Utf8ToWide(hostport.substr(0, colon));
        out.port = atoi(hostport.substr(colon + 1).c_str());
    } else {
        out.host = Utf8ToWide(hostport);
        out.port = out.secure ? 443 : 80;
    }
    return !out.host.empty();
}

namespace {

struct Session {
    HINTERNET h = nullptr;
    Session() {
        h = WinHttpOpen(Utf8ToWide(kUserAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (h) {
            // 显式启用 TLS 1.1 / 1.2：Mojang/Fabric 等服务器已拒绝 TLS 1.0，
            // 若按系统默认可能只协商到 TLS 1.0 导致 WinHttpSendRequest 失败。
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                              WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS,
                             &protocols, sizeof(protocols));
        }
    }
    ~Session() { if (h) WinHttpCloseHandle(h); }
};

// 全局单例 session（WinHttpOpen 可复用；首次调用线程安全由 magic static 保证）
static HINTERNET GlobalSession() {
    static Session s;
    return s.h;
}

// 发送请求并读取完整响应体
static Response DoRequest(const std::string& url, const char* method,
                          const std::string& body, const std::string& contentType,
                          int timeoutMs, const std::string& extraHeaders) {
    Response r;
    UrlParts parts;
    if (!ParseUrl(url, parts)) { r.error = "invalid url"; return r; }

    HINTERNET hSess = GlobalSession();
    if (!hSess) { r.error = "WinHttpOpen failed"; return r; }

    HINTERNET hConn = WinHttpConnect(hSess, parts.host.c_str(), (INTERNET_PORT)parts.port, 0);
    if (!hConn) { r.error = "connect failed"; return r; }

    DWORD flags = parts.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, Utf8ToWide(method).c_str(),
                                        parts.path.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConn); r.error = "open request failed"; return r; }

    // 超时（毫秒）
    WinHttpSetTimeouts(hReq, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    // 头
    std::wstring hdrs;
    hdrs += L"Accept: */*\r\n";
    hdrs += L"Cache-Control: no-cache\r\n";
    if (!extraHeaders.empty()) {
        hdrs += Utf8ToWide(extraHeaders) + L"\r\n";
    }
    if (!body.empty() || contentType.size()) {
        std::wstring ct = Utf8ToWide(contentType.empty() ? "application/json" : contentType);
        hdrs += L"Content-Type: " + ct + L"\r\n";
    }
    BOOL hasBody = !body.empty();
    // 注意：dwHeadersLength 是「字符数」，不是字节数（MSDN 明确）。传 -1 表示以 null 结尾自动计算，最稳妥。
    DWORD headerLen = (DWORD)hdrs.size();

    BOOL ok = WinHttpSendRequest(hReq, hdrs.c_str(), headerLen,
                                 hasBody ? (LPVOID)body.data() : WINHTTP_NO_REQUEST_DATA,
                                 hasBody ? (DWORD)body.size() : 0,
                                 hasBody ? (DWORD)body.size() : 0, 0);
    if (!ok) {
        DWORD e = GetLastError();
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
        r.error = "send failed (WinHTTP " + std::to_string(e) + ")";
        return r;
    }

    if (!WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); r.error = "receive failed"; return r;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;

    // 读取全部
    std::string out;
    std::vector<char> buf(64 * 1024);
    DWORD read = 0;
    while (WinHttpReadData(hReq, buf.data(), (DWORD)buf.size(), &read) && read > 0) {
        out.append(buf.data(), read);
    }
    r.body = std::move(out);

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    return r;
}

} // namespace

Response Get(const std::string& url, int timeoutMs) {
    return DoRequest(url, "GET", "", "", timeoutMs, "");
}

Response Post(const std::string& url, const std::string& body,
              const std::string& contentType, int timeoutMs) {
    return DoRequest(url, "POST", body, contentType, timeoutMs, "");
}

Response GetWithHeader(const std::string& url, const std::string& headerLine, int timeoutMs) {
    return DoRequest(url, "GET", "", "", timeoutMs, headerLine);
}

DownloadResult Download(const std::string& url, const std::string& destPath,
                        ProgressFn progress, int timeoutMs,
                        const std::string* expectedSha1) {
    DownloadResult res;
    UrlParts parts;
    if (!ParseUrl(url, parts)) { res.error = "invalid url"; return res; }

    HINTERNET hSess = GlobalSession();
    if (!hSess) { res.error = "WinHttpOpen failed"; return res; }

    HINTERNET hConn = WinHttpConnect(hSess, parts.host.c_str(), (INTERNET_PORT)parts.port, 0);
    if (!hConn) { res.error = "connect failed"; return res; }

    DWORD flags = parts.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", parts.path.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConn); res.error = "open request failed"; return res; }

    WinHttpSetTimeouts(hReq, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    std::wstring hdrs = L"Accept: */*\r\n";
    // 注意：dwHeadersLength 是「字符数」不是字节数（MSDN 明确），传字节数会越界读返回 87。
    if (!WinHttpSendRequest(hReq, hdrs.c_str(), (DWORD)hdrs.size(),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        DWORD e = GetLastError();
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
        res.error = "send failed (WinHTTP " + std::to_string(e) + ")";
        return res;
    }
    if (!WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); res.error = "receive failed"; return res;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if ((int)status >= 400) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
        res.error = "http " + std::to_string((int)status);
        return res;
    }

    // 总长度
    DWORD total = 0, totalSize = sizeof(total);
    if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &total, &totalSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        res.total = (int64_t)total;
    } else {
        res.total = -1;
    }

    // 创建父目录
    std::error_code ec;
    fs::path p = fs::u8path(destPath);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
        if (ec) {
            WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
            res.error = "create dir failed: " + ec.message();
            return res;
        }
    }

    // 写入临时文件，成功后再改名，避免半成品
    std::string tmp = destPath + ".part";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
        res.error = "open file failed (errno " + std::to_string(errno) + ", winerr " +
                    std::to_string((int)GetLastError()) + ")";
        return res;
    }

    std::vector<char> buf(256 * 1024);
    DWORD read = 0;
    int64_t got = 0;
    while (WinHttpReadData(hReq, buf.data(), (DWORD)buf.size(), &read) && read > 0) {
        f.write(buf.data(), read);
        got += read;
        if (progress && !progress(got, res.total)) {
            f.close();
            WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
            std::error_code e2; fs::remove(fs::u8path(tmp), e2);
            res.error = "cancelled";
            return res;
        }
    }
    f.close();
    res.got = got;

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);

    // 计算 sha1
    std::string sha = crypto::sha1File(tmp);
    res.sha1 = sha;

    // 校验
    if (expectedSha1 && !expectedSha1->empty()) {
        // 忽略大小写比较
        std::string a = sha, b = *expectedSha1;
        for (auto& c : a) c = (char)tolower((unsigned char)c);
        for (auto& c : b) c = (char)tolower((unsigned char)c);
        if (a != b) {
            std::error_code e2; fs::remove(fs::u8path(tmp), e2);
            res.ok = false;
            res.error = "sha1 mismatch (got " + sha + ")";
            return res;
        }
    }

    // 原子替换
    std::error_code e3;
    fs::remove(fs::u8path(destPath), e3);
    fs::rename(fs::u8path(tmp), fs::u8path(destPath), e3);
    if (e3) { res.error = "rename failed"; res.ok = false; return res; }

    res.ok = true;
    return res;
}

} // namespace http
