// Minecraft服务器IP检测工具.cpp
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <iostream>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <vector>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

using namespace std;

// ========== WinPcap 类型定义 ==========
typedef struct pcap pcap_t;
typedef unsigned int bpf_u_int32;

struct pcap_pkthdr {
    struct timeval ts;
    bpf_u_int32 caplen;
    bpf_u_int32 len;
};

struct bpf_insn {
    unsigned short code;
    unsigned char jt;
    unsigned char jf;
    int k;
};

struct bpf_program {
    unsigned int bf_len;
    struct bpf_insn* bf_insns;
};

struct pcap_addr {
    struct pcap_addr* next;
    struct sockaddr* addr;
    struct sockaddr* netmask;
    struct sockaddr* broadaddr;
    struct sockaddr* dstaddr;
};

typedef struct pcap_if pcap_if_t;
struct pcap_if {
    struct pcap_if* next;
    char* name;
    char* description;
    struct pcap_addr* addresses;
    bpf_u_int32 flags;
};

struct pcap_rmtauth {
    int type;
    char* username;
    char* password;
};

#define PCAP_ERRBUF_SIZE 256
#define PCAP_OPENFLAG_PROMISCUOUS 1

// ========== WinPcap 函数指针 ==========
typedef pcap_t* (*pcap_open_t)(const char*, int, int, int, pcap_rmtauth*, char*);
typedef void (*pcap_close_t)(pcap_t*);
typedef int (*pcap_compile_t)(pcap_t*, bpf_program*, const char*, int, bpf_u_int32);
typedef int (*pcap_setfilter_t)(pcap_t*, bpf_program*);
typedef int (*pcap_dispatch_t)(pcap_t*, int, void(*)(u_char*, const pcap_pkthdr*, const u_char*), u_char*);
typedef void (*pcap_freecode_t)(bpf_program*);
typedef int (*pcap_findalldevs_ex_t)(const char*, pcap_rmtauth*, pcap_if_t**, char*);
typedef void (*pcap_freealldevs_t)(pcap_if_t*);
typedef void (*pcap_breakloop_t)(pcap_t*);
typedef char* (*pcap_geterr_t)(pcap_t*);

pcap_open_t pcap_open_func = nullptr;
pcap_close_t pcap_close_func = nullptr;
pcap_compile_t pcap_compile_func = nullptr;
pcap_setfilter_t pcap_setfilter_func = nullptr;
pcap_dispatch_t pcap_dispatch_func = nullptr;
pcap_freecode_t pcap_freecode_func = nullptr;
pcap_findalldevs_ex_t pcap_findalldevs_ex_func = nullptr;
pcap_freealldevs_t pcap_freealldevs_func = nullptr;
pcap_breakloop_t pcap_breakloop_func = nullptr;
pcap_geterr_t pcap_geterr_func = nullptr;

HMODULE wpcap_dll = nullptr;

#define pcap_open pcap_open_func
#define pcap_close pcap_close_func
#define pcap_compile pcap_compile_func
#define pcap_setfilter pcap_setfilter_func
#define pcap_dispatch pcap_dispatch_func
#define pcap_freecode pcap_freecode_func
#define pcap_findalldevs_ex pcap_findalldevs_ex_func
#define pcap_freealldevs pcap_freealldevs_func
#define pcap_breakloop pcap_breakloop_func
#define pcap_geterr pcap_geterr_func

// ========== 数据包结构 ==========
#pragma pack(push, 1)
struct 以太网头 {
    unsigned char 目标MAC[6];
    unsigned char 源MAC[6];
    unsigned short 类型;
};

struct IP头 {
    unsigned char 版本长度;
    unsigned char 服务类型;
    unsigned short 总长度;
    unsigned short 标识;
    unsigned short 标志偏移;
    unsigned char 生存时间;
    unsigned char 协议;
    unsigned short 校验和;
    unsigned int 源地址;
    unsigned int 目标地址;
};

struct TCP头 {
    unsigned short 源端口;
    unsigned short 目标端口;
    unsigned int 序号;
    unsigned int 确认号;
    unsigned char 数据偏移;
    unsigned char 标志;
    unsigned short 窗口;
    unsigned short 校验和;
    unsigned short 紧急指针;
};

