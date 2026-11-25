#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_

#include "ApiAiNative.h"
#include "YXGuiPlugin.h"
#include <winsock2.h>
#include <ws2tcpip.h>

#include "winwrap.h"
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <winhttp.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")

// v12: 现代设置窗口背景与圆角路径需要 GDI+（与 JSPlugin.cpp 一致）
using namespace Gdiplus;

// 前向声明：子类过程定义在 JSPlugin.cpp 中，设置窗口创建时需要用到
LRESULT CALLBACK PanelCardSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK FlatButtonSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
// 宿主导出的富文本链接清理（C 链接，在 JSPlugin.cpp 中定义）
extern "C" void YXGui_ClearRichLinks(HWND);

// T9：INI 读写与壁纸应用需要宽窄转换（实现在文件尾部）
static std::string WToUtf8(const std::wstring& w);
static std::wstring Utf8ToW(const std::string& u);

// T10：壁纸库目录确保存在 + zip/pkg 解压（实现在 SaveSettingsTools 之前）
static std::wstring EnsureWallpaperLib(const std::wstring& configDir);
static bool ExtractZip(const std::wstring& archive, const std::wstring& destDir);

// ============================================================================
// 调色板（对齐 main.js 的 C / QUICK_COLORS）
// ============================================================================
namespace {

// 解析 "#RRGGBB" -> COLORREF（0x00BBGGRR）；失败返回 RGB(0,0,0)
COLORREF ParseHex(const std::wstring& hex) {
    std::wstring s = hex;
    if (!s.empty() && s[0] == L'#') s.erase(0, 1);
    if (s.size() != 6) return RGB(0, 0, 0);
    auto hx = [&](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return 0;
    };
    int r = hx(s[0]) * 16 + hx(s[1]);
    int g = hx(s[2]) * 16 + hx(s[3]);
    int b = hx(s[4]) * 16 + hx(s[5]);
    return RGB(r, g, b);
}

// main.js 调色板 C（v9.6）
const COLORREF C_ink   = ParseHex(L"#0F172A");
const COLORREF C_ink2  = ParseHex(L"#334155");
const COLORREF C_ink3  = ParseHex(L"#64748B");
const COLORREF C_card      = ParseHex(L"#FFFFFF");
const COLORREF C_cardEdge  = ParseHex(L"#CBD5E1");
const COLORREF C_input     = ParseHex(L"#FFFFFF");
const COLORREF C_inputEdge = ParseHex(L"#94A3B8");
const COLORREF C_divider   = ParseHex(L"#E2E8F0");
const COLORREF C_primaryBg   = ParseHex(L"#DBEAFE");
const COLORREF C_primaryFg   = ParseHex(L"#1E3A8A");
const COLORREF C_primaryEdge = ParseHex(L"#2563EB");
const COLORREF C_ghostFg     = ParseHex(L"#1E3A8A");
const COLORREF C_ghostEdge   = ParseHex(L"#94A3B8");
const COLORREF C_activeFg    = ParseHex(L"#1E3A8A");
const COLORREF C_activeEdge  = ParseHex(L"#2563EB");
const COLORREF C_dangerFg    = ParseHex(L"#991B1B");
const COLORREF C_dangerEdge  = ParseHex(L"#DC2626");
const COLORREF C_warnFg      = ParseHex(L"#92400E");

// QUICK_COLORS（12 色，对齐 main.js）
const COLORREF QUICK_COLORS[] = {
    ParseHex(L"#FFFFFF"), ParseHex(L"#F4F7FB"), ParseHex(L"#E9F1FD"), ParseHex(L"#EAF7F0"),
    ParseHex(L"#FFF6E5"), ParseHex(L"#FFF0F2"), ParseHex(L"#EDEDED"), ParseHex(L"#D9E0E8"),
    ParseHex(L"#BFC9D6"), ParseHex(L"#AEB9C8"), ParseHex(L"#1D4ED8"), ParseHex(L"#0F172A")
};
const int QUICK_COLOR_COUNT = 12;

const wchar_t* VERSION_TEXT = L"9.6.0";

// v12 现代化补充色
const COLORREF C_accent  = ParseHex(L"#2563EB");   // 头部强调条 / 主色
const COLORREF C_readyFg = ParseHex(L"#15803D");   // 就绪状态（绿）
const COLORREF C_railTop = ParseHex(L"#1E293B");   // 设置窗导航栏渐变上
const COLORREF C_railBot = ParseHex(L"#0F172A");   // 设置窗导航栏渐变下
const COLORREF C_selBlue = ParseHex(L"#3B82F6");   // 导航选中蓝
const COLORREF C_socket  = ParseHex(L"#DCE4EE");   // 色板插座（内陷底色）

// v12: 圆角矩形路径（与 JSPlugin.cpp 同名 helper 相互独立，避免跨 TU 依赖）
void AddRoundRectPath(Gdiplus::GraphicsPath& path, float x, float y,
                      float w, float h, float r) {
    float d = r * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    r = d / 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

} // namespace

// ============================================================================
// 构造 / 析构
// ============================================================================

ApiAiNativePlugin::ApiAiNativePlugin()
    : m_apiModel(L"gpt-4o-mini"),
      m_bgColor(L"#F4F7FB") {
    SetMetaInfo(
        L"builtin.light.ai",
        L"AI接口",
        L"1.0.0",
        L"Light 内置",
        L"内置 AI 对话与网页模式，属于插件，需安装。",
        L"✨");
    MarkLoaded();
}


ApiAiNativePlugin::~ApiAiNativePlugin() {
    // 析构不做清理：宿主在关闭时通过 CallOnUnload -> Unload() 统一回收。
}

// ============================================================================
// 生命周期（宿主分派）
// ============================================================================

void ApiAiNativePlugin::CallOnLoad() {
    if (m_initialized) return;
    m_initialized = true;
    // T8: init paths, load cfg/history/branches
    // 统一路径：与 CacheDir()/CfgPath() 保持一致
    //   m_configDir  = <exe>\Cache\gui        （api_ai.m 所在目录）
    //   m_historyDir = <exe>\Cache\gui_api_ai （history/branches）
    //   m_wallpaperLibDir = <exe>\Cache\gui\wallpapers
    {
        wchar_t exe[1024] = {0};
        GetModuleFileNameW(NULL, exe, 1024);
        std::wstring cd = std::wstring(exe);
        size_t pos = cd.find_last_of(L"\\/");
        if (pos != std::wstring::npos) cd = cd.substr(0, pos);
        cd += L"\\Cache";
        m_configDir = cd + L"\\gui";
        m_historyDir = cd + L"\\gui_api_ai";
    }
    // T10: 壁纸库根目录（EnsureWallpaperLib 会创建目录并返回路径）
    m_wallpaperLibDir = EnsureWallpaperLib(m_configDir);
    LoadCfg();
    LoadHistory();
    LoadBranches();
}

void ApiAiNativePlugin::CallOnViewShow(HWND hostContainer, int w, int h) {
    m_host = hostContainer;
    SetHostContainer(hostContainer);
    m_width = std::max(760, w);
    m_height = std::max(560, h);
    m_viewVisible = true;

    if (!m_initialized) CallOnLoad();

    // 应用持久化背景（仅页面背景；配置未变化时内部跳过，避免强制快照重建）
    ApplySavedWallpaper();
    // v12: chat_output 跨重建保留 + 历史版本守卫——视图切换不再全量重渲染
    renderMain();
}

void ApiAiNativePlugin::CallOnViewHide() {
    m_viewVisible = false;
}

void ApiAiNativePlugin::CallOnResize(int w, int h) {
    int nw = std::max(760, w), nh = std::max(560, h);
    // v12: 尺寸未变化时跳过（拖动窗口会产生大量重复 WM_SIZE）
    if (m_viewVisible && nw == m_width && nh == m_height) return;
    m_width = nw;
    m_height = nh;
    if (m_viewVisible) renderMain();
}

void ApiAiNativePlugin::CallOnUnload() {
    // 先关闭设置窗口，回收 m_setControls 里的 HWND
    CloseSettingsWindow();
    // v12: 回收异步结果消息窗与画刷/字体
    if (m_asyncHwnd && IsWindow(m_asyncHwnd)) DestroyWindow(m_asyncHwnd);
    m_asyncHwnd = nullptr;
    for (auto& kv : m_setBrushes) if (kv.second) DeleteObject(kv.second);
    m_setBrushes.clear();
    if (m_fontSub)    DeleteObject(m_fontSub),    m_fontSub = nullptr;
    if (m_fontBig)    DeleteObject(m_fontBig),    m_fontBig = nullptr;
    if (m_fontTitle)  DeleteObject(m_fontTitle),  m_fontTitle = nullptr;
    m_wallpaperApplied = false;
    Unload();
    m_initialized = false;
    m_viewVisible = false;
}

// ============================================================================
// 事件分发（T6：主窗口基础交互；T7/T8 扩展其余控件）
// ============================================================================

void ApiAiNativePlugin::CallOnControlEvent(const std::wstring& controlId, int code) {
    // T5：主界面不再有 tab；壁纸/关于全部移入独立设置窗口。

    // v12: 富文本链接点击（宿主 EN_LINK → code=0x8000，tag 经 ConsumePendingLinkTag 取）
    if (code == 0x8000) {
        HandleRichLinkTag(ConsumePendingLinkTag());
        return;
    }

    // 对话页模式条
    if (controlId == L"mode_api")        { m_mode = 0; renderMain(); return; }
    if (controlId == L"mode_web")        { m_mode = 1; renderMain(); return; }

    // 加载更早消息（chat_load_more）
    if (controlId == L"chat_load_more")  {
        m_displayLimit += 20;
        renderMain();
        return;
    }

    // 右上角圆形设置按钮：独立入口 openSettings
    if (controlId == L"hdr_settings")    { OpenSettingsWindow(); return; }

    // ===== 发送消息 =====
    if (controlId == L"chat_send" || (controlId == L"chat_prompt" && code == 0x1000)) {
        // 0x1000 = 回车键（宿主 EDIT 控件映射）
        if (m_busy) return;
        HWND hPrompt = Ctl(L"chat_prompt");
        if (!hPrompt || !IsWindow(hPrompt)) return;
        int len = GetWindowTextLengthW(hPrompt);
        if (len < 1) return;
        std::wstring prompt(len + 1, L'\0');
        GetWindowTextW(hPrompt, &prompt[0], len + 1);
        prompt.resize(len);
        if (prompt.empty()) return;

        // 添加到历史（T9 再支持图片；当前纯文本）
        AppendUserMsg(prompt, {});
        m_pendingImageCount = 0;
        SetWindowTextW(hPrompt, L"");
        m_draftPrompt.clear();
        m_busy = true;
        m_status = L"";
        renderMain();

        // 构建并发送
        std::string payload = BuildMessages();
        if (payload.empty()) {
            m_status = L"构建请求失败";
            m_busy = false;
            renderMain();
            return;
        }
        StartSseRequest(payload);
        return;
    }

    // ===== 工具开关 =====
    if (controlId == L"input_tool_toggle") {
        m_toolsEnabled = !m_toolsEnabled;
        renderMain();
        return;
    }

    // ===== 网页模式 =====
    if (controlId == L"web_open") {
        if (!m_cfgWebUrl.empty())
            ShellExecuteW(NULL, L"open", m_cfgWebUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    if (controlId == L"web_fetch") {
        m_status = L"抓取页面功能开发中…";
        renderMain();
        return;
    }

    // 其余控件事件（设置页/对话框/消息操作等）—— T7/T8 填充
    (void)code;
}

// v12: 富文本链接点击动作分发（copy / edit / regen / branch / del / loadmore）
void ApiAiNativePlugin::HandleRichLinkTag(const std::wstring& tag) {
    if (tag.rfind(L"act:", 0) != 0) return;
    std::wstring rest = tag.substr(4);
    if (rest.empty()) return;
    size_t colon = rest.rfind(L':');
    std::wstring act = (colon == std::wstring::npos) ? rest : rest.substr(0, colon);
    int idx = -1;
    if (colon != std::wstring::npos && colon + 1 < rest.size())
        idx = _wtoi(rest.c_str() + colon + 1);

    if (act == L"loadmore") {
        m_displayLimit += 20;
        InvalidateHistory();
        if (m_viewVisible) renderMain();
        return;
    }
    if (act == L"copy") {
        DoCopyMessage(idx);
        m_status = L"已复制到剪贴板。";
        if (m_viewVisible) renderMain();
        return;
    }
    if (act == L"edit")    { DoEditMessage(idx); return; }
    if (act == L"regen")   { DoRegenerateMessage(idx); return; }
    if (act == L"branch")  {
        DoBranchMessage(idx);
        m_status = L"已创建分支。";
        if (m_viewVisible) renderMain();
        return;
    }
    if (act == L"del")     {
        DoDeleteMessage(idx);
        if (m_viewVisible) renderMain();
        return;
    }
}

// ============================================================================
// v12: 异步结果消息窗（后台线程 HTTP → UI 线程回调）
// ============================================================================
void ApiAiNativePlugin::EnsureAsyncWnd() {
    if (m_asyncHwnd && IsWindow(m_asyncHwnd)) return;
    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = AsyncWndProcStatic;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"ApiAiAsyncWnd";
        RegisterClassExW(&wc);
        s_reg = true;
    }
    m_asyncHwnd = CreateWindowExW(0, L"ApiAiAsyncWnd", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, GetModuleHandleW(NULL), this);
}

LRESULT CALLBACK ApiAiNativePlugin::AsyncWndProcStatic(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = (ApiAiNativePlugin*)(LONG_PTR)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (self) return self->AsyncWndProc(hWnd, msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT ApiAiNativePlugin::AsyncWndProc(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    if (msg == WM_API_HTTP_DONE) {
        uint32_t seq = (uint32_t)wParam;
        auto* r = (JSPlugin::HttpResult*)lParam;
        if (r) {
            JSPlugin::HttpResult copy = *r;
            delete r;
            CallHttpDone(seq, copy);
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void ApiAiNativePlugin::CallHttpDone(uint32_t seq, const JSPlugin::HttpResult& result) {
    // v12: 原生 pending 表按 seq 认领 HTTP 结果（保证在 UI 线程调用）。
    std::function<void(int, const std::string&, const std::string&)> cb;
    {
        std::lock_guard<std::mutex> lock(m_pendingHttpMtx);
        auto it = m_pendingHttp.find(seq);
        if (it == m_pendingHttp.end()) return;
        cb = std::move(it->second.cb);
        m_pendingHttp.erase(it);
    }
    if (cb) cb(result.status, result.body, result.error);
}

// ============================================================================
// 控件创建封装
// ============================================================================

HWND ApiAiNativePlugin::MakeLabel(const std::wstring& id, const std::wstring& text,
    int x, int y, int w, int h, int fg, HFONT font) {
    CtrlColor cc;
    cc.fg = fg;
    // 修复：显式设 bg，避免 STATIC 默认白底挡住背景色号/壁纸
    cc.bg = (int)HexToColor(m_bgColor.empty() ? L"#F4F7FB" : m_bgColor);
    HWND hw = CreateNativeControl("label", text, x, y, w, h, id);
    if (hw) {
        SetCtrlColor(hw, cc);
        HFONT hf = font ? font : FlatFont();
        if (hf) SendMessageW(hw, WM_SETFONT, (WPARAM)hf, TRUE);
    }
    return hw;
}

HWND ApiAiNativePlugin::MakePanel(const std::wstring& id, int x, int y, int w, int h,
    int fill, int edge) {
    CtrlColor cc;
    cc.bg = fill;
    cc.border = edge;
    HWND hw = CreateNativeControl("panel", L"", x, y, w, h, id);
    if (hw) SetCtrlColor(hw, cc);
    return hw;
}

void ApiAiNativePlugin::MakeRule(const std::wstring& id, int x, int y, int w, int color) {
    CtrlColor cc;
    cc.bg = color;
    cc.border = color;
    HWND hw = CreateNativeControl("panel", L"", x, y, w, 1, id);
    if (hw) SetCtrlColor(hw, cc);
}

HWND ApiAiNativePlugin::MakeButton(const std::wstring& id, const std::wstring& text,
    int x, int y, int w, int h, int kind, int fgOverride, int borderOverride) {
    CtrlColor cc;
    switch (kind) {
        case 1:  cc.fg = C_primaryFg; cc.border = C_primaryEdge; cc.tint = C_primaryBg; break;
        case 2:  cc.fg = C_activeFg;  cc.border = C_activeEdge;  cc.active = 1; break;
        case 3:  cc.fg = C_dangerFg;  cc.border = C_dangerEdge;  cc.tint = (int)ParseHex(L"#FEE2E2"); break;
        default: cc.fg = C_ghostFg;   cc.border = C_ghostEdge;   break;
    }
    // 色块指示（pal_*）：覆盖文字色与边框色，不整块刷背景。
    if (fgOverride != -1) cc.fg = fgOverride;
    if (borderOverride != -1) cc.border = borderOverride;
    HWND hw = CreateNativeControl("button", text, x, y, w, h, id);
    if (hw) {
        SetCtrlColor(hw, cc);
        // v12: 控件矩形比可见玻璃板外扩 5px，作为 hover 放大空间（防裁剪）
        SetGlassPad(hw, 5);
    }
    return hw;
}

// v12: 内陷色板（调色板）——实色色片嵌在凹陷插座里，选中描蓝环 + ✓
HWND ApiAiNativePlugin::MakeSwatch(const std::wstring& id, COLORREF color,
    int x, int y, int d, bool selected) {
    CtrlColor cc;
    cc.fg = (int)C_ink;
    cc.border = selected ? C_activeEdge : C_ghostEdge;
    cc.active = selected ? 1 : -1;
    cc.swatchColor = (int)color;
    HWND hw = CreateNativeControl("button", L"", x, y, d, d, id);
    if (hw) {
        SetCtrlColor(hw, cc);
        SetGlassPad(hw, 5);
    }
    return hw;
}

HWND ApiAiNativePlugin::MakeEdit(const std::wstring& id, const std::wstring& text,
    int x, int y, int w, int h, bool textarea, bool password) {
    CtrlColor cc;
    cc.bg = C_input;
    cc.fg = C_ink;
    cc.border = C_inputEdge;
    HWND hw = CreateNativeControl(textarea ? "textarea" : "edit",
        text, x, y, w, h, id);
    if (hw) {
        SetCtrlColor(hw, cc);
        // 密码框：给 EDIT 加 ES_PASSWORD（textarea 不加）
        if (password && !textarea) {
            // ES_PASSWORD 是创建期样式，运行时改用 EM_SETPASSWORDCHAR
            SendMessageW(hw, EM_SETPASSWORDCHAR, (WPARAM)L'*', 0);
            InvalidateRect(hw, NULL, TRUE);
        }
    }
    return hw;
}

HWND ApiAiNativePlugin::MakeRoundButton(const std::wstring& id,
    const std::wstring& text, int x, int y, int d, int kind,
    int fgOverride, int borderOverride) {
    CtrlColor cc;
    switch (kind) {
        case 1:  cc.fg = C_primaryFg; cc.border = C_primaryEdge; cc.tint = C_primaryBg; break;
        case 2:  cc.fg = C_activeFg;  cc.border = C_activeEdge;  cc.active = 1; break;
        case 3:  cc.fg = C_dangerFg;  cc.border = C_dangerEdge;  cc.tint = (int)ParseHex(L"#FEE2E2"); break;
        default: cc.fg = C_ghostFg;   cc.border = C_ghostEdge;   break;
    }
    if (fgOverride != -1) cc.fg = fgOverride;
    if (borderOverride != -1) cc.border = borderOverride;
    HWND hw = CreateNativeControl("roundbutton", text, x, y, d, d, id);
    if (hw) {
        SetCtrlColor(hw, cc);
        SetGlassPad(hw, 5);
    }
    return hw;
}

HWND ApiAiNativePlugin::MakeRichOut(const std::wstring& id, int x, int y, int w, int h) {
    CtrlColor cc;
    cc.bg = C_card;
    cc.fg = C_ink;
    cc.border = C_cardEdge;
    HWND hw = CreateNativeControl("richoutput", L"", x, y, w, h, id);
    if (hw) SetCtrlColor(hw, cc);
    return hw;
}

// ============================================================================
// 通用工具
// ============================================================================

COLORREF ApiAiNativePlugin::HexToColor(const std::wstring& hex) {
    return ParseHex(hex);
}

std::wstring ApiAiNativePlugin::Trunc(const std::wstring& s, int n) {
    if ((int)s.size() <= n) return s;
    return s.substr(0, n) + L"...";
}

std::wstring ApiAiNativePlugin::FileNameOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? path : path.substr(p + 1);
}

// ============================================================================
// 渲染：主窗口
// ============================================================================

void ApiAiNativePlugin::ClearChildControls(const std::wstring& keepId) {
    auto& map = MutableControls();
    std::wstring keepKey = keepId.empty() ? std::wstring() : (GetId() + L"::" + keepId);
    for (auto it = map.begin(); it != map.end(); ) {
        if (!keepKey.empty() && it->first == keepKey) { ++it; continue; }
        if (it->second && IsWindow(it->second)) {
            DestroyWindow(it->second);
        }
        it = map.erase(it);
    }
}

// v12: 富文本输出控件——存在则移位复用，不存在则创建
HWND ApiAiNativePlugin::EnsureRichOutput(int x, int y, int w, int h) {
    HWND hOut = Ctl(L"chat_output");
    if (hOut && IsWindow(hOut)) {
        SetWindowPos(hOut, HWND_TOP, x, y, w, h,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return hOut;
    }
    HWND nh = MakeRichOut(L"chat_output", x, y, w, h);
    if (nh) m_richVersion = -1;   // 新控件内容为空，必须重渲染
    return nh;
}

HWND ApiAiNativePlugin::Ctl(const std::wstring& id) {
    auto& map = MutableControls();
    auto it = map.find(GetId() + L"::" + id);
    return (it != map.end()) ? it->second : nullptr;
}

// 设置窗口控件查 HWND（查 m_setControls，key 无 pluginId 前缀）
HWND ApiAiNativePlugin::SetCtl(const std::wstring& id) {
    auto it = m_setControls.find(id);
    return (it != m_setControls.end()) ? it->second : nullptr;
}

void ApiAiNativePlugin::renderMain() {
    // 重建前捕获输入草稿（对齐 main.js captureDraft）
    {
        HWND hPrompt = Ctl(L"chat_prompt");
        if (hPrompt && IsWindow(hPrompt)) {
            int len = GetWindowTextLengthW(hPrompt);
            if (len > 0) {
                std::wstring buf(len + 1, L'\0');
                GetWindowTextW(hPrompt, &buf[0], len + 1);
                buf.resize(len);
                m_draftPrompt = buf;
            }
        }
    }

    // 全量重建（对齐 clearWindow('main') + 顺序渲染）；
    // v12: chat_output 跨重建保留，避免每次 renderMain 全量重渲染历史
    ClearChildControls(L"chat_output");

    int W = m_width, H = m_height;
    renderHeader(L"main", L"光语 AI", L"AI 请求 · 网页逆向");

    // T5：主界面只保留对话页（模式条 / 输入卡 / 富文本历史 / 消息操作）；
    // 壁纸 / 关于移入独立设置窗口，不再在此渲染 tab。
    (void)W; (void)H;
    renderChat();
}

void ApiAiNativePlugin::renderHeader(const std::wstring& windowId,
    const std::wstring& title, const std::wstring& subtitle) {
    int W = m_width;
    int hdrH = (windowId == L"main") ? 92 : 64;
    // 头部白卡 + 左侧蓝色强调条 + 标题/副标题 + 版本 + 圆形玻璃设置钮
    MakePanel(L"hdr", 0, 0, W, hdrH, C_card, C_cardEdge);
    MakePanel(L"hdr_accent", 22, 24, 5, 30, C_accent, C_accent);
    MakeLabel(L"hdr_title", title, 38, 14,
        std::min(300, W - 280), 28, (int)C_ink, TitleFont());
    MakeLabel(L"hdr_sub", subtitle, 38, 46,
        std::min(560, W - 280), 18, (int)C_ink3, SubFont());
    MakeLabel(L"hdr_ver", std::wstring(L"v") + VERSION_TEXT,
        std::max(20, W - 152), 20, 76, 20, (int)C_ink3, SubFont());
    if (windowId == L"main") {
        // 右上角圆形玻璃设置按钮
        MakeRoundButton(L"hdr_settings", L"gear", W - 58, 16, 44, 0);
        // 模式切换（玻璃按钮；选中态浅蓝高亮）
        MakeButton(L"mode_api", L"API", 22, 58, 64, 28,
            m_mode == 0 ? 2 : 0);
        MakeButton(L"mode_web", L"网页", 100, 58, 64, 28,
            m_mode == 1 ? 2 : 0);
    }
}

// ============================================================================
// 渲染：对话页
// ============================================================================

void ApiAiNativePlugin::renderChat() {
    int x = 20;
    int w = m_width - 40;
    int barY = 104;
    int barH = 46;

    // 状态条：白卡 + 状态点 + 文字（就绪绿 / 未配置灰 / 异常琥珀）
    MakePanel(L"chat_bar", x, barY, w, barH, C_card, C_cardEdge);

    std::wstring statusText;
    int statusColor = (int)C_ink3;
    bool warn = !m_status.empty();
    bool ready = false;
    if (warn) {
        statusText = m_status;
        statusColor = (int)C_warnFg;
    } else if (m_mode == 0) {
        ready = !m_cfgApiUrl.empty();
        statusText = ready
            ? L"就绪：" + Trunc(m_cfgApiUrl, 46)
            : L"未配置 API 地址";
    } else {
        ready = !m_cfgWebUrl.empty();
        statusText = ready
            ? L"就绪：" + Trunc(m_cfgWebUrl, 46)
            : L"未配置网页 URL";
    }
    if (!warn) statusColor = ready ? (int)C_readyFg : (int)C_ink3;
    statusText = (ready && !warn ? L"● " : L"● ") + statusText;
    MakeLabel(L"chat_status_text", statusText,
        x + 16, barY + 13, w - 32, 22, statusColor, SubFont());

    int contentY = barY + barH + 12;
    int contentH = m_height - contentY - 20;
    if (contentH < 200) contentH = 200;

    bool hasMessages = m_historyCount > 0 || m_busy;
    if (!hasMessages) renderChatEmpty(x, contentY, w, contentH);
    else              renderChatActive(x, contentY, w, contentH);
}

void ApiAiNativePlugin::renderChatEmpty(int x, int y, int w, int h) {
    // v12: 空状态隐藏富文本输出（控件与内容保留，切回活跃态免重渲染）
    HWND hOld = Ctl(L"chat_output");
    if (hOld && IsWindow(hOld)) ShowWindow(hOld, SW_HIDE);

    int cardW = std::min(720, w - 60);
    int cardX = x + (int)std::floor((double)(w - cardW) / 2);
    int cardH = 150 + (m_pendingImageCount > 0 ? 60 : 0);
    int blockH = 40 + 16 + cardH;
    int blockY = y + std::max(30, (int)std::floor((double)(h - blockH) / 2));

    MakeLabel(L"chat_greeting", L"你好，我能帮你什么？",
        cardX, blockY, cardW, 40, (int)C_ink, BigFont());

    int inputY = blockY + 56;
    if (m_pendingImageCount > 0) {
        // T8：待发送图片缩略图（renderPendingImages）
        inputY += 60;
    }
    renderInputCard(cardX, inputY, cardW, cardH);
}

void ApiAiNativePlugin::renderChatActive(int x, int y, int w, int h) {
    int inputH = 140 + (m_pendingImageCount > 0 ? 60 : 0);
    int msgH = h - inputH - 12;
    if (msgH < 120) msgH = 120;

    MakePanel(L"chat_box", x, y, w, msgH, C_card, C_cardEdge);

    bool hasMore = m_historyCount > m_displayLimit;
    int toolH = hasMore ? 34 : 0;
    if (hasMore) {
        MakeButton(L"chat_load_more",
            L"加载更早（还有 " + std::to_wstring(m_historyCount - m_displayLimit) + L" 条）",
            x + 12, y + 8, 260, 26, 0);
    }

    int outY = y + 10 + toolH;
    int outH = msgH - 20 - toolH;
    // v12: 富文本输出跨重建保留——仅当历史内容版本变化才全量重渲染
    HWND hRich = EnsureRichOutput(x + 12, outY, w - 24, outH);
    if (hRich && m_richVersion != m_histVersion) RenderHistoryToRich();

    int y2 = y + msgH + 12;
    if (m_pendingImageCount > 0) {
        y2 += 60;
    }
    renderInputCard(x, y2, w, 140);
}

void ApiAiNativePlugin::renderInputCard(int x, int y, int w, int h) {
    int pad = 14;
    int footerH = 46;

    MakePanel(L"input_card", x, y, w, h, C_input, C_inputEdge);

    int editX = x + pad;
    int editY = y + pad;
    int editW = w - pad * 2;
    int editH = h - pad - footerH - 4;
    if (editH < 30) editH = 30;
    MakeEdit(L"chat_prompt", m_draftPrompt,
        editX, editY, editW, editH, true, false);

    int footerY = y + h - footerH;
    MakeRule(L"input_divider", x + 12, footerY, w - 24, C_divider);

    // 左侧说明：API 模式显示模型 + 工具状态 + 图片数；网页模式显示登录态
    std::wstring footerLeft;
    if (m_mode == 0) {
        footerLeft = m_apiModel;
        if (m_toolsEnabled) {
            footerLeft += m_toolsRequireConfirm ? L" · 🔧 询问" : L" · 🔧 自动";
        }
        if (m_pendingImageCount > 0) {
            footerLeft += L" · " + std::to_wstring(m_pendingImageCount) + L" 图";
        }
    } else {
        footerLeft = m_webCookies ? L"网页逆向（已登录）" : L"网页逆向";
    }
    // 网页模式下左侧放 web_open/web_fetch，input_mode 右移避免重叠
    int inputModeX = (m_mode == 1) ? (x + 230) : (x + 16);
    int inputModeW = (m_mode == 1) ? (w - 470) : (w - 260);
    if (inputModeW < 60) inputModeW = 60;
    MakeLabel(L"input_mode", footerLeft,
        inputModeX, footerY + 14, inputModeW, 20, (int)C_ink3, SubFont());

    // v12: 玻璃控件矩形外扩 5px，按钮间距 ≥12px 防重叠
    MakeButton(L"input_tool_toggle",
        m_toolsEnabled ? L"工具：开" : L"工具：关",
        x + w - 212, footerY + 6, 96, 34,
        m_toolsEnabled ? 2 : 0);

    MakeButton(L"chat_send", m_busy ? L"接收中" : L"发送",
        x + w - 104, footerY + 6, 96, 34,
        m_busy ? 0 : 1);

    // 网页模式：把「打开网页 / 抓取页面」放进输入卡 footer，避免叠到消息框上
    if (m_mode == 1) {
        MakeButton(L"web_open", L"打开网页",
            x + 16, footerY + 6, 96, 34, 0);
        MakeButton(L"web_fetch", L"抓取页面",
            x + 124, footerY + 6, 96, 34, 0);
    }
}

// ============================================================================
// 渲染：壁纸页
// ============================================================================

void ApiAiNativePlugin::renderWall() {
    int x = 20, w = m_width - 40, y = 140;

    MakeLabel(L"wall_title", L"壁纸管理", x, y, 160, 24, (int)C_ink);
    MakeLabel(L"wall_sub", L"颜色 / 图片 / 轮播分开管理；选择后立即应用到页面背景",
        x + 88, y + 3, 460, 20, (int)C_ink3);

    int rowY = y + 40;
    MakeButton(L"wall_solid", L"纯色", x, rowY, 84, 34,
        m_bgMode == 0 ? 2 : 0);
    MakeButton(L"wall_image", L"图片", x + 92, rowY, 84, 34,
        m_bgMode == 1 ? 2 : 0);
    MakeButton(L"wall_cycle", L"轮播", x + 184, rowY, 84, 34,
        m_bgMode == 2 ? 2 : 0);
    MakeButton(L"wall_video", L"视频", x + 276, rowY, 84, 34,
        m_bgMode == 4 ? 2 : 0);
    MakeButton(L"wall_lively", L"动态", x + 368, rowY, 84, 34,
        m_bgMode == 5 ? 2 : 0);
    MakeButton(L"wall_refresh", L"刷新", x + w - 92, rowY, 92, 34, 0);
    MakeRule(L"wall_rule", x, rowY + 48, w, C_divider);

    int bodyY = rowY + 62;
    if (m_bgMode == 0) {
        MakeLabel(L"pal_title", L"快捷色 / 自定义", x, bodyY, 200, 20, (int)C_ink);
        for (int i = 0; i < QUICK_COLOR_COUNT; i++) {
            int bx = x + (i % 6) * 74;
            int by = bodyY + 30 + (int)std::floor((double)i / 6) * 44;
            bool active = (ParseHex(m_bgColor) == QUICK_COLORS[i]);
            // 色块指示：按钮保持玻璃样式（不整块刷色），
            // 用 "●" 色样圆点 + fg 色值标明待选色；选中项 √ + 蓝色高亮边框。
            MakeButton(L"pal_" + std::to_wstring(i),
                active ? L"√" : L"●",
                bx, by, 64, 34,
                active ? 2 : 0,
                active ? (int)C_primaryFg : (int)QUICK_COLORS[i],
                active ? (int)C_activeEdge : (int)C_ghostEdge);
        }
        MakeLabel(L"pal_current", L"当前颜色：" + m_bgColor,
            x, bodyY + 132, 260, 20, (int)C_ink2);
        MakeButton(L"wall_pick_color", L"打开调色盘…",
            x + 280, bodyY + 126, 140, 32, 1);
        MakeButton(L"wall_reset", L"恢复默认",
            x + 430, bodyY + 126, 100, 32, 0);
        MakeLabel(L"pal_hex_label", L"Hex", x + 540, bodyY + 132, 40, 20, (int)C_ink3);
        std::wstring hexDraft = m_bgColor;
        if (!hexDraft.empty() && hexDraft[0] == L'#') hexDraft.erase(0, 1);
        MakeEdit(L"wall_hex_input", hexDraft,
            x + 578, bodyY + 126, 100, 32, false, false);
        MakeButton(L"wall_hex_apply", L"应用",
            x + 686, bodyY + 126, 70, 32, 1);
    } else if (m_bgMode == 1) {
        MakeLabel(L"img_hint", L"在“已启用”列表选择图片，然后点击“设为当前”。",
            x, bodyY, w, 20, (int)C_ink2);
    } else {
        MakeLabel(L"cycle_title", L"轮播间隔（毫秒）", x, bodyY, 160, 20, (int)C_ink);
        std::wstring msText = m_cycleDraftMs > 0
            ? std::to_wstring(m_cycleDraftMs)
            : std::to_wstring(m_cycleMs);
        MakeEdit(L"wall_cycle_ms", msText,
            x + 166, bodyY - 2, 108, 32, false, false);
        MakeButton(L"wall_cycle_apply", L"应用", x + 284, bodyY - 2, 80, 32, 1);
        MakeLabel(L"cycle_hint", L"建议 500 ~ 86400000",
            x + 376, bodyY + 4, 240, 20, (int)C_ink3);
    }

    int listY = bodyY + (m_bgMode == 0 ? 170 : 62);
    int listH = std::max(180, m_height - listY - 40);
    int gap = 14;
    int actionW = 168;
    int listW = (int)std::floor((double)(w - actionW - gap * 2) / 2);
    int actionX = x + listW + gap;
    int rightX = x + listW + gap + actionW + gap;
    renderWallList(L"ct", L"未启用", x, listY, listW, listH);
    renderWallActions(actionX, listY, actionW, listH);
    renderWallList(L"af", L"已启用", rightX, listY, listW, listH);
}

void ApiAiNativePlugin::renderWallList(const std::wstring& kind,
    const std::wstring& title, int x, int y, int w, int h) {
    MakePanel(L"wl_" + kind, x, y, w, h, C_card, C_cardEdge);

    const std::vector<std::wstring>* arr =
        (kind == L"ct") ? &m_wallCt : &m_wallAf;
    int selected = (kind == L"ct") ? m_wallSelCt : m_wallSelAf;
    int pageIdx = (kind == L"ct") ? m_wallPageCt : m_wallPageAf;
    int start = pageIdx * WALL_PAGE_SIZE;

    MakeLabel(L"wl_" + kind + L"_title",
        title + L"  ·  " + std::to_wstring(arr->size()),
        x + 14, y + 12, w - 28, 20, (int)C_ink);

    // 缩略图网格（对齐原版：3 列 72x72，8px 间距）
    int cols = 3, boxW = 72, boxH = 72, gapX = 8, gapY = 8, padTop = 42;
    int cellW = boxW + gapX, cellH = boxH + 16 + gapY;
    int totalW = cols * boxW + (cols - 1) * gapX;
    int startX = x + (int)std::floor((double)(w - totalW) / 2);

    for (int i = 0; i < WALL_PAGE_SIZE; i++) {
        int idx = start + i;
        if (idx >= (int)arr->size()) break;
        int col = i % cols, rowI = (int)std::floor((double)i / cols);
        int cx = startX + col * cellW;
        int cy = y + padTop + rowI * cellH;
        bool isSel = (idx == selected);
        // T10：imagebox 缩略图 + SetImagePath 填充
        {
            std::wstring imgId = L"wl_" + kind + L"_img_" + std::to_wstring(i);
            CtrlColor ic;
            ic.bg = C_card;
            ic.border = isSel ? C_activeEdge : C_cardEdge;
            HWND hImg = CreateNativeControl("imagebox", L"",
                cx, cy, boxW, boxH, imgId);
            if (hImg) {
                SetCtrlColor(hImg, ic);
                SetImagePath(hImg, (*arr)[idx]);
            }
        }
        MakeLabel(L"wl_" + kind + L"_nm_" + std::to_wstring(i),
            Trunc((*arr)[idx], 12),
            cx - 2, cy + boxH + 2, boxW + 4, 14,
            isSel ? (int)C_activeFg : (int)C_ink3);
    }
    if (arr->empty()) {
        MakeLabel(L"empty_" + kind, L"暂无图片",
            x + 14, y + 60, w - 28, 20, (int)C_ink3);
    }

    int pages = std::max(1, (int)std::ceil((double)arr->size() / WALL_PAGE_SIZE));
    MakeButton(L"prev_" + kind, L"<", x + 12, y + h - 40, 36, 30, 0);
    MakeLabel(L"pg_" + kind, std::to_wstring(pageIdx + 1) + L"/" + std::to_wstring(pages),
        x + 56, y + h - 36, 56, 20, (int)C_ink3);
    MakeButton(L"next_" + kind, L">", x + 118, y + h - 40, 36, 30, 0);
}

void ApiAiNativePlugin::renderWallActions(int x, int y, int w, int h) {
    MakePanel(L"wall_actions_bg", x, y, w, h, C_card, C_cardEdge);
    MakeLabel(L"wall_actions", L"文件操作", x + 14, y + 12, w - 28, 20, (int)C_ink);
    MakeLabel(L"wall_action_hint", L"选择列表项后操作",
        x + 14, y + 34, w - 28, 18, (int)C_ink3);
    int bx = x + 14, bw = w - 28, by = y + 70;
    MakeButton(L"wall_enable",  L"启用  →", bx, by,        bw, 34, 1);
    MakeButton(L"wall_disable", L"←  停用", bx, by + 44,   bw, 34, 0);
    MakeButton(L"wall_set",     L"设为当前", bx, by + 88,  bw, 34, 1);
    MakeButton(L"wall_del_ct",  L"删除左侧", bx, by + 132, bw, 34, 3);
    MakeButton(L"wall_del_af",  L"删除右侧", bx, by + 176, bw, 34, 3);
}

// ============================================================================
// 渲染：关于页
// ============================================================================

void ApiAiNativePlugin::renderAbout() {
    int x = 20, w = m_width - 40, y = 150;
    MakePanel(L"about_card", x, y, w, 480, C_card, C_cardEdge);
    MakeLabel(L"about_title", L"光语 AI", x + 24, y + 20, 200, 28, (int)C_ink);
    MakeLabel(L"about_desc", L"独立 AI/API DLL · 高对比度自绘 UI · 真正独立弹窗",
        x + 24, y + 52, w - 48, 20, (int)C_ink2);
    MakeRule(L"about_rule", x + 24, y + 86, w - 48, C_divider);

    struct Row { const wchar_t* k; const wchar_t* v; };
    static const Row ROWS[] = {
        { L"界面",    L"按钮由 native 自绘；圆角 + 边框 + hover / 按下反馈。" },
        { L"输入",    L"无边框 textarea，嵌在白卡里；Enter 发送、Shift+Enter 换行。" },
        { L"流式",    L"SSE 逐字显示（WinHTTP 后台读，主线程回调）。" },
        { L"Markdown", L"RichEdit 富文本：标题 / 列表 / 引用 / 代码块 / 粗体 / 斜体 / 链接。" },
        { L"操作条",  L"每条消息下有 [复制 / 编辑 / 重生成 / 分叉 / 删除] 富文本链接。" },
        { L"工具",    L"Function Calling：10 个内置工具，最多 6 轮，可选需确认。" },
        { L"弹窗",    L"alert / confirm / inputBox 三件套（native 模态）。" },
        { L"分支",    L"从任意消息分叉；分支可切换 / 重命名 / 删除。" },
        { L"历史",    L"全量存 Cache\\gui_api_ai\\full_history.json；滚顶自动分页。" },
        { L"登录",    L"设置页可弹出 WebView2 覆盖层登录；Cookie 存 light_ai.m。" },
        { L"多模态",  L"拖放图片 → base64 → OpenAI / Claude 兼容 content。" },
        { L"调色",    L"纯色页支持系统调色盘（chooseColor）与 Hex 直接输入。" },
    };
    int yy = y + 108;
    int lx = x + 24, vx = x + 120, lw = 90, vw = w - 148;
    for (int i = 0; i < 12; i++) {
        MakeLabel(L"about_" + std::to_wstring(i) + L"a", ROWS[i].k,
            lx, yy, lw, 20, (int)C_ink3);
        MakeLabel(L"about_" + std::to_wstring(i) + L"b", ROWS[i].v,
            vx, yy, vw, 20, (int)C_ink2);
        yy += 30;
    }
}

// ============================================================================
// T6：模型预设（22 个，对齐原版 main.js MODEL_PRESETS）
// ============================================================================
const wchar_t* ApiAiNativePlugin::MODEL_PRESETS[22] = {
    L"gpt-4o-mini", L"gpt-4o", L"gpt-4-turbo", L"gpt-3.5-turbo",
    L"claude-3-5-sonnet-20241022", L"claude-3-5-haiku-20241022",
    L"claude-3-opus-20240229",
    L"deepseek-chat", L"deepseek-reasoner",
    L"qwen-turbo", L"qwen-plus", L"qwen-max",
    L"glm-4", L"glm-4-plus", L"glm-4-air",
    L"moonshot-v1-8k", L"moonshot-v1-32k", L"moonshot-v1-128k",
    L"gemini-1.5-pro", L"gemini-1.5-flash",
    L"yi-large", L"yi-medium"
};

// ============================================================================
// T6：设置窗口创建 / 销毁
// ============================================================================

static const wchar_t* kSettingsWndClass = L"ApiAiSettingsWndClass";

LRESULT CALLBACK ApiAiNativePlugin::SettingsWndProcStatic(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = (ApiAiNativePlugin*)(LONG_PTR)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (self) return self->SettingsWndProc(hWnd, msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void ApiAiNativePlugin::OpenSettingsWindow() {
    if (m_hSettings && IsWindow(m_hSettings)) {
        SetForegroundWindow(m_hSettings);
        return;
    }
    // 注册窗口类（仅一次）；背景置 NULL，全部由 WM_ERASEBKGND 自绘（现代样式）
    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SettingsWndProcStatic;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = kSettingsWndClass;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        s_reg = true;
    }

    HWND owner = GetOwnerWnd() ? GetOwnerWnd() : GetHostContainer();
    // v12: 尺寸对齐主窗口设置窗口（1000x700）
    int cw = 1000, ch = 700;
    RECT rc = { 0, 0, cw, ch };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        FALSE, 0);

    // 居中于 owner
    int px = CW_USEDEFAULT, py = CW_USEDEFAULT;
    if (owner && IsWindow(owner)) {
        RECT orc;
        GetWindowRect(owner, &orc);
        int ow = orc.right - orc.left, oh = orc.bottom - orc.top;
        px = orc.left + (ow - (rc.right - rc.left)) / 2;
        py = orc.top + (oh - (rc.bottom - rc.top)) / 2;
        if (px < 0) px = 0; if (py < 0) py = 0;
    }

    m_hSettings = CreateWindowExW(WS_EX_DLGMODALFRAME,
        kSettingsWndClass, L"光语 AI \u8bbe\u7f6e",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        px, py, rc.right - rc.left, rc.bottom - rc.top,
        owner, NULL, GetModuleHandle(NULL), this);

    if (m_hSettings) {
        ShowWindow(m_hSettings, SW_SHOW);
        UpdateWindow(m_hSettings);
    }
}

void ApiAiNativePlugin::CloseSettingsWindow() {
    if (m_hSettings && IsWindow(m_hSettings)) {
        DestroyWindow(m_hSettings);
    }
    m_hSettings = nullptr;
    m_setControls.clear();
    m_setCtrlType.clear();
    m_setCtrlColors.clear();
    m_setCtrlHover.clear();
    m_navHover = -1;
}

LRESULT ApiAiNativePlugin::SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
case WM_CREATE:
        m_hSettings = hWnd;   // WM_CREATE 期间成员还未赋值，这里先绑定
        m_settingsTab = (m_mode == 1) ? TAB_WEB : TAB_API;
        m_navHover = -1;
        DragAcceptFiles(hWnd, TRUE); // T10: 接收文件拖拽
        // WM_CREATE 时客户区尚未最终确定，延后到首次 WM_SIZE 再渲染
        m_settingsFirstRender = true;
        return 0;
    case WM_ERASEBKGND: {
        // v12: 现代设置窗口背景（深色导航栏 + 浅色内容区 + 白底栏）整幅自绘
        if (m_hSettings == hWnd) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            RenderSettingsBackground((HDC)wParam, rc);
            return 1;
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        // v12: 左侧导航栏 hit-test（悬停高亮）
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        int hit = SettingsNavHitTest(mx, my);
        if (hit != m_navHover) {
            m_navHover = hit;
            RECT rail = { 0, 0, SET_RAIL_W, 4096 };
            InvalidateRect(hWnd, &rail, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        // v12: 点击导航项切换 tab
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        int hit = SettingsNavHitTest(mx, my);
        if (hit >= 0 && hit < TAB_COUNT && hit != (int)m_settingsTab) {
            m_settingsTab = (SettingsTab)hit;
            RenderSettingsTab();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
        // v12: 设置窗口控件文字/背景色（内容区浅底 + 白底编辑框）
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        COLORREF bg = RGB(244, 247, 251);
        COLORREF fg = RGB(30, 41, 59);
        if (msg == WM_CTLCOLOREDIT) bg = RGB(255, 255, 255);
        auto cit = m_setCtrlColors.find(hCtrl);
        if (cit != m_setCtrlColors.end()) {
            if (cit->second.bg != -1) bg = (COLORREF)cit->second.bg;
            if (cit->second.fg != -1) fg = (COLORREF)cit->second.fg;
        }
        SetBkColor(hdc, bg);
        SetTextColor(hdc, fg);
        auto bit = m_setBrushes.find(bg);
        if (bit == m_setBrushes.end()) {
            HBRUSH nb = CreateSolidBrush(bg);
            m_setBrushes[bg] = nb;
            bit = m_setBrushes.find(bg);
        }
        return (LRESULT)bit->second;
    }
    case WM_SIZE: {
        int w = LOWORD(lParam), h = HIWORD(lParam);
        (void)h;
        // 首次 WM_SIZE 时客户区已确定，此时渲染 tab
        if (m_settingsFirstRender) {
            m_settingsFirstRender = false;
            RenderSettingsTab();
            return 0;
        }
        // 重绘当前 tab（简单实现：清掉所有设置控件，重新渲染）
        for (auto& kv : m_setControls) {
            if (kv.second && IsWindow(kv.second)) DestroyWindow(kv.second);
        }
        m_setControls.clear();
        m_setCtrlType.clear();
        m_setCtrlColors.clear();
        m_setCtrlHover.clear();
        RenderSettingsTab();
        return 0;
    }
    case WM_COMMAND: {
        int ctrlId = LOWORD(wParam);
        int code = HIWORD(wParam);
        // 反查控件 id
        std::wstring foundId;
        for (auto& kv : m_setControls) {
            if (kv.second && GetDlgCtrlID(kv.second) == ctrlId) {
                foundId = kv.first; break;
            }
        }
        if (!foundId.empty()) {
            OnSettingsControlEvent(foundId, code);
        }
        return 0;
    }
case WM_DROPFILES: {
        // T10: 拖拽文件落盘 → 统一导入
        HDROP hDrop = (HDROP)wParam;
        UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        std::vector<std::wstring> files;
        for (UINT i = 0; i < n; i++) {
            wchar_t path[MAX_PATH] = {};
            UINT len = DragQueryFileW(hDrop, i, path, MAX_PATH);
            if (len > 0 && len < MAX_PATH) files.push_back(path);
        }
        DragFinish(hDrop);
        if (!files.empty()) {
            ImportWallpaperFiles(files);
        }
        return 0;
    }
    case WM_CLOSE:
        CloseSettingsWindow();
        return 0;
    case WM_DESTROY:
        DragAcceptFiles(hWnd, FALSE);
        m_hSettings = nullptr;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================================
// T6：设置窗口控件创建（在 m_hSettings 上创建）
// ============================================================================

HWND ApiAiNativePlugin::MakeSetLabel(const std::wstring& id,
    const std::wstring& text, int x, int y, int w, int h, int fg, HFONT font) {
    if (!m_hSettings) return nullptr;
    CtrlColor cc; cc.fg = fg;
    int cid = m_nextSetCtrlId++;
    HWND hw = CreateWindowExW(0, L"STATIC", text.c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, m_hSettings, (HMENU)(INT_PTR)cid,
        GetModuleHandle(NULL), NULL);
    if (hw) {
        m_setControls[id] = hw;
        m_setCtrlType[hw] = "label";
        m_setCtrlColors[hw] = cc;
        HFONT hf = font ? font : FlatFont();
        if (hf) SendMessageW(hw, WM_SETFONT, (WPARAM)hf, TRUE);
    }
    return hw;
}

HWND ApiAiNativePlugin::MakeSetPanel(const std::wstring& id,
    int x, int y, int w, int h, int fill, int edge) {
    if (!m_hSettings) return nullptr;
    CtrlColor cc; cc.bg = fill; cc.border = edge;
    int cid = m_nextSetCtrlId++;
    HWND hw = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, m_hSettings, (HMENU)(INT_PTR)cid,
        GetModuleHandle(NULL), NULL);
    if (hw) {
        m_setControls[id] = hw;
        m_setCtrlType[hw] = "panel";
        m_setCtrlColors[hw] = cc;
        SetWindowSubclass(hw, PanelCardSubclassProc, 0, (DWORD_PTR)this);
    }
    return hw;
}

HWND ApiAiNativePlugin::MakeSetButton(const std::wstring& id,
    const std::wstring& text, int x, int y, int w, int h, int kind,
    int fgOverride, int borderOverride) {
    if (!m_hSettings) return nullptr;
    CtrlColor cc;
    switch (kind) {
        case 1: cc.fg = C_primaryFg; cc.border = C_primaryEdge; cc.tint = C_primaryBg; break;
        case 2: cc.fg = C_activeFg;  cc.border = C_activeEdge;  cc.active = 1; break;
        case 3: cc.fg = C_dangerFg;  cc.border = C_dangerEdge;  cc.tint = (int)ParseHex(L"#FEE2E2"); break;
        default: cc.fg = C_ghostFg;   cc.border = C_ghostEdge;   break;
    }
    if (fgOverride != -1) cc.fg = fgOverride;
    if (borderOverride != -1) cc.border = borderOverride;
    int cid = m_nextSetCtrlId++;
    HWND hw = CreateWindowExW(0, L"BUTTON", text.c_str(),
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x - 5, y - 5, w + 10, h + 10, m_hSettings, (HMENU)(INT_PTR)cid,
        GetModuleHandle(NULL), NULL);
    if (hw) {
        m_setControls[id] = hw;
        m_setCtrlType[hw] = "button";
        m_setCtrlColors[hw] = cc;
        SetWindowSubclass(hw, FlatButtonSubclassProc, 0, (DWORD_PTR)this);
        // v12: 视觉内边距 5px（控件矩形外扩，hover 放大不裁剪）
        SetGlassPad(hw, 5);
        HFONT hf = FlatFont();
        if (hf) SendMessageW(hw, WM_SETFONT, (WPARAM)hf, TRUE);
    }
    return hw;
}

// v12: 设置窗口内陷色板（与 MakeSwatch 同规范，挂在 m_hSettings 上）
HWND ApiAiNativePlugin::MakeSetSwatch(const std::wstring& id, COLORREF color,
    int x, int y, int d, bool selected) {
    if (!m_hSettings) return nullptr;
    CtrlColor cc;
    cc.fg = (int)C_ink;
    cc.border = selected ? C_activeEdge : C_ghostEdge;
    cc.active = selected ? 1 : -1;
    cc.swatchColor = (int)color;
    int cid = m_nextSetCtrlId++;
    HWND hw = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x - 5, y - 5, d + 10, d + 10, m_hSettings, (HMENU)(INT_PTR)cid,
        GetModuleHandle(NULL), NULL);
    if (hw) {
        m_setControls[id] = hw;
        m_setCtrlType[hw] = "button";
        m_setCtrlColors[hw] = cc;
        SetWindowSubclass(hw, FlatButtonSubclassProc, 0, (DWORD_PTR)this);
        SetGlassPad(hw, 5);
    }
    return hw;
}

HWND ApiAiNativePlugin::MakeSetEdit(const std::wstring& id,
    const std::wstring& text, int x, int y, int w, int h,
    bool textarea, bool password) {
    if (!m_hSettings) return nullptr;
    CtrlColor cc;
    cc.bg = C_input; cc.fg = C_ink; cc.border = C_inputEdge;
    const wchar_t* cls = textarea ? L"EDIT" : L"EDIT";
        DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
    if (textarea) style = WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
    if (password) style |= ES_PASSWORD;
    int cid = m_nextSetCtrlId++;
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, cls, text.c_str(),
        style, x, y, w, h, m_hSettings, (HMENU)(INT_PTR)cid,
        GetModuleHandle(NULL), NULL);
    if (hw) {
        m_setControls[id] = hw;
        m_setCtrlType[hw] = textarea ? "textarea" : "edit";
        m_setCtrlColors[hw] = cc;
        HFONT hf = FlatFont();
        if (hf) SendMessageW(hw, WM_SETFONT, (WPARAM)hf, TRUE);
    }
    return hw;
}

// ============================================================================
// T6：设置窗口 tab 渲染
// ============================================================================

void ApiAiNativePlugin::RenderSettingsTab() {
    if (!m_hSettings) return;
    // 清除旧控件
    for (auto& kv : m_setControls) {
        if (kv.second && IsWindow(kv.second)) DestroyWindow(kv.second);
    }
    m_setControls.clear();
    m_setCtrlType.clear();
    m_setCtrlColors.clear();
    m_setCtrlHover.clear();

    RECT rc;
    GetClientRect(m_hSettings, &rc);
    int W = rc.right - rc.left;
    int H = rc.bottom - rc.top;

    // v12: 内容区 = 导航栏(216) 右侧；标题区 24~80，内容 96 起，底栏 72px
    int x = SET_RAIL_W + 24;
    int w = W - x - 24;
    if (w < 400) w = 400;
    int y = 96;
    int bottom = H - 88;
    if (bottom < y + 200) bottom = y + 200;

    // 底栏：关闭（玻璃 ghost）
    MakeSetButton(L"set_close", L"关闭", W - 24 - 96, H - 56, 96, 36, 0);

    switch (m_settingsTab) {
        case TAB_API:    RenderSettingsApi(x, y, w, bottom);    break;
        case TAB_WEB:    RenderSettingsWeb(x, y, w, bottom);    break;
        case TAB_DISP:   RenderSettingsDisp(x, y, w, bottom);   break;
        case TAB_TOOLS:  RenderSettingsTools(x, y, w, bottom);  break;
        case TAB_BRANCH: RenderSettingsBranch(x, y, w, bottom); break;
        case TAB_ABOUT:  RenderSettingsAbout(x, y, w, bottom);  break;
    }
}

void ApiAiNativePlugin::RenderSettingsApi(int x, int y, int w, int bottom) {
    (void)bottom;
    int cardH = 336;
    MakeSetPanel(L"set_api_card", x, y, w, cardH, C_card, C_cardEdge);
    MakeSetLabel(L"set_api_label", L"API 地址", x + 20, y + 18, 180, 20, (int)C_ink, SubFont());
    MakeSetEdit(L"set_api_url", m_cfgApiUrl, x + 20, y + 44, w - 40, 34);

    MakeSetLabel(L"set_api_model_label", L"模型（可直接输入）",
        x + 20, y + 94, w - 40, 20, (int)C_ink, SubFont());
    MakeSetEdit(L"set_api_model", m_apiModel, x + 20, y + 120, w - 40, 34);

    MakeSetLabel(L"set_api_key_label", L"API Key", x + 20, y + 170, 120, 20, (int)C_ink, SubFont());
    MakeSetEdit(L"set_api_key", L"", x + 20, y + 196, w - 40, 34, false, true);

    std::wstring testLabel = m_settingsTestBusy ? L"测试中..." : L"测试连接";
    MakeSetButton(L"set_api_test", testLabel, x + 20, y + 256, 120, 38, 0);
    MakeSetButton(L"set_api_save", L"保存 API", x + w - 136, y + 256, 116, 38, 1);

    std::wstring status = m_status.empty()
        ? L"保存后配置写入 Cache\\gui\\light_ai.m"
        : m_status;
    int statusColor = m_status.empty() ? (int)C_ink3 : (int)C_warnFg;
    MakeSetLabel(L"set_api_status", status, x + 20, y + 308, w - 40, 24, statusColor, SubFont());
}

void ApiAiNativePlugin::RenderSettingsWeb(int x, int y, int w, int bottom) {
    (void)bottom;
    int cardH = 540;
    MakeSetPanel(L"set_web_card", x, y, w, cardH, C_card, C_cardEdge);
    MakeSetLabel(L"set_web_url_label", L"目标网页 URL",
        x + 20, y + 16, 220, 20, (int)C_ink, SubFont());
    MakeSetEdit(L"set_web_url", m_cfgWebUrl, x + 20, y + 40, w - 40, 34);

    MakeSetLabel(L"set_login_label", L"登录（覆盖层）",
        x + 20, y + 88, w - 40, 20, (int)C_ink, SubFont());
    MakeSetButton(L"set_web_login_open", L"打开登录覆盖层",
        x + 20, y + 112, 168, 36, 1);
    MakeSetButton(L"set_web_login_pull", L"拉取 Cookie",
        x + 200, y + 112, 140, 36, 0);

    std::wstring cookieState = m_webCookies
        ? L"已保存 Cookie"
        : L"尚未登录。点左侧按钮 → 覆盖层登录 → 回到本页点“拉取 Cookie”。";
    MakeSetLabel(L"set_login_state", cookieState, x + 20, y + 156, w - 40, 20, (int)C_ink3, SubFont());

    MakeSetLabel(L"set_selector_label", L"输入框选择器（参考）",
        x + 20, y + 188, 260, 20, (int)C_ink, SubFont());
    MakeSetEdit(L"set_selector", m_cfgWeb.selector, x + 20, y + 212, w - 40, 34);

    MakeSetLabel(L"set_method_label", L"请求方式",
        x + 20, y + 258, 120, 20, (int)C_ink, SubFont());
    MakeSetButton(L"set_post", L"POST", x + 20,  y + 282, 78, 34, m_cfgWeb.method == L"POST" ? 2 : 0);
    MakeSetButton(L"set_get",  L"GET",  x + 118, y + 282, 78, 34, m_cfgWeb.method == L"GET" ? 2 : 0);
    MakeSetButton(L"set_json", L"JSON", x + 216, y + 282, 78, 34, m_cfgWeb.ctype == L"json" ? 2 : 0);
    MakeSetButton(L"set_form", L"表单", x + 314, y + 282, 78, 34, m_cfgWeb.ctype == L"form" ? 2 : 0);

    MakeSetLabel(L"set_template_label", L"请求模板：{{prompt}}",
        x + 20, y + 330, 320, 20, (int)C_ink, SubFont());
    MakeSetEdit(L"set_template", m_cfgWeb.templateStr,
        x + 20, y + 354, w - 40, 90, true);

    MakeSetButton(L"set_web_save", L"保存网页", x + 20, y + 470, 116, 38, 1);
    MakeSetButton(L"set_web_default", L"恢复模板", x + 148, y + 470, 116, 38, 0);
    MakeSetButton(L"set_web_clear_cookie", L"清空 Cookie",
        x + 276, y + 470, 130, 38, 3);
}

void ApiAiNativePlugin::RenderSettingsDisp(int x, int y, int w, int bottom) {
    (void)bottom;
    int cardH = 480;
    MakeSetPanel(L"set_disp_card", x, y, w, cardH, C_card, C_cardEdge);
    MakeSetLabel(L"set_disp_mode", L"背景模式",
        x + 20, y + 16, 160, 20, (int)C_ink, SubFont());
    MakeSetButton(L"set_solid", L"纯色", x + 20,  y + 40, 84, 32, m_bgMode == 0 ? 2 : 0);
    MakeSetButton(L"set_image", L"图片", x + 118, y + 40, 84, 32, m_bgMode == 1 ? 2 : 0);
    MakeSetButton(L"set_cycle", L"轮播", x + 216, y + 40, 84, 32, m_bgMode == 2 ? 2 : 0);
    MakeSetButton(L"set_video", L"视频", x + 314, y + 40, 84, 32, m_bgMode == 4 ? 2 : 0);
    MakeSetButton(L"set_lively", L"动态", x + 412, y + 40, 84, 32, m_bgMode == 5 ? 2 : 0);

    MakeSetLabel(L"set_color_label", L"当前颜色",
        x + 20, y + 88, 120, 20, (int)C_ink, SubFont());
    MakeSetEdit(L"set_color_value", m_bgColor, x + 144, y + 86, 180, 28);
    MakeSetButton(L"set_color_apply", L"应用色号", x + 336, y + 86, 96, 30, 1);
    MakeSetButton(L"set_pick_color", L"调色盘", x + 442, y + 86, 96, 30, 0);

    // 12 个内陷色板（点击即应用）
    for (int i = 0; i < QUICK_COLOR_COUNT; i++) {
        int bx = x + 20 + (i % 6) * 64;
        int by = y + 128 + (i / 6) * 64;
        bool active = (ParseHex(m_bgColor) == QUICK_COLORS[i]);
        MakeSetSwatch(L"set_pal_" + std::to_wstring(i),
            QUICK_COLORS[i], bx, by, 52, active);
    }

    // T9/T10：image / video / lively 路径输入区（按当前模式显示）
    if (m_bgMode == 1) {
        MakeSetLabel(L"set_img_label", L"图片文件（png/jpg/gif/bmp/webp）",
            x + 20, y + 258, w - 40, 20, (int)C_ink, SubFont());
        MakeSetEdit(L"set_img_path", m_cfgDisp.bgImage,
            x + 20, y + 280, w - 150, 32);
        MakeSetButton(L"set_img_browse", L"选择图片…",
            x + w - 124, y + 280, 104, 32, 1);
    } else if (m_bgMode == 4) {
        MakeSetLabel(L"set_video_label", L"视频文件（mp4 / webm）",
            x + 20, y + 258, w - 40, 20, (int)C_ink, SubFont());
        MakeSetEdit(L"set_video_path", m_cfgDisp.videoPath,
            x + 20, y + 280, w - 150, 32);
        MakeSetButton(L"set_video_browse", L"选择视频…",
            x + w - 124, y + 280, 104, 32, 1);
    } else if (m_bgMode == 5) {
        MakeSetLabel(L"set_lively_label",
            L"Lively 目录（包含 project.json）",
            x + 20, y + 258, w - 40, 20, (int)C_ink, SubFont());
        MakeSetEdit(L"set_lively_dir", m_cfgDisp.livelyDir,
            x + 20, y + 280, w - 150, 32);
        MakeSetButton(L"set_lively_browse", L"选择目录…",
            x + w - 124, y + 280, 104, 32, 1);
    }

    // T9/T10：壁纸可用项列表（点击即应用）
    if (m_bgMode == 1) {
        ScanImageItems();
        MakeSetLabel(L"set_img_items_label",
            L"壁纸库图片（点击选用） · 共 " + std::to_wstring(m_imgItems.size()) + L" 个",
            x + 20, y + 324, w - 40, 18, (int)C_ink3, SubFont());
        for (int i = 0; i < (int)m_imgItems.size() && i < 4; i++) {
            MakeSetButton(L"set_img_item_" + std::to_wstring(i),
                Trunc(FileNameOf(m_imgItems[i]), 46),
                x + 20, y + 346 + i * 32, w - 40, 30,
                (m_cfgDisp.bgImage == m_imgItems[i]) ? 2 : 0);
        }
    } else if (m_bgMode == 4) {
        ScanVideoItems();
        MakeSetLabel(L"set_video_items_label",
            L"同目录视频（点击选用） · 共 " + std::to_wstring(m_videoItems.size()) + L" 个",
            x + 20, y + 324, w - 40, 18, (int)C_ink3, SubFont());
        for (int i = 0; i < (int)m_videoItems.size() && i < 4; i++) {
            MakeSetButton(L"set_video_item_" + std::to_wstring(i),
                Trunc(FileNameOf(m_videoItems[i]), 46),
                x + 20, y + 346 + i * 32, w - 40, 30,
                (m_cfgDisp.videoPath == m_videoItems[i]) ? 2 : 0);
        }
    } else if (m_bgMode == 5) {
        ScanLivelyItems();
        MakeSetLabel(L"set_lively_items_label",
            L"壁纸文件夹（点击选用） · 共 " + std::to_wstring(m_livelyItems.size()) + L" 个",
            x + 20, y + 324, w - 40, 18, (int)C_ink3, SubFont());
        for (int i = 0; i < (int)m_livelyItems.size() && i < 4; i++) {
            MakeSetButton(L"set_lively_item_" + std::to_wstring(i),
                Trunc(FileNameOf(m_livelyItems[i]), 46),
                x + 20, y + 346 + i * 32, w - 40, 30,
                (m_cfgDisp.livelyDir == m_livelyItems[i]) ? 2 : 0);
        }
    }

    MakeSetButton(L"set_disp_save", L"保存显示", x + 20, y + 492, 116, 38, 1);

    // T10：导入区（面板 + 提示 + 导入按钮；可拖拽落盘）
    MakeSetPanel(L"set_import_drop", x, y + 492 + 52, w, 96,
        C_card, C_activeEdge);
    MakeSetLabel(L"set_import_title", L"导入壁纸",
        x + 16, y + 492 + 62, 200, 20, (int)C_ink, SubFont());
    MakeSetLabel(L"set_import_hint",
        L"支持 zip / pkg（解压识别 project.json）、视频 mp4/webm/wmv、图片 png/jpg/gif/bmp/webp；也可以直接拖拽文件到这里。",
        x + 16, y + 492 + 86, w - 180, 40, (int)C_ink3, SubFont());
    MakeSetButton(L"set_import_btn", L"选择文件…",
        x + w - 150, y + 492 + 80, 130, 40, 1);
}

void ApiAiNativePlugin::RenderSettingsTools(int x, int y, int w, int bottom) {
    (void)bottom;
    int cardH = 510;
    MakeSetPanel(L"set_tools_card", x, y, w, cardH, C_card, C_cardEdge);
    MakeSetLabel(L"set_tools_title", L"工具调用（Function Calling）",
        x + 20, y + 16, w - 40, 22, (int)C_ink, BigFont());
    MakeSetLabel(L"set_tools_hint",
        L"开启后每次请求会附带 tools 定义；模型返回 tool_calls 时会自动执行并继续对话。",
        x + 20, y + 42, w - 40, 36, (int)C_ink3, SubFont());
    MakeSetPanel(L"set_tools_rule1", x + 20, y + 84, w - 40, 1, C_divider, C_divider);

    MakeSetLabel(L"set_tools_enable_label", L"总开关",
        x + 20, y + 98, 120, 20, (int)C_ink, SubFont());
    MakeSetButton(L"set_tool_on",  L"启用", x + 20,  y + 122, 84, 34,
        m_toolsEnabled ? 2 : 0);
    MakeSetButton(L"set_tool_off", L"关闭", x + 120, y + 122, 84, 34,
        m_toolsEnabled ? 0 : 2);

    MakeSetLabel(L"set_tools_confirm_label", L"执行前确认",
        x + 240, y + 98, 160, 20, (int)C_ink, SubFont());
    MakeSetButton(L"set_tool_confirm_on",  L"每次询问", x + 240, y + 122, 100, 34,
        m_toolsRequireConfirm ? 2 : 0);
    MakeSetButton(L"set_tool_confirm_off", L"自动执行", x + 356, y + 122, 100, 34,
        m_toolsRequireConfirm ? 0 : 2);

    MakeSetLabel(L"set_tool_max_label", L"最大轮数（1-12）",
        x + 20, y + 172, 180, 20, (int)C_ink, SubFont());
    MakeSetEdit(L"set_tool_max", std::to_wstring(m_cfgTools.maxRounds),
        x + 20, y + 196, 80, 34);

    MakeSetPanel(L"set_tools_rule2", x + 20, y + 246, w - 40, 1, C_divider, C_divider);
    MakeSetLabel(L"set_tools_list_title", L"内置工具清单",
        x + 20, y + 260, w - 40, 22, (int)C_ink, BigFont());
    MakeSetLabel(L"set_tools_list_hint", L"共 10 个；如模型支持可调用。",
        x + 20, y + 284, w - 40, 20, (int)C_ink3, SubFont());

    int ly = y + 310;
    static const wchar_t* TOOL_NAMES[] = {
        L"read_text_file", L"write_text_file", L"list_dir", L"fetch_url",
        L"move_file", L"delete_file", L"create_dir", L"remove_dir",
        L"run_command", L"get_clipboard"
    };
    for (int i = 0; i < 10; i++) {
        MakeSetLabel(L"set_tool_item_" + std::to_wstring(i),
            std::wstring(L"· ") + TOOL_NAMES[i],
            x + 28, ly, w - 56, 18, (int)C_ink2);
        ly += 20;
    }

    MakeSetButton(L"set_tools_save", L"保存工具设置",
        x + w - 156, y + 462, 136, 38, 1);
}

void ApiAiNativePlugin::RenderSettingsBranch(int x, int y, int w, int bottom) {
    (void)bottom;
    int cardH = 420;
    MakeSetPanel(L"set_branch_card", x, y, w, cardH, C_card, C_cardEdge);
    MakeSetLabel(L"set_branch_title", L"对话分支",
        x + 20, y + 16, 200, 22, (int)C_ink, BigFont());
    MakeSetLabel(L"set_branch_hint",
        L"从任意消息“分叉”会把当前对话保存为快照；切换分支会把当前对话也保存。",
        x + 20, y + 44, w - 40, 20, (int)C_ink3, SubFont());
    MakeSetPanel(L"set_branch_rule", x + 20, y + 76, w - 40, 1, C_divider, C_divider);

    if (m_branchCount == 0) {
        MakeSetLabel(L"set_branch_empty",
            L"还没有分支。在对话中点击“分叉”创建。",
            x + 20, y + 92, w - 40, 20, (int)C_ink3, SubFont());
    } else {
        MakeSetLabel(L"set_branch_stub",
            L"分支列表（T8 填充） · 共 " + std::to_wstring(m_branchCount) + L" 个",
            x + 20, y + 92, w - 40, 20, (int)C_ink3, SubFont());
    }

    MakeSetButton(L"set_branch_clear_all", L"清空所有分支",
        x + 20, y + 340, 140, 38, 3);
}

void ApiAiNativePlugin::RenderSettingsAbout(int x, int y, int w, int bottom) {
    (void)bottom;
    int cardH = 400;
    MakeSetPanel(L"set_about_card", x, y, w, cardH, C_card, C_cardEdge);
    MakeSetLabel(L"set_about_title", L"光语 AI",
        x + 24, y + 20, 200, 28, (int)C_ink, TitleFont());
    MakeSetLabel(L"set_about_desc",
        L"原生 C++ AI 助手（不依赖 .js）· 实心玻璃按钮 · 独立顶层设置窗口",
        x + 24, y + 54, w - 48, 20, (int)C_ink2, SubFont());
    MakeSetPanel(L"set_about_rule", x + 24, y + 86, w - 48, 1, C_divider, C_divider);

    struct Row { const wchar_t* k; const wchar_t* v; };
    static const Row ROWS[] = {
        { L"界面",   L"玻璃板按钮：中心薄、边缘厚；悬停时玻璃与文字一起放大上移。" },
        { L"输入",   L"无边框 textarea，嵌在白卡里；Enter 发送、Shift+Enter 换行。" },
        { L"流式",   L"SSE 逐字显示（WinHTTP 后台读，主线程增量渲染）。" },
        { L"Markdown", L"RichEdit 富文本：标题 / 列表 / 引用 / 代码块 / 粗体 / 斜体 / 链接。" },
        { L"工具",   L"Function Calling：10 个内置工具，最多 6 轮，可选需确认。" },
        { L"分支",   L"从任意消息分叉；分支可切换 / 重命名 / 删除。" },
        { L"壁纸",   L"纯色 / 图片 / 轮播 / 视频 / 动态五模式，支持调色盘与导入。" },
    };
    int yy = y + 106;
    int lx = x + 24, vx = x + 120, lw = 90, vw = w - 148;
    for (int i = 0; i < 7; i++) {
        MakeSetLabel(L"set_about_" + std::to_wstring(i) + L"a", ROWS[i].k,
            lx, yy, lw, 20, (int)C_ink3, SubFont());
        MakeSetLabel(L"set_about_" + std::to_wstring(i) + L"b", ROWS[i].v,
            vx, yy, vw, 20, (int)C_ink2, SubFont());
        yy += 30;
    }
}

// ============================================================================
// v12: 设置窗口现代背景（对齐主窗口设置窗口：深色导航栏 + 浅色内容区 + 白底栏）
// ============================================================================
void ApiAiNativePlugin::RenderSettingsBackground(HDC hdc, const RECT& rc) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    const int railW = SET_RAIL_W;
    int W = rc.right - rc.left, H = rc.bottom - rc.top;

    // 内容区底色
    Gdiplus::SolidBrush contentBg(Gdiplus::Color(255, 244, 247, 251));
    g.FillRectangle(&contentBg, 0, 0, W, H);

    // 左侧深色导航栏（渐变）
    Gdiplus::LinearGradientBrush rail(
        Gdiplus::Point(0, 0), Gdiplus::Point(0, H),
        Gdiplus::Color(255, GetRValue(C_railTop), GetGValue(C_railTop), GetBValue(C_railTop)),
        Gdiplus::Color(255, GetRValue(C_railBot), GetGValue(C_railBot), GetBValue(C_railBot)));
    g.FillRectangle(&rail, 0, 0, railW, H);

    Gdiplus::Font brandFont(L"Microsoft YaHei", 22, FontStyleBold, UnitPixel);
    Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush dim(Gdiplus::Color(255, 148, 163, 184));
    Gdiplus::StringFormat left;
    left.SetAlignment(Gdiplus::StringAlignmentNear);
    left.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"设置", -1, &brandFont,
        Gdiplus::RectF(22, 26, 170, 38), &left, &white);

    Gdiplus::Font subFont(L"Microsoft YaHei", 11, FontStyleRegular, UnitPixel);
    g.DrawString(L"Light Preferences", -1, &subFont,
        Gdiplus::RectF(24, 62, 170, 20), &left, &dim);

    // 导航项（hit-test 矩形同步写入 m_navRects）
    static const wchar_t* NAV_ICONS[TAB_COUNT] = {
        L"\U0001F517", L"\U0001F310", L"\U0001F5BC",
        L"\U0001F9F0", L"\U0001F33F", L"\u2139"
    };
    static const wchar_t* NAV_LABELS[TAB_COUNT] = {
        L"API", L"网页", L"显示", L"工具", L"分支", L"关于"
    };
    int navY = 108, navH = 46, navGap = 4;
    Gdiplus::Font iconFont(L"Segoe UI Emoji", 14, FontStyleRegular, UnitPixel);
    Gdiplus::Font labelFont(L"Microsoft YaHei", 13, FontStyleRegular, UnitPixel);
    Gdiplus::Font labelBold(L"Microsoft YaHei", 13, FontStyleBold, UnitPixel);
    for (int i = 0; i < TAB_COUNT; i++) {
        Gdiplus::RectF r(12.0f, (Gdiplus::REAL)navY, railW - 24, (Gdiplus::REAL)navH);
        m_navRects[i].left   = (LONG)r.X;
        m_navRects[i].top    = (LONG)r.Y;
        m_navRects[i].right  = (LONG)(r.X + r.Width);
        m_navRects[i].bottom = (LONG)(r.Y + r.Height);
        bool sel = ((int)m_settingsTab == i);
        bool hov = (m_navHover == i);
        if (sel || hov) {
            Gdiplus::GraphicsPath p;
            AddRoundRectPath(p, r.X, r.Y, r.Width, r.Height, 9.0f);
            Gdiplus::SolidBrush bg(sel
                ? Gdiplus::Color(255, GetRValue(C_selBlue), GetGValue(C_selBlue), GetBValue(C_selBlue))
                : Gdiplus::Color(36, 255, 255, 255));
            g.FillPath(&bg, &p);
        }
        Gdiplus::SolidBrush ib(sel ? Gdiplus::Color(255, 255, 255, 255) : (hov
            ? Gdiplus::Color(255, 226, 232, 240) : Gdiplus::Color(255, 148, 163, 184)));
        Gdiplus::SolidBrush tb(sel ? Gdiplus::Color(255, 255, 255, 255) : (hov
            ? Gdiplus::Color(255, 241, 245, 249) : Gdiplus::Color(255, 203, 213, 225)));
        Gdiplus::StringFormat cl;
        cl.SetAlignment(Gdiplus::StringAlignmentNear);
        cl.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        cl.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        g.DrawString(NAV_ICONS[i], -1, &iconFont,
            Gdiplus::RectF(r.X + 12, r.Y, 26, r.Height), &cl, &ib);
        g.DrawString(NAV_LABELS[i], -1, sel ? &labelBold : &labelFont,
            Gdiplus::RectF(r.X + 44, r.Y, r.Width - 52, r.Height), &cl, &tb);
        navY += navH + navGap;
    }

    // 栏底版本信息
    Gdiplus::Font verFont(L"Microsoft YaHei", 10, FontStyleRegular, UnitPixel);
    std::wstring verText = std::wstring(L"光语 AI · v") + VERSION_TEXT;
    g.DrawString(verText.c_str(), -1, &verFont,
        Gdiplus::RectF(22, (Gdiplus::REAL)(H - 34), 180, 20), &left, &dim);

    // 栏右分隔线
    Gdiplus::Pen railLine(Gdiplus::Color(255, 51, 65, 85), 1.0f);
    g.DrawLine(&railLine, (Gdiplus::REAL)railW, 0.0f,
        (Gdiplus::REAL)railW, (Gdiplus::REAL)H);

    // 内容页标题 + 副标题
    static const wchar_t* TITLES[TAB_COUNT] = {
        L"API 接口", L"网页逆向", L"显示与壁纸", L"工具调用", L"对话分支", L"关于"
    };
    static const wchar_t* SUBS[TAB_COUNT] = {
        L"配置 OpenAI 兼容接口地址、默认模型与 API Key。",
        L"配置目标网页与请求模板；登录覆盖层拉取 Cookie。",
        L"页面背景模式、快捷色板与壁纸库导入。",
        L"Function Calling 开关、确认策略与内置工具清单。",
        L"从任意消息分叉快照，管理全部分支。",
        L"版本信息与功能说明。"
    };
    int tab = (int)m_settingsTab;
    if (tab < 0 || tab >= TAB_COUNT) tab = 0;
    Gdiplus::Font pageFont(L"Microsoft YaHei", 20, FontStyleBold, UnitPixel);
    Gdiplus::SolidBrush ink(Gdiplus::Color(255, 30, 41, 59));
    g.DrawString(TITLES[tab], -1, &pageFont,
        Gdiplus::RectF((Gdiplus::REAL)(railW + 24), 24,
            (Gdiplus::REAL)(W - railW - 48), 32), &left, &ink);

    Gdiplus::Font pageSub(L"Microsoft YaHei", 12, FontStyleRegular, UnitPixel);
    Gdiplus::SolidBrush muted(Gdiplus::Color(255, 100, 116, 139));
    g.DrawString(SUBS[tab], -1, &pageSub,
        Gdiplus::RectF((Gdiplus::REAL)(railW + 24), 58,
            (Gdiplus::REAL)(W - railW - 48), 22), &left, &muted);

    // 底部操作区（白底 + 分隔线）
    Gdiplus::SolidBrush footer(Gdiplus::Color(255, 255, 255, 255));
    g.FillRectangle(&footer, (Gdiplus::REAL)railW,
        (Gdiplus::REAL)(H - 72), (Gdiplus::REAL)(W - railW), 72.0f);
    Gdiplus::Pen footerLine(Gdiplus::Color(255, 226, 232, 240), 1.0f);
    g.DrawLine(&footerLine, (Gdiplus::REAL)railW, (Gdiplus::REAL)(H - 72),
        (Gdiplus::REAL)W, (Gdiplus::REAL)(H - 72));
}

// v12: 导航栏 hit-test（命中返回 tab 索引，否则 -1）
int ApiAiNativePlugin::SettingsNavHitTest(int x, int y) const {
    if (x < 0 || x >= SET_RAIL_W || y < 0) return -1;
    for (int i = 0; i < TAB_COUNT; i++) {
        const RECT& r = m_navRects[i];
        if (r.right == 0 && r.bottom == 0) continue;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
    }
    return -1;
}

// v12: 设置窗口辅助字体（懒创建）
HFONT ApiAiNativePlugin::SubFont() {
    if (!m_fontSub) {
        LOGFONTW lf = { 0 };
        lf.lfHeight = -12;
        lf.lfWeight = FW_NORMAL;
        wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
        lf.lfQuality = CLEARTYPE_QUALITY;
        m_fontSub = CreateFontIndirectW(&lf);
    }
    return m_fontSub;
}
HFONT ApiAiNativePlugin::BigFont() {
    if (!m_fontBig) {
        LOGFONTW lf = { 0 };
        lf.lfHeight = -15;
        lf.lfWeight = FW_BOLD;
        wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
        lf.lfQuality = CLEARTYPE_QUALITY;
        m_fontBig = CreateFontIndirectW(&lf);
    }
    return m_fontBig;
}
HFONT ApiAiNativePlugin::TitleFont() {
    if (!m_fontTitle) {
        LOGFONTW lf = { 0 };
        lf.lfHeight = -20;
        lf.lfWeight = FW_BOLD;
        wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
        lf.lfQuality = CLEARTYPE_QUALITY;
        m_fontTitle = CreateFontIndirectW(&lf);
    }
    return m_fontTitle;
}

// ============================================================================
// T6：设置窗口事件处理
// ============================================================================

void ApiAiNativePlugin::OnSettingsControlEvent(const std::wstring& ctrlId, int code) {
    (void)code;
    // ---- tab 切换 ----
    if (ctrlId == L"set_tab_api")    { m_settingsTab = TAB_API;    RenderSettingsTab(); return; }
    if (ctrlId == L"set_tab_web")    { m_settingsTab = TAB_WEB;    RenderSettingsTab(); return; }
    if (ctrlId == L"set_tab_disp")   { m_settingsTab = TAB_DISP;   RenderSettingsTab(); return; }
    if (ctrlId == L"set_tab_tools")  { m_settingsTab = TAB_TOOLS;  RenderSettingsTab(); return; }
    if (ctrlId == L"set_tab_branch") { m_settingsTab = TAB_BRANCH; RenderSettingsTab(); return; }
    if (ctrlId == L"set_tab_about")  { m_settingsTab = TAB_ABOUT;  RenderSettingsTab(); return; }

    // ---- API tab ----
    if (ctrlId == L"set_api_test")   { TestApiConnection(); return; }
    if (ctrlId == L"set_api_save")   { SaveSettingsApi();   return; }

    // ---- Web tab ----
    if (ctrlId == L"set_web_save")   { SaveSettingsWeb();   return; }
    if (ctrlId == L"set_web_clear_cookie") { m_webCookies = false; RenderSettingsTab(); return; }

    // ---- 关闭（底栏） ----
    if (ctrlId == L"set_close")      { CloseSettingsWindow(); return; }

    // ---- Disp tab：模式切换即时应用 ----
    if (ctrlId == L"set_solid")      { m_bgMode = 0; m_cfgDisp.bgMode = L"solid";  WriteCfg(); ApplySavedWallpaper(); RenderSettingsTab(); return; }
    if (ctrlId == L"set_image")      { m_bgMode = 1; m_cfgDisp.bgMode = L"image";  WriteCfg(); ApplySavedWallpaper(); RenderSettingsTab(); return; }
    if (ctrlId == L"set_cycle")      { m_bgMode = 2; m_cfgDisp.bgMode = L"cycle";  WriteCfg(); ApplySavedWallpaper(); RenderSettingsTab(); return; }
    if (ctrlId == L"set_video")      { m_bgMode = 4; m_cfgDisp.bgMode = L"video";  WriteCfg(); ApplySavedWallpaper(); RenderSettingsTab(); return; }
    if (ctrlId == L"set_lively")     { m_bgMode = 5; m_cfgDisp.bgMode = L"lively"; WriteCfg(); ApplySavedWallpaper(); RenderSettingsTab(); return; }
    if (ctrlId == L"set_disp_save")  { SaveSettingsDisp(); return; }

    // ---- Disp tab：内陷色板点击即应用 ----
    if (ctrlId.rfind(L"set_pal_", 0) == 0) {
        int idx = _wtoi(ctrlId.c_str() + 8);
        if (idx >= 0 && idx < QUICK_COLOR_COUNT) {
            wchar_t buf[16];
            swprintf_s(buf, L"#%02X%02X%02X",
                GetRValue(QUICK_COLORS[idx]),
                GetGValue(QUICK_COLORS[idx]),
                GetBValue(QUICK_COLORS[idx]));
            m_bgColor = buf;
            m_cfgDisp.bgColor = m_bgColor;
            WriteCfg();
            ApplySavedWallpaper();
            m_status = L"背景颜色已应用：" + m_bgColor;
            RenderSettingsTab();
        }
        return;
    }
    // ---- Disp tab：Hex 输入应用 ----
    if (ctrlId == L"set_color_apply") {
        std::wstring v = SetCtlRead(L"set_color_value");
        std::wstring h = v;
        if (!h.empty() && h[0] == L'#') h.erase(0, 1);
        bool ok = (h.size() == 6);
        for (wchar_t c : h) {
            if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') ||
                  (c >= L'A' && c <= L'F'))) { ok = false; break; }
        }
        if (ok) {
            m_bgColor = L"#" + h;
            m_cfgDisp.bgColor = m_bgColor;
            WriteCfg();
            ApplySavedWallpaper();
            m_status = L"背景颜色已应用：" + m_bgColor;
        } else {
            m_status = L"色号格式不正确（应为 6 位十六进制，如 3B82F6）。";
        }
        RenderSettingsTab();
        return;
    }
    // ---- Disp tab：系统调色盘 ----
    if (ctrlId == L"set_pick_color") {
        static COLORREF s_custom[16] = {};
        CHOOSECOLORW cc = { sizeof(cc) };
        cc.hwndOwner = m_hSettings;
        cc.lpCustColors = s_custom;
        cc.rgbResult = ParseHex(m_bgColor);
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&cc)) {
            wchar_t buf[16];
            swprintf_s(buf, L"#%02X%02X%02X",
                GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
            m_bgColor = buf;
            m_cfgDisp.bgColor = m_bgColor;
            WriteCfg();
            ApplySavedWallpaper();
            m_status = L"背景颜色已应用：" + m_bgColor;
        }
        RenderSettingsTab();
        return;
    }
    if (ctrlId == L"set_video_browse")      { BrowseVideoFile(); RenderSettingsTab(); return; }
    if (ctrlId == L"set_lively_browse")     { BrowseLivelyDir(); RenderSettingsTab(); return; }
    if (ctrlId == L"set_img_browse")        { BrowseImageFile(); RenderSettingsTab(); return; }
    if (ctrlId.rfind(L"set_img_item_", 0) == 0) {
        int idx = _wtoi(ctrlId.c_str() + 13);
        if (idx >= 0 && idx < (int)m_imgItems.size()) {
            m_cfgDisp.bgImage = m_imgItems[idx];
            m_bgMode = 1;
            WriteCfg();
            ApplySavedWallpaper();
            m_status = L"\u5df2\u5e94\u7528\u56fe\u7247\u58c1\u7eb8\uff1a" + FileNameOf(m_cfgDisp.bgImage);
            RenderSettingsTab();
        }
        return;
    }
    if (ctrlId.rfind(L"set_video_item_", 0) == 0) {
        int idx = _wtoi(ctrlId.c_str() + 15); // "set_video_item_" 15 字符
        if (idx >= 0 && idx < (int)m_videoItems.size()) {
            m_cfgDisp.videoPath = m_videoItems[idx];
            m_bgMode = 4;
            WriteCfg();
            ApplySavedWallpaper();
            m_status = L"\u5df2\u5e94\u7528\u89c6\u9891\u58c1\u7eb8\uff1a" + FileNameOf(m_cfgDisp.videoPath);
            RenderSettingsTab();
        }
        return;
    }
    if (ctrlId.rfind(L"set_lively_item_", 0) == 0) {
        int idx = _wtoi(ctrlId.c_str() + 16); // "set_lively_item_" 16 字符
        if (idx >= 0 && idx < (int)m_livelyItems.size()) {
            m_cfgDisp.livelyDir = m_livelyItems[idx];
            m_bgMode = 5;
            WriteCfg();
            ApplySavedWallpaper();
            m_status = L"\u5df2\u5e94\u7528\u52a8\u6001\u58c1\u7eb8\uff1a" + FileNameOf(m_cfgDisp.livelyDir);
            RenderSettingsTab();
        }
        return;
    }
    if (ctrlId == L"set_import_btn")  { ChooseImportFiles(); return; }

    // ---- Tools tab ----
    if (ctrlId == L"set_tool_on")    { m_toolsEnabled = true;  RenderSettingsTab(); return; }
    if (ctrlId == L"set_tool_off")   { m_toolsEnabled = false; RenderSettingsTab(); return; }
    if (ctrlId == L"set_tool_confirm_on")  { m_toolsRequireConfirm = true;  RenderSettingsTab(); return; }
    if (ctrlId == L"set_tool_confirm_off") { m_toolsRequireConfirm = false; RenderSettingsTab(); return; }
    if (ctrlId == L"set_tools_save") { SaveSettingsTools(); return; }
}

void ApiAiNativePlugin::SaveSettingsApi() {
    HWND hUrl   = SetCtl(L"set_api_url");
    HWND hModel = SetCtl(L"set_api_model");
    HWND hKey   = SetCtl(L"set_api_key");

    auto readEdit = [](HWND h, std::wstring& out) {
        if (!h || !IsWindow(h)) return;
        int len = GetWindowTextLengthW(h);
        if (len <= 0) { out.clear(); return; }
        std::wstring buf(len + 1, L'\0');
        GetWindowTextW(h, &buf[0], len + 1);
        buf.resize(len);
        out = buf;
    };

    readEdit(hUrl,   m_cfgApi.url);
    readEdit(hModel, m_cfgApi.model);
    readEdit(hKey,   m_cfgApi.key);

    // 同步展示层缓存
    m_cfgApiUrl = m_cfgApi.url;
    m_apiModel  = m_cfgApi.model.empty() ? L"gpt-4o-mini" : m_cfgApi.model;
    if (m_cfgApi.model.empty()) m_cfgApi.model = m_apiModel;

    // 写盘
    WriteCfg();

    m_status = L"API 配置已保存。";
    RenderSettingsTab();
}

void ApiAiNativePlugin::SaveSettingsWeb() {
    HWND hUrl = SetCtl(L"set_web_url");
    if (hUrl && IsWindow(hUrl)) {
        int len = GetWindowTextLengthW(hUrl);
        if (len > 0) {
            std::wstring buf(len + 1, L'\0');
            GetWindowTextW(hUrl, &buf[0], len + 1);
            buf.resize(len);
            m_cfgWebUrl = buf;
        }
    }
    m_status = L"\u7f51\u9875\u914d\u7f6e\u5df2\u4fdd\u5b58\u3002";
    RenderSettingsTab();
}

// T9：按 m_bgMode 应用持久化背景（初始化 / 显示设置保存后调用）
void ApiAiNativePlugin::ApplySavedWallpaper() {
    // v12: 配置未变化时跳过——避免每次视图切换都 BumpBgVersion +
    //      触发玻璃按钮同步重建父容器背景快照（切换卡顿的主因之一）
    std::wstring key = std::to_wstring(m_bgMode) + L"|" + m_bgColor + L"|" +
        m_cfgDisp.bgImage + L"|" + m_cfgDisp.videoPath + L"|" +
        m_cfgDisp.livelyDir + L"|" + std::to_wstring(m_cycleMs);
    if (m_wallpaperApplied && key == m_wallKey) return;
    m_wallKey = key;
    m_wallpaperApplied = true;

    std::string colorHex = WToUtf8(m_bgColor.empty() ? L"#F4F7FB" : m_bgColor);
    switch (m_bgMode) {
        case 0: // solid
            ApplyWallpaper("solid", colorHex, L"", L"", m_cycleMs);
            break;
        case 1: // image
            ApplyWallpaper("image", colorHex, m_cfgDisp.bgImage, L"", m_cycleMs);
            break;
        case 2: // cycle
            ApplyWallpaper("cycle", colorHex, L"", m_cfgDisp.bgImage, m_cycleMs);
            break;
        case 4: // video
            ApplyWallpaper("video", colorHex, m_cfgDisp.videoPath, L"", m_cycleMs);
            break;
        case 5: // lively
            ApplyWallpaper("lively", colorHex, L"", m_cfgDisp.livelyDir, m_cycleMs);
            break;
        default:
            ApplyWallpaper("solid", colorHex, L"", L"", m_cycleMs);
            break;
    }
}

void ApiAiNativePlugin::SaveSettingsDisp() {
    // 读取当前 tab 上的 video / lively 路径输入（设置窗口控件表）
    auto setCtl = [&](const std::wstring& id) -> std::wstring {
        auto it = m_setControls.find(id);
        if (it == m_setControls.end() || !IsWindow(it->second)) return L"";
        int len = GetWindowTextLengthW(it->second);
        if (len < 1) return L"";
        std::wstring buf(len + 1, L'\0');
        GetWindowTextW(it->second, &buf[0], len + 1);
        buf.resize(len);
        return buf;
    };
    std::wstring vp = setCtl(L"set_video_path");
    if (!vp.empty()) m_cfgDisp.videoPath = vp;
    std::wstring ld = setCtl(L"set_lively_dir");
    if (!ld.empty()) m_cfgDisp.livelyDir = ld;
    m_cfgDisp.bgColor = m_bgColor;
    m_cfgDisp.cycleMs = m_cycleMs;
    WriteCfg();
    ApplySavedWallpaper();
    m_status = L"\u663e\u793a\u914d\u7f6e\u5df2\u4fdd\u5b58\uff0c\u80cc\u666f\u5df2\u5e94\u7528\u3002";
    RenderSettingsTab();
}

// T9：视频文件选择（mp4/webm/wmv），结果回填到 m_cfgDisp.videoPath
void ApiAiNativePlugin::BrowseVideoFile() {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hSettings;
    ofn.lpstrFilter = L"\u89c6\u9891\u6587\u4ef6\0*.mp4;*.webm;*.wmv\0\u6240\u6709\u6587\u4ef6\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"\u9009\u62e9\u89c6\u9891\u6587\u4ef6";
    if (GetOpenFileNameW(&ofn)) {
        m_cfgDisp.videoPath = path;
        if (m_bgMode != 4) m_bgMode = 4;
        WriteCfg();
        ApplySavedWallpaper();
        m_status = L"\u5df2\u9009\u62e9\u89c6\u9891\uff0c\u80cc\u666f\u5df2\u4e3a\u89c6\u9891\u6a21\u5f0f\u3002";
    }
}

// T9：Lively 壁纸目录选择（含 project.json），结果回填到 m_cfgDisp.livelyDir
void ApiAiNativePlugin::BrowseLivelyDir() {
    BROWSEINFOW bi = {};
    bi.hwndOwner = m_hSettings;
    bi.lpszTitle = L"\u9009\u62e9 Lively \u52a8\u6001\u58c1\u7eb8\u76ee\u5f55\uff08\u5305\u542b project.json\uff09";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH] = {};
        if (SHGetPathFromIDListW(pidl, path)) {
            m_cfgDisp.livelyDir = path;
            if (m_bgMode != 5) m_bgMode = 5;
            WriteCfg();
            ApplySavedWallpaper();
            m_status = L"\u5df2\u9009\u62e9\u52a8\u6001\u58c1\u7eb8\u76ee\u5f55\uff0c\u80cc\u666f\u5df2\u4e3a\u52a8\u6001\u6a21\u5f0f\u3002";
        }
        CoTaskMemFree(pidl);
    }
}

