// MlcData.cpp — Data/mlc 数据层实现
#include "MlcData.h"
#include "Json.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mlc {

namespace fs = std::filesystem;

// ---------------------------------------------------------------
// 路径
// ---------------------------------------------------------------
std::wstring ExeDir() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    wchar_t path[MAX_PATH * 2] = { 0 };
    // 传 NULL 得到宿主进程（light.exe）路径，即「主程序目录」
    DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    std::wstring p(path, n);
    size_t slash = p.find_last_of(L"\\/");
    cached = (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
    return cached;
}

std::wstring BaseDir() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    cached = ExeDir() + L"\\Data\\mlc";
    return cached;
}

std::wstring CoversDir() { return BaseDir() + L"\\cache\\covers"; }
std::wstring BackupsDir() { return BaseDir() + L"\\backups"; }
std::wstring LogsDir() { return BaseDir() + L"\\logs"; }

void EnsureDirs() {
    std::error_code ec;
    fs::create_directories(fs::path(BaseDir()) / L"config", ec);
    fs::create_directories(fs::path(BaseDir()) / L"games", ec);
    fs::create_directories(fs::path(BaseDir()) / L"cache" / L"covers", ec);
    fs::create_directories(fs::path(BaseDir()) / L"cache" / L"meta", ec);
    fs::create_directories(fs::path(BaseDir()) / L"backups", ec);
    fs::create_directories(fs::path(BaseDir()) / L"logs", ec);
    fs::create_directories(fs::path(BaseDir()) / L"accounts", ec);
}

// ---------------------------------------------------------------
// 工具
// ---------------------------------------------------------------
std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string WToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string o((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &o[0], n, nullptr, nullptr);
    return o;
}

std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '\\' || last == '/') return a + b;
    return a + "\\" + b;
}

bool DirExists(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(fs::u8path(p), ec);
}

bool FileExists(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(fs::u8path(p), ec);
}

std::string ReadTextFile(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return std::string();
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

bool WriteTextFile(const std::string& path, const std::string& text) {
    std::error_code ec;
    fs::path p = fs::u8path(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return true;
}

void Log(const std::string& line) {
    EnsureDirs();
    std::string path = WToUtf8(LogsDir()) + "\\launcher.log";
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::app);
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char head[64];
    snprintf(head, sizeof(head), "[%04d-%02d-%02d %02d:%02d:%02d] ", st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond);
    f << head << line << "\r\n";
}

std::string MakeId(const std::string& seed) {
    // FNV-1a 64 → 12 位十六进制
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : seed) { h ^= c; h *= 1099511628211ULL; }
    char buf[32];
    snprintf(buf, sizeof(buf), "%012llx", (unsigned long long)(h & 0xFFFFFFFFFFFFULL));
    return buf;
}

// ---------------------------------------------------------------
// 渠道分类
// ---------------------------------------------------------------
const wchar_t* ChannelName(Channel c) {
    switch (c) {
        case Channel::Release: return L"正式版";
        case Channel::PreRelease: return L"测试版";
        case Channel::Snapshot: return L"快照版";
        case Channel::AprilFools: return L"愚人节";
        case Channel::OldBeta: return L"远古 Beta";
        case Channel::OldAlpha: return L"远古 Alpha";
    }
    return L"其他";
}

const char* ChannelKey(Channel c) {
    switch (c) {
        case Channel::Release: return "release";
        case Channel::PreRelease: return "prerelease";
        case Channel::Snapshot: return "snapshot";
        case Channel::AprilFools: return "aprilfools";
        case Channel::OldBeta: return "oldbeta";
        case Channel::OldAlpha: return "oldalpha";
    }
    return "other";
}

