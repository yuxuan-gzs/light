// LauncherApp.cpp — 启动器状态机实现
#include "LauncherApp.h"
#include "Json.h"
#include "Http.h"
#include "Crypto.h"
#include "Theme.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <shlobj.h>

namespace launcher {

namespace fs = std::filesystem;

static std::wstring configPath() {
    wchar_t appdata[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata) == S_OK) {
        return std::wstring(appdata) + L"\\YXGLauncher\\config.json";
    }
    return L"yxg-launcher-config.json";
}

static std::string defaultGameDir() {
    const char* appdata = std::getenv("APPDATA");
    std::string base = appdata && *appdata ? appdata : "C:\\Users\\Public";
    return base + "\\.minecraft";
}

App& App::Instance() {
    static App inst;
    return inst;
}

App::App() {
    m_gameDir = defaultGameDir();
    loadConfig();
    if (m_javaPath.empty()) {
        java::JavaRuntime jr = java::DetectBest(8);
        if (!jr.path.empty()) m_javaPath = jr.path;
    }
}

App::~App() {
    abortInstall();
    if (m_installThread.joinable()) m_installThread.join();
    if (m_waitThread.joinable()) m_waitThread.join();
    if (m_hProcess) { CloseHandle(m_hProcess); m_hProcess = nullptr; }
}

void App::setGameDir(const std::string& d) {
    std::string s = d;
    while (!s.empty() && (s.back() == '\\' || s.back() == '/')) s.pop_back();
    m_gameDir = s;
}

void App::setSource(const std::string& name) {
    if (name == "mojang") m_source = mc::DownloadSource::Mojang;
    else if (name == "mcbbs") m_source = mc::DownloadSource::Mcbbs;
    else m_source = mc::DownloadSource::Bmclapi;
}

void App::loadConfig() {
    std::ifstream f(configPath(), std::ios::binary);
    if (!f.is_open()) return;
    std::stringstream ss; ss << f.rdbuf();
    try {
        json::Value j = json::parse(ss.str());
        const json::Value& l = j.get("launcher");
        if (l.isObject()) {
            if (!l.get("gameDir").asString().empty()) setGameDir(l.get("gameDir").asString());
            if (!l.get("source").asString().empty()) setSource(l.get("source").asString());
            if (!l.get("java").asString().empty()) m_javaPath = l.get("java").asString();
            if (l.get("maxMemory").asInt() > 0) m_maxMemory = (int)l.get("maxMemory").asInt();
            if (l.get("isolation").asBool()) m_versionIsolation = true;
            const json::Value& sv = l.get("servers");
            for (size_t i = 0; i < sv.size(); ++i) {
                std::string addr = sv.at(i).get("address").asString();
                int port = (int)sv.at(i).get("port").asInt();
                if (!addr.empty()) m_servers.push_back({ addr, port > 0 ? port : 25565 });
            }
            const json::Value& vs = l.get("versionSettings");
            for (size_t i = 0; i < vs.size(); ++i) {
                const json::Value& e = vs.at(i);
                std::string vid = e.get("versionId").asString();
                if (vid.empty()) continue;
                VersionSettings s;
                s.javaPath = e.get("javaPath").asString();
                s.maxMemoryMB = (int)e.get("maxMemoryMB").asInt();
                s.width = (int)e.get("width").asInt();
                s.height = (int)e.get("height").asInt();
                s.fullscreen = e.get("fullscreen").asBool();
                s.server = e.get("server").asString();
                s.serverPort = (int)e.get("serverPort").asInt();
                const json::Value& ja = e.get("jvmArgs");
                for (size_t k = 0; k < ja.size(); ++k) s.jvmArgs.push_back(ja.at(k).asString());
                const json::Value& ga = e.get("gameArgs");
                for (size_t k = 0; k < ga.size(); ++k) s.gameArgs.push_back(ga.at(k).asString());
                if (s.width <= 0) s.width = 854;
                if (s.height <= 0) s.height = 480;
                if (s.serverPort <= 0) s.serverPort = 25565;
                m_versionSettings[vid] = std::move(s);
            }
        }
    } catch (...) {}
}