struct UDP头 {
    unsigned short 源端口;
    unsigned short 目标端口;
    unsigned short 长度;
    unsigned short 校验和;
};
#pragma pack(pop)

// ========== 全局变量 ==========
unordered_set<string> 发现的所有IP;
unordered_map<string, unordered_set<int>> IP端口映射;
unordered_map<string, chrono::steady_clock::time_point> IP首次出现时间;
unordered_map<string, int> IP数据包计数;
unordered_map<string, long long> IP流量字节数;
unordered_map<string, chrono::steady_clock::time_point> IP最后活跃时间;

vector<string> Minecraft服务器IP列表;
unordered_set<string> 可疑子服务器IP;

pcap_t* 捕获句柄 = nullptr;
volatile bool 停止捕获 = false;
bool 正在捕获 = false;
int 总数据包数 = 0;

string 目标服务器IP = "";  // 用户输入的服务器IP

// ========== 辅助函数 ==========
string 获取协议名(unsigned char 协议) {
    switch (协议) {
    case 1: return "ICMP";
    case 6: return "TCP";
    case 17: return "UDP";
    default: return "协议" + to_string(协议);
    }
}

string 获取当前时间() {
    auto now = chrono::system_clock::now();
    auto t = chrono::system_clock::to_time_t(now);
    auto ms = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()) % 1000;
    tm buf;
    localtime_s(&buf, &t);
    ostringstream ss;
    ss << put_time(&buf, "%H:%M:%S") << '.' << setfill('0') << setw(3) << ms.count();
    return ss.str();
}