// T10：图片文件选择（png/jpg/gif/bmp/webp），结果回填 bgImage + mode=1
void ApiAiNativePlugin::BrowseImageFile() {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hSettings;
    ofn.lpstrFilter = L"\u56fe\u7247\u6587\u4ef6\0*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp\0\u6240\u6709\u6587\u4ef6\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"\u9009\u62e9\u56fe\u7247\u6587\u4ef6";
    if (GetOpenFileNameW(&ofn)) {
        m_cfgDisp.bgImage = path;
        if (m_bgMode != 1) m_bgMode = 1;
        WriteCfg();
        ApplySavedWallpaper();
        m_status = L"\u5df2\u9009\u62e9\u56fe\u7247\uff0c\u80cc\u666f\u5df2\u4e3a\u56fe\u7247\u6a21\u5f0f\u3002";
    }
}

// T9: 扫 videoPath 所在目录的视频文件（mp4/webm/wmv），填充 m_videoItems
void ApiAiNativePlugin::ScanVideoItems() {
    m_videoItems.clear();
    if (m_cfgDisp.videoPath.empty()) return;
    wchar_t dir[MAX_PATH] = {};
    wcsncpy_s(dir, MAX_PATH, m_cfgDisp.videoPath.c_str(), _TRUNCATE);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (!slash) slash = wcsrchr(dir, L'/');
    if (slash && slash != dir) *slash = L'\0';
    else wcscpy_s(dir, L".");
    static const wchar_t* EXTS[] = { L"*.mp4", L"*.webm", L"*.wmv" };
    std::set<std::wstring> seen;
    for (int e = 0; e < 3; e++) {
        std::wstring pat = std::wstring(dir) + L"\\" + EXTS[e];
        WIN32_FIND_DATAW fd = {};
        HANDLE hf = FindFirstFileW(pat.c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring full = std::wstring(dir) + L"\\" + fd.cFileName;
                if (seen.insert(full).second) m_videoItems.push_back(full);
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
}

// T9: 扫 livelyDir 下含 project.json 的子目录，填充 m_livelyItems
void ApiAiNativePlugin::ScanLivelyItems() {
    m_livelyItems.clear();
    if (m_cfgDisp.livelyDir.empty()) return;
    std::wstring pat = m_cfgDisp.livelyDir + L"\\*";
    WIN32_FIND_DATAW fd = {};
    HANDLE hf = FindFirstFileW(pat.c_str(), &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring sub = std::wstring(m_cfgDisp.livelyDir) + L"\\" + fd.cFileName;
            std::wstring pj = sub + L"\\project.json";
            DWORD attrs = GetFileAttributesW(pj.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                m_livelyItems.push_back(sub);
            }
        }
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

// T10: 扫壁纸库 images 目录的图片文件（png/jpg/gif/bmp/webp），填充 m_imgItems
void ApiAiNativePlugin::ScanImageItems() {
    m_imgItems.clear();
    if (m_wallpaperLibDir.empty()) return;
    std::wstring dir = m_wallpaperLibDir + L"\\images";
    static const wchar_t* EXTS[] = { L"*.png", L"*.jpg", L"*.jpeg", L"*.gif", L"*.bmp", L"*.webp" };
    std::set<std::wstring> seen;
    for (int e = 0; e < 6; e++) {
        std::wstring pat = dir + L"\\" + EXTS[e];
        WIN32_FIND_DATAW fd = {};
        HANDLE hf = FindFirstFileW(pat.c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring full = dir + L"\\" + fd.cFileName;
                if (seen.insert(full).second) m_imgItems.push_back(full);
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
}

// T10: 确保壁纸库目录存在（Cache\gui_api_ai\wallpapers），返回其路径
static std::wstring EnsureWallpaperLib(const std::wstring& configDir) {
    std::wstring lib = configDir + L"\\wallpapers";
    CreateDirectoryW(lib.c_str(), NULL);
    CreateDirectoryW((lib + L"\\videos").c_str(), NULL);
    CreateDirectoryW((lib + L"\\images").c_str(), NULL);
    return lib;
}

// T10: 解压 zip / pkg（WE .pkg 实为 zip）到 destDir，成功返回 true
static bool ExtractZip(const std::wstring& archive, const std::wstring& destDir) {
    CreateDirectoryW(destDir.c_str(), NULL);
    // pkg 与 zip 同为 zip 容器；Expand-Archive 按内容识别格式
    std::wstring ps = L"powershell -NoProfile -Command "
        L"\"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath '"
        + archive + L"' -DestinationPath '" + destDir
        + L"' -Force\"";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(NULL, &ps[0], NULL, NULL, FALSE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (ec == 0);
}

// T10: 扫壁纸库目录，填充 m_wallCt（视频/图片）与 m_wallAf（动态壁纸目录）
void ApiAiNativePlugin::ScanWallpaperLibrary() {
    m_wallCt.clear();
    m_wallAf.clear();
    if (m_wallpaperLibDir.empty()) return;

    WIN32_FIND_DATAW fd = {};
    HANDLE hf = FindFirstFileW((m_wallpaperLibDir + L"\\*").c_str(), &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = m_wallpaperLibDir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // 含 project.json 的目录 → 动态壁纸（已启用）
            DWORD attrs = GetFileAttributesW((full + L"\\project.json").c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                m_wallAf.push_back(full);
                if (m_livelyItems.empty() || std::find(m_livelyItems.begin(), m_livelyItems.end(), full) == m_livelyItems.end()) {
                    m_livelyItems.push_back(full);
                }
            }
        } else {
            std::wstring low = fd.cFileName;
            std::transform(low.begin(), low.end(), low.begin(), ::towlower);
            bool isVid = low.size() > 4 &&
                (low.substr(low.size() - 4) == L".mp4" ||
                 low.substr(low.size() - 5) == L".webm" ||
                 low.substr(low.size() - 4) == L".wmv");
            bool isImg = low.size() > 4 &&
                (low.substr(low.size() - 4) == L".png" ||
                 low.substr(low.size() - 4) == L".jpg" ||
                 low.substr(low.size() - 5) == L".jpeg" ||
                 low.substr(low.size() - 4) == L".gif" ||
                 low.substr(low.size() - 4) == L".bmp" ||
                 low.substr(low.size() - 5) == L".webp");
            if (isVid || isImg) m_wallCt.push_back(full);
        }
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

// T10: 统一导入入口（文件多选按钮 / 拖拽落盘共用）
void ApiAiNativePlugin::ImportWallpaperFiles(const std::vector<std::wstring>& files) {
    if (files.empty() || m_configDir.empty()) return;
    if (m_wallpaperLibDir.empty()) {
        m_wallpaperLibDir = EnsureWallpaperLib(m_configDir);
    }
    bool any = false;
    for (const std::wstring& f : files) {
        std::wstring low = f;
        std::transform(low.begin(), low.end(), low.begin(), ::towlower);
        std::wstring name = FileNameOf(f);
        bool isZip = low.size() > 4 && low.substr(low.size() - 4) == L".zip";
        bool isPkg = low.size() > 4 && low.substr(low.size() - 4) == L".pkg";
        bool isVid = low.size() > 4 &&
            (low.substr(low.size() - 4) == L".mp4" ||
             low.substr(low.size() - 5) == L".webm" ||
             low.substr(low.size() - 4) == L".wmv");
        bool isImg = low.size() > 4 &&
            (low.substr(low.size() - 4) == L".png" ||
             low.substr(low.size() - 4) == L".jpg" ||
             low.substr(low.size() - 5) == L".jpeg" ||
             low.substr(low.size() - 4) == L".gif" ||
             low.substr(low.size() - 4) == L".bmp" ||
             low.substr(low.size() - 5) == L".webp");

        if (isZip || isPkg) {
            // 解压后识别 project.json（可能嵌一层子目录）
            std::wstring base = name.substr(0, name.find_last_of(L'.'));
            if (base.empty()) base = L"wallpaper";
            std::wstring dest = m_wallpaperLibDir + L"\\" + base;
            if (ExtractZip(f, dest)) {
                // 识别 project.json（根目录或子目录一层）
                std::wstring proj = dest + L"\\project.json";
                DWORD a = GetFileAttributesW(proj.c_str());
                if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) {
                    WIN32_FIND_DATAW sd = {};
                    HANDLE hd = FindFirstFileW((dest + L"\\*").c_str(), &sd);
                    if (hd != INVALID_HANDLE_VALUE) {
                        do {
                            if (sd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
                                wcscmp(sd.cFileName, L".") != 0 && wcscmp(sd.cFileName, L"..") != 0) {
                                std::wstring p2 = dest + L"\\" + sd.cFileName + L"\\project.json";
                                DWORD a2 = GetFileAttributesW(p2.c_str());
                                if (a2 != INVALID_FILE_ATTRIBUTES && !(a2 & FILE_ATTRIBUTE_DIRECTORY)) {
                                    proj = p2; break;
                                }
                            }
                        } while (FindNextFileW(hd, &sd));
                        FindClose(hd);
                    }
                }
                if (GetFileAttributesW(proj.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    std::wstring projDir = proj.substr(0, proj.find_last_of(L'\\'));
                    if (std::find(m_livelyItems.begin(), m_livelyItems.end(), projDir) == m_livelyItems.end()) {
                        m_livelyItems.push_back(projDir);
                    }
                    if (std::find(m_wallAf.begin(), m_wallAf.end(), projDir) == m_wallAf.end()) {
                        m_wallAf.push_back(projDir);
                    }
                    any = true;
                } else {
                    // 无 project.json：归入静态资源（图片目录内找图）
                    m_wallCt.push_back(dest);
                    any = true;
                }
            }
        } else if (isVid) {
            std::wstring dst = m_wallpaperLibDir + L"\\videos\\" + name;
            CopyFileW(f.c_str(), dst.c_str(), FALSE);
            if (std::find(m_videoItems.begin(), m_videoItems.end(), dst) == m_videoItems.end()) {
                m_videoItems.push_back(dst);
            }
            m_cfgDisp.videoPath = dst;
            m_bgMode = 4;
            any = true;
        } else if (isImg) {
            std::wstring dst = m_wallpaperLibDir + L"\\images\\" + name;
            CopyFileW(f.c_str(), dst.c_str(), FALSE);
            if (std::find(m_wallCt.begin(), m_wallCt.end(), dst) == m_wallCt.end()) {
                m_wallCt.push_back(dst);
            }
            m_cfgDisp.bgImage = dst;
            m_bgMode = 1;
            any = true;
        }
    }
    if (any) {
        WriteCfg();
        ApplySavedWallpaper();
        m_status = L"\u5df2\u5bfc\u5165\u58c1\u7eb8\u5e76\u5e94\u7528\u5230\u9875\u9762\u80cc\u666f\u3002";
    } else {
        m_status = L"\u672a\u8bc6\u522b\u5230\u58c1\u7eb8\u6587\u4ef6\uff08zip / pkg / \u89c6\u9891 / \u56fe\u7247\uff09\u3002";
    }
    RenderSettingsTab();
}

// T10: 文件多选导入（zip/pkg/MP4/WEBM/WMV/图片）
void ApiAiNativePlugin::ChooseImportFiles() {
    wchar_t buf[16384] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hSettings;
    ofn.lpstrFilter = L"\u58c1\u7eb8\u6587\u4ef6\0*.zip;*.pkg;*.mp4;*.webm;*.wmv;*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp\0"
                      L"\u6240\u6709\u6587\u4ef6\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 16384;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"\u5bfc\u5165\u58c1\u7eb8\uff08\u53ef\u591a\u9009\uff09";
    if (!GetOpenFileNameW(&ofn)) return;
    std::vector<std::wstring> files;
    // 多选：第一个字符串是目录，后续是文件名；单选：第一个是完整路径
    std::wstring dir;
    {
        wchar_t* p = buf;
        dir = p;
        p += wcslen(p) + 1;
        if (*p) {
            while (*p) {
                files.push_back(dir + L"\\" + p);
                p += wcslen(p) + 1;
            }
        } else {
            files.push_back(dir);
        }
    }
    ImportWallpaperFiles(files);
}

std::wstring ApiAiNativePlugin::SetCtlRead(const std::wstring& id) {
    HWND h = SetCtl(id);
    if (!h || !IsWindow(h)) return L"";
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return L"";
    std::wstring buf(len + 1, L'\0');
    GetWindowTextW(h, &buf[0], len + 1);
    buf.resize(len);
    return buf;
}

    void ApiAiNativePlugin::SaveSettingsTools() {
        HWND hMax = SetCtl(L"set_tool_max");
    if (hMax && IsWindow(hMax)) {
        wchar_t buf[16] = {};
        GetWindowTextW(hMax, buf, 16);
        int mx = _wtoi(buf);
        if (mx < 1) mx = 1; if (mx > 12) mx = 12;
        (void)mx; // T8: store into cfg
    }
    m_status = L"\u5de5\u5177\u8bbe\u7f6e\u5df2\u4fdd\u5b58\u3002";
    RenderSettingsTab();
}

void ApiAiNativePlugin::TestApiConnection() {
    if (m_settingsTestBusy) return;
    if (m_cfgApi.url.empty()) {
        m_status = L"请先填写 API 地址再测试。";
        RenderSettingsTab();
        return;
    }
    m_settingsTestBusy = true;
    m_status = L"正在测试连接...";
    RenderSettingsTab();

    // 最小请求：1 条消息、关闭流式、限制 1 token
    // （此处手写 JSON：JsonVal 定义在文件更后方，避免前向声明不完整类型）
    auto JsonEsc = [](const std::wstring& in) -> std::wstring {
        std::wstring out;
        for (wchar_t c : in) {
            if (c == L'\\' || c == L'"') out += L'\\', out += c;
            else if (c == L'\n') out += L"\\n";
            else if (c == L'\r') out += L"\\r";
            else if (c == L'\t') out += L"\\t";
            else out += c;
        }
        return out;
    };
    std::wstring model = m_cfgApi.model.empty() ? L"gpt-4o-mini" : m_cfgApi.model;
    std::wstring wbody =
        L"{\"model\":\"" + JsonEsc(model) + L"\","
        L"\"messages\":[{\"role\":\"user\",\"content\":\"ping\"}],"
        L"\"max_tokens\":1,\"stream\":false}";
    std::string body = WToUtf8(wbody);

    EnsureAsyncWnd();
    HttpPost(m_cfgApi.url, body, m_cfgApi.key,
        [this](int status, const std::string& resp, const std::string& err) {
            m_settingsTestBusy = false;
            if (!err.empty() || status < 0) {
                m_status = L"连接失败：" + Utf8ToW(err.empty() ? "network error" : err);
            } else if (status == 200) {
                m_status = L"连接成功（HTTP 200），API 可用。";
            } else {
                std::wstring r = Utf8ToW(resp);
                if (r.size() > 80) r = r.substr(0, 80) + L"...";
                m_status = L"HTTP " + std::to_wstring(status) + L"：" + r;
            }
            if (m_hSettings && IsWindow(m_hSettings)) RenderSettingsTab();
        });
}

// ============================================================================
// T8 data layer implementation
// ============================================================================

std::wstring ApiAiNativePlugin::CacheDir() const {
    wchar_t exe[1024] = {0};
    GetModuleFileNameW(NULL, exe, 1024);
    std::wstring cd = std::wstring(exe);
    size_t pos = cd.find_last_of(L"\\/");
    if (pos != std::wstring::npos) cd = cd.substr(0, pos);
    cd += L"\\Cache";
    return cd;
}
std::wstring ApiAiNativePlugin::CfgPath() const  { return CacheDir() + L"\\gui\\light_ai.m"; }
std::wstring ApiAiNativePlugin::ChatPath() const { return CacheDir() + L"\\gui_api_ai\\full_history.json"; }
std::wstring ApiAiNativePlugin::BranchPath() const { return CacheDir() + L"\\gui_api_ai\\branches.json"; }
std::wstring ApiAiNativePlugin::GetCacheDirW() const { return CacheDir(); }

// ---- INI ----
static std::wstring ReadIniString(const std::wstring& path,
    const std::wstring& section, const std::wstring& key,
    const std::wstring& def) {
    wchar_t buf[4096] = {};
    GetPrivateProfileStringW(section.c_str(), key.c_str(), def.c_str(), buf, 4096, path.c_str());
    return std::wstring(buf);
}
static void WriteIniString(const std::wstring& path,
    const std::wstring& section, const std::wstring& key,
    const std::wstring& value) {
    WritePrivateProfileStringW(section.c_str(), key.c_str(), value.c_str(), path.c_str());
}

void ApiAiNativePlugin::LoadCfg() {
    std::wstring p = CfgPath();
    // 旧版配置迁移：api_ai.m -> light_ai.m
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring oldPath = CacheDir() + L"\\gui\\api_ai.m";
        if (GetFileAttributesW(oldPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            CopyFileW(oldPath.c_str(), p.c_str(), FALSE);
        }
    }
    m_cfgApi.url   = ReadIniString(p, L"api", L"url", L"");
    m_cfgApi.key   = ReadIniString(p, L"api", L"key", L"");
    m_cfgApi.model = ReadIniString(p, L"api", L"model", L"gpt-4o-mini");
    m_cfgWeb.url        = ReadIniString(p, L"web", L"url", L"");
    m_cfgWeb.selector   = ReadIniString(p, L"web", L"selector", L"");
    m_cfgWeb.method     = ReadIniString(p, L"web", L"method", L"POST");
    m_cfgWeb.ctype      = ReadIniString(p, L"web", L"ctype", L"json");
    m_cfgWeb.templateStr= ReadIniString(p, L"web", L"template", L"{\"prompt\":\"{{prompt}}\"}");
    m_cfgWeb.cookies    = ReadIniString(p, L"web", L"cookies", L"");
    m_cfgDisp.bgMode   = ReadIniString(p, L"disp", L"bgMode", L"solid");
    m_cfgDisp.bgColor  = ReadIniString(p, L"disp", L"bgColor", L"#F4F7FB");
    m_cfgDisp.bgImage  = ReadIniString(p, L"disp", L"bgImage", L"");
    m_cfgDisp.cycleMs  = _wtoi(ReadIniString(p, L"disp", L"cycleMs", L"3000").c_str());
    m_cfgDisp.videoPath= ReadIniString(p, L"disp", L"videoPath", L"");
    m_cfgDisp.livelyDir= ReadIniString(p, L"disp", L"livelyDir", L"");
    if (m_cfgDisp.cycleMs < 100) m_cfgDisp.cycleMs = 3000;
    m_cfgTools.enabled        = ReadIniString(p, L"tools", L"enabled", L"1") == L"1";
    m_cfgTools.maxRounds      = _wtoi(ReadIniString(p, L"tools", L"maxRounds", L"6").c_str());
    m_cfgTools.requireConfirm = ReadIniString(p, L"tools", L"requireConfirm", L"1") == L"1";
    m_cfgApiUrl = m_cfgApi.url;
    m_cfgWebUrl = m_cfgWeb.url;
    m_apiModel  = m_cfgApi.model;
    if (m_cfgDisp.bgMode == L"image") m_bgMode = 1;
    else if (m_cfgDisp.bgMode == L"cycle") m_bgMode = 2;
    else if (m_cfgDisp.bgMode == L"video") m_bgMode = 4;
    else if (m_cfgDisp.bgMode == L"lively") m_bgMode = 5;
    else m_bgMode = 0;
    m_bgColor   = m_cfgDisp.bgColor;
    m_cycleMs   = m_cfgDisp.cycleMs;
    m_toolsEnabled       = m_cfgTools.enabled;
    m_toolsRequireConfirm= m_cfgTools.requireConfirm;
}

void ApiAiNativePlugin::WriteCfg() {
    std::wstring p = CfgPath();
    {
        std::wstring dir = p.substr(0, p.find_last_of(L"\\/"));
        if (!dir.empty()) {
            wchar_t tmp[1024];
            wcscpy_s(tmp, dir.c_str());
            SHCreateDirectoryExW(NULL, tmp, NULL);
        }
    }
    WritePrivateProfileSectionW(L"api", L"", p.c_str());
    WriteIniString(p, L"api", L"url", m_cfgApi.url);
    WriteIniString(p, L"api", L"key", m_cfgApi.key);
    WriteIniString(p, L"api", L"model", m_cfgApi.model);
    WritePrivateProfileSectionW(L"web", L"", p.c_str());
    WriteIniString(p, L"web", L"url", m_cfgWeb.url);
    WriteIniString(p, L"web", L"selector", m_cfgWeb.selector);
    WriteIniString(p, L"web", L"method", m_cfgWeb.method);
    WriteIniString(p, L"web", L"ctype", m_cfgWeb.ctype);
    WriteIniString(p, L"web", L"template", m_cfgWeb.templateStr);
    WriteIniString(p, L"web", L"cookies", m_cfgWeb.cookies);
WritePrivateProfileSectionW(L"disp", L"", p.c_str());
    WriteIniString(p, L"disp", L"bgMode", m_cfgDisp.bgMode);
    WriteIniString(p, L"disp", L"bgColor", m_cfgDisp.bgColor);
    WriteIniString(p, L"disp", L"bgImage", m_cfgDisp.bgImage);
    WriteIniString(p, L"disp", L"cycleMs", std::to_wstring(m_cfgDisp.cycleMs));
    WriteIniString(p, L"disp", L"videoPath", m_cfgDisp.videoPath);
    WriteIniString(p, L"disp", L"livelyDir", m_cfgDisp.livelyDir);
    WritePrivateProfileSectionW(L"tools", L"", p.c_str());
    WriteIniString(p, L"tools", L"enabled", m_cfgTools.enabled ? L"1" : L"0");
    WriteIniString(p, L"tools", L"maxRounds", std::to_wstring(m_cfgTools.maxRounds));
    WriteIniString(p, L"tools", L"requireConfirm", m_cfgTools.requireConfirm ? L"1" : L"0");
}

// ---- mini JSON ----
struct ApiAiNativePlugin::JsonVal {
    enum Type { NULL_, BOOL, NUMBER, STRING, ARRAY, OBJECT } type = NULL_;
    bool b = false;
    double n = 0;
    std::wstring s;
    std::vector<JsonVal> a;
    std::map<std::wstring, JsonVal> o;
    JsonVal() {}
    JsonVal(bool v) : type(BOOL), b(v) {}
    JsonVal(double v) : type(NUMBER), n(v) {}
    JsonVal(const std::wstring& v) : type(STRING), s(v) {}
    JsonVal(const std::vector<JsonVal>& v) : type(ARRAY), a(v) {}
    JsonVal(const std::map<std::wstring, JsonVal>& v) : type(OBJECT), o(v) {}
    bool Has(const std::wstring& k) const { return o.find(k) != o.end(); }
    const JsonVal& Get(const std::wstring& k) const {
        static JsonVal empty;
        auto it = o.find(k); return it != o.end() ? it->second : empty;
    }
};

static std::wstring Utf8ToW(const std::string& u) {
    if (u.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), &w[0], n);
    return w;
}
static std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string u(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &u[0], n, NULL, NULL);
    return u;
}

static void SkipSpace(const char*& p) { while (*p && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r')) ++p; }

static ApiAiNativePlugin::JsonVal ParseJsonVal(const char*& p) {
    SkipSpace(p);
    if (*p == 'n') { p += 4; return ApiAiNativePlugin::JsonVal(); }
    if (*p == 't') { p += 4; return ApiAiNativePlugin::JsonVal(true); }
    if (*p == 'f') { p += 5; return ApiAiNativePlugin::JsonVal(false); }
    if (*p == '"') {
        ++p;
        std::string s;
        while (*p && *p != '"') {
            if (*p == '\\' && *(p+1)) {
                ++p;
                switch (*p) {
                    case 'n': s += '\n'; break;
                    case 'r': s += '\r'; break;
                    case 't': s += '\t'; break;
                    case '\\': s += '\\'; break;
                    case '"': s += '"'; break;
                    default: s += *p; break;
                }
                ++p;
            } else { s += *p++; }
        }
        if (*p == '"') ++p;
        return ApiAiNativePlugin::JsonVal(Utf8ToW(s));
    }
    if (*p == '[') {
        ++p;
        std::vector<ApiAiNativePlugin::JsonVal> arr;
        SkipSpace(p);
        if (*p == ']') { ++p; return ApiAiNativePlugin::JsonVal(arr); }
        while (*p) {
            arr.push_back(ParseJsonVal(p));
            SkipSpace(p);
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; break; }
            ++p;
        }
        return ApiAiNativePlugin::JsonVal(arr);
    }
    if (*p == '{') {
        ++p;
        std::map<std::wstring, ApiAiNativePlugin::JsonVal> obj;
        SkipSpace(p);
        if (*p == '}') { ++p; return ApiAiNativePlugin::JsonVal(obj); }
        while (*p) {
            auto key = ParseJsonVal(p);
            SkipSpace(p);
            if (*p == ':') ++p;
            auto val = ParseJsonVal(p);
            obj[key.s] = val;
            SkipSpace(p);
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; break; }
            ++p;
        }
        return ApiAiNativePlugin::JsonVal(obj);
    }
    const char* start = p;
    if (*p == '-') ++p;
    while (*p >= '0' && *p <= '9') ++p;
    if (*p == '.') { ++p; while (*p >= '0' && *p <= '9') ++p; }
    // 科学计数法：e / E [+/-] digits
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-') ++p;
        while (*p >= '0' && *p <= '9') ++p;
    }
    std::string num(start, p - start);
    try {
        return ApiAiNativePlugin::JsonVal(std::stod(num));
    } catch (...) {
        return ApiAiNativePlugin::JsonVal(0.0);
    }
}

ApiAiNativePlugin::JsonVal ApiAiNativePlugin::ParseJson(const std::string& s) {
    const char* p = s.c_str();
    return ParseJsonVal(p);
}

static void JsonValToStringImpl(const ApiAiNativePlugin::JsonVal& v, std::string& out, int indent);

static void JsonValToStringImpl(const ApiAiNativePlugin::JsonVal& v, std::string& out, int indent) {
    switch (v.type) {
        case ApiAiNativePlugin::JsonVal::NULL_: out += "null"; break;
        case ApiAiNativePlugin::JsonVal::BOOL: out += v.b ? "true" : "false"; break;
        case ApiAiNativePlugin::JsonVal::NUMBER: out += std::to_string(v.n); break;
        case ApiAiNativePlugin::JsonVal::STRING: {
            out += '"';
            for (wchar_t c : v.s) {
                // 逐字符转义：覆盖 JSON 规范要求的控制字符与特殊字符
                switch (c) {
                    case L'"':  out += "\""; break;
                    case L'\\': out += "\\"; break;
                    case L'\b': out += "\b"; break;
                    case L'\f': out += "\f"; break;
                    case L'\n': out += "\n"; break;
                    case L'\r': out += "\r"; break;
                    case L'\t': out += "\t"; break;
                    default:
                        if (c < 0x20) {
                            // 其他控制字符用 \u00XX
                            char tmp[8];
                            sprintf_s(tmp, "\\u%04X", (unsigned)c);
                            out += tmp;
                        } else {
                            char buf[8];
                            int n = WideCharToMultiByte(CP_UTF8, 0, &c, 1,
                                buf, 8, NULL, NULL);
                            out.append(buf, n);
                        }
                        break;
                }
            }
            out += '"';
            break;
        }
        case ApiAiNativePlugin::JsonVal::ARRAY: {
            out += '[';
            for (size_t i = 0; i < v.a.size(); ++i) {
                if (i) out += ',';
                JsonValToStringImpl(v.a[i], out, indent);
            }
            out += ']';
            break;
        }
        case ApiAiNativePlugin::JsonVal::OBJECT: {
            out += '{';
            bool first = true;
            for (auto& kv : v.o) {
                if (!first) out += ',';
                first = false;
                out += '"';
                out += WToUtf8(kv.first);
                out += "\":";
                JsonValToStringImpl(kv.second, out, indent);
            }
            out += '}';
            break;
        }
    }
}

std::string ApiAiNativePlugin::JsonToString(const JsonVal& v, bool pretty) {
    (void)pretty;
    std::string out;
    JsonValToStringImpl(v, out, 0);
    return out;
}

// ---- History ----
void ApiAiNativePlugin::LoadHistory() {
    std::wstring p = ChatPath();
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) return;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    auto root = ParseJson(data);
    if (root.type != JsonVal::ARRAY) return;
    m_history.clear();
    for (auto& item : root.a) {
        HistMsg m;
        m.id   = item.Get(L"id").s;
        m.role = item.Get(L"role").s;
        m.text = item.Get(L"text").s;
        auto imgArr = item.Get(L"images");
        if (imgArr.type == JsonVal::ARRAY)
            for (auto& im : imgArr.a) m.images.push_back(im.s);
        m.toolCallId = item.Get(L"toolCallId").s;
        m.toolName   = item.Get(L"toolName").s;
        m.toolArgs   = item.Get(L"toolArgs").s;
        m.toolResult = item.Get(L"toolResult").s;
        m_history.push_back(std::move(m));
    }
    m_historyCount = (int)m_history.size();
    InvalidateHistory();   // v12: 历史内容变化，rich 需要重渲染
}

void ApiAiNativePlugin::SaveHistory() {
    std::wstring p = ChatPath();
    {
        std::wstring dir = p.substr(0, p.find_last_of(L"\\/"));
        if (!dir.empty()) {
            wchar_t tmp[1024];
            wcscpy_s(tmp, dir.c_str());
            SHCreateDirectoryExW(NULL, tmp, NULL);
        }
    }
    std::vector<JsonVal> arr;
    for (auto& m : m_history) {
        std::map<std::wstring, JsonVal> obj;
        obj[L"id"] = JsonVal(m.id);
        obj[L"role"] = JsonVal(m.role);
        obj[L"text"] = JsonVal(m.text);
        std::vector<JsonVal> imgs;
        for (auto& im : m.images) imgs.push_back(JsonVal(im));
        obj[L"images"] = JsonVal(imgs);
        obj[L"toolCallId"] = JsonVal(m.toolCallId);
        obj[L"toolName"] = JsonVal(m.toolName);
        obj[L"toolArgs"] = JsonVal(m.toolArgs);
        obj[L"toolResult"] = JsonVal(m.toolResult);
        arr.push_back(JsonVal(obj));
    }
    std::string s = JsonToString(JsonVal(arr), false);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (f.is_open()) { f.write(s.data(), (std::streamsize)s.size()); f.close(); }
}

void ApiAiNativePlugin::ClearHistoryUI() {
    m_history.clear();
    m_historyCount = 0;
    m_displayLimit = 20;
    SaveHistory();
    InvalidateHistory();
}

int ApiAiNativePlugin::AppendUserMsg(const std::wstring& text,
    const std::vector<std::wstring>& images) {
    HistMsg m;
    m.id = L"m" + std::to_wstring(m_history.size());
    m.role = L"user";
    m.text = text;
    m.images = images;
    m_history.push_back(m);
    m_historyCount = (int)m_history.size();
    SaveHistory();
    InvalidateHistory();
    return (int)m_history.size() - 1;
}

int ApiAiNativePlugin::AppendAiMsg(const std::wstring& text) {
    HistMsg m;
    m.id = L"m" + std::to_wstring(m_history.size());
    m.role = L"assistant";
    m.text = text;
    m_history.push_back(m);
    m_historyCount = (int)m_history.size();
    SaveHistory();
    InvalidateHistory();
    return (int)m_history.size() - 1;
}

void ApiAiNativePlugin::SetAiMsgText(int idx, const std::wstring& text) {
    if (idx >= 0 && idx < (int)m_history.size()) {
        m_history[idx].text = text;
        InvalidateHistory();   // v12: 流式增量也标记历史变化
    }
}

// ---- Branches ----
void ApiAiNativePlugin::LoadBranches() {
    std::wstring p = BranchPath();
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) return;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    auto root = ParseJson(data);
    if (root.type != JsonVal::ARRAY) return;
    m_branches.clear();
    for (auto& b : root.a) {
        Branch br;
        br.name = b.Get(L"name").s;
        auto items = b.Get(L"items");
        if (items.type == JsonVal::ARRAY) {
            for (auto& item : items.a) {
                HistMsg m;
                m.id = item.Get(L"id").s;
                m.role = item.Get(L"role").s;
                m.text = item.Get(L"text").s;
                br.items.push_back(std::move(m));
            }
        }
        m_branches.push_back(std::move(br));
    }
    m_branchCount = (int)m_branches.size();
}

void ApiAiNativePlugin::SaveBranches() {
    std::wstring p = BranchPath();
    {
        std::wstring dir = p.substr(0, p.find_last_of(L"\\/"));
        if (!dir.empty()) {
            wchar_t tmp[1024];
            wcscpy_s(tmp, dir.c_str());
            SHCreateDirectoryExW(NULL, tmp, NULL);
        }
    }
    std::vector<JsonVal> arr;
    for (auto& br : m_branches) {
        std::map<std::wstring, JsonVal> obj;
        obj[L"name"] = JsonVal(br.name);
        std::vector<JsonVal> items;
        for (auto& m : br.items) {
            std::map<std::wstring, JsonVal> o;
            o[L"id"] = JsonVal(m.id);
            o[L"role"] = JsonVal(m.role);
            o[L"text"] = JsonVal(m.text);
            items.push_back(JsonVal(o));
        }
        obj[L"items"] = JsonVal(items);
        arr.push_back(JsonVal(obj));
    }
    std::string s = JsonToString(JsonVal(arr), false);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (f.is_open()) { f.write(s.data(), (std::streamsize)s.size()); f.close(); }
}

// ---- Build messages ----
std::string ApiAiNativePlugin::BuildMessagesOpenAI() {
    std::map<std::wstring, JsonVal> payload;
    payload[L"model"] = JsonVal(m_cfgApi.model);
    std::vector<JsonVal> msgs;
    for (auto& m : m_history) {
        std::map<std::wstring, JsonVal> msg;
        msg[L"role"] = JsonVal(m.role);
        if (!m.images.empty()) {
            std::vector<JsonVal> content;
            content.push_back(JsonVal(std::map<std::wstring,JsonVal>{{L"type",JsonVal(L"text")},{L"text",JsonVal(m.text)}}));
            for (auto& im : m.images) {
                std::map<std::wstring,JsonVal> img;
                img[L"type"] = JsonVal(L"image_url");
                std::map<std::wstring,JsonVal> iu;
                iu[L"url"] = JsonVal(im);
                img[L"image_url"] = JsonVal(iu);
                content.push_back(JsonVal(img));
            }
            msg[L"content"] = JsonVal(content);
        } else {
            msg[L"content"] = JsonVal(m.text);
        }
        msgs.push_back(JsonVal(msg));
    }
    payload[L"messages"] = JsonVal(msgs);
    payload[L"stream"] = JsonVal(true);
    if (m_cfgTools.enabled) {
        std::vector<JsonVal> tools;
        static const wchar_t* TNS[] = {
            L"get_current_time", L"read_text_file", L"write_text_file", L"list_dir",
            L"fetch_url", L"open_browser", L"get_clipboard", L"set_clipboard",
            L"get_cache_dir", L"web_search_hint"
        };
        static const wchar_t* TDS[] = {
            L"Get current local time",
            L"Read text file content (max 200KB)",
            L"Write text to file",
            L"List directory contents",
            L"Fetch URL content",
            L"Open URL in browser",
            L"Get clipboard text",
            L"Set clipboard text",
            L"Get cache directory path",
            L"Web search hint"
        };
        for (int i = 0; i < 10; ++i) {
            std::map<std::wstring,JsonVal> fn;
            fn[L"name"] = JsonVal(TNS[i]);
            fn[L"description"] = JsonVal(TDS[i]);
            fn[L"parameters"] = JsonVal(std::map<std::wstring,JsonVal>{
                {L"type",JsonVal(L"object")},
                {L"properties",JsonVal(std::map<std::wstring,JsonVal>())}
            });
            std::map<std::wstring,JsonVal> t;
            t[L"type"] = JsonVal(L"function");
            t[L"function"] = JsonVal(fn);
            tools.push_back(JsonVal(t));
        }
        payload[L"tools"] = JsonVal(tools);
    }
    return JsonToString(JsonVal(payload), false);
}

std::string ApiAiNativePlugin::BuildMessagesClaude() {
    return BuildMessagesOpenAI();
}

std::string ApiAiNativePlugin::BuildMessages() {
    bool isClaude = m_cfgApi.model.find(L"claude") != std::wstring::npos;
    return isClaude ? BuildMessagesClaude() : BuildMessagesOpenAI();
}

// ---- HTTP (non-stream) ----
uint32_t ApiAiNativePlugin::HttpPost(const std::wstring& url,
    const std::string& body,
    const std::wstring& authBearer,
    std::function<void(int,const std::string&,const std::string&)> cb) {
    uint32_t seq = m_nextHttpSeq++;
    {
        std::lock_guard<std::mutex> lock(m_pendingHttpMtx);
        m_pendingHttp[seq] = PendingHttp{ std::move(cb) };
    }
    struct Args {
        ApiAiNativePlugin* self;
        uint32_t seq;
        std::wstring url;
        std::string body;
        std::wstring auth;
    };
    auto* a = new Args{ this, seq, url, body, authBearer };
    CloseHandle(CreateThread(NULL, 0, [](LPVOID p) -> DWORD {
        auto* args = (Args*)p;
        HINTERNET hSession = WinHttpOpen(L"ApiAiNative/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            args->self->OnHttpDone(args->seq, -1, "", "WinHttpOpen failed");
            delete args;
            return 0;
        }
        URL_COMPONENTS uc = { sizeof(uc) };
        wchar_t host[256] = {}, path[1024] = {}, scheme[32] = {};
        uc.lpszHostName = host; uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
        uc.lpszScheme = scheme; uc.dwSchemeLength = 32;
        WinHttpCrackUrl(args->url.c_str(), (DWORD)args->url.size(), 0, &uc);
        HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            args->self->OnHttpDone(args->seq, -1, "", "WinHttpConnect failed");
            delete args;
            return 0;
        }
        DWORD flags = (wcsncmp(scheme, L"https", 5) == 0) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path,
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            args->self->OnHttpDone(args->seq, -1, "", "WinHttpOpenRequest failed");
            delete args;
            return 0;
        }
        WinHttpAddRequestHeaders(hRequest, (L"Authorization: Bearer " + args->auth).c_str(), (ULONG)-1L,
            WINHTTP_ADDREQ_FLAG_ADD);
        WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json", (ULONG)-1L,
            WINHTTP_ADDREQ_FLAG_ADD);
        BOOL sent = WinHttpSendRequest(hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (void*)args->body.data(), (DWORD)args->body.size(),
            (DWORD)args->body.size(), 0);
        if (!sent || !WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            args->self->OnHttpDone(args->seq, -1, "", "HTTP request failed");
            delete args;
            return 0;
        }
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            NULL, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        std::string resp;
        DWORD len = 0;
        do {
            len = 0;
            WinHttpQueryDataAvailable(hRequest, &len);
            if (len) {
                std::string buf(len, 0);
                DWORD rd = 0;
                WinHttpReadData(hRequest, &buf[0], len, &rd);
                resp.append(buf, 0, rd);
            }
        } while (len > 0);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        args->self->OnHttpDone(args->seq, (int)status, resp, "");
        delete args;
        return 0;
    }, a, 0, NULL));
    return seq;
}

