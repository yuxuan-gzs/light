// Http.h — WinHTTP 封装：GET/POST 与带进度的文件下载
#pragma once

#include <string>
#include <functional>
#include <cstdint>

namespace http {

struct Response {
    int status = -1;      // HTTP 状态码；<0 表示网络错误
    std::string body;     // 响应体（文本）
    std::string error;    // 错误描述（status<0 时有效）
};

struct DownloadResult {
    bool ok = false;
    std::string error;
    int64_t total = 0;    // 期望字节数（-1 未知）
    int64_t got = 0;      // 实际写入字节数
    std::string sha1;     // 下载后计算的 sha1（hex）
};

// 进度回调：返回 false 可中止下载
using ProgressFn = std::function<bool(int64_t got, int64_t total)>;

// 用户代理
extern const char* kUserAgent;

Response Get(const std::string& url, int timeoutMs = 30000);
Response Post(const std::string& url, const std::string& body,
              const std::string& contentType, int timeoutMs = 30000);

// GET 并附带额外请求头（如 "Authorization: Bearer xxx"）
Response GetWithHeader(const std::string& url, const std::string& headerLine,
                       int timeoutMs = 30000);

// 下载到文件（自动创建父目录），支持进度与 sha1 校验
// 若 expectedSha1 非空，下载完成后校验，不匹配返回 ok=false
DownloadResult Download(const std::string& url, const std::string& destPath,
                        ProgressFn progress = nullptr, int timeoutMs = 60000,
                        const std::string* expectedSha1 = nullptr);

// 解析 URL：返回 <host, port, path>，失败返回空
struct UrlParts { std::wstring host; int port = 0; std::wstring path; bool secure = true; };
bool ParseUrl(const std::string& url, UrlParts& out);

// UTF-8 <-> 宽字符转换（内部使用）
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

} // namespace http