// 已知愚人节版本（Mojang 历年 4/1 发布）
static bool isAprilFoolsId(const std::string& id) {
    static const char* kIds[] = {
        "2point0_red", "2point0_purple", "2point0_blue",
        "15w14a", "1.RV-Pre1", "3D Shareware v1.34", "3d-shareware",
        "20w14infinite", "20w14~", "22w13oneblockatatime", "23w13a_or_b",
        "24w14potato", "25w14craftmine", "1.14.3 - Combat Test",
    };
    std::string low = id;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    for (const char* s : kIds) {
        std::string t = s;
        std::transform(t.begin(), t.end(), t.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (low == t) return true;
    }
    return false;
}

Channel ClassifyVersion(const std::string& id, const std::string& type,
                        const std::string& releaseTime) {
    if (type == "old_alpha") return Channel::OldAlpha;
    if (type == "old_beta") return Channel::OldBeta;
    if (isAprilFoolsId(id)) return Channel::AprilFools;

    // 4 月 1 日发布的快照/正式版 → 愚人节
    if (releaseTime.size() >= 10 && releaseTime.substr(5, 5) == "04-01"
        && type != "release") {
        return Channel::AprilFools;
    }

    std::string low = id;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    if (type == "release") return Channel::Release;
    if (type != "snapshot") return Channel::Release;

    // 快照里再分：预发布 / 候选版 → 测试版；其余 → 快照版
    if (low.find("-pre") != std::string::npos || low.find("-rc") != std::string::npos)
        return Channel::PreRelease;
    return Channel::Snapshot;
}

// ---------------------------------------------------------------
// 名称校验
// ---------------------------------------------------------------
bool IsValidGameName(const std::wstring& name) {
    if (name.empty() || name.size() > 64) return false;
    for (wchar_t c : name) {
        if (c == L'_') continue;
        if (c < 0x20) return false;
        // 字母/数字（含中文等非 ASCII 文字）放行；ASCII 标点与空格拒绝
        if (iswalnum(c)) continue;
        if (c > 0x7F && !iswspace(c) && !iswpunct(c)) continue;  // CJK 等
        return false;
    }
    // 不能全是下划线
    if (name.find_first_not_of(L'_') == std::wstring::npos) return false;
    return true;
}

std::wstring SanitizeGameName(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    for (wchar_t c : in) {
        if (c == L'_') { out.push_back(c); continue; }
        if (c < 0x20) continue;
        if (iswalnum(c)) { out.push_back(c); continue; }
        if (c > 0x7F && !iswspace(c) && !iswpunct(c)) { out.push_back(c); continue; }
        // 其它符号：换成下划线（保持长度可读）
        if (!out.empty() && out.back() != L'_') out.push_back(L'_');
    }
    while (!out.empty() && out.back() == L'_') out.pop_back();
    if (out.empty()) out = L"";
    return out;
}

std::wstring DefaultNameFromVersion(const std::string& versionId) {
    std::wstring w = Utf8ToW(versionId);
    std::wstring s = SanitizeGameName(w);
    if (s.empty()) s = L"游戏";
    return s;
}

// 离线玩家名：1–16 位，只允许 ASCII 字母、数字、下划线（与正版 ID / HMCL 规则一致）
bool IsValidPlayerName(const std::wstring& name) {
    if (name.empty() || name.size() > 16) return false;
    for (wchar_t c : name) {
        bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  (c >= L'0' && c <= L'9') || c == L'_';
        if (!ok) return false;
    }
    return true;
}

// ---------------------------------------------------------------
// Instance 路径
// ---------------------------------------------------------------
std::string Instance::versionsDir() const { return JoinPath(folder, "versions"); }
std::string Instance::librariesDir() const { return JoinPath(folder, "libraries"); }
std::string Instance::assetsDir() const { return JoinPath(folder, "assets"); }

bool Instance::isolateEnabled() const {
    if (settings.isolate == 1) return true;
    if (settings.isolate == 0) return false;
    return Store::I().spec().isolate;
}

std::string Instance::dataDir() const {
    if (isolateEnabled()) return JoinPath(versionsDir(), versionId);
    return folder;
}

std::string Instance::modsDir() const { return JoinPath(dataDir(), "mods"); }
std::string Instance::savesDir() const { return JoinPath(dataDir(), "saves"); }
std::string Instance::resourcePacksDir() const { return JoinPath(dataDir(), "resourcepacks"); }
std::string Instance::shaderPacksDir() const { return JoinPath(dataDir(), "shaderpacks"); }
std::string Instance::schematicsDir() const { return JoinPath(dataDir(), "schematics"); }

// ---------------------------------------------------------------
// Store
// ---------------------------------------------------------------
static std::string settingsPath() { return WToUtf8(BaseDir()) + "\\config\\global.json"; }
static std::string registryPath() { return WToUtf8(BaseDir()) + "\\games\\registry.json"; }
static std::string foldersPath() { return WToUtf8(BaseDir()) + "\\config\\folders.json"; }

// ---- 账号存储（照 HMCL：每个账号各自带 portable 标记）----
// 便携 = <启动器目录>\Data\mlc\accounts（跟着启动器/整合包走）
// 全局 = %APPDATA%\YXGLauncher（换启动器位置也还在）
std::string Store::PortableAccountsDir() const {
    return WToUtf8(BaseDir()) + "\\accounts";
}

std::string Store::GlobalAccountsDir() const {
    static std::string cached;
    if (!cached.empty()) return cached;
    wchar_t buf[MAX_PATH] = { 0 };
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        // 沿用旧版的 %APPDATA%\YXGLauncher\accounts，老用户的全局账号不会丢
        cached = WToUtf8(std::wstring(buf, n) + L"\\YXGLauncher\\accounts");
        return cached;
    }
    cached = WToUtf8(BaseDir()) + "\\accounts-global";   // 兜底
    return cached;
}

std::wstring Store::SkinsDirFor(bool portable) const {
    std::string dir = (portable ? PortableAccountsDir() : GlobalAccountsDir()) + "\\skins";
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    return Utf8ToW(dir);
}

static std::string instanceSettingsPath(const std::string& id) {
    return WToUtf8(BaseDir()) + "\\games\\" + id + "\\settings.json";
}

