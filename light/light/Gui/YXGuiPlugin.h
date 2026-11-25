// YXGuiPlugin.h
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <thread>
#include <set>
#include <atomic>
#include <mutex>
#include <memory>
#include "winwrap.h"
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <deque>

#include "../PluginInterface.h" // 明确使用 light 根目录共享的插件 ABI，避免同名转发头解析歧义
#include "JSPlugin.h"
#include "PermissionManager.h"
#include "JSWebViewBridge.h"
#include "PluginCenter.h"
#include "GuiDllStore.h"

// WebView2 前置声明
struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;
struct ICoreWebView2BrowserExtension;

// ==================== 主窗口控件 ID ====================
#define IDC_HELP_EDIT        1001
#define IDC_CONSOLE_EDIT     1002
#define IDC_CONSOLE_INPUT    1003
#define IDC_BROWSER_ADDRESS  1004
#define IDC_BROWSER_RESTORE  1005
#define IDC_PROMPT_STATIC    1012

#define IDC_NAV_BACK         1006
#define IDC_NAV_FORWARD      1007
#define IDC_NAV_REFRESH      1008
#define IDC_NAV_HOME         1009
#define IDC_NAV_STAR         1010
#define IDC_NAV_FAV          1011
#define IDC_NAV_ZOOM_IN      1020
#define IDC_NAV_ZOOM_OUT     1021
#define IDC_NAV_DOWNLOAD     1023
#define IDC_NAV_EXTENSION    1024
#define IDC_NAV_SETTINGS     1025

#define IDC_TOPBAR_SETTINGS  1032

#define IDC_AI_BASE          2000

// ==================== 扩展管理窗口 ====================
#define IDC_EXT_INSTALL_FROM_STORE  3009
#define IDC_EXT_LIST         3000
#define IDC_EXT_ENABLE       3001
#define IDC_EXT_DISABLE      3002
#define IDC_EXT_REMOVE       3003
#define IDC_EXT_OPEN_DIR     3004
#define IDC_EXT_ADD          3005
#define IDC_EXT_OPEN_POPUP   3006
#define IDC_EXT_OPEN_OPTIONS 3007
#define IDC_EXT_DETAIL       3008
#define IDC_EXT_INSTALL_FROM_LINK   3010

// ==================== 设置对话框 ====================
#define IDC_SET_TAB          4000
#define IDC_SET_AI_LIST      4001
#define IDC_SET_AI_ADD       4002
#define IDC_SET_AI_EDIT      4003
#define IDC_SET_AI_DEL       4004
#define IDC_SET_AI_UP        4005
#define IDC_SET_AI_DOWN      4006
#define IDC_SET_TB_LIST      4010
#define IDC_SET_TB_UP        4011
#define IDC_SET_TB_DOWN      4012
#define IDC_SET_TB_TOGGLE    4013
#define IDC_SET_WIN_MAIN_W   4020
#define IDC_SET_WIN_MAIN_H   4021
#define IDC_SET_WIN_BROWSER_H 4022
#define IDC_SET_WIN_EXT_W    4023
#define IDC_SET_WIN_EXT_H    4024
#define IDC_SET_WIN_POPUP_W  4025
#define IDC_SET_WIN_POPUP_H  4026
#define IDC_SET_WIN_OPT_W    4027
#define IDC_SET_WIN_OPT_H    4028
#define IDC_SET_OK           4030
#define IDC_SET_CANCEL       4031
#define IDC_SET_APPLY        4032
#define IDC_SET_RESET        4033
#define IDC_SET_PLUGIN_TEMPLATE 4034
#define IDC_SET_TEMPLATE_NAME   4035
#define IDC_SET_TEMPLATE_ID     4036
#define IDC_SET_TEMPLATE_AUTHOR 4037
#define IDC_SET_PLUGIN_CENTER   4040
#define IDC_SET_GUI_DLL_STORE   4041

#define IDC_AI_EDIT_NAME     4100
#define IDC_AI_EDIT_URL      4101
#define IDC_AI_EDIT_OK       4102
#define IDC_AI_EDIT_CANCEL   4103

#define IDC_SIDEBAR          5000