void ApiAiNativePlugin::OnHttpDone(uint32_t seq, int status,
    const std::string& body, const std::string& err) {
    // v12: 本函数在后台线程执行，必须封送到 UI 线程再回调
    //      （回调里会 RenderSettingsTab 重建控件，跨线程操作 HWND 不安全）
    if (m_asyncHwnd && IsWindow(m_asyncHwnd)) {
        auto* r = new HttpResult();
        r->status = status;
        r->body = body;
        r->error = err;
        if (!PostMessageW(m_asyncHwnd, WM_API_HTTP_DONE, (WPARAM)seq, (LPARAM)r)) {
            delete r;
            HttpResult rr; rr.status = status; rr.body = body; rr.error = err;
            CallHttpDone(seq, rr);
        }
        return;
    }
    HttpResult r;
    r.status = status;
    r.body = body;
    r.error = err;
    CallHttpDone(seq, r);
}

// SSE 线程参数（文件级，供 SseThreadProc 使用）
struct SseArgs {
    ApiAiNativePlugin* self;
    std::wstring url;
    std::wstring auth;
    std::string body;
};

// ---- SSE 窗口过程（处理后台线程 PostMessage 的 WM_SSE_*） ----
LRESULT CALLBACK ApiAiNativePlugin::SseWndProcStatic(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = (ApiAiNativePlugin*)(LONG_PTR)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (self) return self->SseWndProc(hWnd, msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT ApiAiNativePlugin::SseWndProc(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SSE_CHUNK: {
        std::string* p = (std::string*)lParam;
        if (p) { OnSseChunk(*p); delete p; }
        return 0;
    }
    case WM_SSE_DONE:
        OnSseDone();
        return 0;
    case WM_SSE_ERROR: {
        std::string* p = (std::string*)lParam;
        if (p) { OnSseError(*p); delete p; }
        return 0;
    }
    case WM_SSE_TOOL_CONFIRM: {
        // 工具确认：从后台线程同步到 UI 线程弹 MessageBox
        const wchar_t* msgText = (const wchar_t*)wParam;
        HWND owner = GetOwnerWnd();
        int r = MessageBoxW(owner ? owner : hWnd,
            msgText ? msgText : L"Allow tool?",
            L"Tool Confirmation", MB_YESNO | MB_ICONQUESTION);
        return (LRESULT)r;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---- SSE ----
void ApiAiNativePlugin::StartSseRequest(const std::string& payload) {
    if (m_sseStreaming) return;
    m_sseStreaming = true;
    m_sse = SSEState();
    m_sseRendered = 0;            // v12: 新流复位流式渲染进度
    int aiIdx = AppendAiMsg(L"");
    m_sseAiIdx = aiIdx;
    if (!m_sseHwnd || !IsWindow(m_sseHwnd)) {
        static bool s_sseReg = false;
        if (!s_sseReg) {
            WNDCLASSEXW wc = { sizeof(wc) };
            wc.lpfnWndProc = SseWndProcStatic;
            wc.hInstance = GetModuleHandleW(NULL);
            wc.lpszClassName = L"ApiAiSseWnd";
            RegisterClassExW(&wc);
            s_sseReg = true;
        }
        // lpParam = this，SseWndProcStatic 在 WM_NCCREATE 里取出
        m_sseHwnd = CreateWindowExW(0, L"ApiAiSseWnd", L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, NULL, GetModuleHandleW(NULL), this);
    }
    auto* a = new SseArgs{ this, m_cfgApi.url, m_cfgApi.key, payload };
    CloseHandle(CreateThread(NULL, 0, SseThreadProc, a, 0, NULL));
}

DWORD WINAPI ApiAiNativePlugin::SseThreadProc(LPVOID lpParam) {
    auto* args = (SseArgs*)lpParam;
    HINTERNET hSession = WinHttpOpen(L"ApiAiNative/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        PostMessageW(args->self->m_sseHwnd, WM_SSE_ERROR, 0,
            (LPARAM)new std::string("WinHttpOpen failed"));
        delete args;
        return 0;
    }
    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t host[256] = {}, path[1024] = {}, scheme[32] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    uc.lpszScheme = scheme; uc.dwSchemeLength = 32;
    WinHttpCrackUrl(args->url.c_str(), (DWORD)args->url.size(), 0, &uc);
    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        PostMessageW(args->self->m_sseHwnd, WM_SSE_ERROR, 0,
            (LPARAM)new std::string("WinHttpConnect failed"));
        delete args;
        return 0;
    }
    DWORD flags = (wcsncmp(scheme, L"https", 5) == 0) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        PostMessageW(args->self->m_sseHwnd, WM_SSE_ERROR, 0,
            (LPARAM)new std::string("WinHttpOpenRequest failed"));
        delete args;
        return 0;
    }
    WinHttpAddRequestHeaders(hRequest, (L"Authorization: Bearer " + args->auth).c_str(), (ULONG)-1L,
        WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json", (ULONG)-1L,
        WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, L"Accept: text/event-stream", (ULONG)-1L,
        WINHTTP_ADDREQ_FLAG_ADD);
    BOOL sent = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        (void*)args->body.data(), (DWORD)args->body.size(),
        (DWORD)args->body.size(), 0);
    if (!sent || !WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        PostMessageW(args->self->m_sseHwnd, WM_SSE_ERROR, 0,
            (LPARAM)new std::string("HTTP request failed"));
        delete args;
        return 0;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        NULL, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        std::string resp;
        DWORD len = 0;
        do {
            len = 0;
            WinHttpQueryDataAvailable(hRequest, &len);
            if (len) {
                std::string buf(len, 0);
                DWORD rd = 0;
                WinHttpReadData(hRequest, &buf[0], len, &rd);
                resp.append(buf, 0, rd);
            }
        } while (len > 0);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        PostMessageW(args->self->m_sseHwnd, WM_SSE_ERROR, 0,
            (LPARAM)new std::string("HTTP " + std::to_string(status) + ": " + resp));
        delete args;
        return 0;
    }
    std::string chunk;
    DWORD len = 0;
    do {
        len = 0;
        WinHttpQueryDataAvailable(hRequest, &len);
        if (len) {
            std::string buf(len, 0);
            DWORD rd = 0;
            WinHttpReadData(hRequest, &buf[0], len, &rd);
            chunk.append(buf, 0, rd);
            auto* p = new std::string(chunk);
            if (!PostMessageW(args->self->m_sseHwnd, WM_SSE_CHUNK, 0, (LPARAM)p)) {
                delete p;   // 窗口已销毁，避免泄漏
            }
            chunk.clear();
        }
    } while (len > 0);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    PostMessageW(args->self->m_sseHwnd, WM_SSE_DONE, 0, 0);
    delete args;
    return 0;
}

