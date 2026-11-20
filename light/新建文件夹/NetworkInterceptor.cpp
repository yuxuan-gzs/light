// NetworkInterceptor.cpp
#include "NetworkInterceptor.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <regex>

extern std::string GetCurrentTimeString();

NetworkInterceptor* NetworkInterceptor::instance = nullptr;

NetworkInterceptor::NetworkInterceptor()
    : isRunning(false)
    , debugMode(false)
    , driverAvailable(false)
    , lastRuleCleanup(std::chrono::steady_clock::now())
    , lastDriverCheck(std::chrono::steady_clock::now()) {
}

NetworkInterceptor::~NetworkInterceptor() {
    Stop();
}

NetworkInterceptor* NetworkInterceptor::GetInstance() {
    if (!instance) {
        instance = new NetworkInterceptor();
    }
    return instance;
}

// ============== 驱动通信 ==============

bool NetworkInterceptor::SendToDriver(DWORD ioctl, void* input, DWORD inputSize,
    void* output, DWORD outputSize, DWORD* bytesReturned) {
    HANDLE hDevice = CreateFileW(
        DEVICE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytes = 0;
    BOOL result = DeviceIoControl(
        hDevice,
        ioctl,
        input, inputSize,
        output, outputSize,
        bytesReturned ? bytesReturned : &bytes,
        NULL
    );

    CloseHandle(hDevice);
    return result != FALSE;
}

bool NetworkInterceptor::IsDriverAvailable() {
    HANDLE hDevice = CreateFileW(
        DEVICE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        return false;
    }

    CloseHandle(hDevice);
    return true;
}

bool NetworkInterceptor::AddToDriver(const std::string& domain, const std::string& ip, uint64_t timeoutMs) {
    // 将 IP 字符串转换为网络字节序
    struct in_addr addr;
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
        return false;
    }

    BLOCK_IP_ENTRY entry;
    entry.IpAddress = addr.s_addr;  // 已经是网络字节序
    entry.Timeout = timeoutMs;

    DWORD bytesReturned;
    return SendToDriver(IOCTL_ADD_BLOCK_IP, &entry, sizeof(entry), NULL, 0, &bytesReturned);
}

bool NetworkInterceptor::RemoveFromDriver(const std::string& domain) {
    // 从 blockRules 中获取 IP
    std::lock_guard<std::mutex> lock(rulesMutex);
    auto it = blockRules.find(domain);
    if (it == blockRules.end()) {
        return false;
    }

    if (it->second.resolvedIp.empty()) {
        return false;
    }

    struct in_addr addr;
    if (inet_pton(AF_INET, it->second.resolvedIp.c_str(), &addr) != 1) {
        return false;
    }

    UINT32 ip = addr.s_addr;
    DWORD bytesReturned;
    return SendToDriver(IOCTL_REMOVE_BLOCK_IP, &ip, sizeof(ip), NULL, 0, &bytesReturned);
}

bool NetworkInterceptor::ClearDriver() {
    DWORD bytesReturned;
    return SendToDriver(IOCTL_CLEAR_ALL, NULL, 0, NULL, 0, &bytesReturned);
}

STATS_DATA NetworkInterceptor::GetDriverStats() {
    STATS_DATA stats = { 0 };
    DWORD bytesReturned;
    if (SendToDriver(IOCTL_GET_STATS, NULL, 0, &stats, sizeof(stats), &bytesReturned)) {
        return stats;
    }
    return stats;
}

// ============== 域名解析 ==============

std::string NetworkInterceptor::ResolveDomainToIP(const std::string& domain) {
    struct addrinfo hints = {};
    struct addrinfo* result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(domain.c_str(), nullptr, &hints, &result) != 0) {
        return "";
    }

    std::string ip;
    if (result) {
        struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
        char ipBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ipBuf, INET_ADDRSTRLEN);
        ip = ipBuf;
        freeaddrinfo(result);
    }
    return ip;
}

// ============== 启动/停止 ==============

bool NetworkInterceptor::Start() {
    if (isRunning) {
        return true;
    }

    // 检查驱动是否可用
    driverAvailable = IsDriverAvailable();
    if (!driverAvailable) {
        // 驱动不可用，但仍然可以运行（仅应用层检查）
        debugMode ? OutputDebugStringA("[Interceptor] Driver not available, running in app-only mode\n") : 0;
    }

    CleanupExpiredRules();

    isRunning = true;
    lastRuleCleanup = std::chrono::steady_clock::now();

    cleanupThread = std::thread([this]() {
        while (isRunning) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            CleanupExpiredRules();
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                if (domainCache.size() > 1000) {
                    domainCache.clear();
                }
            }
        }
        });

    return true;
}

void NetworkInterceptor::Stop() {
    if (!isRunning) return;

    isRunning = false;

    if (cleanupThread.joinable()) {
        cleanupThread.join();
    }

    // 清除驱动中的所有规则
    if (driverAvailable) {
        ClearDriver();
    }
}