// ==================== 消息 ====================
#define WM_YXGUI_APPEND_OUTPUT      (WM_USER + 600)
#define WM_YXGUI_UPDATE_NAV         (WM_USER + 601)
#define WM_YXGUI_UPDATE_PROMPT      (WM_USER + 602)
#define WM_YXGUI_CRX_DOWNLOADED     (WM_USER + 700)
#define WM_YXGUI_CRX_INSTALL_DONE   (WM_USER + 703)
#define WM_YXGUI_NEW_TAB            (WM_USER + 710)
#define WM_YXGUI_CONFIG_CHANGED     (WM_USER + 711)
#define WM_YXGUI_ADD_EXTENSION_TO_PROFILE   (WM_USER + 712)
#define WM_YXGUI_SWITCH_VIEW        (WM_USER + 720)
#define WM_YXGUI_REFRESH_SIDEBAR    (WM_USER + 721)

// ==================== 布局常量 ====================
static const int LEFT_WIDTH = 260;
static const int RIGHT_WIDTH = 120;
static const int SPLITTER_H = 6;
static const int BROWSER_MIN_H = 150;
static const int BROWSER_MAX_H = 900;
static const int TOOLBAR_H = 32;
static const int TAB_BAR_H = 32;
static const int TOPBAR_H = 26;
static const int AI_BTN_SIZE = 80;
static const int AI_BTN_GAP = 12;
static const int AI_SCROLLBAR_W = 8;
static const int TAB_MIN_W = 80;
static const int TAB_MAX_W = 240;
static const int TAB_DEFAULT_W = 180;
static const int SIDEBAR_WIDTH = 88;

// ==================== 配置结构 ====================
struct AiButtonConfig {
    std::wstring name;
    std::wstring url;
};

struct ToolbarButtonConfig {
    std::wstring id;
    bool visible = true;
};

struct GuiConfig {
    std::vector<AiButtonConfig> aiButtons;
    std::vector<ToolbarButtonConfig> toolbarOrder;
    int mainW = 1100, mainH = 750;
    int browserH = 320;
    int extMgrW = 900, extMgrH = 620;
    int popupW = 380, popupH = 500;
    int optionsW = 800, optionsH = 600;
    int tabWidth = TAB_DEFAULT_W;
    bool aiEnabled = true;                              // 内置「光语 AI」开关
    std::set<std::wstring> disabledPlugins;             // 被停用的 JS 扩展 id
};

// ==================== 标签结构 ====================
struct BrowserTab {
    std::wstring url;
    std::wstring title;
    HWND hostWnd = nullptr;
    ICoreWebView2Controller* controller = nullptr;
    ICoreWebView2* webView = nullptr;
};

// ==================== 扩展信息 ====================
struct ExtInfo {
    std::wstring id;
    std::wstring name;
    std::wstring version;
    std::wstring description;
    std::wstring path;
    std::wstring popupPath;
    std::wstring optionsPath;
    std::vector<std::wstring> permissions;
    bool enabled = true;
    ICoreWebView2BrowserExtension* comExt = nullptr;
};

class YXGuiPlugin : public IPlugin, public IGuiProvider {
public:
    void ShowOverlayWebView(JSPlugin* plugin, const std::wstring& url);
    void HideOverlayWebView(JSPlugin* plugin);

    using CommandHandler = std::function<void(const std::string&)>;
    using OutputHandler = std::function<void(const std::string&)>;

    YXGuiPlugin();
    ~YXGuiPlugin() override;

    // IPlugin
    PluginInfo GetInfo() override;
    bool Initialize() override;
    void Shutdown() override;
    std::vector<PluginCommand> GetCommands() override;
    void OnLoad() override;
    void OnUnload() override;
    void SetUIManager(PluginUIManager* ui) override;

    // IGuiProvider
    LayoutType GetLayoutType() override { return LayoutType::Custom; }
    std::string GetWindowTitle() override;
    void GetWindowSize(int& w, int& h) override;
    HWND CreateCustomWindow(HINSTANCE hInstance) override;
    void SetCommandHandler(CommandHandler handler) override;
    void SetOutputHandler(OutputHandler handler) override;
    void OnWindowClose() override;
    void AppendOutput(const std::string& text) override;
    void UpdatePrompt(const std::string& path) override;
    void OnDownloadCompleted(const std::string& path, bool success) override;

