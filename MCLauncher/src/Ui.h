// Ui.h — 自绘主窗口（HMCL 风格 + PCL2 磨砂质感）
#pragma once

#include "Theme.h"
#include "MlcData.h"
#include "ContentLib.h"
#include "AddonRepo.h"
#include "JavaDetect.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ui {

// 事件槽：后台线程只持有它（shared_ptr），UI 销毁后 alive=false，线程不再碰已释放的 Ui。
struct EventSink {
    std::mutex mu;
    std::vector<std::pair<int, std::string>> queue;
    std::atomic<bool> alive{ true };
    HWND hwnd = nullptr;
    void post(int type, const std::string& data);
};

// 卡片网格里的一张卡片
struct GridItem {
    std::wstring key;        // 唯一键（文件名/实例 id/版本 id/slug）
    std::wstring title;
    std::wstring subtitle;
    std::wstring badge;      // 右上角角标
    std::wstring cover;      // 封面图片路径（空 = 用占位图）
    COLORREF tint = 0;       // 占位色（0 = 主题色）
    int loaderIcon = 0;      // 1 Fabric 2 Quilt 3 Forge 4 NeoForge（画内置 logo）
    bool on = true;          // 开关状态
    bool toggleable = false; // 显示开关
    std::wstring action;     // 显示按钮（空 = 无按钮）
    bool actionDanger = false;
    std::vector<std::wstring> tags;   // HMCL TwoLineListItem 的类别/渠道标签
    bool section = false;             // 分组标题行（不可点击）
    COLORREF tagColor = 0;            // 渠道色：稳定/测试/快照
};

struct Widget {
    enum Type { Btn, Label, Edit, Combo, List, Progress, Slider, Check, Panel, Grid, Rows };
    Type type = Label;
    std::wstring id;
    std::wstring text;
    RECT rc{};               // 相对客户区
    bool hover = false;
    bool pressed = false;
    COLORREF accent = 0;     // 强调色（0=默认）
    bool active = false;     // 选中态（Tab）
    int style = 0;           // 文本样式：0 正文 1 小标题 2 大标题
    COLORREF color = 0;      // 文本色（0 = 按样式自动）

    // Edit
    std::wstring editText;
    bool focused = false;
    std::wstring placeholder;

    // Combo
    std::vector<std::wstring> items;
    int sel = -1;
    bool dropped = false;

    // List
    std::vector<std::wstring> listItems;
    std::vector<std::wstring> subItems;
    std::vector<std::wstring> rowIcons;   // 可选：每行左侧的皮肤 PNG 路径（画成头像）
    int scroll = 0;
    int hot = -1;
    int selIdx = -1;

    // Progress
    float progress = 0.0f;

    // Slider
    int value = 0, vmin = 0, vmax = 100;

    // Check
    bool checked = false;

    // Grid / Rows（平铺列表）
    std::vector<GridItem> items2;
    int cols = 4;
    int cardW = 176;
    int cardH = 208;
    int gap = 12;
    int gscroll = 0;
    int hotIdx = -1;
    int rowH = 56;           // Rows 行高
};

class Ui {
public:
    // 页面
    enum Page { Home, Versions, GameSelect, Download, Account, Settings };

    // 版本管理内的标签页
    enum VerTab { TabMods = 0, TabLoader, TabVerSet, TabPacks, TabShaders, TabWorlds, TabSchematics };

    Ui() = default;
    ~Ui();

    bool Create(HINSTANCE hInstance, HWND parent);
    HWND hwnd() const { return m_hwnd; }

    void SetPage(Page p);
    Page page() const { return m_page; }

    void Rebuild();
    void Paint(HDC hdc, const RECT& client);

    void OnMouseMove(int x, int y);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnChar(wchar_t c);
    void OnKeyDown(WPARAM vk);
    void OnMouseWheel(int delta, int x, int y);
    void OnTimerTick();
    void OnAsyncEvent(int type, const std::string& data);
    void CommitEdit();              // 提交原生编辑框内容到当前控件
    // 从任意线程投递一个事件到 UI 线程（内部加锁 + PostMessage）
    void PostEvent(int type, const std::string& data);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    // ---- 页面构建 ----
    void BuildHome();
    void BuildVersions();
    void BuildGameSelect();
    void BuildDownload();
    void BuildAccount();
    void BuildSettings();
    void BuildSidebarHints();   // 占位（无控件）

