#pragma once

// ============================================================================
// ApiAiNativePlugin —— com.yx.api_ai 的原生 C++ 实现（不再依赖任何 .js）
//
// 宿主不再为 id=com.yx.api_ai 创建 QuickJS runtime、不再 Eval main.js；
// 插件全部 UI / 状态 / 网络 / 工具逻辑由本类用原生 C++ 实现，
// 生命周期通过 JSPlugin 基类虚方法由宿主分派。
//
// 复用的基类基础设施：
//   - CreateNativeControl : 控件创建 + 玻璃按钮/圆角卡片子类化 + 登记
//   - MutableControls()   : 控件表（JSPluginId::ctrlId -> HWND）
//   - FlatFont / SetCtrlColor / RunHooks : 自绘字体 / 颜色 / 扩展 Hook
//   - Unload()            : 统一清理（销毁控件 + 样式 + 壁纸缓存）
//   - RequestPermission   : 宿主权限弹窗
//
// 布局几何全部对齐 include/ApiAiMainJs.inc 原版（功能蓝本，不再参与编译）。
// ============================================================================

#include "JSPlugin.h"
#include "YXGuiPlugin.h"
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class ApiAiNativePlugin : public JSPlugin {
public:
    ApiAiNativePlugin();
    virtual ~ApiAiNativePlugin();

    // ===== 生命周期 override（宿主直接调用，多态分派到本类） =====
    void CallOnLoad();
    void CallOnViewShow(HWND hostContainer, int w, int h);
    void CallOnViewHide();
    // 注：CallOnLoad 移除 override 是为了修复编译器 C3668，当基类因宏/编码问题未被识别为可重写时仍能编译。请在修复根因后恢复 override。    
    void CallOnResize(int w, int h);
    void CallOnUnload();
    void CallOnControlEvent(const std::wstring& controlId, int code);

    // 异步 HTTP 完成（宿主 WM_YXGUI_HTTP_DONE 会遍历全部插件调用；
    // 原生插件自持 pending 表按 seq 认领，不走 JS resolve）
    void CallHttpDone(uint32_t seq, const JSPlugin::HttpResult& result);

    // 允许直接使用基类富文本链接注册函数（解决编译时名查找问题）
    void RegisterRichLink(HWND hEdit, LONG startPos, LONG endPos, const std::wstring& tag) {
        JSPlugin::RegisterRichLink(hEdit, startPos, endPos, tag);
    }

protected:

    // ===== 渲染 =====
    void renderMain();      // 头部 + 右上角圆形设置按钮 + 仅对话页
    void renderChat();      // 对话页：模式条 / 输入卡 / 富文本历史 / 消息操作
    void renderWall();      // 壁纸管理（显示设置窗口内复用）
    void renderAbout();     // 关于页（设置窗口内复用）

    // ===== 控件创建封装（对齐 main.js 的 make/label/panel/button/edit/rule）=====
    HWND MakeLabel(const std::wstring& id, const std::wstring& text,
        int x, int y, int w, int h, int fg, HFONT font = nullptr);
    HWND MakePanel(const std::wstring& id, int x, int y, int w, int h,
        int fill, int edge);
    void MakeRule(const std::wstring& id, int x, int y, int w, int color);
    // kind: 0=ghost 1=primary 2=active 3=danger（玻璃按钮：不设 bg，防贴纸感）
    HWND MakeButton(const std::wstring& id, const std::wstring& text,
        int x, int y, int w, int h, int kind, int fgOverride = -1,
        int borderOverride = -1);
    // v12: 内陷色板（调色板）：实色色片嵌在凹陷插座里，selected 描蓝环 + ✓
    HWND MakeSwatch(const std::wstring& id, COLORREF color,
        int x, int y, int d, bool selected);
    HWND MakeEdit(const std::wstring& id, const std::wstring& text,
        int x, int y, int w, int h, bool textarea = false, bool password = false);
    // kind: 0=ghost 1=primary 2=active 3=danger；文本传 L"gear" 表示画齿轮图标
    HWND MakeRoundButton(const std::wstring& id, const std::wstring& text,
        int x, int y, int d, int kind, int fgOverride = -1, int borderOverride = -1);
    HWND MakeRichOut(const std::wstring& id, int x, int y, int w, int h);

    // ===== 通用工具 =====
    static COLORREF HexToColor(const std::wstring& hex);
    static std::wstring Trunc(const std::wstring& s, int n);
    static std::wstring FileNameOf(const std::wstring& path); // 取路径末尾文件名/目录名

    // ===== 子渲染（对齐 main.js 同名函数） =====
    void renderHeader(const std::wstring& windowId, const std::wstring& title,
        const std::wstring& subtitle);
    void renderChatEmpty(int x, int y, int w, int h);
    void renderChatActive(int x, int y, int w, int h);
    void renderInputCard(int x, int y, int w, int h);
    void renderWallList(const std::wstring& kind, const std::wstring& title,
        int x, int y, int w, int h);
    void renderWallActions(int x, int y, int w, int h);

    // 辅助：销毁全部子控件并清空登记表（重建视图前调用）
    // keepId 非空时保留该 id 的控件（chat_output：跨重建保留，避免全量重渲染历史）
    void ClearChildControls(const std::wstring& keepId = L"");

    // v12: 富文本输出控件——存在则移位复用，不存在则创建（返回 nullptr 表示创建失败）
    HWND EnsureRichOutput(int x, int y, int w, int h);

    // v12: 历史内容版本（增删改/加载时递增；rich 渲染后记录版本，未变化则跳过重渲染）
    void InvalidateHistory() { ++m_histVersion; }

    // v12: 流式增量渲染——只把 AI 消息的新增片段追加到 rich 末尾
    void AppendStreamDelta(const std::wstring& delta);

    // v12: 富文本链接点击（code=0x8000）动作分发
    void HandleRichLinkTag(const std::wstring& tag);

    // v12: 设置窗口现代化（对齐主窗口设置窗口：深色导航栏 + 浅色内容区 + 白底栏）
    void RenderSettingsBackground(HDC hdc, const RECT& rc);
    int  SettingsNavHitTest(int x, int y) const;
    void EnsureAsyncWnd();
    static LRESULT CALLBACK AsyncWndProcStatic(HWND hWnd, UINT msg,
        WPARAM wParam, LPARAM lParam);
    LRESULT AsyncWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 按控件 id 查 HWND（id 不含 pluginId:: 前缀），未找到返回 nullptr
    HWND Ctl(const std::wstring& id);

    // 设置窗口控件查 HWND（查 m_setControls，key 无 pluginId 前缀）
    HWND SetCtl(const std::wstring& id);
    std::wstring SetCtlRead(const std::wstring& id);

    // ============================================================================
    // T6：原生独立设置窗口（owner=宿主主窗，客户区 960x800，6 tab）
    // ============================================================================
public:
    void OpenSettingsWindow();   // 创建独立顶层设置窗口（点击齿轮时调用）
    void CloseSettingsWindow();  // 销毁设置窗口

protected:
    enum SettingsTab {
        TAB_API = 0,
        TAB_WEB,
        TAB_DISP,
        TAB_TOOLS,
        TAB_BRANCH,
        TAB_ABOUT,
        TAB_COUNT
    };

    // 设置窗口 WndProc（静态，内部转 this）
    static LRESULT CALLBACK SettingsWndProcStatic(HWND hWnd, UINT msg,
        WPARAM wParam, LPARAM lParam);

    // SSE 窗口过程（后台线程 PostMessage -> UI 线程）
    static LRESULT CALLBACK SseWndProcStatic(HWND hWnd, UINT msg,
        WPARAM wParam, LPARAM lParam);
    LRESULT SseWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 设置窗口内渲染（按当前 tab 分派）
    void RenderSettingsTab();
    void RenderSettingsApi(int x, int y, int w, int bottom);
    void RenderSettingsWeb(int x, int y, int w, int bottom);
    void RenderSettingsDisp(int x, int y, int w, int bottom);
    void RenderSettingsTools(int x, int y, int w, int bottom);
    void RenderSettingsBranch(int x, int y, int w, int bottom);
    void RenderSettingsAbout(int x, int y, int w, int bottom);

    // 设置窗口内控件创建（在 m_hSettings 上创建，id 加 "set_" 前缀）
    HWND MakeSetLabel(const std::wstring& id, const std::wstring& text,
        int x, int y, int w, int h, int fg, HFONT font = nullptr);
    HWND MakeSetPanel(const std::wstring& id, int x, int y, int w, int h,
        int fill, int edge);
    HWND MakeSetButton(const std::wstring& id, const std::wstring& text,
        int x, int y, int w, int h, int kind, int fgOverride = -1, int borderOverride = -1);
    HWND MakeSetSwatch(const std::wstring& id, COLORREF color,
        int x, int y, int d, bool selected);
    HWND MakeSetEdit(const std::wstring& id, const std::wstring& text,
        int x, int y, int w, int h, bool textarea = false, bool password = false);

    // v12: 设置窗口辅助字体（12px 正文 / 15px 加重 / 20px 标题）
    HFONT SubFont();
    HFONT BigFont();
    HFONT TitleFont();

    // 设置窗口事件处理
    void OnSettingsControlEvent(const std::wstring& ctrlId, int code);
    void SaveSettingsApi();
    void SaveSettingsWeb();
    void SaveSettingsDisp();
    void SaveSettingsTools();
    void BrowseVideoFile();   // T9: 选择视频文件（mp4/webm/wmv）
    void BrowseLivelyDir();   // T9: 选择 Lively 壁纸目录（含 project.json）
    void BrowseImageFile();   // T10: 选择图片文件（png/jpg/gif/bmp/webp）
    void ChooseImportFiles(); // T10: 文件多选导入（zip/pkg/视频/图片）
    void ImportWallpaperFiles(const std::vector<std::wstring>& files); // T10: 统一导入入口（点击/拖拽）
    void ScanWallpaperLibrary(); // T10: 扫壁纸库目录填充 m_wallCt/m_wallAf
    void TestApiConnection();

    // T9：按 m_bgMode 应用持久化背景（初始化 / 显示设置保存后调用）
    void ApplySavedWallpaper();

    // 模型预设（22 个，对齐原版 main.js MODEL_PRESETS）
    static const wchar_t* MODEL_PRESETS[22];

private:
    // ===== 视图状态 =====
    HWND m_host = nullptr;        // 宿主挂载容器（GetHostContainer 副本）
    int  m_width = 0;             // 最近一次 onViewShow 的宽
    int  m_height = 0;            // 最近一次 onViewShow 的高
    bool m_viewVisible = false;   // 当前是否处于显示状态
    bool m_initialized = false;   // CallOnLoad 是否已完成初始化

    // ===== 对话状态（T6 布局渲染用；T8 扩展数据层） =====
    int  m_mode = 0;              // 0=api 1=reverse
    bool m_busy = false;
    std::wstring m_status;        // 状态提示（warnFg 显示）
    int  m_historyCount = 0;      // 历史消息条数（>0 走活跃视图）
    int  m_pendingImageCount = 0; // 待发送图片数
    int  m_displayLimit = 20;     // 当前显示上限（PAGE_SIZE）
    std::wstring m_draftPrompt;   // 重建前捕获的输入草稿
    bool m_toolsEnabled = false;
    bool m_toolsRequireConfirm = true;
    int  m_branchCount = 0;       // 分支数

    // ===== 配置（T8 读 INI 对齐原版；T6 用默认值仅驱动布局） =====
    std::wstring m_cfgApiUrl;     // api.url
    std::wstring m_cfgWebUrl;     // web.url
    std::wstring m_apiModel;      // 默认 gpt-4o-mini
    bool m_webCookies = false;    // web.cookies 非空（显示"已登录"）

    // ===== 壁纸页状态 =====
    int  m_bgMode = 0;            // 0=solid 1=image 2=cycle
    std::wstring m_bgColor;       // 默认 #F4F7FB
    int  m_cycleMs = 3000;
    int  m_cycleDraftMs = 0;      // 重建前捕获的 cycleMs 草稿
    std::wstring m_wallpaperLibDir; // 壁纸库根目录（Cache\gui_api_ai\wallpapers）
    std::vector<std::wstring> m_wallCt;  // 未启用列表
    std::vector<std::wstring> m_wallAf;  // 已启用列表
    int  m_wallSelCt = -1;
    int  m_wallSelAf = -1;
    int  m_wallPageCt = 0;
    int  m_wallPageAf = 0;

    // T9: 动态壁纸可用项（video: 同目录视频文件；lively: 子目录含 project.json）
    std::vector<std::wstring> m_videoItems;
    std::vector<std::wstring> m_livelyItems;
    std::vector<std::wstring> m_imgItems;   // T10: 壁纸库 images 目录中的图片
    void ScanVideoItems();   // 扫 videoPath 所在目录的视频文件
    void ScanLivelyItems();  // 扫 livelyDir 下含 project.json 的子目录
    void ScanImageItems();   // T10: 扫壁纸库 images 目录的图片文件
    static const int WALL_PAGE_SIZE = 6;

    // ===== 配置 / 数据路径（T8 对齐外部 main.js 的读写位置） =====
    std::wstring m_configDir;     // 插件配置目录（建议指向原 Cache\gui\API_AI）
    std::wstring m_historyDir;    // 对话历史目录

    // ===== T6：独立设置窗口状态 =====
    HWND m_hSettings = nullptr;       // 设置窗口 HWND
    SettingsTab m_settingsTab = TAB_API;
    bool m_settingsTestBusy = false;  // API 测试连接中
    int  m_nextSetCtrlId = 20000;     // 设置窗口控件 ID 起点（与主窗口 10000 错开）
    std::map<std::wstring, HWND> m_setControls;  // 设置窗口控件表
    std::map<HWND, std::string>  m_setCtrlType;
    // 设置窗口首次 WM_SIZE 前不渲染 tab（WM_CREATE 时客户区未定）
    bool m_settingsFirstRender = false;
    std::map<HWND, CtrlColor>    m_setCtrlColors;
    std::map<HWND, bool>         m_setCtrlHover;
    // v12: 设置窗口现代化（深色导航栏 hit-test）
    static const int SET_RAIL_W = 216;
    RECT m_navRects[TAB_COUNT] = {};
    int  m_navHover = -1;
    std::map<COLORREF, HBRUSH> m_setBrushes;   // WM_CTLCOLOR* 画刷缓存

    // ===== v12: 渲染性能状态 =====
    int  m_histVersion = 0;          // 历史内容版本（变化时递增）
    int  m_richVersion = -1;         // 已渲染进当前 rich 控件的历史版本（-1=需重渲染）
    int  m_sseRendered = 0;          // 流式期间已追加到 rich 的字符数
    bool m_wallpaperApplied = false; // 壁纸是否已应用（避免重复 apply 触发快照重建）
    std::wstring m_wallKey;          // 上次应用的壁纸配置键

    // ===== v12: 辅助字体 =====
    HFONT m_fontSub = nullptr;       // 12px 正文
    HFONT m_fontBig = nullptr;       // 15px 加重
    HFONT m_fontTitle = nullptr;     // 20px 标题

    // ===== v12: 异步结果消息窗（HTTP 测试等，后台线程 → UI 线程）=====

    HWND m_asyncHwnd = nullptr;
    static const UINT WM_API_HTTP_DONE = WM_USER + 0x4A11;

    // ============================================================================
    // T8 数据层：配置 / 历史 / 分支 / HTTP pending / SSE / 富文本 / 工具
    // ============================================================================
private:
    // ---- 配置结构体（对齐 main.js DEFAULT + INI） ----
    struct CfgApi {
        std::wstring url;
        std::wstring key;
        std::wstring model = L"gpt-4o-mini";
    };
    struct CfgWeb {
        std::wstring url;
        std::wstring selector;
        std::wstring method = L"POST";   // POST / GET
        std::wstring ctype  = L"json";   // json / form
        std::wstring templateStr = L"{\"prompt\":\"{{prompt}}\"}";
        std::wstring cookies;
    };
    struct CfgDisp {
        std::wstring bgMode = L"solid";  // solid / image / cycle / video / lively
        std::wstring bgColor = L"#F4F7FB";
        std::wstring bgImage;
        int cycleMs = 3000;
        std::wstring videoPath;  // T9: 视频壁纸路径
        std::wstring livelyDir;  // T9: Lively 动态壁纸目录
    };
    struct CfgTools {
        bool enabled = true;
        int  maxRounds = 6;
        bool requireConfirm = true;
    };
    CfgApi   m_cfgApi;
    CfgWeb   m_cfgWeb;
    CfgDisp  m_cfgDisp;
    CfgTools m_cfgTools;

    // ---- 历史消息结构 ----
    struct HistMsg {
        std::wstring id;           // 前端消息 id（如 L"m0"）
        std::wstring role;         // system / user / assistant / tool
        std::wstring text;         // 文本内容
        std::vector<std::wstring> images; // base64 数据 URL（仅 user）
        // tool_calls / tool 响应字段
        std::vector<std::wstring> toolCallIds; // 并行 tool_calls 时各 id
        std::wstring toolCallId;   // tool 消息的 call_id
        std::wstring toolName;
        std::wstring toolArgs;
        std::wstring toolResult;
    };
    std::vector<HistMsg> m_history; // 当前会话全部历史

    // ---- 分支结构 ----
    struct Branch {
        std::wstring name;
        std::vector<HistMsg> items;
    };
    std::vector<Branch> m_branches;
    int m_branchSel = -1;           // 当前激活分支索引（-1=主线）

    // ---- HTTP 异步通道（原生 pending 表，seq 自 0x40000000 起） ----
    struct PendingHttp {
        std::function<void(int status, const std::string& body, const std::string& err)> cb;
    };
    std::map<uint32_t, PendingHttp> m_pendingHttp;
    uint32_t m_nextHttpSeq = 0x40000000;
    std::mutex m_pendingHttpMtx;

    // ---- SSE 自建窗口与状态 ----
    HWND m_sseHwnd = nullptr;       // message-only 窗口，接收后台线程 PostMessage
    static const UINT WM_SSE_CHUNK = WM_USER + 901;
    static const UINT WM_SSE_DONE  = WM_USER + 902;
    static const UINT WM_SSE_ERROR = WM_USER + 903;
    static const UINT WM_SSE_TOOL_CONFIRM = WM_USER + 904;
    struct SSEState {
        std::wstring buffer;  // 旧字段，保留兼容
        std::string bufferStr;         // 原始 SSE 缓冲
        std::wstring text;           // 当前增量文本
        bool done = false;
        // tool_calls 解析槽（按 index）
        struct TC {
            std::wstring id;
            std::wstring type;
            std::wstring name;
            std::wstring args;
        };
        std::map<int, TC> toolCalls;
        std::vector<int> toolOrder;  // 按出现顺序的 index
    };
    SSEState m_sse;
    int m_sseAiIdx = -1;            // 当前助手消息在 m_history 中的索引
    bool m_sseStreaming = false;    // 是否正在流式接收

    // ---- 富文本代码块缓存（对齐 g_codeBlocks） ----
    std::map<int, std::wstring> m_codeBlocks; // idx -> 代码文本
    int m_nextCodeBlockIdx = 0;

    // ---- 工具执行相关 ----
    int m_toolRound = 0;            // 当前工具调用轮数（防超限）

    // ============================================================================
    // T8 方法声明
    // ============================================================================
    // -- 路径 --
    std::wstring CacheDir() const;
    std::wstring CfgPath() const;
    std::wstring ChatPath() const;
    std::wstring BranchPath() const;

    // -- INI --
    void LoadCfg();
    void WriteCfg();

public:
    // -- JSON 读写（mini 实现） --
    struct JsonVal;
    JsonVal ParseJson(const std::string& s);
    std::string JsonToString(const JsonVal& v, bool pretty = false);

protected:

    // -- 历史 / 分支 --
    void LoadHistory();
    void SaveHistory();
    void LoadBranches();
    void SaveBranches();
    void ClearHistoryUI();
    int  AppendUserMsg(const std::wstring& text,
                       const std::vector<std::wstring>& images);
    int  AppendAiMsg(const std::wstring& text);
    void SetAiMsgText(int idx, const std::wstring& text);

    // -- 消息构建（OpenAI / Claude） --
    std::string BuildMessagesOpenAI();
    std::string BuildMessagesClaude();
    std::string BuildMessages();

    // -- HTTP（非流式 & SSE） --
    uint32_t HttpPost(const std::wstring& url,
        const std::string& body,
        const std::wstring& authBearer,
        std::function<void(int,const std::string&,const std::string&)> cb);
    void OnHttpDone(uint32_t seq, int status, const std::string& body,
                    const std::string& err);
    void StartSseRequest(const std::string& payload);
    void OnSseChunk(const std::string& chunk);
    void OnSseDone();
    void OnSseError(const std::string& err);

    // -- 工具 --
    std::wstring RunTool(const std::wstring& name,
                         const std::wstring& argsJson);
    bool RunToolCalls(const std::vector<std::pair<std::wstring,std::wstring>>& calls);
    std::wstring GetCacheDirW() const;

    // -- 富文本渲染 --
    void RichClear();
    void RichAppendText(const std::wstring& text);
    void RichAppendCode(const std::wstring& code, const std::wstring& lang);
    void RichAppendHeading(int level, const std::wstring& text);
    void RichAppendQuote(const std::wstring& text);
    void RichAppendBullet(const std::wstring& text);
    void RichAppendLink(const std::wstring& text,
                        const std::wstring& tag);
    void RichAppendHr();
    void RichAppendImage(const std::wstring& alt,
                         const std::wstring& dataUrl);
    void ParseAndRenderMarkdown(const std::wstring& md);
    void RenderHistoryToRich();

    // -- 消息操作 --
    void DoCopyMessage(int idx);
    void DoEditMessage(int idx);
    void DoRegenerateMessage(int idx);
    void DoBranchMessage(int idx);
    void DoDeleteMessage(int idx);
    void DoShowToolArgs(int idx);

    // -- SSE 后台线程（static 入口） --
    static DWORD WINAPI SseThreadProc(LPVOID lpParam);
};
