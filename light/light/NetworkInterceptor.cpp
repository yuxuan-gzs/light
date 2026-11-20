// NetworkInterceptor.cpp

#include "NetworkInterceptor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "../../../WinDivert-2.2.2-A/x64/WinDivert.lib")

NetworkInterceptor* NetworkInterceptor::instance = nullptr;
NetworkInterceptor* g_NetworkInterceptor = nullptr;

// ========== 全局调试计数器 ==========
static int g_debugPacketCount = 0;
static int g_debugTlsCount = 0;
static int g_debugBlockedCount = 0;
static bool g_debugInit = false;

namespace
{
    std::string ToLower(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
        return value;
    }

    std::string NormalizeDomain(std::string domain)
    {
        while (!domain.empty() &&
            (domain.front() == ' ' ||
                domain.front() == '\t' ||
                domain.front() == '\r' ||
                domain.front() == '\n')) {
            domain.erase(domain.begin());
        }

        while (!domain.empty() &&
            (domain.back() == ' ' ||
                domain.back() == '\t' ||
                domain.back() == '\r' ||
                domain.back() == '\n')) {
            domain.pop_back();
        }

        domain = ToLower(domain);

        while (!domain.empty() && domain.back() == '.') {
            domain.pop_back();
        }

        return domain;
    }

    bool IsValidIPv4String(const std::string& value)
    {
        in_addr address{};
        return inet_pton(AF_INET, value.c_str(), &address) == 1;
    }

    bool SendPacket(HANDLE handle, PVOID packet, UINT packetLength, const WINDIVERT_ADDRESS* address)
    {
        if (handle == INVALID_HANDLE_VALUE || packet == nullptr || packetLength == 0) {
            return false;
        }
        return WinDivertSend(handle, packet, packetLength, nullptr, address) != FALSE;
    }

    bool GetPacketHeaders(
        PVOID packet,
        UINT packetLength,
        WINDIVERT_IPHDR** ipHeader,
        WINDIVERT_IPV6HDR** ipv6Header,
        WINDIVERT_TCPHDR** tcpHeader,
        WINDIVERT_UDPHDR** udpHeader,
        PVOID* payload,
        UINT* payloadLength,
        UINT8* protocol)
    {
        if (packet == nullptr || packetLength == 0) {
            return false;
        }

        *ipHeader = nullptr;
        *ipv6Header = nullptr;
        *tcpHeader = nullptr;
        *udpHeader = nullptr;
        *payload = nullptr;
        *payloadLength = 0;
        *protocol = 0;

        return WinDivertHelperParsePacket(
            packet,
            packetLength,
            ipHeader,
            ipv6Header,
            protocol,
            nullptr,
            nullptr,
            tcpHeader,
            udpHeader,
            payload,
            payloadLength,
            nullptr,
            nullptr) != FALSE;
    }
}

// ============== TCP Stream Reassembly ==============

NetworkInterceptor::TcpStreamKey NetworkInterceptor::CreateStreamKey(PVOID packet, UINT packetLength)
{
    WINDIVERT_IPHDR* ipHeader = nullptr;
    WINDIVERT_IPV6HDR* ipv6Header = nullptr;
    WINDIVERT_TCPHDR* tcpHeader = nullptr;
    WINDIVERT_UDPHDR* udpHeader = nullptr;
    PVOID payload = nullptr;
    UINT payloadLength = 0;
    UINT8 protocol = 0;

    TcpStreamKey key{};
    key.srcAddr = 0;
    key.dstAddr = 0;
    key.srcPort = 0;
    key.dstPort = 0;

    if (!GetPacketHeaders(
        packet,
        packetLength,
        &ipHeader,
        &ipv6Header,
        &tcpHeader,
        &udpHeader,
        &payload,
        &payloadLength,
        &protocol)) {
        return key;
    }

    if (ipHeader == nullptr || ipv6Header != nullptr || protocol != IPPROTO_TCP || tcpHeader == nullptr) {
        return key;
    }

    key.srcAddr = ipHeader->SrcAddr;
    key.dstAddr = ipHeader->DstAddr;
    key.srcPort = tcpHeader->SrcPort;
    key.dstPort = tcpHeader->DstPort;

    return key;
}

void NetworkInterceptor::CleanupExpiredStreams()
{
    std::lock_guard<std::mutex> lock(tcpStreamsMutex);
    auto now = std::chrono::steady_clock::now();

    auto it = tcpStreams.begin();
    while (it != tcpStreams.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.lastActivity).count();

        if (elapsed > STREAM_TIMEOUT_SECONDS || tcpStreams.size() > MAX_STREAMS) {
            it = tcpStreams.erase(it);
        }
        else {
            ++it;
        }
    }
}