    void SubmitConsoleCommand();
    void NavigateToAddressBarText();
    void NavigateTo(const std::wstring& url);

private:
    // ===== v9.3: WebView2 覆盖层（登录用） =====
    HWND m_hOverlayWnd = nullptr;
    HWND m_hOverlayHost = nullptr;
    ICoreWebView2Controller* m_ovController = nullptr;
    ICoreWebView2* m_ovWebView = nullptr;
    ICoreWebView2Environment* m_ovEnv = nullptr;
    JSPlugin* m_ovPlugin = nullptr;

    static LRESULT CALLBACK OverlayWndProc(HWND, UINT, WPARAM, LPARAM);

    // ==================== 配置 ====================
    void LoadConfig();
    void SaveConfig();
    std::string GetConfigFilePath();
    void ResetConfigToDefault();
    void ApplyConfig();

    // ==================== 标签管理 ====================
    ICoreWebView2* GetActiveWebView();
    ICoreWebView2Controller* GetActiveController();
    void AddNewTab(const std::wstring& url = L"");
    void CloseTab(int idx);
    void SwitchToTab(int idx);
    void UpdateTabBar();
    void ResizeAllTabs();
    void SetupTabEvents(int tabIndex);
    RECT GetTabContainerRect();
    int  HitTestTabBar(int x, int y, bool& onClose);
    static LRESULT CALLBACK TabBarWndProc(HWND, UINT, WPARAM, LPARAM);

// ==================== 主窗口 ====================
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
    void OnSize(int width, int height);
    void CleanupGdiResources();
    void RebuildAiButtons();

    // ==================== 插件宿主容器 ====================
    // 拦截 JS 插件子控件（BUTTON/EDIT/LISTBOX...）的 WM_COMMAND 通知，
    // 反查归属 JSPlugin 并转发给 JS 全局 onControlEvent(ctrlId, code)
    static LRESULT CALLBACK PluginHostWndProc(HWND, UINT, WPARAM, LPARAM);

    void StartCrxWatcher();
    void StopCrxWatcher();
    std::wstring InstallExtensionFromCrx(const std::wstring& crxPath);
    void HandleDownloadedCrx(const std::wstring& crxPath);

    void AddExtensionToProfileOnUIThread(const std::wstring& extensionPath);

    void AppendConsoleText(const std::string& text);
    void InitWebView();
    void UpdateNavButtons();

    // ==================== 收藏夹 ====================
    void LoadFavorites();
    void SaveFavorites();
    void ToggleFavorite();
    void ShowFavoritesMenu();

    // ==================== 功能 ====================
    void ShowDownloadList();
    void ShowExtensions();
    void ShowSettings();
    void OpenExtensionStore();

    // ==================== 扩展系统 ====================
    void ScanExtensions();
    void ParseExtensionManifest(ExtInfo& ext);
    void LoadAllExtensions();
    void ShowExtensionManager();
    void OpenExtensionPopup(const std::wstring& extId, const std::wstring& popupPath);
    void OpenExtensionOptions(const std::wstring& extId, const std::wstring& optionsPath);

    static LRESULT CALLBACK ExtMgrWndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK PopupWndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK DetailWndProc(HWND, UINT, WPARAM, LPARAM);

    // ==================== 设置对话框 ====================
    void ShowSettingsDialog();
    static LRESULT CALLBACK SettingsWndProc(HWND, UINT, WPARAM, LPARAM);
    void RefreshSettingsAiList();
    void RefreshSettingsTbList();
    void RefreshSettingsWindowValues();
    void ApplySettingsWindowValues();
    void SettingsOnTabChange();
    void LayoutSettingsControls();
    void DrawSettingsBackground(HDC hdc, const RECT& rc);
    void DrawSettingsButton(LPDRAWITEMSTRUCT lpdis);
    void DrawSettingsNav(LPDRAWITEMSTRUCT lpdis);

    // ==================== 插件中心（现代化插件管理） ====================
    std::vector<PluginCenterItem> BuildPluginCenterItems();
    void RefreshPluginCenter();
    void HandlePluginCenterToggle(const std::wstring& id, bool enabled);
    void HandlePluginCenterAction(const std::wstring& id, const std::wstring& action);
    void ReloadJSPlugins();
    bool IsJSPluginDisabled(const std::wstring& id) const;
    void SetJSPluginDisabled(const std::wstring& id, bool disabled);
    void OpenBuiltinAiView();
    JSPlugin* BuiltinAiPlugin() const;

