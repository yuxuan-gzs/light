// MainWindow.cpp
#include "MainWindow.h"
#include "Theme.h"
#include "core/util.h"
#include "core/ProjectStore.h"
#include "core/CodeGenerator.h"
#include "core/Decompiler.h"
#include "core/LibraryScanner.h"
#include "core/DllBuilder.h"
#include "core/JdkCompiler.h"
#include "core/MsvcCompiler.h"
#include "core/AppSettings.h"
#include "LibraryStorePanel.h"
#include "StartupDialog.h"
#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <set>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace vcb {

namespace {

// ---- 布局常量 ----
const float kBlockH = 34.0f;
const float kReporterH = 26.0f;
const float kRadius = 9.0f;
const float kPadX = 12.0f;
const float kSlotPadX = 7.0f;
const float kSlotH = 24.0f;
const float kIndent = 24.0f;
const float kGap = 5.0f;
const float kBodyTopPad = 7.0f;
const float kBodyBottomPad = 9.0f;
const float kReporterGap = 3.0f;
const float kElseBarH = 24.0f;
const float kCanvasMargin = 18.0f;

// ---- 工具栏按钮 ----
struct ToolButton { int id; const wchar_t* label; };
const ToolButton kToolButtons[] = {
    { 0, L"新建" }, { 1, L"保存" }, { 2, L"编译" }, { 3, L"反编译" },
    { 4, L"编译DLL" }, { 5, L"扫描库" }, { 6, L"生成翻译" }, { 7, L"删除" },
    { 8, L"库商城" }, { 9, L"▶ 运行" }, { 10, L"⚙ 设置" },
};
const int kToolButtonCount = 11;

// ---- 标签分段（{0}/{1} 占位） ----
struct Segment { bool param = false; int idx = 0; std::wstring text; };

std::vector<Segment> SplitLabel(const std::string& label) {
    std::vector<Segment> out;
    std::wstring w = Utf8ToWide(label);
    std::wstring cur;
    size_t i = 0;
    while (i < w.size()) {
        if (w[i] == L'{') {
            size_t j = i + 1;
            while (j < w.size() && w[j] >= L'0' && w[j] <= L'9') ++j;
            if (j > i + 1 && j < w.size() && w[j] == L'}') {
                if (!cur.empty()) { Segment s; s.text = cur; out.push_back(s); cur.clear(); }
                Segment s; s.param = true; s.idx = (int)(w[i + 1] - L'0'); out.push_back(s);
                i = j + 1;
                continue;
            }
        }
        cur += w[i];
        ++i;
    }
    if (!cur.empty()) { Segment s; s.text = cur; out.push_back(s); }
    return out;
}

bool IsContainer(const Block& b) {
    return b.typeId == "if" || b.typeId == "ifElse" || b.typeId == "repeat" || b.typeId == "while";
}

// ---- 输入对话框 ----
struct PromptCtx {
    std::wstring initial;
    std::wstring result;
    bool ok = false;
    bool done = false;
    HWND edit = nullptr;
};

LRESULT CALLBACK PromptProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    PromptCtx* c = (PromptCtx*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
        c = (PromptCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)c);
        HINSTANCE inst = cs->hInstance;
        c->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", c->initial.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 14, 14, 330, 26, h, (HMENU)100, inst, nullptr);
        SendMessageW(c->edit, EM_SETSEL, 0, -1);
        CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            150, 56, 88, 28, h, (HMENU)1, inst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
            248, 56, 88, 28, h, (HMENU)2, inst, nullptr);
        SetFocus(c->edit);
        return 0;
    }
    if (!c) return DefWindowProcW(h, m, w, l);
    if (m == WM_COMMAND) {
        int id = LOWORD(w);
        if (id == 1) {
            wchar_t buf[512];
            GetWindowTextW(c->edit, buf, 512);
            c->result = buf;
            c->ok = true;
            DestroyWindow(h);
            return 0;
        }
        if (id == 2) { c->ok = false; DestroyWindow(h); return 0; }
    }
    if (m == WM_CLOSE) { DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) { c->done = true; return 0; }
    return DefWindowProcW(h, m, w, l);
}

std::wstring PromptText(HWND parent, const std::wstring& title, const std::wstring& initial) {
    static bool clsRegistered = false;
    HINSTANCE inst = (HINSTANCE)GetModuleHandleW(nullptr);
    if (!clsRegistered) {
        WNDCLASSW wc{}; wc.lpfnWndProc = PromptProc; wc.hInstance = inst;
        wc.lpszClassName = L"VCBlocksPrompt"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        RegisterClassW(&wc);
        clsRegistered = true;
    }
    PromptCtx ctx; ctx.initial = initial;
    HWND h = CreateWindowExW(0, L"VCBlocksPrompt", title.c_str(),
        WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 372, 130,
        parent, nullptr, inst, &ctx);
    if (!h) return initial;
    // 居中于父窗口
    RECT pr, wr;
    GetWindowRect(parent, &pr);
    GetWindowRect(h, &wr);
    SetWindowPos(h, nullptr, (pr.left + pr.right) / 2 - (wr.right - wr.left) / 2,
        (pr.top + pr.bottom) / 2 - (wr.bottom - wr.top) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(h, SW_SHOW);
    EnableWindow(parent, FALSE);
    MSG msg;
    while (!ctx.done && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    return ctx.ok ? ctx.result : initial;
}

// ---- 新建项目对话框（项目名 + 目标语言） ----
struct NewProjectCtx {
    std::wstring initial;
    std::wstring result;
    int lang = 0;   // 0=C, 1=Cpp, 2=Java
    bool ok = false;
    bool done = false;
    HWND edit = nullptr;
    HWND combo = nullptr;
};

LRESULT CALLBACK NewProjectProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    NewProjectCtx* c = (NewProjectCtx*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
        c = (NewProjectCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)c);
        HINSTANCE inst = cs->hInstance;
        CreateWindowExW(0, L"STATIC", L"项目名称:", WS_CHILD | WS_VISIBLE, 14, 16, 80, 20, h, (HMENU)200, inst, nullptr);
        c->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", c->initial.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 100, 12, 244, 26, h, (HMENU)100, inst, nullptr);
        SendMessageW(c->edit, EM_SETSEL, 0, -1);
        CreateWindowExW(0, L"STATIC", L"目标语言:", WS_CHILD | WS_VISIBLE, 14, 54, 80, 20, h, (HMENU)201, inst, nullptr);
        c->combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            100, 50, 244, 300, h, (HMENU)101, inst, nullptr);
        SendMessageW(c->combo, CB_ADDSTRING, 0, (LPARAM)L"C (内置编译器)");
        SendMessageW(c->combo, CB_ADDSTRING, 0, (LPARAM)L"C++ (MSVC)");
        SendMessageW(c->combo, CB_ADDSTRING, 0, (LPARAM)L"Java (JDK)");
        SendMessageW(c->combo, CB_SETCURSEL, c->lang, 0);
        CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            150, 88, 88, 28, h, (HMENU)1, inst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
            248, 88, 88, 28, h, (HMENU)2, inst, nullptr);
        SetFocus(c->edit);
        return 0;
    }
    if (!c) return DefWindowProcW(h, m, w, l);
    if (m == WM_COMMAND) {
        int id = LOWORD(w);
        if (id == 1) {
            wchar_t buf[512];
            GetWindowTextW(c->edit, buf, 512);
            c->result = buf;
            c->lang = (int)SendMessageW(c->combo, CB_GETCURSEL, 0, 0);
            c->ok = true;
            DestroyWindow(h);
            return 0;
        }
        if (id == 2) { c->ok = false; DestroyWindow(h); return 0; }
    }
    if (m == WM_CLOSE) { DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) { c->done = true; return 0; }
    return DefWindowProcW(h, m, w, l);
}