// 修复 ProcessTcpSegment 函数
bool NetworkInterceptor::ProcessTcpSegment(
    PVOID packet,
    UINT packetLength,
    const WINDIVERT_ADDRESS& address)
{
    WINDIVERT_IPHDR* ipHeader = nullptr;
    WINDIVERT_IPV6HDR* ipv6Header = nullptr;
    WINDIVERT_TCPHDR* tcpHeader = nullptr;
    WINDIVERT_UDPHDR* udpHeader = nullptr;
    PVOID payload = nullptr;
    UINT payloadLength = 0;
    UINT8 protocol = 0;

    if (!GetPacketHeaders(
        packet,
        packetLength,
        &ipHeader,
        &ipv6Header,
        &tcpHeader,
        &udpHeader,
        &payload,
        &payloadLength,
        &protocol)) {
        return false;
    }

    if (ipHeader == nullptr || ipv6Header != nullptr || protocol != IPPROTO_TCP || tcpHeader == nullptr) {
        return false;
    }

    TcpStreamKey key = CreateStreamKey(packet, packetLength);
    if (key.srcAddr == 0 && key.dstAddr == 0) {
        return false;
    }

    uint32_t seqNum = ntohl(tcpHeader->SeqNum);
    uint32_t ackNum = ntohl(tcpHeader->AckNum);
    bool isSyn = (tcpHeader->Syn != 0);
    bool isFin = (tcpHeader->Fin != 0);
    bool isRst = (tcpHeader->Rst != 0);
    bool isAck = (tcpHeader->Ack != 0);

    // RST: 清理流状态
    if (isRst) {
        std::lock_guard<std::mutex> lock(tcpStreamsMutex);
        tcpStreams.erase(key);
        // 同时清理反向流
        TcpStreamKey revKey{};
        revKey.srcAddr = key.dstAddr;
        revKey.dstAddr = key.srcAddr;
        revKey.srcPort = key.dstPort;
        revKey.dstPort = key.srcPort;
        tcpStreams.erase(revKey);
        return false;
    }

    // FIN: 标记流结束，但不立即删除
    if (isFin) {
        std::lock_guard<std::mutex> lock(tcpStreamsMutex);
        auto it = tcpStreams.find(key);
        if (it != tcpStreams.end()) {
            // 标记流即将关闭
            it->second.lastActivity = std::chrono::steady_clock::now() -
                std::chrono::seconds(STREAM_TIMEOUT_SECONDS);  // 让清理线程尽快删除
        }
        return false;  // 允许FIN包通过
    }

    std::lock_guard<std::mutex> lock(tcpStreamsMutex);

    // 检查是否已阻止
    auto it = tcpStreams.find(key);
    if (it != tcpStreams.end() && it->second.isBlocked) {
        return true;  // 继续阻止
    }

    // 检查反向流是否已阻止
    TcpStreamKey revKey{};
    revKey.srcAddr = key.dstAddr;
    revKey.dstAddr = key.srcAddr;
    revKey.srcPort = key.dstPort;
    revKey.dstPort = key.srcPort;

    auto revIt = tcpStreams.find(revKey);
    if (revIt != tcpStreams.end() && revIt->second.isBlocked) {
        if (it == tcpStreams.end()) {
            TcpStream s;
            s.key = key;
            s.isBlocked = true;
            s.lastActivity = std::chrono::steady_clock::now();
            tcpStreams[key] = std::move(s);
        }
        else {
            it->second.isBlocked = true;
        }
        return true;
    }

    // 处理新的SYN - 清除旧状态
    if (isSyn && !isAck) {
        // 这是新的连接请求，清除所有旧状态
        if (it != tcpStreams.end()) {
            // 重置流状态
            it->second.nextExpectedSeq = seqNum + 1;
            it->second.lastAck = ackNum;
            it->second.lastActivity = std::chrono::steady_clock::now();
            it->second.handshakeComplete = false;
            it->second.isTls = false;
            it->second.sniExtracted = false;
            it->second.extractedSni.clear();
            it->second.bytesProcessed = 0;
            it->second.reassembledData.clear();
            it->second.pendingSegments.clear();
            it->second.isBlocked = false;
            return false;
        }
        else {
            // 创建新流
            TcpStream newStream;
            newStream.key = key;
            newStream.nextExpectedSeq = seqNum + 1;
            newStream.lastAck = ackNum;
            newStream.lastActivity = std::chrono::steady_clock::now();
            newStream.handshakeComplete = false;
            newStream.isTls = false;
            newStream.sniExtracted = false;
            newStream.bytesProcessed = 0;
            newStream.maxDataSize = MAX_STREAM_DATA;
            tcpStreams[key] = newStream;
            return false;
        }
    }

    // 创建新流（如果没有SYN但有数据）
    if (it == tcpStreams.end()) {
        if (tcpStreams.size() >= MAX_STREAMS) {
            CleanupExpiredStreams();
            if (tcpStreams.size() >= MAX_STREAMS) {
                return false;
            }
        }

        TcpStream newStream;
        newStream.key = key;
        newStream.nextExpectedSeq = seqNum;
        newStream.lastAck = ackNum;
        newStream.lastActivity = std::chrono::steady_clock::now();
        newStream.handshakeComplete = (payloadLength > 0);  // 有数据就认为握手完成
        newStream.isTls = false;
        newStream.sniExtracted = false;
        newStream.bytesProcessed = 0;
        newStream.maxDataSize = MAX_STREAM_DATA;
        tcpStreams[key] = newStream;
        it = tcpStreams.find(key);
    }

    TcpStream& stream = it->second;
    stream.lastActivity = std::chrono::steady_clock::now();

    // SYN-ACK from server
    if (isSyn && isAck) {
        stream.nextExpectedSeq = seqNum + 1;
        return false;
    }

    // ACK - 完成握手
    if (isAck && !isSyn && !isFin && payloadLength == 0) {
        stream.handshakeComplete = true;
        return false;
    }

    // 处理数据
    if (payloadLength > 0) {
        stream.handshakeComplete = true;

        if (seqNum == stream.nextExpectedSeq) {
            if (stream.reassembledData.size() + payloadLength <= stream.maxDataSize) {
                const BYTE* data = static_cast<const BYTE*>(payload);
                stream.reassembledData.insert(stream.reassembledData.end(), data, data + payloadLength);
                stream.nextExpectedSeq = seqNum + payloadLength;
                stream.bytesProcessed += payloadLength;

                if (!stream.sniExtracted) {
                    std::string extracted;
                    size_t consumed = 0;

                    // 检测TLS
                    if (!stream.isTls && stream.reassembledData.size() >= 3) {
                        const BYTE* firstBytes = stream.reassembledData.data();
                        if (firstBytes[0] == 0x16 && firstBytes[1] == 0x03) {
                            stream.isTls = true;
                        }
                    }

                    if (stream.isTls) {
                        if (ExtractSNIFromStream(stream.reassembledData, extracted, consumed)) {
                            stream.extractedSni = extracted;
                            stream.sniExtracted = true;

                            bool isBlocked = IsBlocked(extracted);
                            bool isWhitelisted = IsWhitelisted(extracted);

                            if (isBlocked && !isWhitelisted) {
                                DWORD pid = GetPacketProcessId(packet, packetLength);
                                std::string processName = GetProcessName(pid);
                                {
                                    std::lock_guard<std::mutex> statsLock(statsMutex);
                                    stats.blockedPackets++;
                                    stats.blockedDomains[extracted]++;
                                }
                                if (blockCallback) {
                                    blockCallback(extracted, processName, pid);
                                }

                                stream.isBlocked = true;
                                if (revIt == tcpStreams.end()) {
                                    TcpStream rev;
                                    rev.key = revKey;
                                    rev.isBlocked = true;
                                    rev.lastActivity = std::chrono::steady_clock::now();
                                    tcpStreams[revKey] = std::move(rev);
                                }
                                else {
                                    revIt->second.isBlocked = true;
                                }
                                return true;
                            }
                        }
                    }
                    else {
                        if (ExtractHTTPHostFromStream(stream.reassembledData, extracted, consumed)) {
                            stream.extractedSni = extracted;
                            stream.sniExtracted = true;

                            bool isBlocked = IsBlocked(extracted);
                            bool isWhitelisted = IsWhitelisted(extracted);

                            if (isBlocked && !isWhitelisted) {
                                DWORD pid = GetPacketProcessId(packet, packetLength);
                                std::string processName = GetProcessName(pid);
                                {
                                    std::lock_guard<std::mutex> statsLock(statsMutex);
                                    stats.blockedPackets++;
                                    stats.blockedDomains[extracted]++;
                                }
                                if (blockCallback) {
                                    blockCallback(extracted, processName, pid);
                                }

                                stream.isBlocked = true;
                                if (revIt == tcpStreams.end()) {
                                    TcpStream rev;
                                    rev.key = revKey;
                                    rev.isBlocked = true;
                                    rev.lastActivity = std::chrono::steady_clock::now();
                                    tcpStreams[revKey] = std::move(rev);
                                }
                                else {
                                    revIt->second.isBlocked = true;
                                }
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    return false;
}

bool NetworkInterceptor::ExtractSNIFromStream(const std::vector<BYTE>& data, std::string& sni, size_t& consumed)
{
    if (data.size() < 5) {
        consumed = 0;
        return false;
    }

    const BYTE* rawData = data.data();
    size_t remaining = data.size();
    size_t offset = 0;

    bool found = false;
    size_t tlsOffset = 0;
    while (tlsOffset + 5 <= remaining) {
        if (rawData[tlsOffset] == 0x16 && rawData[tlsOffset + 1] == 0x03) {
            found = true;
            break;
        }
        tlsOffset++;
    }

    if (!found) {
        consumed = 0;
        return false;
    }

    offset = tlsOffset;
    remaining = remaining - offset;
    rawData = rawData + offset;

    if (remaining < 5) {
        consumed = 0;
        return false;
    }

    const size_t recordLength = (static_cast<size_t>(rawData[3]) << 8) | rawData[4];
    if (recordLength + 5 > remaining) {
        consumed = 0;
        return false;
    }

    size_t pos = 5;

    if (pos + 4 > remaining || rawData[pos] != 0x01) {
        consumed = 0;
        return false;
    }

    const size_t helloLength =
        (static_cast<size_t>(rawData[pos + 1]) << 16) |
        (static_cast<size_t>(rawData[pos + 2]) << 8) |
        rawData[pos + 3];

    pos += 4;

    if (helloLength > remaining - pos || helloLength < 34) {
        consumed = 0;
        return false;
    }

    const size_t helloEnd = pos + helloLength;

    if (pos + 34 > helloEnd) {
        consumed = 0;
        return false;
    }
    pos += 34;

    if (pos + 1 > helloEnd) {
        consumed = 0;
        return false;
    }
    const size_t sessionLength = rawData[pos++];
    if (sessionLength > helloEnd - pos) {
        consumed = 0;
        return false;
    }
    pos += sessionLength;

    if (pos + 2 > helloEnd) {
        consumed = 0;
        return false;
    }
    const size_t cipherLength = (static_cast<size_t>(rawData[pos]) << 8) | rawData[pos + 1];
    pos += 2;
    if (cipherLength > helloEnd - pos) {
        consumed = 0;
        return false;
    }
    pos += cipherLength;

    if (pos + 1 > helloEnd) {
        consumed = 0;
        return false;
    }
    const size_t compressionLength = rawData[pos++];
    if (compressionLength > helloEnd - pos) {
        consumed = 0;
        return false;
    }
    pos += compressionLength;

    if (pos + 2 > helloEnd) {
        consumed = 0;
        return false;
    }
    const size_t extensionsLength = (static_cast<size_t>(rawData[pos]) << 8) | rawData[pos + 1];
    pos += 2;
    if (extensionsLength > helloEnd - pos) {
        consumed = 0;
        return false;
    }

    const size_t extensionsEnd = pos + extensionsLength;

    while (pos + 4 <= extensionsEnd) {
        const uint16_t extensionType =
            static_cast<uint16_t>(
                (static_cast<uint16_t>(rawData[pos]) << 8) |
                rawData[pos + 1]);

        const size_t extensionLength =
            (static_cast<size_t>(rawData[pos + 2]) << 8) |
            rawData[pos + 3];

        pos += 4;

        if (extensionLength > extensionsEnd - pos) {
            break;
        }

        if (extensionType == 0x0000) {
            if (extensionLength < 5) {
                break;
            }

            const size_t extensionEnd = pos + extensionLength;
            const size_t listLength = (static_cast<size_t>(rawData[pos]) << 8) | rawData[pos + 1];
            pos += 2;

            if (listLength > extensionEnd - pos || listLength < 3) {
                break;
            }

            const BYTE nameType = rawData[pos++];
            const size_t nameLength = (static_cast<size_t>(rawData[pos]) << 8) | rawData[pos + 1];
            pos += 2;

            if (nameType != 0 || nameLength == 0 || nameLength > extensionEnd - pos) {
                break;
            }

            sni.assign(reinterpret_cast<const char*>(rawData + pos), nameLength);
            sni = NormalizeDomain(sni);

            consumed = pos + nameLength;
            return !sni.empty();
        }

        pos += extensionLength;
    }

    consumed = 0;
    return false;
}

bool NetworkInterceptor::ExtractHTTPHostFromStream(const std::vector<BYTE>& data, std::string& host, size_t& consumed)
{
    if (data.size() < 10) {
        consumed = 0;
        return false;
    }

    const char* rawData = reinterpret_cast<const char*>(data.data());
    size_t size = data.size();

    size_t searchLimit = std::min<size_t>(size, 4096);

    for (size_t i = 0; i < searchLimit - 5; i++) {
        if ((rawData[i] == 'H' || rawData[i] == 'h') &&
            (rawData[i + 1] == 'O' || rawData[i + 1] == 'o') &&
            (rawData[i + 2] == 'S' || rawData[i + 2] == 's') &&
            (rawData[i + 3] == 'T' || rawData[i + 3] == 't') &&
            rawData[i + 4] == ':' &&
            (rawData[i + 5] == ' ' || rawData[i + 5] == '\t')) {

            size_t hostStart = i + 6;
            while (hostStart < searchLimit &&
                (rawData[hostStart] == ' ' || rawData[hostStart] == '\t')) {
                hostStart++;
            }

            if (hostStart >= searchLimit) {
                break;
            }

            size_t hostEnd = hostStart;
            while (hostEnd < searchLimit &&
                rawData[hostEnd] != '\r' &&
                rawData[hostEnd] != '\n') {
                hostEnd++;
            }

            if (hostEnd > hostStart) {
                host = std::string(rawData + hostStart, hostEnd - hostStart);
                size_t colon = host.find(':');
                if (colon != std::string::npos) {
                    host = host.substr(0, colon);
                }
                host = NormalizeDomain(host);
                consumed = hostEnd + 2;
                return !host.empty();
            }
            break;
        }
    }

    consumed = 0;
    return false;
}

// ============== IP Cache ==============

void NetworkInterceptor::AddDomainIPCache(
    const std::string& domain,
    const std::string& ip)
{
    if (domain.empty() || ip.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(ipCacheMutex);
    ipDomainCache[ip] = NormalizeDomain(domain);

    if (ipDomainCache.size() > 500) {
        auto it = ipDomainCache.begin();
        const size_t removeCount = ipDomainCache.size() / 2;
        for (size_t i = 0; i < removeCount && it != ipDomainCache.end(); ++i) {
            it = ipDomainCache.erase(it);
        }
    }
}

std::string NetworkInterceptor::GetDomainByIP(const std::string& ip)
{
    std::lock_guard<std::mutex> lock(ipCacheMutex);
    auto it = ipDomainCache.find(ip);
    return it != ipDomainCache.end() ? it->second : "";
}

void NetworkInterceptor::CleanIPCache()
{
    std::lock_guard<std::mutex> lock(ipCacheMutex);
    if (ipDomainCache.size() <= 100) {
        return;
    }
    auto it = ipDomainCache.begin();
    while (ipDomainCache.size() > 100 && it != ipDomainCache.end()) {
        it = ipDomainCache.erase(it);
    }
}

// ============== PID Cache ==============

void NetworkInterceptor::UpdatePidCache()
{
    std::lock_guard<std::mutex> lock(pidCacheMutex);
    const auto now = std::chrono::steady_clock::now();

    if (!portPidCache.empty()) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastPidCacheUpdate).count();
        if (elapsed < PID_CACHE_TTL) {
            return;
        }
    }

    portPidCache.clear();

    HMODULE ipHelper = GetModuleHandleA("iphlpapi.dll");
    if (ipHelper == nullptr) {
        lastPidCacheUpdate = now;
        return;
    }

    using GetExtendedTcpTableFunction =
        DWORD(WINAPI*)(PVOID, PDWORD, BOOL, ULONG, TCP_TABLE_CLASS, ULONG);

    auto getExtendedTcpTable =
        reinterpret_cast<GetExtendedTcpTableFunction>(
            GetProcAddress(ipHelper, "GetExtendedTcpTable"));

    if (getExtendedTcpTable != nullptr) {
        DWORD size = 0;
        getExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);

        if (size > 0) {
            std::vector<BYTE> buffer(size);
            if (getExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
                auto* table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const auto& row = table->table[i];
                    const UINT port = ntohs(static_cast<USHORT>(row.dwLocalPort));
                    if (port == 0) continue;
                    portPidCache[port] = { row.dwOwningPid, now };
                }
            }
        }
    }

    using GetExtendedUdpTableFunction =
        DWORD(WINAPI*)(PVOID, PDWORD, BOOL, ULONG, UDP_TABLE_CLASS, ULONG);

    auto getExtendedUdpTable =
        reinterpret_cast<GetExtendedUdpTableFunction>(
            GetProcAddress(ipHelper, "GetExtendedUdpTable"));

    if (getExtendedUdpTable != nullptr) {
        DWORD size = 0;
        getExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);

        if (size > 0) {
            std::vector<BYTE> buffer(size);
            if (getExtendedUdpTable(buffer.data(), &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
                auto* table = reinterpret_cast<PMIB_UDPTABLE_OWNER_PID>(buffer.data());
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const auto& row = table->table[i];
                    const UINT port = ntohs(static_cast<USHORT>(row.dwLocalPort));
                    if (port == 0) continue;
                    portPidCache[port] = { row.dwOwningPid, now };
                }
            }
        }
    }

    lastPidCacheUpdate = now;
}

DWORD NetworkInterceptor::GetPidFromCache(UINT port)
{
    std::lock_guard<std::mutex> lock(pidCacheMutex);
    auto it = portPidCache.find(port);
    if (it == portPidCache.end()) {
        return 0;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.timestamp).count();
    if (elapsed >= PID_CACHE_TTL) {
        return 0;
    }
    return it->second.pid;
}

// ============== Constructor / Destructor ==============

NetworkInterceptor::NetworkInterceptor()
    : winDivertHandle(INVALID_HANDLE_VALUE)
    , isRunning(false)
    , lastIPCacheCleanup(std::chrono::steady_clock::now())
    , lastCacheCleanup(std::chrono::steady_clock::now())
    , lastProcessUpdate(std::chrono::steady_clock::now())
    , lastPidCacheUpdate(
        std::chrono::steady_clock::now() -
        std::chrono::milliseconds(PID_CACHE_TTL))
{
    WSADATA wsaData{};
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

NetworkInterceptor::~NetworkInterceptor()
{
    Stop();
    WSACleanup();

    if (g_NetworkInterceptor == this) {
        g_NetworkInterceptor = nullptr;
    }
}

NetworkInterceptor* NetworkInterceptor::GetInstance()
{
    if (instance == nullptr) {
        instance = new NetworkInterceptor();
        g_NetworkInterceptor = instance;
    }
    return instance;
}

// ============== Start / Stop ==============

bool NetworkInterceptor::Start()
{
    if (isRunning.load()) {
        return true;
    }

    const std::string filter =
        "outbound && !impostor && "
        "ip.DstAddr != 127.0.0.1 && "
        "ip.DstAddr != 0.0.0.0 && "
        "("
        "tcp || "
        "udp.PayloadLength > 0"
        ")";

    winDivertHandle = WinDivertOpen(
        filter.c_str(),
        WINDIVERT_LAYER_NETWORK,
        0,
        0);

    if (winDivertHandle == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "WinDivertOpen failed: %lu\n", GetLastError());
        return false;
    }

    isRunning.store(true);

    interceptorThread = std::thread(&NetworkInterceptor::InterceptorThread, this);
    statsThread = std::thread(&NetworkInterceptor::StatsThread, this);

    return true;
}

void NetworkInterceptor::Stop()
{
    const bool wasRunning = isRunning.exchange(false);

    if (winDivertHandle != INVALID_HANDLE_VALUE) {
        WinDivertClose(winDivertHandle);
        winDivertHandle = INVALID_HANDLE_VALUE;
    }

    if (interceptorThread.joinable()) {
        interceptorThread.join();
    }

    if (statsThread.joinable()) {
        statsThread.join();
    }

    if (wasRunning) {
        ClearAllBlocks();
    }
}

// ============== Interceptor Thread ==============

void NetworkInterceptor::InterceptorThread()
{
    std::vector<BYTE> packet(0xFFFF);

    while (isRunning.load()) {
        UINT packetLength = 0;
        WINDIVERT_ADDRESS address{};
        memset(&address, 0, sizeof(address));

        if (!WinDivertRecv(
            winDivertHandle,
            packet.data(),
            static_cast<UINT>(packet.size()),
            &packetLength,
            &address)) {
            if (!isRunning.load()) {
                break;
            }
            continue;
        }

        if (packetLength == 0) {
            continue;
        }

        g_debugPacketCount++;

        ProcessPacket(packet.data(), packetLength, address);
    }
}

// ============== Packet Processing ==============

// 在 ProcessPacket 函数中，处理TCP连接时添加IP反查

void NetworkInterceptor::ProcessPacket(
    PVOID packet,
    UINT packetLength,
    const WINDIVERT_ADDRESS& address)
{
    WINDIVERT_IPHDR* ipHeader = nullptr;
    WINDIVERT_IPV6HDR* ipv6Header = nullptr;
    WINDIVERT_TCPHDR* tcpHeader = nullptr;
    WINDIVERT_UDPHDR* udpHeader = nullptr;
    PVOID payload = nullptr;
    UINT payloadLength = 0;
    UINT8 protocol = 0;

    auto forward = [&]()
        {
            {
                std::lock_guard<std::mutex> lock(statsMutex);
                ++stats.totalPackets;
                ++stats.allowedPackets;
            }
            SendPacket(winDivertHandle, packet, packetLength, &address);
        };

    if (!GetPacketHeaders(
        packet,
        packetLength,
        &ipHeader,
        &ipv6Header,
        &tcpHeader,
        &udpHeader,
        &payload,
        &payloadLength,
        &protocol)) {
        forward();
        return;
    }

    if (ipHeader == nullptr || ipv6Header != nullptr) {
        forward();
        return;
    }

    char ipBuffer[INET_ADDRSTRLEN]{};
    in_addr destinationAddress{};
    destinationAddress.s_addr = ipHeader->DstAddr;

    if (inet_ntop(AF_INET, &destinationAddress, ipBuffer, sizeof(ipBuffer)) == nullptr) {
        forward();
        return;
    }

    const std::string destinationIP(ipBuffer);

    // 检查IP黑名单
    if (ShouldBlockIP(destinationIP)) {
        if (protocol == IPPROTO_TCP && tcpHeader != nullptr) {
            SendTCPReset(packet, packetLength);
        }
        return;
    }

    // ====== UDP (DNS) ======
    if (protocol == IPPROTO_UDP && udpHeader != nullptr) {
        const UINT destinationPort = ntohs(udpHeader->DstPort);

        if (destinationPort == 53) {
            std::string domain;
            uint16_t queryId = 0;

            if (ExtractDNSDomain(packet, packetLength, domain, queryId)) {
                domain = NormalizeDomain(domain);

                if (IsBlocked(domain) && !IsWhitelisted(domain)) {
                    // 记录域名到IP的映射（用于后续IP反查）
                    AddDomainIPCache(domain, "127.0.0.1");

                    // 发送DNS欺骗响应
                    if (!SendDNSResponseToRequester(
                        packet,
                        packetLength,
                        domain,
                        "127.0.0.1",
                        queryId)) {
                        forward();
                    }
                    return;
                }
            }
        }

        forward();
        return;
    }

    // ====== TCP ======
    if (protocol == IPPROTO_TCP && tcpHeader != nullptr) {
        bool shouldBlock = ProcessTcpSegment(packet, packetLength, address);
        if (shouldBlock) {
            SendTCPReset(packet, packetLength);

            // 清理流状态，让下次连接能正常工作
            TcpStreamKey key = CreateStreamKey(packet, packetLength);
            TcpStreamKey revKey = key;
            std::swap(revKey.srcAddr, revKey.dstAddr);
            std::swap(revKey.srcPort, revKey.dstPort);

            {
                std::lock_guard<std::mutex> lock(tcpStreamsMutex);
                // 标记为已阻止，但设置较短超时
                auto it = tcpStreams.find(key);
                if (it != tcpStreams.end()) {
                    it->second.isBlocked = true;
                    it->second.lastActivity = std::chrono::steady_clock::now() -
                        std::chrono::seconds(STREAM_TIMEOUT_SECONDS - 5);  // 5秒后清理
                }

                auto revIt = tcpStreams.find(revKey);
                if (revIt != tcpStreams.end()) {
                    revIt->second.isBlocked = true;
                    revIt->second.lastActivity = std::chrono::steady_clock::now() -
                        std::chrono::seconds(STREAM_TIMEOUT_SECONDS - 5);
                }
            }

            {
                std::lock_guard<std::mutex> lock(statsMutex);
                ++stats.totalPackets;
                ++stats.blockedPackets;
            }
            return;
        }
        forward();
        return;
    }

    forward();
}

// ============== DNS Extraction ==============

bool NetworkInterceptor::ExtractDNSDomain(
    PVOID packet,
    UINT packetLength,
    std::string& domain,
    uint16_t& queryId)
{
    WINDIVERT_IPHDR* ipHeader = nullptr;
    WINDIVERT_IPV6HDR* ipv6Header = nullptr;
    WINDIVERT_TCPHDR* tcpHeader = nullptr;
    WINDIVERT_UDPHDR* udpHeader = nullptr;
    PVOID payload = nullptr;
    UINT payloadLength = 0;
    UINT8 protocol = 0;

    if (!GetPacketHeaders(
        packet,
        packetLength,
        &ipHeader,
        &ipv6Header,
        &tcpHeader,
        &udpHeader,
        &payload,
        &payloadLength,
        &protocol)) {
        return false;
    }

    if (ipHeader == nullptr ||
        ipv6Header != nullptr ||
        protocol != IPPROTO_UDP ||
        udpHeader == nullptr ||
        payload == nullptr ||
        payloadLength < 12) {
        return false;
    }

    const BYTE* dns = static_cast<const BYTE*>(payload);

    queryId = ntohs(*reinterpret_cast<const uint16_t*>(dns));

    const uint16_t flags =
        ntohs(*reinterpret_cast<const uint16_t*>(dns + 2));

    if ((flags & 0x8000) != 0) {
        return false;
    }

    const uint16_t questionCount =
        ntohs(*reinterpret_cast<const uint16_t*>(dns + 4));

    if (questionCount == 0) {
        return false;
    }

    size_t offset = 12;
    std::string result;

    while (offset < payloadLength) {
        const BYTE labelLength = dns[offset++];

        if (labelLength == 0) {
            break;
        }

        if ((labelLength & 0xC0) != 0) {
            return false;
        }

        if (labelLength > 63 ||
            offset + labelLength > payloadLength) {
            return false;
        }

        if (!result.empty()) {
            result.push_back('.');
        }

        result.append(
            reinterpret_cast<const char*>(dns + offset),
            labelLength);

        offset += labelLength;

        if (result.size() > 253) {
            return false;
        }
    }

    if (result.empty() ||
        offset + 4 > payloadLength) {
        return false;
    }

    domain = NormalizeDomain(result);
    return !domain.empty();
}

// ============== HTTP Host (single packet, legacy) ==============

bool NetworkInterceptor::ExtractHTTPHost(
    PVOID packet,
    UINT packetLength,
    std::string& host)
{
    WINDIVERT_IPHDR* ipHeader = nullptr;
    WINDIVERT_IPV6HDR* ipv6Header = nullptr;
    WINDIVERT_TCPHDR* tcpHeader = nullptr;
    WINDIVERT_UDPHDR* udpHeader = nullptr;
    PVOID payload = nullptr;
    UINT payloadLength = 0;
    UINT8 protocol = 0;

    if (!GetPacketHeaders(
        packet,
        packetLength,
        &ipHeader,
        &ipv6Header,
        &tcpHeader,
        &udpHeader,
        &payload,
        &payloadLength,
        &protocol)) {
        return false;
    }

    if (protocol != IPPROTO_TCP ||
        tcpHeader == nullptr ||
        payload == nullptr ||
        payloadLength == 0) {
        return false;
    }

    const UINT dataLength = std::min(payloadLength, static_cast<UINT>(8192));
    std::string data(static_cast<const char*>(payload), dataLength);
    std::string lower = ToLower(data);

    size_t lineStart = 0;
    while (lineStart < lower.size()) {
        size_t lineEnd = lower.find("\r\n", lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = lower.size();
        }

        constexpr const char* prefix = "host:";
        constexpr size_t prefixLength = 5;

        if (lineEnd >= lineStart + prefixLength &&
            lower.compare(lineStart, prefixLength, prefix) == 0) {
            size_t valueStart = lineStart + prefixLength;
            while (valueStart < lineEnd &&
                (data[valueStart] == ' ' || data[valueStart] == '\t')) {
                ++valueStart;
            }

            if (valueStart >= lineEnd) {
                return false;
            }

            host = data.substr(valueStart, lineEnd - valueStart);

            size_t colon = host.find(':');
            if (colon != std::string::npos) {
                host.resize(colon);
            }

            host = NormalizeDomain(host);
            return !host.empty();
        }

        if (lineEnd == lower.size()) {
            break;
        }
        lineStart = lineEnd + 2;
    }

    return false;
}

// ============== TLS SNI (single packet, legacy) ==============

bool NetworkInterceptor::ExtractTLSSNI(
    PVOID packet,
    UINT packetLength,
    std::string& sni)
{
    WINDIVERT_IPHDR* ipHeader = nullptr;
    WINDIVERT_IPV6HDR* ipv6Header = nullptr;
    WINDIVERT_TCPHDR* tcpHeader = nullptr;
    WINDIVERT_UDPHDR* udpHeader = nullptr;
    PVOID payload = nullptr;
    UINT payloadLength = 0;
    UINT8 protocol = 0;

    if (!GetPacketHeaders(
        packet,
        packetLength,
        &ipHeader,
        &ipv6Header,
        &tcpHeader,
        &udpHeader,
        &payload,
        &payloadLength,
        &protocol)) {
        return false;
    }

    if (protocol != IPPROTO_TCP ||
        tcpHeader == nullptr ||
        payload == nullptr ||
        payloadLength < 5) {
        return false;
    }

    const BYTE* data = static_cast<const BYTE*>(payload);
    size_t remaining = payloadLength;

    size_t tlsOffset = 0;
    bool found = false;
    while (tlsOffset + 5 <= remaining) {
        if (data[tlsOffset] == 0x16 && data[tlsOffset + 1] == 0x03) {
            found = true;
            break;
        }
        tlsOffset++;
    }

    if (!found) {
        return false;
    }

    data = data + tlsOffset;
    remaining = remaining - tlsOffset;

    if (remaining < 5) return false;

    const size_t recordLength = (static_cast<size_t>(data[3]) << 8) | data[4];
    if (recordLength + 5 > remaining) {
        return false;
    }

    size_t offset = 5;

    if (offset + 4 > remaining || data[offset] != 0x01) {
        return false;
    }

    const size_t helloLength =
        (static_cast<size_t>(data[offset + 1]) << 16) |
        (static_cast<size_t>(data[offset + 2]) << 8) |
        data[offset + 3];

    offset += 4;

    if (helloLength > remaining - offset || helloLength < 34) {
        return false;
    }

    const size_t helloEnd = offset + helloLength;

    if (offset + 34 > helloEnd) {
        return false;
    }
    offset += 34;

    if (offset + 1 > helloEnd) {
        return false;
    }
    const size_t sessionLength = data[offset++];
    if (sessionLength > helloEnd - offset) {
        return false;
    }
    offset += sessionLength;

    if (offset + 2 > helloEnd) {
        return false;
    }
    const size_t cipherLength = (static_cast<size_t>(data[offset]) << 8) | data[offset + 1];
    offset += 2;
    if (cipherLength > helloEnd - offset) {
        return false;
    }
    offset += cipherLength;

    if (offset + 1 > helloEnd) {
        return false;
    }
    const size_t compressionLength = data[offset++];
    if (compressionLength > helloEnd - offset) {
        return false;
    }
    offset += compressionLength;

    if (offset + 2 > helloEnd) {
        return false;
    }
    const size_t extensionsLength = (static_cast<size_t>(data[offset]) << 8) | data[offset + 1];
    offset += 2;
    if (extensionsLength > helloEnd - offset) {
        return false;
    }

    const size_t extensionsEnd = offset + extensionsLength;

    while (offset + 4 <= extensionsEnd) {
        const uint16_t extensionType =
            static_cast<uint16_t>(
                (static_cast<uint16_t>(data[offset]) << 8) |
                data[offset + 1]);

        const size_t extensionLength =
            (static_cast<size_t>(data[offset + 2]) << 8) |
            data[offset + 3];

        offset += 4;

        if (extensionLength > extensionsEnd - offset) {
            return false;
        }

        if (extensionType == 0x0000) {
            if (extensionLength < 5) {
                return false;
            }

            const size_t extensionEnd = offset + extensionLength;
            const size_t listLength = (static_cast<size_t>(data[offset]) << 8) | data[offset + 1];
            offset += 2;

            if (listLength > extensionEnd - offset || listLength < 3) {
                return false;
            }

            const BYTE nameType = data[offset++];
            const size_t nameLength = (static_cast<size_t>(data[offset]) << 8) | data[offset + 1];
            offset += 2;

            if (nameType != 0 || nameLength == 0 || nameLength > extensionEnd - offset) {
                return false;
            }

            sni.assign(reinterpret_cast<const char*>(data + offset), nameLength);
            sni = NormalizeDomain(sni);
            return !sni.empty();
        }

        offset += extensionLength;
    }

    return false;
}

// ============== DNS Spoof Response ==============

bool NetworkInterceptor::SendDNSResponseToRequester(
    PVOID packet,
    UINT packetLength,
    const std::string& domain,
    const std::string& fakeIP,
    uint16_t queryId)
{
    WINDIVERT_IPHDR* ipHeader = nullptr;
    WINDIVERT_IPV6HDR* ipv6Header = nullptr;
    WINDIVERT_TCPHDR* tcpHeader = nullptr;
    WINDIVERT_UDPHDR* udpHeader = nullptr;
    PVOID payload = nullptr;
    UINT payloadLength = 0;
    UINT8 protocol = 0;

    if (!GetPacketHeaders(
        packet,
        packetLength,
        &ipHeader,
        &ipv6Header,
        &tcpHeader,
        &udpHeader,
        &payload,
        &payloadLength,
        &protocol)) {
        return false;
    }

    if (ipHeader == nullptr ||
        ipv6Header != nullptr ||
        protocol != IPPROTO_UDP ||
        udpHeader == nullptr ||
        payload == nullptr ||
        payloadLength < 12 ||
        !IsValidIPv4String(fakeIP)) {
        return false;
    }

    const BYTE* requestDNS = static_cast<const BYTE*>(payload);

    size_t qnameEnd = 12;
    while (qnameEnd < payloadLength) {
        const BYTE length = requestDNS[qnameEnd++];
        if (length == 0) break;
        if ((length & 0xC0) != 0 || length > 63 || qnameEnd + length > payloadLength) {
            return false;
        }
        qnameEnd += length;
    }

    if (qnameEnd == payloadLength || qnameEnd + 4 > payloadLength) {
        return false;
    }

    const size_t questionLength = qnameEnd + 1 + 4 - 12;

    std::vector<BYTE> dnsResponse(12 + questionLength + 16, 0);

    *reinterpret_cast<uint16_t*>(dnsResponse.data()) = htons(queryId);
    *reinterpret_cast<uint16_t*>(dnsResponse.data() + 2) = htons(0x8180);
    *reinterpret_cast<uint16_t*>(dnsResponse.data() + 4) = htons(1);
    *reinterpret_cast<uint16_t*>(dnsResponse.data() + 6) = htons(1);

    std::memcpy(dnsResponse.data() + 12, requestDNS + 12, questionLength);

    BYTE* answer = dnsResponse.data() + 12 + questionLength;
    *reinterpret_cast<uint16_t*>(answer) = htons(0xC00C);
    *reinterpret_cast<uint16_t*>(answer + 2) = htons(1);
    *reinterpret_cast<uint16_t*>(answer + 4) = htons(1);
    *reinterpret_cast<uint32_t*>(answer + 6) = htonl(60);
    *reinterpret_cast<uint16_t*>(answer + 10) = htons(4);

    in_addr fakeAddress{};
    inet_pton(AF_INET, fakeIP.c_str(), &fakeAddress);
    std::memcpy(answer + 12, &fakeAddress.s_addr, sizeof(fakeAddress.s_addr));

    const UINT dnsLength = static_cast<UINT>(dnsResponse.size());
    const UINT ipLength = sizeof(WINDIVERT_IPHDR);
    const UINT udpLength = sizeof(WINDIVERT_UDPHDR) + dnsLength;
    const UINT totalLength = ipLength + udpLength;

    std::vector<BYTE> response(totalLength, 0);

    auto* responseIP = reinterpret_cast<WINDIVERT_IPHDR*>(response.data());
    auto* responseUDP = reinterpret_cast<WINDIVERT_UDPHDR*>(response.data() + ipLength);

    responseIP->Version = 4;
    responseIP->HdrLength = 5;
    responseIP->Length = htons(static_cast<USHORT>(totalLength));
    responseIP->Id = htons(static_cast<USHORT>(GetTickCount()));
    responseIP->TTL = 64;
    responseIP->Protocol = IPPROTO_UDP;
    responseIP->SrcAddr = ipHeader->DstAddr;
    responseIP->DstAddr = ipHeader->SrcAddr;

    responseUDP->SrcPort = udpHeader->DstPort;
    responseUDP->DstPort = udpHeader->SrcPort;
    responseUDP->Length = htons(static_cast<USHORT>(udpLength));

    std::memcpy(response.data() + ipLength + sizeof(WINDIVERT_UDPHDR), dnsResponse.data(), dnsLength);

    WinDivertHelperCalcChecksums(response.data(), totalLength, nullptr, 0);

    WINDIVERT_ADDRESS responseAddress{};
    memset(&responseAddress, 0, sizeof(responseAddress));
    responseAddress.Layer = WINDIVERT_LAYER_NETWORK;
    responseAddress.Network.IfIdx = 0;
    responseAddress.Network.SubIfIdx = 0;

    return SendPacket(winDivertHandle, response.data(), totalLength, &responseAddress);
}

bool NetworkInterceptor::SendDNSResponse(
    const std::string&,
    const std::string&,
    uint16_t)
{
    return false;
}

// ============== RST Implementation ==============

bool NetworkInterceptor::SendSingleRST(
    DWORD srcAddr,
    DWORD dstAddr,
    USHORT srcPort,
    USHORT dstPort,
    UINT32 seqNum,
    UINT32 ackNum)
{
    const UINT ipLen = sizeof(WINDIVERT_IPHDR);
    const UINT tcpLen = sizeof(WINDIVERT_TCPHDR);
    const UINT totalLen = ipLen + tcpLen;

    std::vector<BYTE> packet(totalLen, 0);

    auto* ip = reinterpret_cast<WINDIVERT_IPHDR*>(packet.data());
    auto* tcp = reinterpret_cast<WINDIVERT_TCPHDR*>(packet.data() + ipLen);

    ip->Version = 4;
    ip->HdrLength = 5;
    ip->Length = htons(static_cast<USHORT>(totalLen));
    ip->Id = htons(static_cast<USHORT>(GetTickCount()));
    ip->TTL = 64;
    ip->Protocol = IPPROTO_TCP;
    ip->SrcAddr = srcAddr;
    ip->DstAddr = dstAddr;

    tcp->SrcPort = srcPort;
    tcp->DstPort = dstPort;
    tcp->SeqNum = htonl(seqNum);
    tcp->AckNum = htonl(ackNum);
    tcp->HdrLength = sizeof(WINDIVERT_TCPHDR) / 4;
    tcp->Rst = 1;
    tcp->Ack = 1;
    tcp->Window = 0;

    WinDivertHelperCalcChecksums(packet.data(), totalLen, nullptr, 0);

    WINDIVERT_ADDRESS addr{};
    addr.Layer = WINDIVERT_LAYER_NETWORK;
    addr.Network.IfIdx = 0;
    addr.Network.SubIfIdx = 0;

    return WinDivertSend(winDivertHandle, packet.data(), totalLen, nullptr, &addr) != FALSE;
}

bool NetworkInterceptor::SendTCPReset(PVOID packet, UINT packetLength)
{
    WINDIVERT_IPHDR* ipHeader = nullptr;
    WINDIVERT_IPV6HDR* ipv6Header = nullptr;
    WINDIVERT_TCPHDR* tcpHeader = nullptr;
    WINDIVERT_UDPHDR* udpHeader = nullptr;
    PVOID payload = nullptr;
    UINT payloadLength = 0;
    UINT8 protocol = 0;

    if (!GetPacketHeaders(
        packet,
        packetLength,
        &ipHeader,
        &ipv6Header,
        &tcpHeader,
        &udpHeader,
        &payload,
        &payloadLength,
        &protocol)) {
        return false;
    }

    if (ipHeader == nullptr || tcpHeader == nullptr) {
        return false;
    }

    // RST to client (src -> dst)
    uint32_t clientSeq = ntohl(tcpHeader->AckNum);
    uint32_t clientAck = ntohl(tcpHeader->SeqNum) + payloadLength;
    if (tcpHeader->Syn) clientAck++;
    if (tcpHeader->Fin) clientAck++;

    SendSingleRST(
        ipHeader->SrcAddr,
        ipHeader->DstAddr,
        tcpHeader->SrcPort,
        tcpHeader->DstPort,
        clientSeq,
        clientAck);

    // RST to server (dst -> src)
    uint32_t serverSeq = ntohl(tcpHeader->SeqNum) + payloadLength;
    if (tcpHeader->Syn) serverSeq++;
    if (tcpHeader->Fin) serverSeq++;
    uint32_t serverAck = ntohl(tcpHeader->AckNum);

    SendSingleRST(
        ipHeader->DstAddr,
        ipHeader->SrcAddr,
        tcpHeader->DstPort,
        tcpHeader->SrcPort,
        serverSeq,
        serverAck);

    return true;
}

// ============== Process Related ==============

DWORD NetworkInterceptor::GetPacketProcessId(PVOID packet, UINT packetLength)
{
    WINDIVERT_IPHDR* ipHeader = nullptr;
    WINDIVERT_IPV6HDR* ipv6Header = nullptr;
    WINDIVERT_TCPHDR* tcpHeader = nullptr;
    WINDIVERT_UDPHDR* udpHeader = nullptr;
    PVOID payload = nullptr;
    UINT payloadLength = 0;
    UINT8 protocol = 0;

    if (!GetPacketHeaders(
        packet,
        packetLength,
        &ipHeader,
        &ipv6Header,
        &tcpHeader,
        &udpHeader,
        &payload,
        &payloadLength,
        &protocol)) {
        return 0;
    }

    if (ipHeader == nullptr || ipv6Header != nullptr) {
        return 0;
    }

    UINT localPort = 0;

    if (protocol == IPPROTO_TCP && tcpHeader != nullptr) {
        localPort = ntohs(tcpHeader->SrcPort);
    }
    else if (protocol == IPPROTO_UDP && udpHeader != nullptr) {
        localPort = ntohs(udpHeader->SrcPort);
    }
    else {
        return 0;
    }

    if (localPort == 0) {
        return 0;
    }

    DWORD pid = GetPidFromCache(localPort);
    if (pid != 0) {
        return pid;
    }

    UpdatePidCache();
    return GetPidFromCache(localPort);
}

std::string NetworkInterceptor::GetProcessName(DWORD pid)
{
    if (pid == 0) {
        return "System";
    }

    std::lock_guard<std::mutex> lock(processMutex);

    auto cached = processCache.find(pid);
    if (cached != processCache.end()) {
        return cached->second;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return "Unknown";
    }

    char path[MAX_PATH]{};
    DWORD pathLength = MAX_PATH;
    std::string result = "Unknown";

    if (QueryFullProcessImageNameA(process, 0, path, &pathLength)) {
        std::string fullPath(path, pathLength);
        const size_t slash = fullPath.find_last_of("\\/");
        result = slash == std::string::npos ? fullPath : fullPath.substr(slash + 1);
    }

    CloseHandle(process);
    processCache[pid] = result;
    return result;
}

// ============== Block Rules ==============

void NetworkInterceptor::BlockDomain(
    const std::string& domain,
    uint64_t timeoutMs)
{
    const std::string normalized = NormalizeDomain(domain);

    if (normalized.empty()) return;

    BlockRule rule{};
    rule.domain = normalized;
    rule.pattern = normalized;
    rule.startTime = GetTickCount64();
    rule.timeout = timeoutMs;
    rule.isPermanent = timeoutMs == 0;

    {
        std::lock_guard<std::mutex> lock(rulesMutex);
        blockRules[normalized] = rule;
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        domainCache.erase(normalized);
    }

    // 清除IP缓存，确保新的DNS查询会被拦截
    {
        std::lock_guard<std::mutex> lock(ipCacheMutex);
        // 清除所有映射到该域名的IP
        for (auto it = ipDomainCache.begin(); it != ipDomainCache.end();) {
            if (it->second == normalized) {
                it = ipDomainCache.erase(it);
            }
            else {
                ++it;
            }
        }
    }
}

void NetworkInterceptor::BlockDomainForProcess(
    const std::string& domain,
    const std::string& processName,
    uint64_t timeoutMs)
{
    const std::string normalized = NormalizeDomain(domain);
    if (normalized.empty()) return;

    BlockRule rule{};
    rule.domain = normalized;
    rule.pattern = normalized;
    rule.startTime = GetTickCount64();
    rule.timeout = timeoutMs;
    rule.isPermanent = timeoutMs == 0;

    if (!processName.empty()) {
        rule.processNames.insert(ToLower(processName));
    }

    {
        std::lock_guard<std::mutex> lock(rulesMutex);
        blockRules[normalized] = rule;
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        domainCache.erase(normalized);
    }
}

void NetworkInterceptor::UnblockDomain(
    const std::string& domain)
{
    const std::string normalized = NormalizeDomain(domain);
    {
        std::lock_guard<std::mutex> lock(rulesMutex);
        blockRules.erase(normalized);
    }
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        domainCache.erase(normalized);
    }
}

void NetworkInterceptor::ClearAllBlocks()
{
    {
        std::lock_guard<std::mutex> lock(rulesMutex);
        blockRules.clear();
    }
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        domainCache.clear();
    }
}

bool NetworkInterceptor::IsBlocked(const std::string& domain)
{
    const std::string normalized = NormalizeDomain(domain);

    if (normalized.empty()) {
        return false;
    }

    if (IsWhitelisted(normalized)) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        domainCache[normalized] = false;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto cached = domainCache.find(normalized);
        if (cached != domainCache.end()) {
            return cached->second;
        }
    }

    const bool blocked = IsDomainInBlockList(normalized);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        domainCache[normalized] = blocked;
    }

    return blocked;
}