void ApiAiNativePlugin::OnSseChunk(const std::string& chunk) {
    // m_sse.bufferStr 语义上为 UTF-8 字节缓冲；统一按 std::string 处理，
    // 避免 UTF-8/UTF-16 反复转换在截断处出错。
    std::string buf = m_sse.bufferStr + chunk;
    size_t pos = 0;
    while (pos < buf.size()) {
        size_t end = buf.find("\n\n", pos);
        if (end == std::string::npos) break;
        std::string event = buf.substr(pos, end - pos);
        pos = end + 2;
        std::string data;
        size_t dp = 0;
        while (dp < event.size()) {
            size_t nl = event.find('\n', dp);
            std::string line = event.substr(dp, nl - dp);
            if (line.substr(0, 6) == "data: ") {
                if (!data.empty()) data += "\n";
                data += line.substr(6);
            }
            if (nl == std::string::npos) break;
            dp = nl + 1;
        }
        if (data == "[DONE]") {
            m_sse.done = true;
            continue;
        }
        if (!data.empty()) {
            auto j = ParseJson(data);
            auto choices = j.Get(L"choices");
            if (choices.type == JsonVal::ARRAY && !choices.a.empty()) {
                auto delta = choices.a[0].Get(L"delta");
                auto content = delta.Get(L"content");
                if (content.type == JsonVal::STRING) {
                    m_sse.text += content.s;
                    // v12: 流式期间直接写回历史，不递增版本 → 走增量渲染路径
                    if (m_sseAiIdx >= 0 && m_sseAiIdx < (int)m_history.size())
                        m_history[m_sseAiIdx].text = m_sse.text;
                }
                auto toolCalls = delta.Get(L"tool_calls");
                if (toolCalls.type == JsonVal::ARRAY) {
                    for (auto& tc : toolCalls.a) {
                        int idx = (int)tc.Get(L"index").n;
                        auto& slot = m_sse.toolCalls[idx];
                        auto id = tc.Get(L"id");
                        if (id.type == JsonVal::STRING && !id.s.empty()) slot.id = id.s;
                        auto type = tc.Get(L"type");
                        if (type.type == JsonVal::STRING && !type.s.empty()) slot.type = type.s;
                        auto fn = tc.Get(L"function");
                        auto fnName = fn.Get(L"name");
                        if (fnName.type == JsonVal::STRING && !fnName.s.empty()) slot.name = fnName.s;
                        auto fnArgs = fn.Get(L"arguments");
                        if (fnArgs.type == JsonVal::STRING) slot.args += fnArgs.s;
                        if (std::find(m_sse.toolOrder.begin(), m_sse.toolOrder.end(), idx)
                            == m_sse.toolOrder.end())
                            m_sse.toolOrder.push_back(idx);
                    }
                }
            }
            auto cbd = j.Get(L"content_block_delta");
            if (cbd.type == JsonVal::OBJECT) {
                auto delta2 = cbd.Get(L"delta");
                auto text2 = delta2.Get(L"text");
                if (text2.type == JsonVal::STRING) {
                    m_sse.text += text2.s;
                    if (m_sseAiIdx >= 0 && m_sseAiIdx < (int)m_history.size())
                        m_history[m_sseAiIdx].text = m_sse.text;
                }
            }
        }
    }
    m_sse.bufferStr = buf.substr(pos);
    // v12: 增量渲染——只把新增片段追加到 rich 末尾；
    //      历史版本不一致（如刚重渲染/刚切换视图）才退化为全量渲染
    if (m_viewVisible) {
        HWND hOut = Ctl(L"chat_output");
        if (hOut && IsWindow(hOut) && m_richVersion == m_histVersion) {
            if ((int)m_sse.text.size() > m_sseRendered) {
                AppendStreamDelta(m_sse.text.substr(m_sseRendered));
                m_sseRendered = (int)m_sse.text.size();
            }
        } else {
            RenderHistoryToRich();
        }
    }
}

