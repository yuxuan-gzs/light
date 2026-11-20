#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <ctime>
#include <algorithm>
#include <direct.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

// ============================================================
// 1. 工具函数
// ============================================================

// 设置控制台为 UTF-8 编码
void SetConsoleUTF8() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

// URL 编码
std::string UrlEncode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        }
        else {
            escaped << '%' << std::setw(2) << int((unsigned char)c);
        }
    }
    return escaped.str();
}

// 生成随机字符串
std::string GenerateRandomString(int length) {
    static const char alphanum[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string result;
    result.reserve(length);
    srand((unsigned)time(NULL));
    for (int i = 0; i < length; ++i) {
        result += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    return result;
}

// 生成 CVID (32位十六进制)
std::string GenerateCVID() {
    std::string cvid;
    cvid.reserve(32);
    srand((unsigned)time(NULL));
    for (int i = 0; i < 32; ++i) {
        char c = "0123456789abcdef"[rand() % 16];
        cvid += c;
    }
    return cvid;
}

// 获取当前时间字符串
std::string GetCurrentTimeString() {
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return std::string(buffer);
}

// 获取当前日期字符串（用于文件名）
std::string GetCurrentDateString() {
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y%m%d", &timeinfo);
    return std::string(buffer);
}

// ============================================================
// 2. Bing 搜索类
// ============================================================

class BingSearch {
private:
    HINTERNET hSession;   // WinHTTP 会话句柄
    HINTERNET hConnect;   // 连接句柄
    std::map<std::wstring, std::wstring> cookies;  // Cookie 存储
    std::vector<std::string> foundLinks;  // 存储找到的所有链接
    std::string selectedUrl;  // 用户选择的 URL

public:
    BingSearch() : hSession(NULL), hConnect(NULL) {}

    ~BingSearch() {
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
    }

    // --------------------------------------------------------
    // 2.1 初始化 WinHTTP 会话
    // --------------------------------------------------------
    bool Initialize() {
        // 创建会话
        hSession = WinHttpOpen(
            L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );

        if (!hSession) {
            std::cout << "[错误] WinHttpOpen 失败: " << GetLastError() << std::endl;
            return false;
        }

        // 设置超时（30秒）
        DWORD timeout = 30000;
        WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

        // 启用 TLS 1.2
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS,
            &protocols, sizeof(protocols));

        return true;
    }

    // --------------------------------------------------------
    // 2.2 获取 Cookie
    // --------------------------------------------------------
    bool GetInitialCookies() {
        if (!hSession) {
            std::cout << "[错误] 会话未初始化" << std::endl;
            return false;
        }

        // 连接到 Bing
        hConnect = WinHttpConnect(hSession, L"cn.bing.com",
            INTERNET_DEFAULT_HTTPS_PORT, 0);

        if (!hConnect) {
            std::cout << "[错误] WinHttpConnect 失败: " << GetLastError() << std::endl;
            return false;
        }

        // 创建请求
        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect,
            L"GET",
            L"/",
            NULL,
            WINHTTP_NO_REFERER,
            NULL,
            WINHTTP_FLAG_SECURE
        );

        if (!hRequest) {
            std::cout << "[错误] WinHttpOpenRequest 失败: " << GetLastError() << std::endl;
            return false;
        }

        // 设置请求头（禁用 gzip 压缩）
        std::wstring headers =
            L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n"
            L"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
            L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n"
            L"Connection: keep-alive\r\n"
            L"Upgrade-Insecure-Requests: 1\r\n"
            L"Cache-Control: no-cache\r\n";

        if (!WinHttpSendRequest(hRequest, headers.c_str(), headers.length(),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            std::cout << "[错误] WinHttpSendRequest 失败: " << GetLastError() << std::endl;
            WinHttpCloseHandle(hRequest);
            return false;
        }

        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            std::cout << "[错误] WinHttpReceiveResponse 失败: " << GetLastError() << std::endl;
            WinHttpCloseHandle(hRequest);
            return false;
        }

        // 检查状态码
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            NULL, &statusCode, &statusCodeSize, NULL)) {
            std::cout << "[信息] 状态码: " << statusCode << std::endl;
        }

        // 提取 Cookie
        ExtractCookiesFromResponse(hRequest);

        // 读取响应（不保存到文件）
        std::string response = ReadResponse(hRequest);
        WinHttpCloseHandle(hRequest);

        // 如果没有获取到 Cookie，手动构造
        if (cookies.empty()) {
            std::cout << "[信息] 未获取到 Cookie，手动构造..." << std::endl;
            ConstructDefaultCookies();
        }

        return true;
    }

    // --------------------------------------------------------
    // 2.3 执行搜索
    // --------------------------------------------------------
    std::string Search(const std::string& query) {
        if (!hConnect) {
            std::cout << "[错误] 未连接到 Bing" << std::endl;
            return "";
        }

        // 清空之前的链接
        foundLinks.clear();
        selectedUrl.clear();

        std::string encodedQuery = UrlEncode(query + " download");

        // 构建搜索 URL
        std::wstring path = L"/search?q=" + std::wstring(encodedQuery.begin(), encodedQuery.end())
            + L"&form=QBLH";

        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect,
            L"GET",
            path.c_str(),
            NULL,
            L"https://cn.bing.com/",
            NULL,
            WINHTTP_FLAG_SECURE
        );

        if (!hRequest) {
            std::cout << "[错误] 创建搜索请求失败" << std::endl;
            return "";
        }

        // 构建请求头（包含 Cookie，禁用 gzip）
        std::wstring headers =
            L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n"
            L"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
            L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n"
            L"Connection: keep-alive\r\n"
            L"Upgrade-Insecure-Requests: 1\r\n"
            L"Cache-Control: no-cache\r\n"
            L"Referer: https://cn.bing.com/\r\n"
            L"Cookie: " + BuildCookieString() + L"\r\n";

        if (!WinHttpSendRequest(hRequest, headers.c_str(), headers.length(),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            std::cout << "[错误] 发送搜索请求失败: " << GetLastError() << std::endl;
            WinHttpCloseHandle(hRequest);
            return "";
        }

        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            std::cout << "[错误] 接收搜索响应失败: " << GetLastError() << std::endl;
            WinHttpCloseHandle(hRequest);
            return "";
        }

        // 检查状态码
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            NULL, &statusCode, &statusCodeSize, NULL)) {
            std::cout << "[信息] 搜索状态码: " << statusCode << std::endl;
            if (statusCode != 200) {
                WinHttpCloseHandle(hRequest);
                return "";
            }
        }

        // 读取响应
        std::string response = ReadResponse(hRequest);
        WinHttpCloseHandle(hRequest);

        return response;
    }

    // --------------------------------------------------------
    // 2.4 提取并显示所有下载链接
    // --------------------------------------------------------
    bool ExtractAndDisplayLinks(const std::string& html, const std::string& query) {
        if (html.empty()) {
            std::cout << "[错误] HTML 为空" << std::endl;
            return false;
        }

        // 检查是否是正常的搜索页面
        if (html.find("<title>") != std::string::npos &&
            html.find("搜索</title>") != std::string::npos) {
            std::cout << "[信息] 检测到正常搜索页面" << std::endl;
        }

        // 提取所有链接
        foundLinks.clear();

        size_t searchPos = 0;
        while ((searchPos = html.find("href=\"", searchPos)) != std::string::npos) {
            size_t linkStart = searchPos + 6;
            size_t linkEnd = html.find("\"", linkStart);
            if (linkEnd == std::string::npos) break;

            std::string link = html.substr(linkStart, linkEnd - linkStart);

            // 过滤规则
            if ((link.find("http://") == 0 || link.find("https://") == 0)) {
                if (link.find("bing.com") == std::string::npos &&
                    link.find("microsoft.com") == std::string::npos &&
                    link.find("go.microsoft.com") == std::string::npos) {
                    // 检查是否是下载链接
                    if (IsValidDownloadLink(link)) {
                        foundLinks.push_back(link);
                    }
                }
            }

            searchPos = linkEnd + 1;
        }

        // 去重
        std::sort(foundLinks.begin(), foundLinks.end());
        foundLinks.erase(std::unique(foundLinks.begin(), foundLinks.end()), foundLinks.end());

        if (foundLinks.empty()) {
            std::cout << "[信息] 未找到任何下载链接" << std::endl;
            return false;
        }

        // 显示所有链接让用户选择
        std::cout << "\n========================================" << std::endl;
        std::cout << "找到 " << foundLinks.size() << " 个下载链接：" << std::endl;
        std::cout << "========================================" << std::endl;

        for (size_t i = 0; i < foundLinks.size(); ++i) {
            std::cout << "[" << (i + 1) << "] " << foundLinks[i] << std::endl;
        }

        std::cout << "========================================" << std::endl;
        std::cout << "[0] 退出" << std::endl;
        std::cout << "请选择链接编号 (0-" << foundLinks.size() << "): ";

        int choice;
        std::cin >> choice;

        if (choice == 0) {
            std::cout << "[信息] 用户取消选择" << std::endl;
            selectedUrl = "";
            return false;
        }

        if (choice < 1 || choice >(int)foundLinks.size()) {
            std::cout << "[错误] 无效选择" << std::endl;
            selectedUrl = "";
            return false;
        }

        selectedUrl = foundLinks[choice - 1];
        std::cout << "[信息] 已选择: " << selectedUrl << std::endl;

        return true;
    }

    // --------------------------------------------------------
    // 2.5 保存日志到 Cache 目录
    // --------------------------------------------------------
    void SaveLog(const std::string& query, const std::string& htmlContent) {
        if (selectedUrl.empty()) {
            std::cout << "[警告] 没有选择的 URL，跳过日志保存" << std::endl;
            return;
        }

        // 创建 Cache 目录
        std::string cacheDir = ".\\Cache";
        if (_mkdir(cacheDir.c_str()) != 0 && errno != EEXIST) {
            std::cout << "[警告] 无法创建 Cache 目录" << std::endl;
        }

        // 生成日志文件名（使用时间戳）
        std::string dateStr = GetCurrentDateString();
        std::string timeStr = GetCurrentTimeString();
        // 替换时间中的特殊字符
        std::replace(timeStr.begin(), timeStr.end(), ':', '-');
        std::replace(timeStr.begin(), timeStr.end(), ' ', '_');

        std::string logFileName = cacheDir + "\\download_" + dateStr + "_" + timeStr + ".log";

        // 打开日志文件
        std::ofstream logFile(logFileName, std::ios::out | std::ios::trunc);

        if (!logFile.is_open()) {
            std::cout << "[错误] 无法打开日志文件: " << logFileName << std::endl;
            return;
        }

        // 写入日志头部
        logFile << "========================================" << std::endl;
        logFile << "Bing 下载链接搜索日志" << std::endl;
        logFile << "========================================" << std::endl;
        logFile << "时间: " << GetCurrentTimeString() << std::endl;
        logFile << "搜索关键词: " << query << std::endl;
        logFile << "选择的 URL: " << selectedUrl << std::endl;
        logFile << "========================================" << std::endl;
        logFile << std::endl;

        // 写入网页源代码
        logFile << "================ 网页源代码开始 ================" << std::endl;
        logFile << htmlContent << std::endl;
        logFile << "================ 网页源代码结束 ================" << std::endl;

        logFile.close();
        std::cout << "[信息] 日志已保存到: " << logFileName << std::endl;
        std::cout << "[信息] 日志大小: " << htmlContent.size() + 256 << " 字节" << std::endl;
    }


    // 获取用户选择的 URL
    std::string GetSelectedUrl() const {
        return selectedUrl;
    }

