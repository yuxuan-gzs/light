// Http.cpp — WinHTTP 实现（连接池版）
#include "Http.h"
#include "Crypto.h"

#include <windows.h>
#include <winhttp.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <mutex>
#include <map>
#include <memory>
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

// ---------------------------------------------------------------
// 全局 session（复用）
// ---------------------------------------------------------------
namespace {

struct Session {
    HINTERNET h = nullptr;
    Session() {
        h = WinHttpOpen(Utf8ToWide(kUserAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (h) {
            // 显式启用 TLS 1.1 / 1.2（Mojang/Fabric 已拒绝 TLS 1.0）
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                              WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS,
                             &protocols, sizeof(protocols));
        }
    }
    ~Session() { if (h) WinHttpCloseHandle(h); }
};

static HINTERNET GlobalSession() {
    static Session s;
    return s.h;
}

// ---------------------------------------------------------------
// 连接池：按 host:port 缓存 hConn
// ---------------------------------------------------------------
struct ConnPool {
    std::mutex mu;
    std::map<std::wstring, std::vector<HINTERNET>> idle;
    std::map<std::wstring, int> total;
    // 资源下载用 32~48 线程，空闲连接要留够，否则每个文件都要重新做一次 TLS 握手
    static constexpr int kMaxIdlePerHost = 48;
    static constexpr int kMaxTotalPerHost = 64;
};
static ConnPool g_pool;

static HINTERNET BorrowConn(const std::wstring& host, int port) {
    std::wstring key = host + L":" + std::to_wstring(port);
    std::lock_guard<std::mutex> lk(g_pool.mu);
    auto it = g_pool.idle.find(key);
    if (it != g_pool.idle.end() && !it->second.empty()) {
        HINTERNET h = it->second.back();
        it->second.pop_back();
        return h;
    }
    HINTERNET h = WinHttpConnect(GlobalSession(), host.c_str(), (INTERNET_PORT)port, 0);
    if (h) g_pool.total[key] += 1;
    return h;
}

static void ReturnConn(const std::wstring& host, int port, HINTERNET h, bool ok) {
    if (!h) return;
    std::wstring key = host + L":" + std::to_wstring(port);
    std::lock_guard<std::mutex> lk(g_pool.mu);
    auto& vec = g_pool.idle[key];
    if (ok && (int)vec.size() < ConnPool::kMaxIdlePerHost) {
        vec.push_back(h);
        return;
    }
    WinHttpCloseHandle(h);
    auto it = g_pool.total.find(key);
    if (it != g_pool.total.end()) {
        it->second -= 1;
        if (it->second <= 0) g_pool.total.erase(it);
    }
}

// ---------------------------------------------------------------
// 请求头拼接辅助
// ---------------------------------------------------------------
static std::wstring BuildHeaders(const std::string& contentType,
                                 const std::string& extraHeaders,
                                 bool hasBody) {
    std::wstring hdrs;
    hdrs += L"Accept: */*\r\n";
    hdrs += L"Cache-Control: no-cache\r\n";
    if (!extraHeaders.empty()) {
        hdrs += Utf8ToWide(extraHeaders) + L"\r\n";
    }
    if (hasBody || !contentType.empty()) {
        std::wstring ct = Utf8ToWide(contentType.empty() ? "application/json" : contentType);
        hdrs += L"Content-Type: " + ct + L"\r\n";
    }
    return hdrs;
}

// 发送请求并读取完整响应体
static Response DoRequest(const std::string& url, const char* method,
                          const std::string& body, const std::string& contentType,
                          int timeoutMs, const std::string& extraHeaders) {
    Response r;
    UrlParts parts;
    if (!ParseUrl(url, parts)) { r.error = "invalid url"; return r; }

    HINTERNET hConn = BorrowConn(parts.host, parts.port);
    if (!hConn) { r.error = "connect failed"; return r; }

    DWORD flags = parts.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, Utf8ToWide(method).c_str(),
                                        parts.path.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) {
        ReturnConn(parts.host, parts.port, hConn, false);
        r.error = "open request failed";
        return r;
    }

