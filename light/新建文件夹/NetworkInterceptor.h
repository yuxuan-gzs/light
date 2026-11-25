// NetworkInterceptor.h
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <functional>
#include <atomic>
#include <thread>
#include <chrono>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// WFP Callout 驱动 IOCTL
#define DEVICE_NAME L"\\\\.\\WfpCallout"
#define IOCTL_ADD_BLOCK_IP CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_REMOVE_BLOCK_IP CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CLEAR_ALL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_STATS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)
typedef struct _BLOCK_IP_ENTRY {
    UINT32 IpAddress;
    UINT64 Timeout;
} BLOCK_IP_ENTRY;

typedef struct _STATS_DATA {
    UINT64 TotalBlocked;
    UINT64 ActiveRules;
} STATS_DATA;
#pragma pack(pop)

class SecurityCore;
extern SecurityCore* g_SecurityCore;

class NetworkInterceptor {
public:
    struct TrafficStats {
        uint64_t totalRequests = 0;
        uint64_t blockedRequests = 0;
        uint64_t dnsQueries = 0;
        std::unordered_map<std::string, uint64_t> blockedDomains;
    };

    struct BlockRule {
        std::string domain;
        std::string pattern;
        uint64_t startTime;
        uint64_t timeout;
        bool isPermanent;
        std::unordered_set<std::string> processNames;
        std::string resolvedIp;
    };

    typedef std::function<void(const std::string& domain, const std::string& processName, DWORD pid)> BlockCallback;

    static NetworkInterceptor* GetInstance();

    bool Start();
    void Stop();
    bool IsRunning() const { return isRunning; }

    void AddWhitelistDomain(const std::string& domain);
    void RemoveWhitelistDomain(const std::string& domain);
    void ClearWhitelist();
    bool IsWhitelisted(const std::string& domain);
    std::unordered_set<std::string> GetWhitelist();
    bool LoadWhitelistFromFile(const std::string& filePath);
    bool SaveWhitelistToFile(const std::string& filePath);

    void BlockDomain(const std::string& domain, uint64_t timeoutMs = 0);
    void UnblockDomain(const std::string& domain);
    void ClearAllBlocks();
    bool IsBlocked(const std::string& domain);
    std::unordered_map<std::string, BlockRule> GetBlockedDomains();

    bool ShouldBlockRequest(const std::string& url);
    bool ShouldBlockHost(const std::string& host);

    TrafficStats GetStats();
    void ResetStats();

    void SetBlockCallback(BlockCallback callback);
    void BlockUrl(const std::string& url, uint64_t timeoutMs = 0);

    void SetDebugMode(bool enabled) { debugMode = enabled; }

private:
    NetworkInterceptor();
    ~NetworkInterceptor();
    NetworkInterceptor(const NetworkInterceptor&) = delete;
    NetworkInterceptor& operator=(const NetworkInterceptor&) = delete;

    void CleanupExpiredRules();
    bool MatchDomainPattern(const std::string& domain, const std::string& pattern);
    bool IsDomainInBlockListLocked(const std::string& domain);
    std::string ExtractHost(const std::string& url);
    std::string ResolveDomainToIP(const std::string& domain);

    // 驱动通信
    bool SendToDriver(DWORD ioctl, void* input, DWORD inputSize, void* output, DWORD outputSize, DWORD* bytesReturned);
    bool IsDriverAvailable();
    bool AddToDriver(const std::string& domain, const std::string& ip, uint64_t timeoutMs);
    bool RemoveFromDriver(const std::string& domain);
    bool ClearDriver();
    STATS_DATA GetDriverStats();

    static NetworkInterceptor* instance;

    std::atomic<bool> isRunning;
    std::thread cleanupThread;

    std::unordered_set<std::string> whitelistDomains;
    std::unordered_map<std::string, BlockRule> blockRules;
    std::unordered_map<std::string, bool> domainCache;

    mutable std::mutex rulesMutex;
    mutable std::mutex cacheMutex;
    mutable std::mutex statsMutex;

    TrafficStats stats;
    BlockCallback blockCallback;
    bool debugMode;
    bool driverAvailable;

    std::chrono::steady_clock::time_point lastRuleCleanup;
    std::chrono::steady_clock::time_point lastDriverCheck;
};