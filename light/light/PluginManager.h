// PluginManager.h
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include "winwrap.h"
#include "PluginInterface.h"

// 插件命令信息结构（用于展示）- 必须在类定义之前
struct PluginCommandInfo {
    std::string pluginName;
    std::string commandName;
    std::string description;
    std::string usage;
};

// 插件中心展示用的完整记录（含来源、路径、启用状态）
struct PluginRecord {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string path;          // DLL / ZIP 路径（内置项为空）
    bool isApiPlugin = false;  // 程序目录下 api_*.dll 新增指令集
    bool isGuiPlugin = false;  // 提供 IGuiProvider
    bool isFromZip = false;
    bool enabled = true;
};

// api_*.dll 静默安全扫描结果
struct ApiPluginScanResult {
    bool safe = false;
    bool signedPlugin = false;
    std::string reason;
    std::vector<std::string> dependencies;       // PE 导入表中的 DLL 名
    std::vector<std::string> localDependencies;  // 在程序目录/插件目录找到的附属 DLL
};

// 插件加载回调
struct PluginLoadCallback {
    std::function<void(const std::string& pluginName, bool success, const std::string& errorMsg)> onLoad;
    std::function<void(const std::string& pluginName)> onUnload;
    std::function<void(int total, int loaded, int failed)> onBatchProgress;
};

class PluginManager {
public:
    static PluginManager* GetInstance();
    ~PluginManager();

    // 插件操作
    bool LoadPlugin(const std::string& pluginPath, bool silent = false);
    bool LoadPluginFromZip(const std::string& zipPath, bool silent = false);
    void UnloadPlugin(const std::string& pluginId, bool silent = false);
    void UnloadAllPlugins(bool silent = false);

    // 热重载
    void ReloadAllPlugins(bool silent = false);
    void ReloadPlugin(const std::string& pluginId, bool silent = false);

    // 批量加载
    int LoadPluginsFromDirectory(const std::string& dirPath, bool silent = false);
    int LoadPluginsFromDirectoryWithUI(const std::string& dirPath);

    // ============================================================
    // api_*.dll 新增指令集（程序所在目录，静默加载）
    // ============================================================
    // 扫描 dirPath 下所有 api_ 前缀 DLL，静默校验 PE / 依赖 / 危险导入，
    // 通过后加载为普通 IPlugin（命令集）。
    int LoadApiPluginsFromDirectory(const std::string& dirPath, bool silent = true);
    bool LoadApiPlugin(const std::string& dllPath, bool silent = true);

    // 静默安全扫描（可在加载前由界面调用）
    ApiPluginScanResult ScanApiPlugin(const std::string& dllPath) const;

    // 命令处理
    bool ExecuteCommand(const std::string& cmd, const std::vector<std::string>& args);
    std::vector<std::string> GetPluginCommands() const;
    std::vector<PluginCommandInfo> GetAllPluginCommands() const;
    std::string GetCommandHelp(const std::string& cmd) const;

    // 状态查询
    bool IsPluginLoaded(const std::string& pluginId) const;
    std::vector<PluginInfo> GetLoadedPlugins() const;
    // 插件中心：含来源/路径/GUI 标记
    std::vector<PluginRecord> GetPluginRecords() const;
    bool GetPluginPath(const std::string& pluginId, std::string& outPath) const;
    // 启用/停用（停用后不参与命令分发，但保持已加载状态）
    bool SetPluginEnabled(const std::string& pluginId, bool enabled);
    bool IsPluginEnabled(const std::string& pluginId) const;

    // 回调设置
    void SetLoadCallback(const PluginLoadCallback& callback);
    void ClearCallbacks();

    // ============================================================
    // ========== 新增：GUI 相关方法 ==========
    // ============================================================
public:
    // 设置 UI 管理器（供普通插件使用）
    void SetUIManager(PluginUIManager* ui);
    PluginUIManager* GetUIManager() const { return m_uiManager; }

    // GUI 插件条目
    struct GuiPluginEntry {
        IPlugin* plugin;
        IGuiProvider* provider;
        std::string pluginId;
    };

    // 查找第一个实现了 IGuiProvider 的插件
    GuiPluginEntry FindGuiPlugin() const;

    // 注册 GUI 提供者（供插件加载时调用）
    void RegisterGuiProvider(const std::string& pluginId, IGuiProvider* provider);

private:
    PluginManager() = default;
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    struct LoadedPlugin {
        HMODULE handle = nullptr;
        IPlugin* instance = nullptr;
        PluginInfo info;
        std::string dllPath;
        std::string extractPath;
        bool isFromZip = false;
        bool isApiPlugin = false;
        bool isGuiOnly = false;
        bool enabled = true;
        IGuiProvider* guiProvider = nullptr;
        void (*destroyGuiProvider)(IGuiProvider*) = nullptr;
    };

    bool ExtractZipToTemp(const std::string& zipPath, std::string& tempDir);
    bool LoadPluginDll(const std::string& dllPath, const std::string& pluginId,
        bool silent = false, bool apiPlugin = false);
    // 在同一互斥锁下判断 DLL 是否已经被加载
    bool IsDllAlreadyLoaded(const std::string& dllPath) const;
    std::string GetTempPluginDir(const std::string& pluginId);
    void CleanupTempDirs();

    mutable std::mutex m_mutex;
    std::map<std::string, LoadedPlugin> m_plugins;
    std::map<std::string, std::string> m_tempDirs;
    PluginLoadCallback m_callback;
    PluginUIManager* m_uiManager = nullptr;   // 新增字段

    static PluginManager* s_instance;
};