string IP转字符串(unsigned int ip) {
    char s[16];
    snprintf(s, sizeof(s), "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    return string(s);
}

bool 是Minecraft端口(int 端口) {
    return (端口 == 25565 || 端口 == 25566 ||
        (端口 >= 25565 && 端口 <= 25575) ||
        端口 == 19132 || 端口 == 19133 ||
        端口 == 30000 || 端口 == 40000);
}

string 获取Minecraft端口说明(int 端口) {
    switch (端口) {
    case 25565: return "Java版默认";
    case 25566: return "Java版备用";
    case 19132: return "基岩版默认";
    case 19133: return "基岩版备用";
    default:
        if (端口 >= 25565 && 端口 <= 25575) return "Java版范围";
        return "其他MC端口";
    }
}

// ========== 数据包处理 ==========
void 数据包处理回调(u_char* 参数, const struct pcap_pkthdr* 头部, const u_char* 数据) {
    if (头部->len < sizeof(以太网头) + sizeof(IP头)) return;

    以太网头* eth = (以太网头*)数据;
    if (ntohs(eth->类型) != 0x0800) return;

    IP头* ip = (IP头*)(数据 + sizeof(以太网头));
    if (((ip->版本长度 >> 4) & 0xF) != 4) return;

    unsigned char ip头长度 = (ip->版本长度 & 0xF) * 4;
    string 源IP = IP转字符串(ip->源地址);
    string 目标IP = IP转字符串(ip->目标地址);

    if (源IP == "0.0.0.0" || 目标IP == "0.0.0.0") return;
    if (源IP == "127.0.0.1" || 目标IP == "127.0.0.1") return;

    string 协议 = 获取协议名(ip->协议);
    int 源端口 = 0, 目标端口 = 0;

    if (ip->协议 == 6) {
        TCP头* tcp = (TCP头*)(数据 + sizeof(以太网头) + ip头长度);
        源端口 = ntohs(tcp->源端口);
        目标端口 = ntohs(tcp->目标端口);
    }
    else if (ip->协议 == 17) {
        UDP头* udp = (UDP头*)(数据 + sizeof(以太网头) + ip头长度);
        源端口 = ntohs(udp->源端口);
        目标端口 = ntohs(udp->目标端口);
    }
    else {
        return;
    }

    // 记录所有IP
    if (发现的所有IP.find(源IP) == 发现的所有IP.end()) {
        发现的所有IP.insert(源IP);
        IP首次出现时间[源IP] = chrono::steady_clock::now();
    }
    if (发现的所有IP.find(目标IP) == 发现的所有IP.end()) {
        发现的所有IP.insert(目标IP);
        IP首次出现时间[目标IP] = chrono::steady_clock::now();
    }

    IP端口映射[源IP].insert(源端口);
    IP端口映射[目标IP].insert(目标端口);
    IP数据包计数[源IP]++;
    IP数据包计数[目标IP]++;
    IP流量字节数[源IP] += 头部->len;
    IP流量字节数[目标IP] += 头部->len;
    IP最后活跃时间[源IP] = chrono::steady_clock::now();
    IP最后活跃时间[目标IP] = chrono::steady_clock::now();

    总数据包数++;

    // ========== Minecraft相关检测 ==========
    bool 与Minecraft相关 = false;
    string MC角色 = "";

    // 检测是否与目标服务器通信
    if (源IP == 目标服务器IP || 目标IP == 目标服务器IP) {
        与Minecraft相关 = true;
        if (源IP == 目标服务器IP) MC角色 = "服务器→本机";
        else MC角色 = "本机→服务器";
    }

    // 检测Minecraft端口
    if (是Minecraft端口(源端口) || 是Minecraft端口(目标端口)) {
        与Minecraft相关 = true;

        string 新服务器IP = (是Minecraft端口(目标端口)) ? 目标IP : 源IP;
        int MC端口 = (是Minecraft端口(目标端口)) ? 目标端口 : 源端口;

        // 排除已知的目标服务器
        if (新服务器IP != 目标服务器IP && 新服务器IP.find("192.168") == string::npos) {
            if (可疑子服务器IP.find(新服务器IP) == 可疑子服务器IP.end()) {
                可疑子服务器IP.insert(新服务器IP);
                cout << "\n🎮 [发现Minecraft服务器] " << 新服务器IP << ":" << MC端口
                    << " (" << 获取Minecraft端口说明(MC端口) << ")" << endl;
                cout << "   ⚠️  这可能就是子服务器IP！" << endl;
            }
        }

        if (find(Minecraft服务器IP列表.begin(), Minecraft服务器IP列表.end(), 新服务器IP) == Minecraft服务器IP列表.end()) {
            Minecraft服务器IP列表.push_back(新服务器IP);
        }
    }

    // 显示与目标服务器相关的流量
    if (与Minecraft相关) {
        cout << "[" << 获取当前时间() << "] ";
        cout << left << setw(16) << 源IP;
        if (源端口) cout << ":" << setw(5) << 源端口;
        else cout << "      ";
        cout << " → " << setw(16) << 目标IP;
        if (目标端口) cout << ":" << setw(5) << 目标端口;
        else cout << "      ";
        cout << " [" << 协议 << "] " << MC角色;
        if (是Minecraft端口(源端口) || 是Minecraft端口(目标端口)) cout << " 🎮";
        cout << endl;
    }
}

// ========== 系统函数 ==========
BOOL WINAPI 控制台处理(DWORD 信号) {
    if (信号 == CTRL_C_EVENT || 信号 == CTRL_BREAK_EVENT) {
        if (正在捕获) {
            cout << "\n\n正在停止捕获...\n";
            停止捕获 = true;
            if (捕获句柄) pcap_breakloop(捕获句柄);
            return TRUE;
        }
    }
    return FALSE;
}

void 清屏() { system("cls"); }

int 获取整数输入(const string& 提示, int 最小值, int 最大值) {
    int 值;
    while (true) {
        cout << 提示;
        if (cin >> 值 && 值 >= 最小值 && 值 <= 最大值) { cin.ignore(INT_MAX, '\n'); return 值; }
        cout << "请输入 " << 最小值 << "-" << 最大值 << " 之间的数字: ";
        cin.clear();
        cin.ignore(INT_MAX, '\n');
    }
}

string 获取字符串输入(const string& 提示) {
    string 输入;
    cout << 提示;
    getline(cin, 输入);
    return 输入;
}

// ========== 加载WinPcap ==========
bool 加载WinPcap() {
    wpcap_dll = LoadLibraryA("wpcap.dll");
    if (!wpcap_dll) wpcap_dll = LoadLibraryA("C:\\Windows\\System32\\Npcap\\wpcap.dll");
    if (!wpcap_dll) {
        cerr << "❌ 找不到 wpcap.dll！" << endl;
        cerr << "请安装 WinPcap 或 Npcap" << endl;
        cerr << "下载地址: https://npcap.com/" << endl;
        return false;
    }

    pcap_open_func = (pcap_open_t)GetProcAddress(wpcap_dll, "pcap_open");
    pcap_close_func = (pcap_close_t)GetProcAddress(wpcap_dll, "pcap_close");
    pcap_compile_func = (pcap_compile_t)GetProcAddress(wpcap_dll, "pcap_compile");
    pcap_setfilter_func = (pcap_setfilter_t)GetProcAddress(wpcap_dll, "pcap_setfilter");
    pcap_dispatch_func = (pcap_dispatch_t)GetProcAddress(wpcap_dll, "pcap_dispatch");
    pcap_freecode_func = (pcap_freecode_t)GetProcAddress(wpcap_dll, "pcap_freecode");
    pcap_findalldevs_ex_func = (pcap_findalldevs_ex_t)GetProcAddress(wpcap_dll, "pcap_findalldevs_ex");
    pcap_freealldevs_func = (pcap_freealldevs_t)GetProcAddress(wpcap_dll, "pcap_freealldevs");
    pcap_breakloop_func = (pcap_breakloop_t)GetProcAddress(wpcap_dll, "pcap_breakloop");
    pcap_geterr_func = (pcap_geterr_t)GetProcAddress(wpcap_dll, "pcap_geterr");

    if (!pcap_open_func || !pcap_findalldevs_ex_func) {
        cerr << "❌ 获取WinPcap函数失败" << endl;
        return false;
    }

    cout << "✅ WinPcap加载成功" << endl;
    return true;
}

// ========== 查询Minecraft服务器信息 ==========
string 查询MC服务器(const string& ip, int port = 25565) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return "无法创建套接字";

    DWORD timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return "无法连接";
    }

    // 构建握手包
    string hostname = ip;
    vector<unsigned char> packet;

    // 包ID: 0x00
    packet.push_back(0x00);
    // 协议版本 (VarInt 754)
    packet.push_back(0xFA);
    packet.push_back(0x05);
    // 服务器地址
    packet.push_back(hostname.length());
    for (char c : hostname) packet.push_back(c);
    // 端口
    packet.push_back((port >> 8) & 0xFF);
    packet.push_back(port & 0xFF);
    // 下一状态: 1
    packet.push_back(0x01);

    // 发送长度前缀 + 数据
    unsigned char lenVarInt[5];
    int lenSize = 0;
    int length = packet.size();
    do {
        lenVarInt[lenSize++] = (length & 0x7F) | (length > 0x7F ? 0x80 : 0);
        length >>= 7;
    } while (length > 0);

    send(sock, (char*)lenVarInt, lenSize, 0);
    send(sock, (char*)packet.data(), packet.size(), 0);

    // 请求状态
    unsigned char req[] = { 0x01, 0x00 };
    send(sock, (char*)req, 2, 0);

    // 接收响应
    char buffer[65536];
    int received = recv(sock, buffer, sizeof(buffer), 0);
    closesocket(sock);

    if (received > 5) {
        // 跳过长度和包ID
        int pos = 0;
        int packetLen = 0, shift = 0;
        do {
            packetLen |= (buffer[pos] & 0x7F) << shift;
            shift += 7;
        } while (buffer[pos++] & 0x80);

        // 跳过包ID
        int jsonLen = 0;
        shift = 0;
        do {
            jsonLen |= (buffer[pos] & 0x7F) << shift;
            shift += 7;
        } while (buffer[pos++] & 0x80);

        string json(buffer + pos, min(jsonLen, received - pos));
        return json;
    }

    return "无响应";
}

