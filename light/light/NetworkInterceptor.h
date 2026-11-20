// NetworkInterceptor.h
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "winwrap.h"
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <cstdint>
#include <queue>

#include "../../../WinDivert-2.2.2-A/include/windivert.h"

class NetworkInterceptor {
public:
    struct TrafficStats {
        uint64_t totalPackets = 0;
        uint64_t blockedPackets = 0;
        uint64_t allowedPackets = 0;
        std::map<std::string, uint64_t> blockedDomains;
    };

    using BlockCallback = std::function<void(
        const std::string& domain,
        const std::string& processName,
        DWORD pid)>;

    ~NetworkInterceptor();
    static NetworkInterceptor* GetInstance();

    bool Start();
    void Stop();
    bool IsRunning() const { return isRunning; }

    void BlockDomain(const std::string& domain, uint64_t timeoutMs = 0);
    void BlockDomainForProcess(const std::string& domain, const std::string& processName, uint64_t timeoutMs = 0);
    void UnblockDomain(const std::string& domain);
    void ClearAllBlocks();
    bool IsBlocked(const std::string& domain);
    bool IsDomainInBlockList(const std::string& domain);
    void LoadBlockList(const std::vector<std::string>& domains);
    void LoadBlockListFromFile(const std::string& filePath);

    void AddWhitelistDomain(const std::string& domain);
    void RemoveWhitelistDomain(const std::string& domain);
    bool IsWhitelisted(const std::string& domain);

    void AddBlockIP(const std::string& ip);
    void AddBlockIPRange(const std::string& startIP, const std::string& endIP);
    void RemoveBlockIP(const std::string& ip);
    bool ShouldBlockIP(const std::string& ipAddress);
    bool IsIPInRange(const std::string& ip, const std::string& start, const std::string& end);
    uint32_t IPToUint(const std::string& ip);
    std::string UintToIP(uint32_t ip);

    TrafficStats GetStats();
    void ResetStats();
    void SetBlockCallback(BlockCallback callback);

    bool InjectBlockedPacket(PVOID pPacket, UINT packetLen);

    void BlockUrl(const std::string& url, uint64_t timeoutMs = 60000) {
        std::string domain = url;
        if (domain.find("http://") == 0) domain = domain.substr(7);
        else if (domain.find("https://") == 0) domain = domain.substr(8);
        size_t pos = domain.find('/');
        if (pos != std::string::npos) domain = domain.substr(0, pos);
        if (domain.find("www.") == 0) domain = domain.substr(4);
        BlockDomain(domain, timeoutMs);
    }

private:
    // ====== TCP Stream Reassembly ======
    struct TcpStreamKey {
        DWORD srcAddr;
        DWORD dstAddr;
        USHORT srcPort;
        USHORT dstPort;
        bool operator<(const TcpStreamKey& other) const {
            if (srcAddr != other.srcAddr) return srcAddr < other.srcAddr;
            if (dstAddr != other.dstAddr) return dstAddr < other.dstAddr;
            if (srcPort != other.srcPort) return srcPort < other.srcPort;
            return dstPort < other.dstPort;
        }
        bool operator==(const TcpStreamKey& other) const {
            return srcAddr == other.srcAddr &&
                dstAddr == other.dstAddr &&
                srcPort == other.srcPort &&
                dstPort == other.dstPort;
        }
    };

    struct TcpSegment {
        uint32_t seqNum;
        uint32_t ackNum;
        std::vector<BYTE> data;
        bool isSyn;
        bool isFin;
        bool isRst;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct TcpStream {
        TcpStreamKey key;
        uint32_t nextExpectedSeq;
        uint32_t lastAck;
        std::map<uint32_t, TcpSegment> pendingSegments;
        std::vector<BYTE> reassembledData;
        std::chrono::steady_clock::time_point lastActivity;
        bool handshakeComplete;
        bool isTls;
        bool sniExtracted;
        std::string extractedSni;
        size_t bytesProcessed;
        size_t maxDataSize;
        bool isBlocked;

        TcpStream()
            : nextExpectedSeq(0)
            , lastAck(0)
            , lastActivity(std::chrono::steady_clock::now())
            , handshakeComplete(false)
            , isTls(false)
            , sniExtracted(false)
            , bytesProcessed(0)
            , maxDataSize(65536)
            , isBlocked(false) {
        }
    };