    bool ShowAiEditDialog(AiButtonConfig& cfg, const std::wstring& title, bool isNew);
    static LRESULT CALLBACK AiEditWndProc(HWND, UINT, WPARAM, LPARAM);

    // ==================== 绘制 ====================
    void DrawAiButton(LPDRAWITEMSTRUCT lpdis, int index);
    void DrawToolbarButton(LPDRAWITEMSTRUCT lpdis, int id);
    void DrawAiScrollBar(HDC hdc, const RECT& rcClient);

    // ==================== 主程序下载桥接 ====================
    void RequestDownloadViaMainProgram(const std::wstring& url,
        const std::wstring& suggestedName);

    // ==================== cd / 输入框动态布局 ====================
    std::wstring FormatCdDisplay(int maxWidthPx, int& outWidthPx);
    void LayoutCdAndInput();

    // ==================== Tooltip ====================
    void AddTooltip(HWND hWnd, const std::wstring& text);

    // ==================== 工具栏映射 ====================
    HWND GetToolbarHwndById(const std::wstring& id);
    int ToolbarIdFromString(const std::wstring& s);
    std::wstring ToolbarStringFromId(int id);
    std::wstring ToolbarDisplayName(const std::wstring& id);
    void ApplyToolbarLayout();

    // ==================== 侧边栏 ====================
    void CreateSidebar();
    void RefreshSidebar();
    int  HitTestSidebar(int x, int y);
    void SwitchView(int sidebarIndex);
    void LayoutSidebar(int width, int height);
    static LRESULT CALLBACK SidebarWndProc(HWND, UINT, WPARAM, LPARAM);

    // ==================== JS 插件系统（.zip） ====================
    void LoadJSPlugins();
    JSPlugin* CurrentJSPlugin();                    // 当前显示中的 JS 插件（无则 nullptr）
    void PaintPluginHostBackground(HDC hdc);        // 绘制当前插件视图背景壁纸
    void UnloadJSPlugins();
    int  FindPluginIndexById(const std::wstring& id) const;
    bool PromptPermission(const std::wstring& pluginId,
        const std::wstring& pluginName,
        const std::wstring& actionDesc);
    void OpenJSPluginView(int index);
    void OpenMainView();
    void RefreshSidebarJS();

    // ==================== 原生面板（外部 DLL 注册的原生界面） ====================
    void LoadNativePanels();
    void OpenNativePanelView(int index);
    void CloseNativePanel();

    // ==================== 成员 ====================
    std::atomic<bool> m_crxWatcherRunning{ false };
    std::thread m_crxWatcherThread;
    std::set<std::wstring> m_crxSeenFiles;
    std::mutex m_crxMutex;

    std::mutex m_extInstallMutex;
    bool m_extensionsLoaded = false;

    struct CrxFileProbe {
        LONGLONG size = -1;
        FILETIME mtime = {};
        int stableCount = 0;
        int retryCount = 0;
    };
    std::map<std::wstring, CrxFileProbe> m_crxProbes;

    PluginUIManager* m_ui = nullptr;
    CommandHandler m_commandHandler;
    OutputHandler m_outputHandler;

    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;

    GuiConfig m_config;

    HWND m_hTopbarSettingsBtn = nullptr;

    HWND m_hHelpEdit = nullptr;
    HWND m_hConsoleEdit = nullptr;
    HWND m_hConsoleInput = nullptr;
    HWND m_hPromptStatic = nullptr;
    HWND m_hAddressBar = nullptr;
    HWND m_hRestoreBtn = nullptr;

    HWND m_hTabBar = nullptr;
    HWND m_hTabContainer = nullptr;

    HWND m_hNavBack = nullptr;
    HWND m_hNavForward = nullptr;
    HWND m_hNavRefresh = nullptr;
    HWND m_hNavHome = nullptr;
    HWND m_hNavStar = nullptr;
    HWND m_hFavBtn = nullptr;
    HWND m_hNavZoomIn = nullptr;
    HWND m_hNavZoomOut = nullptr;
    HWND m_hNavDownload = nullptr;
    HWND m_hNavExtension = nullptr;