    WinHttpSetTimeouts(hReq, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    std::wstring hdrs = BuildHeaders(contentType, extraHeaders, !body.empty());
    BOOL hasBody = !body.empty();

    BOOL ok = WinHttpSendRequest(hReq, hdrs.c_str(), (DWORD)hdrs.size(),
                                 hasBody ? (LPVOID)body.data() : WINHTTP_NO_REQUEST_DATA,
                                 hasBody ? (DWORD)body.size() : 0,
                                 hasBody ? (DWORD)body.size() : 0, 0);
    if (!ok) {
        DWORD e = GetLastError();
        WinHttpCloseHandle(hReq);
        ReturnConn(parts.host, parts.port, hConn, false);
        r.error = "send failed (WinHTTP " + std::to_string(e) + ")";
        return r;
    }

    if (!WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq);
        ReturnConn(parts.host, parts.port, hConn, false);
        r.error = "receive failed";
        return r;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;

    std::string out;
    std::vector<char> buf(64 * 1024);
    DWORD read = 0;
    bool readOk = true;
    while (WinHttpReadData(hReq, buf.data(), (DWORD)buf.size(), &read) && read > 0) {
        out.append(buf.data(), read);
    }
    
    DWORD lastErr = 0;
    if (!WinHttpReadData(hReq, buf.data(), 0, &read)) lastErr = GetLastError();
    readOk = (lastErr == 0 || lastErr == ERROR_WINHTTP_CONNECTION_ERROR ? lastErr == 0 : true);

    r.body = std::move(out);

    WinHttpCloseHandle(hReq);
    ReturnConn(parts.host, parts.port, hConn, readOk);
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

Response PostWithHeader(const std::string& url, const std::string& body,
                        const std::string& contentType, const std::string& headerLine,
                        int timeoutMs) {
    return DoRequest(url, "POST", body, contentType, timeoutMs, headerLine);
}

// ---------------------------------------------------------------
// 下载到文件（带连接池）
// ---------------------------------------------------------------
DownloadResult Download(const std::string& url, const std::string& destPath,
                        ProgressFn progress, int timeoutMs,
                        const std::string* expectedSha1) {
    DownloadResult res;
    UrlParts parts;
    if (!ParseUrl(url, parts)) { res.error = "invalid url"; return res; }

    HINTERNET hConn = BorrowConn(parts.host, parts.port);
    if (!hConn) { res.error = "connect failed"; return res; }

    DWORD flags = parts.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", parts.path.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) {
        ReturnConn(parts.host, parts.port, hConn, false);
        res.error = "open request failed";
        return res;
    }

    WinHttpSetTimeouts(hReq, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    std::wstring hdrs = L"Accept: */*\r\n";
    if (!WinHttpSendRequest(hReq, hdrs.c_str(), (DWORD)hdrs.size(),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        DWORD e = GetLastError();
        WinHttpCloseHandle(hReq);
        ReturnConn(parts.host, parts.port, hConn, false);
        res.error = "send failed (WinHTTP " + std::to_string(e) + ")";
        return res;
    }
    if (!WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq);
        ReturnConn(parts.host, parts.port, hConn, false);
        res.error = "receive failed";
        return res;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if ((int)status >= 400) {
        WinHttpCloseHandle(hReq);
        ReturnConn(parts.host, parts.port, hConn, false);
        res.error = "http " + std::to_string((int)status);
        return res;
    }

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
            WinHttpCloseHandle(hReq);
            ReturnConn(parts.host, parts.port, hConn, false);
            res.error = "create dir failed: " + ec.message();
            return res;
        }
    }

    // 临时文件，成功后再改名
    std::string tmp = destPath + ".part";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
        WinHttpCloseHandle(hReq);
        ReturnConn(parts.host, parts.port, hConn, false);
        res.error = "无法创建临时文件 (路径过长或权限不足): " + tmp;
        return res;
    }
    if (!f) {
        WinHttpCloseHandle(hReq);
        ReturnConn(parts.host, parts.port, hConn, false);
        res.error = "open file failed (errno " + std::to_string(errno) + ", winerr " +
                    std::to_string((int)GetLastError()) + ")";
        return res;
    }

    std::vector<char> buf(256 * 1024);
    DWORD read = 0;
    int64_t got = 0;
    bool downloadOk = true;
    bool cancelled = false;
    while (WinHttpReadData(hReq, buf.data(), (DWORD)buf.size(), &read) && read > 0) {
        f.write(buf.data(), read);
        got += read;
        if (progress && !progress(got, res.total)) {
            cancelled = true;
            downloadOk = false;
            break;
        }
    }
    if (!cancelled) {
        DWORD lastErr = GetLastError();
        if (lastErr != 0 && lastErr != ERROR_SUCCESS && lastErr != ERROR_WINHTTP_CONNECTION_ERROR) {
            downloadOk = false;
        }
    }
    f.close();
    res.got = got;

    WinHttpCloseHandle(hReq);
    ReturnConn(parts.host, parts.port, hConn, downloadOk);

    if (cancelled) {
        std::error_code e2; fs::remove(fs::u8path(tmp), e2);
        res.error = "cancelled";
        return res;
    }

    // 计算 sha1
    std::string sha = crypto::sha1File(tmp);
    res.sha1 = sha;

    if (expectedSha1 && !expectedSha1->empty()) {
        std::string a = sha, b = *expectedSha1;
        for (auto& c : a) c = (char)tolower((unsigned char)c);
        for (auto& c : b) c = (char)tolower((unsigned char)c);
        if (a != b) {
            // 校验失败：清理临时文件，并返回错误，让外层 LauncherCore 知道这个源不可用
            std::error_code e2; fs::remove(fs::u8path(tmp), e2);
            res.ok = false;
            res.error = "sha1 mismatch";
            return res;
        }
    }

   // 检查 .part 文件是否真的存在
    if (!fs::exists(fs::u8path(tmp))) {
        res.error = "临时文件丢失，无法重命名";
        return res;
    }

    // ---------------------------------------------------------
    // 终极重命名机制：指数退避重试，彻底扛过 Defender 的扫描锁
    // ---------------------------------------------------------
    std::error_code e3;
    fs::remove(fs::u8path(destPath), e3); // 先确保目标不存在

    bool renameOk = false;
    DWORD lastError = 0;
    
    // 重试 10 次（100ms, 200ms, 300ms... 最长等约 5.5 秒）
    for (int retry = 0; retry < 10; ++retry) {
        if (MoveFileExW(Utf8ToWide(tmp).c_str(), Utf8ToWide(destPath).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            renameOk = true;
            break;
        }
        
        lastError = GetLastError();
        // 32 = ERROR_SHARING_VIOLATION (文件被占用)
        // 5  = ERROR_ACCESS_DENIED (拒绝访问)
        if (lastError == 32 || lastError == 5) {
            Sleep(100 * (retry + 1)); // 指数退避等待
            continue;
        }
        break; // 其他错误（如路径不存在）没必要重试
    }

    if (!renameOk) {
        std::error_code e2; fs::remove(fs::u8path(tmp), e2);
        res.error = "rename failed (winerr " + std::to_string(lastError) + ")";
        res.ok = false;
        return res;
    }

    res.ok = true;
    return res;
}

} // namespace http