bool NetworkInterceptor::IsDomainInBlockList(const std::string& domain)
{
    const std::string normalized = NormalizeDomain(domain);
    const uint64_t now = GetTickCount64();

    std::vector<std::string> expired;
    bool result = false;

    {
        std::lock_guard<std::mutex> lock(rulesMutex);

        for (auto it = blockRules.begin(); it != blockRules.end();) {
            const BlockRule& rule = it->second;
            if (!rule.isPermanent && now - rule.startTime > rule.timeout) {
                expired.push_back(it->first);
                it = blockRules.erase(it);
            }
            else {
                ++it;
            }
        }

        auto exact = blockRules.find(normalized);
        if (exact != blockRules.end()) {
            result = true;
        }
        else {
            for (const auto& item : blockRules) {
                if (MatchDomainPattern(normalized, item.second.pattern)) {
                    result = true;
                    break;
                }
            }
        }
    }

    if (!expired.empty()) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (const auto& item : expired) {
            domainCache.erase(item);
        }
    }

    return result;
}

// ============== Domain Matching ==============

bool NetworkInterceptor::MatchDomainPattern(
    const std::string& domain,
    const std::string& pattern)
{
    const std::string normalizedDomain = NormalizeDomain(domain);
    const std::string normalizedPattern = NormalizeDomain(pattern);

    if (normalizedDomain.empty() || normalizedPattern.empty()) {
        return false;
    }

    if (normalizedDomain == normalizedPattern) {
        return true;
    }

    if (normalizedPattern.size() > 2 &&
        normalizedPattern[0] == '*' &&
        normalizedPattern[1] == '.') {
        const std::string base = normalizedPattern.substr(2);
        if (normalizedDomain == base) {
            return true;
        }
        const std::string suffix = "." + base;
        bool match = normalizedDomain.size() > suffix.size() &&
            normalizedDomain.compare(
                normalizedDomain.size() - suffix.size(),
                suffix.size(),
                suffix) == 0;
        return match;
    }

    if (normalizedPattern[0] == '.') {
        const std::string base = normalizedPattern.substr(1);
        bool match = normalizedDomain == base ||
            (normalizedDomain.size() > normalizedPattern.size() &&
                normalizedDomain.compare(
                    normalizedDomain.size() - normalizedPattern.size(),
                    normalizedPattern.size(),
                    normalizedPattern) == 0);
        return match;
    }

    return false;
}