void 提取JSON字段(const string& json, const string& 字段名, const string& 显示名) {
    size_t pos = json.find(字段名);
    if (pos != string::npos) {
        pos = json.find("\"", pos + 字段名.length() + 3);
        if (pos != string::npos) {
            size_t end = json.find("\"", pos + 1);
            if (end != string::npos) {
                cout << "   " << 显示名 << ": " << json.substr(pos + 1, end - pos - 1) << endl;
            }
        }
    }
}

// ========== 显示结果 ==========
void 显示MC分析结果() {
    清屏();
    cout << "╔══════════════════════════════════════════╗" << endl;
    cout << "║     Minecraft 服务器分析结果             ║" << endl;
    cout << "╚══════════════════════════════════════════╝" << endl << endl;

    cout << "📡 目标服务器: " << 目标服务器IP << endl;
    cout << "📊 总数据包: " << 总数据包数 << endl;
    cout << "🌐 发现IP数: " << 发现的所有IP.size() << endl;

    // 显示Minecraft服务器
    cout << "\n=== 🎮 Minecraft 服务器列表 ===" << endl;
    cout << string(60, '-') << endl;

    int 序号 = 1;
    for (const auto& ip : Minecraft服务器IP列表) {
        cout << 序号++ << ". " << ip;
        if (ip == 目标服务器IP) cout << " ← 主服务器";
        if (可疑子服务器IP.find(ip) != 可疑子服务器IP.end()) cout << " ⚠️ 疑似子服务器";

        auto it = IP端口映射.find(ip);
        if (it != IP端口映射.end()) {
            cout << " [端口:";
            for (int p : it->second) cout << " " << p;
            cout << " ]";
        }
        cout << endl;
    }

    // 分析流量
    cout << "\n=== 📈 流量分析 ===" << endl;
    cout << string(60, '-') << endl;
    cout << left << setw(18) << "IP地址" << setw(10) << "数据包" << setw(12) << "流量" << "说明" << endl;
    cout << string(60, '-') << endl;

    for (const auto& ip : Minecraft服务器IP列表) {
        auto it计数 = IP数据包计数.find(ip);
        auto it流量 = IP流量字节数.find(ip);

        int 包数 = (it计数 != IP数据包计数.end()) ? it计数->second : 0;
        long long 流量 = (it流量 != IP流量字节数.end()) ? it流量->second : 0;

        string 说明 = "";
        if (ip == 目标服务器IP) 说明 = "主服务器";
        else if (可疑子服务器IP.find(ip) != 可疑子服务器IP.end()) 说明 = "⚠️ 疑似子服务器";

        cout << left << setw(18) << ip;
        cout << setw(10) << 包数;

        if (流量 < 1024) cout << setw(12) << (to_string(流量) + "B");
        else if (流量 < 1024 * 1024) cout << setw(12) << (to_string(流量 / 1024) + "KB");
        else cout << setw(12) << (to_string(流量 / (1024 * 1024)) + "MB");

        cout << 说明 << endl;
    }

    // 可疑子服务器详情
    if (!可疑子服务器IP.empty()) {
        cout << "\n=== ⚠️ 可疑子服务器详情 ===" << endl;
        cout << string(60, '-') << endl;
        cout << "以下IP可能是隐藏的子服务器：" << endl << endl;

        for (const auto& ip : 可疑子服务器IP) {
            cout << "🔍 " << ip << endl;

            // 尝试查询服务器信息
            cout << "   正在查询服务器信息..." << endl;
            string json = 查询MC服务器(ip);
            if (!json.empty() && json != "无法连接" && json != "无响应") {
                提取JSON字段(json, "\"description\"", "描述");
                提取JSON字段(json, "\"version\"", "版本");
                cout << "   📄 服务器响应: " << json.substr(0, 100) << "..." << endl;

                // 检查BungeeCord
                if (json.find("BungeeCord") != string::npos) {
                    cout << "   🔗 确认使用BungeeCord代理" << endl;
                }
            }
            else {
                cout << "   ❌ " << json << endl;
            }
            cout << endl;
        }
    }

    // 结论
    cout << "=== 📋 分析结论 ===" << endl;
    cout << string(60, '-') << endl;

    if (可疑子服务器IP.empty() && Minecraft服务器IP列表.size() == 1) {
        cout << "✅ 只有一个Minecraft服务器IP，排队服和游玩服使用同一IP" << endl;
        cout << "   可能是通过不同端口或内部代理区分" << endl;
    }
    else if (!可疑子服务器IP.empty()) {
        cout << "⚠️  发现 " << 可疑子服务器IP.size() << " 个可疑子服务器IP" << endl;
        cout << "   这些可能是排队服或游玩服的真实IP" << endl;
    }
}

