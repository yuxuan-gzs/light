// LauncherApp.h — 启动器状态机（直接 C++ 接口，UI 通过事件回调感知异步结果）
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "LauncherCore.h"
#include "MsAuth.h"
#include "JavaDetect.h"
#include "LoaderInstall.h"

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <map>

namespace launcher {

// 事件类型（回调给 UI）
enum class Ev {
    Log,            // 日志文本
    InstallProgress,// 安装进度（data 为 JSON）
    InstallDone,    // 安装完成
    InstallError,   // 安装失败（data 为错误信息）
    GameStart,      // 游戏启动（data 为 pid）
    GameExit,       // 游戏退出（data 为退出码）
    AccountChange,  // 账号变化
};

// 单个版本的独立设置（HMCL 的「版本设置」）
struct VersionSettings {
    std::string javaPath;              // 覆盖全局 Java（空=用全局）
    int maxMemoryMB = -1;              // 覆盖全局内存（-1=用全局）
    int width = 854;                   // 窗口宽
    int height = 480;                  // 窗口高
    bool fullscreen = false;           // 全屏
    std::vector<std::string> jvmArgs;  // 额外 JVM 参数
    std::vector<std::string> gameArgs; // 额外游戏参数
    std::string server;                // 直连服务器（空=不直连）
    int serverPort = 25565;
};

class App {
public:
    static App& Instance();

    // ---- 配置 ----
    std::string gameDir() const { return m_gameDir; }
    void setGameDir(const std::string& d);
    mc::DownloadSource source() const { return m_source; }
    void setSource(const std::string& name);
    std::string javaPath() const { return m_javaPath; }
    void setJavaPath(const std::string& p) { m_javaPath = p; }
    int maxMemory() const { return m_maxMemory; }
    void setMaxMemory(int m) { m_maxMemory = m; }
    bool versionIsolation() const { return m_versionIsolation; }
    void setVersionIsolation(bool v) { m_versionIsolation = v; }
    void loadConfig();
    void saveConfig();

    // ---- 版本清单 ----
    bool manifestLoaded() const { return m_manifestLoaded; }
    mc::VersionManifest manifest() const;
    bool refreshRemote(std::string& err);   // 同步拉取（应在后台线程调用）

    // ---- 安装 ----
    bool installing() const { return m_installing.load(); }
    mc::InstallProgress installProgress();
    void startInstall(const std::string& id);
    void abortInstall() { m_abort.store(true); }
    // 下载原版 + 自动安装加载器（loaderType: 0无 1Fabric 2Quilt 3Forge 4NeoForge）
    // loaderVersion 为空则自动选最新稳定版
    void startInstallWithLoader(const std::string& id, int loaderType,
                                const std::string& loaderVersion = "");

    // ---- 加载器安装（Fabric / Quilt / Forge）----
    void startInstallLoader(const std::string& mcVersion,
                            const std::string& loaderVersion, bool quilt);
    void startInstallForge(const std::string& fullVersion);
    void startInstallNeoForge(const std::string& fullVersion);

    // ---- 账号 ----
    msauth::Account account() const;
    std::string accountType() const { return m_accountType; }
    void authOffline(const std::string& name);
    bool authMsStart(msauth::DeviceCode& dc, std::string& err);
    int  authMsPoll(msauth::Account& acc, std::string& err); // 1 成功 0 等待 -1 失败

    // ---- 启动 ----
    bool launch(const std::string& versionId, int width, int height, int mem,
                std::string& err);
    bool gameRunning() const;
    void stopGame();
    int  gameExitCode() const { return m_gameExit.load(); }
    std::string gameLogTail(size_t maxBytes = 8192) const;

    // ---- Java ----
    std::vector<java::JavaRuntime> detectJava();

    // ---- 服务器列表 / 直连 ----
    std::vector<std::pair<std::string,int>> servers() const;
    void addServer(const std::string& address, int port);
    void removeServer(size_t index);
    void setDirectServer(const std::string& address, int port);
    std::string directServer() const;
    int directPort() const;

    // ---- 版本设置（每个版本独立的 JVM/游戏参数/分辨率等）----
    VersionSettings versionSettings(const std::string& versionId) const;
    void setVersionSettings(const std::string& versionId, const VersionSettings& s);

    // ---- 事件回调（可能从后台线程调用，UI 需自行转发到 UI 线程）----
    void setEventCallback(std::function<void(Ev, const std::string&)> cb) { m_cb = std::move(cb); }
    void emit(Ev type, const std::string& data = "");

private:
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void installWorker(const std::string& versionId);
    void installWithLoaderWorker(const std::string& versionId, int loaderType,
                                 const std::string& loaderVersion);
    void installLoaderWorker(const std::string& mcVersion,
                             const std::string& loaderVersion, bool quilt);
    void installForgeWorker(const std::string& fullVersion);
    void installNeoForgeWorker(const std::string& fullVersion);
    void waitWorker();

    std::string m_gameDir;
    mc::DownloadSource m_source = mc::DownloadSource::Bmclapi;
    std::string m_javaPath;
    int m_maxMemory = 2048;
    bool m_versionIsolation = false;

    mutable std::mutex m_serverMu;
    std::vector<std::pair<std::string,int>> m_servers;
    std::string m_directServer;
    int m_directPort = 25565;

    mutable std::mutex m_verSetMu;
    std::map<std::string, VersionSettings> m_versionSettings;

    mutable std::mutex m_manifestMu;
    mc::VersionManifest m_manifest;
    bool m_manifestLoaded = false;

    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_installing{false};
    std::mutex m_installMu;
    mc::InstallProgress m_installProgress;
    std::thread m_installThread;

    mutable std::mutex m_accountMu;
    std::string m_accountType;
    msauth::Account m_account;
    msauth::DeviceCode m_deviceCode;   // 微软登录设备码（UI 从 authMsStart 获取后也在此保存）

    mutable std::mutex m_gameMu;
    HANDLE m_hProcess = nullptr;
    DWORD m_pid = 0;
    std::atomic<int> m_gameExit{-1};
    std::string m_logPath;
    std::thread m_waitThread;

    std::function<void(Ev, const std::string&)> m_cb;
};

} // namespace launcher