void NetworkInterceptor::LoadBlockList(
    const std::vector<std::string>& domains)
{
    for (const auto& domain : domains) {
        BlockDomain(domain, 0);
    }
}

void NetworkInterceptor::LoadBlockListFromFile(
    const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        const size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        line = NormalizeDomain(line);
        if (!line.empty()) {
            BlockDomain(line, 0);
        }
    }
}

// ============== Stats ==============

NetworkInterceptor::TrafficStats
NetworkInterceptor::GetStats()
{
    std::lock_guard<std::mutex> lock(statsMutex);
    return stats;
}

void NetworkInterceptor::ResetStats()
{
    std::lock_guard<std::mutex> lock(statsMutex);
    stats = TrafficStats{};
}

// ============== Stats Thread ==============

void NetworkInterceptor::StatsThread()
{
    while (isRunning.load()) {
        for (int i = 0; i < 50 && isRunning.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!isRunning.load()) break;

        UpdatePidCache();

        {
            std::lock_guard<std::mutex> lock(dnsMutex);
            if (pendingDNSQueries.size() > 100) {
                pendingDNSQueries.clear();
            }
        }

        {
            std::lock_guard<std::mutex> lock(processMutex);
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(
                now - lastProcessUpdate).count() > 60) {
                processCache.clear();
                lastProcessUpdate = now;
            }
        }

        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            if (domainCache.size() > 1000) {
                domainCache.clear();
            }
        }

        CleanIPCache();
        CleanupExpiredStreams();
    }
}