// ========== 主函数 ==========
int main() {
    SetConsoleTitle(TEXT("Minecraft服务器IP检测工具"));
    SetConsoleCtrlHandler(控制台处理, TRUE);

    system("mode con cols=110 lines=40");

    清屏();
    cout << "╔══════════════════════════════════════════╗" << endl;
    cout << "║   Minecraft 服务器IP检测工具 v1.0        ║" << endl;
    cout << "╠══════════════════════════════════════════╣" << endl;
    cout << "║  功能:                                   ║" << endl;
    cout << "║  🔍 检测Minecraft服务器真实IP            ║" << endl;
    cout << "║  🎮 区分排队服和游玩服                   ║" << endl;
    cout << "║  📡 发现隐藏的子服务器                   ║" << endl;
    cout << "║  📊 分析流量和连接                       ║" << endl;
    cout << "╚══════════════════════════════════════════╝" << endl << endl;

    // ===== 输入目标服务器IP =====
    cout << "请输入目标Minecraft服务器IP地址:" << endl;
    cout << "（可以是域名，如 mc.example.com 或IP地址）" << endl;
    cout << "> ";
    目标服务器IP = 获取字符串输入("");

    if (目标服务器IP.empty()) {
        cout << "❌ 未输入服务器地址，程序退出" << endl;
        system("pause");
        return 1;
    }

    cout << "\n目标服务器: " << 目标服务器IP << endl;
    cout << "正在尝试查询服务器信息..." << endl;

    string 服务器信息 = 查询MC服务器(目标服务器IP);
    if (服务器信息 != "无法连接" && 服务器信息 != "无响应") {
        cout << "\n✅ 服务器在线！" << endl;
        提取JSON字段(服务器信息, "\"description\"", "描述");
        提取JSON字段(服务器信息, "\"version\"", "版本");

        // 检测BungeeCord
        if (服务器信息.find("BungeeCord") != string::npos) {
            cout << "   ⚠️  检测到BungeeCord代理！可能存在子服务器" << endl;
        }
    }
    else {
        cout << "⚠️  " << 服务器信息 << "（将继续捕获流量）" << endl;
    }

    // ===== 加载WinPcap =====
    cout << "\n正在加载网络捕获组件..." << endl;
    if (!加载WinPcap()) {
        system("pause");
        return 1;
    }

    // ===== 选择网卡 =====
    char 源字符串[] = "rpcap://";
    char 错误缓冲[PCAP_ERRBUF_SIZE];
    pcap_if_t* 所有网卡 = nullptr;

    if (pcap_findalldevs_ex(源字符串, nullptr, &所有网卡, 错误缓冲) == -1) {
        cerr << "❌ 查找网卡失败: " << 错误缓冲 << endl;
        cerr << "请以管理员权限运行！" << endl;
        system("pause");
        return 1;
    }

    vector<pcap_if_t*> 网卡列表;
    for (pcap_if_t* d = 所有网卡; d; d = d->next) 网卡列表.push_back(d);

    清屏();
    cout << "可用网卡列表:" << endl << endl;
    for (size_t i = 0; i < 网卡列表.size(); i++) {
        cout << "[" << (i + 1) << "] " << 网卡列表[i]->name << endl;
        if (网卡列表[i]->description) cout << "    描述: " << 网卡列表[i]->description << endl;
        for (struct pcap_addr* a = 网卡列表[i]->addresses; a; a = a->next) {
            if (a->addr->sa_family == AF_INET) {
                sockaddr_in* ip4 = (sockaddr_in*)a->addr;
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(ip4->sin_addr), ip, sizeof(ip));
                cout << "    IP: " << ip << endl;
            }
        }
        cout << endl;
    }

    int 选择 = 获取整数输入("请选择网卡 (1-" + to_string(网卡列表.size()) + "): ", 1, (int)网卡列表.size());
    pcap_if_t* 选中网卡 = 网卡列表[选择 - 1];

    // ===== 设置捕获时长 =====
    cout << "\n捕获时长设置:" << endl;
    cout << "1. 30秒（快速检测）" << endl;
    cout << "2. 1分钟" << endl;
    cout << "3. 3分钟（推荐）" << endl;
    cout << "4. 5分钟" << endl;
    cout << "5. 自定义" << endl;
    int 时长选择 = 获取整数输入("请选择 (1-5): ", 1, 5);

    int 捕获秒数;
    switch (时长选择) {
    case 1: 捕获秒数 = 30; break;
    case 2: 捕获秒数 = 60; break;
    case 3: 捕获秒数 = 180; break;
    case 4: 捕获秒数 = 300; break;
    default: 捕获秒数 = 获取整数输入("请输入秒数 (10-600): ", 10, 600); break;
    }

    // ===== 打开网卡 =====
    捕获句柄 = pcap_open(选中网卡->name, 65536, PCAP_OPENFLAG_PROMISCUOUS, 1000, nullptr, 错误缓冲);
    if (!捕获句柄) {
        cerr << "❌ 打开网卡失败: " << 错误缓冲 << endl;
        pcap_freealldevs(所有网卡);
        system("pause");
        return 1;
    }

    // ===== 设置过滤器 =====
    string 过滤规则 = "host " + 目标服务器IP + " and (tcp or udp)";
    struct bpf_program 过滤器;
    if (pcap_compile(捕获句柄, &过滤器, 过滤规则.c_str(), 0, 0) == -1) {
        // 如果IP过滤失败，使用端口过滤
        cout << "⚠️  IP过滤失败，使用端口过滤" << endl;
        if (pcap_compile(捕获句柄, &过滤器, "port 25565 or port 25566 or port 19132 or portrange 25565-25575", 0, 0) == -1) {
            cerr << "❌ 过滤器编译失败: " << pcap_geterr(捕获句柄) << endl;
            pcap_close(捕获句柄);
            pcap_freealldevs(所有网卡);
            system("pause");
            return 1;
        }
    }
    pcap_setfilter(捕获句柄, &过滤器);
    pcap_freecode(&过滤器);

    清屏();
    cout << "╔══════════════════════════════════════════╗" << endl;
    cout << "║       正在捕获 Minecraft 流量            ║" << endl;
    cout << "╚══════════════════════════════════════════╝" << endl;
    cout << "🎯 目标: " << 目标服务器IP << endl;
    cout << "📡 网卡: " << 选中网卡->name << endl;
    cout << "⏱️  时长: " << 捕获秒数 << " 秒" << endl;
    cout << "💡 提示: 请在游戏中操作（排队→进入游戏）" << endl;
    cout << "⚠️  按 Ctrl+C 可提前停止" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl << endl;

    正在捕获 = true;
    auto 开始时间 = chrono::steady_clock::now();
    auto 结束时间 = 开始时间 + chrono::seconds(捕获秒数);

    while (!停止捕获) {
        auto 当前时间 = chrono::steady_clock::now();
        if (当前时间 >= 结束时间) {
            cout << "\n⏰ 捕获时间已到！" << endl;
            break;
        }

        auto 剩余秒数 = chrono::duration_cast<chrono::seconds>(结束时间 - 当前时间).count();

        int 结果 = pcap_dispatch(捕获句柄, 10, 数据包处理回调, nullptr);
        if (结果 < 0) break;

        this_thread::sleep_for(chrono::milliseconds(1));

        static auto 上次统计时间 = 开始时间;
        if (chrono::duration_cast<chrono::seconds>(当前时间 - 上次统计时间).count() >= 15) {
            cout << "\n📊 [统计] 数据包:" << 总数据包数
                << " | IP:" << 发现的所有IP.size()
                << " | MC服务器:" << Minecraft服务器IP列表.size()
                << " | ⏳ 剩余:" << 剩余秒数 << "秒\n" << endl;
            上次统计时间 = 当前时间;
        }
    }

    正在捕获 = false;
    pcap_close(捕获句柄);
    pcap_freealldevs(所有网卡);

    if (wpcap_dll) FreeLibrary(wpcap_dll);

    // 显示分析结果
    显示MC分析结果();

    cout << "\n按回车键退出...";
    cin.get();
    return 0;
}