private:
    // --------------------------------------------------------
    // 2.6 内部辅助函数
    // --------------------------------------------------------

    // 从响应中提取 Cookie
    void ExtractCookiesFromResponse(HINTERNET hRequest) {
        DWORD dwSize = 0;

        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
            NULL, NULL, &dwSize, NULL) && dwSize > 0) {
            std::vector<wchar_t> buffer(dwSize / sizeof(wchar_t) + 1);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                NULL, buffer.data(), &dwSize, NULL)) {
                std::wstring headers(buffer.data());
                size_t pos = 0;

                while ((pos = headers.find(L"Set-Cookie:", pos)) != std::wstring::npos) {
                    size_t end = headers.find(L"\r\n", pos);
                    if (end != std::wstring::npos) {
                        std::wstring cookieLine = headers.substr(pos + 11, end - pos - 11);
                        size_t eqPos = cookieLine.find(L'=');

                        if (eqPos != std::wstring::npos) {
                            std::wstring key = cookieLine.substr(0, eqPos);
                            size_t semicolon = cookieLine.find(L';');
                            std::wstring value;

                            if (semicolon != std::wstring::npos) {
                                value = cookieLine.substr(eqPos + 1, semicolon - eqPos - 1);
                            }
                            else {
                                value = cookieLine.substr(eqPos + 1);
                            }

                            key.erase(0, key.find_first_not_of(L" \t"));
                            key.erase(key.find_last_not_of(L" \t") + 1);
                            value.erase(0, value.find_first_not_of(L" \t"));
                            value.erase(value.find_last_not_of(L" \t") + 1);

                            cookies[key] = value;
                        }
                        pos = end + 2;
                    }
                    else {
                        break;
                    }
                }
            }
        }
    }

    // 读取响应内容
    std::string ReadResponse(HINTERNET hRequest) {
        std::string response;
        DWORD bytesRead = 0;
        char buffer[8192];

        while (WinHttpReadData(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            response += buffer;
        }

        return response;
    }

    // 构造默认 Cookie
    void ConstructDefaultCookies() {
        std::string randomValue = GenerateRandomString(16);
        std::string cvid = GenerateCVID();

        cookies[L"SRCHD"] = L"AF=NOFORM";
        cookies[L"SRCHUID"] = L"V=2&GUID=" + std::wstring(randomValue.begin(), randomValue.end()) + L"&dmnchg=1";
        cookies[L"SRCHUSR"] = L"DOB=20260823";
        cookies[L"_EDGE_S"] = L"M=1&S=1&";
        cookies[L"_EDGE_V"] = L"1";
        cookies[L"MUID"] = std::wstring(randomValue.begin(), randomValue.end());
        cookies[L"MUIDB"] = std::wstring(randomValue.begin(), randomValue.end());
        cookies[L"_SS"] = L"S=1";
        cookies[L"SRCHHPGUSR"] = L"SRCHLANG=zh-Hans";
        cookies[L"cvid"] = std::wstring(cvid.begin(), cvid.end());

        std::cout << "[信息] 构造了 " << cookies.size() << " 个默认 Cookie" << std::endl;
    }

    // 构建 Cookie 字符串
    std::wstring BuildCookieString() {
        std::wstring cookieStr;
        for (const auto& pair : cookies) {
            if (!cookieStr.empty()) cookieStr += L"; ";
            cookieStr += pair.first + L"=" + pair.second;
        }
        return cookieStr;
    }

    // 检查链接是否为有效的下载链接
    bool IsValidDownloadLink(const std::string& link) {
        // 过滤黑名单域名
        std::vector<std::string> blacklist = {
            "bing.com", "microsoft.com", "go.microsoft.com",
            "google.com", "gstatic.com", "facebook.com",
            "twitter.com", "youtube.com", "googlesyndication.com",
            "doubleclick.net", "baidu.com", "zhihu.com"
        };

        std::string lowerLink = link;
        std::transform(lowerLink.begin(), lowerLink.end(), lowerLink.begin(), ::tolower);

        for (const auto& domain : blacklist) {
            if (lowerLink.find(domain) != std::string::npos) {
                return false;
            }
        }

        // 检查文件扩展名
        std::vector<std::string> extensions = {
            ".exe", ".msi", ".zip", ".dmg", ".pkg",
            ".deb", ".rpm", ".apk", ".tar.gz", ".tgz",
            ".7z", ".rar", ".jar"
        };

        for (const auto& ext : extensions) {
            if (lowerLink.find(ext) != std::string::npos) {
                return true;
            }
        }

        // 检查关键词
        std::vector<std::string> keywords = {
            "download", "setup", "install", "release",
            "latest", "version", "stable", "portable",
            "server"
        };

        for (const auto& keyword : keywords) {
            if (lowerLink.find(keyword) != std::string::npos) {
                return true;
            }
        }

        return false;
    }
};