Store& Store::I() {
    static Store s;
    return s;
}

Store::Store() {
    EnsureDirs();
    Load();
}

Settings& Store::spec() { return m_spec; }
const Settings& Store::spec() const { return m_spec; }
std::vector<Instance>& Store::list() { return m_inst; }

void Store::Load() {
    // ---- 全局设置 ----
    std::string txt = ReadTextFile(settingsPath());
    if (!txt.empty()) {
        try {
            json::Value j = json::parse(txt);
            auto& s = m_spec;
            if (j.get("maxMemoryMB").asInt() > 0) s.maxMemoryMB = (int)j.get("maxMemoryMB").asInt();
            if (!j.get("source").asString().empty()) s.source = j.get("source").asString();
            if (j.has("isolate")) s.isolate = j.get("isolate").asBool();
            if (j.has("checkIntegrity")) s.checkIntegrity = j.get("checkIntegrity").asBool();
            if (j.has("autoRepair")) s.autoRepair = j.get("autoRepair").asBool();
            if (j.has("backupSaves")) s.backupSaves = j.get("backupSaves").asBool();
            if (j.get("width").asInt() > 0) s.width = (int)j.get("width").asInt();
            if (j.get("height").asInt() > 0) s.height = (int)j.get("height").asInt();
            s.javaPath = j.get("javaPath").asString();
            s.javaMajor = (int)j.get("javaMajor").asInt();
            if (j.has("msClientId")) s.msClientId = j.get("msClientId").asString();
            if (j.has("accountStorage")) s.accountStorage = j.get("accountStorage").asString();
            if (j.has("currentAccount")) s.currentAccount = j.get("currentAccount").asString();
            if (j.get("downloadThreads").asInt() > 0) s.downloadThreads = (int)j.get("downloadThreads").asInt();
            if (j.has("keepLauncherOpen")) s.keepLauncherOpen = j.get("keepLauncherOpen").asBool();
            s.directServer = j.get("directServer").asString();
            int dp = (int)j.get("directPort").asInt();
            s.directPort = dp > 0 ? dp : 25565;
            if (j.has("cfBase")) s.cfBase = j.get("cfBase").asString();
            if (j.has("cfApiKey")) s.cfApiKey = j.get("cfApiKey").asString();
            const json::Value& sv = j.get("servers");
            for (size_t i = 0; i < sv.size(); ++i) {
                std::string addr = sv.at(i).get("address").asString();
                int port = (int)sv.at(i).get("port").asInt();
                if (!addr.empty()) s.servers.push_back({ addr, port > 0 ? port : 25565 });
            }
        } catch (...) {}
    }

    // ---- 游戏文件夹 ----
    {
        std::string ftxt = ReadTextFile(foldersPath());
        if (!ftxt.empty()) {
            try {
                json::Value j = json::parse(ftxt);
                m_curFolder = j.get("current").asString();
                const json::Value& arr = j.get("folders");
                for (size_t i = 0; i < arr.size(); ++i) {
                    std::string f = arr.at(i).asString();
                    if (!f.empty()) m_folders.push_back(f);
                }
            } catch (...) {}
        }
    }

    // ---- 账号 ----
    LoadAccounts();

    // ---- 实例注册表 ----
    std::string reg = ReadTextFile(registryPath());
    if (reg.empty()) return;
    try {
        json::Value j = json::parse(reg);
        m_current = j.get("current").asString();
        const json::Value& arr = j.get("instances");
        for (size_t i = 0; i < arr.size(); ++i) {
            const json::Value& e = arr.at(i);
            Instance in;
            in.id = e.get("id").asString();
            in.name = e.get("name").asString();
            in.folder = e.get("folder").asString();
            in.versionId = e.get("versionId").asString();
            in.loader = e.get("loader").asString();
            in.loaderVersion = e.get("loaderVersion").asString();
            in.iconPath = e.get("icon").asString();
            in.lastPlayed = e.get("lastPlayed").asInt();
            if (in.id.empty() || in.folder.empty() || in.versionId.empty()) continue;
            if (in.name.empty()) in.name = WToUtf8(DefaultNameFromVersion(in.versionId));
            LoadInstanceSettings(in);
            m_inst.push_back(std::move(in));
        }
    } catch (...) {}
}