void App::saveConfig() {
    json::Value j;
    bool loaded = false;
    {
        std::ifstream f(configPath(), std::ios::binary);
        if (f.is_open()) {
            std::stringstream ss; ss << f.rdbuf();
            try { j = json::parse(ss.str()); loaded = true; } catch (...) {}
        }
    }
    if (!loaded) j = json::makeObject();

    json::Value l = json::makeObject();
    l.obj["gameDir"] = json::makeString(m_gameDir);
    l.obj["source"] = json::makeString(mc::sourceName(m_source));
    l.obj["java"] = json::makeString(m_javaPath);
    l.obj["maxMemory"] = json::makeInt(m_maxMemory);
    l.obj["isolation"] = json::makeBool(m_versionIsolation);
    json::Value servers = json::makeArray();
    for (auto& s : m_servers) {
        json::Value e = json::makeObject();
        e.obj["address"] = json::makeString(s.first);
        e.obj["port"] = json::makeInt(s.second);
        servers.arr.push_back(e);
    }
    l.obj["servers"] = servers;

    // 版本设置
    json::Value verSets = json::makeArray();
    {
        std::lock_guard<std::mutex> lk(m_verSetMu);
        for (auto& kv : m_versionSettings) {
            const std::string& vid = kv.first;
            const VersionSettings& s = kv.second;
            json::Value e = json::makeObject();
            e.obj["versionId"] = json::makeString(vid);
            e.obj["javaPath"] = json::makeString(s.javaPath);
            e.obj["maxMemoryMB"] = json::makeInt(s.maxMemoryMB);
            e.obj["width"] = json::makeInt(s.width);
            e.obj["height"] = json::makeInt(s.height);
            e.obj["fullscreen"] = json::makeBool(s.fullscreen);
            e.obj["server"] = json::makeString(s.server);
            e.obj["serverPort"] = json::makeInt(s.serverPort);
            json::Value ja = json::makeArray();
            for (auto& a : s.jvmArgs) ja.arr.push_back(json::makeString(a));
            e.obj["jvmArgs"] = ja;
            json::Value ga = json::makeArray();
            for (auto& a : s.gameArgs) ga.arr.push_back(json::makeString(a));
            e.obj["gameArgs"] = ga;
            verSets.arr.push_back(e);
        }
    }
    l.obj["versionSettings"] = verSets;
    j.obj["launcher"] = l;

    std::error_code ec;
    fs::create_directories(fs::path(configPath()).parent_path(), ec);
    std::ofstream f(configPath(), std::ios::binary | std::ios::trunc);
    if (f.is_open()) f << json::dumpPretty(j);
}

std::vector<std::pair<std::string,int>> App::servers() const {
    std::lock_guard<std::mutex> lk(m_serverMu);
    return m_servers;
}

void App::addServer(const std::string& address, int port) {
    if (address.empty()) return;
    {
        std::lock_guard<std::mutex> lk(m_serverMu);
        for (auto& s : m_servers) if (s.first == address && s.second == port) return;
        m_servers.push_back({ address, port > 0 ? port : 25565 });
    }
    saveConfig();
}

void App::removeServer(size_t index) {
    {
        std::lock_guard<std::mutex> lk(m_serverMu);
        if (index < m_servers.size()) m_servers.erase(m_servers.begin() + index);
    }
    saveConfig();
}

void App::setDirectServer(const std::string& address, int port) {
    std::lock_guard<std::mutex> lk(m_serverMu);
    m_directServer = address;
    m_directPort = port > 0 ? port : 25565;
}

std::string App::directServer() const {
    std::lock_guard<std::mutex> lk(m_serverMu);
    return m_directServer;
}

int App::directPort() const {
    std::lock_guard<std::mutex> lk(m_serverMu);
    return m_directPort;
}

VersionSettings App::versionSettings(const std::string& versionId) const {
    std::lock_guard<std::mutex> lk(m_verSetMu);
    auto it = m_versionSettings.find(versionId);
    if (it != m_versionSettings.end()) return it->second;
    return VersionSettings{};
}

void App::setVersionSettings(const std::string& versionId, const VersionSettings& s) {
    {
        std::lock_guard<std::mutex> lk(m_verSetMu);
        m_versionSettings[versionId] = s;
    }
    saveConfig();
}

void App::emit(Ev type, const std::string& data) {
    if (m_cb) m_cb(type, data);
}