// ============================================================
// 3. 主函数
// ============================================================

int main() {

    std::cout << "========================================" << std::endl;
    std::cout << "        Bing 下载链接搜索工具" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    BingSearch bing;

    // 1. 初始化
    std::cout << "[1/4] 初始化 WinHTTP..." << std::endl;
    if (!bing.Initialize()) {
        std::cout << "[错误] 初始化失败" << std::endl;
        system("pause");
        return 1;
    }

    // 2. 获取 Cookie
    std::cout << "[2/4] 获取 Cookie..." << std::endl;
    if (!bing.GetInitialCookies()) {
        std::cout << "[错误] 获取 Cookie 失败" << std::endl;
        system("pause");
        return 1;
    }
    std::cout << "[成功] Cookie 获取成功" << std::endl;
    std::cout << std::endl;

    // 3. 输入搜索关键词
    std::string query;
    std::cout << "[3/4] 请输入搜索关键词" << std::endl;
    std::cout << "      (直接回车使用默认: Minecraft Java Edition): ";
    std::getline(std::cin, query);

    if (query.empty()) {
        query = "Minecraft Java Edition";
    }
    std::cout << std::endl;

    // 4. 执行搜索
    std::cout << "[4/4] 正在搜索: " << query << std::endl;
    std::string html = bing.Search(query);

    if (html.empty()) {
        std::cout << "[错误] 搜索返回空结果" << std::endl;
        system("pause");
        return 1;
    }

    // 5. 提取并显示链接
    std::cout << std::endl;
    std::cout << "正在提取下载链接..." << std::endl;

    if (!bing.ExtractAndDisplayLinks(html, query)) {
        std::cout << "[失败] 未找到下载链接" << std::endl;
        system("pause");
        return 1;
    }

    // 6. 保存日志
    bing.SaveLog(query, html);

    // 7. 显示最终结果
    std::string selectedUrl = bing.GetSelectedUrl();
    if (!selectedUrl.empty()) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "[成功] 已选择的下载链接:" << std::endl;
        std::cout << selectedUrl << std::endl;
        std::cout << "========================================" << std::endl;
    }

    system("pause");
    return 0;
}