void Store::LoadInstanceSettings(Instance& inst) {
    std::string txt = ReadTextFile(instanceSettingsPath(inst.id));
    if (txt.empty()) return;
    try {
        json::Value j = json::parse(txt);
        auto& s = inst.settings;
        s.javaPath = j.get("javaPath").asString();
        s.maxMemoryMB = (int)j.get("maxMemoryMB").asInt();
        s.width = (int)j.get("width").asInt();
        s.height = (int)j.get("height").asInt();
        s.fullscreen = j.get("fullscreen").asBool();
        s.isolate = j.has("isolate") ? (int)j.get("isolate").asInt() : -1;
        s.checkIntegrity = j.has("checkIntegrity") ? (int)j.get("checkIntegrity").asInt() : -1;
        s.nickname = j.get("nickname").asString();
        s.server = j.get("server").asString();
        s.serverPort = (int)j.get("serverPort").asInt();
        if (s.serverPort <= 0) s.serverPort = 25565;
        const json::Value& ja = j.get("jvmArgs");
        for (size_t k = 0; k < ja.size(); ++k) s.jvmArgs.push_back(ja.at(k).asString());
        const json::Value& ga = j.get("gameArgs");
        for (size_t k = 0; k < ga.size(); ++k) s.gameArgs.push_back(ga.at(k).asString());
    } catch (...) {}
}

void Store::SaveSettings() {
    json::Value j = json::makeObject();
    const auto& s = m_spec;
    j.obj["maxMemoryMB"] = json::makeInt(s.maxMemoryMB);
    j.obj["source"] = json::makeString(s.source);
    j.obj["isolate"] = json::makeBool(s.isolate);
    j.obj["checkIntegrity"] = json::makeBool(s.checkIntegrity);
    j.obj["autoRepair"] = json::makeBool(s.autoRepair);
    j.obj["backupSaves"] = json::makeBool(s.backupSaves);
    j.obj["width"] = json::makeInt(s.width);
    j.obj["height"] = json::makeInt(s.height);
    j.obj["javaPath"] = json::makeString(s.javaPath);
    j.obj["javaMajor"] = json::makeInt(s.javaMajor);
    j.obj["msClientId"] = json::makeString(s.msClientId);
    j.obj["accountStorage"] = json::makeString(s.accountStorage);
    j.obj["currentAccount"] = json::makeString(s.currentAccount);
    j.obj["downloadThreads"] = json::makeInt(s.downloadThreads);
    j.obj["keepLauncherOpen"] = json::makeBool(s.keepLauncherOpen);
    j.obj["directServer"] = json::makeString(s.directServer);
    j.obj["directPort"] = json::makeInt(s.directPort);
    j.obj["cfBase"] = json::makeString(s.cfBase);
    j.obj["cfApiKey"] = json::makeString(s.cfApiKey);
    json::Value sv = json::makeArray();
    for (auto& srv : s.servers) {
        json::Value e = json::makeObject();
        e.obj["address"] = json::makeString(srv.first);
        e.obj["port"] = json::makeInt(srv.second);
        sv.arr.push_back(e);
    }
    j.obj["servers"] = sv;
    WriteTextFile(settingsPath(), json::dumpPretty(j));
}

void Store::SaveInstances() {
    json::Value j = json::makeObject();
    j.obj["current"] = json::makeString(m_current);
    json::Value arr = json::makeArray();
    for (const auto& in : m_inst) {
        json::Value e = json::makeObject();
        e.obj["id"] = json::makeString(in.id);
        e.obj["name"] = json::makeString(in.name);
        e.obj["folder"] = json::makeString(in.folder);
        e.obj["versionId"] = json::makeString(in.versionId);
        e.obj["loader"] = json::makeString(in.loader);
        e.obj["loaderVersion"] = json::makeString(in.loaderVersion);
        e.obj["icon"] = json::makeString(in.iconPath);
        e.obj["lastPlayed"] = json::makeInt(in.lastPlayed);
        arr.arr.push_back(e);
    }
    j.obj["instances"] = arr;
    WriteTextFile(registryPath(), json::dumpPretty(j));
}

void Store::SaveInstanceSettings(const Instance& inst) {
    const auto& s = inst.settings;
    json::Value j = json::makeObject();
    j.obj["javaPath"] = json::makeString(s.javaPath);
    j.obj["maxMemoryMB"] = json::makeInt(s.maxMemoryMB);
    j.obj["width"] = json::makeInt(s.width);
    j.obj["height"] = json::makeInt(s.height);
    j.obj["fullscreen"] = json::makeBool(s.fullscreen);
    j.obj["isolate"] = json::makeInt(s.isolate);
    j.obj["checkIntegrity"] = json::makeInt(s.checkIntegrity);
    j.obj["nickname"] = json::makeString(s.nickname);
    j.obj["server"] = json::makeString(s.server);
    j.obj["serverPort"] = json::makeInt(s.serverPort);
    json::Value ja = json::makeArray();
    for (auto& a : s.jvmArgs) ja.arr.push_back(json::makeString(a));
    j.obj["jvmArgs"] = ja;
    json::Value ga = json::makeArray();
    for (auto& a : s.gameArgs) ga.arr.push_back(json::makeString(a));
    j.obj["gameArgs"] = ga;
    WriteTextFile(instanceSettingsPath(inst.id), json::dumpPretty(j));
}

std::vector<std::string>& Store::folders() { return m_folders; }

std::string Store::CurrentFolder() const {
    if (!m_curFolder.empty()) return m_curFolder;
    if (!m_folders.empty()) return m_folders.front();
    return std::string();
}

