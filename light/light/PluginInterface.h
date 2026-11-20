// PluginInterface.h
#pragma once
#include <string>
#include <vector>
#include <functional>
#include "winwrap.h"

// ============================================================
// 原有插件信息
// ============================================================
struct PluginInfo {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string id;
};

struct PluginCommand {
    std::string name;
    std::string description;
    std::string usage;
    std::function<void(const std::vector<std::string>& args)> callback;
};

// ============================================================
// 插件基类
// ============================================================
class PluginUIManager;

class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual PluginInfo GetInfo() = 0;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual std::vector<PluginCommand> GetCommands() { return {}; }
    virtual void OnLoad() {}
    virtual void OnUnload() {}
    virtual void OnCommand(const std::string& cmd, const std::vector<std::string>& args) {}

    // UI 集成（可选）
    virtual void SetUIManager(PluginUIManager* ui) { m_ui = ui; }

protected:
    PluginUIManager* m_ui = nullptr;
};

// ============================================================
// 新增：GUI 提供者接口
// 插件实现此接口，即可让主程序切换到 GUI 模式
// ============================================================
class IGuiProvider {
public:
    virtual ~IGuiProvider() = default;

    virtual void UpdatePrompt(const std::string& path) {}

    // 布局类型（用于主程序判断如何初始化窗口）
    enum class LayoutType {
        Custom,       // 完全由插件自己创建窗口（主程序只提供消息循环）
        Tabbed,       // 主程序提供 Tab 框架，插件往里面塞面板
        Simple        // 主程序提供 输出区+输入区，插件只提供标题和图标
    };

    // 返回布局类型
    virtual LayoutType GetLayoutType() = 0;

    // 返回主窗口标题
    virtual std::string GetWindowTitle() = 0;

    // 返回窗口初始大小
    virtual void GetWindowSize(int& width, int& height) {
        width = 900;
        height = 600;
    }

    // ========================================================
    // 方式一：Custom 布局
    // 插件自己创建主窗口，返回 HWND
    // 主程序会使用此 HWND 作为主窗口，并跑消息循环
    // ========================================================
    virtual HWND CreateCustomWindow(HINSTANCE hInstance) { return nullptr; }

    // ========================================================
    // 方式二：Tabbed 布局
    // 主程序创建 Tab 框架，插件提供一个或多个面板
    // ========================================================
    struct TabPanel {
        std::string title;
        HWND hPanel;   // 插件创建的子面板
    };
    virtual std::vector<TabPanel> CreateTabPanels(HWND parent) { return {}; }

    // ========================================================
    // 方式三：Simple 布局
    // 主程序提供输出面板和输入框，插件只提供标题
    // 主程序会把输出重定向回调注册给插件
    // ========================================================
    virtual bool UseOutputRedirect() { return true; }

    // ========================================================
    // 通用：命令执行回调
    // 主程序会把这个回调设置给插件，插件调用它来执行命令
    // ========================================================
    using CommandHandler = std::function<void(const std::string& cmd)>;
    virtual void SetCommandHandler(CommandHandler handler) { m_commandHandler = handler; }

    // 通用：输出回调（插件用来往输出面板写内容）
    using OutputHandler = std::function<void(const std::string& text)>;
    virtual void SetOutputHandler(OutputHandler handler) { m_outputHandler = handler; }

    // 通用：窗口关闭时调用
    virtual void OnWindowClose() {}

    // ========================================================
    // 通用：追加输出（主程序调用）
    // 主程序会把 std::cout / std::cerr 重定向后的内容通过此函数
    // 发给插件，插件应把它追加到自己的控制台显示。
    // 注意：可能从主程序的工作线程调用，实现方必须保证线程安全
    // （例如 PostMessage 到自己的 UI 线程）。
    // ========================================================
    virtual void AppendOutput(const std::string& text) { (void)text; }

    // 通用：下载完成回调（主程序在异步下载结束后调用）
    // path: 下载到的本地路径（UTF-8），success: 是否成功
    virtual void OnDownloadCompleted(const std::string& path, bool success) { (void)path; (void)success; }

protected:
    CommandHandler m_commandHandler;
    OutputHandler m_outputHandler;
};

// ============================================================
// UI 管理器（给普通插件用）
// ============================================================
class PluginUIManager {
public:
    std::function<void(const std::string&)> WriteOutput;
    std::function<void(const std::string&)> WriteLine;
    std::function<void()> ClearOutput;
    std::function<HWND()> GetMainWindow;
    std::function<void(const std::string&, const std::string&)> ShowMessage;
    std::function<void(std::function<void()>)> RunOnUIThread;
};

// ============================================================
// 导出函数
// ============================================================
#define PLUGIN_API extern "C" __declspec(dllexport)

PLUGIN_API IPlugin* CreatePlugin();
PLUGIN_API void DestroyPlugin(IPlugin* plugin);

// 如果插件提供 GUI，还需导出这个
PLUGIN_API IGuiProvider* CreateGuiProvider();
PLUGIN_API void DestroyGuiProvider(IGuiProvider* provider);

// 全局 GUI 提供者（由主程序定义），插件/其它模块可引用
extern IGuiProvider* g_guiProvider;