    // ---- 绘制 ----
    void PaintHome(Gdiplus::Graphics& g);
    void PaintVersions(Gdiplus::Graphics& g);
    void PaintGameSelect(Gdiplus::Graphics& g);
    void PaintDownload(Gdiplus::Graphics& g);
    void PaintAccount(Gdiplus::Graphics& g);
    void PaintSettings(Gdiplus::Graphics& g);

    void DrawWidget(Gdiplus::Graphics& g, const Widget& w);
    void DrawGrid(Gdiplus::Graphics& g, const Widget& w);
    void DrawRows(Gdiplus::Graphics& g, const Widget& w);
    void DrawCard(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, const GridItem& it,
                  bool hot, bool sel);
    void DrawDropdown(Gdiplus::Graphics& g, const Widget& w);
    void DrawSidebar(Gdiplus::Graphics& g);
    void DrawDownloadDialog(Gdiplus::Graphics& g);
    void DrawAddonDialog(Gdiplus::Graphics& g);   // 模组版本确认 / 更新日志弹窗底图
    void DrawOfflineDialog(Gdiplus::Graphics& g);
    void DrawToast(Gdiplus::Graphics& g);

    // ---- 命中/交互 ----
    Widget* Find(const std::wstring& id);
    void HandleButton(const std::wstring& id);
    void HandleGridClick(Widget& w, int idx, bool inButton);
    void HandleComboSelect(Widget& w);
    int  HitGridIndex(const Widget& w, int x, int y);
    bool HitGridButton(const Widget& w, int idx, int x, int y);
    int  HitRowIndex(const Widget& w, int x, int y);
    bool HitRowButton(const Widget& w, int idx, int x, int y);
    int  GridRowsVisible(const Widget& w);

    // ---- 数据 ----
    void RefreshMods(bool force);
    void RefreshPacks(bool force);
    void RefreshShaders(bool force);
    void RefreshWorlds(bool force);
    void RefreshSchematics(bool force);
    void RefreshInstances();
    void RefreshLoaderVersions();
    void StartDownloadSearch();
    // ---- HMCL 式模组下载 ----
    void BuildModDownload();                 // 搜索列表 / 项目详情
    void StartAddonSearch(int page);
    void StartAddonCategories();
    void StartAddonVersions(const std::string& projectId);
    void StartAddonDependencies();
    void StartAddonChangelog();
    void StartAddonInstall(bool saveAs);
    void ShowAddonVersionDialog();
    void StartIntegrityCheck(bool repair);
    void StartLaunch(const std::wstring& instanceId);
    void ApplyPageScroll(int delta);
    void ShowToast(const std::wstring& text, bool error = false);

    mlc::Instance* CurInst();
    std::wstring CurVerIdW();

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    int m_width = 1180, m_height = 760;
    Page m_page = Home;

    std::vector<Widget> m_widgets;
    // 后台线程只碰 m_sink（shared_ptr），UI 销毁后不会再访问已释放对象
    std::shared_ptr<EventSink> m_sink = std::make_shared<EventSink>();
    std::mutex m_evMu;
    std::vector<std::pair<int, std::string>> m_evQueue;

    // 安装/任务
    std::wstring m_progressText;
    float m_progress = 0.0f;
    bool m_installing = false;
    bool m_showDlDialog = false;
    std::wstring m_dlDialogTitle;

    // 提示条
    std::wstring m_toast;
    bool m_toastError = false;
    ULONGLONG m_toastUntil = 0;

    // 账号
    std::wstring m_msUserCode;
    std::wstring m_msVerifyUri;
    bool m_msPending = false;
    std::wstring m_msStatus;
    std::wstring m_loginName;

    // 创建离线账号弹窗
    bool m_showOfflineDlg = false;

    // 账号列表当前选中的行（0-based；-1 = 跟随当前账号）
    // Rebuild 后必须保留，否则「使用选中账号」会拿回当前账号，切不动。
    int m_accSel = -1;

    // 皮肤预览（账号页右侧，可鼠标交互）
    RECT m_skinBox{ 0, 0, 0, 0 };
    float m_skinYaw = 22.0f;
    float m_skinPitch = -6.0f;
    float m_skinZoom = 1.0f;
    bool m_skinDrag = false;
    int m_dragX = 0, m_dragY = 0;