void Store::SaveFoldersFile() {
    json::Value j = json::makeObject();
    j.obj["current"] = json::makeString(m_curFolder);
    json::Value arr = json::makeArray();
    for (auto& f : m_folders) arr.arr.push_back(json::makeString(f));
    j.obj["folders"] = arr;
    WriteTextFile(foldersPath(), json::dumpPretty(j));
}

void Store::SetCurrentFolder(const std::string& folder) {
    if (folder.empty()) return;
    EnsureFolder(folder);
    m_curFolder = folder;
    SaveFoldersFile();
}

bool Store::AddFolderPath(const std::string& folder, std::string& err) {
    std::string f = folder;
    while (!f.empty() && (f.back() == '\\' || f.back() == '/')) f.pop_back();
    if (f.empty()) { err = "目录无效"; return false; }
    std::error_code ec;
    fs::create_directories(fs::u8path(f), ec);   // 不存在就建：允许先把文件夹准备好再下载
    for (auto& x : m_folders) {
        if (x == f) {
            if (m_curFolder.empty()) m_curFolder = f;
            SaveFoldersFile();
            err = "该文件夹已在列表中";
            return true;
        }
    }
    m_folders.push_back(f);
    m_curFolder = f;
    SaveFoldersFile();
    return true;
}

void Store::EnsureFolder(const std::string& folder) {
    if (folder.empty()) return;
    for (auto& x : m_folders) if (x == folder) return;
    m_folders.push_back(folder);
    if (m_curFolder.empty()) m_curFolder = folder;
    SaveFoldersFile();
}

bool Store::RemoveFolderPath(const std::string& folder, std::string& err) {
    int cnt = CountInstancesInFolder(folder);
    if (cnt > 0) {
        err = "该文件夹里还有 " + std::to_string(cnt) + " 个游戏，请先在列表里卸载它们";
        return false;
    }
    for (size_t i = 0; i < m_folders.size(); ++i) {
        if (m_folders[i] != folder) continue;
        m_folders.erase(m_folders.begin() + (std::ptrdiff_t)i);
        if (m_curFolder == folder) m_curFolder = m_folders.empty() ? "" : m_folders.front();
        SaveFoldersFile();
        return true;
    }
    err = "该文件夹不在列表中";
    return false;
}

int Store::CountInstancesInFolder(const std::string& folder) {
    int n = 0;
    for (auto& in : m_inst) if (in.folder == folder) ++n;
    return n;
}

std::vector<AccountRec>& Store::accounts() { return m_accs; }
std::string Store::AccountKey() const { return m_accKey; }

void Store::SetAccountKey(const std::string& key) {
    m_accKey = key;
    m_spec.currentAccount = key;      // 存在设置里：账号在便携/全局间迁移时不会丢
    SaveAccounts();
    SaveSettings();
}

AccountRec* Store::FindAccount(const std::string& key) {
    for (auto& a : m_accs) if (a.uuid == key || a.name == key) return &a;
    return nullptr;
}

AccountRec* Store::CurrentAccount() {
    AccountRec* p = FindAccount(m_accKey);
    if (p) return p;
    if (!m_accs.empty()) { m_accKey = m_accs.front().uuid; return &m_accs.front(); }
    return nullptr;
}

std::string Store::UpsertAccount(const AccountRec& rec) {
    std::string key = rec.uuid.empty() ? rec.name : rec.uuid;
    for (auto& a : m_accs) {
        if ((!rec.uuid.empty() && a.uuid == rec.uuid) ||
            (rec.uuid.empty() && a.name == rec.name && a.type == rec.type)) {
            a = rec;
            a.lastUsed = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            m_accKey = key;
            SaveAccounts();
            return key;
        }
    }
    AccountRec a = rec;
    a.lastUsed = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_accs.push_back(std::move(a));
    m_accKey = key;
    SaveAccounts();
    return key;
}

void Store::RemoveAccount(const std::string& key) {
    for (size_t i = 0; i < m_accs.size(); ++i) {
        if (m_accs[i].uuid != key && m_accs[i].name != key) continue;
        m_accs.erase(m_accs.begin() + (std::ptrdiff_t)i);
        if (m_accKey == key) m_accKey = m_accs.empty() ? "" : m_accs.front().uuid;
        SaveAccounts();
        return;
    }
}