mc::VersionManifest App::manifest() const {
    std::lock_guard<std::mutex> lk(m_manifestMu);
    return m_manifest;
}

bool App::refreshRemote(std::string& err) {
    mc::VersionManifest mf;
    if (!mc::fetchManifest(m_source, mf, err)) return false;
    {
        std::lock_guard<std::mutex> lk(m_manifestMu);
        m_manifest = std::move(mf);
        m_manifestLoaded = true;
    }
    return true;
}

mc::InstallProgress App::installProgress() {
    std::lock_guard<std::mutex> lk(m_installMu);
    return m_installProgress;
}

void App::installWorker(const std::string& versionId) {
    m_installing.store(true);
    m_abort.store(false);
    {
        std::lock_guard<std::mutex> lk(m_installMu);
        m_installProgress = mc::InstallProgress{};
    }
    std::string err;

    mc::VersionManifest mf;
    if (!m_manifestLoaded) {
        if (!mc::fetchManifest(m_source, mf, err)) {
            m_installing.store(false);
            emit(Ev::InstallError, err.empty() ? "无法获取版本清单" : err);
            return;
        }
        std::lock_guard<std::mutex> lk(m_manifestMu);
        m_manifest = mf;
        m_manifestLoaded = true;
    } else {
        mf = manifest();
    }

    bool ok = mc::installVersion(
        m_gameDir + "\\versions", m_gameDir + "\\libraries", m_gameDir + "\\assets",
        mf, versionId, m_source,
        [this](const mc::InstallProgress& p) -> bool {
            if (m_abort.load()) return false;
            {
                std::lock_guard<std::mutex> lk(m_installMu);
                m_installProgress = p;
            }
            // 通知 UI
            json::Value v = json::makeObject();
            v.obj["phase"] = json::makeString(p.phase);
            v.obj["done"] = json::makeInt(p.done);
            v.obj["total"] = json::makeInt(p.total);
            v.obj["current"] = json::makeString(p.current);
            emit(Ev::InstallProgress, json::dump(v));
            return true;
        }, err);

    m_installing.store(false);
    {
        std::lock_guard<std::mutex> lk(m_installMu);
        m_installProgress.finished = true;
        m_installProgress.error = ok ? "" : err;
    }
    if (ok) emit(Ev::InstallDone, versionId);
    else emit(Ev::InstallError, err.empty() ? "安装失败" : err);
}

void App::startInstall(const std::string& versionId) {
    if (m_installing.load()) return;
    if (m_installThread.joinable()) m_installThread.join();
    m_installThread = std::thread(&App::installWorker, this, versionId);
}

