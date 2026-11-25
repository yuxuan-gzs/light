// GuiDllStore.h
// 插件商店 + GUI 附属插件宿主
// ============================================================================
// 商店：仿应用市场卡片网格布局，支持搜索、加载循环条（旋转指示器）、
//       一键下载安装、自定义仓库链接 / 本地链接。
// 附属插件：存放在 Cache\gui\dll\ 下的 .dll，仅作为 GUI 扩展（JS API / Hook）
//           加载（YXG_RegisterPlugin ABI），**不允许添加任何指令**。
//           未启用时以 .dll.N 后缀保存。
// 伴生文件（同名）：.txt=插件介绍，.png/.jpg=插件 logo，.mp4=介绍视频。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <mutex>

#include "YXExtension.h"
#include "ExtensionManager.h"

// 商店条目
struct GuiDllStoreItem {
    std::wstring name;         // 插件显示名 = DLL 文件名（不含扩展名）
    std::wstring description;  // 来自同名 .txt
    std::wstring dllUrl;       // .dll 下载地址（远程）或本地路径
    std::wstring txtUrl;       // .txt 地址/路径
    std::wstring logoUrl;      // .png/.jpg 地址/路径
    std::wstring videoUrl;     // .mp4 地址/路径
    std::wstring logoPath;     // 本地已缓存 logo 路径（用于绘制）
    std::wstring videoPath;    // 本地已缓存视频路径
    bool installed = false;
    bool enabled = false;
    bool installing = false;
};

// 仓库来源
struct GuiDllRepo {
    std::wstring display;
    int type = 0;              // 0=github, 1=本地目录, 2=http 基址(list.txt)
    std::wstring owner, repo, branch;  // github 专用
    std::wstring path;         // 本地目录 / http 基址
};

// ============================================================================
// 全局附属插件宿主（单例）
// ============================================================================
class GuiDllPluginHost {
public:
    static GuiDllPluginHost* Instance();

    std::wstring DllDir();     // Cache\gui\dll 绝对路径（自动创建）

    void LoadAll();            // 扫描并加载 *.dll（跳过 .dll.N）
    void UnloadAll();
    void Reload();

    bool IsInstalled(const std::wstring& name) const;
    bool IsEnabled(const std::wstring& name) const;
    bool SetEnabled(const std::wstring& name, bool enabled);  // 重命名 .dll <-> .dll.N
    std::vector<std::wstring> InstalledNames() const;         // 已安装的全部插件（含已停用）
    std::wstring DllPath(const std::wstring& name) const;     // .dll 或 .dll.N 完整路径
    std::wstring Description(const std::wstring& name) const; // 读取同名 .txt 介绍

    // 供 JS 插件执行路径查询（转发到内部 ExtensionManager）
    int  RunHooks(YXG_HookType type, const wchar_t* pluginId, void* in, void* out);
    bool HasJSApi(const std::string& ns, const std::string& name) const;
    bool CallJSApi(const std::string& ns, const std::string& name,
                   int argc, YXG_JSValue* argv, YXG_JSValue* ret);
    std::vector<std::string> GetJSApiNames(const std::string& ns) const;
    std::vector<std::string> GetNamespaces() const;
    // 枚举所有 DLL 注册的原生面板（供侧边栏渲染新 Tab）
    std::vector<NativePanelRecord> GetNativePanels() const;

private:
    GuiDllPluginHost();
    ~GuiDllPluginHost();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    mutable std::mutex m_mu;
};

extern GuiDllPluginHost* g_guiDllHost;

// ============================================================================
// 商店面板（自绘窗口，内嵌于设置对话框）
// ============================================================================
class GuiDllStorePanel {
public:
    GuiDllStorePanel();
    ~GuiDllStorePanel();

    bool Create(HWND parent, HINSTANCE instance, int controlId);
    HWND Hwnd() const { return m_hWnd; }
    void Destroy();
    void Move(int x, int y, int w, int h);
    void Show(bool show);
    void Refresh();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static void EnsureClass(HINSTANCE instance);

    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
    void Paint(HDC hdc);
    void DrawCard(void* g, const GuiDllStoreItem& item, int x, int y, int w, int h);
    void DrawSpinner(void* g, int x, int y, int r);
    void DrawPill(void* g, const wchar_t* text, int x, int y, int w, int h,
                  int kind);   // kind 0=安装 1=已安装(启用) 2=已停用 3=加载中
    int  CardAt(int x, int y, int& button) const;  // button 0=主按钮 1=播放视频
    void HandleClick(int x, int y);
    void UpdateHover(int x, int y);
    void AddRepoDialog();
    void RebuildFilter();
    void StartLoad();
    void OnLoadFinished(std::vector<GuiDllStoreItem>* items, std::wstring* status);
    void LoadRepoInBackground(const GuiDllRepo& repo);
    void InstallItem(const GuiDllStoreItem& item);
    void DoInstallInBackground(GuiDllStoreItem item);
    void DoToggleEnabled(const GuiDllStoreItem& item);
    void UpdateInstalledState();
    void PlayVideo(const GuiDllStoreItem& item);
    void ShowDetails(const GuiDllStoreItem& item);

    HWND m_hWnd = nullptr;
    HWND m_hParent = nullptr;
    HINSTANCE m_hInstance = nullptr;
    int m_controlId = 0;

    HWND m_hSearch = nullptr;
    HWND m_hAddRepo = nullptr;
    HWND m_hRefresh = nullptr;

    std::vector<GuiDllStoreItem> m_items;
    std::vector<int> m_filtered;
    std::wstring m_search;
    bool m_loading = false;
    std::wstring m_status;

    int m_scrollY = 0;
    int m_hoverCard = -1;
    int m_hoverButton = -1;
    float m_spin = 0.0f;
    bool m_spinActive = false;
    int m_cardCols = 3;

    std::vector<GuiDllRepo> m_repos;
    int m_curRepo = 0;
    std::mutex m_loadMutex;
    bool m_loadRunning = false;

    std::map<std::wstring, void*> m_logoCache;  // Gdiplus::Bitmap* 缓存
};