// ============== 域名匹配 ==============

bool NetworkInterceptor::MatchDomainPattern(const std::string& domain, const std::string& pattern) {
    if (domain == pattern) {
        return true;
    }

    if (pattern.length() > 2 && pattern[0] == '*' && pattern[1] == '.') {
        std::string suffix = pattern.substr(2);
        if (domain == suffix) {
            return true;
        }
        if (domain.length() > suffix.length() + 1 &&
            domain[domain.length() - suffix.length() - 1] == '.' &&
            domain.substr(domain.length() - suffix.length()) == suffix) {
            return true;
        }
        return false;
    }

    if (!pattern.empty() && pattern[0] == '.') {
        if (domain == pattern.substr(1)) {
            return true;
        }
        if (domain.find(pattern) != std::string::npos) {
            return true;
        }
        return false;
    }

    return false;
}

std::string NetworkInterceptor::ExtractHost(const std::string& url) {
    std::string host = url;

    if (host.find("http://") == 0) host = host.substr(7);
    else if (host.find("https://") == 0) host = host.substr(8);

    size_t pos = host.find('/');
    if (pos != std::string::npos) host = host.substr(0, pos);

    pos = host.find(':');
    if (pos != std::string::npos) host = host.substr(0, pos);

    if (host.find("www.") == 0) host = host.substr(4);

    return host;
}

// ============== 清理过期规则 ==============

void NetworkInterceptor::CleanupExpiredRules() {
    uint64_t now = GetTickCount64();
    std::lock_guard<std::mutex> lock(rulesMutex);

    auto it = blockRules.begin();
    while (it != blockRules.end()) {
        if (!it->second.isPermanent && (now - it->second.startTime > it->second.timeout)) {
            // 从驱动删除
            if (driverAvailable && !it->second.resolvedIp.empty()) {
                RemoveFromDriver(it->first);
            }
            it = blockRules.erase(it);
        }
        else {
            ++it;
        }
    }
}

// ============== 白名单管理 ==============

void NetworkInterceptor::AddWhitelistDomain(const std::string& domain) {
    if (domain.empty()) return;
    std::lock_guard<std::mutex> lock(rulesMutex);
    whitelistDomains.insert(domain);
}

void NetworkInterceptor::RemoveWhitelistDomain(const std::string& domain) {
    std::lock_guard<std::mutex> lock(rulesMutex);
    whitelistDomains.erase(domain);
}

void NetworkInterceptor::ClearWhitelist() {
    std::lock_guard<std::mutex> lock(rulesMutex);
    whitelistDomains.clear();
}

bool NetworkInterceptor::IsWhitelisted(const std::string& domain) {
    if (domain.empty()) return false;

    std::lock_guard<std::mutex> lock(rulesMutex);
    for (const auto& wl : whitelistDomains) {
        if (MatchDomainPattern(domain, wl)) {
            return true;
        }
    }
    return false;
}

std::unordered_set<std::string> NetworkInterceptor::GetWhitelist() {
    std::lock_guard<std::mutex> lock(rulesMutex);
    return whitelistDomains;
}

// ============== 从文件加载白名单 ==============

bool NetworkInterceptor::LoadWhitelistFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    if (content.empty()) {
        return false;
    }

    std::vector<std::string> domains;
    size_t pos = 0;

    size_t whitelistPos = content.find("\"whitelist\"");
    if (whitelistPos == std::string::npos) {
        return false;
    }

    size_t arrayStart = content.find('[', whitelistPos);
    if (arrayStart == std::string::npos) {
        return false;
    }

    size_t arrayEnd = content.find(']', arrayStart);
    if (arrayEnd == std::string::npos) {
        return false;
    }

    pos = arrayStart;
    while (pos < arrayEnd) {
        size_t quoteStart = content.find('"', pos);
        if (quoteStart == std::string::npos || quoteStart > arrayEnd) break;

        size_t quoteEnd = content.find('"', quoteStart + 1);
        if (quoteEnd == std::string::npos || quoteEnd > arrayEnd) break;

        std::string domain = content.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        if (!domain.empty()) {
            domains.push_back(domain);
        }
        pos = quoteEnd + 1;
    }

    if (domains.empty()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(rulesMutex);
        for (const auto& domain : domains) {
            std::string trimmed = domain;
            trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
            if (!trimmed.empty()) {
                whitelistDomains.insert(trimmed);
            }
        }
    }

    return true;
}

bool NetworkInterceptor::SaveWhitelistToFile(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(rulesMutex);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n    \"whitelist\": [\n";
    size_t i = 0;
    for (const auto& domain : whitelistDomains) {
        file << "        \"" << domain << "\"";
        if (++i < whitelistDomains.size()) {
            file << ",";
        }
        file << "\n";
    }
    file << "    ]\n}\n";
    file.close();

    return true;
}

// ============== 黑名单管理 ==============