void App::installWithLoaderWorker(const std::string& versionId, int loaderType,
                                  const std::string& loaderVersion) {
    m_installing.store(true);
    m_abort.store(false);
    std::string err;

    // 1. 装原版
    mc::VersionManifest mf;
    if (!m_manifestLoaded) {
        if (!mc::fetchManifest(m_source, mf, err)) {
            m_installing.store(false);
            emit(Ev::InstallError, err.empty() ? "无法获取版本清单" : err);
            return;
        }
        std::lock_guard<std::mutex> lk(m_manifestMu);
        m_manifest = mf;
        m_manifestLoaded = true;
    } else {
        mf = manifest();
    }

    bool ok = mc::installVersion(
        m_gameDir + "\\versions", m_gameDir + "\\libraries", m_gameDir + "\\assets",
        mf, versionId, m_source,
        [this](const mc::InstallProgress& p) -> bool {
            if (m_abort.load()) return false;
            {
                std::lock_guard<std::mutex> lk(m_installMu);
                m_installProgress = p;
            }
            json::Value v = json::makeObject();
            v.obj["phase"] = json::makeString(p.phase);
            v.obj["done"] = json::makeInt(p.done);
            v.obj["total"] = json::makeInt(p.total);
            v.obj["current"] = json::makeString(p.current);
            emit(Ev::InstallProgress, json::dump(v));
            return true;
        }, err);
    if (!ok) {
        m_installing.store(false);
        emit(Ev::InstallError, err.empty() ? "安装失败" : err);
        return;
    }

    // 2. 装加载器（1 Fabric 2 Quilt 3 Forge 4 NeoForge）
    if (loaderType >= 1 && loaderType <= 4) {
        emit(Ev::Log, "正在安装加载器...");
        if (loaderType == 1 || loaderType == 2) {
            bool quilt = (loaderType == 2);
            std::string lv = loaderVersion;
            if (lv.empty()) {
                auto vers = loader::FetchVersions(versionId, quilt, err);
                for (auto& v : vers) if (v.stable) { lv = v.version; break; }
                if (lv.empty() && !vers.empty()) lv = vers[0].version;
            }
            if (lv.empty()) {
                m_installing.store(false);
                emit(Ev::InstallError, err.empty() ? "无可用加载器版本" : err);
                return;
            }
            std::string id = loader::Install(versionId, lv, quilt,
                m_gameDir + "\\versions", m_gameDir + "\\libraries",
                [this](const std::string& msg) { emit(Ev::Log, msg); }, err);
            if (id.empty()) {
                m_installing.store(false);
                emit(Ev::InstallError, err.empty() ? "安装加载器失败" : err);
                return;
            }
        } else if (loaderType == 3) {
            std::string fv = loaderVersion;
            if (fv.empty()) {
                auto vers = loader::FetchForgeVersions(versionId, err);
                if (vers.empty()) {
                    m_installing.store(false);
                    emit(Ev::InstallError, err.empty() ? "无可用 Forge 版本" : err);
                    return;
                }
                fv = vers[0];
            }
            std::string java = m_javaPath;
            if (java.empty()) { auto jr = java::DetectBest(17); java = jr.path; }
            if (!loader::InstallForge(fv, m_gameDir, java,
                [this](const std::string& msg) { emit(Ev::Log, msg); }, err)) {
                m_installing.store(false);
                emit(Ev::InstallError, err.empty() ? "安装 Forge 失败" : err);
                return;
            }
        } else {
            std::string nv = loaderVersion;
            if (nv.empty()) {
                auto vers = loader::FetchNeoForgeVersions(versionId, err);
                if (vers.empty()) {
                    m_installing.store(false);
                    emit(Ev::InstallError, err.empty() ? "无可用 NeoForge 版本" : err);
                    return;
                }
                nv = vers[0];
            }
            std::string java = m_javaPath;
            if (java.empty()) { auto jr = java::DetectBest(17); java = jr.path; }
            if (!loader::InstallNeoForge(nv, m_gameDir, java,
                [this](const std::string& msg) { emit(Ev::Log, msg); }, err)) {
                m_installing.store(false);
                emit(Ev::InstallError, err.empty() ? "安装 NeoForge 失败" : err);
                return;
            }
        }
    }

    m_installing.store(false);
    emit(Ev::InstallDone, versionId);
}

void App::startInstallWithLoader(const std::string& versionId, int loaderType,
                                 const std::string& loaderVersion) {
    if (m_installing.load()) return;
    if (m_installThread.joinable()) m_installThread.join();
    m_installThread = std::thread(&App::installWithLoaderWorker, this,
                                  versionId, loaderType, loaderVersion);
}

void App::installLoaderWorker(const std::string& mcVersion,
                              const std::string& loaderVersion, bool quilt) {
    m_installing.store(true);
    m_abort.store(false);
    std::string err;
    std::string id = loader::Install(mcVersion, loaderVersion, quilt,
        m_gameDir + "\\versions", m_gameDir + "\\libraries",
        [this](const std::string& msg) { emit(Ev::Log, msg); }, err);
    m_installing.store(false);
    if (!id.empty()) emit(Ev::InstallDone, id);
    else emit(Ev::InstallError, err.empty() ? "安装加载器失败" : err);
}

void App::startInstallLoader(const std::string& mcVersion,
                             const std::string& loaderVersion, bool quilt) {
    if (m_installing.load()) return;
    if (m_installThread.joinable()) m_installThread.join();
    m_installThread = std::thread(&App::installLoaderWorker, this,
                                  mcVersion, loaderVersion, quilt);
}