void Store::LoadAccounts() {
    m_accs.clear();
    m_accKey.clear();

    // 两个账号文件合并读取：便携优先（同 key 的账号只保留便携那份，和 HMCL 一致）
    auto loadOne = [&](const std::string& path, bool portable) {
        std::string txt = ReadTextFile(path);
        if (txt.empty()) return;
        try {
            json::Value j = json::parse(txt);
            const json::Value& arr = j.get("accounts");
            for (size_t i = 0; i < arr.size(); ++i) {
                const json::Value& e = arr.at(i);
                AccountRec a;
                a.type = e.get("type").asString();
                a.name = e.get("name").asString();
                a.uuid = e.get("uuid").asString();
                a.accessToken = e.get("accessToken").asString();
                a.refreshToken = e.get("refreshToken").asString();
                a.expiresAt = e.get("expiresAt").asInt();
                a.lastUsed = e.get("lastUsed").asInt();
                a.skin = e.get("skin").asString();
                a.skinPath = e.get("skinPath").asString();
                a.cape = e.get("cape").asString();
                a.capePath = e.get("capePath").asString();
                a.skinModel = e.get("skinModel").asString();
                a.skinRemoteAt = e.get("skinRemoteAt").asInt();
                // 旧数据没有 portable 字段：便携文件里的默认 true，全局文件里的默认 false
                a.portable = e.has("portable") ? e.get("portable").asBool() : portable;
                if (a.name.empty()) continue;
                std::string k = a.uuid.empty() ? a.name : a.uuid;
                bool dup = false;
                for (const auto& x : m_accs) {
                    if ((x.uuid.empty() ? x.name : x.uuid) == k) { dup = true; break; }
                }
                if (!dup) m_accs.push_back(std::move(a));
            }
            // 兼容旧数据：没有设置里的 currentAccount 时，用账号文件里的 current
            if (m_accKey.empty()) m_accKey = j.get("current").asString();
        } catch (...) {}
    };

    loadOne(PortableAccountsDir() + "\\accounts.json", true);
    loadOne(GlobalAccountsDir() + "\\accounts.json", false);

    // 设置里的 currentAccount 权威（迁移账号不影响它），但要确认账号还存在
    if (!m_spec.currentAccount.empty()) {
        for (const auto& a : m_accs) {
            if ((a.uuid.empty() ? a.name : a.uuid) == m_spec.currentAccount) {
                m_accKey = m_spec.currentAccount;
                break;
            }
        }
    }
    if (!m_accKey.empty()) {
        bool found = false;
        for (const auto& a : m_accs) {
            if ((a.uuid.empty() ? a.name : a.uuid) == m_accKey) { found = true; break; }
        }
        if (!found) m_accKey.clear();
    }
}

void Store::ReloadAccounts() { LoadAccounts(); }

std::string Store::SkinFileOf(const AccountRec& a) const {
    if (a.skin.empty()) return std::string();
    std::error_code ec;
    const std::string own = (a.portable ? PortableAccountsDir() : GlobalAccountsDir()) +
                            "\\skins\\" + a.skin;
    if (fs::is_regular_file(fs::u8path(own), ec)) return own;
    // 兜底：旧版切换「便携/全局」用的是复制而不是迁移，可能出现账号在这一侧、
    // 皮肤只留在另一侧。这里跨侧找一次，避免升级后皮肤显示成默认。
    const std::string other = (a.portable ? GlobalAccountsDir() : PortableAccountsDir()) +
                              "\\skins\\" + a.skin;
    if (fs::is_regular_file(fs::u8path(other), ec)) return other;
    return std::string();
}

std::wstring Store::EffectiveSkin(const AccountRec& a) const {
    // 1) 自定义绝对路径 2) 该账号所在那一侧的 skins\ 托管文件
    if (!a.skinPath.empty()) {
        std::error_code ec;
        if (fs::is_regular_file(fs::u8path(a.skinPath), ec)) return Utf8ToW(a.skinPath);
    }
    std::string p = SkinFileOf(a);
    return p.empty() ? std::wstring() : Utf8ToW(p);
}

std::wstring Store::EffectiveCape(const AccountRec& a) const {
    if (!a.capePath.empty()) {
        std::error_code ec;
        if (fs::is_regular_file(fs::u8path(a.capePath), ec)) return Utf8ToW(a.capePath);
    }
    if (!a.cape.empty()) {
        std::error_code ec;
        const std::string own = (a.portable ? PortableAccountsDir() : GlobalAccountsDir()) +
                                "\\skins\\" + a.cape;
        if (fs::is_regular_file(fs::u8path(own), ec)) return Utf8ToW(own);
        const std::string other = (a.portable ? GlobalAccountsDir() : PortableAccountsDir()) +
                                  "\\skins\\" + a.cape;
        if (fs::is_regular_file(fs::u8path(other), ec)) return Utf8ToW(other);
    }
    return std::wstring();
}

bool Store::IsSlim(const AccountRec& a) const { return a.skinModel == "slim"; }