void ApiAiNativePlugin::OnSseDone() {
    m_sseStreaming = false;
    m_sse.done = true;
    m_busy = false;
    // v12: 流式结束——内容定稿，落盘历史并做一次全量（markdown 格式化）渲染
    if (m_sseAiIdx >= 0 && m_sseAiIdx < (int)m_history.size())
        m_history[m_sseAiIdx].text = m_sse.text;
    SaveHistory();
    InvalidateHistory();
    if (!m_sse.toolCalls.empty() && m_cfgTools.enabled) {
        std::vector<std::pair<std::wstring,std::wstring>> calls;
        for (int idx : m_sse.toolOrder) {
            auto& tc = m_sse.toolCalls[idx];
            calls.push_back(std::make_pair(tc.name, tc.args));
        }
        if (RunToolCalls(calls)) {
            // After tools, continue conversation with new payload
            std::string payload = BuildMessages();
            StartSseRequest(payload);
            return;   // 工具轮继续：等下一轮 SSE_DONE 再收尾渲染
        }
    }
    // v12: 重建主界面（发送按钮由“接收中”恢复为“发送”），
    //      内部会走 EnsureRichOutput 复用 + 版本守卫触发一次 markdown 收尾渲染
    if (m_viewVisible) renderMain();
}

void ApiAiNativePlugin::OnSseError(const std::string& err) {
    m_sseStreaming = false;
    m_busy = false;
    if (m_sseAiIdx >= 0) {
        SetAiMsgText(m_sseAiIdx, L"[error] " + Utf8ToW(err));
    }
    SaveHistory();          // v12: 出错也要落盘，否则重启后消息丢失
    InvalidateHistory();
    if (m_viewVisible) RenderHistoryToRich();
}