    HWND m_hTooltip = nullptr;

    std::vector<HWND> m_aiButtons;
    std::vector<bool> m_aiHovered;
    int m_aiScrollY = 0;
    bool m_aiDraggingScroll = false;
    int m_aiScrollDragStartY = 0;
    int m_aiScrollDragStartOffset = 0;

    friend LRESULT CALLBACK ToolbarButtonSubclassProc(HWND, UINT, WPARAM, LPARAM,
        UINT_PTR, DWORD_PTR);
    friend LRESULT CALLBACK AiButtonSubclassProc(HWND, UINT, WPARAM, LPARAM,
        UINT_PTR, DWORD_PTR);
    int m_toolbarHoveredId = 0;

    ICoreWebView2Environment* m_webViewEnv = nullptr;
    bool m_webViewReady = false;

    std::vector<BrowserTab> m_tabs;
    int m_activeTab = -1;

    bool m_browserExpanded = false;
    bool m_draggingSplitter = false;
    int m_dragStartY = 0;
    int m_dragStartH = 0;
    int m_browserHeight = 320;

    std::vector<std::wstring> m_favorites;
    std::wstring m_currentPath = L"D:";
    double m_zoomFactor = 1.0;

    std::vector<ExtInfo> m_extensions;
    bool m_extensionsEnabled = false;

    HWND m_hExtMgrWnd = nullptr;
    HWND m_hExtList = nullptr;
    HWND m_hExtDetail = nullptr;

    HWND m_hPopupWnd = nullptr;
    ICoreWebView2Environment* m_popupEnv = nullptr;
    ICoreWebView2Controller* m_popupController = nullptr;
    ICoreWebView2* m_popupWebView = nullptr;
    bool m_popupReady = false;

    HWND m_hOptionsWnd = nullptr;
    ICoreWebView2Environment* m_optionsEnv = nullptr;
    ICoreWebView2Controller* m_optionsController = nullptr;
    ICoreWebView2* m_optionsWebView = nullptr;

    HWND m_hDetailWnd = nullptr;

    HWND m_hSettingsWnd = nullptr;
    HWND m_hSetTab = nullptr;          // 左侧现代化导航（owner-draw LISTBOX）
    HWND m_hSetAiList = nullptr;
    HWND m_hSetTbList = nullptr;
    HWND m_hSetWinMainW = nullptr;
    HWND m_hSetWinMainH = nullptr;
    HWND m_hSetWinBrowserH = nullptr;
    HWND m_hSetWinExtW = nullptr;
    HWND m_hSetWinExtH = nullptr;
    HWND m_hSetWinPopupW = nullptr;
    HWND m_hSetWinPopupH = nullptr;
    HWND m_hSetWinOptW = nullptr;
    HWND m_hSetWinOptH = nullptr;
    int m_settingsCurTab = 0;
    int m_settingsNavHover = -1;
    PluginCenterPanel m_pluginCenter;
    GuiDllStorePanel m_guiDllStore;

    HWND m_hSidebar = nullptr;
    int m_sidebarHovered = -1;
    int m_sidebarPressed = -1;
    int m_sidebarActive = -2;

// ========== JS 插件（.zip） ==========
    std::vector<std::unique_ptr<JSPlugin>> m_jsPlugins;
    HWND m_hPluginHost = nullptr;
    WNDPROC m_oldPluginHostProc = nullptr;  // 宿主容器原窗口过程（替换前保存）
    int  m_currentPluginIndex = -1;
    int  m_aiIndex = -1;                    // 内置「光语 AI」在 m_jsPlugins 中的位置
    bool m_aiEnabled = true;                // 内置 AI 是否在侧边栏显示

// ========== 原生面板（外部 DLL 注册的原生界面） ==========
    std::vector<NativePanelRecord> m_nativePanels;
    HWND m_nativePanelHwnd = nullptr;
    int  m_activeNativePanel = -1;

    ULONG_PTR m_gdiplusToken = 0;
    bool m_oleInitialized = false;
    HFONT m_hFontUI = nullptr;
    HFONT m_hFontConsole = nullptr;
    HFONT m_hFontAddress = nullptr;
    HFONT m_hFontSidebar = nullptr;
    HBRUSH m_hbrBackground = nullptr;
};