void NetworkInterceptor::CleanCache()
{
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (domainCache.size() > 1000) {
        domainCache.clear();
    }
}

// ============== Whitelist ==============

void NetworkInterceptor::AddWhitelistDomain(
    const std::string& domain)
{
    const std::string normalized = NormalizeDomain(domain);
    if (normalized.empty()) return;

    {
        std::lock_guard<std::mutex> lock(whitelistMutex);
        whitelistDomains.insert(normalized);
    }

    std::lock_guard<std::mutex> lock(cacheMutex);
    domainCache.clear();
}

void NetworkInterceptor::RemoveWhitelistDomain(
    const std::string& domain)
{
    const std::string normalized = NormalizeDomain(domain);
    {
        std::lock_guard<std::mutex> lock(whitelistMutex);
        whitelistDomains.erase(normalized);
    }
    std::lock_guard<std::mutex> lock(cacheMutex);
    domainCache.clear();
}

bool NetworkInterceptor::IsWhitelisted(
    const std::string& domain)
{
    const std::string normalized = NormalizeDomain(domain);
    std::lock_guard<std::mutex> lock(whitelistMutex);

    for (const auto& item : whitelistDomains) {
        if (MatchDomainPattern(normalized, item)) {
            return true;
        }
    }
    return false;
}

