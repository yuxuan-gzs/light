// Ui.h — 自绘主窗口（HMCL 风格 + PCL2 磨砂质感）
#pragma once

#include "Theme.h"
#include "ModManager.h"
#include <string>
#include <vector>
#include <mutex>

namespace ui {

struct Widget {
    enum Type { Btn, Label, Edit, Combo, List, Progress, Slider, Check, Panel };
    Type type = Label;
    std::wstring id;
    std::wstring text;
    RECT rc{};               // 相对客户区
    bool hover = false;
    bool pressed = false;
    COLORREF accent = 0;     // 按钮强调色（0=默认）
    // Edit
    std::wstring editText;
    bool focused = false;
    int cursor = 0;
    // Combo
    std::vector<std::wstring> items;
    int sel = -1;
    bool dropped = false;
    // List
    std::vector<std::wstring> listItems;
    std::vector<std::wstring> subItems;   // 副文本（灰色说明）
    int scroll = 0;
    int hot = -1;
    int selIdx = -1;
    // Progress
    float progress = 0.0f;
    // Slider
    int value = 0, vmin = 0, vmax = 100;
    // Check
    bool checked = false;
};

class Ui {
public:
    enum Page { Home, Versions, Download, Account, Mods, Saves, Settings, VersionSettings };

    Ui() = default;
    ~Ui();

    bool Create(HINSTANCE hInstance, HWND parent);
    HWND hwnd() const { return m_hwnd; }

    void SetPage(Page p);
    Page page() const { return m_page; }

    // 重建当前页面的控件布局（窗口大小变化 / 页面切换时调用）
    void Rebuild();

    // 绘制（双缓冲）
    void Paint(HDC hdc, const RECT& client);

    // 输入事件
    void OnMouseMove(int x, int y);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnChar(wchar_t c);
    void OnKeyDown(WPARAM vk);
    void OnMouseWheel(int delta, int x, int y);
    void OnTimerTick();

    // 异步事件（由 launcher 回调线程触发，转发到 UI 线程）
    void OnAsyncEvent(int type, const std::string& data);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    void BuildHome();
    void BuildVersions();
    void BuildDownload();
    void BuildAccount();
    void BuildMods();
    void BuildSaves();
    void BuildSettings();
    void BuildVersionSettings();

    void PaintHome(Gdiplus::Graphics& g);
    void PaintVersions(Gdiplus::Graphics& g);
    void PaintDownload(Gdiplus::Graphics& g);
    void PaintAccount(Gdiplus::Graphics& g);
    void PaintMods(Gdiplus::Graphics& g);
    void PaintSaves(Gdiplus::Graphics& g);
    void PaintSettings(Gdiplus::Graphics& g);
    void PaintVersionSettings(Gdiplus::Graphics& g);

    void DrawWidget(Gdiplus::Graphics& g, const Widget& w);
    void DrawDropdown(Gdiplus::Graphics& g, const Widget& w);
    void DrawSidebar(Gdiplus::Graphics& g);

    Widget* Find(const std::wstring& id);
    void HandleButton(const std::wstring& id);
    void RefreshVersionsList();
    void RefreshModsList();
    void TriggerLoaderVersionFetch();

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    int m_width = 1100, m_height = 720;
    Page m_page = Home;

    std::vector<Widget> m_widgets;

    // 异步事件队列（线程安全）
    std::mutex m_evMu;
    std::vector<std::pair<int, std::string>> m_evQueue;

    // 安装进度显示
    std::wstring m_progressText;
    float m_progress = 0.0f;
    bool m_installing = false;

    // 微软登录设备码
    std::wstring m_msUserCode;
    std::wstring m_msVerifyUri;
    bool m_msPending = false;

    // mod 检测结果
    std::vector<std::string> m_missingMods;
    std::wstring m_modStatus;

    // 缓存（避免每次 Rebuild 都扫描磁盘/解析 jar）
    std::vector<modmgr::ModInfo> m_modCache;
    bool m_modCacheValid = false;

    // Modrinth 搜索结果（Mod 搜索下载用）
    std::vector<modmgr::SearchResult> m_searchResults;

    // 版本页：加载器版本列表 + manifest 自动加载标记 + 选中状态（Rebuild 后保留）
    std::vector<std::wstring> m_loaderVersions;
    bool m_manifestRequested = false;
    std::wstring m_selectedVersion;
    int m_selectedLoaderType = 0;
    std::wstring m_versionFilter;

    // 版本设置页：正在编辑的版本 id
    std::wstring m_editVersionId;

    // 当前游戏版本（启动目标，侧边栏「当前版本」大块显示）
    std::wstring m_currentGameVersion;

    // 下载页分类（0 游戏 1 模组 2 资源包 3 光影 4 世界）+ 内容搜索结果
    int m_downloadCategory = 0;
    std::vector<modmgr::SearchResult> m_dlResults;

    // 下载进度弹窗（HMCL 式：下载时弹出，完成后可关闭）
    bool m_showDlDialog = false;
    std::wstring m_dlDialogTitle;
    void DrawDownloadDialog(Gdiplus::Graphics& g);
};

} // namespace ui