void App::installForgeWorker(const std::string& fullVersion) {
    m_installing.store(true);
    m_abort.store(false);
    std::string err;
    std::string java = m_javaPath;
    if (java.empty()) { auto jr = java::DetectBest(17); java = jr.path; }
    bool ok = loader::InstallForge(fullVersion, m_gameDir, java,
        [this](const std::string& msg) { emit(Ev::Log, msg); }, err);
    m_installing.store(false);
    if (ok) emit(Ev::InstallDone, fullVersion);
    else emit(Ev::InstallError, err.empty() ? "安装 Forge 失败" : err);
}

void App::startInstallForge(const std::string& fullVersion) {
    if (m_installing.load()) return;
    if (m_installThread.joinable()) m_installThread.join();
    m_installThread = std::thread(&App::installForgeWorker, this, fullVersion);
}

void App::installNeoForgeWorker(const std::string& fullVersion) {
    m_installing.store(true);
    m_abort.store(false);
    std::string err;
    std::string java = m_javaPath;
    if (java.empty()) { auto jr = java::DetectBest(17); java = jr.path; }
    bool ok = loader::InstallNeoForge(fullVersion, m_gameDir, java,
        [this](const std::string& msg) { emit(Ev::Log, msg); }, err);
    m_installing.store(false);
    if (ok) emit(Ev::InstallDone, fullVersion);
    else emit(Ev::InstallError, err.empty() ? "安装 NeoForge 失败" : err);
}

void App::startInstallNeoForge(const std::string& fullVersion) {
    if (m_installing.load()) return;
    if (m_installThread.joinable()) m_installThread.join();
    m_installThread = std::thread(&App::installNeoForgeWorker, this, fullVersion);
}

msauth::Account App::account() const {
    std::lock_guard<std::mutex> lk(m_accountMu);
    return m_account;
}

void App::authOffline(const std::string& name) {
    std::string uuid = crypto::offlineUuid(name);
    msauth::Account acc;
    acc.name = name; acc.uuid = uuid; acc.accessToken = "0"; acc.ok = true;
    {
        std::lock_guard<std::mutex> lk(m_accountMu);
        m_account = acc;
        m_accountType = "offline";
    }
    emit(Ev::AccountChange);
}

bool App::authMsStart(msauth::DeviceCode& dc, std::string& err) {
    if (!msauth::StartDeviceCodeFlow(dc, err)) return false;
    m_deviceCode = dc;
    return true;
}

int App::authMsPoll(msauth::Account& acc, std::string& err) {
    int r = msauth::PollForToken(m_deviceCode, acc, err);
    if (r == 1) {
        std::lock_guard<std::mutex> lk(m_accountMu);
        m_account = acc;
        m_accountType = "msa";
        emit(Ev::AccountChange);
    }
    return r;
}