    // 皮肤操作（选择 / 上传 / 从正版拉取）
    bool m_skinBusy = false;         // 后台任务进行中
    std::wstring m_skinMsg;          // 皮肤区状态行
    std::wstring m_skinSelKey;       // 当前预览的账号 key（列表选中项）
    std::wstring m_skinAutoKey;      // 已自动拉取过远程皮肤的账号 key（避免反复请求）

    // 页面滚动
    int m_scrollY = 0;

    // 编辑框覆盖（原生 EDIT，支持中文输入法）
    HWND m_editOverlay = nullptr;
    std::wstring m_editTargetId;

    // 版本选择：文件夹切换（0 = 全部）
    int m_gsFolderSel = 0;

    // 版本管理
    int m_verTab = TabMods;
    std::wstring m_verInstId;      // 正在管理的实例（为空 = 当前实例）
    std::wstring m_verTabStatus;
    std::vector<content::Item> m_mods, m_packs, m_worlds, m_schems, m_shaders;
    bool m_modsLoaded = false, m_packsLoaded = false, m_worldsLoaded = false,
         m_schemsLoaded = false, m_shadersLoaded = false;
    std::wstring m_cacheInstId;    // 缓存对应的实例

    // 加载器页
    int m_loaderSel = 0;           // 0 Fabric 1 Quilt 2 Forge 3 NeoForge
    std::vector<std::wstring> m_loaderVersions;
    bool m_loaderFetching = false;
    std::wstring m_loaderStatus;

    // 下载页
    int m_dlCat = 0;               // 0 游戏 1 模组 2 资源包 3 光影 4 世界
    int m_dlChannel = 0;           // 0 正式 1 测试 2 快照 3 愚人节 4 远古
    std::wstring m_dlSearch;
    std::wstring m_dlVersionId;
    std::wstring m_dlName;
    int m_dlLoader = 0;            // 0 原版 1 Fabric 2 Quilt 3 Forge 4 NeoForge
    std::wstring m_dlLoaderVer;
    int m_dlFolderSel = 0;
    std::vector<std::wstring> m_dlFolders;   // 已注册的文件夹（去重）
    std::vector<content::Hit> m_hits;
    bool m_dlSearching = false;
    int m_dlOffset = 0;                // 内容搜索分页偏移（每页 20）
    bool m_manifestRequested = false;

    // 下载页 · 模组：HMCL 式搜索 / 详情 / 版本弹窗
    int m_addonPage = 0, m_addonPages = 0;
    int m_addonSort = 0;               // addon::Sort
    std::wstring m_addonMcVer;
    std::vector<std::wstring> m_addonCategoryNames;
    std::vector<std::string> m_addonCategoryIds;
    int m_addonCategorySel = 0;
    std::vector<addon::Hit> m_addonHits;
    unsigned long long m_addonSearchSeq = 0;
    bool m_addonDetail = false;
    addon::Hit m_addonProject;
    std::vector<addon::Version> m_addonVersions;
    bool m_addonVersionsLoading = false;
    addon::Version m_addonVersion;
    bool m_addonShowDlg = false;       // 版本确认弹窗
    std::vector<addon::Dependency> m_addonDeps;
    bool m_addonDepsLoading = false;
    std::wstring m_addonChangelog;
    bool m_addonShowChangelog = false;

    // 下载页 · 世界：0 = CurseForge 在线，1 = 本地存档
    int m_worldSource = 0;
    std::vector<content::cf::WorldHit> m_cfHits;
    bool m_cfSearching = false;
    int m_cfPage = 0;
    std::wstring m_cfStatus;
    void StartCfSearch(int page);
    void StartCfDownload(const content::cf::WorldHit& hit);

    // Java 检测缓存（避免每次重建都扫注册表）
    std::vector<java::JavaRuntime> m_javas;
    bool m_javasLoaded = false;

    // 版本设置表单状态（保存时读取）
    int m_vsTabScroll = 0;

    // 游戏运行状态
    bool m_running = false;
    bool m_launching = false;   // 进程已创建、正在等游戏窗口
    ULONGLONG m_launchTick = 0; // 「启动中」计时起点（用于显示已等待秒数）
    DWORD m_pid = 0;
};

} // namespace ui