// ---- Tools ----
std::wstring ApiAiNativePlugin::RunTool(const std::wstring& name,
    const std::wstring& argsJson) {
    if (name == L"get_current_time") {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return std::wstring(buf);
    }
    if (name == L"get_cache_dir") return GetCacheDirW();
    if (name == L"read_text_file") {
        auto j = ParseJson(WToUtf8(argsJson));
        std::wstring path = j.Get(L"path").s;
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return L"[error] cannot open file";
        std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        if (data.size() > 200000) data = data.substr(0, 200000) + "\r\n... (truncated)";
        return Utf8ToW(data);
    }
    if (name == L"write_text_file") {
        auto j = ParseJson(WToUtf8(argsJson));
        std::wstring path = j.Get(L"path").s;
        std::wstring content = j.Get(L"content").s;
        std::string u = WToUtf8(content);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return L"[error] cannot write file";
        f.write(u.data(), (std::streamsize)u.size());
        f.close();
        return L"ok";
    }
    if (name == L"list_dir") {
        auto j = ParseJson(WToUtf8(argsJson));
        std::wstring path = j.Get(L"path").s;
        std::wstring result;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return L"[error] cannot list directory";
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            result += fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) result += L"/";
            result += L"\r\n";
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return result;
    }
    if (name == L"fetch_url") {
        return L"[async] fetch_url needs async execution";
    }
    if (name == L"open_browser") {
        auto j = ParseJson(WToUtf8(argsJson));
        std::wstring url = j.Get(L"url").s;
        ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return L"ok";
    }
    if (name == L"get_clipboard") {
        if (!OpenClipboard(NULL)) return L"";
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        std::wstring r;
        if (h) {
            wchar_t* p = (wchar_t*)GlobalLock(h);
            if (p) r = p;
            GlobalUnlock(h);
        }
        CloseClipboard();
        return r;
    }
    if (name == L"set_clipboard") {
        auto j = ParseJson(WToUtf8(argsJson));
        std::wstring text = j.Get(L"text").s;
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
            if (h) {
                wchar_t* p = (wchar_t*)GlobalLock(h);
                wcscpy_s(p, text.size() + 1, text.c_str());
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            }
            CloseClipboard();
        }
        return L"ok";
    }
    if (name == L"web_search_hint") {
        auto j = ParseJson(WToUtf8(argsJson));
        std::wstring query = j.Get(L"query").s;
        return L"https://www.bing.com/search?q=" + query;
    }
    return L"[unknown tool] " + name;
}