std::wstring PromptNewProject(HWND parent, const std::wstring& initial, int& outLang) {
    static bool clsRegistered = false;
    HINSTANCE inst = (HINSTANCE)GetModuleHandleW(nullptr);
    if (!clsRegistered) {
        WNDCLASSW wc{}; wc.lpfnWndProc = NewProjectProc; wc.hInstance = inst;
        wc.lpszClassName = L"VCBlocksNewProject"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        RegisterClassW(&wc);
        clsRegistered = true;
    }
    NewProjectCtx ctx; ctx.initial = initial; ctx.lang = outLang;
    HWND h = CreateWindowExW(0, L"VCBlocksNewProject", L"新建项目",
        WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 372, 158,
        parent, nullptr, inst, &ctx);
    if (!h) return initial;
    RECT pr, wr;
    GetWindowRect(parent, &pr);
    GetWindowRect(h, &wr);
    SetWindowPos(h, nullptr, (pr.left + pr.right) / 2 - (wr.right - wr.left) / 2,
        (pr.top + pr.bottom) / 2 - (wr.bottom - wr.top) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(h, SW_SHOW);
    EnableWindow(parent, FALSE);
    MSG msg;
    while (!ctx.done && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (ctx.ok) outLang = ctx.lang;
    return ctx.ok ? ctx.result : initial;
}

// ---- 运行可执行文件并捕获输出（同步，用于「编译并运行」） ----
bool RunExecutable(const std::wstring& exePath, std::string& output) {
    std::wstring cmd = L"\"" + exePath + L"\"";
    FILE* pipe = _wpopen(cmd.c_str(), L"r");
    if (!pipe) return false;
    char buf[1024];
    output.clear();
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
    int rc = _pclose(pipe);
    return true; // 即使程序返回非 0，也算运行了；输出已捕获
}

// ---- 解析 "#RRGGBB" -> ARGB ----
uint32_t ParseHexColor(const std::string& s) {
    std::string t = s;
    if (!t.empty() && t[0] == '#') t = t.substr(1);
    unsigned long v = 0;
    try { v = std::stoul(t.empty() ? "1E1E1E" : t, nullptr, 16); } catch (...) { v = 0x1E1E1E; }
    return 0xFF000000u | (uint32_t)v;
}

// ---- 从编译日志解析第一个错误行号（兼容 TCC/javac/MSVC 格式）----
// TCC:    path.c:5: error: ...
// javac:  path.java:3: error: ...
// MSVC:   path.cpp(12) : error C2065: ...
int ParseErrorLine(const std::string& log) {
    for (size_t i = 0; i + 1 < log.size(); ++i) {
        char c = log[i];
        if ((c == ':' || c == '(') && isdigit((unsigned char)log[i + 1])) {
            size_t j = i + 1;
            int num = 0;
            while (j < log.size() && isdigit((unsigned char)log[j])) {
                num = num * 10 + (log[j] - '0');
                if (num > 1000000) break;
                ++j;
            }
            if (num > 0 && num < 1000000) return num;
            i = j - 1;
        }
    }
    return -1;
}

// ---- 综合设置对话框（编译参数 + 背景样式） ----
struct SettingsCtx {
    CompileConfig cfg;
    int bgStyle = 0;
    std::wstring bgColor;
    std::wstring bgImage;
    bool ok = false;
    bool done = false;
    HWND comboConfig = nullptr, comboRuntime = nullptr, comboOutput = nullptr;
    HWND comboOpt = nullptr, comboWarn = nullptr;
    HWND chkRun = nullptr, chkSymbols = nullptr, chkPlugin = nullptr;
    HWND comboBg = nullptr;
    HWND editColor = nullptr;
    HWND staticImage = nullptr;
};

LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    SettingsCtx* c = (SettingsCtx*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
        c = (SettingsCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)c);
        HINSTANCE inst = cs->hInstance;
        auto mk = [&](const wchar_t* txt, int x, int y, int wd, int ht, int id) {
            return CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE, x, y, wd, ht, h, (HMENU)(size_t)id, inst, nullptr);
        };
        auto mkCombo = [&](int x, int y, int wd, int id) {
            return CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                x, y, wd, 300, h, (HMENU)(size_t)id, inst, nullptr);
        };

        int lx = 16, cx = 128, cw = 230;
        // 编译设置
        mk(L"构建配置:", lx, 16, 90, 20, 201);
        c->comboConfig = mkCombo(cx, 13, cw, 202);
        SendMessageW(c->comboConfig, CB_ADDSTRING, 0, (LPARAM)L"正式版 (Release)");
        SendMessageW(c->comboConfig, CB_ADDSTRING, 0, (LPARAM)L"测试版 (Debug)");
        SendMessageW(c->comboConfig, CB_SETCURSEL, c->cfg.config == BuildConfig::Debug ? 1 : 0, 0);

        mk(L"运行库:", lx, 48, 90, 20, 203);
        c->comboRuntime = mkCombo(cx, 45, cw, 204);
        SendMessageW(c->comboRuntime, CB_ADDSTRING, 0, (LPARAM)L"/MT  静态链接(正式)");
        SendMessageW(c->comboRuntime, CB_ADDSTRING, 0, (LPARAM)L"/MTd 静态链接(调试)");
        SendMessageW(c->comboRuntime, CB_SETCURSEL, c->cfg.runtime == RuntimeLib::StaticDebug ? 1 : 0, 0);

        mk(L"输出类型:", lx, 80, 90, 20, 205);
        c->comboOutput = mkCombo(cx, 77, cw, 206);
        SendMessageW(c->comboOutput, CB_ADDSTRING, 0, (LPARAM)L".exe 可执行文件");
        SendMessageW(c->comboOutput, CB_ADDSTRING, 0, (LPARAM)L".dll 动态链接库");
        SendMessageW(c->comboOutput, CB_SETCURSEL, c->cfg.output == OutputType::Dll ? 1 : 0, 0);

        mk(L"优化等级:", lx, 112, 90, 20, 207);
        c->comboOpt = mkCombo(cx, 109, cw, 208);
        const wchar_t* opts[] = { L"O0 无优化", L"O1 基本优化", L"O2 标准优化", L"O3 最大优化" };
        for (int i = 0; i < 4; ++i) SendMessageW(c->comboOpt, CB_ADDSTRING, 0, (LPARAM)opts[i]);
        SendMessageW(c->comboOpt, CB_SETCURSEL, c->cfg.optimize, 0);

        mk(L"警告等级:", lx, 144, 90, 20, 209);
        c->comboWarn = mkCombo(cx, 141, cw, 210);
        const wchar_t* warns[] = { L"W0 关闭", L"W1", L"W2", L"W3 (默认)", L"W4" };
        for (int i = 0; i < 5; ++i) SendMessageW(c->comboWarn, CB_ADDSTRING, 0, (LPARAM)warns[i]);
        SendMessageW(c->comboWarn, CB_SETCURSEL, c->cfg.warnLevel, 0);

        c->chkRun = CreateWindowExW(0, L"BUTTON", L"编译后自动运行", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            lx, 174, 180, 22, h, (HMENU)211, inst, nullptr);
        if (c->cfg.runAfterBuild) SendMessageW(c->chkRun, BM_SETCHECK, BST_CHECKED, 0);

        c->chkSymbols = CreateWindowExW(0, L"BUTTON", L"生成调试符号", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            lx, 198, 160, 22, h, (HMENU)212, inst, nullptr);
        if (c->cfg.symbols) SendMessageW(c->chkSymbols, BM_SETCHECK, BST_CHECKED, 0);

        c->chkPlugin = CreateWindowExW(0, L"BUTTON", L"YXG 插件格式(.dll 时)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            cx, 198, 220, 22, h, (HMENU)213, inst, nullptr);
        if (c->cfg.pluginDll) SendMessageW(c->chkPlugin, BM_SETCHECK, BST_CHECKED, 0);

        // 背景设置
        mk(L"── 背景样式 ──", lx, 230, 160, 20, 220);
        mk(L"背景样式:", lx, 254, 90, 20, 221);
        c->comboBg = mkCombo(cx, 251, cw, 222);
        SendMessageW(c->comboBg, CB_ADDSTRING, 0, (LPARAM)L"纯色");
        SendMessageW(c->comboBg, CB_ADDSTRING, 0, (LPARAM)L"网格");
        SendMessageW(c->comboBg, CB_ADDSTRING, 0, (LPARAM)L"图片");
        SendMessageW(c->comboBg, CB_SETCURSEL, c->bgStyle, 0);

        mk(L"背景颜色:", lx, 286, 90, 20, 223);
        c->editColor = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            cx, 283, 140, 24, h, (HMENU)224, inst, nullptr);
        std::wstring hex = c->bgColor;
        if (hex.size() >= 1 && hex[0] == L'#') hex = hex.substr(1);
        SetWindowTextW(c->editColor, hex.c_str());

        CreateWindowExW(0, L"BUTTON", L"选择图片...", WS_CHILD | WS_VISIBLE,
            280, 282, 100, 26, h, (HMENU)225, inst, nullptr);
        c->staticImage = mk(c->bgImage.empty() ? L"(未选择图片)" : c->bgImage.c_str(),
            16, 312, 370, 20, 226);

        CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            150, 342, 100, 30, h, (HMENU)1, inst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
            262, 342, 100, 30, h, (HMENU)2, inst, nullptr);
        return 0;
    }
    if (!c) return DefWindowProcW(h, m, w, l);
    if (m == WM_COMMAND) {
        int id = LOWORD(w);
        if (id == 225) { // 选择图片
            wchar_t path[MAX_PATH] = {0};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = h;
            ofn.lpstrFilter = L"图片文件 (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0所有文件 (*.*)\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                c->bgImage = path;
                SetWindowTextW(c->staticImage, path);
            }
            return 0;
        }
        if (id == 1) { // 确定
            c->cfg.config = (SendMessageW(c->comboConfig, CB_GETCURSEL, 0, 0) == 1) ? BuildConfig::Debug : BuildConfig::Release;
            c->cfg.runtime = (SendMessageW(c->comboRuntime, CB_GETCURSEL, 0, 0) == 1) ? RuntimeLib::StaticDebug : RuntimeLib::StaticRelease;
            c->cfg.output = (SendMessageW(c->comboOutput, CB_GETCURSEL, 0, 0) == 1) ? OutputType::Dll : OutputType::Exe;
            c->cfg.optimize = (int)SendMessageW(c->comboOpt, CB_GETCURSEL, 0, 0);
            c->cfg.warnLevel = (int)SendMessageW(c->comboWarn, CB_GETCURSEL, 0, 0);
            c->cfg.runAfterBuild = (SendMessageW(c->chkRun, BM_GETCHECK, 0, 0) == BST_CHECKED);
            c->cfg.symbols = (SendMessageW(c->chkSymbols, BM_GETCHECK, 0, 0) == BST_CHECKED);
            c->cfg.pluginDll = (SendMessageW(c->chkPlugin, BM_GETCHECK, 0, 0) == BST_CHECKED);
            c->bgStyle = (int)SendMessageW(c->comboBg, CB_GETCURSEL, 0, 0);
            wchar_t hex[64];
            GetWindowTextW(c->editColor, hex, 64);
            c->bgColor = std::wstring(L"#") + hex;
            c->ok = true;
            DestroyWindow(h);
            return 0;
        }
        if (id == 2) { c->ok = false; DestroyWindow(h); return 0; }
    }
    if (m == WM_CLOSE) { DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) { c->done = true; return 0; }
    return DefWindowProcW(h, m, w, l);
}