bool Store::MoveAccount(const std::string& key, bool toPortable, std::string& err) {
    AccountRec* a = FindAccount(key);
    if (!a) { err = "账号不存在"; return false; }
    if (a->portable == toPortable) return true;      // 已经在这一侧

    const std::string fromDir = a->portable ? PortableAccountsDir() : GlobalAccountsDir();
    const std::string toDir = toPortable ? PortableAccountsDir() : GlobalAccountsDir();

    // 托管皮肤文件名 = <账号 key>.png / <key>-cape.png
    std::string name = a->uuid.empty() ? a->name : a->uuid;
    for (char& c : name) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    }

    std::error_code ec;
    fs::create_directories(fs::u8path(toDir) / "skins", ec);
    if (ec) { err = "无法创建目录：" + ec.message(); return false; }

    for (const char* suffix : { ".png", "-cape.png" }) {
        std::string f = name + suffix;
        std::string src = fromDir + "\\skins\\" + f;
        std::string dst = toDir + "\\skins\\" + f;
        std::error_code ec2;
        if (!fs::is_regular_file(fs::u8path(src), ec2)) continue;
        fs::copy_file(fs::u8path(src), fs::u8path(dst),
                      fs::copy_options::overwrite_existing, ec2);
        if (ec2) { err = "复制皮肤失败：" + ec2.message(); return false; }
        // 迁移而不是复制：否则两个目录各留一份，改一边另一边还是旧图
        std::error_code ec3;
        fs::remove(fs::u8path(src), ec3);
    }

    a->portable = toPortable;
    if (m_accKey == key) m_spec.currentAccount = m_accKey;
    SaveAccounts();
    SaveSettings();
    return true;
}

void Store::SaveAccounts() {
    // 按 portable 分两个文件写（照 HMCL 的 accounts.json / 用户账号文件）
    for (int pass = 0; pass < 2; ++pass) {
        const bool portable = (pass == 0);
        const std::string dir = portable ? PortableAccountsDir() : GlobalAccountsDir();

        json::Value j = json::makeObject();
        json::Value arr = json::makeArray();
        bool hasCurrent = false;
        for (const auto& a : m_accs) {
            if (a.portable != portable) continue;
            if ((a.uuid.empty() ? a.name : a.uuid) == m_accKey) hasCurrent = true;
            json::Value e = json::makeObject();
            e.obj["type"] = json::makeString(a.type);
            e.obj["name"] = json::makeString(a.name);
            e.obj["uuid"] = json::makeString(a.uuid);
            e.obj["accessToken"] = json::makeString(a.accessToken);
            e.obj["refreshToken"] = json::makeString(a.refreshToken);
            e.obj["expiresAt"] = json::makeInt(a.expiresAt);
            e.obj["lastUsed"] = json::makeInt(a.lastUsed);
            e.obj["skin"] = json::makeString(a.skin);
            e.obj["skinPath"] = json::makeString(a.skinPath);
            e.obj["cape"] = json::makeString(a.cape);
            e.obj["capePath"] = json::makeString(a.capePath);
            e.obj["skinModel"] = json::makeString(a.skinModel);
            e.obj["skinRemoteAt"] = json::makeInt(a.skinRemoteAt);
            e.obj["portable"] = json::makeBool(a.portable);
            arr.arr.push_back(e);
        }
        j.obj["accounts"] = arr;
        j.obj["current"] = json::makeString(hasCurrent ? m_accKey : "");

        std::error_code ec;
        fs::create_directories(fs::u8path(dir) / "skins", ec);
        WriteTextFile(dir + "\\accounts.json", json::dumpPretty(j));
    }
}

Instance* Store::Find(const std::string& id) {    for (auto& in : m_inst) if (in.id == id) return &in;
    return nullptr;
}

Instance* Store::Current() {
    Instance* p = Find(m_current);
    if (p) return p;
    if (!m_inst.empty()) { m_current = m_inst.front().id; return &m_inst.front(); }
    return nullptr;
}

std::string Store::CurrentId() const { return m_current; }

void Store::SetCurrent(const std::string& id) {
    m_current = id;
    SaveInstances();
}

