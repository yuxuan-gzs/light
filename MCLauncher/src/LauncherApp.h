// LauncherApp.h — 启动器状态机（直接 C++ 接口，UI 通过事件回调感知异步结果）
// 已改为「实例（实例 = 文件夹 + 版本）」模型：不再有全局游戏目录/全局 Java 路径，
// 每个实例的目录、Java、内存、分辨率等由 mlc::Store 提供。
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
#include "MlcData.h"

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace launcher {

// 事件类型（回调给 UI）
enum class Ev {
    Log,              // 日志文本
    InstallProgress,  // 安装进度（data 为 JSON）
    InstallDone,      // 任务完成（data 为标识）
    InstallError,     // 失败（data 为错误信息）
    Info,             // 普通提示（不属于错误）
    IntegrityDone,    // 完整性检查完成（data 为 JSON）
    GameLaunching,    // 进程已创建，正在等游戏窗口出现（data 为 pid）
    GameStart,        // 游戏窗口已出现（data 为 pid）
    GameExit,         // 游戏退出（data 为退出码）
    AccountChange,    // 账号变化
};

class App {
public:
    static App& Instance();

    // ---- 配置（下载源等，其余配置在 mlc::Store）----
    mc::DownloadSource source() const { return m_source; }
    void setSource(const std::string& name);
    void loadConfig();

    // ---- 版本清单 ----
    bool manifestLoaded() const { return m_manifestLoaded; }
    mc::VersionManifest manifest() const;
    bool refreshRemote(std::string& err);   // 同步拉取（应在后台线程调用）

    // ---- 安装：把某个 MC 版本装进指定实例目录（folder = .minecraft 根）----
    bool installing() const { return m_installing.load(); }
    mc::InstallProgress installProgress();
    void startInstall(const std::string& folder, const std::string& versionId,
                      int loaderType, const std::string& loaderVersion,
                      const std::string& displayName);
    void abortInstall() { m_abort.store(true); }

    // ---- 加载器（针对已安装版本）----
    void startInstallLoader(const std::string& folder, const std::string& mcVersion,
                            const std::string& loaderVersion, bool quilt);
    void startInstallForge(const std::string& folder, const std::string& fullVersion);
    void startInstallNeoForge(const std::string& folder, const std::string& fullVersion);
    // 卸载加载器版本（删除 versions\<loaderId> 目录）
    static bool removeLoaderVersion(const std::string& folder, const std::string& versionId,
                                    std::string& err);

    // ---- 完整性 ----
    void startIntegrityCheck(const std::string& instanceId, bool repair);
    // 同步检查（返回报告；err 为过程错误）
    static mc::IntegrityReport checkInstance(const mlc::Instance& inst, std::string& err);

    // ---- 账号（多账号：保存 / 选择 / 续期）----
    void loadAccountFromStore();
    bool selectAccount(const std::string& key, std::string& err);
    bool refreshAccount(std::string& err);      // 用 refresh_token 续期当前账号
    std::string accountKey() const;
    msauth::Account account() const;
    std::string accountType() const;
    void authOffline(const std::string& name);
    bool authMsStart(msauth::DeviceCode& dc, std::string& err);
    int  authMsPoll(msauth::Account& acc, std::string& err); // 1 成功 0 等待 -1 失败
    // 一步完成设备码登录（后台线程调用）：onCode 用来把「用户码 + 授权网址」推给 UI，
    // 之后阻塞轮询直到成功/超时。成功后已保存账号并设为当前账号。
    bool authMsAutoLogin(const std::function<void(const msauth::DeviceCode&)>& onCode,
                         std::string& err);

    // ---- 启动（含完整性检查 / 自动修复 / 存档备份）----
    bool launchInstance(const std::string& instanceId, std::string& err);
    bool gameRunning() const;
    void stopGame();
    int  gameExitCode() const { return m_gameExit.load(); }
    std::string gameLogTail(size_t maxBytes = 8192) const;

    // ---- Java ----
    std::vector<java::JavaRuntime> detectJava();
    // satisfied 非空时返回「找到的 Java 是否满足 neededMajor」
    static std::string resolveJava(const mlc::Instance& inst, int neededMajor, std::string& err,
                                   bool* satisfied = nullptr);

    // ---- 服务器（存在全局设置里）----
    std::vector<std::pair<std::string, int>> servers() const;
    void addServer(const std::string& address, int port);
    void removeServer(size_t index);
    void setDirectServer(const std::string& address, int port);

    // ---- 事件回调（可能从后台线程调用，UI 需自行转发到 UI 线程）----
    void setEventCallback(std::function<void(Ev, const std::string&)> cb) { m_cb = std::move(cb); }
    void emit(Ev type, const std::string& data = "");

private:
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool ensureManifest(std::string& err);
    void installWorker(std::string folder, std::string versionId, int loaderType,
                       std::string loaderVersion, std::string displayName);
    void integrityWorker(std::string instanceId, bool repair);
    void waitWorker();
    void startThread(std::thread& t, std::function<void()> fn);

    mc::DownloadSource m_source = mc::DownloadSource::Bmclapi;

    mutable std::mutex m_manifestMu;
    mc::VersionManifest m_manifest;
    bool m_manifestLoaded = false;

    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_installing{false};
    std::mutex m_installMu;
    mc::InstallProgress m_installProgress;
    std::thread m_installThread;

    mutable std::mutex m_accountMu;
    std::string m_accountType = "offline";
    msauth::Account m_account;
    msauth::DeviceCode m_deviceCode;

    mutable std::mutex m_gameMu;
    HANDLE m_hProcess = nullptr;
    DWORD m_pid = 0;
    std::atomic<int> m_gameExit{-1};
    std::string m_logPath;
    std::thread m_waitThread;

    std::function<void(Ev, const std::string&)> m_cb;
};

} // namespace launcher