    std::map<TcpStreamKey, TcpStream> tcpStreams;
    std::mutex tcpStreamsMutex;
    static constexpr size_t MAX_STREAMS = 1000;
    static constexpr int STREAM_TIMEOUT_SECONDS = 30;
    static constexpr size_t MAX_STREAM_DATA = 65536;

    void CleanupExpiredStreams();
    TcpStreamKey CreateStreamKey(PVOID packet, UINT packetLength);  // 修改：添加 packetLength 参数
    bool ProcessTcpSegment(PVOID packet, UINT packetLength, const WINDIVERT_ADDRESS& address);
    bool ReassembleTcpStream(TcpStream& stream);
    bool ExtractSNIFromStream(const std::vector<BYTE>& data, std::string& sni, size_t& consumed);
    bool ExtractHTTPHostFromStream(const std::vector<BYTE>& data, std::string& host, size_t& consumed);

    // ====== IP Cache ======
    std::map<std::string, std::string> ipDomainCache;
    std::mutex ipCacheMutex;
    std::chrono::steady_clock::time_point lastIPCacheCleanup;

    void AddDomainIPCache(const std::string& domain, const std::string& ip);
    std::string GetDomainByIP(const std::string& ip);
    void CleanIPCache();

    // ====== PID Cache ======
    struct PidCacheEntry {
        DWORD pid;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::map<UINT, PidCacheEntry> portPidCache;
    std::mutex pidCacheMutex;
    std::chrono::steady_clock::time_point lastPidCacheUpdate;
    static constexpr int PID_CACHE_TTL = 5000;

    void UpdatePidCache();
    DWORD GetPidFromCache(UINT port);

    // ====== RST ======
    bool SendDNSResponseToRequester(PVOID pPacket, UINT packetLen,
        const std::string& domain, const std::string& fakeIP, uint16_t queryId);
    bool SendTCPReset(PVOID pPacket, UINT packetLen);
    bool SendSingleRST(DWORD srcAddr, DWORD dstAddr, USHORT srcPort, USHORT dstPort,
        UINT32 seqNum, UINT32 ackNum);

    // ====== WinDivert ======
    static NetworkInterceptor* instance;
    HANDLE winDivertHandle;
    std::atomic<bool> isRunning;
    std::thread interceptorThread;
    std::thread statsThread;

    // ====== Block Rules ======
    struct BlockRule {
        std::string domain;
        std::string pattern;
        uint64_t startTime;
        uint64_t timeout;
        bool isPermanent;
        std::set<std::string> processNames;
    };

    std::map<std::string, BlockRule> blockRules;
    std::mutex rulesMutex;
    std::map<std::string, bool> domainCache;
    std::mutex cacheMutex;
    std::set<std::string> whitelistDomains;
    std::mutex whitelistMutex;

    // ====== Stats ======
    TrafficStats stats;
    std::mutex statsMutex;

    // ====== Callback ======
    BlockCallback blockCallback;
    std::mutex callbackMutex;

    // ====== Process Cache ======
    std::map<DWORD, std::string> processCache;
    std::mutex processMutex;
    std::chrono::steady_clock::time_point lastCacheCleanup;
    std::chrono::steady_clock::time_point lastProcessUpdate;

    // ====== DNS ======
    std::mutex dnsMutex;
    std::vector<uint16_t> pendingDNSQueries;

    // ====== IP Blacklist ======
    std::mutex ipMutex;
    std::set<std::string> blockIPs;
    std::vector<std::pair<std::string, std::string>> blockIPRanges;

    // ====== Constructor ======
    NetworkInterceptor();

    // ====== Internal Methods ======
    void InterceptorThread();
    void StatsThread();
    void ProcessPacket(PVOID pPacket, UINT packetLen, const WINDIVERT_ADDRESS& address);
    bool ExtractDNSDomain(PVOID pPacket, UINT packetLen, std::string& domain, uint16_t& queryId);
    bool ExtractHTTPHost(PVOID pPacket, UINT packetLen, std::string& host);
    bool ExtractTLSSNI(PVOID pPacket, UINT packetLen, std::string& sni);
    bool MatchDomainPattern(const std::string& domain, const std::string& pattern);
    std::string GetProcessName(DWORD pid);
    DWORD GetPacketProcessId(PVOID pPacket, UINT packetLen);
    void CleanCache();
    bool SendDNSResponse(const std::string& domain, const std::string& fakeIP, uint16_t queryId);
};

extern NetworkInterceptor* g_NetworkInterceptor;