bool App::launch(const std::string& versionId, int width, int height, int mem,
                 std::string& err) {
    mc::Version v;
    if (!mc::loadVersion(m_gameDir + "\\versions", versionId, v, err)) return false;

    std::string java = m_javaPath;
    if (java.empty()) { auto jr = java::DetectBest(v.javaMajor); java = jr.path; }
    if (java.empty()) { err = "未找到 Java，请先安装 Java 或在设置中指定路径"; return false; }
    if (java.size() >= 10 && java.substr(java.size() - 10) == "javaw.exe") {
        java = java.substr(0, java.size() - 10) + "java.exe";
    }

    msauth::Account acc = account();
    if (acc.name.empty()) { err = "请先登录账号"; return false; }

    // 应用该版本的独立设置（HMCL 的「版本设置」）
    VersionSettings vs = versionSettings(versionId);
    if (!vs.javaPath.empty()) {
        java = vs.javaPath;
        if (java.size() >= 10 && java.substr(java.size() - 10) == "javaw.exe") {
            java = java.substr(0, java.size() - 10) + "java.exe";
        }
    }

    mc::LaunchOptions opt;
    opt.gameDir = m_gameDir;
    if (m_versionIsolation) {
        // 版本隔离：游戏数据（saves/config/mods）放到该版本自己的目录
        opt.dataDir = m_gameDir + "\\versions\\" + versionId + "\\data";
    }
    opt.javaPath = java;
    opt.authName = acc.name;
    opt.authUuid = acc.uuid;
    opt.authToken = acc.accessToken.empty() ? "0" : acc.accessToken;
    opt.userType = (m_accountType == "msa") ? "msa" : "legacy";
    // 窗口大小：传入参数 > 版本设置 > 默认
    opt.width = width > 0 ? width : (vs.width > 0 ? vs.width : 854);
    opt.height = height > 0 ? height : (vs.height > 0 ? vs.height : 480);
    // 内存：版本设置覆盖 > 传入参数 > 全局
    opt.maxMemoryMB = vs.maxMemoryMB > 0 ? vs.maxMemoryMB : (mem > 0 ? mem : m_maxMemory);
    opt.extraJvmArgs = vs.jvmArgs;
    opt.extraGameArgs = vs.gameArgs;
    if (vs.fullscreen) opt.extraGameArgs.push_back("--fullscreen");
    // 直连服务器：版本设置 > 全局直连
    if (!vs.server.empty()) {
        opt.server = vs.server;
        opt.serverPort = vs.serverPort;
    } else if (!m_directServer.empty()) {
        opt.server = m_directServer;
        opt.serverPort = m_directPort;
    }

    std::vector<std::string> args;
    if (!mc::buildLaunchCommand(v, opt, args, err)) return false;

    // 拼接命令行
    auto quote = [](const std::string& a) {
        if (a.find_first_of(" \t\n\"") == std::string::npos) return a;
        std::string o = "\"";
        for (char c : a) { if (c == '"') o += "\\\""; else o += c; }
        o += "\"";
        return o;
    };
    std::string cmdline = quote(java);
    for (const auto& a : args) cmdline += " " + quote(a);

    fs::path logDir = fs::u8path(m_gameDir) / "logs";
    std::error_code ec; fs::create_directories(logDir, ec);
    m_logPath = (logDir / "yxg-launcher.log").u8string();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hLog = CreateFileW(theme::Utf8ToW(m_logPath).c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hLog;
    si.hStdError = hLog;

    PROCESS_INFORMATION pi{};
    std::wstring app = theme::Utf8ToW(java);
    std::wstring cl = theme::Utf8ToW(cmdline);
    std::wstring wd = theme::Utf8ToW(m_gameDir);

    BOOL ok = CreateProcessW(app.c_str(), cl.data(), nullptr, nullptr,
                             hLog != INVALID_HANDLE_VALUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             nullptr, wd.c_str(), &si, &pi);
    if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
    if (!ok) { err = "CreateProcess 失败，错误码 " + std::to_string(GetLastError()); return false; }

    CloseHandle(pi.hThread);
    {
        std::lock_guard<std::mutex> lk(m_gameMu);
        if (m_hProcess) CloseHandle(m_hProcess);
        m_hProcess = pi.hProcess;
        m_pid = pi.dwProcessId;
        m_gameExit.store(-1);
    }
    if (m_waitThread.joinable()) m_waitThread.join();
    m_waitThread = std::thread(&App::waitWorker, this);
    emit(Ev::GameStart, std::to_string(pi.dwProcessId));
    return true;
}

void App::waitWorker() {
    HANDLE h = nullptr;
    { std::lock_guard<std::mutex> lk(m_gameMu); h = m_hProcess; }
    if (h) {
        WaitForSingleObject(h, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(h, &code);
        m_gameExit.store((int)code);
        { std::lock_guard<std::mutex> lk(m_gameMu); CloseHandle(h); m_hProcess = nullptr; m_pid = 0; }
    }
    emit(Ev::GameExit, std::to_string(m_gameExit.load()));
}

bool App::gameRunning() const {
    std::lock_guard<std::mutex> lk(m_gameMu);
    return m_hProcess != nullptr;
}

void App::stopGame() {
    std::lock_guard<std::mutex> lk(m_gameMu);
    if (m_hProcess) TerminateProcess(m_hProcess, 1);
}

std::string App::gameLogTail(size_t maxBytes) const {
    if (m_logPath.empty()) return "";
    std::ifstream f(m_logPath, std::ios::binary | std::ios::ate);
    if (!f) return "";
    std::streamoff size = f.tellg();
    std::streamoff read = (std::streamoff)std::min<size_t>((size_t)size, maxBytes);
    f.seekg(size - read);
    std::string out((size_t)read, '\0');
    f.read(&out[0], read);
    return out;
}

std::vector<java::JavaRuntime> App::detectJava() {
    return java::DetectAll();
}

} // namespace launcher