bool ApiAiNativePlugin::RunToolCalls(
    const std::vector<std::pair<std::wstring,std::wstring>>& calls) {
    if (m_toolRound >= m_cfgTools.maxRounds) return false;
    ++m_toolRound;
    for (auto& c : calls) {
        if (m_cfgTools.requireConfirm) {
            std::wstring msg = L"Allow tool \"" + c.first + L"\"?";
            // 工具确认必须在 UI 线程弹窗，避免在 SSE 回调线程阻塞消息循环。
            // 若当前不在 UI 线程，通过 SendMessage 同步到主窗口执行。
            int r = IDNO;
            HWND owner = GetOwnerWnd();
            DWORD uiTid = GetWindowThreadProcessId(owner, NULL);
            if (GetCurrentThreadId() == uiTid) {
                r = MessageBoxW(owner, msg.c_str(), L"Tool Confirmation",
                    MB_YESNO | MB_ICONQUESTION);
            } else {
                // 通过 WM_APP+0x51 自定义消息同步到 UI 线程
                r = (int)SendMessageW(m_sseHwnd, WM_SSE_TOOL_CONFIRM,
                    (WPARAM)msg.c_str(), 0);
            }
            if (r != IDYES) {
                HistMsg tm;
                tm.id = L"m" + std::to_wstring(m_history.size());
                tm.role = L"tool";
                tm.text = L"User refused to execute this tool.";
                tm.toolName = c.first;
                m_history.push_back(tm);
                continue;
            }
        }
        std::wstring result = RunTool(c.first, c.second);
        HistMsg tm;
        tm.id = L"m" + std::to_wstring(m_history.size());
        tm.role = L"tool";
        tm.text = result;
        tm.toolName = c.first;
        tm.toolArgs = c.second;
        m_history.push_back(tm);
    }
    SaveHistory();
    InvalidateHistory();
    return true;
}