// ============== IP Blacklist ==============

void NetworkInterceptor::AddBlockIP(
    const std::string& ip)
{
    if (!IsValidIPv4String(ip)) return;
    std::lock_guard<std::mutex> lock(ipMutex);
    blockIPs.insert(ip);
}

void NetworkInterceptor::AddBlockIPRange(
    const std::string& startIP,
    const std::string& endIP)
{
    if (!IsValidIPv4String(startIP) || !IsValidIPv4String(endIP)) return;
    std::lock_guard<std::mutex> lock(ipMutex);
    blockIPRanges.emplace_back(startIP, endIP);
}

void NetworkInterceptor::RemoveBlockIP(
    const std::string& ip)
{
    std::lock_guard<std::mutex> lock(ipMutex);
    blockIPs.erase(ip);
}

bool NetworkInterceptor::ShouldBlockIP(
    const std::string& ipAddress)
{
    std::lock_guard<std::mutex> lock(ipMutex);

    if (blockIPs.find(ipAddress) != blockIPs.end()) {
        return true;
    }

    for (const auto& range : blockIPRanges) {
        if (IsIPInRange(ipAddress, range.first, range.second)) {
            return true;
        }
    }

    return false;
}

bool NetworkInterceptor::IsIPInRange(
    const std::string& ip,
    const std::string& start,
    const std::string& end)
{
    if (!IsValidIPv4String(ip) || !IsValidIPv4String(start) || !IsValidIPv4String(end)) {
        return false;
    }

    const uint32_t ipValue = IPToUint(ip);
    const uint32_t startValue = IPToUint(start);
    const uint32_t endValue = IPToUint(end);

    return ipValue >= startValue && ipValue <= endValue;
}

uint32_t NetworkInterceptor::IPToUint(
    const std::string& ip)
{
    in_addr address{};
    if (inet_pton(AF_INET, ip.c_str(), &address) != 1) {
        return 0;
    }
    return ntohl(address.s_addr);
}

std::string NetworkInterceptor::UintToIP(
    uint32_t ip)
{
    in_addr address{};
    address.s_addr = htonl(ip);

    char buffer[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &address, buffer, sizeof(buffer)) == nullptr) {
        return {};
    }
    return buffer;
}

// ============== Callback ==============

void NetworkInterceptor::SetBlockCallback(
    BlockCallback callback)
{
    std::lock_guard<std::mutex> lock(callbackMutex);
    blockCallback = std::move(callback);
}

// ============== Compatibility ==============

bool NetworkInterceptor::InjectBlockedPacket(PVOID, UINT)
{
    return false;
}