bool ShowSettingsDialog(HWND parent, CompileConfig& cfg, AppSettings& settings) {
    static bool clsRegistered = false;
    HINSTANCE inst = (HINSTANCE)GetModuleHandleW(nullptr);
    if (!clsRegistered) {
        WNDCLASSW wc{}; wc.lpfnWndProc = SettingsProc; wc.hInstance = inst;
        wc.lpszClassName = L"VCBlocksSettings"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        RegisterClassW(&wc);
        clsRegistered = true;
    }
    SettingsCtx ctx;
    ctx.cfg = cfg;
    ctx.bgStyle = settings.backgroundStyle;
    ctx.bgColor = Utf8ToWide(settings.backgroundColor);
    ctx.bgImage = settings.backgroundImage;

    HWND h = CreateWindowExW(0, L"VCBlocksSettings", L"设置",
        WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 406, 402,
        parent, nullptr, inst, &ctx);
    if (!h) return false;
    RECT pr, wr;
    GetWindowRect(parent, &pr);
    GetWindowRect(h, &wr);
    SetWindowPos(h, nullptr, (pr.left + pr.right) / 2 - (wr.right - wr.left) / 2,
        (pr.top + pr.bottom) / 2 - (wr.bottom - wr.top) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(h, SW_SHOW);
    EnableWindow(parent, FALSE);
    MSG msg;
    while (!ctx.done && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (ctx.ok) {
        cfg = ctx.cfg;
        settings.backgroundStyle = ctx.bgStyle;
        settings.backgroundColor = WideToUtf8(ctx.bgColor);
        settings.backgroundImage = ctx.bgImage;
    }
    return ctx.ok;
}

} // namespace

// ============================================================
// 构造/析构
// ============================================================
MainWindow::~MainWindow() {
    delete m_fontUi; delete m_fontBlock; delete m_fontSmall; delete m_fontTitle;
    delete m_bgImage;
}

bool MainWindow::CreatePanel(HINSTANCE inst, HWND parent) {
    m_inst = inst;
    // 恢复用户设置（语言、编译配置、背景、最近项目）
    AppSettings& settings = AppSettings::Instance();
    std::string serr;
    settings.Load(serr);
    m_locale = settings.locale;
    m_compile = settings.compile;
    m_target = settings.defaultLang;

    m_fontUi = theme::MakeFont(L"Microsoft YaHei UI", 13);
    m_fontBlock = theme::MakeFont(L"Microsoft YaHei UI", 13);
    m_fontSmall = theme::MakeFont(L"Microsoft YaHei UI", 11);
    m_fontTitle = theme::MakeFont(L"Microsoft YaHei UI", 15, Gdiplus::FontStyleBold);

    WNDCLASSW wc{};
    wc.lpfnWndProc = MainWindow::WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"VCBlocksPanelWnd";
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(0, L"VCBlocksPanelWnd", L"",
        WS_CHILD | WS_VISIBLE, 0, 0, 100, 100,
        parent, nullptr, inst, this);
    if (!m_hwnd) return false;

    // 语言下拉框
    m_comboLang = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        0, 0, 120, 300, m_hwnd, (HMENU)1001, inst, nullptr);
    SendMessageW(m_comboLang, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    for (const auto& l : Translator::SupportedLocales()) {
        SendMessageW(m_comboLang, CB_ADDSTRING, 0, (LPARAM)Utf8ToWide(l.display).c_str());
    }
    // 选择 zh-CN
    auto locs = Translator::SupportedLocales();
    int sel = 0;
    for (size_t i = 0; i < locs.size(); ++i) if (locs[i].locale == m_locale) sel = (int)i;
    SendMessageW(m_comboLang, CB_SETCURSEL, sel, 0);

    // 目标语言下拉框（C / C++ / Java）
    m_comboTarget = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        0, 0, 110, 300, m_hwnd, (HMENU)1003, inst, nullptr);
    SendMessageW(m_comboTarget, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessageW(m_comboTarget, CB_ADDSTRING, 0, (LPARAM)L"C (内置编译器)");
    SendMessageW(m_comboTarget, CB_ADDSTRING, 0, (LPARAM)L"C++ (MSVC)");
    SendMessageW(m_comboTarget, CB_ADDSTRING, 0, (LPARAM)L"Java (JDK)");
    int langSel = (m_target == TargetLang::Cpp) ? 1 : (m_target == TargetLang::Java) ? 2 : 0;
    SendMessageW(m_comboTarget, CB_SETCURSEL, langSel, 0);

    // 参数编辑框（隐藏）
    m_editParam = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
        0, 0, 60, 22, m_hwnd, (HMENU)1002, inst, nullptr);

    // 资源管理器 TreeView
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TREEVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    m_tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT,
        0, 0, 100, 100, m_hwnd, (HMENU)1004, inst, nullptr);
    SendMessageW(m_tree, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    // 初始化内置编译系统（TCC）
    std::string terr;
    m_tccReady = TccCompiler::Init(terr);

    // 预检测 JDK（SearchPathW 很快）；MSVC 采用懒检测（编译 C++ 时才扫描，避免打开面板卡顿）
    std::string jerr;
    m_jdkReady = JdkCompiler::Init(jerr);

    // 启动弹窗：选择创建 / 打开项目
    StartupResult sr = StartupDialog::Show(inst, m_hwnd, settings.recentProjects);
    if (sr.ok && sr.createNew) {
        std::string nm = sr.projectName;
        if (!nm.empty() && ProjectStore::IsValidProjectName(nm)) {
            m_project = nm;
            m_target = sr.lang;
            m_program = Program();
            m_program.name = nm;
            m_selBlock = nullptr;
            SaveProgram();
        }
    } else if (sr.ok) {
        m_project = sr.projectName;
        m_program = BlockLibrary::MakeSampleProgram();
        LoadProgram();
    } else {
        m_program = BlockLibrary::MakeSampleProgram();
    }

    // 记录最近项目 + 保存设置（语言、编译配置、背景）
    settings.defaultLang = m_target;
    settings.PushRecent(m_project);
    settings.Save(serr);

    // 初始化
    ReloadTranslation();
    RebuildCoreTranslation();

    // 首次定位控件（窗口创建时的 WM_SIZE 可能早于控件创建）
    RECT rc; GetClientRect(m_hwnd, &rc);
    OnSize(rc.right, rc.bottom);

    // 填充资源管理器树 + 头文件积木分类
    RefreshProjectTree();
    RefreshHeaderBlocks();

    return true;
}