// ---- Rich text ----
void ApiAiNativePlugin::RichClear() {
    HWND h = Ctl(L"chat_output");
    if (!h || !IsWindow(h)) return;
    SetWindowTextW(h, L"");
    YXGui_ClearRichLinks(h);
    m_codeBlocks.clear();
    m_nextCodeBlockIdx = 0;
    m_richVersion = -1;   // v12: 内容已清空，下次必须重渲染
}

static void SetRichSel(HWND h) {
    SendMessageW(h, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
}
static void SetRichFormat(HWND h, const CHARFORMAT2W& cf) {
    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

void ApiAiNativePlugin::RichAppendText(const std::wstring& text) {
    HWND h = Ctl(L"chat_output");
    if (!h || !IsWindow(h)) return;
    SetRichSel(h);
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_CHARSET;
    cf.bCharSet = DEFAULT_CHARSET;
    wcscpy_s(cf.szFaceName, L"Microsoft YaHei");
    cf.yHeight = 210;
    cf.crTextColor = RGB(15, 23, 42);
    SetRichFormat(h, cf);
    PARAFORMAT2 pf;
    memset(&pf, 0, sizeof(pf));
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_STARTINDENT | PFM_LINESPACING;
    pf.dxStartIndent = 0;
    pf.bLineSpacingRule = 5;
    pf.dyLineSpacing = 260;
    SendMessageW(h, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}

// v12: 流式增量追加——只把 AI 消息的新增片段写到 rich 末尾
// （流式期间用纯文本；SSE 结束后由 RenderHistoryToRich 做 markdown 格式化）
void ApiAiNativePlugin::AppendStreamDelta(const std::wstring& delta) {
    if (delta.empty()) return;
    HWND h = Ctl(L"chat_output");
    if (!h || !IsWindow(h)) return;
    LONG nLen = (LONG)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    CHARRANGE cr;
    cr.cpMin = nLen;
    cr.cpMax = nLen;
    SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_CHARSET;
    cf.bCharSet = DEFAULT_CHARSET;
    wcscpy_s(cf.szFaceName, L"Microsoft YaHei");
    cf.yHeight = 210;
    cf.crTextColor = RGB(15, 23, 42);
    SendMessageW(h, EM_SETCHARFORMAT, (WPARAM)SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)delta.c_str());
    SendMessageW(h, WM_VSCROLL, SB_BOTTOM, 0);
}

void ApiAiNativePlugin::RichAppendCode(const std::wstring& code, const std::wstring& lang) {
    HWND h = Ctl(L"chat_output");
    if (!h || !IsWindow(h)) return;
    SetRichSel(h);
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_CHARSET;
    cf.bCharSet = DEFAULT_CHARSET;
    wcscpy_s(cf.szFaceName, L"Consolas");
    cf.yHeight = 200;
    cf.crTextColor = RGB(15, 23, 42);
    SetRichFormat(h, cf);
    PARAFORMAT2 pf;
    memset(&pf, 0, sizeof(pf));
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_STARTINDENT | PFM_LINESPACING | PFM_SPACEBEFORE;
    pf.dxStartIndent = 210;
    pf.bLineSpacingRule = 5;
    pf.dyLineSpacing = 240;
    pf.dySpaceBefore = 60;
    SendMessageW(h, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    int idx = m_nextCodeBlockIdx++;
    m_codeBlocks[idx] = code;
    RichAppendText(L"[" + lang + L"] ");
    LONG start = (LONG)GetWindowTextLengthW(h);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)L"copy");
    LONG end = (LONG)GetWindowTextLengthW(h);
    this->RegisterRichLink(h, start, end, std::wstring(L"copy:") + std::to_wstring(idx));
    RichAppendText(L"\n" + code + L"\n");

}

void ApiAiNativePlugin::RichAppendHeading(int level, const std::wstring& text) {
    HWND h = Ctl(L"chat_output");
    if (!h || !IsWindow(h)) return;
    SetRichSel(h);
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_COLOR | CFM_CHARSET;
    cf.bCharSet = DEFAULT_CHARSET;
    wcscpy_s(cf.szFaceName, L"Microsoft YaHei");
    cf.yHeight = (level == 1) ? 400 : ((level == 2) ? 320 : 260);
    cf.dwEffects = CFE_BOLD;
    cf.crTextColor = RGB(15, 23, 42);
    SetRichFormat(h, cf);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    RichAppendText(L"\n");
}

void ApiAiNativePlugin::RichAppendQuote(const std::wstring& text) {
    HWND h = Ctl(L"chat_output");
    if (!h || !IsWindow(h)) return;
    SetRichSel(h);
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_ITALIC | CFM_COLOR | CFM_CHARSET;
    cf.bCharSet = DEFAULT_CHARSET;
    wcscpy_s(cf.szFaceName, L"Microsoft YaHei");
    cf.yHeight = 210;
    cf.dwEffects = CFE_ITALIC;
    cf.crTextColor = RGB(100, 116, 139);
    SetRichFormat(h, cf);
    PARAFORMAT2 pf;
    memset(&pf, 0, sizeof(pf));
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_STARTINDENT | PFM_LINESPACING;
    pf.dxStartIndent = 210;
    pf.bLineSpacingRule = 5;
    pf.dyLineSpacing = 260;
    SendMessageW(h, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    RichAppendText(L"\n");
}

void ApiAiNativePlugin::RichAppendBullet(const std::wstring& text) {
    RichAppendText(L"\u2022 " + text + L"\n");
}

void ApiAiNativePlugin::RichAppendLink(const std::wstring& text,
    const std::wstring& tag) {
    HWND h = Ctl(L"chat_output");
    if (!h || !IsWindow(h)) return;
    SetRichSel(h);
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_LINK | CFM_COLOR | CFM_CHARSET;
    cf.bCharSet = DEFAULT_CHARSET;
    wcscpy_s(cf.szFaceName, L"Microsoft YaHei");
    cf.yHeight = 210;
    cf.dwEffects = CFE_LINK | CFE_UNDERLINE;
    cf.crTextColor = RGB(29, 78, 216);
    SetRichFormat(h, cf);
    LONG start = (LONG)GetWindowTextLengthW(h);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    LONG end = (LONG)GetWindowTextLengthW(h);
    RegisterRichLink(h, start, end, tag);
    CHARFORMAT2W cfR;
    memset(&cfR, 0, sizeof(cfR));
    cfR.cbSize = sizeof(cfR);
    cfR.dwMask = CFM_LINK | CFM_UNDERLINE;
    cfR.dwEffects = 0;
    SetRichSel(h);
    SetRichFormat(h, cfR);
}

void ApiAiNativePlugin::RichAppendHr() {
    HWND h = Ctl(L"chat_output");
    if (!h || !IsWindow(h)) return;
    SetRichSel(h);
    PARAFORMAT2 pf;
    memset(&pf, 0, sizeof(pf));
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_SPACEBEFORE | PFM_SPACEAFTER;
    pf.dySpaceBefore = 80;
    pf.dySpaceAfter = 80;
    SendMessageW(h, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    RichAppendText(L"\n---\n");
}

void ApiAiNativePlugin::RichAppendImage(const std::wstring& alt,
    const std::wstring& dataUrl) {
    RichAppendLink(L"[img: " + alt + L"]", L"img:" + dataUrl);
}

void ApiAiNativePlugin::ParseAndRenderMarkdown(const std::wstring& md) {
    std::vector<std::wstring> lines;
    size_t pos = 0;
    while (pos < md.size()) {
        size_t nl = md.find(L'\n', pos);
        if (nl == std::wstring::npos) {
            lines.push_back(md.substr(pos));
            break;
        }
        lines.push_back(md.substr(pos, nl - pos));
        pos = nl + 1;
    }
    bool inCode = false;
    std::wstring lang;
    std::wstring codeBuf;
    for (auto& line : lines) {
        if (!inCode && line.size() >= 3 && line.substr(0, 3) == L"```") {
            inCode = true;
            lang = line.substr(3);
            codeBuf.clear();
            continue;
        }
        if (inCode && line.size() >= 3 && line.substr(0, 3) == L"```") {
            inCode = false;
            RichAppendCode(codeBuf, lang);
            continue;
        }
        if (inCode) {
            if (!codeBuf.empty()) codeBuf += L"\n";
            codeBuf += line;
            continue;
        }
        if (line.empty()) { RichAppendText(L"\n"); continue; }
        if (line == L"---" || line == L"***") { RichAppendHr(); continue; }
        if (line.size() >= 4 && line.substr(0, 4) == L"### ") {
            RichAppendHeading(3, line.substr(4)); continue;
        }
        if (line.size() >= 3 && line.substr(0, 3) == L"## ") {
            RichAppendHeading(2, line.substr(3)); continue;
        }
        if (line.size() >= 2 && line.substr(0, 2) == L"# ") {
            RichAppendHeading(1, line.substr(2)); continue;
        }
        if (line.size() >= 2 && line.substr(0, 2) == L"> ") {
            RichAppendQuote(line.substr(2)); continue;
        }
        if (line.size() >= 2 && line.substr(0, 2) == L"- ") {
            RichAppendBullet(line.substr(2)); continue;
        }
        RichAppendText(line + L"\n");
    }
}

void ApiAiNativePlugin::RenderHistoryToRich() {
    HWND h = Ctl(L"chat_output");
    if (!h || !IsWindow(h)) return;
    RichClear();
    int start = std::max(0, (int)m_history.size() - m_displayLimit);
    if (start > 0) {
        RichAppendLink(L"[load more]", L"act:loadmore:");
        RichAppendText(L"\n");
    }
    for (int i = start; i < (int)m_history.size(); ++i) {
        auto& m = m_history[i];
        std::wstring label = (m.role == L"user") ? L"User" :
            (m.role == L"assistant") ? L"AI" :
            (m.role == L"tool") ? (L"Tool: " + m.toolName) : L"System";
        RichAppendHeading(3, label);
        if (!m.images.empty()) {
            for (auto& im : m.images)
                RichAppendImage(L"image", im);
        }
        if (m.role == L"tool") {
            RichAppendCode(m.text, L"result");
        } else {
            ParseAndRenderMarkdown(m.text);
        }
        RichAppendText(L" ");
        RichAppendLink(L"copy", L"act:copy:" + std::to_wstring(i));
        RichAppendText(L" ");
        RichAppendLink(L"edit", L"act:edit:" + std::to_wstring(i));
        RichAppendText(L" ");
        RichAppendLink(L"retry", L"act:regen:" + std::to_wstring(i));
        RichAppendText(L" ");
        RichAppendLink(L"branch", L"act:branch:" + std::to_wstring(i));
        RichAppendText(L" ");
        RichAppendLink(L"del", L"act:del:" + std::to_wstring(i));
        RichAppendText(L"\n\n");
    }
    SendMessageW(h, WM_VSCROLL, SB_BOTTOM, 0);
    // v12: 记录已渲染的历史版本与流式进度（避免重复全量渲染）
    m_richVersion = m_histVersion;
    m_sseRendered = (int)m_sse.text.size();
}

// ---- Message operations ----
void ApiAiNativePlugin::DoCopyMessage(int idx) {
    if (idx < 0 || idx >= (int)m_history.size()) return;
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        std::wstring& text = m_history[idx].text;
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (h) {
            wchar_t* p = (wchar_t*)GlobalLock(h);
            wcscpy_s(p, text.size() + 1, text.c_str());
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
        CloseClipboard();
    }
}

void ApiAiNativePlugin::DoEditMessage(int idx) {
    if (idx < 0 || idx >= (int)m_history.size()) return;
    // v12: 修正控件 id（原为不存在的 chat_input，导致“edit”点了没反应）
    HWND h = Ctl(L"chat_prompt");
    if (h && IsWindow(h)) {
        SetWindowTextW(h, m_history[idx].text.c_str());
        SetFocus(h);
        int n = (int)m_history[idx].text.size();
        SendMessageW(h, EM_SETSEL, (WPARAM)n, (LPARAM)n);
    }
}

void ApiAiNativePlugin::DoRegenerateMessage(int idx) {
    if (idx < 0 || idx >= (int)m_history.size()) return;
    if (m_history[idx].role == L"assistant") {
        m_history.resize(idx);
    } else {
        m_history.resize(idx + 1);
    }
    m_historyCount = (int)m_history.size();
    SaveHistory();
    InvalidateHistory();
    std::string payload = BuildMessages();
    StartSseRequest(payload);
}

void ApiAiNativePlugin::DoBranchMessage(int idx) {
    if (idx < 0 || idx >= (int)m_history.size()) return;
    Branch br;
    br.name = L"Branch " + std::to_wstring(m_branches.size() + 1);
    for (int i = 0; i <= idx; ++i) br.items.push_back(m_history[i]);
    m_branches.push_back(std::move(br));
    m_branchCount = (int)m_branches.size();
    SaveBranches();
    m_status = L"Branch created.";
}

void ApiAiNativePlugin::DoDeleteMessage(int idx) {
    if (idx < 0 || idx >= (int)m_history.size()) return;
    m_history.erase(m_history.begin() + idx);
    m_historyCount = (int)m_history.size();
    SaveHistory();
    InvalidateHistory();
    if (m_viewVisible) RenderHistoryToRich();
}

void ApiAiNativePlugin::DoShowToolArgs(int idx) {
    if (idx < 0 || idx >= (int)m_history.size()) return;
    auto& m = m_history[idx];
    if (m.role == L"tool") {
        MessageBoxW(GetOwnerWnd(), m.toolArgs.c_str(), L"Tool Args", MB_OK);
    }
}