std::vector<std::string> Store::VersionsInFolder(const std::string& folder) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::path vd = fs::u8path(JoinPath(folder, "versions"));
    if (!fs::is_directory(vd, ec)) return out;
    for (auto& e : fs::directory_iterator(vd, ec)) {
        if (!e.is_directory(ec)) continue;
        std::string id = WToUtf8(e.path().filename().wstring());
        if (id.empty()) continue;
        if (!FileExists(WToUtf8((e.path() / fs::u8path(id + ".json")).wstring()))) continue;
        out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool Store::FolderRegistered(const std::string& folder) {
    for (auto& in : m_inst) if (in.folder == folder) return true;
    return false;
}

int Store::AddFolder(const std::wstring& folderW, std::string& err) {
    std::string folder = WToUtf8(folderW);
    while (!folder.empty() && (folder.back() == '\\' || folder.back() == '/')) folder.pop_back();
    if (folder.empty()) { err = "目录无效"; return 0; }
    // 允许目录暂不存在：AddFolderPath 会创建它

    // 支持直接选择 versions 子目录或 .minecraft 根目录
    if (!DirExists(JoinPath(folder, "versions"))) {
        std::string parent = folder;
        size_t s = parent.find_last_of("\\/");
        if (s != std::string::npos) {
            std::string p = parent.substr(0, s);
            if (DirExists(JoinPath(p, "versions"))) folder = p;
        }
    }

    // 允许空文件夹：先登记为「下载目标」，再扫描已有版本
    AddFolderPath(folder, err);
    err.clear();

    auto vers = VersionsInFolder(folder);
    int added = 0;
    for (const auto& vid : vers) {
        bool dup = false;
        for (auto& in : m_inst) {
            if (in.folder == folder && in.versionId == vid) { dup = true; break; }
        }
        if (dup) continue;
        AddInstance(folder, vid, "");
        ++added;
    }
    if (added == 0) err = "该文件夹已登记（目前没有已安装版本，可直接作为下载目标）";
    return added;
}

std::string Store::AddInstance(const std::string& folder, const std::string& versionId,
                               const std::string& name) {
    Instance in;
    in.folder = folder;
    in.versionId = versionId;
    in.id = MakeId(folder + "|" + versionId);
    // id 冲突（极小概率）追加序号
    int bump = 1;
    while (Find(in.id)) { in.id += "_" + std::to_string(bump++); }

    // 显示名：用户输入的名称按规则清洗；自动名直接用版本 id（1.20.1 这类含点号的保持可读）
    std::string base = name.empty() ? versionId : name;
    std::wstring w = Utf8ToW(base);
    if (!name.empty()) {
        if (!IsValidGameName(w)) w = SanitizeGameName(w);
        if (w.empty()) w = DefaultNameFromVersion(versionId);
    }
    if (w.empty()) w = Utf8ToW(versionId);
    in.name = WToUtf8(w);
    // 名称去重
    int n = 2;
    std::string tryName = in.name;
    auto nameTaken = [&](const std::string& nm) {
        for (auto& x : m_inst) if (x.name == nm) return true;
        return false;
    };
    while (nameTaken(tryName)) tryName = in.name + " (" + std::to_string(n++) + ")";
    in.name = tryName;

    DetectLoader(in);
    m_inst.push_back(std::move(in));
    Instance& added = m_inst.back();
    SaveInstances();
    if (m_current.empty()) m_current = added.id;
    SaveInstances();
    return added.id;
}

std::string Store::EnsureInstance(const std::string& folder, const std::string& versionId) {
    for (auto& in : m_inst) {
        if (in.folder == folder && in.versionId == versionId) return in.id;
    }
    return AddInstance(folder, versionId, "");
}

void Store::RemoveInstance(const std::string& id) {
    for (size_t i = 0; i < m_inst.size(); ++i) {
        if (m_inst[i].id != id) continue;
        std::error_code ec;
        fs::remove_all(fs::u8path(instanceSettingsPath(id)).parent_path(), ec);
        m_inst.erase(m_inst.begin() + i);
        if (m_current == id) m_current = m_inst.empty() ? "" : m_inst.front().id;
        SaveInstances();
        return;
    }
}

bool Store::DeleteInstanceFiles(const std::string& id, std::string& err) {
    Instance* in = Find(id);
    if (!in) { err = "实例不存在"; return false; }
    fs::path vdir = fs::u8path(JoinPath(in->versionsDir(), in->versionId));
    std::error_code ec;
    if (!fs::is_directory(vdir, ec)) { err = "版本目录不存在: " + vdir.u8string(); return false; }
    fs::remove_all(vdir, ec);
    if (ec) { err = "删除失败: " + ec.message(); return false; }
    RemoveInstance(id);
    return true;
}

void Store::Touch(const std::string& id) {
    Instance* in = Find(id);
    if (!in) return;
    in->lastPlayed = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    SaveInstances();
}

void Store::DetectLoader(Instance& inst) {
    std::string jsonPath = JoinPath(JoinPath(JoinPath(inst.folder, "versions"), inst.versionId),
                                    inst.versionId + ".json");
    std::string txt = ReadTextFile(jsonPath);
    inst.loader.clear();
    inst.loaderVersion.clear();
    if (txt.empty()) return;
    std::string vid, parent;
    try {
        json::Value j = json::parse(txt);
        vid = j.get("id").asString();
        parent = j.get("inheritsFrom").asString();
    } catch (...) { return; }

    std::string low = vid;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    auto pick = [&](const char* tag, const char* name) {
        size_t p = low.find(tag);
        if (p == std::string::npos) return false;
        inst.loader = name;
        // 版本号：tag 之后、"<mcver>" 之前，形如 fabric-loader-0.15.11-1.20.4
        std::string rest = vid.substr(p + strlen(tag));
        while (!rest.empty() && (rest.front() == '-' || rest.front() == '_')) rest.erase(rest.begin());
        size_t dash = rest.find('-');
        inst.loaderVersion = (dash == std::string::npos) ? rest : rest.substr(0, dash);
        return true;
    };

    if (pick("fabric-loader", "fabric")) return;
    if (pick("quilt-loader", "quilt")) return;
    if (pick("neoforge", "neoforge")) return;
    if (pick("forge", "forge")) return;
}

} // namespace mlc