// ============================================================
// 消息处理
// ============================================================
LRESULT CALLBACK MainWindow::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    MainWindow* self;
    if (m == WM_NCCREATE) {
        self = (MainWindow*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = h;
    } else {
        self = (MainWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (self) return self->HandleMessage(h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

LRESULT MainWindow::HandleMessage(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: OnPaint(); return 0;
    case WM_SIZE: { RECT rc; GetClientRect(h, &rc); OnSize(rc.right, rc.bottom); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: OnMouseMove(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_LBUTTONDOWN: SetFocus(h); OnLButtonDown(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_LBUTTONUP: OnLButtonUp(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_LBUTTONDBLCLK: OnLButtonDblClk(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(w);
        m_canvasScrollY -= delta / 120.0f * 40.0f;
        if (m_canvasScrollY < 0) m_canvasScrollY = 0;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN: OnKeyDown(w); return 0;
    case WM_COMMAND:
        if (HIWORD(w) == CBN_SELCHANGE && l == (LPARAM)m_comboLang) {
            int idx = (int)SendMessageW(m_comboLang, CB_GETCURSEL, 0, 0);
            auto locs = Translator::SupportedLocales();
            if (idx >= 0 && idx < (int)locs.size()) {
                m_locale = locs[idx].locale;
                RebuildCoreTranslation();
                ReloadTranslation();
                AppSettings::Instance().locale = m_locale;
                std::string e; AppSettings::Instance().Save(e);
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        if (HIWORD(w) == CBN_SELCHANGE && l == (LPARAM)m_comboTarget) {
            int idx = (int)SendMessageW(m_comboTarget, CB_GETCURSEL, 0, 0);
            if (idx == 1) m_target = TargetLang::Cpp;
            else if (idx == 2) m_target = TargetLang::Java;
            else m_target = TargetLang::C;
            AppSettings::Instance().defaultLang = m_target;
            std::string e; AppSettings::Instance().Save(e);
            const wchar_t* names[] = { L"C", L"C++", L"Java" };
            SetStatus(std::wstring(L"目标语言: ") + names[idx]);
            InvalidateRect(h, nullptr, FALSE);
        }
        if (HIWORD(w) == EN_KILLFOCUS || (HIWORD(w) == 0 && l == (LPARAM)m_editParam && LOWORD(w) == IDOK)) {
            if (m_editing) {
                wchar_t buf[512];
                GetWindowTextW(m_editParam, buf, 512);
                std::string text = WideToUtf8(buf);
                if (m_editIsReporter && m_editReporterBlock) {
                    if (!m_editReporterBlock->params.empty()) m_editReporterBlock->params[0].text = text;
                } else if (m_editBlock && m_editParamIndex >= 0 && m_editParamIndex < (int)m_editBlock->params.size()) {
                    m_editBlock->params[m_editParamIndex].text = text;
                }
                m_editing = false;
                ShowWindow(m_editParam, SW_HIDE);
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void MainWindow::OnSize(int w, int h) {
    // 目标语言下拉框 + 语言下拉框放到工具栏右侧
    RECT rc; GetClientRect(m_hwnd, &rc);
    MoveWindow(m_comboTarget, w - 280, 12, 132, 300, TRUE);
    MoveWindow(m_comboLang, w - 140, 12, 126, 300, TRUE);
    // 资源管理器树：左侧标签栏(26px)下方
    MoveWindow(m_tree, 0, m_toolbarH + 26, m_paletteW, h - m_toolbarH - m_statusH - 26, TRUE);
    // 根据标签页显示/隐藏
    ShowWindow(m_tree, m_leftTab == 0 ? SW_SHOW : SW_HIDE);
}

// ============================================================
// 绘制
// ============================================================
void MainWindow::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);
    RECT rc; GetClientRect(m_hwnd, &rc);
    int w = rc.right, h = rc.bottom;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);

    Gdiplus::Graphics g(mem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // 背景
    g.Clear(theme::C(theme::color::WindowBg));

    DrawToolbar(g, w);
    DrawPalette(g, h);
    int outH = m_outputVisible ? m_outputH : 0;
    int canvasH = h - m_toolbarH - m_statusH - outH;
    if (canvasH < 40) canvasH = 40;
    DrawCanvas(g, m_paletteW, m_toolbarH, w - m_paletteW, canvasH);
    if (m_outputVisible) DrawOutput(g, w, h);
    DrawStatus(g, w, h);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(m_hwnd, &ps);
}

void MainWindow::DrawToolbar(Gdiplus::Graphics& g, int w) {
    Gdiplus::SolidBrush bg(theme::C(theme::color::PanelBg));
    g.FillRectangle(&bg, 0, 0, w, m_toolbarH);
    Gdiplus::SolidBrush border(theme::C(theme::color::Border));
    g.FillRectangle(&border, 0, m_toolbarH - 1, w, 1);

    // 标题
    theme::DrawText(g, L"VCBlocks", *m_fontTitle, theme::C(theme::color::Text),
        Gdiplus::RectF(18, 0, 130, m_toolbarH));

    m_buttons.clear();
    float x = 160;
    for (int i = 0; i < kToolButtonCount; ++i) {
        std::wstring label = kToolButtons[i].label;
        float bw = theme::MeasureText(g, label, *m_fontUi).Width + 24;
        RECT r{ (int)x, 10, (int)(x + bw), m_toolbarH - 10 };
        m_buttons.push_back(r);
        bool hover = (m_hoverButton == i);
        if (hover) {
            Gdiplus::SolidBrush hb(theme::C(theme::color::HoverTint));
            theme::FillRoundRect(g, Gdiplus::RectF((float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top)), 7, hb);
        }
        theme::DrawText(g, label, *m_fontUi, theme::C(theme::color::Text),
            Gdiplus::RectF((float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top)),
            Gdiplus::StringAlignmentCenter);
        x += bw + 8;
    }
}

void MainWindow::DrawPalette(Gdiplus::Graphics& g, int h) {
    int ph = h - m_toolbarH - m_statusH;
    Gdiplus::SolidBrush bg(theme::C(theme::color::PanelBg));
    g.FillRectangle(&bg, 0, m_toolbarH, m_paletteW, ph);
    Gdiplus::SolidBrush border(theme::C(theme::color::Border));
    g.FillRectangle(&border, m_paletteW - 1, m_toolbarH, 1, ph);

    // 顶部标签栏（项目 / 积木）
    const int tabH = 26;
    const wchar_t* tabs[2] = { L"项目", L"积木" };
    for (int t = 0; t < 2; ++t) {
        Gdiplus::RectF r((float)(t * m_paletteW / 2), (float)m_toolbarH, (float)(m_paletteW / 2), (float)tabH);
        if (m_leftTab == t) {
            Gdiplus::SolidBrush ab(theme::C(theme::color::HoverTint));
            g.FillRectangle(&ab, r);
            Gdiplus::SolidBrush bar(theme::C(theme::color::Accent));
            g.FillRectangle(&bar, r.X, r.Y + (float)(tabH - 2), r.Width, 2.0f);
        }
        theme::DrawText(g, tabs[t], *m_fontSmall,
            m_leftTab == t ? theme::C(theme::color::Text) : theme::C(theme::color::TextDim),
            r, Gdiplus::StringAlignmentCenter);
    }

    if (m_leftTab == 0) return;  // 项目页：TreeView 已显示

    int catTop = m_toolbarH + tabH;

    // 分类标签（左侧竖排）
    auto cats = BlockLibrary::Categories();
    int catW = 78;
    for (int i = 0; i < (int)cats.size(); ++i) {
        Gdiplus::RectF r(0, (float)(catTop + i * 52), (float)catW, 52);
        bool active = (i == m_palCat);
        if (active) {
            Gdiplus::SolidBrush ab(theme::C(theme::color::CanvasBg));
            g.FillRectangle(&ab, r);
            Gdiplus::SolidBrush bar(theme::C(theme::color::Accent));
            g.FillRectangle(&bar, 0.0f, r.Y, 3.0f, r.Height);
        }
        uint32_t col = cats[i].color;
        Gdiplus::SolidBrush dot(theme::C(col));
        g.FillEllipse(&dot, 14.0f, r.Y + 18.0f, 12.0f, 12.0f);
        theme::DrawText(g, Tr(cats[i].label), *m_fontSmall,
            active ? theme::C(theme::color::Text) : theme::C(theme::color::TextDim),
            Gdiplus::RectF(34, r.Y, catW - 34, r.Height));
    }

    // 积木列表
    auto blocks = BlockLibrary::BlocksInCategory(cats[m_palCat].id);
    m_palBlockRects.clear();
    float y = (float)catTop + 16 - m_palScroll;
    for (int i = 0; i < (int)blocks.size(); ++i) {
        std::wstring label = Tr(blocks[i].label);
        float bw = theme::MeasureText(g, label, *m_fontSmall).Width + 26;
        if (bw < 60) bw = 60;
        RECT r{ catW + 12, (int)y, (int)(catW + 12 + bw), (int)(y + 30) };
        m_palBlockRects.push_back(r);
        uint32_t col = CategoryColor(blocks[i].category);
        theme::DrawShadow(g, Gdiplus::RectF((float)r.left, (float)r.top, (float)(r.right - r.left), 30), 7, 22, 1);
        Gdiplus::SolidBrush bb(theme::C(col));
        theme::FillRoundRect(g, Gdiplus::RectF((float)r.left, (float)r.top, (float)(r.right - r.left), 30), 8, bb);
        theme::DrawText(g, label, *m_fontSmall, theme::C(0xFFFFFFFF),
            Gdiplus::RectF((float)r.left, (float)r.top, (float)(r.right - r.left), 30),
            Gdiplus::StringAlignmentCenter);
        y += 38;
    }
}

void MainWindow::DrawCanvas(Gdiplus::Graphics& g, int x, int y, int w, int h) {
    Gdiplus::RectF area((float)x, (float)y, (float)w, (float)h);
    AppSettings& settings = AppSettings::Instance();

    if (settings.backgroundStyle == 2 && !settings.backgroundImage.empty()) {
        // 图片背景（拉伸铺满）
        if (!m_bgImage || m_bgImagePath != settings.backgroundImage) {
            delete m_bgImage;
            m_bgImage = Gdiplus::Image::FromFile(settings.backgroundImage.c_str());
            m_bgImagePath = settings.backgroundImage;
        }
        if (m_bgImage && m_bgImage->GetLastStatus() == Gdiplus::Ok) {
            g.DrawImage(m_bgImage, area);
        } else {
            Gdiplus::SolidBrush bg(theme::C(theme::color::CanvasBg));
            g.FillRectangle(&bg, area);
        }
    } else {
        // 纯色 / 网格背景（颜色可自定义）
        uint32_t bgCol = ParseHexColor(settings.backgroundColor);
        Gdiplus::SolidBrush bg(theme::C(bgCol));
        g.FillRectangle(&bg, area);
        if (settings.backgroundStyle == 1) theme::DrawGrid(g, area, 24);
    }

    // 构建布局（含滚动偏移）
    BuildLayout(g);

    // 错误跳转：滚动到选中积木（重新布局一次使其可见）
    if (m_scrollToSelection && m_selBlock) {
        m_scrollToSelection = false;
        for (auto* n : m_flat) {
            if (n->block == m_selBlock) {
                m_canvasScrollY += n->total.Y - m_toolbarH - kCanvasMargin;
                if (m_canvasScrollY < 0) m_canvasScrollY = 0;
                break;
            }
        }
        BuildLayout(g);
    }

    // 选中高亮
    if (m_selBlock) {
        for (auto* n : m_flat) {
            if (n->block == m_selBlock) {
                Gdiplus::Pen pen(theme::C(theme::color::Selection), 3);
                Gdiplus::GraphicsPath path;
                theme::AddRoundRect(path, n->total, kRadius + 2);
                g.DrawPath(&pen, &path);
                break;
            }
        }
    }

    // 拖拽幽灵
    if (m_dragging && m_dragValid) {
        DrawBlockGhost(g, m_dragBlock, (float)m_dragCur.x, (float)m_dragCur.y);
    }
}

void MainWindow::DrawStatus(Gdiplus::Graphics& g, int w, int h) {
    int y = h - m_statusH;
    Gdiplus::SolidBrush bg(theme::C(theme::color::PanelBg));
    g.FillRectangle(&bg, 0, y, w, m_statusH);
    Gdiplus::SolidBrush border(theme::C(theme::color::Border));
    g.FillRectangle(&border, 0, y, w, 1);

    std::wstring toolchain = m_tccReady ? L"TCC 已就绪" : L"TCC 未就绪";
    std::wstring jdk = m_jdkReady ? L"JDK 已就绪" : L"JDK 未检测到";
    std::wstring msvc = m_msvcReady ? L"MSVC 已就绪" : L"MSVC 待检测";
    std::wstring lang = (m_target == TargetLang::Java) ? L"目标: Java"
                      : (m_target == TargetLang::Cpp) ? L"目标: C++"
                      : L"目标: C";
    std::wstring status = m_status.empty()
        ? (toolchain + L"  |  " + msvc + L"  |  " + jdk + L"  |  " + lang)
        : (m_status + L"   |   " + toolchain + L"  |  " + msvc + L"  |  " + jdk);
    theme::DrawText(g, status, *m_fontSmall, theme::C(theme::color::TextDim),
        Gdiplus::RectF(12, (float)y, (float)(w - 24), (float)m_statusH),
        Gdiplus::StringAlignmentNear);
    theme::DrawText(g, L"项目: " + Utf8ToWide(m_project), *m_fontSmall, theme::C(theme::color::TextDim),
        Gdiplus::RectF((float)(w - 320), (float)y, 300, (float)m_statusH),
        Gdiplus::StringAlignmentFar);
}

void MainWindow::DrawOutput(Gdiplus::Graphics& g, int w, int h) {
    int y = h - m_statusH - m_outputH;
    Gdiplus::SolidBrush bg(theme::C(theme::color::PanelBg));
    g.FillRectangle(&bg, 0, y, w, m_outputH);
    Gdiplus::SolidBrush border(theme::C(theme::color::Border));
    g.FillRectangle(&border, 0, y, w, 1);

    // 标题
    theme::DrawText(g, L"输出", *m_fontSmall, theme::C(theme::color::TextDim),
        Gdiplus::RectF(12, (float)y, 80, 20));

    // 按 \n 分行
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (wchar_t c : m_outputText) {
        if (c == L'\n') { lines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.empty()) lines.push_back(L"（无输出）");

    // 显示最近能容纳的行
    const float lineH = 17.0f;
    int maxLines = (int)((m_outputH - 24) / lineH);
    if (maxLines < 1) maxLines = 1;
    int start = (int)lines.size() > maxLines ? (int)lines.size() - maxLines : 0;
    float ty = (float)y + 24;
    for (int i = start; i < (int)lines.size(); ++i) {
        theme::DrawText(g, lines[i], *m_fontSmall, theme::C(theme::color::Text),
            Gdiplus::RectF(12, ty, (float)(w - 24), lineH));
        ty += lineH;
    }
}

// 绘制拖拽中的积木（半透明）
void MainWindow::DrawBlockGhost(Gdiplus::Graphics& g, const Block& b, float x, float y) {
    // 简化：绘制一个半透明圆角块
    float w = MeasureHeader(g, b);
    if (w < 60) w = 60;
    Gdiplus::SolidBrush bb(Gdiplus::Color(160, 45, 127, 249));
    theme::FillRoundRect(g, Gdiplus::RectF(x, y, w, kBlockH), kRadius, bb);
    std::wstring label = Tr(b.label);
    theme::DrawText(g, label, *m_fontBlock, Gdiplus::Color(255, 255, 255, 255),
        Gdiplus::RectF(x + 8, y, w - 16, kBlockH), Gdiplus::StringAlignmentCenter);
}

// ============================================================
// 布局
// ============================================================
void MainWindow::BuildLayout(Gdiplus::Graphics& g) {
    m_nodes.clear();
    m_flat.clear();
    float cx = (float)m_paletteW + kCanvasMargin;
    float cy = (float)m_toolbarH + kCanvasMargin - m_canvasScrollY;
    for (size_t i = 0; i < m_program.blocks.size(); ++i) {
        m_nodes.push_back(Node());
        Node& n = m_nodes.back();
        LayoutDraw(g, m_program.blocks[i], &m_program.blocks, i, cx, cy, n);
        cy += n.total.Height + kGap;
    }
    // 记录内容高度（含滚动）
    m_canvasContentH = cy - ((float)m_toolbarH + kCanvasMargin - m_canvasScrollY);
    // 扁平化
    for (auto& n : m_nodes) Flatten(n);
}

void MainWindow::Flatten(Node& n) {
    m_flat.push_back(&n);
    for (auto& r : n.children) Flatten(r);
    for (auto& r : n.elseChildren) Flatten(r);
}

float MainWindow::MeasureHeader(Gdiplus::Graphics& g, const Block& b) {
    if (b.typeId == "num" || b.typeId == "var") {
        std::wstring t = Utf8ToWide(b.params.empty() ? std::string("0") : b.params[0].text);
        return kPadX * 2 + theme::MeasureText(g, t, *m_fontBlock).Width + kSlotPadX * 2;
    }
    auto segs = SplitLabel(b.label);
    float w = kPadX * 2;
    for (const auto& s : segs) {
        if (!s.param) {
            w += theme::MeasureText(g, s.text, *m_fontBlock).Width;
        } else if (s.idx < (int)b.params.size()) {
            const Param& p = b.params[s.idx];
            if (p.type == ParamType::Reporter) {
                if (p.reporter) w += MeasureHeader(g, *p.reporter) + kReporterGap * 2;
                else w += 20;
            } else {
                std::wstring t = Utf8ToWide(p.text);
                w += theme::MeasureText(g, t, *m_fontBlock).Width + kSlotPadX * 2;
            }
        }
    }
    return w;
}

void MainWindow::LayoutDraw(Gdiplus::Graphics& g, Block& b, std::vector<Block>* list, size_t index,
                            float x, float y, Node& out) {
    out.block = &b;
    out.list = list;
    out.index = index;

    float hw = MeasureHeader(g, b);
    if (IsContainer(b)) {
        float childX = x + kIndent;
        float cy = y + kBlockH + kBodyTopPad;
        float childW = 0;
        for (size_t i = 0; i < b.children.size(); ++i) {
            out.children.push_back(Node());
            Node& cn = out.children.back();
            LayoutDraw(g, b.children[i], &b.children, i, childX, cy, cn);
            cy += cn.total.Height + kGap;
            childW = (std::max)(childW, cn.total.Width);
        }
        float elseW = 0;
        float elseH = 0;
        if (b.typeId == "ifElse" && !b.elseChildren.empty()) {
            cy += kElseBarH;
            float ey = cy;
            for (size_t i = 0; i < b.elseChildren.size(); ++i) {
                out.elseChildren.push_back(Node());
                Node& en = out.elseChildren.back();
                LayoutDraw(g, b.elseChildren[i], &b.elseChildren, i, childX, ey, en);
                ey += en.total.Height + kGap;
                elseW = (std::max)(elseW, en.total.Width);
                elseH += en.total.Height + kGap;
            }
        }
        float bodyW = (std::max)(hw, (std::max)(childW, elseW) + kIndent + kPadX);
        float bodyH = kBodyTopPad + (cy - y - kBlockH - kBodyTopPad) + (b.typeId == "ifElse" && !b.elseChildren.empty() ? kElseBarH + elseH : 0) + kBodyBottomPad;
        // 重新计算 bodyH：直接用子块累计高度
        float thenH = 0;
        for (auto& cn : out.children) thenH += cn.total.Height + kGap;
        bodyH = kBodyTopPad + thenH + (b.typeId == "ifElse" && !b.elseChildren.empty() ? kElseBarH + elseH : 0) + kBodyBottomPad;

        out.rect = Gdiplus::RectF(x, y, bodyW, kBlockH);
        out.body = Gdiplus::RectF(x, y + kBlockH - 8, bodyW, bodyH + 8);
        out.total = Gdiplus::RectF(x, y, bodyW, kBlockH + bodyH);
    } else {
        out.rect = Gdiplus::RectF(x, y, hw, kBlockH);
        out.total = out.rect;
    }

    // 绘制
    DrawNode(g, out);
}

void MainWindow::DrawNode(Gdiplus::Graphics& g, Node& n) {
    Block& b = *n.block;
    uint32_t col = CategoryColor(b.category);
    bool container = IsContainer(b);

    theme::DrawShadow(g, n.total, kRadius, 26, 2);

    if (container) {
        Gdiplus::SolidBrush bb(theme::C(col));
        theme::FillRoundRect(g, n.body, kRadius, bb);
        Gdiplus::SolidBrush hb(theme::C(col));
        theme::FillRoundRect(g, n.rect, kRadius, hb);
    } else {
        Gdiplus::SolidBrush hb(theme::C(col));
        theme::FillRoundRect(g, n.rect, kRadius, hb);
    }

    // 头部内容
    DrawHeaderContent(g, b, n.rect, col, n, false);

    // else 分隔条
    if (b.typeId == "ifElse" && !n.elseChildren.empty()) {
        float ey = n.elseChildren[0].rect.Y - kElseBarH - 3;
        Gdiplus::SolidBrush eb(theme::C(theme::Blend(col, 0x1F000000)));
        theme::FillRoundRect(g, Gdiplus::RectF(n.rect.X + kIndent, ey, 64, kElseBarH), kElseBarH / 2, eb);
        theme::DrawText(g, Tr("else"), *m_fontSmall, theme::C(0xFFFFFFFF),
            Gdiplus::RectF(n.rect.X + kIndent, ey, 64, kElseBarH), Gdiplus::StringAlignmentCenter);
    }

    for (auto& cn : n.children) DrawNode(g, cn);
    for (auto& en : n.elseChildren) DrawNode(g, en);
}

void MainWindow::DrawHeaderContent(Gdiplus::Graphics& g, Block& b, const Gdiplus::RectF& rect,
                                   uint32_t col, Node& out, bool isReporter) {
    float h = rect.Height;
    if (b.typeId == "num" || b.typeId == "var") {
        std::wstring t = Utf8ToWide(b.params.empty() ? std::string("0") : b.params[0].text);
        theme::DrawText(g, t, *m_fontBlock, theme::C(0xFFFFFFFF), rect, Gdiplus::StringAlignmentCenter);
        out.paramRects.push_back({ rect, 0 });
        return;
    }
    auto segs = SplitLabel(b.label);
    float cx = rect.X + kPadX;
    for (const auto& s : segs) {
        if (!s.param) {
            float tw = theme::MeasureText(g, s.text, *m_fontBlock).Width + 1;
            theme::DrawText(g, s.text, *m_fontBlock, theme::C(0xFFFFFFFF),
                Gdiplus::RectF(cx, rect.Y, tw, h), Gdiplus::StringAlignmentNear);
            cx += tw;
        } else if (s.idx < (int)b.params.size()) {
            Param& p = b.params[s.idx];
            if (p.type == ParamType::Reporter) {
                float ry = rect.Y + (h - kReporterH) / 2;
                if (p.reporter) {
                    float rw = DrawReporterInline(g, *p.reporter, cx, ry, out);
                    cx += rw + kReporterGap * 2;
                } else {
                    cx += 20;
                }
            } else {
                std::wstring t = Utf8ToWide(p.text);
                float tw = theme::MeasureText(g, t, *m_fontBlock).Width + kSlotPadX * 2;
                Gdiplus::RectF slot(cx, rect.Y + (h - kSlotH) / 2, tw, kSlotH);
                Gdiplus::SolidBrush sb(theme::C(0x33FFFFFF));
                theme::FillRoundRect(g, slot, kSlotH / 2, sb);
                theme::DrawText(g, t, *m_fontBlock, theme::C(0xFFFFFFFF),
                    Gdiplus::RectF(cx + kSlotPadX, rect.Y, tw - kSlotPadX * 2, h),
                    Gdiplus::StringAlignmentCenter);
                out.paramRects.push_back({ slot, s.idx });
                cx += tw;
            }
        }
    }
}

float MainWindow::DrawReporterInline(Gdiplus::Graphics& g, Block& rep, float x, float y, Node& out) {
    float w = MeasureHeader(g, rep);
    if (w < 20) w = 20;
    uint32_t col = CategoryColor(rep.category);
    Gdiplus::RectF pill(x, y, w, kReporterH);
    Gdiplus::SolidBrush bb(theme::C(col));
    theme::FillRoundRect(g, pill, kReporterH / 2, bb);
    DrawHeaderContent(g, rep, pill, col, out, true);
    if (rep.typeId == "num" || rep.typeId == "var") {
        out.reporterRects.push_back({ pill, &rep });
    }
    return w;
}

// ============================================================
// 命中测试
// ============================================================
Node* MainWindow::HitBlock(int x, int y) {
    for (auto it = m_flat.rbegin(); it != m_flat.rend(); ++it) {
        Node* n = *it;
        if (x >= n->total.X && x <= n->total.X + n->total.Width &&
            y >= n->total.Y && y <= n->total.Y + n->total.Height) {
            return n;
        }
    }
    return nullptr;
}

bool MainWindow::HitParam(int x, int y, Block*& block, int& paramIndex, bool& isReporter, Block*& reporterBlock) {
    for (auto it = m_flat.rbegin(); it != m_flat.rend(); ++it) {
        Node* n = *it;
        for (auto& r : n->reporterRects) {
            if (x >= r.rect.X && x <= r.rect.X + r.rect.Width && y >= r.rect.Y && y <= r.rect.Y + r.rect.Height) {
                block = n->block; isReporter = true; reporterBlock = r.block; paramIndex = 0;
                return true;
            }
        }
        for (auto& pr : n->paramRects) {
            const Gdiplus::RectF& r = pr.first;
            if (x >= r.X && x <= r.X + r.Width && y >= r.Y && y <= r.Y + r.Height) {
                block = n->block; isReporter = false; reporterBlock = nullptr; paramIndex = pr.second;
                return true;
            }
        }
    }
    return false;
}

int MainWindow::HitToolbar(int x, int y) {
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const RECT& r = m_buttons[i];
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return (int)i;
    }
    return -1;
}

int MainWindow::HitPalette(int x, int y, int h) {
    // 分类
    auto cats = BlockLibrary::Categories();
    int catTop = m_toolbarH + 26;
    for (int i = 0; i < (int)cats.size(); ++i) {
        if (x >= 0 && x <= 78 && y >= catTop + i * 52 && y <= catTop + i * 52 + 52) return 1000 + i;
    }
    // 积木
    for (size_t i = 0; i < m_palBlockRects.size(); ++i) {
        const RECT& r = m_palBlockRects[i];
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return (int)i;
    }
    return -1;
}

// ============================================================
// 交互
// ============================================================
void MainWindow::OnMouseMove(int x, int y) {
    if (m_dragging) {
        m_dragCur = { x, y };
        m_dragValid = true;
        // 计算目标
        m_dragTarget = nullptr;
        m_dragIntoBody = false;
        for (auto it = m_flat.rbegin(); it != m_flat.rend(); ++it) {
            Node* n = *it;
            if (IsExcluded(n->block)) continue;
            if (x >= n->total.X && x <= n->total.X + n->total.Width &&
                y >= n->total.Y && y <= n->total.Y + n->total.Height) {
                m_dragTarget = n->block;
                // 容器体检测
                if (IsContainer(*n->block) && n->body.Height > 0 &&
                    y >= n->body.Y && y <= n->body.Y + n->body.Height) {
                    m_dragIntoBody = true;
                } else {
                    m_dragIntoBody = false;
                }
                break;
            }
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    int hb = HitToolbar(x, y);
    if (hb != m_hoverButton) {
        m_hoverButton = hb;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MainWindow::OnLButtonDown(int x, int y) {
    // 结束正在进行的编辑
    if (m_editing) { SendMessageW(m_hwnd, WM_COMMAND, MAKEWPARAM(0, EN_KILLFOCUS), (LPARAM)m_editParam); }

    // 工具栏
    int tb = HitToolbar(x, y);
    if (tb >= 0) { RunAction(tb); return; }

    // 左侧标签栏（项目 / 积木）
    if (x < m_paletteW && y >= m_toolbarH && y <= m_toolbarH + 26) {
        int newTab = (x < m_paletteW / 2) ? 0 : 1;
        if (newTab != m_leftTab) {
            m_leftTab = newTab;
            if (m_leftTab == 0) RefreshProjectTree();
            RECT rc; GetClientRect(m_hwnd, &rc);
            OnSize(rc.right, rc.bottom);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    // 调色板
    if (x < m_paletteW && y > m_toolbarH) {
        int hit = HitPalette(x, y, 0);
        if (hit >= 1000) { m_palCat = hit - 1000; InvalidateRect(m_hwnd, nullptr, FALSE); return; }
        if (hit >= 0) {
            auto cats = BlockLibrary::Categories();
            auto blocks = BlockLibrary::BlocksInCategory(cats[m_palCat].id);
            if (hit < (int)blocks.size()) {
                Block nb = BlockLibrary::MakeBlock(blocks[hit].typeId);
                m_program.blocks.push_back(nb);
                m_selBlock = &m_program.blocks.back();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return;
        }
        return;
    }

    // 画布
    Node* n = HitBlock(x, y);
    if (n) {
        m_selBlock = n->block;
        // 开始拖拽
        m_dragging = true;
        m_dragValid = false;
        m_dragStart = { x, y };
        m_dragCur = { x, y };
        m_dragBlock = *n->block;
        m_dragSrcList = n->list;
        m_dragSrcIndex = n->index;
        CollectExcluded(n->block);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else {
        ClearSelection();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MainWindow::OnLButtonUp(int x, int y) {
    if (m_dragging) {
        m_dragging = false;
        if (m_dragValid && m_dragTarget && m_dragSrcList) {
            Node* targetNode = nullptr;
            for (auto* n : m_flat) if (n->block == m_dragTarget) { targetNode = n; break; }
            if (targetNode) MoveBlock(m_dragBlock, *targetNode, m_dragIntoBody);
        }
        m_dragTarget = nullptr;
        m_dragExcluded.clear();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MainWindow::OnLButtonDblClk(int x, int y) {
    Block* block = nullptr; int pi = -1; bool isRep = false; Block* rb = nullptr;
    if (HitParam(x, y, block, pi, isRep, rb)) {
        m_editing = true;
        m_editBlock = block;
        m_editParamIndex = pi;
        m_editIsReporter = isRep;
        m_editReporterBlock = rb;
        // 定位编辑框（找不到则用安全默认位置，避免未初始化矩形）
        Gdiplus::RectF r(0, 0, 40, 22);
        if (isRep && rb) {
            for (auto* n : m_flat) {
                for (auto& rr : n->reporterRects) if (rr.block == rb) { r = rr.rect; break; }
            }
        } else {
            for (auto* n : m_flat) {
                if (n->block == block) {
                    for (auto& pr : n->paramRects) if (pr.second == pi) { r = pr.first; break; }
                    break;
                }
            }
        }
        std::string cur = isRep && rb ? (rb->params.empty() ? "" : rb->params[0].text)
                                      : (block && pi >= 0 && pi < (int)block->params.size() ? block->params[pi].text : "");
        MoveWindow(m_editParam, (int)r.X, (int)r.Y, (int)(r.Width < 40 ? 40 : r.Width), (int)r.Height, TRUE);
        SetWindowTextW(m_editParam, Utf8ToWide(cur).c_str());
        ShowWindow(m_editParam, SW_SHOW);
        SetFocus(m_editParam);
        SendMessageW(m_editParam, EM_SETSEL, 0, -1);
    }
}

void MainWindow::OnKeyDown(WPARAM key) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // Delete：删除选中积木
    if (key == VK_DELETE && m_selBlock) {
        DeleteSelected();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    // F5：编译并运行
    if (key == VK_F5) { ActionCompileAndRun(); return; }
    // Ctrl+S：保存
    if (ctrl && key == 'S') { ActionSave(); return; }
    // Ctrl+Shift+B 或 Ctrl+B：编译
    if (ctrl && key == 'B') { ActionCompile(); return; }
    // Ctrl+N：新建项目
    if (ctrl && key == 'N') { ActionNewProject(); return; }
    // Ctrl+D：反编译
    if (ctrl && key == 'D') { ActionDecompile(); return; }
    // Ctrl+L：库商城
    if (ctrl && key == 'L') { ActionStoreLibrary(); return; }
}

bool MainWindow::IsExcluded(Block* b) {
    for (auto* e : m_dragExcluded) if (e == b) return true;
    return false;
}

void MainWindow::CollectExcluded(Block* b) {
    m_dragExcluded.clear();
    std::function<void(Block*)> walk = [&](Block* x) {
        m_dragExcluded.push_back(x);
        for (auto& r : x->reporters) walk(&r);
        for (auto& c : x->children) walk(&c);
        for (auto& c : x->elseChildren) walk(&c);
    };
    walk(b);
}

// ============================================================
// 树操作
// ============================================================
void MainWindow::ClearSelection() { m_selBlock = nullptr; }

void MainWindow::DeleteSelected() {
    if (!m_selBlock) return;
    for (auto* n : m_flat) {
        if (n->block == m_selBlock) {
            if (n->list && n->index < n->list->size()) n->list->erase(n->list->begin() + n->index);
            m_selBlock = nullptr;
            return;
        }
    }
}

void MainWindow::MoveBlock(Block b, Node& target, bool intoBody) {
    std::vector<Block>* srcList = m_dragSrcList;
    size_t srcIdx = m_dragSrcIndex;
    if (!srcList || srcIdx >= srcList->size()) return;

    // 目标为容器体
    if (intoBody && target.block && IsContainer(*target.block)) {
        // 不能拖入自身
        if (target.block == m_selBlock) return;
        // 关键：erase 可能使 target.block 悬空（若目标容器与源块同列表且位置在源块之后，
        //       erase 会导致 vector 中后续元素前移）。先判断是否需要重新定位。
        bool targetShifts = (target.list == srcList && target.index > srcIdx);
        srcList->erase(srcList->begin() + srcIdx);
        Block* container = target.block;
        if (targetShifts) container = &(*srcList)[target.index - 1];
        container->children.push_back(b);
        m_selBlock = &container->children.back();
        return;
    }

    std::vector<Block>* dstList = target.list;
    size_t dstIdx = target.index;
    // 拖到目标下方 → 插到其后
    POINT p = m_dragCur;
    if (p.y > target.rect.Y + target.rect.Height / 2) dstIdx++;

    if (dstList == srcList && dstIdx > srcIdx) dstIdx--;
    srcList->erase(srcList->begin() + srcIdx);
    if (dstIdx > dstList->size()) dstIdx = dstList->size();
    dstList->insert(dstList->begin() + dstIdx, b);
    m_selBlock = &(*dstList)[dstIdx];
}

// ============================================================
// 动作
// ============================================================
void MainWindow::RunAction(int id) {
    switch (id) {
    case 0: ActionNewProject(); break;
    case 1: ActionSave(); break;
    case 2: ActionCompile(); break;
    case 3: ActionDecompile(); break;
    case 4: ActionBuildDll(); break;
    case 5: ActionScanLibrary(); break;
    case 6: ActionGenerateCoreTranslation(); break;
    case 7: DeleteSelected(); InvalidateRect(m_hwnd, nullptr, FALSE); break;
    case 8: ActionStoreLibrary(); break;
    case 9: ActionCompileAndRun(); break;
    case 10: ActionCompileSettings(); break;
    }
}

void MainWindow::ActionNewProject() {
    int lang = (m_target == TargetLang::Java) ? 2 : (m_target == TargetLang::Cpp) ? 1 : 0;
    std::wstring name = PromptNewProject(m_hwnd, Utf8ToWide(m_project), lang);
    std::string nm = WideToUtf8(name);
    if (nm.empty() || !ProjectStore::IsValidProjectName(nm)) {
        SetStatus(L"项目名无效");
        return;
    }
    // 应用目标语言
    if (lang == 1) m_target = TargetLang::Cpp;
    else if (lang == 2) m_target = TargetLang::Java;
    else m_target = TargetLang::C;
    if (m_comboTarget) SendMessageW(m_comboTarget, CB_SETCURSEL, lang, 0);

    m_project = nm;
    m_program = Program();
    m_program.name = nm;
    m_selBlock = nullptr;
    SaveProgram();
    SetStatus(L"已新建项目: " + Utf8ToWide(nm));
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::ActionSave() {
    SaveProgram();
    SetStatus(L"已保存 blocks.json");
}

void MainWindow::ActionCompile() {
    if (m_target == TargetLang::Java) { ActionCompileJava(); return; }
    // 输出类型为 .dll 时，走 DLL 编译（DllBuilder + TCC）
    if (m_compile.output == OutputType::Dll) { ActionBuildDll(); return; }
    if (m_target == TargetLang::Cpp) ActionCompileCpp();
    else ActionCompileC();
}

void MainWindow::ActionCompileAndRun() {
    // 编译并运行：Java 已内置运行；C/C++ 强制编译 exe 并在成功后运行
    if (m_target == TargetLang::Java) { ActionCompileJava(); return; }
    CompileConfig saved = m_compile;
    m_compile.output = OutputType::Exe;
    m_compile.runAfterBuild = true;
    if (m_target == TargetLang::Cpp) ActionCompileCpp();
    else ActionCompileC();
    m_compile = saved;
}

void MainWindow::ActionCompileSettings() {
    AppSettings& settings = AppSettings::Instance();
    if (ShowSettingsDialog(m_hwnd, m_compile, settings)) {
        settings.compile = m_compile;
        std::string e;
        settings.Save(e);
        SetStatus(L"设置已保存");
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MainWindow::ActionCompileC() {
    std::string err;
    std::vector<int> lineStarts;
    std::string code = CodeGenerator::GenerateProgram(m_program, TargetLang::C, err, &lineStarts);
    if (!err.empty()) {
        SetOutput(L"代码生成失败:\n" + Utf8ToWide(err));
        SetStatus(L"代码生成失败");
        return;
    }
    std::wstring cPath = ProjectStore::SrcDir(m_project) + Utf8ToWide(m_project) + L".c";
    WriteFileText(cPath, code);
    if (!m_tccReady) m_tccReady = TccCompiler::Init(err);
    if (!m_tccReady) {
        SetOutput(L"内置编译器不可用:\n" + Utf8ToWide(err));
        SetStatus(L"内置编译器不可用");
        return;
    }
    std::wstring exePath = ProjectStore::OutDir(m_project) + Utf8ToWide(m_project) + L".exe";
    std::string log;
    // 传额外头文件搜索目录（libs\c），使「头文件」积木可解析
    bool ok = TccCompiler::CompileToExe(code, exePath, log, ProjectStore::LibsDir(TargetLang::C));
    if (ok) {
        if (m_compile.runAfterBuild) {
            std::string out;
            RunExecutable(exePath, out);
            SetOutput(L"编译成功: " + exePath + L"\n\n=== 运行输出 ===\n" + Utf8ToWide(out));
            SetStatus(L"编译并运行成功: " + exePath);
        } else {
            SetOutput(L"编译成功: " + exePath);
            SetStatus(L"编译成功: " + exePath);
        }
    } else {
        SetOutput(Utf8ToWide(log));
        SetStatus(L"编译失败，详见输出面板");
        SelectBlockByError(log, lineStarts);
    }
}

void MainWindow::ActionCompileCpp() {
    std::string err;
    std::vector<int> lineStarts;
    std::string code = CodeGenerator::GenerateProgram(m_program, TargetLang::Cpp, err, &lineStarts);
    if (!err.empty()) {
        SetOutput(L"代码生成失败:\n" + Utf8ToWide(err));
        SetStatus(L"代码生成失败");
        return;
    }
    std::wstring cppPath = ProjectStore::SrcDir(m_project) + Utf8ToWide(m_project) + L".cpp";
    WriteFileText(cppPath, code);
    if (!m_msvcReady) m_msvcReady = MsvcCompiler::Init(err);
    if (!m_msvcReady) {
        SetOutput(L"未检测到 MSVC:\n" + Utf8ToWide(err));
        SetStatus(L"未检测到 MSVC（cl.exe）");
        return;
    }
    std::wstring exePath = ProjectStore::OutDir(m_project) + Utf8ToWide(m_project) + L".exe";
    std::string log;
    // 传编译参数（/MT /MTd /O2 /W3 等）到 MSVC
    bool ok = MsvcCompiler::CompileFile(cppPath, exePath, log, m_compile.MsBuildFlags());
    if (ok) {
        if (m_compile.runAfterBuild) {
            std::string out;
            RunExecutable(exePath, out);
            SetOutput(L"C++ 编译成功: " + exePath + L"\n\n=== 运行输出 ===\n" + Utf8ToWide(out));
            SetStatus(L"C++ 编译并运行成功: " + exePath);
        } else {
            SetOutput(L"C++ 编译成功: " + exePath);
            SetStatus(L"C++ 编译成功: " + exePath);
        }
    } else {
        SetOutput(Utf8ToWide(log));
        SetStatus(L"C++ 编译失败，详见输出面板");
        SelectBlockByError(log, lineStarts);
    }
}

void MainWindow::ActionCompileJava() {
    std::string err;
    // 类名与文件名必须一致：统一用项目名规范化
    std::string cls = CodeGenerator::SanitizeJavaClassName(m_project);
    Program prog = m_program;
    prog.name = cls;

    std::vector<int> lineStarts;
    std::string code = CodeGenerator::GenerateProgram(prog, TargetLang::Java, err, &lineStarts);
    if (!err.empty()) {
        SetOutput(L"代码生成失败:\n" + Utf8ToWide(err));
        SetStatus(L"代码生成失败");
        return;
    }
    std::wstring javaPath = ProjectStore::SrcDir(m_project) + Utf8ToWide(cls) + L".java";
    WriteFileText(javaPath, code);

    if (!m_jdkReady) m_jdkReady = JdkCompiler::Init(err);
    if (!m_jdkReady) {
        SetOutput(L"未检测到 JDK:\n" + Utf8ToWide(err));
        SetStatus(L"未检测到 JDK（javac）");
        return;
    }

    std::wstring outDir = ProjectStore::OutDir(m_project);
    std::string log;
    bool ok = JdkCompiler::Compile(javaPath, outDir, log);
    if (!ok) {
        SetOutput(Utf8ToWide(log));
        SetStatus(L"Java 编译失败，详见输出面板");
        SelectBlockByError(log, lineStarts);
        return;
    }

    // 编译成功，运行并显示输出
    std::string runLog;
    bool ran = JdkCompiler::Run(outDir, cls, runLog);
    std::wstring out = L"Java 编译成功: " + outDir + L"\n源码: " + javaPath;
    if (ran) out += L"\n\n=== 运行输出 ===\n" + Utf8ToWide(runLog);
    SetOutput(out);
    SetStatus(L"Java 编译成功: " + outDir);
}

void MainWindow::ActionDecompile() {
    // 根据当前目标语言选择文件过滤器和反编译语言
    std::wstring filter = (m_target == TargetLang::Java)
        ? L"Java 源码 (*.java)\0*.java\0C/C++ 源码 (*.c;*.cpp;*.h)\0*.c;*.cpp;*.h\0所有文件 (*.*)\0*.*\0"
        : L"C/C++ 源码 (*.c;*.cpp;*.h)\0*.c;*.cpp;*.h\0Java 源码 (*.java)\0*.java\0所有文件 (*.*)\0*.*\0";
    std::wstring path = PickOpenFile(filter.c_str());
    if (path.empty()) return;
    std::string code;
    if (!ReadFileText(path, code)) {
        SetStatus(L"无法读取文件");
        return;
    }
    // 根据扩展名推断语言
    TargetLang lang = m_target;
    std::string lower = ToLowerAscii(WideToUtf8(path));
    if (lower.find(".java") != std::string::npos) lang = TargetLang::Java;
    else if (lower.find(".cpp") != std::string::npos || lower.find(".cc") != std::string::npos) lang = TargetLang::Cpp;

    std::string err;
    Program p = Decompiler::Decompile(code, lang, err);
    if (!err.empty()) {
        SetStatus(L"反编译出错: " + Utf8ToWide(err));
    }
    m_program = p;
    m_program.name = m_project;
    m_selBlock = nullptr;
    SaveProgram();
    SetStatus(L"反编译完成，共 " + std::to_wstring(p.blocks.size()) + L" 个顶层积木");
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::ActionBuildDll() {
    std::string defaultName = "YXG_" + m_project;
    std::wstring name = PromptText(m_hwnd, L"编译为 DLL", Utf8ToWide(defaultName));
    std::string dllName = WideToUtf8(name);
    if (dllName.empty()) return;

    DllBuilder::Options opt;
    opt.dllName = dllName;
    opt.description = "Generated by VCBlocks for project " + m_project;
    std::string err;
    std::string src = DllBuilder::GenerateSource(m_program, opt, err);
    if (!err.empty()) {
        SetStatus(L"DLL 源码生成失败: " + Utf8ToWide(err));
        return;
    }
    std::wstring cPath = ProjectStore::SrcDir(m_project) + Utf8ToWide(dllName) + L".c";
    WriteFileText(cPath, src);
    // 伴生 .txt 说明
    WriteFileText(ProjectStore::OutDir(m_project) + Utf8ToWide(dllName) + L".txt", opt.description);

    if (!m_tccReady) m_tccReady = TccCompiler::Init(err);
    if (!m_tccReady) {
        SetStatus(L"内置编译器不可用: " + Utf8ToWide(err));
        MessageBoxW(m_hwnd, (L"已生成 DLL 源码，但内置编译器不可用：\n" + Utf8ToWide(err) + L"\n\n" + cPath).c_str(), L"编译DLL", MB_ICONWARNING);
        return;
    }
    std::wstring dllPath = ProjectStore::OutDir(m_project) + Utf8ToWide(dllName) + L".dll";
    std::string log;
    bool ok = TccCompiler::CompileToDll(src, dllPath, log, ProjectStore::LibsDir(TargetLang::C));
    if (ok) {
        SetStatus(L"DLL 编译成功: " + dllPath);
        MessageBoxW(m_hwnd, (L"DLL 编译成功（内置 TCC 编译器）！\n\n" + dllPath).c_str(), L"编译DLL", MB_OK | MB_ICONINFORMATION);
    } else {
        SetStatus(L"DLL 编译失败，详见日志");
        MessageBoxW(m_hwnd, Utf8ToWide(log).c_str(), L"编译DLL 失败", MB_ICONERROR);
    }
}

void MainWindow::ActionScanLibrary() {
    // 根据目标语言选择过滤器
    std::wstring filter = (m_target == TargetLang::Java)
        ? L"Java 库 (*.java)\0*.java\0C/C++ 库 (*.c;*.h;*.cpp)\0*.c;*.h;*.cpp\0所有文件 (*.*)\0*.*\0"
        : L"C/C++ 库 (*.c;*.h;*.cpp)\0*.c;*.h;*.cpp\0Java 库 (*.java)\0*.java\0所有文件 (*.*)\0*.*\0";
    std::wstring path = PickOpenFile(filter.c_str());
    if (path.empty()) return;
    std::string code;
    if (!ReadFileText(path, code)) {
        SetStatus(L"无法读取库文件");
        return;
    }
    std::string libName = ProjectStore::SanitizeLibraryName(WideToUtf8(BaseName(path)));
    std::string language, locale = m_locale;
    const LocaleInfo* li = Translator::FindLocale(m_locale);
    language = li ? li->display : m_locale;

    LibraryScanner::Result r;
    std::string err;
    if (!LibraryScanner::Scan(code, libName, language, locale, r, err)) {
        SetStatus(L"扫描失败: " + Utf8ToWide(err));
        return;
    }
    std::string json = LibraryScanner::BuildTranslationJson(r);

    // 库按语言分开放：源文件 + 翻译 json 存到 Cache\vC\libs\<lang> 目录
    std::wstring libDir = ProjectStore::LibsDir(m_target);
    std::wstring fileName = path;
    size_t slash = fileName.find_last_of(L"\\/");
    if (slash != std::wstring::npos) fileName = fileName.substr(slash + 1);
    WriteFileText(libDir + fileName, code);
    WriteFileText(libDir + Utf8ToWide(libName) + L".json", json);

    SetStatus(L"已生成库: " + libDir + L"  （" + std::to_wstring(r.functions.size()) + L" 个函数）");
    MessageBoxW(m_hwnd, (L"已在库中找到 " + std::to_wstring(r.functions.size()) +
        L" 个可用函数，按语言存入：\n\n" + libDir).c_str(), L"扫描库", MB_OK | MB_ICONINFORMATION);

    // 刷新侧边栏「头文件」积木分类
    RefreshHeaderBlocks();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::ActionStoreLibrary() {
    LibraryStorePanel panel;
    panel.ShowModal(m_inst, m_hwnd);
    SetStatus(L"库商城已关闭");
    // 商城可能安装了新库，刷新「头文件」积木分类
    RefreshHeaderBlocks();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::ActionGenerateCoreTranslation() {
    RebuildCoreTranslation();
    SetStatus(L"已生成核心库翻译: " + ProjectStore::LangPath(m_project, "core"));
    MessageBoxW(m_hwnd, (L"已生成「core.json」翻译文件（" + Utf8ToWide(m_translator.language) + L"）。").c_str(), L"生成翻译", MB_OK | MB_ICONINFORMATION);
}

// ============================================================
// 辅助
// ============================================================
void MainWindow::ReloadTranslation() {
    m_translator.Clear();
    m_translator.locale = m_locale;
    const LocaleInfo* li = Translator::FindLocale(m_locale);
    m_translator.language = li ? li->display : m_locale;
    m_translator.library = "core";
    std::string err;
    m_translator.LoadFromFile(ProjectStore::LangPath(m_project, "core"), err); // 失败则用内置词典
}

void MainWindow::RebuildCoreTranslation() {
    const LocaleInfo* li = Translator::FindLocale(m_locale);
    std::string language = li ? li->display : m_locale;

    Translator t;
    t.library = "core";
    t.language = language;
    t.locale = m_locale;
    for (const auto& c : BlockLibrary::Categories()) {
        t.strings[c.label] = t.Translate(c.label);
    }
    for (const auto& b : BlockLibrary::Blocks()) {
        if (b.label.empty()) continue;
        t.strings[b.label] = t.Translate(b.label);
    }
    // 附加常用词
    t.strings["else"] = t.Translate("else");
    t.SaveToFile(ProjectStore::LangPath(m_project, "core"));
    ReloadTranslation();
}

void MainWindow::SaveProgram() {
    WriteFileText(ProjectStore::BlocksPath(m_project), m_program.ToJsonString());
}

void MainWindow::LoadProgram() {
    std::string s;
    if (!ReadFileText(ProjectStore::BlocksPath(m_project), s)) return;
    std::string err;
    Program p;
    if (p.FromJsonString(s, err)) {
        m_program = p;
        m_program.name = m_project;
    }
}

void MainWindow::RefreshHeaderBlocks() {
    // 扫描已安装库（libs\<lang>\ 下的 .json/.h/.hpp），刷新「头文件」积木分类
    std::vector<std::pair<std::string, std::string>> funcs;
    std::vector<std::string> headers;
    std::set<std::string> seen;

    const char* langs[] = { "c", "cpp", "java" };
    for (const char* lang : langs) {
        std::wstring dir = ProjectStore::LibsDirById(lang);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string name = WideToUtf8(fd.cFileName);
            // 收集头文件名（用于 #include）
            bool isHeader = (name.size() >= 2 && name.substr(name.size() - 2) == ".h") ||
                            (name.size() >= 4 && name.substr(name.size() - 4) == ".hpp");
            if (isHeader) headers.push_back(name);
            // 解析 json 提取函数（functions 数组：name/translation/description）
            if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
                std::string text;
                if (ReadFileText(dir + fd.cFileName, text)) {
                    JsonValue root;
                    std::string e;
                    if (ParseJson(text, root, e) && root.IsObject() && root.Has("functions") &&
                        root.Get("functions").IsArray()) {
                        for (const auto& f : root.Get("functions").arr) {
                            std::string fnm = f.Get("name").AsString();
                            std::string tr = f.Get("translation").AsString();
                            if (tr.empty()) tr = f.Get("description").AsString();
                            if (fnm.empty()) continue;
                            if (!seen.count(fnm)) {
                                seen.insert(fnm);
                                funcs.push_back({ fnm, tr });
                            }
                        }
                    }
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    BlockLibrary::SetHeaderFunctions(funcs, headers);
}

void MainWindow::RefreshProjectTree() {
    if (!m_tree) return;
    SendMessageW(m_tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(m_tree);

    TVINSERTSTRUCTW tv{};
    tv.hParent = TVI_ROOT;
    tv.hInsertAfter = TVI_LAST;
    tv.item.mask = TVIF_TEXT | TVIF_PARAM;

    std::wstring projName = Utf8ToWide(m_project);
    tv.item.pszText = &projName[0];
    tv.item.lParam = (LPARAM)-1;
    HTREEITEM hProj = (HTREEITEM)SendMessageW(m_tree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

    const wchar_t* dirs[] = { L"src", L"lang", L"out" };
    const wchar_t* dlabels[] = { L"源代码 (src)", L"翻译 (lang)", L"输出 (out)" };
    for (int di = 0; di < 3; ++di) {
        tv.hParent = hProj;
        tv.item.pszText = (LPWSTR)dlabels[di];
        tv.item.lParam = (LPARAM)-2;
        HTREEITEM hDir = (HTREEITEM)SendMessageW(m_tree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

        std::wstring dirPath = ProjectStore::ProjectDir(m_project) + dirs[di] + L"\\";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dirPath + L"*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (fd.cFileName[0] == L'.') continue;
                tv.hParent = hDir;
                tv.item.pszText = fd.cFileName;
                tv.item.lParam = (LPARAM)-3;
                SendMessageW(m_tree, TVM_INSERTITEMW, 0, (LPARAM)&tv);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    SendMessageW(m_tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)hProj);
    SendMessageW(m_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_tree, nullptr, TRUE);
}

std::wstring MainWindow::PickOpenFile(const wchar_t* filter) {
    wchar_t buf[MAX_PATH * 4] = { 0 };
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) return buf;
    return L"";
}

uint32_t MainWindow::CategoryColor(const std::string& cat) {
    const CategoryDef* c = BlockLibrary::FindCategory(cat);
    return c ? c->color : 0xFF2D7FF9;
}

std::wstring MainWindow::Tr(const std::string& english) {
    return m_translator.TranslateW(english);
}

void MainWindow::SetStatus(const std::wstring& s) {
    m_status = s;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::SetOutput(const std::wstring& text) {
    m_outputText = text;
    m_outputVisible = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::SelectBlockByError(const std::string& log, const std::vector<int>& blockLines) {
    int line = ParseErrorLine(log);
    if (line <= 0 || blockLines.empty()) return;
    // 找最后一个起始行 <= 错误行号的顶层积木
    int idx = -1;
    for (int i = 0; i < (int)blockLines.size(); ++i) {
        if (blockLines[i] <= line) idx = i;
        else break;
    }
    if (idx >= 0 && idx < (int)m_program.blocks.size()) {
        m_selBlock = &m_program.blocks[idx];
        m_scrollToSelection = true;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

} // namespace vcb