void NetworkInterceptor::BlockDomain(const std::string& domain, uint64_t timeoutMs) {
    if (domain.empty()) return;

    // 检查是否在白名单中
    if (IsWhitelisted(domain)) {
        return;
    }

    std::lock_guard<std::mutex> lock(rulesMutex);

    // 检查是否已存在
    auto it = blockRules.find(domain);
    if (it != blockRules.end()) {
        // 更新超时
        it->second.timeout = timeoutMs;
        it->second.startTime = GetTickCount64();
        it->second.isPermanent = (timeoutMs == 0);
        return;
    }

    BlockRule rule;
    rule.domain = domain;
    rule.pattern = domain;
    rule.startTime = GetTickCount64();
    rule.timeout = timeoutMs;
    rule.isPermanent = (timeoutMs == 0);

    // 解析 IP 并添加到驱动
    if (driverAvailable) {
        rule.resolvedIp = ResolveDomainToIP(domain);
        if (!rule.resolvedIp.empty()) {
            AddToDriver(domain, rule.resolvedIp, timeoutMs);
        }
    }

    blockRules[domain] = rule;

    {
        std::lock_guard<std::mutex> cacheLock(cacheMutex);
        domainCache.erase(domain);
    }
}

void NetworkInterceptor::UnblockDomain(const std::string& domain) {
    if (domain.empty()) return;

    {
        std::lock_guard<std::mutex> lock(rulesMutex);
        auto it = blockRules.find(domain);
        if (it != blockRules.end()) {
            // 从驱动删除
            if (driverAvailable && !it->second.resolvedIp.empty()) {
                RemoveFromDriver(domain);
            }
            blockRules.erase(it);
        }
    }

    {
        std::lock_guard<std::mutex> cacheLock(cacheMutex);
        domainCache.erase(domain);
    }
}

void NetworkInterceptor::ClearAllBlocks() {
    {
        std::lock_guard<std::mutex> lock(rulesMutex);
        blockRules.clear();
    }

    if (driverAvailable) {
        ClearDriver();
    }

    {
        std::lock_guard<std::mutex> cacheLock(cacheMutex);
        domainCache.clear();
    }
}

bool NetworkInterceptor::IsBlocked(const std::string& domain) {
    if (domain.empty()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = domainCache.find(domain);
        if (it != domainCache.end()) {
            return it->second;
        }
    }

    bool blocked = false;
    {
        std::lock_guard<std::mutex> lock(rulesMutex);
        blocked = IsDomainInBlockListLocked(domain);
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        domainCache[domain] = blocked;
        if (domainCache.size() > 1000) {
            domainCache.clear();
        }
    }

    return blocked;
}

bool NetworkInterceptor::IsDomainInBlockListLocked(const std::string& domain) {
    if (blockRules.empty()) {
        return false;
    }

    uint64_t now = GetTickCount64();

    for (const auto& rule : blockRules) {
        if (!rule.second.isPermanent && (now - rule.second.startTime > rule.second.timeout)) {
            continue;
        }
        if (MatchDomainPattern(domain, rule.second.pattern)) {
            return true;
        }
    }
    return false;
}

std::unordered_map<std::string, NetworkInterceptor::BlockRule> NetworkInterceptor::GetBlockedDomains() {
    std::lock_guard<std::mutex> lock(rulesMutex);
    return blockRules;
}

// ============== 请求拦截 ==============

bool NetworkInterceptor::ShouldBlockRequest(const std::string& url) {
    if (url.empty()) return false;

    std::string host = ExtractHost(url);

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.totalRequests++;
        stats.dnsQueries++;
    }

    if (IsWhitelisted(host)) {
        return false;
    }

    if (IsBlocked(host)) {
        DWORD pid = GetCurrentProcessId();
        char processName[MAX_PATH];
        GetModuleFileNameA(NULL, processName, MAX_PATH);
        std::string name(processName);
        size_t pos = name.find_last_of("\\/");
        std::string procName = (pos != std::string::npos) ? name.substr(pos + 1) : name;

        {
            std::lock_guard<std::mutex> lock(statsMutex);
            stats.blockedRequests++;
            stats.blockedDomains[host]++;
        }

        if (blockCallback) {
            blockCallback(host, procName, pid);
        }

        return true;
    }

    return false;
}

bool NetworkInterceptor::ShouldBlockHost(const std::string& host) {
    if (host.empty()) return false;
    return ShouldBlockRequest("http://" + host);
}

// ============== 统计 ==============

NetworkInterceptor::TrafficStats NetworkInterceptor::GetStats() {
    std::lock_guard<std::mutex> lock(statsMutex);
    return stats;
}

void NetworkInterceptor::ResetStats() {
    std::lock_guard<std::mutex> lock(statsMutex);
    stats = TrafficStats{};
}

void NetworkInterceptor::SetBlockCallback(BlockCallback callback) {
    blockCallback = callback;
}

void NetworkInterceptor::BlockUrl(const std::string& url, uint64_t timeoutMs) {
    std::string host = ExtractHost(url);
    if (!host.empty()) {
        BlockDomain(host, timeoutMs);
    }
}