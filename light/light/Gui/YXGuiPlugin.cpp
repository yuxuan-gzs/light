// src/YXGuiPlugin.cpp
// ============================================================================
// winsock2.h 保护：必须在 windows.h 之前包含 winsock2.h，
// 否则 winsock.h 会被 windows.h 先包含，导致 WSA* / sockaddr 等重定义。
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "YXGuiPlugin.h"
#include <richedit.h>
#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <ole2.h>
#include <windowsx.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <set>
#include <atomic>
#include <mutex>
#include <map>
#include <deque>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <filesystem>

#include <wrl.h>
#include "WebView2.h"
#include "JSWebViewBridge.h"
#include "ApiAiNative.h"
#include "../PluginManager.h"
using namespace Microsoft::WRL;

// light.cpp 中的全局插件管理器（同进程共享）
extern PluginManager* g_PluginManager;

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comdlg32.lib")

namespace fs = std::filesystem;

// 原生面板侧边栏索引起点（避开 -2 主界面 / -3 AI / 0..N-1 JS 插件）
static const int NATIVE_PANEL_BASE = 1000;

// 子类过程前置声明（供头文件中的 friend 声明匹配）
LRESULT CALLBACK ToolbarButtonSubclassProc(HWND, UINT, WPARAM, LPARAM,
    UINT_PTR, DWORD_PTR);
LRESULT CALLBACK AiButtonSubclassProc(HWND, UINT, WPARAM, LPARAM,
    UINT_PTR, DWORD_PTR);

// v9.1: JSPlugin.cpp 侧提供的文件拖放转发入口
extern void YXGui_PushDroppedFiles(JSPlugin* plugin,
    const wchar_t* const* paths, int count);

// v9.2: 流式 chunk 消息负载（结构由 JSPlugin.cpp 定义）
// 返回 true 表示该 payload 已被某个插件认领并释放；
// false 表示无人认领，调用方应调用 YXGui_FreeStreamChunk 兜底释放。
extern "C" bool YXGui_ProcessStreamChunk(void* payload);
extern "C" void YXGui_FreeStreamChunk(void* payload);

// v9.3: WebView2 覆盖层（登录用）——由 JSPlugin.cpp 侧调用
extern "C" void YXGui_ShowOverlayWebView(void* self, void* plugin, const wchar_t* url);
extern "C" void YXGui_HideOverlayWebView(void* self, void* plugin);

// v9.4: 富文本链接 tag 反查
extern "C" std::wstring YXGui_QueryRichLinkTag(HWND h, LONG cpMin, LONG cpMax);
extern "C" void YXGui_SetPendingLinkTag(const std::wstring& t);

#ifndef WM_YXGUI_HTTP_STREAM
#define WM_YXGUI_HTTP_STREAM (WM_APP + 900)
#endif

// ============================================================
// YXGui 视觉系统
// ============================================================
namespace YXStyle {
    static constexpr int LEFT_WIDTH       = 280;
    static constexpr int RIGHT_WIDTH      = 190;
    static constexpr int SPLITTER_H       = 8;
    static constexpr int TOOLBAR_H        = 44;
    static constexpr int TAB_BAR_H        = 40;
    static constexpr int TOPBAR_H         = 42;

    static constexpr int AI_BTN_SIZE      = 136;
    static constexpr int AI_BTN_GAP       = 14;
    static constexpr int AI_SCROLLBAR_W   = 10;

    static constexpr int BROWSER_MIN_H    = 160;
    static constexpr int BROWSER_MAX_H    = 900;
    static constexpr int EXT_LIST_ITEM_H  = 118;
    static constexpr int PANEL_HEADER_H   = 36;

    static constexpr COLORREF BG          = RGB(244, 247, 251);
    static constexpr COLORREF PANEL       = RGB(255, 255, 255);
    static constexpr COLORREF BORDER      = RGB(222, 228, 237);
    static constexpr COLORREF MUTED       = RGB(112, 122, 138);
    static constexpr COLORREF TEXT        = RGB(35, 43, 55);
    static constexpr COLORREF BLUE        = RGB(53, 104, 188);
    static constexpr COLORREF BLUE_DARK   = RGB(36, 71, 126);
    static constexpr COLORREF BLUE_SOFT   = RGB(233, 241, 253);
    static constexpr COLORREF CONSOLE_BG  = RGB(17, 24, 39);
    static constexpr COLORREF CONSOLE_IN  = RGB(23, 32, 50);
    static constexpr COLORREF CONSOLE_TEXT= RGB(210, 218, 231);

    static constexpr COLORREF SB_BG        = RGB(28, 39, 58);
    static constexpr COLORREF SB_BG_TOP    = RGB(22, 32, 50);
    static constexpr COLORREF SB_HOVER     = RGB(48, 68, 102);
    static constexpr COLORREF SB_ACTIVE    = RGB(64, 116, 194);
    static constexpr COLORREF SB_TEXT      = RGB(220, 230, 245);
    static constexpr COLORREF SB_TEXT_DIM  = RGB(150, 168, 195);
    static constexpr COLORREF SB_DIVIDER   = RGB(48, 62, 86);
}

// ============================================================
// 前向声明
// ============================================================
static LRESULT CALLBACK ExtListSubclassProc(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

// ============================================================
// 从商店链接提取扩展 ID
// ============================================================
static std::wstring ExtractExtIdFromUrl(const std::wstring& rawUrl) {
    std::wstring url = rawUrl;
    while (!url.empty() && (url.front() == L' ' || url.front() == L'\t')) url.erase(0, 1);
    while (!url.empty() && (url.back() == L' ' || url.back() == L'\t' ||
        url.back() == L'\r' || url.back() == L'\n')) url.pop_back();
    if (url.empty()) return L"";
    size_t q = url.find_first_of(L"?#");
    if (q != std::wstring::npos) url = url.substr(0, q);
    while (!url.empty() && url.back() == L'/') url.pop_back();
    size_t slash = url.find_last_of(L'/');
    if (slash == std::wstring::npos) return L"";
    std::wstring id = url.substr(slash + 1);
    if (id.length() < 20 || id.length() > 64) return L"";
    for (wchar_t c : id) {
        if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9'))) return L"";
    }
    return id;
}

// ============================================================
// 通用输入对话框
// ============================================================
struct InputDialogData {
    std::wstring title;
    std::wstring label;
    std::wstring value;
    bool ok = false;
};

static LRESULT CALLBACK YXGuiInputDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    InputDialogData* p = (InputDialogData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        p = (InputDialogData*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)p);
        if (!p) return -1;

        HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

        HWND hLabel = CreateWindowExW(0, L"STATIC", p->label.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, 14, 460, 22, hWnd, NULL, NULL, NULL);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", p->value.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            12, 40, 460, 26, hWnd, (HMENU)100, NULL, NULL);
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hOk = CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            316, 78, 74, 28, hWnd, (HMENU)IDOK, NULL, NULL);
        SendMessageW(hOk, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hCancel = CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            398, 78, 74, 28, hWnd, (HMENU)IDCANCEL, NULL, NULL);
        SendMessageW(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetFocus(hEdit);
        return 0;
    }
    case WM_COMMAND:
        if (!p) break;
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[4096] = { 0 };
            GetDlgItemTextW(hWnd, 100, buf, 4096);
            p->value = buf;
            p->ok = true;
            DestroyWindow(hWnd);
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hWnd);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static bool ShowInputDialog(HINSTANCE hInst, HWND parent,
    const std::wstring& title,
    const std::wstring& label,
    std::wstring& value) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = YXGuiInputDialogProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"YXGuiInputDialogWnd";
        RegisterClassExW(&wc);
        reg = true;
    }

    InputDialogData data;
    data.title = title;
    data.label = label;
    data.value = value;

    RECT rcP = { 0, 0, 800, 600 };
    if (parent) GetWindowRect(parent, &rcP);
    int w = 500, h = 156;
    int x = rcP.left + ((rcP.right - rcP.left) - w) / 2;
    int y = rcP.top + ((rcP.bottom - rcP.top) - h) / 2;

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"YXGuiInputDialogWnd", title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, parent, NULL, hInst, &data);
    if (!hDlg) return false;

    ShowWindow(hDlg, SW_SHOW);
    SetForegroundWindow(hDlg);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (data.ok) {
        value = data.value;
        return true;
    }
    return false;
}

// ============================================================
// EnvironmentOptions
// ============================================================
class SimpleEnvOptions : public ICoreWebView2EnvironmentOptions,
    public ICoreWebView2EnvironmentOptions6
{
public:
    SimpleEnvOptions() : m_ref(1), m_extEnabled(TRUE) {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown ||
            riid == IID_ICoreWebView2EnvironmentOptions) {
            *ppv = static_cast<ICoreWebView2EnvironmentOptions*>(this);
        }
        else if (riid == IID_ICoreWebView2EnvironmentOptions6) {
            *ppv = static_cast<ICoreWebView2EnvironmentOptions6*>(this);
        }
        else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&m_ref); }
    STDMETHOD_(ULONG, Release)() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }

    STDMETHOD(get_AdditionalBrowserArguments)(LPWSTR* value) override {
        if (!value) return E_POINTER;
        *value = (LPWSTR)CoTaskMemAlloc(sizeof(wchar_t));
        if (*value) (*value)[0] = L'\0';
        return S_OK;
    }
    STDMETHOD(put_AdditionalBrowserArguments)(LPCWSTR) override { return S_OK; }

    STDMETHOD(get_Language)(LPWSTR* value) override {
        if (!value) return E_POINTER;
        *value = (LPWSTR)CoTaskMemAlloc(sizeof(wchar_t));
        if (*value) (*value)[0] = L'\0';
        return S_OK;
    }
    STDMETHOD(put_Language)(LPCWSTR) override { return S_OK; }

    STDMETHOD(get_TargetCompatibleBrowserVersion)(LPWSTR* value) override {
        if (!value) return E_POINTER;
        *value = (LPWSTR)CoTaskMemAlloc(sizeof(wchar_t));
        if (*value) (*value)[0] = L'\0';
        return S_OK;
    }
    STDMETHOD(put_TargetCompatibleBrowserVersion)(LPCWSTR) override { return S_OK; }

    STDMETHOD(get_AllowSingleSignOnUsingOSPrimaryAccount)(BOOL* allow) override {
        if (allow) *allow = FALSE;
        return S_OK;
    }
    STDMETHOD(put_AllowSingleSignOnUsingOSPrimaryAccount)(BOOL) override { return S_OK; }

    STDMETHOD(get_AreBrowserExtensionsEnabled)(BOOL* value) override {
        if (value) *value = m_extEnabled;
        return S_OK;
    }
    STDMETHOD(put_AreBrowserExtensionsEnabled)(BOOL value) override {
        m_extEnabled = value;
        return S_OK;
    }
private:
    LONG m_ref;
    BOOL m_extEnabled;
};

// ============================================================
// 编码 / 路径
// ============================================================
static std::wstring AnsiToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), NULL, 0);
    UINT cp = CP_UTF8;
    if (len <= 0) {
        cp = CP_ACP;
        len = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), NULL, 0);
        if (len <= 0) return L"";
    }
    std::wstring out(len, 0);
    MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
        s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], len, NULL, NULL);
    return out;
}

static std::string GetExeDir() {
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir(exePath);
    size_t p = exeDir.find_last_of("\\/");
    if (p != std::string::npos) exeDir = exeDir.substr(0, p);
    return exeDir;
}

static std::string GetCacheSubDir(const std::string& sub) {
    std::string exeDir = GetExeDir();
    std::string cacheDir = exeDir + "\\Cache";
    CreateDirectoryA(cacheDir.c_str(), NULL);
    if (sub.empty()) return cacheDir;
    std::string dir = cacheDir + "\\" + sub;
    CreateDirectoryA(dir.c_str(), NULL);
    return dir;
}

// ============================================================
// CRX 解包
// ============================================================
static bool CrxStripper_WriteZip(const std::wstring& crxPath,
    const std::wstring& outZipPath) {
    std::ifstream in(crxPath, std::ios::binary);
    if (!in.is_open()) return false;

    unsigned char head[16] = { 0 };
    in.read((char*)head, 16);
    if (in.gcount() < 12) return false;
    if (head[0] != 'C' || head[1] != 'r' || head[2] != '2' || head[3] != '4')
        return false;

    unsigned int version = head[4] | (head[5] << 8) | (head[6] << 16) | (head[7] << 24);
    size_t zipOffset = 0;

    if (version == 2) {
        if (in.gcount() < 16) return false;
        unsigned int pk = head[8] | (head[9] << 8) | (head[10] << 16) | (head[11] << 24);
        unsigned int sk = head[12] | (head[13] << 8) | (head[14] << 16) | (head[15] << 24);
        zipOffset = 16 + pk + sk;
    }
    else if (version == 3) {
        unsigned int hs = head[8] | (head[9] << 8) | (head[10] << 16) | (head[11] << 24);
        zipOffset = 12 + hs;
    }
    else return false;

    in.seekg((std::streamoff)zipOffset, std::ios::beg);
    if (!in.good()) return false;

    std::ofstream out(outZipPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    char buf[64 * 1024];
    while (in) {
        in.read(buf, sizeof(buf));
        std::streamsize n = in.gcount();
        if (n <= 0) break;
        out.write(buf, n);
    }
    return true;
}

static bool WaitForManifest(const std::wstring& destDir, int timeoutMs) {
    std::wstring manifest = destDir + L"\\manifest.json";
    int elapsed = 0;
    while (elapsed < timeoutMs) {
        if (GetFileAttributesW(manifest.c_str()) != INVALID_FILE_ATTRIBUTES)
            return true;
        Sleep(200);
        elapsed += 200;
    }
    return GetFileAttributesW(manifest.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// ============================================================
// JSON 简易解析
// ============================================================
static std::wstring JsonExtractString(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return L"";
    p += pat.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':' || json[p] == '\t')) p++;
    if (p >= json.size() || json[p] != '"') return L"";
    p++;
    size_t end = p;
    while (end < json.size() && json[end] != '"') {
        if (json[end] == '\\') end++;
        end++;
    }
    return AnsiToWide(json.substr(p, end - p));
}

static std::vector<std::wstring> JsonExtractStringArray(const std::string& json,
    const std::string& key) {
    std::vector<std::wstring> result;
    std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return result;
    p += pat.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':' || json[p] == '\t')) p++;
    if (p >= json.size() || json[p] != '[') return result;
    p++;
    while (p < json.size() && json[p] != ']') {
        while (p < json.size() && (json[p] == ' ' || json[p] == ',' || json[p] == '\n' ||
            json[p] == '\r' || json[p] == '\t')) p++;
        if (p < json.size() && json[p] == '"') {
            p++;
            size_t end = p;
            while (end < json.size() && json[end] != '"') {
                if (json[end] == '\\') end++;
                end++;
            }
            result.push_back(AnsiToWide(json.substr(p, end - p)));
            p = end + 1;
        }
        else break;
    }
    return result;
}

// ============================================================
// Help 注册表解密
// ============================================================
static const std::string HELP_PASSWORD = "sr5r4g464dry8k78665sglnjtr4m5z578tk78q";
static const std::string HELP_REGISTRY_RELATIVE = "\\rppcst\\sys.xayx";

static std::string Help_Xor(const std::string& data, const std::string& key) {
    if (data.empty() || key.empty()) return data;
    std::string result = data;
    for (size_t i = 0; i < result.size(); ++i)
        result[i] ^= key[i % key.size()];
    return result;
}

static std::string Help_ReadRegistryFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return "";
    file.seekg(0, std::ios::end);
    std::streamoff endPos = file.tellg();
    if (endPos <= 0) { file.close(); return ""; }
    size_t size = (size_t)endPos;
    file.seekg(0, std::ios::beg);
    std::string encrypted(size, '\0');
    file.read(&encrypted[0], size);
    file.close();
    return Help_Xor(encrypted, HELP_PASSWORD);
}

static std::string Help_GetHelpText(const std::string& programDir) {
    std::string registryPath = programDir + HELP_REGISTRY_RELATIVE;
    std::string data = Help_ReadRegistryFile(registryPath);
    if (data.empty())
        return "注册表异常 - 无法读取注册表文件\r\n\r\n路径: " + registryPath;

    size_t pos = 0;
    std::vector<std::string> path = { "HKEY_LOCAL_MACHINE", "xbyx", "xcyx" };
    for (const auto& node : path) {
        pos = data.find("NODE:" + node, pos);
        if (pos == std::string::npos) return "未找到路径节点";
    }

    size_t helpDirPos = data.find("NODE:help\nISKEY:1", pos);
    if (helpDirPos == std::string::npos)
        helpDirPos = data.find("NODE:help\r\nISKEY:1", pos);

    if (helpDirPos == std::string::npos) {
        size_t childrenPos = data.find("CHILDREN:1", pos);
        while (childrenPos != std::string::npos) {
            size_t searchStart = (childrenPos > 50) ? childrenPos - 50 : 0;
            if (searchStart < pos) searchStart = pos;
            size_t helpPos = data.rfind("NODE:help", childrenPos);
            if (helpPos != std::string::npos && helpPos >= searchStart) {
                helpDirPos = helpPos;
                break;
            }
            childrenPos = data.find("CHILDREN:1", childrenPos + 10);
        }
    }
    if (helpDirPos == std::string::npos) return "未找到帮助目录节点";

    size_t childHelpPos = data.find("NODE:help", helpDirPos + 1);
    if (childHelpPos == std::string::npos) return "未找到子节点 help";

    size_t valuePos = data.find("VALUE:", childHelpPos);
    if (valuePos == std::string::npos) return "未找到 VALUE";
    valuePos += 6;
    size_t endPos = data.find("\n", valuePos);
    std::string helpValue = (endPos == std::string::npos)
        ? data.substr(valuePos)
        : data.substr(valuePos, endPos - valuePos);

    size_t p = 0;
    while ((p = helpValue.find("\\n", p)) != std::string::npos) {
        helpValue.replace(p, 2, "\r\n");
        p += 2;
    }
    return helpValue;
}

static void Help_Populate(HWND hRichEdit, const std::string& programDir) {
    if (!hRichEdit) return;
    std::string helpText = Help_GetHelpText(programDir);
    std::vector<std::string> lines;
    std::string current;
    for (char c : helpText) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            lines.push_back(current);
            current.clear();
        }
        else current += c;
    }
    if (!current.empty()) lines.push_back(current);

    SetWindowTextW(hRichEdit, L"");
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = RGB(30, 30, 30);
    SendMessageW(hRichEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

    for (const auto& line : lines) {
        int textLen = GetWindowTextLengthW(hRichEdit);
        SendMessageW(hRichEdit, EM_SETSEL, textLen, textLen);
        if (line.empty()) {
            SendMessageW(hRichEdit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
            continue;
        }
        std::wstring wline = AnsiToWide(line);
        wline += L"\r\n";
        SendMessageW(hRichEdit, EM_REPLACESEL, FALSE, (LPARAM)wline.c_str());
    }
    SendMessageW(hRichEdit, EM_SETSEL, 0, 0);
    SendMessageW(hRichEdit, EM_SCROLLCARET, 0, 0);
}

// ============================================================
// 构造 / 析构
// ============================================================
YXGuiPlugin::YXGuiPlugin() {
    LoadConfig();
    m_browserHeight = m_config.browserH;
    m_aiHovered.assign(m_config.aiButtons.size(), false);
    m_aiEnabled = m_config.aiEnabled;
    m_sidebarActive = -2;
}

YXGuiPlugin::~YXGuiPlugin() {
    StopCrxWatcher();

    if (m_hSettingsWnd && IsWindow(m_hSettingsWnd)) {
        DestroyWindow(m_hSettingsWnd);
        m_hSettingsWnd = nullptr;
    }

    // 先卸载 JS 插件（释放 QuickJS 引擎 + 子控件）
    UnloadJSPlugins();

    // 释放 CRX popup / options 的 WebView2
    if (m_popupWebView) { m_popupWebView->Release(); m_popupWebView = nullptr; }
    if (m_popupController) {
        m_popupController->Close();
        m_popupController->Release();
        m_popupController = nullptr;
    }
    if (m_popupEnv) { m_popupEnv->Release(); m_popupEnv = nullptr; }
    m_popupReady = false;

    if (m_optionsWebView) { m_optionsWebView->Release(); m_optionsWebView = nullptr; }
    if (m_optionsController) {
        m_optionsController->Close();
        m_optionsController->Release();
        m_optionsController = nullptr;
    }
    if (m_optionsEnv) { m_optionsEnv->Release(); m_optionsEnv = nullptr; }

    for (auto& t : m_tabs) {
        if (t.controller) { t.controller->Close(); t.controller->Release(); t.controller = nullptr; }
        if (t.webView) { t.webView->Release(); t.webView = nullptr; }
        if (t.hostWnd) { DestroyWindow(t.hostWnd); t.hostWnd = nullptr; }
    }
    m_tabs.clear();

    if (m_hWnd && IsWindow(m_hWnd)) DestroyWindow(m_hWnd);
    CleanupGdiResources();

    for (auto& ext : m_extensions) {
        if (ext.comExt) { ext.comExt->Release(); ext.comExt = nullptr; }
    }
    m_extensions.clear();
}

void YXGuiPlugin::CleanupGdiResources() {
    if (m_hFontUI) { DeleteObject(m_hFontUI);       m_hFontUI = nullptr; }
    if (m_hFontConsole) { DeleteObject(m_hFontConsole);  m_hFontConsole = nullptr; }
    if (m_hFontAddress) { DeleteObject(m_hFontAddress);  m_hFontAddress = nullptr; }
    if (m_hFontSidebar) { DeleteObject(m_hFontSidebar);  m_hFontSidebar = nullptr; }
    if (m_hbrBackground) { DeleteObject(m_hbrBackground); m_hbrBackground = nullptr; }
    if (m_gdiplusToken) { Gdiplus::GdiplusShutdown(m_gdiplusToken); m_gdiplusToken = 0; }
    if (m_oleInitialized) { OleUninitialize(); m_oleInitialized = false; }
}

// ============================================================
// IPlugin
// ============================================================
PluginInfo YXGuiPlugin::GetInfo() {
    PluginInfo info;
    info.name = "YXGui";
    info.version = "1.0.0";
    info.author = "YX";
    info.description = "图形化界面插件 - WebView2 + 扩展 + 标签 + JS 插件";
    info.id = "com.yx.gui";
    return info;
}
bool YXGuiPlugin::Initialize() {
    if (m_ui && m_ui->WriteLine) m_ui->WriteLine("[YXGui] 插件初始化完成");
    return true;
}
void YXGuiPlugin::Shutdown() {
    if (m_ui && m_ui->WriteLine) m_ui->WriteLine("[YXGui] 插件已关闭");
}
std::vector<PluginCommand> YXGuiPlugin::GetCommands() { return {}; }
void YXGuiPlugin::OnLoad() {}
void YXGuiPlugin::OnUnload() {}
void YXGuiPlugin::SetUIManager(PluginUIManager* ui) { m_ui = ui; }

std::string YXGuiPlugin::GetWindowTitle() {
    return "Light - 安全核心 (GUI)";
}

void YXGuiPlugin::GetWindowSize(int& w, int& h) {
    w = m_config.mainW;
    h = m_config.mainH;
}

// ============================================================
// 格式化 cd 显示
// ============================================================
std::wstring YXGuiPlugin::FormatCdDisplay(int maxWidthPx, int& outWidthPx) {
    outWidthPx = 40;
    if (!m_hWnd) return L"<...>";

    std::wstring fullText = L"<" + m_currentPath + L">";

    HDC hdc = GetDC(m_hWnd);
    if (!hdc) return fullText;

    HFONT oldFont = (HFONT)SelectObject(hdc,
        m_hFontConsole ? m_hFontConsole : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

    SIZE sz = { 0 };

    GetTextExtentPoint32W(hdc, fullText.c_str(), (int)fullText.size(), &sz);
    if (sz.cx <= maxWidthPx) {
        SelectObject(hdc, oldFont);
        ReleaseDC(m_hWnd, hdc);
        outWidthPx = sz.cx + 6;
        return fullText;
    }

    std::wstring path = m_currentPath;
    while (true) {
        size_t pos = path.find_first_of(L"\\/");
        if (pos == std::wstring::npos) break;
        if (pos + 1 >= path.size()) break;
        path = path.substr(pos + 1);

        std::wstring candidate = L"<..." + path + L">";
        GetTextExtentPoint32W(hdc, candidate.c_str(), (int)candidate.size(), &sz);
        if (sz.cx <= maxWidthPx) {
            SelectObject(hdc, oldFont);
            ReleaseDC(m_hWnd, hdc);
            outWidthPx = sz.cx + 6;
            return candidate;
        }
    }

    std::wstring tail = m_currentPath;
    while (!tail.empty()) {
        std::wstring candidate = L"<..." + tail + L">";
        GetTextExtentPoint32W(hdc, candidate.c_str(), (int)candidate.size(), &sz);
        if (sz.cx <= maxWidthPx) {
            SelectObject(hdc, oldFont);
            ReleaseDC(m_hWnd, hdc);
            outWidthPx = sz.cx + 6;
            return candidate;
        }
        tail.erase(0, 1);
    }

    SelectObject(hdc, oldFont);
    ReleaseDC(m_hWnd, hdc);
    outWidthPx = 40;
    return L"<...>";
}

// ============================================================
// cd / 输入框动态布局
// ============================================================
void YXGuiPlugin::LayoutCdAndInput() {
    if (!m_hWnd || !m_hPromptStatic || !m_hConsoleInput) return;
    if (m_browserExpanded) return;

    RECT rc;
    GetClientRect(m_hWnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;

    int contentLeft = SIDEBAR_WIDTH;
    int contentTop = YXStyle::TOPBAR_H + 2;
    int contentH = rc.bottom - m_browserHeight - YXStyle::SPLITTER_H - contentTop;
    if (contentH < 150) contentH = 150;

    int middleW = rc.right - contentLeft - YXStyle::LEFT_WIDTH - YXStyle::RIGHT_WIDTH;
    if (middleW < 100) return;

    int bottomRowH = 30;
    int consoleOutH = contentH - bottomRowH;
    if (consoleOutH < 50) consoleOutH = 50;

    int rowY = contentTop + consoleOutH;

    int maxCdW = middleW * 3 / 5;
    if (maxCdW < 40) maxCdW = 40;

    int cdW = 0;
    std::wstring display = FormatCdDisplay(maxCdW, cdW);
    SetWindowTextW(m_hPromptStatic, display.c_str());

    int inputLeft = contentLeft + YXStyle::LEFT_WIDTH + cdW;
    int inputW = middleW - cdW;

    if (inputW < 60) {
        inputW = 60;
        cdW = middleW - inputW;
        if (cdW < 40) cdW = 40;
        inputLeft = contentLeft + YXStyle::LEFT_WIDTH + cdW;
        inputW = middleW - cdW;

        int newCdW = 0;
        std::wstring d2 = FormatCdDisplay(cdW - 6, newCdW);
        SetWindowTextW(m_hPromptStatic, d2.c_str());
        cdW = newCdW;
        inputLeft = contentLeft + YXStyle::LEFT_WIDTH + cdW;
        inputW = middleW - cdW;
        if (inputW < 40) inputW = 40;
    }

    MoveWindow(m_hPromptStatic, contentLeft + YXStyle::LEFT_WIDTH, rowY, cdW, bottomRowH, TRUE);
    MoveWindow(m_hConsoleInput, inputLeft, rowY, inputW, bottomRowH, TRUE);
}

void YXGuiPlugin::UpdatePrompt(const std::string& path) {
    std::wstring* pText = new std::wstring(AnsiToWide(path));
    if (!PostMessageW(m_hWnd, WM_YXGUI_UPDATE_PROMPT, 0, (LPARAM)pText))
        delete pText;
}

// ============================================================
// 主程序下载桥接
// ============================================================
void YXGuiPlugin::RequestDownloadViaMainProgram(const std::wstring& url,
    const std::wstring& suggestedName) {
    if (!m_commandHandler) {
        OutputDebugStringW(L"[YXGui] 主程序未提供命令处理器，回退到 WebView2 内置下载\n");
        ICoreWebView2* wv = GetActiveWebView();
        if (wv) wv->Navigate(url.c_str());
        return;
    }

    std::string crxDir = GetCacheSubDir("crx");
    std::wstring wCrxDir = AnsiToWide(crxDir);
    SHCreateDirectoryExW(NULL, wCrxDir.c_str(), NULL);

    std::wstring fileName = suggestedName;
    if (fileName.empty()) {
        size_t slash = url.find_last_of(L'/');
        if (slash != std::wstring::npos) {
            std::wstring tail = url.substr(slash + 1);
            size_t q = tail.find(L'?');
            if (q != std::wstring::npos) tail = tail.substr(0, q);
            if (!tail.empty()) fileName = tail;
        }
    }
    if (fileName.empty()) fileName = L"download.crx";
    if (fileName.find(L".crx") == std::wstring::npos) fileName += L".crx";

    wchar_t ts[32];
    swprintf_s(ts, L"_%llu", (unsigned long long)GetTickCount64());
    size_t dot = fileName.find_last_of(L'.');
    if (dot != std::wstring::npos)
        fileName = fileName.substr(0, dot) + ts + L".crx";
    else
        fileName += ts;

    std::wstring target = wCrxDir + L"\\" + fileName;

    std::string cmd = "download \"";
    cmd += WideToUtf8(url);
    cmd += "\" \"";
    cmd += WideToUtf8(target);
    cmd += "\"";

    m_commandHandler(cmd);
}

void YXGuiPlugin::OnDownloadCompleted(const std::string& outPathUtf8, bool success) {
    if (!success) {
        if (m_hWnd && IsWindow(m_hWnd)) {
            std::wstring* pMsg = new std::wstring(L"扩展下载失败，请检查网络。");
            PostMessageW(m_hWnd, WM_YXGUI_CRX_INSTALL_DONE, 0, (LPARAM)pMsg);
        }
        return;
    }
    if (outPathUtf8.empty()) return;
    std::wstring* pPath = new std::wstring(AnsiToWide(outPathUtf8));
    if (m_hWnd && IsWindow(m_hWnd)) {
        PostMessageW(m_hWnd, WM_YXGUI_CRX_DOWNLOADED, 0, (LPARAM)pPath);
    }
    else {
        delete pPath;
    }
}

// ============================================================
// 配置读写
// ============================================================
std::string YXGuiPlugin::GetConfigFilePath() {
    std::string dir = GetCacheSubDir("gui");
    CreateDirectoryA(dir.c_str(), NULL);
    return dir + "\\gt.b";
}

void YXGuiPlugin::ResetConfigToDefault() {
    m_config = GuiConfig();

    m_config.aiButtons = {
        { L"豆包",      L"https://www.doubao.com/" },
        { L"千问",      L"https://www.qianwen.com/" },
        { L"DeepSeek",  L"https://chat.deepseek.com/" },
        { L"文心一言",  L"https://yiyan.baidu.com/" },
        { L"Kimi",      L"https://kimi.moonshot.cn/" },
        { L"智谱清言",  L"https://chatglm.cn/" },
        { L"讯飞星火",  L"https://xinghuo.xfyun.cn/" },
        { L"腾讯元宝",  L"https://yuanbao.tencent.com/" },
        { L"MiniMax",   L"https://www.minimaxi.com/" },
        { L"通义千问",  L"https://tongyi.aliyun.com/qianwen/" },
    };

    m_config.toolbarOrder = {
        { L"back",     true }, { L"forward",  true },
        { L"refresh",  true }, { L"home",     true },
        { L"star",     true }, { L"fav",      true },
        { L"zoomin",   true }, { L"zoomout",  true },
        { L"download", true }, { L"extension",true },
    };

    m_config.mainW = 1200;
    m_config.mainH = 800;
    m_config.browserH = 320;
    m_config.extMgrW = 820;
    m_config.extMgrH = 560;
    m_config.popupW = 380;
    m_config.popupH = 500;
    m_config.optionsW = 800;
    m_config.optionsH = 600;
    m_config.tabWidth = TAB_DEFAULT_W;
    m_config.aiEnabled = true;
    m_config.disabledPlugins.clear();
}

void YXGuiPlugin::LoadConfig() {
    ResetConfigToDefault();

    std::ifstream f(GetConfigFilePath());
    if (!f.is_open()) return;

    std::map<int, AiButtonConfig> aiMap;
    std::string line, section;
    bool toolbarLoaded = false;

    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos) section = line.substr(1, end - 1);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        try {
            if (section == "AI") {
                size_t dot = key.find('.');
                if (dot == std::string::npos) continue;
                int idx = std::stoi(key.substr(0, dot));
                std::string field = key.substr(dot + 1);
                if (field == "name") aiMap[idx].name = AnsiToWide(val);
                else if (field == "url") aiMap[idx].url = AnsiToWide(val);
            }
            else if (section == "Toolbar") {
                if (key == "order") {
                    m_config.toolbarOrder.clear();
                    std::stringstream ss(val);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        if (item.empty()) continue;
                        ToolbarButtonConfig tb;
                        size_t colon = item.find(':');
                        if (colon != std::string::npos) {
                            tb.id = AnsiToWide(item.substr(0, colon));
                            tb.visible = (item.substr(colon + 1) == "1");
                        }
                        else {
                            tb.id = AnsiToWide(item);
                            tb.visible = true;
                        }
                        if (tb.id == L"settings") continue;
                        m_config.toolbarOrder.push_back(tb);
                    }
                    toolbarLoaded = true;
                }
            }
            else if (section == "Window") {
                if (key == "mainW")         m_config.mainW = std::stoi(val);
                else if (key == "mainH")    m_config.mainH = std::stoi(val);
                else if (key == "browserH") m_config.browserH = std::stoi(val);
                else if (key == "extMgrW")  m_config.extMgrW = std::stoi(val);
                else if (key == "extMgrH")  m_config.extMgrH = std::stoi(val);
                else if (key == "popupW")   m_config.popupW = std::stoi(val);
                else if (key == "popupH")   m_config.popupH = std::stoi(val);
                else if (key == "optionsW") m_config.optionsW = std::stoi(val);
                else if (key == "optionsH") m_config.optionsH = std::stoi(val);
                else if (key == "tabWidth") m_config.tabWidth = std::stoi(val);
            }
            else if (section == "Plugins") {
                if (key == "aiEnabled") {
                    m_config.aiEnabled = (val != "0");
                }
                else if (key == "disabled") {
                    m_config.disabledPlugins.clear();
                    std::stringstream ss(val);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        if (!item.empty()) {
                            m_config.disabledPlugins.insert(AnsiToWide(item));
                        }
                    }
                }
            }
        }
        catch (...) {}
        (void)toolbarLoaded;
    }

    if (!aiMap.empty()) {
        m_config.aiButtons.clear();
        for (auto& kv : aiMap) m_config.aiButtons.push_back(kv.second);
    }

    if (m_config.toolbarOrder.empty()) {
        m_config.toolbarOrder = {
            { L"back",     true }, { L"forward",  true },
            { L"refresh",  true }, { L"home",     true },
            { L"star",     true }, { L"fav",      true },
            { L"zoomin",   true }, { L"zoomout",  true },
            { L"download", true }, { L"extension",true },
        };
    }

    if (m_config.mainW < 900)  m_config.mainW = 900;
    if (m_config.mainH < 600)  m_config.mainH = 600;
    if (m_config.browserH < YXStyle::BROWSER_MIN_H) m_config.browserH = YXStyle::BROWSER_MIN_H;
    if (m_config.browserH > YXStyle::BROWSER_MAX_H) m_config.browserH = YXStyle::BROWSER_MAX_H;
    if (m_config.tabWidth < TAB_MIN_W) m_config.tabWidth = TAB_MIN_W;
    if (m_config.tabWidth > TAB_MAX_W) m_config.tabWidth = TAB_MAX_W;
    if (m_config.extMgrW < 760) m_config.extMgrW = 760;
    if (m_config.extMgrW > 1200) m_config.extMgrW = 1200;
    if (m_config.extMgrH < 500) m_config.extMgrH = 500;
    if (m_config.extMgrH > 900) m_config.extMgrH = 900;

    m_aiEnabled = m_config.aiEnabled;
}

void YXGuiPlugin::SaveConfig() {
    CreateDirectoryA(GetCacheSubDir("gui").c_str(), NULL);
    std::ofstream f(GetConfigFilePath(), std::ios::trunc);
    if (!f.is_open()) return;

    f << "# Light GUI Configuration\n";
    f << "# 保存 AI 按钮、工具栏顺序、窗口尺寸等配置\n\n";

    f << "[AI]\n";
    for (size_t i = 0; i < m_config.aiButtons.size(); i++) {
        f << i << ".name=" << WideToUtf8(m_config.aiButtons[i].name) << "\n";
        f << i << ".url=" << WideToUtf8(m_config.aiButtons[i].url) << "\n";
    }

    f << "\n[Toolbar]\norder=";
    for (size_t i = 0; i < m_config.toolbarOrder.size(); i++) {
        if (i > 0) f << ",";
        f << WideToUtf8(m_config.toolbarOrder[i].id);
        f << ":" << (m_config.toolbarOrder[i].visible ? "1" : "0");
    }
    f << "\n";

    f << "\n[Window]\n";
    f << "mainW=" << m_config.mainW << "\n";
    f << "mainH=" << m_config.mainH << "\n";
    f << "browserH=" << m_config.browserH << "\n";
    f << "extMgrW=" << m_config.extMgrW << "\n";
    f << "extMgrH=" << m_config.extMgrH << "\n";
    f << "popupW=" << m_config.popupW << "\n";
    f << "popupH=" << m_config.popupH << "\n";
    f << "optionsW=" << m_config.optionsW << "\n";
    f << "optionsH=" << m_config.optionsH << "\n";
    f << "tabWidth=" << m_config.tabWidth << "\n";

    f << "\n[Plugins]\n";
    m_config.aiEnabled = m_aiEnabled;
    f << "aiEnabled=" << (m_aiEnabled ? "1" : "0") << "\n";
    f << "disabled=";
    bool first = true;
    for (const auto& id : m_config.disabledPlugins) {
        if (!first) f << ",";
        f << WideToUtf8(id);
        first = false;
    }
    f << "\n";
}

void YXGuiPlugin::ApplyConfig() {
    m_browserHeight = m_config.browserH;

    if (m_hWnd && IsWindow(m_hWnd)) {
        RECT rc;
        GetWindowRect(m_hWnd, &rc);
        int curW = rc.right - rc.left;
        int curH = rc.bottom - rc.top;
        if (curW != m_config.mainW || curH != m_config.mainH) {
            SetWindowPos(m_hWnd, NULL, 0, 0, m_config.mainW, m_config.mainH,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        RECT clientRc;
        GetClientRect(m_hWnd, &clientRc);
        OnSize(clientRc.right, clientRc.bottom);
    }
    RebuildAiButtons();
    ApplyToolbarLayout();
    if (m_hTabBar) InvalidateRect(m_hTabBar, NULL, TRUE);
    if (m_hSidebar) InvalidateRect(m_hSidebar, NULL, TRUE);
}

// ============================================================
// 标签管理
// ============================================================
ICoreWebView2* YXGuiPlugin::GetActiveWebView() {
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return nullptr;
    return m_tabs[m_activeTab].webView;
}

ICoreWebView2Controller* YXGuiPlugin::GetActiveController() {
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return nullptr;
    return m_tabs[m_activeTab].controller;
}

RECT YXGuiPlugin::GetTabContainerRect() {
    RECT rc = { 0, 0, 800, 400 };
    if (m_hTabContainer && IsWindow(m_hTabContainer)) {
        GetClientRect(m_hTabContainer, &rc);
    }
    return rc;
}

void YXGuiPlugin::SetupTabEvents(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)m_tabs.size()) return;
    ICoreWebView2* wv = m_tabs[tabIndex].webView;
    if (!wv) return;

    EventRegistrationToken t1;
    wv->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this, tabIndex](ICoreWebView2* sender,
                ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    LPWSTR uri = nullptr;
                    args->get_Uri(&uri);
                    std::wstring url = uri ? uri : L"";
                    if (uri) CoTaskMemFree(uri);

                    if (url.rfind(L"microsoft-edge:", 0) == 0) {
                        std::wstring extId;
                        size_t idPos = url.find(L"id=");
                        if (idPos != std::wstring::npos) {
                            extId = url.substr(idPos + 3);
                            size_t amp = extId.find_first_of(L"&#");
                            if (amp != std::wstring::npos) extId = extId.substr(0, amp);
                        }
                        if (extId.empty()) {
                            size_t detailPos = url.find(L"/detail/");
                            if (detailPos != std::wstring::npos) {
                                std::wstring tail = url.substr(detailPos + 8);
                                size_t slash = tail.find(L"/");
                                if (slash != std::wstring::npos) {
                                    std::wstring after = tail.substr(slash + 1);
                                    size_t q = after.find_first_of(L"?&#");
                                    if (q != std::wstring::npos) after = after.substr(0, q);
                                    if (after.length() >= 20 && after.length() <= 64)
                                        extId = after;
                                }
                            }
                        }

                        if (!extId.empty()) {
                            std::wstring crxUrl =
                                L"https://edge.microsoft.com/extensionwebstorebase/v1/crx"
                                L"?response=redirect&x=id%3D" + extId +
                                L"%26installsource%3Dondemand%26uc";
                            RequestDownloadViaMainProgram(crxUrl, L"");
                        }
                        else {
                            if (m_hWnd)
                                MessageBoxW(m_hWnd,
                                    L"无法从商店链接中提取扩展 ID。",
                                    L"提示", MB_OK | MB_ICONINFORMATION);
                        }
                        args->put_Handled(TRUE);
                        return S_OK;
                    }

                    if (!url.empty()) {
                        std::wstring* pUrl = new std::wstring(url);
                        if (m_hWnd && IsWindow(m_hWnd))
                            PostMessageW(m_hWnd, WM_YXGUI_NEW_TAB, 0, (LPARAM)pUrl);
                        else
                            delete pUrl;
                        args->put_Handled(TRUE);
                    }
                    return S_OK;
            }).Get(), &t1);

    EventRegistrationToken t2;
    wv->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this, tabIndex](ICoreWebView2* sender,
                ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                    if (tabIndex < 0 || tabIndex >= (int)m_tabs.size()) return S_OK;
                    if (m_tabs[tabIndex].webView) {
                        LPWSTR uri = nullptr;
                        m_tabs[tabIndex].webView->get_Source(&uri);
                        if (uri) { m_tabs[tabIndex].url = uri; CoTaskMemFree(uri); }

                        LPWSTR title = nullptr;
                        m_tabs[tabIndex].webView->get_DocumentTitle(&title);
                        if (title) {
                            m_tabs[tabIndex].title = title;
                            CoTaskMemFree(title);
                        }
                        else {
                            std::wstring st = m_tabs[tabIndex].url;
                            size_t s = st.find(L"://");
                            if (s != std::wstring::npos) st = st.substr(s + 3);
                            m_tabs[tabIndex].title = st;
                        }
                    }
                    if (tabIndex == m_activeTab && m_hAddressBar &&
                        m_tabs[tabIndex].webView) {
                        LPWSTR uri = nullptr;
                        m_tabs[tabIndex].webView->get_Source(&uri);
                        if (uri) { SetWindowTextW(m_hAddressBar, uri); CoTaskMemFree(uri); }
                    }
                    if (m_hWnd) PostMessageW(m_hWnd, WM_YXGUI_UPDATE_NAV, 0, 0);
                    if (m_hTabBar) InvalidateRect(m_hTabBar, NULL, TRUE);
                    return S_OK;
            }).Get(), &t2);

    EventRegistrationToken t2b;
    wv->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this, tabIndex](ICoreWebView2* sender, IUnknown* args) -> HRESULT {
                if (tabIndex < 0 || tabIndex >= (int)m_tabs.size()) return S_OK;
                if (!m_tabs[tabIndex].webView) return S_OK;
                LPWSTR title = nullptr;
                m_tabs[tabIndex].webView->get_DocumentTitle(&title);
                if (title) {
                    m_tabs[tabIndex].title = title;
                    CoTaskMemFree(title);
                    if (m_hTabBar) InvalidateRect(m_hTabBar, NULL, TRUE);
                }
                return S_OK;
            }).Get(), &t2b);

    EventRegistrationToken t2c;
    wv->add_SourceChanged(
        Callback<ICoreWebView2SourceChangedEventHandler>(
            [this, tabIndex](ICoreWebView2* sender,
                ICoreWebView2SourceChangedEventArgs* args) -> HRESULT {
                    if (tabIndex < 0 || tabIndex >= (int)m_tabs.size()) return S_OK;
                    if (tabIndex == m_activeTab && m_hAddressBar &&
                        m_tabs[tabIndex].webView) {
                        LPWSTR uri = nullptr;
                        m_tabs[tabIndex].webView->get_Source(&uri);
                        if (uri) { SetWindowTextW(m_hAddressBar, uri); CoTaskMemFree(uri); }
                    }
                    return S_OK;
            }).Get(), &t2c);

    ICoreWebView2_4* wv4 = nullptr;
    if (SUCCEEDED(wv->QueryInterface(IID_ICoreWebView2_4, (void**)&wv4)) && wv4) {
        EventRegistrationToken t3;
        wv4->add_DownloadStarting(
            Callback<ICoreWebView2DownloadStartingEventHandler>(
                [this](ICoreWebView2* sender,
                    ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                        ICoreWebView2DownloadOperation* op = nullptr;
                        args->get_DownloadOperation(&op);
                        if (!op) {
                            args->put_Handled(FALSE);
                            return S_OK;
                        }

                        LPWSTR uri = nullptr;
                        op->get_Uri(&uri);
                        std::wstring url = uri ? uri : L"";
                        if (uri) CoTaskMemFree(uri);

                        bool isCrx = false;
                        if (url.size() >= 4) {
                            std::wstring suffix = url.substr(url.size() - 4);
                            for (auto& c : suffix) c = towlower(c);
                            if (suffix == L".crx") isCrx = true;
                        }
                        if (url.find(L"clients2.google.com/service/update2/crx") != std::wstring::npos)
                            isCrx = true;
                        if (url.find(L"extensionwebstorebase/v1/crx") != std::wstring::npos)
                            isCrx = true;

                        if (isCrx) {
                            args->put_Cancel(TRUE);
                            args->put_Handled(TRUE);
                            RequestDownloadViaMainProgram(url, L"");
                        }
                        else {
                            args->put_Handled(FALSE);
                        }

                        if (op) op->Release();
                        return S_OK;
                }).Get(), &t3);
        wv4->Release();
    }
}

void YXGuiPlugin::AddNewTab(const std::wstring& url) {
    if (!m_webViewEnv) return;
    if (!m_hTabContainer) return;

    BrowserTab tab;
    tab.url = url.empty() ? L"https://www.bing.com" : url;
    tab.title = L"新标签页";

    RECT tabRc = GetTabContainerRect();
    int hw = tabRc.right - tabRc.left;
    int hh = tabRc.bottom - tabRc.top;
    if (hw <= 0) hw = 800;
    if (hh <= 0) hh = 400;

    tab.hostWnd = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | SS_NOTIFY,
        0, 0, hw, hh,
        m_hTabContainer, NULL, m_hInstance, NULL);

    if (!tab.hostWnd) return;

    m_tabs.push_back(tab);
    int tabIndex = (int)m_tabs.size() - 1;

    m_webViewEnv->CreateCoreWebView2Controller(
        m_tabs[tabIndex].hostWnd,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this, tabIndex](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                if (FAILED(result) || !controller) {
                    if (tabIndex < (int)m_tabs.size()) {
                        if (m_tabs[tabIndex].hostWnd) {
                            DestroyWindow(m_tabs[tabIndex].hostWnd);
                            m_tabs[tabIndex].hostWnd = nullptr;
                        }
                        m_tabs.erase(m_tabs.begin() + tabIndex);
                        if (m_activeTab >= (int)m_tabs.size())
                            m_activeTab = (int)m_tabs.size() - 1;
                        if (m_activeTab >= 0 && m_activeTab < (int)m_tabs.size()) {
                            if (m_tabs[m_activeTab].controller)
                                m_tabs[m_activeTab].controller->put_IsVisible(TRUE);
                            if (m_tabs[m_activeTab].hostWnd)
                                ShowWindow(m_tabs[m_activeTab].hostWnd, SW_SHOW);
                        }
                        UpdateTabBar();
                        ResizeAllTabs();
                    }
                    return result;
                }

                controller->AddRef();
                m_tabs[tabIndex].controller = controller;
                controller->put_IsVisible(FALSE);

                ICoreWebView2* webView = nullptr;
                controller->get_CoreWebView2(&webView);
                if (webView) {
                    webView->AddRef();
                    m_tabs[tabIndex].webView = webView;

                    ICoreWebView2Controller2* c2 = nullptr;
                    if (SUCCEEDED(controller->QueryInterface(
                        IID_ICoreWebView2Controller2, (void**)&c2)) && c2) {
                        COREWEBVIEW2_COLOR color = { 255, 255, 255, 255 };
                        c2->put_DefaultBackgroundColor(color);
                        c2->Release();
                    }

                    controller->put_ZoomFactor(m_zoomFactor);

                    {
                        ICoreWebView2Settings* settings = nullptr;
                        if (SUCCEEDED(webView->get_Settings(&settings)) && settings) {
                            ICoreWebView2Settings2* s2 = nullptr;
                            if (SUCCEEDED(settings->QueryInterface(
                                IID_ICoreWebView2Settings2, (void**)&s2)) && s2) {
                                s2->put_UserAgent(
                                    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                    L"AppleWebKit/537.36 (KHTML, like Gecko) "
                                    L"Chrome/120.0.0.0 Safari/537.36 Edg/120.0.0.0");
                                s2->Release();
                            }
                            settings->Release();
                        }
                    }

                    {
                        const wchar_t* script =
                            L"(function(){"
                            L"  try {"
                            L"    Object.defineProperty(navigator, 'userAgentData', {"
                            L"      configurable: true,"
                            L"      get: function() {"
                            L"        return {"
                            L"          brands: ["
                            L"            { brand: 'Microsoft Edge', version: '120' },"
                            L"            { brand: 'Chromium', version: '120' },"
                            L"            { brand: 'Not_A Brand', version: '8' }"
                            L"          ],"
                            L"          mobile: false,"
                            L"          platform: 'Windows',"
                            L"          getHighEntropyValues: function() {"
                            L"            return Promise.resolve({"
                            L"              architecture: 'x86',"
                            L"              bitness: '64',"
                            L"              brands: ["
                            L"                { brand: 'Microsoft Edge', version: '120' },"
                            L"                { brand: 'Chromium', version: '120' }"
                            L"              ],"
                            L"              fullVersionList: ["
                            L"                { brand: 'Microsoft Edge', version: '120.0.2210.0' },"
                            L"                { brand: 'Chromium', version: '120.0.6099.0' }"
                            L"              ],"
                            L"              mobile: false,"
                            L"              model: '',"
                            L"              platform: 'Windows',"
                            L"              platformVersion: '15.0.0',"
                            L"              uaFullVersion: '120.0.2210.0'"
                            L"            });"
                            L"          }"
                            L"        };"
                            L"      }"
                            L"    });"
                            L"  } catch(e) {}"
                            L"})();";

                        webView->AddScriptToExecuteOnDocumentCreated(script, nullptr);
                    }

                    SetupTabEvents(tabIndex);
                    webView->Navigate(m_tabs[tabIndex].url.c_str());
                }

                if (m_activeTab < 0)
                    SwitchToTab(tabIndex);

                if (!m_extensionsLoaded) {
                    m_extensionsLoaded = true;
                    LoadAllExtensions();
                }

                ResizeAllTabs();
                UpdateTabBar();
                return S_OK;
            }).Get());
}

void YXGuiPlugin::SwitchToTab(int idx) {
    if (idx < 0 || idx >= (int)m_tabs.size()) return;
    if (m_activeTab == idx) return;

    if (m_activeTab >= 0 && m_activeTab < (int)m_tabs.size()) {
        if (m_tabs[m_activeTab].controller)
            m_tabs[m_activeTab].controller->put_IsVisible(FALSE);
        if (m_tabs[m_activeTab].hostWnd)
            ShowWindow(m_tabs[m_activeTab].hostWnd, SW_HIDE);
        if (m_tabs[m_activeTab].webView) {
            LPWSTR uri = nullptr;
            m_tabs[m_activeTab].webView->get_Source(&uri);
            if (uri) { m_tabs[m_activeTab].url = uri; CoTaskMemFree(uri); }
        }
    }

    m_activeTab = idx;

    if (m_tabs[idx].controller) {
        m_tabs[idx].controller->put_IsVisible(TRUE);
        m_tabs[idx].controller->put_Bounds(GetTabContainerRect());
    }
    if (m_tabs[idx].hostWnd)
        ShowWindow(m_tabs[idx].hostWnd, SW_SHOW);

    if (m_tabs[idx].webView && m_hAddressBar) {
        LPWSTR uri = nullptr;
        m_tabs[idx].webView->get_Source(&uri);
        if (uri) { SetWindowTextW(m_hAddressBar, uri); CoTaskMemFree(uri); }
    }
    UpdateNavButtons();
    UpdateTabBar();
    ResizeAllTabs();
}

void YXGuiPlugin::CloseTab(int idx) {
    if (idx < 0 || idx >= (int)m_tabs.size()) return;
    BrowserTab& t = m_tabs[idx];
    if (t.controller) { t.controller->Close(); t.controller->Release(); t.controller = nullptr; }
    if (t.webView) { t.webView->Release(); t.webView = nullptr; }
    if (t.hostWnd) { DestroyWindow(t.hostWnd); t.hostWnd = nullptr; }

    m_tabs.erase(m_tabs.begin() + idx);

    if (m_tabs.empty()) {
        m_activeTab = -1;
        AddNewTab();
        return;
    }

    if (m_activeTab == idx) {
        int newActive = (std::min)(idx, (int)m_tabs.size() - 1);
        m_activeTab = -1;
        SwitchToTab(newActive);
    }
    else if (m_activeTab > idx) {
        m_activeTab--;
    }
    UpdateTabBar();
}

void YXGuiPlugin::ResizeAllTabs() {
    RECT rc = GetTabContainerRect();
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    if (w <= 0 && m_hTabContainer && IsWindow(m_hTabContainer)) {
        RECT wrc;
        GetWindowRect(m_hTabContainer, &wrc);
        w = wrc.right - wrc.left;
    }
    if (h <= 0 && m_hTabContainer && IsWindow(m_hTabContainer)) {
        RECT wrc;
        GetWindowRect(m_hTabContainer, &wrc);
        h = wrc.bottom - wrc.top;
    }
    if (w <= 0) w = 800;
    if (h <= 0) h = 400;

    for (auto& t : m_tabs) {
        if (t.hostWnd && IsWindow(t.hostWnd))
            MoveWindow(t.hostWnd, 0, 0, w, h, TRUE);
        if (t.controller) {
            RECT localRc = { 0, 0, w, h };
            t.controller->put_Bounds(localRc);
        }
    }

    if (m_activeTab >= 0 && m_activeTab < (int)m_tabs.size()) {
        if (m_tabs[m_activeTab].controller)
            m_tabs[m_activeTab].controller->put_IsVisible(TRUE);
        if (m_tabs[m_activeTab].hostWnd)
            ShowWindow(m_tabs[m_activeTab].hostWnd, SW_SHOW);
    }
}

void YXGuiPlugin::UpdateTabBar() {
    if (m_hTabBar && IsWindow(m_hTabBar))
        InvalidateRect(m_hTabBar, NULL, TRUE);
}

int YXGuiPlugin::HitTestTabBar(int x, int y, bool& onClose) {
    onClose = false;
    int tabW = m_config.tabWidth;
    int startX = 4;
    for (size_t i = 0; i < m_tabs.size(); i++) {
        int left = startX + (int)i * tabW;
        int right = left + tabW - 4;
        if (x >= left && x < right) {
            if (x >= right - 22) onClose = true;
            return (int)i;
        }
    }
    return -1;
}

// ============================================================
// 标签栏窗口过程
// ============================================================
LRESULT CALLBACK YXGuiPlugin::TabBarWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pThis = (YXGuiPlugin*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        if (!pThis) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            Gdiplus::LinearGradientBrush bg(
                Gdiplus::Point(0, 0),
                Gdiplus::Point(0, rc.bottom),
                Gdiplus::Color(255, 24, 38, 62),
                Gdiplus::Color(255, 39, 61, 92));
            g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);

            int tabW = pThis->m_config.tabWidth;
            int startX = 8;
            int tabH = YXStyle::TAB_BAR_H - 8;

            for (size_t i = 0; i < pThis->m_tabs.size(); ++i) {
                Gdiplus::RectF tr(
                    (Gdiplus::REAL)(startX + (int)i * tabW),
                    4.0f,
                    (Gdiplus::REAL)(tabW - 8),
                    (Gdiplus::REAL)tabH);
                bool active = ((int)i == pThis->m_activeTab);

                Gdiplus::GraphicsPath tp;
                const float rr = 10.0f;
                tp.AddArc(tr.X, tr.Y, rr, rr, 180, 90);
                tp.AddArc(tr.X + tr.Width - rr, tr.Y, rr, rr, 270, 90);
                tp.AddArc(tr.X + tr.Width - rr, tr.Y + tr.Height - rr, rr, rr, 0, 90);
                tp.AddArc(tr.X, tr.Y + tr.Height - rr, rr, rr, 90, 90);
                tp.CloseFigure();

                Gdiplus::Color tabColor = active
                    ? Gdiplus::Color(255, 255, 255, 255)
                    : Gdiplus::Color(45, 225, 231, 238);
                Gdiplus::SolidBrush tb(tabColor);
                g.FillPath(&tb, &tp);

                if (active) {
                    Gdiplus::Pen activePen(Gdiplus::Color(255, 111, 159, 224), 1.5f);
                    g.DrawPath(&activePen, &tp);
                }

                HFONT hFont = pThis->m_hFontUI
                    ? pThis->m_hFontUI : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HFONT oldFont = (HFONT)SelectObject(memDC, hFont);
                SetBkMode(memDC, TRANSPARENT);

                std::wstring title = pThis->m_tabs[i].title;
                if (title.empty()) {
                    title = pThis->m_tabs[i].url;
                    size_t pos = title.find(L"://");
                    if (pos != std::wstring::npos) title = title.substr(pos + 3);
                }

                RECT textR = {
                    (int)tr.X + 12, (int)tr.Y,
                    (int)tr.X + (int)tr.Width - 34, (int)tr.Y + (int)tr.Height
                };
                SetTextColor(memDC, active ? RGB(35, 48, 68) : RGB(230, 238, 248));
                DrawTextW(memDC, title.c_str(), -1, &textR,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                RECT closeR = {
                    (int)tr.X + (int)tr.Width - 28, (int)tr.Y,
                    (int)tr.X + (int)tr.Width - 8, (int)tr.Y + (int)tr.Height
                };
                SetTextColor(memDC, active ? RGB(111, 124, 142) : RGB(181, 198, 219));
                DrawTextW(memDC, L"×", -1, &closeR,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(memDC, oldFont);
            }

            int plusX = startX + (int)pThis->m_tabs.size() * tabW + 4;
            Gdiplus::SolidBrush plusBrush(Gdiplus::Color(35, 255, 255, 255));
            g.FillEllipse(&plusBrush, (Gdiplus::REAL)plusX, 7.0f, 26.0f, 26.0f);

            HFONT hFont = pThis->m_hFontUI
                ? pThis->m_hFontUI : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT oldFont = (HFONT)SelectObject(memDC, hFont);
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(226, 237, 251));
            RECT plusR = { plusX, 7, plusX + 26, 33 };
            DrawTextW(memDC, L"+", -1, &plusR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(memDC, oldFont);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (!pThis) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int tabW = pThis->m_config.tabWidth;
        int totalTabsEnd = 4 + (int)pThis->m_tabs.size() * tabW;

        if (x >= totalTabsEnd && x < totalTabsEnd + 28) {
            pThis->AddNewTab();
            return 0;
        }

        bool onClose = false;
        int idx = pThis->HitTestTabBar(x, y, onClose);
        if (idx >= 0) {
            if (onClose) pThis->CloseTab(idx);
            else pThis->SwitchToTab(idx);
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        if (!pThis) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        bool onClose = false;
        int idx = pThis->HitTestTabBar(x, y, onClose);

        HMENU hMenu = CreatePopupMenu();
        if (idx >= 0) {
            AppendMenuW(hMenu, MF_STRING, 1, L"新建标签页");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 2, L"关闭当前标签页");
            AppendMenuW(hMenu, MF_STRING, 3, L"关闭其他标签页");
            AppendMenuW(hMenu, MF_STRING, 4, L"关闭所有标签页");
        }
        else {
            AppendMenuW(hMenu, MF_STRING, 1, L"新建标签页");
        }

        POINT pt;
        GetCursorPos(&pt);
        int cmd = TrackPopupMenu(hMenu,
            TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
            pt.x, pt.y, 0, hWnd, NULL);
        DestroyMenu(hMenu);

        if (cmd == 1) pThis->AddNewTab();
        else if (cmd == 2 && idx >= 0) pThis->CloseTab(idx);
        else if (cmd == 3 && idx >= 0) {
            for (int i = (int)pThis->m_tabs.size() - 1; i >= 0; i--)
                if (i != idx) pThis->CloseTab(i);
        }
        else if (cmd == 4) {
            while ((int)pThis->m_tabs.size() > 1)
                pThis->CloseTab((int)pThis->m_tabs.size() - 1);
            pThis->CloseTab(0);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// WebView2 初始化
// ============================================================
void YXGuiPlugin::InitWebView() {
    if (!m_hTabContainer) return;

    std::string userDataFolder = GetCacheSubDir("WebView2_Plugin");
    std::wstring wUserData = AnsiToWide(userDataFolder);

    ICoreWebView2EnvironmentOptions* options = new SimpleEnvOptions();
    if (options) {
        ICoreWebView2EnvironmentOptions6* o6 = nullptr;
        HRESULT hrOptQI = options->QueryInterface(
            IID_ICoreWebView2EnvironmentOptions6, (void**)&o6);
        if (SUCCEEDED(hrOptQI) && o6) {
            o6->put_AreBrowserExtensionsEnabled(TRUE);
            BOOL verifyEnabled = FALSE;
            HRESULT hrVerify = o6->get_AreBrowserExtensionsEnabled(&verifyEnabled);
            m_extensionsEnabled =
                SUCCEEDED(hrVerify) && verifyEnabled;
            o6->Release();
        }
        else {
            m_extensionsEnabled = false;
            options->Release();
            options = nullptr;
        }
    }

    auto envCallback = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env) {
                wchar_t buf[128];
                swprintf_s(buf, L"[YXGui] WebView2 环境创建失败: 0x%08X", (unsigned)result);
                OutputDebugStringW(buf);
                MessageBoxW(m_hWnd ? m_hWnd : nullptr, buf,
                    L"WebView2 错误", MB_OK | MB_ICONERROR);
                return result;
            }
            m_webViewEnv = env;
            m_webViewEnv->AddRef();

            AddNewTab(L"https://www.bing.com");
            m_webViewReady = true;

            StartCrxWatcher();

            // 加载 .zip 插件（QuickJS 驱动）
            LoadJSPlugins();
            RefreshSidebar();
            return S_OK;
        });

    HRESULT hrCreate = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, wUserData.c_str(), options, envCallback.Get());

    if (hrCreate == E_INVALIDARG) {
        hrCreate = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, wUserData.c_str(), nullptr, envCallback.Get());
    }

    if (FAILED(hrCreate)) {
        wchar_t buf[256];
        swprintf_s(buf,
            L"CreateCoreWebView2EnvironmentWithOptions 失败: 0x%08X\n\n"
            L"请确认已安装 Microsoft Edge WebView2 Runtime。",
            (unsigned)hrCreate);
        OutputDebugStringW(buf);
        MessageBoxW(m_hWnd ? m_hWnd : nullptr, buf,
            L"WebView2 错误", MB_OK | MB_ICONERROR);
    }
    if (options) options->Release();
}

void YXGuiPlugin::UpdateNavButtons() {
    ICoreWebView2* wv = GetActiveWebView();
    if (!wv) return;
    BOOL cb = FALSE, cf = FALSE;
    wv->get_CanGoBack(&cb);
    wv->get_CanGoForward(&cf);
    if (m_hNavBack)    EnableWindow(m_hNavBack, cb);
    if (m_hNavForward) EnableWindow(m_hNavForward, cf);

    wchar_t url[2048] = { 0 };
    if (m_hAddressBar) GetWindowTextW(m_hAddressBar, url, 2048);
    bool isFav = false;
    for (const auto& f : m_favorites) {
        if (f == url) { isFav = true; break; }
    }
    if (m_hNavStar) SetWindowTextW(m_hNavStar, isFav ? L"★" : L"☆");
}

// ============================================================
// 扩展扫描
// ============================================================
void YXGuiPlugin::ScanExtensions() {
    std::lock_guard<std::mutex> lock(m_extInstallMutex);

    std::vector<ExtInfo> oldExtensions;
    oldExtensions.swap(m_extensions);

    std::string extRoot = GetCacheSubDir("Extensions");
    std::string pattern = extRoot + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        for (auto& old : oldExtensions) {
            if (old.comExt) {
                old.comExt->Release();
                old.comExt = nullptr;
            }
        }
        return;
    }

    std::vector<ExtInfo> newExtensions;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

        std::string extPathA = extRoot + "\\" + fd.cFileName;
        std::string manifestPath = extPathA + "\\manifest.json";
        if (GetFileAttributesA(manifestPath.c_str()) == INVALID_FILE_ATTRIBUTES) continue;

        ExtInfo info;
        info.path = AnsiToWide(extPathA);
        ParseExtensionManifest(info);
        if (info.name.empty()) info.name = AnsiToWide(fd.cFileName);

        for (auto& old : oldExtensions) {
            if (_wcsicmp(old.path.c_str(), info.path.c_str()) == 0) {
                info.id = std::move(old.id);
                info.enabled = old.enabled;
                info.comExt = old.comExt;
                old.comExt = nullptr;
                break;
            }
        }

        newExtensions.push_back(std::move(info));
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    m_extensions.swap(newExtensions);

    for (auto& old : oldExtensions) {
        if (old.comExt) {
            old.comExt->Release();
            old.comExt = nullptr;
        }
    }
}

void YXGuiPlugin::ParseExtensionManifest(ExtInfo& ext) {
    std::string manifestPath = WideToUtf8(ext.path) + "\\manifest.json";
    std::ifstream f(manifestPath, std::ios::binary);
    if (!f.is_open()) return;
    std::string json((std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    f.close();
    ext.name = JsonExtractString(json, "name");
    ext.version = JsonExtractString(json, "version");
    ext.description = JsonExtractString(json, "description");
    ext.permissions = JsonExtractStringArray(json, "permissions");

    size_t actionPos = json.find("\"action\"");
    if (actionPos == std::string::npos) actionPos = json.find("\"browser_action\"");
    if (actionPos != std::string::npos) {
        std::string ab = json.substr(actionPos, 500);
        ext.popupPath = JsonExtractString(ab, "default_popup");
    }
    ext.optionsPath = JsonExtractString(json, "options_page");
    if (ext.optionsPath.empty()) {
        size_t op = json.find("\"options_ui\"");
        if (op != std::string::npos) {
            std::string ob = json.substr(op, 300);
            ext.optionsPath = JsonExtractString(ob, "page");
        }
    }
}

void YXGuiPlugin::LoadAllExtensions() {
    ScanExtensions();
    ICoreWebView2* wv = GetActiveWebView();
    if (!wv) return;
    ICoreWebView2_13* wv13 = nullptr;
    if (FAILED(wv->QueryInterface(IID_ICoreWebView2_13, (void**)&wv13)) || !wv13) return;
    ICoreWebView2Profile* profile = nullptr;
    wv13->get_Profile(&profile);
    wv13->Release();
    if (!profile) return;
    ICoreWebView2Profile7* p7 = nullptr;
    HRESULT hr = profile->QueryInterface(IID_ICoreWebView2Profile7, (void**)&p7);
    profile->Release();
    if (FAILED(hr) || !p7) return;

    std::vector<std::wstring> paths;
    {
        std::lock_guard<std::mutex> lock(m_extInstallMutex);
        paths.reserve(m_extensions.size());
        for (const auto& ext : m_extensions) {
            if (ext.comExt && !ext.id.empty())
                continue;
            paths.push_back(ext.path);
        }
    }

    for (const auto& wpath : paths) {
        HRESULT hrAdd = p7->AddBrowserExtension(
            wpath.c_str(),
            Callback<ICoreWebView2ProfileAddBrowserExtensionCompletedHandler>(
                [this, wpath](HRESULT result,
                    ICoreWebView2BrowserExtension* extension) -> HRESULT {
                    if (SUCCEEDED(result) && extension) {
                        extension->AddRef();

                        LPWSTR id = nullptr;
                        extension->get_Id(&id);
                        BOOL en = FALSE;
                        extension->get_IsEnabled(&en);

                        bool stored = false;
                        {
                            std::lock_guard<std::mutex> lock(m_extInstallMutex);
                            for (auto& ext : m_extensions) {
                                if (_wcsicmp(ext.path.c_str(), wpath.c_str()) == 0) {
                                    if (id) ext.id = id;
                                    ext.enabled = (en != FALSE);

                                    if (ext.comExt) ext.comExt->Release();
                                    ext.comExt = extension;
                                    stored = true;
                                    break;
                                }
                            }
                        }

                        if (id) CoTaskMemFree(id);

                        if (m_hExtList && IsWindow(m_hExtList))
                            InvalidateRect(m_hExtList, NULL, TRUE);

                        if (!stored)
                            extension->Release();
                    }
                    return S_OK;
                }).Get());

        if (FAILED(hrAdd)) {
            // 静默失败
        }
    }
    p7->Release();
}

// ============================================================
// 侧边栏
// ============================================================
void YXGuiPlugin::CreateSidebar() {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = SidebarWndProc;
        wc.hInstance = m_hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_HAND);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"YXGuiSidebarWnd";
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        RegisterClassExW(&wc);
        reg = true;
    }

    m_hSidebar = CreateWindowExW(
        0, L"YXGuiSidebarWnd", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, SIDEBAR_WIDTH, 100,
        m_hWnd, (HMENU)(INT_PTR)IDC_SIDEBAR,
        m_hInstance, this);
}

void YXGuiPlugin::LayoutSidebar(int width, int height) {
    if (m_hSidebar && IsWindow(m_hSidebar)) {
        MoveWindow(m_hSidebar, 0, 0, SIDEBAR_WIDTH, height, TRUE);
    }
}

void YXGuiPlugin::RefreshSidebar() {
    if (m_hSidebar && IsWindow(m_hSidebar)) {
        InvalidateRect(m_hSidebar, NULL, TRUE);
        UpdateWindow(m_hSidebar);
    }
}

LRESULT CALLBACK YXGuiPlugin::SidebarWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pThis = (YXGuiPlugin*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        if (!pThis) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            Gdiplus::LinearGradientBrush bg(
                Gdiplus::Point(0, 0),
                Gdiplus::Point(0, rc.bottom),
                Gdiplus::Color(255, 28, 39, 58),
                Gdiplus::Color(255, 22, 32, 50));
            g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);

            Gdiplus::Pen divPen(Gdiplus::Color(255, 48, 62, 86), 1.0f);
            g.DrawLine(&divPen,
                (Gdiplus::REAL)(rc.right - 1), (Gdiplus::REAL)0,
                (Gdiplus::REAL)(rc.right - 1), (Gdiplus::REAL)rc.bottom);

            HFONT hFont = pThis->m_hFontSidebar
                ? pThis->m_hFontSidebar : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT oldFont = (HFONT)SelectObject(memDC, hFont);
            SetBkMode(memDC, TRANSPARENT);

            const int itemW = rc.right - 8;
            // 侧边栏按钮改为紧凑的横向卡片：高度 = 宽度的一半，无圆角。
            const int itemH = std::max(28, itemW / 2);
            const int itemX = 4;
            int itemY = 8;

            auto DrawItem = [&](int itemIndex, const std::wstring& emoji,
                                const std::wstring& label, bool active, bool hovered) {
                Gdiplus::RectF r((Gdiplus::REAL)itemX, (Gdiplus::REAL)itemY,
                    (Gdiplus::REAL)itemW, (Gdiplus::REAL)itemH);

                // 直角矩形（用户要求：按钮可以不圆角）
                if (active) {
                    Gdiplus::SolidBrush b(Gdiplus::Color(255, 64, 116, 194));
                    g.FillRectangle(&b, r);
                    Gdiplus::SolidBrush accent(Gdiplus::Color(255, 138, 192, 255));
                    g.FillRectangle(&accent, (Gdiplus::REAL)itemX, r.Y,
                        3.0f, r.Height);
                }
                else if (hovered) {
                    Gdiplus::SolidBrush b(Gdiplus::Color(255, 48, 68, 102));
                    g.FillRectangle(&b, r);
                }

                Gdiplus::Font emojiFont(L"Segoe UI Emoji", 16,
                    Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush emojiBrush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::StringFormat emojiFmt;
                emojiFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                emojiFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                g.DrawString(emoji.c_str(), -1, &emojiFont,
                    Gdiplus::RectF(r.X + 4, r.Y, 28, r.Height),
                    &emojiFmt, &emojiBrush);

                Gdiplus::Font labelFont(L"Microsoft YaHei", 12,
                    Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush labelBrush(active
                    ? Gdiplus::Color(255, 255, 255, 255)
                    : Gdiplus::Color(255, 200, 216, 236));
                Gdiplus::StringFormat labelFmt;
                labelFmt.SetAlignment(Gdiplus::StringAlignmentNear);
                labelFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                labelFmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                labelFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

                std::wstring disp = label;
                if (disp.length() > 9) disp = disp.substr(0, 8) + L"…";
                g.DrawString(disp.c_str(), -1, &labelFont,
                    Gdiplus::RectF(r.X + 34, r.Y, r.Width - 38, r.Height),
                    &labelFmt, &labelBrush);

                itemY += itemH + 4;
            };

            // 主界面按钮
            {
                bool active = (pThis->m_sidebarActive == -2);
                bool hovered = (pThis->m_sidebarHovered == -2);
                DrawItem(-2, L"🏠", L"主界面", active, hovered);
            }

            // 内置功能：光语 AI（不属于插件，单独置顶）
            if (pThis->m_aiEnabled && pThis->m_aiIndex >= 0 &&
                pThis->m_aiIndex < (int)pThis->m_jsPlugins.size()) {
                bool active = (pThis->m_sidebarActive == -3);
                bool hovered = (pThis->m_sidebarHovered == -3);
                DrawItem(-3, L"✨", L"AI接口", active, hovered);
            }

            // 分隔线 + 扩展插件
            {
                bool hasPlugin = false;
                for (size_t i = 0; i < pThis->m_jsPlugins.size(); ++i) {
                    if ((int)i == pThis->m_aiIndex) continue;
                    if (pThis->IsJSPluginDisabled(pThis->m_jsPlugins[i]->GetId()))
                        continue;
                    hasPlugin = true;
                    break;
                }
                if (hasPlugin) {
                    Gdiplus::Pen p(Gdiplus::Color(255, 48, 62, 86), 1.0f);
                    g.DrawLine(&p, 16.0f, (Gdiplus::REAL)(itemY + 2),
                        (Gdiplus::REAL)(rc.right - 16), (Gdiplus::REAL)(itemY + 2));
                    itemY += 8;
                }
            }

            // 扩展插件按钮（停用的插件不显示在侧边栏）
            for (size_t i = 0; i < pThis->m_jsPlugins.size(); ++i) {
                if ((int)i == pThis->m_aiIndex) continue;
                const auto& plug = pThis->m_jsPlugins[i];
                if (pThis->IsJSPluginDisabled(plug->GetId())) continue;
                bool active = (pThis->m_sidebarActive == (int)i);
                bool hovered = (pThis->m_sidebarHovered == (int)i);
                std::wstring emoji = plug->GetIconEmoji();
                if (emoji.empty()) emoji = L"🔌";
                std::wstring label = plug->GetName();
                DrawItem((int)i, emoji, label, active, hovered);
            }

            // 原生面板按钮（外部 DLL 注册的原生界面）
            for (size_t i = 0; i < pThis->m_nativePanels.size(); ++i) {
                int idx = NATIVE_PANEL_BASE + (int)i;
                bool active = (pThis->m_sidebarActive == idx);
                bool hovered = (pThis->m_sidebarHovered == idx);
                std::wstring emoji = AnsiToWide(pThis->m_nativePanels[i].icon);
                if (emoji.empty()) emoji = L"🧩";
                std::wstring label = AnsiToWide(pThis->m_nativePanels[i].name);
                if (label.empty()) label = L"插件";
                DrawItem(idx, emoji, label, active, hovered);
            }

            SelectObject(memDC, oldFont);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!pThis) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int hit = pThis->HitTestSidebar(x, y);
        if (hit != pThis->m_sidebarHovered) {
            pThis->m_sidebarHovered = hit;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (pThis) {
            if (pThis->m_sidebarHovered != -1) {
                pThis->m_sidebarHovered = -1;
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (!pThis) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int hit = pThis->HitTestSidebar(x, y);
        if (hit != -1) {
            pThis->m_sidebarPressed = hit;
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (!pThis) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int hit = pThis->HitTestSidebar(x, y);
        int pressed = pThis->m_sidebarPressed;
        pThis->m_sidebarPressed = -1;
        if (hit != -1 && hit == pressed) {
            pThis->SwitchView(hit);
        }
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int YXGuiPlugin::HitTestSidebar(int x, int y) {
    if (!m_hSidebar || !IsWindow(m_hSidebar)) return -1;
    RECT rc;
    GetClientRect(m_hSidebar, &rc);

    const int itemW = rc.right - 8;
    const int itemH = std::max(28, itemW / 2);
    const int itemX = 4;
    int itemY = 8;

    if (x >= itemX && x < itemX + itemW && y >= itemY && y < itemY + itemH) {
        return -2;
    }
    itemY += itemH + 4;

    if (m_aiEnabled && m_aiIndex >= 0 &&
        m_aiIndex < (int)m_jsPlugins.size()) {
        if (x >= itemX && x < itemX + itemW && y >= itemY && y < itemY + itemH) {
            return -3;
        }
        itemY += itemH + 4;
    }

    bool hasPlugin = false;
    for (size_t i = 0; i < m_jsPlugins.size(); ++i) {
        if ((int)i == m_aiIndex) continue;
        if (IsJSPluginDisabled(m_jsPlugins[i]->GetId())) continue;
        hasPlugin = true;
        break;
    }
    if (hasPlugin) itemY += 8;

    for (size_t i = 0; i < m_jsPlugins.size(); ++i) {
        if ((int)i == m_aiIndex) continue;
        if (IsJSPluginDisabled(m_jsPlugins[i]->GetId())) continue;
        if (x >= itemX && x < itemX + itemW && y >= itemY && y < itemY + itemH) {
            return (int)i;
        }
        itemY += itemH + 4;
    }

    // 原生面板按钮
    for (size_t i = 0; i < m_nativePanels.size(); ++i) {
        if (x >= itemX && x < itemX + itemW && y >= itemY && y < itemY + itemH) {
            return NATIVE_PANEL_BASE + (int)i;
        }
        itemY += itemH + 4;
    }
    return -1;
}

// ============================================================
// 扩展管理器
// ============================================================
void YXGuiPlugin::ShowExtensionManager() {
    if (m_hExtMgrWnd && IsWindow(m_hExtMgrWnd)) {
        SetForegroundWindow(m_hExtMgrWnd);
        return;
    }

    ScanExtensions();

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = ExtMgrWndProc;
        wc.hInstance = m_hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(245, 246, 248));
        wc.lpszClassName = L"YXGuiExtMgrWnd";
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        reg = true;
    }

    int w = m_config.extMgrW;
    int h = m_config.extMgrH;
    if (w < 760) w = 760;
    if (w > 1200) w = 1200;
    if (h < 500) h = 500;
    if (h > 900) h = 900;

    RECT rcMain;
    GetWindowRect(m_hWnd, &rcMain);
    int x = rcMain.left + ((rcMain.right - rcMain.left) - w) / 2;
    int y = rcMain.top + ((rcMain.bottom - rcMain.top) - h) / 2;

    m_hExtMgrWnd = CreateWindowExW(
        0,
        L"YXGuiExtMgrWnd", L"扩展管理",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        x, y, w, h,
        m_hWnd, NULL, m_hInstance, this);
    if (!m_hExtMgrWnd) return;

    HWND hTitle = CreateWindowExW(0, L"STATIC", L"扩展中心",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        20, 12, 220, 30, m_hExtMgrWnd, NULL, m_hInstance, NULL);
    HFONT hTitleFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

    HWND hBtn1 = CreateWindowExW(0, L"BUTTON", L"🛒 从商店安装",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        w - 300, 12, 136, 28, m_hExtMgrWnd,
        (HMENU)(INT_PTR)IDC_EXT_INSTALL_FROM_STORE,
        m_hInstance, NULL);
    SendMessageW(hBtn1, WM_SETFONT, (WPARAM)m_hFontUI, TRUE);

    HWND hBtn2 = CreateWindowExW(0, L"BUTTON", L"🔗 从链接安装",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        w - 156, 12, 136, 28, m_hExtMgrWnd,
        (HMENU)(INT_PTR)IDC_EXT_INSTALL_FROM_LINK,
        m_hInstance, NULL);
    SendMessageW(hBtn2, WM_SETFONT, (WPARAM)m_hFontUI, TRUE);

    int listY = 56;
    int listH = h - listY - 82;

    m_hExtList = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_HASSTRINGS | WS_TABSTOP,
        14, listY, w - 28, listH, m_hExtMgrWnd,
        (HMENU)IDC_EXT_LIST, m_hInstance, NULL);
    SendMessageW(m_hExtList, WM_SETFONT, (WPARAM)m_hFontUI, TRUE);
    SendMessageW(m_hExtList, LB_SETITEMHEIGHT, 0, YXStyle::EXT_LIST_ITEM_H);

    SetWindowSubclass(m_hExtList, ExtListSubclassProc, 10, (DWORD_PTR)this);
    SetWindowTheme(m_hExtList, L"Explorer", NULL);

    for (size_t i = 0; i < m_extensions.size(); i++) {
        SendMessageW(m_hExtList, LB_ADDSTRING, 0,
            (LPARAM)m_extensions[i].name.c_str());
    }
    if (!m_extensions.empty())
        SendMessageW(m_hExtList, LB_SETCURSEL, 0, 0);

    int bottomY = h - 58;
    int bw = 138;
    int gap = 10;
    int bx = 20;

    struct { const wchar_t* text; int id; } btns[] = {
        { L"打开 Popup",     IDC_EXT_OPEN_POPUP },
        { L"打开 Options",   IDC_EXT_OPEN_OPTIONS },
        { L"卸载",           IDC_EXT_REMOVE },
        { L"打开目录",       IDC_EXT_OPEN_DIR },
        { L"加载文件夹",     IDC_EXT_ADD },
    };
    for (auto& b : btns) {
        HWND btn = CreateWindowExW(0, L"BUTTON", b.text,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            bx, bottomY, bw, 34, m_hExtMgrWnd,
            (HMENU)(INT_PTR)b.id, m_hInstance, NULL);
        SendMessageW(btn, WM_SETFONT, (WPARAM)m_hFontUI, TRUE);
        bx += bw + gap;
    }

    ShowWindow(m_hExtMgrWnd, SW_SHOW);
    UpdateWindow(m_hExtMgrWnd);
    InvalidateRect(m_hExtList, NULL, TRUE);
}

// 扩展列表开关点击
static LRESULT CALLBACK ExtListSubclassProc(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR) {

    if (msg == WM_LBUTTONUP) {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        LRESULT itemResult = SendMessageW(
            hWnd, LB_ITEMFROMPOINT, 0,
            MAKELPARAM((SHORT)pt.x, (SHORT)pt.y));

        int idx = LOWORD(itemResult);
        bool outside = (HIWORD(itemResult) != 0);

        if (!outside && idx >= 0) {
            RECT itemRc = {};
            if (SendMessageW(
                hWnd, LB_GETITEMRECT, idx, (LPARAM)&itemRc) != LB_ERR) {

                constexpr int kMargin = 8;
                constexpr int kSwitchW = 52;
                constexpr int kSwitchH = 28;
                constexpr int kSwitchRightInset = 20;

                int itemW = itemRc.right - itemRc.left;
                int itemH = itemRc.bottom - itemRc.top;

                int cardX = itemRc.left + kMargin;
                int cardW = itemW - kMargin * 2;

                int cardY = itemRc.top + kMargin;
                int cardH = itemH - kMargin * 2;
                int swX = cardX + cardW - kSwitchW - kSwitchRightInset;
                int swY = cardY + cardH - kSwitchH - 18;

                RECT swRc = {
                    swX, swY,
                    swX + kSwitchW,
                    swY + kSwitchH
                };

                if (PtInRect(&swRc, pt)) {
                    SendMessageW(hWnd, LB_SETCURSEL, idx, 0);

                    HWND parent = GetParent(hWnd);
                    if (parent) {
                        SendMessageW(
                            parent,
                            WM_COMMAND,
                            MAKEWPARAM(IDC_EXT_ENABLE, BN_CLICKED),
                            (LPARAM)hWnd);
                    }
                    return 0;
                }
            }
        }
    }

    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// 扩展管理窗口过程
LRESULT CALLBACK YXGuiPlugin::ExtMgrWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pThis = (YXGuiPlugin*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

            Gdiplus::SolidBrush bg(Gdiplus::Color(255, 244, 247, 251));
            g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);

            Gdiplus::LinearGradientBrush header(
                Gdiplus::Point(0, 0), Gdiplus::Point(rc.right, 0),
                Gdiplus::Color(255, 30, 57, 94),
                Gdiplus::Color(255, 62, 105, 170));
            g.FillRectangle(&header, 0, 0, rc.right, 52);

            Gdiplus::SolidBrush footer(Gdiplus::Color(255, 238, 243, 249));
            g.FillRectangle(&footer, 0, rc.bottom - 62, rc.right, 62);

            Gdiplus::Pen line(Gdiplus::Color(255, 218, 225, 235), 1.0f);
            g.DrawLine(&line, 20, 51, rc.right - 20, 51);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
        if (!pThis) break;

        if (lpdis->CtlType == ODT_BUTTON) {
            const int id = (int)lpdis->CtlID;
            HDC hdc = lpdis->hDC;
            RECT rc = lpdis->rcItem;
            bool pressed = (lpdis->itemState & ODS_SELECTED) != 0;

            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            Gdiplus::RectF r(
                (Gdiplus::REAL)rc.left + 1,
                (Gdiplus::REAL)rc.top + 1,
                (Gdiplus::REAL)(rc.right - rc.left - 2),
                (Gdiplus::REAL)(rc.bottom - rc.top - 2));

            Gdiplus::GraphicsPath path;
            const float rr = 10.0f;
            path.AddArc(r.X, r.Y, rr, rr, 180, 90);
            path.AddArc(r.X + r.Width - rr, r.Y, rr, rr, 270, 90);
            path.AddArc(r.X + r.Width - rr, r.Y + r.Height - rr, rr, rr, 0, 90);
            path.AddArc(r.X, r.Y + r.Height - rr, rr, rr, 90, 90);
            path.CloseFigure();

            const bool primary =
                id == IDC_EXT_INSTALL_FROM_STORE ||
                id == IDC_EXT_INSTALL_FROM_LINK;

            Gdiplus::SolidBrush fill(
                primary
                    ? (pressed
                        ? Gdiplus::Color(255, 40, 82, 148)
                        : Gdiplus::Color(255, 64, 116, 194))
                    : (pressed
                        ? Gdiplus::Color(255, 222, 231, 243)
                        : Gdiplus::Color(255, 255, 255, 255)));

            g.FillPath(&fill, &path);

            Gdiplus::Pen pen(
                primary
                    ? Gdiplus::Color(255, 102, 153, 218)
                    : Gdiplus::Color(255, 211, 220, 232), 1.0f);
            g.DrawPath(&pen, &path);

            wchar_t text[128] = { 0 };
            GetWindowTextW(lpdis->hwndItem, text, 128);

            Gdiplus::Font font(L"Microsoft YaHei", 12,
                Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush textBrush(
                primary
                    ? Gdiplus::Color(255, 255, 255, 255)
                    : Gdiplus::Color(255, 48, 61, 79));

            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

            g.DrawString(text, -1, &font, r, &fmt, &textBrush);
            return TRUE;
        }

        if (lpdis->CtlID != IDC_EXT_LIST || lpdis->CtlType != ODT_LISTBOX)
            break;

        int idx = (int)lpdis->itemID;
        ExtInfo extCopy;
        bool valid = false;
        {
            std::lock_guard<std::mutex> lock(pThis->m_extInstallMutex);
            if (idx >= 0 && idx < (int)pThis->m_extensions.size()) {
                extCopy = pThis->m_extensions[idx];
                valid = true;
            }
        }
        if (!valid) return TRUE;

        HDC hdc = lpdis->hDC;
        RECT rc = lpdis->rcItem;
        bool selected = (lpdis->itemState & ODS_SELECTED) != 0;

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        Gdiplus::SolidBrush listBg(Gdiplus::Color(255, 244, 247, 251));
        g.FillRectangle(&listBg, Gdiplus::Rect(
            rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top));

        const float margin = 8.0f;
        Gdiplus::RectF card(
            (Gdiplus::REAL)rc.left + margin,
            (Gdiplus::REAL)rc.top + margin,
            (Gdiplus::REAL)(rc.right - rc.left) - margin * 2,
            (Gdiplus::REAL)(rc.bottom - rc.top) - margin * 2);

        const float radius = 14.0f;
        Gdiplus::GraphicsPath cp;
        cp.AddArc(card.X, card.Y, radius, radius, 180, 90);
        cp.AddArc(card.X + card.Width - radius, card.Y, radius, radius, 270, 90);
        cp.AddArc(card.X + card.Width - radius, card.Y + card.Height - radius, radius, radius, 0, 90);
        cp.AddArc(card.X, card.Y + card.Height - radius, radius, radius, 90, 90);
        cp.CloseFigure();

        Gdiplus::SolidBrush cardBrush(selected
            ? Gdiplus::Color(255, 238, 246, 255)
            : Gdiplus::Color(255, 255, 255, 255));
        g.FillPath(&cardBrush, &cp);

        Gdiplus::Pen cardPen(selected
            ? Gdiplus::Color(255, 92, 142, 214)
            : Gdiplus::Color(255, 220, 227, 237),
            selected ? 1.8f : 1.0f);
        g.DrawPath(&cardPen, &cp);

        Gdiplus::SolidBrush statusBrush(
            extCopy.comExt && extCopy.enabled
                ? Gdiplus::Color(255, 49, 164, 111)
                : extCopy.comExt
                    ? Gdiplus::Color(255, 187, 197, 212)
                    : Gdiplus::Color(255, 224, 194, 147));
        g.FillRectangle(&statusBrush, (Gdiplus::REAL)(card.X + 1.0f),
            (Gdiplus::REAL)(card.Y + 18.0f), 4.0f,
            (Gdiplus::REAL)(card.Height - 36.0f));

        int hash = 0;
        for (wchar_t c : extCopy.name) hash = hash * 31 + (int)c;
        BYTE cr = (BYTE)(61 + (hash & 55));
        BYTE cg = (BYTE)(96 + ((hash >> 8) & 70));
        BYTE cb = (BYTE)(162 + ((hash >> 16) & 52));

        const float iconSize = 64.0f;
        const float iconX = card.X + 22.0f;
        const float iconY = card.Y + (card.Height - iconSize) / 2.0f;

        Gdiplus::SolidBrush iconBrush(Gdiplus::Color(255, cr, cg, cb));
        g.FillEllipse(&iconBrush, iconX, iconY, iconSize, iconSize);

        std::wstring initial = extCopy.name.empty() ? L"?" : extCopy.name.substr(0, 1);
        Gdiplus::Font iconFont(L"Microsoft YaHei", 23,
            Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush iconText(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::StringFormat center;
        center.SetAlignment(Gdiplus::StringAlignmentCenter);
        center.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(initial.c_str(), -1, &iconFont,
            Gdiplus::RectF(iconX, iconY, iconSize, iconSize),
            &center, &iconText);

        const float textX = iconX + iconSize + 20.0f;
        const float rightReserve = 190.0f;
        const float textW = (std::max)(170.0f,
            card.Width - (textX - card.X) - rightReserve);

        Gdiplus::Font nameFont(L"Microsoft YaHei", 16,
            Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 35, 45, 60));
        Gdiplus::StringFormat leftFmt;
        leftFmt.SetAlignment(Gdiplus::StringAlignmentNear);
        leftFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        leftFmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        leftFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        std::wstring nameLine = extCopy.name.empty() ? L"未命名扩展" : extCopy.name;
        if (!extCopy.version.empty())
            nameLine += L"   ·   v" + extCopy.version;

        g.DrawString(nameLine.c_str(), -1, &nameFont,
            Gdiplus::RectF(textX, card.Y + 18, textW, 28),
            &leftFmt, &nameBrush);

        Gdiplus::Font descFont(L"Microsoft YaHei", 12,
            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush descBrush(Gdiplus::Color(255, 112, 124, 143));
        std::wstring desc = extCopy.description.empty() ? L"未提供描述" : extCopy.description;
        if (desc.length() > 92) desc = desc.substr(0, 89) + L"...";

        g.DrawString(desc.c_str(), -1, &descFont,
            Gdiplus::RectF(textX, card.Y + 51, textW, 24),
            &leftFmt, &descBrush);

        std::wstring stateText = extCopy.comExt
            ? (extCopy.enabled ? L"已启用" : L"已停用")
            : L"未注册";

        Gdiplus::Font stateFont(L"Microsoft YaHei", 11,
            Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush stateBg(
            extCopy.comExt
                ? (extCopy.enabled
                    ? Gdiplus::Color(255, 231, 247, 239)
                    : Gdiplus::Color(255, 240, 242, 246))
                : Gdiplus::Color(255, 255, 243, 226));

        Gdiplus::RectF pill(card.X + card.Width - 164, card.Y + 18, 66, 24);
        Gdiplus::GraphicsPath pp;
        const float pr = 12.0f;
        pp.AddArc(pill.X, pill.Y, pr, pr, 180, 90);
        pp.AddArc(pill.X + pill.Width - pr, pill.Y, pr, pr, 270, 90);
        pp.AddArc(pill.X + pill.Width - pr, pill.Y + pill.Height - pr, pr, pr, 0, 90);
        pp.AddArc(pill.X, pill.Y + pill.Height - pr, pr, pr, 90, 90);
        pp.CloseFigure();
        g.FillPath(&stateBg, &pp);

        Gdiplus::SolidBrush stateBrush(
            extCopy.comExt
                ? (extCopy.enabled
                    ? Gdiplus::Color(255, 45, 141, 95)
                    : Gdiplus::Color(255, 111, 123, 143))
                : Gdiplus::Color(255, 181, 119, 40));

        Gdiplus::StringFormat pillFmt;
        pillFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        pillFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(stateText.c_str(), -1, &stateFont, pill, &pillFmt, &stateBrush);

        const float swW = 52.0f;
        const float swH = 28.0f;
        const float swX = card.X + card.Width - swW - 20.0f;
        const float swY = card.Y + card.Height - swH - 18.0f;

        Gdiplus::GraphicsPath swPath;
        swPath.AddArc(swX, swY, swH, swH, 90, 180);
        swPath.AddArc(swX + swW - swH, swY, swH, swH, 270, 180);
        swPath.CloseFigure();

        Gdiplus::SolidBrush swBrush(
            extCopy.comExt
                ? (extCopy.enabled
                    ? Gdiplus::Color(255, 55, 162, 110)
                    : Gdiplus::Color(255, 190, 199, 212))
                : Gdiplus::Color(255, 224, 194, 147));
        g.FillPath(&swBrush, &swPath);

        const float knob = swH - 4.0f;
        const float knobX = extCopy.comExt && extCopy.enabled
            ? swX + swW - swH + 2.0f : swX + 2.0f;
        Gdiplus::SolidBrush knobBrush(Gdiplus::Color(255, 255, 255, 255));
        g.FillEllipse(&knobBrush, knobX, swY + 2, knob, knob);

        return TRUE;
    }

    case WM_COMMAND: {
        if (!pThis) break;
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (code != BN_CLICKED) break;

        int sel = (int)SendMessageW(pThis->m_hExtList, LB_GETCURSEL, 0, 0);

        if (id == IDC_EXT_REMOVE) {
            if (sel < 0 || sel >= (int)pThis->m_extensions.size()) {
                MessageBoxW(hWnd, L"请先选择一个扩展", L"提示",
                    MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            if (MessageBoxW(hWnd, L"确定要卸载这个扩展吗？", L"确认",
                MB_YESNO | MB_ICONQUESTION) == IDYES) {
                std::wstring extPath;
                ICoreWebView2BrowserExtension* comExt = nullptr;
                {
                    std::lock_guard<std::mutex> lock(pThis->m_extInstallMutex);
                    if (sel < (int)pThis->m_extensions.size()) {
                        extPath = pThis->m_extensions[sel].path;
                        comExt = pThis->m_extensions[sel].comExt;
                        if (comExt) comExt->AddRef();
                    }
                }

                if (comExt) {
                    comExt->Remove(
                        Callback<ICoreWebView2BrowserExtensionRemoveCompletedHandler>(
                            [](HRESULT) -> HRESULT { return S_OK; }).Get());
                }

                {
                    std::lock_guard<std::mutex> lock(pThis->m_extInstallMutex);
                    if (sel < (int)pThis->m_extensions.size()) {
                        if (pThis->m_extensions[sel].comExt) {
                            pThis->m_extensions[sel].comExt->Release();
                            pThis->m_extensions[sel].comExt = nullptr;
                        }
                        pThis->m_extensions.erase(pThis->m_extensions.begin() + sel);
                    }
                }
                if (comExt) comExt->Release();

                if (!extPath.empty()) {
                    std::error_code ec;
                    fs::remove_all(fs::path(extPath), ec);
                }

                SendMessageW(pThis->m_hExtList, LB_RESETCONTENT, 0, 0);
                {
                    std::lock_guard<std::mutex> lock(pThis->m_extInstallMutex);
                    for (size_t i = 0; i < pThis->m_extensions.size(); ++i) {
                        SendMessageW(pThis->m_hExtList, LB_ADDSTRING, 0,
                            (LPARAM)pThis->m_extensions[i].name.c_str());
                    }
                    if (!pThis->m_extensions.empty())
                        SendMessageW(pThis->m_hExtList, LB_SETCURSEL, 0, 0);
                }
                InvalidateRect(pThis->m_hExtList, NULL, TRUE);
            }
            return 0;
        }

        if (sel < 0 || sel >= (int)pThis->m_extensions.size()) {
            if (id != IDC_EXT_ADD &&
                id != IDC_EXT_OPEN_DIR &&
                id != IDC_EXT_INSTALL_FROM_STORE &&
                id != IDC_EXT_INSTALL_FROM_LINK) {
                MessageBoxW(hWnd, L"请先选择一个扩展",
                    L"提示", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
        }

        ExtInfo extCopy;
        bool hasExt = false;
        if (sel >= 0 && sel < (int)pThis->m_extensions.size()) {
            std::lock_guard<std::mutex> lock(pThis->m_extInstallMutex);
            if (sel < (int)pThis->m_extensions.size()) {
                extCopy = pThis->m_extensions[sel];
                hasExt = true;
            }
        }

        switch (id) {
        case IDC_EXT_OPEN_POPUP:
            if (hasExt) {
                if (extCopy.popupPath.empty()) {
                    MessageBoxW(hWnd, L"该扩展没有 popup 页面",
                        L"提示", MB_OK | MB_ICONINFORMATION);
                }
                else if (extCopy.id.empty()) {
                    MessageBoxW(hWnd,
                        L"该扩展尚未成功注册到 WebView2。",
                        L"提示", MB_OK | MB_ICONWARNING);
                }
                else {
                    pThis->OpenExtensionPopup(extCopy.id, extCopy.popupPath);
                }
            }
            return 0;

        case IDC_EXT_OPEN_OPTIONS:
            if (hasExt) {
                if (extCopy.optionsPath.empty()) {
                    MessageBoxW(hWnd, L"该扩展没有 options 页面",
                        L"提示", MB_OK | MB_ICONINFORMATION);
                }
                else if (extCopy.id.empty()) {
                    MessageBoxW(hWnd,
                        L"该扩展尚未成功注册到 WebView2。",
                        L"提示", MB_OK | MB_ICONWARNING);
                }
                else {
                    pThis->OpenExtensionOptions(extCopy.id, extCopy.optionsPath);
                }
            }
            return 0;

        case IDC_EXT_ENABLE:
            if (hasExt && extCopy.comExt) {
                const BOOL targetEnabled = extCopy.enabled ? FALSE : TRUE;
                const std::wstring extPath = extCopy.path;

                extCopy.comExt->Enable(
                    targetEnabled,
                    Callback<ICoreWebView2BrowserExtensionEnableCompletedHandler>(
                        [pThis, extPath, targetEnabled](HRESULT result) -> HRESULT {
                            if (SUCCEEDED(result)) {
                                std::lock_guard<std::mutex> lock(pThis->m_extInstallMutex);
                                for (auto& ext : pThis->m_extensions) {
                                    if (_wcsicmp(ext.path.c_str(), extPath.c_str()) == 0) {
                                        ext.enabled = (targetEnabled != FALSE);
                                        break;
                                    }
                                }
                                if (pThis->m_hExtList && IsWindow(pThis->m_hExtList))
                                    InvalidateRect(pThis->m_hExtList, NULL, TRUE);
                            }
                            return S_OK;
                        }).Get());
            }
            return 0;

        case IDC_EXT_OPEN_DIR:
            pThis->ShowExtensions();
            return 0;

        case IDC_EXT_ADD: {
            BROWSEINFOW bi = { 0 };
            bi.hwndOwner = hWnd;
            bi.lpszTitle = L"选择扩展文件夹（包含 manifest.json）";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH] = { 0 };
                if (SHGetPathFromIDListW(pidl, path)) {
                    std::wstring manifestPath = std::wstring(path) + L"\\manifest.json";
                    if (GetFileAttributesW(manifestPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        MessageBoxW(hWnd,
                            L"该目录下没有 manifest.json，不是有效的扩展",
                            L"错误", MB_OK | MB_ICONERROR);
                    }
                    else {
                        std::string destRoot = GetCacheSubDir("Extensions");
                        std::wstring destName = std::wstring(path);
                        size_t p = destName.find_last_of(L"\\/");
                        if (p != std::wstring::npos)
                            destName = destName.substr(p + 1);
                        std::wstring destPath = AnsiToWide(destRoot) + L"\\" + destName;
                        std::wstring cmd = L"xcopy /E /I /Y \"" +
                            std::wstring(path) + L"\" \"" + destPath + L"\"";

                        STARTUPINFOW si = { sizeof(si) };
                        si.dwFlags = STARTF_USESHOWWINDOW;
                        si.wShowWindow = SW_HIDE;
                        PROCESS_INFORMATION pi = { 0 };
                        std::wstring cmdLine = L"cmd.exe /c " + cmd;
                        if (CreateProcessW(NULL, &cmdLine[0], NULL, NULL, FALSE,
                            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                            WaitForSingleObject(pi.hProcess, 30000);
                            CloseHandle(pi.hProcess);
                            CloseHandle(pi.hThread);
                        }

                        pThis->AddExtensionToProfileOnUIThread(destPath);
                        pThis->ScanExtensions();
                        SendMessageW(pThis->m_hExtList, LB_RESETCONTENT, 0, 0);
                        for (size_t i = 0; i < pThis->m_extensions.size(); i++) {
                            SendMessageW(pThis->m_hExtList, LB_ADDSTRING, 0,
                                (LPARAM)pThis->m_extensions[i].name.c_str());
                        }
                        if (!pThis->m_extensions.empty())
                            SendMessageW(pThis->m_hExtList, LB_SETCURSEL, 0, 0);
                        InvalidateRect(pThis->m_hExtList, NULL, TRUE);

                        MessageBoxW(hWnd,
                            L"扩展已添加，并尝试立即启用。",
                            L"提示", MB_OK | MB_ICONINFORMATION);
                    }
                }
                CoTaskMemFree(pidl);
            }
            return 0;
        }

        case IDC_EXT_INSTALL_FROM_STORE:
            pThis->OpenExtensionStore();
            DestroyWindow(hWnd);
            return 0;

        case IDC_EXT_INSTALL_FROM_LINK: {
            std::wstring url;
            if (!ShowInputDialog(pThis->m_hInstance, hWnd,
                L"从链接安装扩展",
                L"粘贴 Edge / Chrome 商店的扩展详情页链接：",
                url)) {
                return 0;
            }
            if (url.empty()) return 0;

            std::wstring extId = ExtractExtIdFromUrl(url);
            if (extId.empty()) {
                MessageBoxW(hWnd,
                    L"无法从链接中解析扩展 ID。\n\n"
                    L"请确认链接形如：\n"
                    L"https://microsoftedge.microsoft.com/addons/detail/<名称>/<32位ID>",
                    L"解析失败", MB_OK | MB_ICONWARNING);
                return 0;
            }

            std::wstring crxUrl =
                L"https://edge.microsoft.com/extensionwebstorebase/v1/crx"
                L"?response=redirect&x=id%3D" + extId +
                L"%26installsource%3Dondemand%26uc";

            pThis->RequestDownloadViaMainProgram(crxUrl, L"");
            DestroyWindow(hWnd);
            return 0;
        }
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        if (pThis) {
            pThis->m_hExtMgrWnd = nullptr;
            pThis->m_hExtList = nullptr;
            pThis->m_hExtDetail = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// JS 插件系统（.zip）
// ============================================================
void YXGuiPlugin::LoadJSPlugins() {
    m_jsPlugins.clear();
    m_aiIndex = -1;
    m_currentPluginIndex = -1;
    m_sidebarActive = -2;

    std::string guiDirA = GetCacheSubDir("gui");
    std::wstring guiDirW = AnsiToWide(guiDirA);
    SHCreateDirectoryExW(NULL, guiDirW.c_str(), NULL);

// 初始化权限管理器
    PermissionManager::Instance().SetStorePath(guiDirW + L"\\cf.m");
    PermissionManager::Instance().Load();

    // ===== 内置功能：光语 AI（原生 C++，不是插件，不参与插件列表） =====
    {
        auto p = std::make_unique<ApiAiNativePlugin>();
        p->SetOwnerWnd(m_hWnd);
        p->SetLightCommandFn([this](const std::string& cmd) {
            if (m_commandHandler) m_commandHandler(cmd);
            });
        p->SetPermissionPrompt([this](
            const std::wstring& id,
            const std::wstring& name,
            const std::wstring& desc) -> bool {
                return PromptPermission(id, name, desc);
            });

        if (PermissionManager::Instance().IsPermanentlyGranted(p->GetId())) {
            p->MarkPermanentlyAuthorized();
        }
        p->CallOnLoad();
        m_aiIndex = (int)m_jsPlugins.size();
        m_jsPlugins.push_back(std::move(p));
    }

    // 扫描 gui 目录下的子文件夹：含 plugin.json 的目录视为插件，直接加载。
    // 不再依赖 .zip 压缩包（安装插件时直接以文件夹形式放置即可）。
    std::wstring pattern = guiDirW + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;

        std::wstring destDir = guiDirW + L"\\" + name;
        std::wstring manifestPath = destDir + L"\\plugin.json";
        if (GetFileAttributesW(manifestPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            continue;   // 该目录不是插件（无 manifest），跳过
        }

        // 跳过与内置插件同 id 的外部目录（内置已加载，避免重复）
        {
            std::ifstream mf(manifestPath, std::ios::binary);
            if (mf.is_open()) {
                std::string manifestJson((std::istreambuf_iterator<char>(mf)),
                    std::istreambuf_iterator<char>());
                if (JsonExtractString(manifestJson, "id") ==
                    std::wstring(L"builtin.light.ai")) {
                    continue;
                }
            }
        }

        // 直接加载已解压的插件文件夹（无需解压、不删除任何目录）
        auto p = std::make_unique<JSPlugin>();
        p->SetOwnerWnd(m_hWnd);
        p->SetLightCommandFn([this](const std::string& cmd) {
            if (m_commandHandler) m_commandHandler(cmd);
            });
        p->SetPermissionPrompt([this](
            const std::wstring& id,
            const std::wstring& name,
            const std::wstring& desc) -> bool {
                return PromptPermission(id, name, desc);
            });

        if (p->Load(destDir, L"")) {
            if (PermissionManager::Instance().IsPermanentlyGranted(p->GetId())) {
                p->MarkPermanentlyAuthorized();
            }
            p->CallOnLoad();
            m_jsPlugins.push_back(std::move(p));
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    // 心跳定时器：驱动 JS 插件动态壁纸轮播（500ms 粒度）
    if (m_hWnd) SetTimer(m_hWnd, 1, 500, NULL);

    // 加载 GUI 附属插件（Cache\gui\dll\*.dll，无指令，仅 JS API / Hook / 原生面板）
    GuiDllPluginHost::Instance()->LoadAll();

    // 收集 DLL 注册的原生面板（供侧边栏渲染新 Tab）
    LoadNativePanels();
}

JSPlugin* YXGuiPlugin::CurrentJSPlugin() {
    if (m_currentPluginIndex >= 0 &&
        m_currentPluginIndex < (int)m_jsPlugins.size()) {
        return m_jsPlugins[m_currentPluginIndex].get();
    }
    return nullptr;
}

// ========== T9：video / lively 背景渲染辅助 ==========

// 常见图片扩展名
static bool IsImageExt(const std::wstring& ext) {
    std::wstring e;
    for (auto& c : ext) e.push_back((wchar_t)towlower(c));
    return e == L".jpg" || e == L".jpeg" || e == L".png" || e == L".bmp"
        || e == L".gif" || e == L".webp" || e == L".tif" || e == L".tiff";
}
// 常见视频扩展名
static bool IsVideoExt(const std::wstring& ext) {
    std::wstring e;
    for (auto& c : ext) e.push_back((wchar_t)towlower(c));
    return e == L".mp4" || e == L".webm" || e == L".avi" || e == L".mkv"
        || e == L".mov" || e == L".wmv" || e == L".flv";
}

// 用 Shell 缩略图 API 提取视频首帧为 Bitmap（无需 Media Foundation 解码栈）
// 返回 nullptr 表示失败（文件不存在/无缩略图提供器）
static Gdiplus::Bitmap* CreateVideoThumbBitmap(const std::wstring& videoPath) {
    if (!fs::exists(videoPath)) return nullptr;
    ComPtr<IShellItemImageFactory> factory;
    HRESULT hr = SHCreateItemFromParsingName(videoPath.c_str(), nullptr, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return nullptr;
    SIZE s = { 960, 540 };
    HBITMAP hbmp = nullptr;
    hr = factory->GetImage(s, SIIGBF_RESIZETOFIT, &hbmp);
    if (FAILED(hr) || !hbmp) return nullptr;
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromHBITMAP(hbmp, nullptr);
    DeleteObject(hbmp);
    if (bmp && bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return nullptr; }
    return bmp;
}

// 解析 project.json 的字符串字段（极简 JSON 取值，用于 Lively 壁纸识别）
static std::wstring ReadJsonStrField(const std::wstring& text, const std::wstring& key) {
    std::wstring pat = L"\"" + key + L"\"";
    size_t pos = text.find(pat);
    if (pos == std::wstring::npos) return L"";
    size_t colon = text.find(L':', pos + pat.size());
    if (colon == std::wstring::npos) return L"";
    size_t q1 = text.find(L'"', colon + 1);
    if (q1 == std::wstring::npos) return L"";
    size_t q2 = text.find(L'"', q1 + 1);
    if (q2 == std::wstring::npos) return L"";
    return text.substr(q1 + 1, q2 - q1 - 1);
}

// 从 Lively 壁纸目录解析出可渲染的媒体路径：
// 优先 project.json 的 "file"（图片直接返回，视频返回视频），
// 其次 "preview"/"thumbs" 预览图，最后回退扫描目录中的第一张图片。
static std::wstring FindLivelyCover(const std::wstring& livelyDir) {
    if (livelyDir.empty() || !fs::is_directory(livelyDir)) return L"";
    std::wstring proj = livelyDir + L"\\project.json";
    if (fs::exists(proj)) {
        std::wifstream in(proj, std::ios::binary);
        if (in) {
            std::wstringstream ss;
            ss << in.rdbuf();
            std::wstring text = ss.str();
            std::wstring file = ReadJsonStrField(text, L"file");
            if (!file.empty()) {
                std::wstring p = livelyDir + L"\\" + file;
                if (fs::exists(p)) {
                    std::wstring ext = fs::path(file).extension().wstring();
                    if (IsImageExt(ext) || IsVideoExt(ext)) return p;
                }
            }
            // 预览图回退
            std::wstring prev = ReadJsonStrField(text, L"preview");
            if (prev.empty()) prev = ReadJsonStrField(text, L"thumbs");
            if (!prev.empty()) {
                std::wstring p = livelyDir + L"\\" + prev;
                if (fs::exists(p) && IsImageExt(fs::path(prev).extension().wstring())) return p;
            }
        }
    }
    // 回退：目录中第一张图片
    try {
        for (auto& e : fs::directory_iterator(livelyDir)) {
            if (!e.is_regular_file()) continue;
            std::wstring ext = e.path().extension().wstring();
            if (IsImageExt(ext)) return e.path().wstring();
        }
    } catch (...) {}
    return L"";
}

// 绘制当前插件视图背景壁纸：none -> 默认浅色；solid -> 纯色；
// image -> 拉申图片；cycle -> 轮播当前图片（imgCache 缓存 Bitmap）
void YXGuiPlugin::PaintPluginHostBackground(HDC hdc) {
    RECT rc;
    GetClientRect(m_hPluginHost, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w < 1 || h < 1) return;

    JSPlugin* cur = CurrentJSPlugin();
    const PluginWallpaper* wp = cur ? cur->GetWallpaper() : nullptr;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    if (!wp || wp->mode == 0) {
        // 默认：浅米白渐变（干净、不刺眼）
        Gdiplus::LinearGradientBrush grad(
            Gdiplus::RectF(0, 0, (Gdiplus::REAL)w, (Gdiplus::REAL)h),
            Gdiplus::Color(255, 246, 249, 253),
            Gdiplus::Color(255, 236, 242, 250),
            Gdiplus::LinearGradientModeVertical);
        g.FillRectangle(&grad, Gdiplus::REAL(0), Gdiplus::REAL(0), Gdiplus::REAL(w), Gdiplus::REAL(h));
        return;
    }

    if (wp->mode == 1) {
        Gdiplus::Color c(
            GetRValue(wp->color), GetGValue(wp->color), GetBValue(wp->color));
        Gdiplus::SolidBrush b(c);
        g.FillRectangle(&b, Gdiplus::REAL(0), Gdiplus::REAL(0), Gdiplus::REAL(w), Gdiplus::REAL(h));
        return;
    }

    // image / cycle / video / lively：当前要显示的图片路径
    std::wstring imgPath;
    bool isVideoSrc = false;   // 渲染源是视频文件（走缩略图提取）
    if (wp->mode == 2) imgPath = wp->image;
    else if (wp->mode == 3 && !wp->cycleFiles.empty()) {
        size_t idx = wp->cycleIndex % wp->cycleFiles.size();
        imgPath = wp->cycleFiles[idx];
    }
    else if (wp->mode == 4) {
        // video：Shell 缩略图 API 提取首帧静态展示（循环播放后续由宿主定时重绘驱动）
        imgPath = wp->videoPath;
        isVideoSrc = true;
    }
    else if (wp->mode == 5) {
        // lively：解析 project.json 定位主媒体；视频同样走首帧缩略图
        std::wstring cover = FindLivelyCover(wp->livelyDir);
        if (cover.empty()) {
            Gdiplus::SolidBrush b(Gdiplus::Color(255, 245, 247, 251));
            g.FillRectangle(&b, Gdiplus::REAL(0), Gdiplus::REAL(0), Gdiplus::REAL(w), Gdiplus::REAL(h));
            return;
        }
        imgPath = cover;
        if (IsVideoExt(fs::path(cover).extension().wstring())) isVideoSrc = true;
    }
    if (imgPath.empty()) {
        Gdiplus::SolidBrush b(Gdiplus::Color(255, 245, 247, 251));
        g.FillRectangle(&b, Gdiplus::REAL(0), Gdiplus::REAL(0), Gdiplus::REAL(w), Gdiplus::REAL(h));
        return;
    }

    // 按路径缓存 Bitmap：路径未变直接复用，变了才重建
    Gdiplus::Bitmap* bmp = (Gdiplus::Bitmap*)wp->imgCache;
    if (bmp && wp->imgCachePath == imgPath) {
        // 命中缓存
    } else {
        if (bmp) { delete bmp; ((PluginWallpaper*)wp)->imgCache = nullptr; }
        Gdiplus::Bitmap* newBmp = isVideoSrc
            ? CreateVideoThumbBitmap(imgPath)
            : Gdiplus::Bitmap::FromFile(imgPath.c_str());
        if (!newBmp || newBmp->GetLastStatus() != Gdiplus::Ok) {
            delete newBmp;
            ((PluginWallpaper*)wp)->imgCachePath.clear();
            Gdiplus::SolidBrush b(Gdiplus::Color(255, 245, 247, 251));
            g.FillRectangle(&b, Gdiplus::REAL(0), Gdiplus::REAL(0), Gdiplus::REAL(w), Gdiplus::REAL(h));
            return;
        }
        ((PluginWallpaper*)wp)->imgCache = newBmp;
        ((PluginWallpaper*)wp)->imgCachePath = imgPath;
        bmp = newBmp;
    }

    int iw = bmp->GetWidth(), ih = bmp->GetHeight();
    if (iw < 1 || ih < 1) {
        Gdiplus::SolidBrush b(Gdiplus::Color(255, 245, 247, 251));
        g.FillRectangle(&b, Gdiplus::REAL(0), Gdiplus::REAL(0), Gdiplus::REAL(w), Gdiplus::REAL(h));
        return;
    }
    // 封面式裁剪填充（保留比例，不留黑边）
    float scale = (float)fmax((double)w / iw, (double)h / ih);
    int dw = (int)(iw * scale), dh = (int)(ih * scale);
    int dx = (w - dw) / 2, dy = (h - dh) / 2;
    g.DrawImage(bmp, Gdiplus::REAL(dx), Gdiplus::REAL(dy), Gdiplus::REAL(dw), Gdiplus::REAL(dh));
}

void YXGuiPlugin::UnloadJSPlugins() {
    CloseNativePanel();
    for (auto& p : m_jsPlugins) {
        if (p) {
            p->CallOnUnload();
            p->Unload();
        }
    }
    m_jsPlugins.clear();
    m_nativePanels.clear();
    m_aiIndex = -1;
    m_currentPluginIndex = -1;
}

int YXGuiPlugin::FindPluginIndexById(const std::wstring& id) const {
    for (size_t i = 0; i < m_jsPlugins.size(); ++i) {
        if (m_jsPlugins[i]->GetId() == id) return (int)i;
    }
    return -1;
}

bool YXGuiPlugin::PromptPermission(const std::wstring& pluginId,
    const std::wstring& pluginName,
    const std::wstring& actionDesc) {
    int r = PermissionManager::Instance().Prompt(m_hWnd, pluginId, pluginName, actionDesc);
    return r != 0;
}

void YXGuiPlugin::RefreshSidebarJS() {
    RefreshSidebar();
}

void YXGuiPlugin::OpenMainView() {
    // 关闭原生面板视图（如果有）
    CloseNativePanel();

    // 通知当前插件隐藏
    if (m_currentPluginIndex >= 0 && m_currentPluginIndex < (int)m_jsPlugins.size()) {
        m_jsPlugins[m_currentPluginIndex]->CallOnViewHide();
    }

    // 隐藏插件宿主
    if (m_hPluginHost && IsWindow(m_hPluginHost))
        ShowWindow(m_hPluginHost, SW_HIDE);

    // 显示主界面控件
    if (m_hTabBar) ShowWindow(m_hTabBar, SW_SHOW);
    if (m_hTabContainer) ShowWindow(m_hTabContainer, SW_SHOW);
    if (m_hAddressBar) ShowWindow(m_hAddressBar, SW_SHOW);
    if (m_hHelpEdit) ShowWindow(m_hHelpEdit, SW_SHOW);
    if (m_hConsoleEdit) ShowWindow(m_hConsoleEdit, SW_SHOW);
    if (m_hConsoleInput) ShowWindow(m_hConsoleInput, SW_SHOW);
    if (m_hPromptStatic) ShowWindow(m_hPromptStatic, SW_SHOW);

    for (const auto& item : m_config.toolbarOrder) {
        HWND h = GetToolbarHwndById(item.id);
        if (h && item.visible) ShowWindow(h, SW_SHOW);
    }
    for (HWND h : m_aiButtons) if (h && IsWindow(h)) ShowWindow(h, SW_SHOW);

    m_currentPluginIndex = -1;

    if (m_hWnd) {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        OnSize(rc.right, rc.bottom);
    }
}

void YXGuiPlugin::OpenJSPluginView(int index) {
    if (index < 0 || index >= (int)m_jsPlugins.size()) return;

    // 关闭原生面板视图（如果有）
    CloseNativePanel();

    // 隐藏所有主界面控件
    if (m_hTabBar) ShowWindow(m_hTabBar, SW_HIDE);
    if (m_hTabContainer) ShowWindow(m_hTabContainer, SW_HIDE);
    if (m_hAddressBar) ShowWindow(m_hAddressBar, SW_HIDE);
    if (m_hHelpEdit) ShowWindow(m_hHelpEdit, SW_HIDE);
    if (m_hConsoleEdit) ShowWindow(m_hConsoleEdit, SW_HIDE);
    if (m_hConsoleInput) ShowWindow(m_hConsoleInput, SW_HIDE);
    if (m_hPromptStatic) ShowWindow(m_hPromptStatic, SW_HIDE);
    if (m_hRestoreBtn) ShowWindow(m_hRestoreBtn, SW_HIDE);

    for (const auto& item : m_config.toolbarOrder) {
        HWND h = GetToolbarHwndById(item.id);
        if (h) ShowWindow(h, SW_HIDE);
    }
    for (HWND h : m_aiButtons) if (h && IsWindow(h)) ShowWindow(h, SW_HIDE);

    m_currentPluginIndex = index;

    // 先布局让宿主尺寸正确
    if (m_hWnd) {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        OnSize(rc.right, rc.bottom);
    }

    // 显示插件宿主
    if (m_hPluginHost && IsWindow(m_hPluginHost))
        ShowWindow(m_hPluginHost, SW_SHOW);

    // 通知插件
    RECT hostRc = { 0, 0, 800, 600 };
    if (m_hPluginHost && IsWindow(m_hPluginHost)) {
        GetClientRect(m_hPluginHost, &hostRc);
    }
    int w = hostRc.right - hostRc.left;
    int h = hostRc.bottom - hostRc.top;
    if (w < 1) w = 800;
    if (h < 1) h = 600;

    // 背景壁纸立即生效
    if (m_hPluginHost && IsWindow(m_hPluginHost)) {
        InvalidateRect(m_hPluginHost, NULL, TRUE);
    }

    m_jsPlugins[index]->CallOnViewShow(m_hPluginHost, w, h);
}

void YXGuiPlugin::LoadNativePanels() {
    CloseNativePanel();
    m_nativePanels.clear();
    if (g_guiDllHost) {
        m_nativePanels = g_guiDllHost->GetNativePanels();
    }
}

void YXGuiPlugin::CloseNativePanel() {
    if (m_nativePanelHwnd && IsWindow(m_nativePanelHwnd)) {
        int idx = m_activeNativePanel;
        if (idx >= 0 && idx < (int)m_nativePanels.size() &&
            m_nativePanels[idx].destroyFn) {
            m_nativePanels[idx].destroyFn(m_nativePanelHwnd);
        }
        else {
            DestroyWindow(m_nativePanelHwnd);
        }
    }
    m_nativePanelHwnd = nullptr;
    m_activeNativePanel = -1;
}

void YXGuiPlugin::OpenNativePanelView(int index) {
    if (index < 0 || index >= (int)m_nativePanels.size()) return;

    // 通知当前 JS 插件隐藏
    if (m_currentPluginIndex >= 0 &&
        m_currentPluginIndex < (int)m_jsPlugins.size()) {
        m_jsPlugins[m_currentPluginIndex]->CallOnViewHide();
    }
    m_currentPluginIndex = -1;

    // 先切换活动面板索引（让 OnSize 走插件视图布局分支）
    if (m_activeNativePanel != index) {
        CloseNativePanel();
        m_activeNativePanel = index;
    }

    // 隐藏所有主界面控件
    if (m_hTabBar) ShowWindow(m_hTabBar, SW_HIDE);
    if (m_hTabContainer) ShowWindow(m_hTabContainer, SW_HIDE);
    if (m_hAddressBar) ShowWindow(m_hAddressBar, SW_HIDE);
    if (m_hHelpEdit) ShowWindow(m_hHelpEdit, SW_HIDE);
    if (m_hConsoleEdit) ShowWindow(m_hConsoleEdit, SW_HIDE);
    if (m_hConsoleInput) ShowWindow(m_hConsoleInput, SW_HIDE);
    if (m_hPromptStatic) ShowWindow(m_hPromptStatic, SW_HIDE);
    if (m_hRestoreBtn) ShowWindow(m_hRestoreBtn, SW_HIDE);
    for (const auto& item : m_config.toolbarOrder) {
        HWND h = GetToolbarHwndById(item.id);
        if (h) ShowWindow(h, SW_HIDE);
    }
    for (HWND h : m_aiButtons) if (h && IsWindow(h)) ShowWindow(h, SW_HIDE);

    // 先布局让宿主尺寸正确（此时 nativePanelActive 已为 true）
    if (m_hWnd) {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        OnSize(rc.right, rc.bottom);
    }

    // 显示插件宿主容器
    if (m_hPluginHost && IsWindow(m_hPluginHost))
        ShowWindow(m_hPluginHost, SW_SHOW);

    // 创建面板窗口（复用 / 重建）
    const NativePanelRecord& panel = m_nativePanels[index];
    if (!m_nativePanelHwnd || !IsWindow(m_nativePanelHwnd)) {
        if (panel.createFn) {
            m_nativePanelHwnd = panel.createFn(panel.ctx, m_hPluginHost);
        }
    }
    if (m_nativePanelHwnd && IsWindow(m_nativePanelHwnd)) {
        RECT rc{ 0, 0, 800, 600 };
        if (m_hPluginHost && IsWindow(m_hPluginHost))
            GetClientRect(m_hPluginHost, &rc);
        MoveWindow(m_nativePanelHwnd, 0, 0,
            rc.right - rc.left, rc.bottom - rc.top, TRUE);
        ShowWindow(m_nativePanelHwnd, SW_SHOW);
        SetFocus(m_nativePanelHwnd);
    }
}

void YXGuiPlugin::SwitchView(int sidebarIndex) {
    m_sidebarActive = sidebarIndex;

    if (sidebarIndex == -2) {
        OpenMainView();
    }
    else if (sidebarIndex == -3) {
        OpenBuiltinAiView();
    }
    else if (sidebarIndex >= NATIVE_PANEL_BASE) {
        int idx = sidebarIndex - NATIVE_PANEL_BASE;
        if (idx >= 0 && idx < (int)m_nativePanels.size()) {
            OpenNativePanelView(idx);
        }
        else {
            OpenMainView();
        }
    }
    else if (sidebarIndex >= 0 && sidebarIndex < (int)m_jsPlugins.size()) {
        if (!IsJSPluginDisabled(m_jsPlugins[sidebarIndex]->GetId())) {
            OpenJSPluginView(sidebarIndex);
        }
        else {
            OpenMainView();
        }
    }

    RefreshSidebar();
}

// ============================================================
// 内置「光语 AI」与插件中心
// ============================================================
JSPlugin* YXGuiPlugin::BuiltinAiPlugin() const {
    if (m_aiIndex >= 0 && m_aiIndex < (int)m_jsPlugins.size()) {
        return m_jsPlugins[m_aiIndex].get();
    }
    return nullptr;
}

void YXGuiPlugin::OpenBuiltinAiView() {
    if (!m_aiEnabled) {
        OpenMainView();
        return;
    }
    if (m_aiIndex >= 0 && m_aiIndex < (int)m_jsPlugins.size()) {
        m_sidebarActive = -3;
        OpenJSPluginView(m_aiIndex);
    }
}

bool YXGuiPlugin::IsJSPluginDisabled(const std::wstring& id) const {
    return m_config.disabledPlugins.find(id) != m_config.disabledPlugins.end();
}

void YXGuiPlugin::SetJSPluginDisabled(const std::wstring& id, bool disabled) {
    if (disabled) m_config.disabledPlugins.insert(id);
    else m_config.disabledPlugins.erase(id);

    if (disabled) {
        int index = FindPluginIndexById(id);
        if (index >= 0 && index == m_currentPluginIndex) {
            OpenMainView();
        }
        if (m_sidebarActive >= 0) {
            int activeIndex = m_sidebarActive;
            if (activeIndex < (int)m_jsPlugins.size() &&
                m_jsPlugins[activeIndex]->GetId() == id) {
                m_sidebarActive = -2;
            }
        }
    }

    SaveConfig();
    RefreshSidebar();
}

void YXGuiPlugin::ReloadJSPlugins() {
    if (m_hPluginHost && IsWindow(m_hPluginHost)) {
        ShowWindow(m_hPluginHost, SW_HIDE);
    }
    m_currentPluginIndex = -1;
    m_sidebarActive = -2;
    OpenMainView();
    UnloadJSPlugins();
    LoadJSPlugins();
    RefreshSidebar();
    RefreshPluginCenter();
}

std::vector<PluginCenterItem> YXGuiPlugin::BuildPluginCenterItems() {
    std::vector<PluginCenterItem> items;

    // 1) 内置功能：光语 AI
    {
        PluginCenterItem item;
        item.id = L"builtin.light.ai";
        item.name = L"AI接口";
        item.version = L"1.0.0";
        item.author = L"Light 内置";
        item.description = L"内置 AI 对话与网页模式，属于插件，需安装。";
        item.kind = L"内置功能";
        item.enabled = m_aiEnabled;
        item.builtin = true;
        item.canToggle = true;
        item.canOpenFolder = false;
        item.canReload = false;
        items.push_back(item);
    }

    // 2) 原生插件（api_*.dll 指令集 / 外置 GUI / 普通 DLL）
    if (g_PluginManager) {
        auto records = g_PluginManager->GetPluginRecords();
        for (const auto& record : records) {
            PluginCenterItem item;
            item.id = AnsiToWide(record.id);
            item.name = AnsiToWide(record.name.empty() ? record.id : record.name);
            item.version = AnsiToWide(record.version.empty() ? "1.0.0" : record.version);
            item.author = AnsiToWide(record.author.empty() ? "未知作者" : record.author);
            item.description = AnsiToWide(record.description);
            item.path = AnsiToWide(record.path);
            if (record.isApiPlugin) item.kind = L"API 指令集";
            else if (record.isGuiPlugin) item.kind = L"GUI 插件";
            else item.kind = L"原生插件";
            item.enabled = record.enabled;
            item.builtin = false;
            item.canToggle = true;
            item.canOpenFolder = !record.path.empty();
            item.canReload = !record.path.empty();
            items.push_back(item);
        }
    }

    // 2.5) 插件市场下载的 GUI 附属插件（Cache\gui\dll）
    if (g_guiDllHost) {
        auto guiNames = g_guiDllHost->InstalledNames();
        for (const auto& name : guiNames) {
            PluginCenterItem item;
            item.id = L"guidll." + name;
            item.name = name;
            item.version = L"1.0.0";
            item.author = L"插件商店";
            item.description = g_guiDllHost->Description(name);
            item.path = g_guiDllHost->DllPath(name);
            item.kind = L"GUI 插件";
            item.enabled = g_guiDllHost->IsEnabled(name);
            item.builtin = false;
            item.canToggle = true;
            item.canOpenFolder = true;
            item.canReload = true;
            items.push_back(item);
        }
    }

    // 3) JS 扩展（不含内置 AI）
    for (size_t i = 0; i < m_jsPlugins.size(); ++i) {
        if ((int)i == m_aiIndex || !m_jsPlugins[i]) continue;
        const auto& plug = m_jsPlugins[i];
        PluginCenterItem item;
        item.id = plug->GetId();
        item.name = plug->GetName();
        item.version = plug->GetVersion();
        item.author = L"JS 扩展";
        item.description = L"基于 QuickJS 的界面扩展";
        item.path = plug->GetFolderPath();
        item.kind = L"JS 扩展";
        item.enabled = !IsJSPluginDisabled(item.id);
        item.builtin = false;
        item.canToggle = true;
        item.canOpenFolder = !item.path.empty();
        item.canReload = true;
        items.push_back(item);
    }

    return items;
}

void YXGuiPlugin::RefreshPluginCenter() {
    if (m_hSettingsWnd && IsWindow(m_hSettingsWnd)) {
        m_pluginCenter.SetItems(BuildPluginCenterItems());
    }
}

void YXGuiPlugin::HandlePluginCenterToggle(const std::wstring& id,
    bool enabled) {
    if (id == L"builtin.light.ai") {
        m_aiEnabled = enabled;
        m_config.aiEnabled = enabled;
        if (!enabled && m_sidebarActive == -3) {
            m_sidebarActive = -2;
            OpenMainView();
        }
        SaveConfig();
        RefreshSidebar();
        RefreshPluginCenter();
        return;
    }

    // 插件市场下载的 GUI 附属插件（启停 = 重命名 .dll <-> .dll.N）
    if (id.rfind(L"guidll.", 0) == 0) {
        std::wstring name = id.substr(7);
        if (g_guiDllHost) {
            g_guiDllHost->SetEnabled(name, enabled);
            g_guiDllHost->Reload();
        }
        RefreshPluginCenter();
        return;
    }

    // 先查找 JS 扩展
    int jsIndex = FindPluginIndexById(id);
    if (jsIndex >= 0) {
        SetJSPluginDisabled(id, !enabled);
        RefreshPluginCenter();
        return;
    }

    // 原生插件由 PluginManager 统一维护启用状态
    if (g_PluginManager) {
        g_PluginManager->SetPluginEnabled(WideToUtf8(id), enabled);
    }
    RefreshPluginCenter();
}

void YXGuiPlugin::HandlePluginCenterAction(const std::wstring& id,
    const std::wstring& action) {
    // 插件市场下载的 GUI 附属插件
    if (id.rfind(L"guidll.", 0) == 0) {
        if (action == L"reload") {
            if (g_guiDllHost) g_guiDllHost->Reload();
            RefreshPluginCenter();
            return;
        }
        if (action == L"open") {
            std::wstring dir = g_guiDllHost ? g_guiDllHost->DllDir() : L"";
            if (!dir.empty())
                ShellExecuteW(m_hSettingsWnd ? m_hSettingsWnd : m_hWnd,
                    L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        return;
    }

    std::wstring path;

    int jsIndex = FindPluginIndexById(id);
    if (jsIndex >= 0) {
        path = m_jsPlugins[jsIndex]->GetFolderPath();
        if (action == L"reload") {
            ReloadJSPlugins();
            return;
        }
    }
    else if (g_PluginManager) {
        std::string utf8Path;
        if (g_PluginManager->GetPluginPath(WideToUtf8(id), utf8Path)) {
            path = AnsiToWide(utf8Path);
        }
        if (action == L"reload") {
            g_PluginManager->ReloadPlugin(WideToUtf8(id), false);
            RefreshPluginCenter();
            return;
        }
    }

    if (action == L"open" && !path.empty()) {
        std::wstring folder = path;
        if (GetFileAttributesW(folder.c_str()) != INVALID_FILE_ATTRIBUTES &&
            !(GetFileAttributesW(folder.c_str()) & FILE_ATTRIBUTE_DIRECTORY)) {
            size_t pos = folder.find_last_of(L"\\/");
            if (pos != std::wstring::npos) folder = folder.substr(0, pos);
        }
        ShellExecuteW(m_hSettingsWnd ? m_hSettingsWnd : m_hWnd,
            L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// ============================================================
// 打开扩展 Popup
// ============================================================
void YXGuiPlugin::OpenExtensionPopup(const std::wstring& extId,
    const std::wstring& popupPath) {
    if (extId.empty() || popupPath.empty()) return;

    if (m_hPopupWnd && IsWindow(m_hPopupWnd)) {
        DestroyWindow(m_hPopupWnd);
        m_hPopupWnd = nullptr;
    }

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = PopupWndProc;
        wc.hInstance = m_hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 35));
        wc.lpszClassName = L"YXGuiPopupWnd";
        RegisterClassExW(&wc);
        reg = true;
    }

    RECT rcBtn;
    GetWindowRect(m_hNavExtension, &rcBtn);
    int w = m_config.popupW;
    int h = m_config.popupH;
    int x = rcBtn.left - w + (rcBtn.right - rcBtn.left);
    int y = rcBtn.bottom + 4;
    if (x < 0) x = 0;
    if (y + h > GetSystemMetrics(SM_CYSCREEN))
        y = GetSystemMetrics(SM_CYSCREEN) - h - 10;

    m_hPopupWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"YXGuiPopupWnd", L"扩展",
        WS_POPUP | WS_BORDER,
        x, y, w, h,
        m_hWnd, NULL, m_hInstance, this);
    if (!m_hPopupWnd) return;

    HWND hHost = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY,
        0, 0, w, h, m_hPopupWnd, NULL, m_hInstance, NULL);

    std::string userDataFolder = GetCacheSubDir("WebView2_Plugin");
    std::wstring wUserData = AnsiToWide(userDataFolder);

    ICoreWebView2EnvironmentOptions* options = new SimpleEnvOptions();
    if (options) {
        ICoreWebView2EnvironmentOptions6* o6 = nullptr;
        if (SUCCEEDED(options->QueryInterface(
            IID_ICoreWebView2EnvironmentOptions6, (void**)&o6)) && o6) {
            o6->put_AreBrowserExtensionsEnabled(TRUE);
            o6->Release();
        }
    }

    std::wstring wExtId = extId;
    std::wstring wPopPath = popupPath;

    auto envCallback = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [this, wExtId, wPopPath, hHost, w, h]
        (HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env) return result;
            m_popupEnv = env;
            m_popupEnv->AddRef();

            env->CreateCoreWebView2Controller(hHost,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, wExtId, wPopPath, w, h]
                    (HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                        if (FAILED(result) || !controller) return result;
                        m_popupController = controller;
                        m_popupController->AddRef();
                        m_popupController->put_IsVisible(TRUE);

                        m_popupController->get_CoreWebView2(&m_popupWebView);
                        if (m_popupWebView) {
                            m_popupWebView->AddRef();

                            ICoreWebView2Controller2* c2 = nullptr;
                            if (SUCCEEDED(m_popupController->QueryInterface(
                                IID_ICoreWebView2Controller2, (void**)&c2)) && c2) {
                                COREWEBVIEW2_COLOR color = { 255, 255, 255, 255 };
                                c2->put_DefaultBackgroundColor(color);
                                c2->Release();
                            }

                            std::wstring url = L"chrome-extension://" +
                                wExtId + L"/" + wPopPath;
                            m_popupWebView->Navigate(url.c_str());
                        }

                        RECT rc = { 0, 0, w, h };
                        m_popupController->put_Bounds(rc);
                        m_popupController->put_IsVisible(TRUE);
                        m_popupReady = true;
                        return S_OK;
                    }).Get());
            return S_OK;
        });

    HRESULT hrCreate = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, wUserData.c_str(), options, envCallback.Get());
    if (hrCreate == E_INVALIDARG) {
        hrCreate = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, wUserData.c_str(), nullptr, envCallback.Get());
    }
    if (options) options->Release();

    ShowWindow(m_hPopupWnd, SW_SHOW);
    UpdateWindow(m_hPopupWnd);
}

LRESULT CALLBACK YXGuiPlugin::PopupWndProc(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pThis = (YXGuiPlugin*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        return 0;
    }
    case WM_SIZE: {
        if (pThis && pThis->m_popupController) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            pThis->m_popupController->put_Bounds(rc);
            pThis->m_popupController->put_IsVisible(TRUE);
        }
        HWND hChild = GetWindow(hWnd, GW_CHILD);
        if (hChild) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            MoveWindow(hChild, 0, 0, rc.right, rc.bottom, TRUE);
        }
        return 0;
    }
    case WM_KILLFOCUS:
        DestroyWindow(hWnd);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        if (pThis) {
            if (pThis->m_popupWebView) {
                pThis->m_popupWebView->Release();
                pThis->m_popupWebView = nullptr;
            }
            if (pThis->m_popupController) {
                pThis->m_popupController->Close();
                pThis->m_popupController->Release();
                pThis->m_popupController = nullptr;
            }
            if (pThis->m_popupEnv) {
                pThis->m_popupEnv->Release();
                pThis->m_popupEnv = nullptr;
            }
            pThis->m_popupReady = false;
            pThis->m_hPopupWnd = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// 打开扩展 Options
// ============================================================
void YXGuiPlugin::OpenExtensionOptions(const std::wstring& extId,
    const std::wstring& optionsPath) {
    if (extId.empty() || optionsPath.empty()) return;

    if (m_hOptionsWnd && IsWindow(m_hOptionsWnd)) {
        DestroyWindow(m_hOptionsWnd);
        m_hOptionsWnd = nullptr;
    }

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = DetailWndProc;
        wc.hInstance = m_hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"YXGuiDetailWnd";
        RegisterClassExW(&wc);
        reg = true;
    }

    RECT rcMain;
    GetWindowRect(m_hWnd, &rcMain);
    int w = m_config.optionsW;
    int h = m_config.optionsH;
    int x = rcMain.left + ((rcMain.right - rcMain.left) - w) / 2;
    int y = rcMain.top + ((rcMain.bottom - rcMain.top) - h) / 2;

    m_hOptionsWnd = CreateWindowExW(0, L"YXGuiDetailWnd", L"扩展设置",
        WS_OVERLAPPEDWINDOW,
        x, y, w, h,
        m_hWnd, NULL, m_hInstance, this);
    if (!m_hOptionsWnd) return;
    m_hDetailWnd = m_hOptionsWnd;

    HWND hHost = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY,
        0, 0, w, h, m_hOptionsWnd, NULL, m_hInstance, NULL);

    std::string userDataFolder = GetCacheSubDir("WebView2_Plugin");
    std::wstring wUserData = AnsiToWide(userDataFolder);

    ICoreWebView2EnvironmentOptions* options = new SimpleEnvOptions();
    if (options) {
        ICoreWebView2EnvironmentOptions6* o6 = nullptr;
        if (SUCCEEDED(options->QueryInterface(
            IID_ICoreWebView2EnvironmentOptions6, (void**)&o6)) && o6) {
            o6->put_AreBrowserExtensionsEnabled(TRUE);
            o6->Release();
        }
    }

    std::wstring wExtId = extId;
    std::wstring wOptPath = optionsPath;

    auto envCallback = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [this, wExtId, wOptPath, hHost, w, h]
        (HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env) return result;

            m_optionsEnv = env;
            m_optionsEnv->AddRef();

            env->CreateCoreWebView2Controller(hHost,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, wExtId, wOptPath, w, h]
                    (HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                        if (FAILED(result) || !controller) return result;
                        m_optionsController = controller;
                        m_optionsController->AddRef();
                        m_optionsController->put_IsVisible(TRUE);

                        m_optionsController->get_CoreWebView2(&m_optionsWebView);
                        if (m_optionsWebView) {
                            m_optionsWebView->AddRef();

                            ICoreWebView2Controller2* c2 = nullptr;
                            if (SUCCEEDED(m_optionsController->QueryInterface(
                                IID_ICoreWebView2Controller2, (void**)&c2)) && c2) {
                                COREWEBVIEW2_COLOR color = { 255, 255, 255, 255 };
                                c2->put_DefaultBackgroundColor(color);
                                c2->Release();
                            }

                            std::wstring url = L"chrome-extension://" +
                                wExtId + L"/" + wOptPath;
                            m_optionsWebView->Navigate(url.c_str());
                        }

                        RECT rc = { 0, 0, w, h };
                        m_optionsController->put_Bounds(rc);
                        m_optionsController->put_IsVisible(TRUE);
                        return S_OK;
                    }).Get());
            return S_OK;
        });

    HRESULT hrCreate = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, wUserData.c_str(), options, envCallback.Get());
    if (hrCreate == E_INVALIDARG) {
        hrCreate = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, wUserData.c_str(), nullptr, envCallback.Get());
    }
    if (options) options->Release();

    ShowWindow(m_hOptionsWnd, SW_SHOW);
    UpdateWindow(m_hOptionsWnd);
}

LRESULT CALLBACK YXGuiPlugin::DetailWndProc(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pThis = (YXGuiPlugin*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        return 0;
    }
    case WM_SIZE: {
        if (pThis && pThis->m_optionsController) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            pThis->m_optionsController->put_Bounds(rc);
            pThis->m_optionsController->put_IsVisible(TRUE);
        }
        HWND hChild = GetWindow(hWnd, GW_CHILD);
        if (hChild) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            MoveWindow(hChild, 0, 0, rc.right, rc.bottom, TRUE);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        if (pThis) {
            if (pThis->m_optionsWebView) {
                pThis->m_optionsWebView->Release();
                pThis->m_optionsWebView = nullptr;
            }
            if (pThis->m_optionsController) {
                pThis->m_optionsController->Close();
                pThis->m_optionsController->Release();
                pThis->m_optionsController = nullptr;
            }
            if (pThis->m_optionsEnv) {
                pThis->m_optionsEnv->Release();
                pThis->m_optionsEnv = nullptr;
            }
            pThis->m_hOptionsWnd = nullptr;
            pThis->m_hDetailWnd = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// 在 UI 线程把扩展注册到 WebView2 Profile
// ============================================================
void YXGuiPlugin::AddExtensionToProfileOnUIThread(const std::wstring& extensionPath) {
    if (!m_webViewEnv || extensionPath.empty()) return;

    ICoreWebView2* wv = GetActiveWebView();
    if (!wv) return;

    ICoreWebView2_13* wv13 = nullptr;
    if (FAILED(wv->QueryInterface(IID_ICoreWebView2_13, (void**)&wv13)) || !wv13) return;
    ICoreWebView2Profile* profile = nullptr;
    wv13->get_Profile(&profile);
    wv13->Release();
    if (!profile) return;

    ICoreWebView2Profile7* p7 = nullptr;
    HRESULT hr = profile->QueryInterface(IID_ICoreWebView2Profile7, (void**)&p7);
    profile->Release();
    if (FAILED(hr) || !p7) return;

    std::wstring pathCopy = extensionPath;
    p7->AddBrowserExtension(pathCopy.c_str(),
        Callback<ICoreWebView2ProfileAddBrowserExtensionCompletedHandler>(
            [this, pathCopy](HRESULT result,
                ICoreWebView2BrowserExtension* extension) -> HRESULT {
                    if (SUCCEEDED(result) && extension) {
                        extension->AddRef();

                        LPWSTR id = nullptr;
                        extension->get_Id(&id);
                        BOOL enabled = FALSE;
                        extension->get_IsEnabled(&enabled);

                        {
                            std::lock_guard<std::mutex> lock(m_extInstallMutex);
                            bool found = false;
                            for (auto& ext : m_extensions) {
                                if (ext.path == pathCopy) {
                                    if (id) ext.id = id;
                                    ext.enabled = (enabled != FALSE);
                                    if (ext.comExt) ext.comExt->Release();
                                    ext.comExt = extension;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                ExtInfo info;
                                info.path = pathCopy;
                                if (id) info.id = id;
                                info.enabled = (enabled != FALSE);
                                info.comExt = extension;
                                ParseExtensionManifest(info);
                                if (info.name.empty()) {
                                    size_t p = pathCopy.find_last_of(L"\\/");
                                    info.name = (p != std::wstring::npos)
                                        ? pathCopy.substr(p + 1) : pathCopy;
                                }
                                m_extensions.push_back(info);
                            }
                        }
                        if (id) CoTaskMemFree(id);
                    }
                    return S_OK;
            }).Get());

    p7->Release();
}

// ============================================================
// CRX 安装 / Watcher
// ============================================================
std::wstring YXGuiPlugin::InstallExtensionFromCrx(const std::wstring& crxPath) {
    if (GetFileAttributesW(crxPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return L"";

    std::wstring fileName = crxPath;
    size_t p = fileName.find_last_of(L"\\/");
    if (p != std::wstring::npos) fileName = fileName.substr(p + 1);
    size_t dot = fileName.find_last_of(L'.');
    if (dot != std::wstring::npos) fileName = fileName.substr(0, dot);
    if (fileName.empty()) fileName = L"extension";

    std::string crxDir = GetCacheSubDir("crx");
    std::wstring wCrxDir = AnsiToWide(crxDir);
    std::wstring zipPath = wCrxDir + L"\\" + fileName + L".zip";

    if (!CrxStripper_WriteZip(crxPath, zipPath)) {
        DeleteFileW(zipPath.c_str());
        return L"";
    }

    std::string extRoot = GetCacheSubDir("Extensions");
    std::wstring wExtRoot = AnsiToWide(extRoot);
    std::wstring destDir = wExtRoot + L"\\" + fileName;

    if (m_commandHandler) {
        std::string cmd = "unzip \"";
        cmd += WideToUtf8(zipPath);
        cmd += "\" \"";
        cmd += WideToUtf8(destDir);
        cmd += "\"";
        m_commandHandler(cmd);
    }
    else {
        DeleteFileW(zipPath.c_str());
        return L"";
    }

    if (!WaitForManifest(destDir, 30000)) {
        DeleteFileW(zipPath.c_str());
        return L"";
    }

    DeleteFileW(zipPath.c_str());
    return destDir;
}

void YXGuiPlugin::StartCrxWatcher() {
    if (m_crxWatcherRunning) return;
    m_crxWatcherRunning = true;

    m_crxWatcherThread = std::thread([this]() {
        std::vector<std::wstring> watchDirs;

        {
            std::string a = GetCacheSubDir("crx");
            std::wstring w = AnsiToWide(a);
            SHCreateDirectoryExW(NULL, w.c_str(), NULL);
            watchDirs.push_back(w);
        }
        {
            PWSTR p = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(
                FOLDERID_Downloads, 0, NULL, &p)) && p) {
                watchDirs.push_back(p);
                CoTaskMemFree(p);
            }
        }
        {
            wchar_t up[MAX_PATH] = { 0 };
            if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) {
                std::wstring fb = std::wstring(up) + L"\\Downloads";
                if (GetFileAttributesW(fb.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    bool dup = false;
                    for (auto& d : watchDirs)
                        if (_wcsicmp(d.c_str(), fb.c_str()) == 0) { dup = true; break; }
                    if (!dup) watchDirs.push_back(fb);
                }
            }
        }

        while (m_crxWatcherRunning) {
            for (auto& wDir : watchDirs) {
                if (!m_crxWatcherRunning) break;

                std::wstring pattern = wDir + L"\\*.crx";
                WIN32_FIND_DATAW fd;
                HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
                if (hFind == INVALID_HANDLE_VALUE) continue;

                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

                    std::wstring fname = fd.cFileName;
                    std::wstring full = wDir + L"\\" + fname;

                    WIN32_FILE_ATTRIBUTE_DATA fad;
                    if (!GetFileAttributesExW(full.c_str(),
                        GetFileExInfoStandard, &fad)) {
                        continue;
                    }

                    LONGLONG size = ((LONGLONG)fad.nFileSizeHigh << 32)
                        | fad.nFileSizeLow;
                    if (size <= 0) continue;

                    bool stable = false;
                    {
                        std::lock_guard<std::mutex> lock(m_crxMutex);
                        auto& probe = m_crxProbes[fname];

                        bool sizeSame = (probe.size == size);
                        bool timeSame =
                            (probe.mtime.dwLowDateTime ==
                                fad.ftLastWriteTime.dwLowDateTime) &&
                            (probe.mtime.dwHighDateTime ==
                                fad.ftLastWriteTime.dwHighDateTime);

                        if (sizeSame && timeSame) {
                            probe.stableCount++;
                        }
                        else {
                            probe.size = size;
                            probe.mtime = fad.ftLastWriteTime;
                            probe.stableCount = 0;
                        }

                        if (probe.stableCount >= 2) stable = true;
                    }

                    if (!stable) continue;

                    std::wstring* pPath = new std::wstring(full);
                    if (m_hWnd && IsWindow(m_hWnd))
                        PostMessageW(m_hWnd, WM_YXGUI_CRX_DOWNLOADED, 0,
                            (LPARAM)pPath);
                    else
                        delete pPath;

                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }

            for (int i = 0; i < 20 && m_crxWatcherRunning; i++)
                Sleep(100);
        }
        });
}

void YXGuiPlugin::StopCrxWatcher() {
    m_crxWatcherRunning = false;
    if (m_crxWatcherThread.joinable()) {
        m_crxWatcherThread.join();
    }
}

void YXGuiPlugin::HandleDownloadedCrx(const std::wstring& crxPath) {
    std::wstring crx = crxPath;

    std::wstring fname = crx;
    {
        size_t p = fname.find_last_of(L"\\/");
        if (p != std::wstring::npos) fname = fname.substr(p + 1);
    }

    {
        std::lock_guard<std::mutex> lock(m_crxMutex);
        if (m_crxSeenFiles.count(fname) > 0) return;
        m_crxSeenFiles.insert(fname);
    }

    std::thread([this, crx, fname]() {
        std::string crxDirA = GetCacheSubDir("crx");
        std::wstring wCrxDir = AnsiToWide(crxDirA);
        SHCreateDirectoryExW(NULL, wCrxDir.c_str(), NULL);

        std::wstring localCopy = crx;
        if (crx.find(wCrxDir) == std::wstring::npos) {
            std::wstring target = wCrxDir + L"\\" + fname;
            if (MoveFileW(crx.c_str(), target.c_str())) {
                localCopy = target;
            }
        }

        std::wstring dest = InstallExtensionFromCrx(localCopy);

        if (dest.empty()) {
            {
                std::lock_guard<std::mutex> lock(m_crxMutex);
                m_crxSeenFiles.erase(fname);
                auto& probe = m_crxProbes[fname];
                probe.retryCount++;
                probe.stableCount = 0;
            }
            DeleteFileW(localCopy.c_str());
            return;
        }

        DeleteFileW(localCopy.c_str());
        if (localCopy != crx) DeleteFileW(crx.c_str());

        std::wstring* pDest = new std::wstring(dest);
        if (m_hWnd && IsWindow(m_hWnd)) {
            if (!PostMessageW(m_hWnd, WM_YXGUI_ADD_EXTENSION_TO_PROFILE, 0,
                (LPARAM)pDest)) {
                delete pDest;
            }
        }
        else {
            delete pDest;
        }
        }).detach();
}

// ============================================================
// 收藏夹
// ============================================================
static std::string GetFavFilePath() {
    return GetCacheSubDir("") + "\\favorites.txt";
}

void YXGuiPlugin::LoadFavorites() {
    m_favorites.clear();
    std::ifstream f(GetFavFilePath());
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        m_favorites.push_back(AnsiToWide(line));
    }
}

void YXGuiPlugin::SaveFavorites() {
    std::ofstream f(GetFavFilePath(), std::ios::trunc);
    if (!f.is_open()) return;
    for (const auto& w : m_favorites) f << WideToUtf8(w) << std::endl;
}

void YXGuiPlugin::ToggleFavorite() {
    wchar_t url[2048] = { 0 };
    if (m_hAddressBar) GetWindowTextW(m_hAddressBar, url, 2048);
    if (wcslen(url) == 0) return;
    std::wstring wurl = url;
    auto it = std::find(m_favorites.begin(), m_favorites.end(), wurl);
    if (it != m_favorites.end()) {
        m_favorites.erase(it);
        if (m_hNavStar) SetWindowTextW(m_hNavStar, L"☆");
        MessageBoxW(m_hWnd, L"已从收藏夹移除", L"提示", MB_OK | MB_ICONINFORMATION);
    }
    else {
        m_favorites.push_back(wurl);
        if (m_hNavStar) SetWindowTextW(m_hNavStar, L"★");
        MessageBoxW(m_hWnd, L"已加入收藏", L"提示", MB_OK | MB_ICONINFORMATION);
    }
    SaveFavorites();
}

void YXGuiPlugin::ShowFavoritesMenu() {
    LoadFavorites();
    HMENU hMenu = CreatePopupMenu();
    if (m_favorites.empty()) {
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"（收藏夹为空）");
    }
    else {
        for (size_t i = 0; i < m_favorites.size(); i++) {
            std::wstring disp = m_favorites[i];
            if (disp.length() > 80) disp = disp.substr(0, 77) + L"...";
            AppendMenuW(hMenu, MF_STRING, 4000 + (UINT)i, disp.c_str());
        }
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, 3999, L"清空收藏夹");
    }
    RECT rc;
    GetWindowRect(m_hFavBtn ? m_hFavBtn : m_hWnd, &rc);
    int cmd = TrackPopupMenu(hMenu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        rc.left, rc.bottom, 0, m_hWnd, NULL);
    DestroyMenu(hMenu);
    if (cmd == 3999) { m_favorites.clear(); SaveFavorites(); return; }
    if (cmd >= 4000) {
        size_t idx = cmd - 4000;
        if (idx < m_favorites.size()) NavigateTo(m_favorites[idx]);
    }
}

// ============================================================
// 控制台命令
// ============================================================
void YXGuiPlugin::SubmitConsoleCommand() {
    if (!m_hConsoleInput) return;
    wchar_t buf[4096] = { 0 };
    GetWindowTextW(m_hConsoleInput, buf, 4096);
    if (wcslen(buf) == 0) return;

    int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return;
    std::string cmd(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, &cmd[0], len, NULL, NULL);

    {
        std::string line = "light> " + cmd + "\r\n";
        std::wstring wline = AnsiToWide(line);
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = RGB(80, 220, 255);
        int textLen = GetWindowTextLengthW(m_hConsoleEdit);
        SendMessageW(m_hConsoleEdit, EM_SETSEL, textLen, textLen);
        SendMessageW(m_hConsoleEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        SendMessageW(m_hConsoleEdit, EM_REPLACESEL, FALSE, (LPARAM)wline.c_str());
        SendMessageW(m_hConsoleEdit, EM_SCROLLCARET, 0, 0);
    }
    SetWindowTextW(m_hConsoleInput, L"");
    if (m_commandHandler) m_commandHandler(cmd);
}

// ============================================================
// 导航
// ============================================================
void YXGuiPlugin::NavigateTo(const std::wstring& url) {
    if (url.empty()) return;
    std::wstring full = url;
    if (full.find(L"://") == std::wstring::npos) full = L"https://" + full;
    if (m_hAddressBar) SetWindowTextW(m_hAddressBar, full.c_str());
    ICoreWebView2* wv = GetActiveWebView();
    if (wv) wv->Navigate(full.c_str());
    UpdateNavButtons();
}

void YXGuiPlugin::NavigateToAddressBarText() {
    if (!m_hAddressBar) return;
    wchar_t buf[2048] = { 0 };
    GetWindowTextW(m_hAddressBar, buf, 2048);
    NavigateTo(buf);
}

// ============================================================
// 子类过程
// ============================================================
static LRESULT CALLBACK ConsoleInputSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
    LPARAM lParam, UINT_PTR, DWORD_PTR dwRefData) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)dwRefData;
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) { pThis->SubmitConsoleCommand(); return 0; }
        if (wParam == VK_ESCAPE) { SetWindowTextW(hWnd, L""); return 0; }
        break;
    case WM_CHAR:
        if (wParam == VK_RETURN || wParam == VK_ESCAPE) return 0;
        break;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK AddressBarSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
    LPARAM lParam, UINT_PTR, DWORD_PTR dwRefData) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)dwRefData;
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) { pThis->NavigateToAddressBarText(); return 0; }
        break;
    case WM_CHAR:
        if (wParam == VK_RETURN) return 0;
        break;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK ToolbarButtonSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
    LPARAM lParam, UINT_PTR, DWORD_PTR dwRefData) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)dwRefData;
    int id = GetDlgCtrlID(hWnd);

    switch (msg) {
    case WM_MOUSEMOVE: {
        if (pThis->m_toolbarHoveredId != id) {
            int old = pThis->m_toolbarHoveredId;
            pThis->m_toolbarHoveredId = id;
            if (old) {
                HWND oldH = GetDlgItem(pThis->m_hWnd, old);
                if (oldH) InvalidateRect(oldH, NULL, TRUE);
            }
            InvalidateRect(hWnd, NULL, TRUE);
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE: {
        if (pThis->m_toolbarHoveredId == id) {
            pThis->m_toolbarHoveredId = 0;
            InvalidateRect(hWnd, NULL, TRUE);
        }
        return 0;
    }
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK AiButtonSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
    LPARAM lParam, UINT_PTR, DWORD_PTR dwRefData) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)dwRefData;
    int idx = GetDlgCtrlID(hWnd) - IDC_AI_BASE;
    if (idx < 0 || idx >= (int)pThis->m_aiHovered.size())
        return DefSubclassProc(hWnd, msg, wParam, lParam);

    switch (msg) {
    case WM_MOUSEMOVE:
        if (!pThis->m_aiHovered[idx]) {
            pThis->m_aiHovered[idx] = true;
            InvalidateRect(hWnd, NULL, TRUE);
        }
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    case WM_MOUSELEAVE:
        pThis->m_aiHovered[idx] = false;
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ============================================================
// Tooltip
// ============================================================
void YXGuiPlugin::AddTooltip(HWND hWnd, const std::wstring& text) {
    if (!m_hTooltip || !hWnd) return;
    TOOLINFOW ti = { 0 };
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = m_hWnd;
    ti.uId = (UINT_PTR)hWnd;
    ti.lpszText = (LPWSTR)text.c_str();
    SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// ============================================================
// 工具栏映射
// ============================================================
HWND YXGuiPlugin::GetToolbarHwndById(const std::wstring& id) {
    if (id == L"back")      return m_hNavBack;
    if (id == L"forward")   return m_hNavForward;
    if (id == L"refresh")   return m_hNavRefresh;
    if (id == L"home")      return m_hNavHome;
    if (id == L"star")      return m_hNavStar;
    if (id == L"fav")       return m_hFavBtn;
    if (id == L"zoomin")    return m_hNavZoomIn;
    if (id == L"zoomout")   return m_hNavZoomOut;
    if (id == L"download")  return m_hNavDownload;
    if (id == L"extension") return m_hNavExtension;
    if (id == L"settings")  return nullptr;
    return nullptr;
}

int YXGuiPlugin::ToolbarIdFromString(const std::wstring& s) {
    if (s == L"back")      return IDC_NAV_BACK;
    if (s == L"forward")   return IDC_NAV_FORWARD;
    if (s == L"refresh")   return IDC_NAV_REFRESH;
    if (s == L"home")      return IDC_NAV_HOME;
    if (s == L"star")      return IDC_NAV_STAR;
    if (s == L"fav")       return IDC_NAV_FAV;
    if (s == L"zoomin")    return IDC_NAV_ZOOM_IN;
    if (s == L"zoomout")   return IDC_NAV_ZOOM_OUT;
    if (s == L"download")  return IDC_NAV_DOWNLOAD;
    if (s == L"extension") return IDC_NAV_EXTENSION;
    return 0;
}

std::wstring YXGuiPlugin::ToolbarStringFromId(int id) {
    switch (id) {
    case IDC_NAV_BACK:      return L"back";
    case IDC_NAV_FORWARD:   return L"forward";
    case IDC_NAV_REFRESH:   return L"refresh";
    case IDC_NAV_HOME:      return L"home";
    case IDC_NAV_STAR:      return L"star";
    case IDC_NAV_FAV:       return L"fav";
    case IDC_NAV_ZOOM_IN:   return L"zoomin";
    case IDC_NAV_ZOOM_OUT:  return L"zoomout";
    case IDC_NAV_DOWNLOAD:  return L"download";
    case IDC_NAV_EXTENSION: return L"extension";
    }
    return L"";
}

std::wstring YXGuiPlugin::ToolbarDisplayName(const std::wstring& id) {
    if (id == L"back")      return L"后退";
    if (id == L"forward")   return L"前进";
    if (id == L"refresh")   return L"刷新";
    if (id == L"home")      return L"主页";
    if (id == L"star")      return L"收藏/取消收藏";
    if (id == L"fav")       return L"收藏夹";
    if (id == L"zoomin")    return L"放大";
    if (id == L"zoomout")   return L"缩小";
    if (id == L"download")  return L"下载管理";
    if (id == L"extension") return L"扩展管理";
    return id;
}

void YXGuiPlugin::ApplyToolbarLayout() {
    if (!m_hWnd) return;
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    OnSize(rc.right, rc.bottom);
}

// ============================================================
// AI 编辑子对话框
// ============================================================
#define AI_EDIT_CLASS  L"YXGuiAiEditWnd"

LRESULT CALLBACK YXGuiPlugin::AiEditWndProc(HWND hWnd, UINT msg, WPARAM wParam,
    LPARAM lParam) {
    struct Param { AiButtonConfig* cfg; bool* ok; };
    Param* p = (Param*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        p = (Param*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)p);
        if (!p) return -1;

        CreateWindowExW(0, L"STATIC", L"名称：",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, 12, 70, 22, hWnd, NULL, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            p->cfg->name.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            88, 10, 320, 26, hWnd, (HMENU)IDC_AI_EDIT_NAME, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"网址：",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, 48, 70, 22, hWnd, NULL, NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            p->cfg->url.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            88, 46, 320, 26, hWnd, (HMENU)IDC_AI_EDIT_URL, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            256, 88, 72, 28, hWnd, (HMENU)IDC_AI_EDIT_OK, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            336, 88, 72, 28, hWnd, (HMENU)IDC_AI_EDIT_CANCEL, NULL, NULL);

        SetFocus(GetDlgItem(hWnd, IDC_AI_EDIT_NAME));
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_AI_EDIT_OK) {
            if (p && p->cfg) {
                wchar_t buf[2048];
                GetDlgItemTextW(hWnd, IDC_AI_EDIT_NAME, buf, 2048);
                p->cfg->name = buf;
                GetDlgItemTextW(hWnd, IDC_AI_EDIT_URL, buf, 2048);
                p->cfg->url = buf;
            }
            if (p && p->ok) *p->ok = true;
            DestroyWindow(hWnd);
        }
        else if (id == IDC_AI_EDIT_CANCEL) {
            DestroyWindow(hWnd);
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool YXGuiPlugin::ShowAiEditDialog(AiButtonConfig& cfg,
    const std::wstring& title, bool isNew) {
    (void)isNew;
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = AiEditWndProc;
        wc.hInstance = m_hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = AI_EDIT_CLASS;
        RegisterClassExW(&wc);
        reg = true;
    }

    bool ok = false;
    struct Param { AiButtonConfig* cfg; bool* ok; };
    Param param;
    param.cfg = &cfg;
    param.ok = &ok;

    RECT rcParent = { 0 };
    if (m_hSettingsWnd) GetWindowRect(m_hSettingsWnd, &rcParent);
    else if (m_hWnd)    GetWindowRect(m_hWnd, &rcParent);

    int w = 440, h = 170;
    int x = rcParent.left + ((rcParent.right - rcParent.left) - w) / 2;
    int y = rcParent.top + ((rcParent.bottom - rcParent.top) - h) / 2;

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        AI_EDIT_CLASS, title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h,
        m_hSettingsWnd ? m_hSettingsWnd : m_hWnd,
        NULL, m_hInstance, &param);
    if (!hDlg) return false;

    ShowWindow(hDlg, SW_SHOW);
    SetForegroundWindow(hDlg);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return ok;
}

// ============================================================
// 设置对话框
// ============================================================
#define SETTINGS_CLASS L"YXGuiSettingsWnd"

void YXGuiPlugin::DrawSettingsBackground(HDC hdc, const RECT& rc) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    const int railW = 216;
    Gdiplus::SolidBrush contentBg(Gdiplus::Color(255, 244, 247, 251));
    g.FillRectangle(&contentBg, Gdiplus::RectF(0, 0,
        (Gdiplus::REAL)rc.right, (Gdiplus::REAL)rc.bottom));

    // 左侧深色导航栏
    Gdiplus::LinearGradientBrush rail(
        Gdiplus::Point(0, 0), Gdiplus::Point(0, rc.bottom),
        Gdiplus::Color(255, 30, 41, 59),
        Gdiplus::Color(255, 15, 23, 42));
    g.FillRectangle(&rail, Gdiplus::RectF(0, 0,
        (Gdiplus::REAL)railW, (Gdiplus::REAL)rc.bottom));

    Gdiplus::Font brandFont(L"Microsoft YaHei", 22,
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush dim(Gdiplus::Color(255, 148, 163, 184));
    Gdiplus::StringFormat left;
    left.SetAlignment(Gdiplus::StringAlignmentNear);
    left.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"设置", -1, &brandFont,
        Gdiplus::RectF(22, 26, 170, 38), &left, &white);

    Gdiplus::Font subFont(L"Microsoft YaHei", 11,
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    g.DrawString(L"Light Preferences", -1, &subFont,
        Gdiplus::RectF(24, 62, 170, 20), &left, &dim);

    Gdiplus::Pen railLine(Gdiplus::Color(255, 51, 65, 85), 1.0f);
    g.DrawLine(&railLine, (Gdiplus::REAL)railW, 0.0f,
        (Gdiplus::REAL)railW, (Gdiplus::REAL)rc.bottom);

    Gdiplus::Font verFont(L"Microsoft YaHei", 10,
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    g.DrawString(L"Light Security Console", -1, &verFont,
        Gdiplus::RectF(22, (Gdiplus::REAL)(rc.bottom - 34), 180, 20),
        &left, &dim);

    // 内容页标题
    static const wchar_t* titles[] = {
        L"AI 快捷按钮", L"工具栏", L"窗口尺寸", L"插件中心", L"插件商店"
    };
    static const wchar_t* subtitles[] = {
        L"管理主界面右侧的 AI 快捷入口，可增删、排序与编辑。",
        L"调整工具栏按钮的顺序与可见性。",
        L"设置主窗口与各功能面板的默认尺寸。",
        L"统一管理内置功能、api_ 指令集、GUI 与 JS 扩展。",
        L"浏览并一键安装 GUI 附属 DLL 插件（不含任何指令）。"
    };
    int tab = m_settingsCurTab;
    if (tab < 0 || tab > 4) tab = 0;

    Gdiplus::Font pageFont(L"Microsoft YaHei", 20,
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush ink(Gdiplus::Color(255, 30, 41, 59));
    Gdiplus::SolidBrush muted(Gdiplus::Color(255, 100, 116, 139));
    g.DrawString(titles[tab], -1, &pageFont,
        Gdiplus::RectF((Gdiplus::REAL)(railW + 22), 24, (Gdiplus::REAL)(rc.right - railW - 40), 32),
        &left, &ink);

    Gdiplus::Font pageSub(L"Microsoft YaHei", 12,
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    g.DrawString(subtitles[tab], -1, &pageSub,
        Gdiplus::RectF((Gdiplus::REAL)(railW + 22), 58, (Gdiplus::REAL)(rc.right - railW - 40), 22),
        &left, &muted);

    // 底部操作区
    Gdiplus::SolidBrush footer(Gdiplus::Color(255, 255, 255, 255));
    g.FillRectangle(&footer, Gdiplus::RectF((Gdiplus::REAL)railW,
        (Gdiplus::REAL)(rc.bottom - 76),
        (Gdiplus::REAL)(rc.right - railW), 76.0f));
    Gdiplus::Pen footerLine(Gdiplus::Color(255, 226, 232, 240), 1.0f);
    g.DrawLine(&footerLine, (Gdiplus::REAL)railW,
        (Gdiplus::REAL)(rc.bottom - 76), (Gdiplus::REAL)rc.right,
        (Gdiplus::REAL)(rc.bottom - 76));
}

void YXGuiPlugin::DrawSettingsButton(LPDRAWITEMSTRUCT lpdis) {
    if (!lpdis) return;
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem;
    int id = (int)lpdis->CtlID;
    bool pressed = (lpdis->itemState & ODS_SELECTED) != 0;
    bool disabled = (lpdis->itemState & ODS_DISABLED) != 0;

    wchar_t text[256] = { 0 };
    GetWindowTextW(lpdis->hwndItem, text, 256);

    bool primary = (id == IDC_SET_OK || id == IDC_SET_APPLY);
    bool ghost = (id == IDC_SET_RESET || id == IDC_SET_CANCEL);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::RectF r((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
        (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));

    Gdiplus::Color bg;
    Gdiplus::Color border;
    Gdiplus::Color fg;
    if (primary) {
        bg = pressed ? Gdiplus::Color(255, 29, 78, 216)
            : Gdiplus::Color(255, 59, 130, 246);
        border = bg;
        fg = Gdiplus::Color(255, 255, 255, 255);
    }
    else if (ghost) {
        bg = pressed ? Gdiplus::Color(255, 241, 245, 249)
            : Gdiplus::Color(255, 255, 255, 255);
        border = Gdiplus::Color(255, 214, 224, 238);
        fg = Gdiplus::Color(255, 71, 85, 105);
    }
    else {
        bg = pressed ? Gdiplus::Color(255, 219, 234, 254)
            : Gdiplus::Color(255, 255, 255, 255);
        border = Gdiplus::Color(255, 198, 214, 235);
        fg = Gdiplus::Color(255, 37, 99, 235);
    }
    if (disabled) {
        bg = Gdiplus::Color(255, 241, 245, 249);
        border = Gdiplus::Color(255, 226, 232, 240);
        fg = Gdiplus::Color(255, 148, 163, 184);
    }

    Gdiplus::GraphicsPath path;
    float radius = 8.0f;
    float d = radius * 2.0f;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush buttonBrush(bg);
    g.FillPath(&buttonBrush, &path);
    Gdiplus::Pen pen(border, 1.0f);
    g.DrawPath(&pen, &path);

    Gdiplus::Font font(L"Microsoft YaHei", 12,
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(fg);
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    g.DrawString(text, -1, &font, r, &fmt, &brush);
}

void YXGuiPlugin::DrawSettingsNav(LPDRAWITEMSTRUCT lpdis) {
    if (!lpdis) return;
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem;
    int index = (int)lpdis->itemID;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::SolidBrush rail(Gdiplus::Color(255, 30, 41, 59));
    g.FillRectangle(&rail, Gdiplus::Rect(rc.left, rc.top,
        rc.right - rc.left, rc.bottom - rc.top));

    if (index < 0 || index > 4) return;

    bool selected = (lpdis->itemState & ODS_SELECTED) != 0;
    Gdiplus::RectF r((Gdiplus::REAL)(rc.left + 4), (Gdiplus::REAL)(rc.top + 4),
        (Gdiplus::REAL)(rc.right - rc.left - 8), (Gdiplus::REAL)(rc.bottom - rc.top - 8));

    if (selected) {
        Gdiplus::GraphicsPath path;
        float radius = 9.0f, d = radius * 2.0f;
        path.AddArc(r.X, r.Y, d, d, 180, 90);
        path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
        path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
        path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
        path.CloseFigure();
        Gdiplus::SolidBrush bg(Gdiplus::Color(255, 59, 130, 246));
        g.FillPath(&bg, &path);
    }

    static const wchar_t* icons[] = { L"🎨", L"🧰", L"🪟", L"🧩", L"🛒" };
    static const wchar_t* labels[] = {
        L"AI 快捷按钮", L"工具栏", L"窗口尺寸", L"插件中心", L"插件商店"
    };

    Gdiplus::Font iconFont(L"Segoe UI Emoji", 14,
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font labelFont(L"Microsoft YaHei", 13,
        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush iconBrush(selected
        ? Gdiplus::Color(255, 255, 255, 255)
        : Gdiplus::Color(255, 148, 163, 184));
    Gdiplus::SolidBrush textBrush(selected
        ? Gdiplus::Color(255, 255, 255, 255)
        : Gdiplus::Color(255, 203, 213, 225));
    Gdiplus::StringFormat centerLeft;
    centerLeft.SetAlignment(Gdiplus::StringAlignmentNear);
    centerLeft.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    centerLeft.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    centerLeft.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

    g.DrawString(icons[index], -1, &iconFont,
        Gdiplus::RectF(r.X + 14, r.Y, 26, r.Height), &centerLeft, &iconBrush);
    g.DrawString(labels[index], -1, &labelFont,
        Gdiplus::RectF(r.X + 46, r.Y, r.Width - 54, r.Height),
        &centerLeft, &textBrush);
}

void YXGuiPlugin::LayoutSettingsControls() {
    if (!m_hSettingsWnd || !IsWindow(m_hSettingsWnd)) return;

    RECT rc;
    GetClientRect(m_hSettingsWnd, &rc);

    if (m_hSetTab && IsWindow(m_hSetTab)) {
        int navH = rc.bottom - 194;
        if (navH < 120) navH = 120;
        MoveWindow(m_hSetTab, 14, 104, 188, navH, TRUE);
    }

    const int contentX = 232;
    int contentW = rc.right - 256;
    if (contentW < 300) contentW = 300;
    const int footerY = rc.bottom - 62;
    const int btnH = 36;
    const int btnW = 96;
    const int gap = 10;

    int right = rc.right - 24;
    int ids[] = { IDC_SET_OK, IDC_SET_APPLY, IDC_SET_CANCEL };
    for (int id : ids) {
        HWND b = GetDlgItem(m_hSettingsWnd, id);
        if (b && IsWindow(b)) {
            MoveWindow(b, right - btnW, footerY, btnW, btnH, TRUE);
        }
        right -= (btnW + gap);
    }
    HWND resetBtn = GetDlgItem(m_hSettingsWnd, IDC_SET_RESET);
    if (resetBtn) MoveWindow(resetBtn, contentX, footerY, 108, btnH, TRUE);

    const int cy = 96;
    const int ch = std::max(180, footerY - cy - 18);

    if (m_hSetAiList && IsWindow(m_hSetAiList)) {
        MoveWindow(m_hSetAiList, contentX, cy, contentW, ch - 54, TRUE);
    }
    {
        int bx = contentX;
        int by = cy + ch - 42;
        int bw = 92, bh = 34, gap2 = 8;
        int aiIds[] = { IDC_SET_AI_ADD, IDC_SET_AI_EDIT, IDC_SET_AI_DEL,
                        IDC_SET_AI_UP, IDC_SET_AI_DOWN };
        for (int id : aiIds) {
            HWND b = GetDlgItem(m_hSettingsWnd, id);
            if (b && IsWindow(b)) MoveWindow(b, bx, by, bw, bh, TRUE);
            bx += bw + gap2;
        }
    }

    if (m_hSetTbList && IsWindow(m_hSetTbList)) {
        MoveWindow(m_hSetTbList, contentX, cy, contentW, ch - 54, TRUE);
    }
    {
        int bx = contentX;
        int by = cy + ch - 42;
        int bw = 100, bh = 34, gap2 = 8;
        int tbIds[] = { IDC_SET_TB_UP, IDC_SET_TB_DOWN, IDC_SET_TB_TOGGLE };
        for (int id : tbIds) {
            HWND b = GetDlgItem(m_hSettingsWnd, id);
            if (b && IsWindow(b)) MoveWindow(b, bx, by, bw, bh, TRUE);
            bx += bw + gap2;
        }
    }

    {
        auto MoveLabelEdit = [&](int labelId, int editId, int row) {
            int ly = cy + 6 + row * 42;
            HWND lbl = GetDlgItem(m_hSettingsWnd, labelId);
            HWND edt = GetDlgItem(m_hSettingsWnd, editId);
            if (lbl && IsWindow(lbl)) {
                MoveWindow(lbl, contentX + 12, ly + 8, 260, 24, TRUE);
            }
            if (edt && IsWindow(edt)) {
                MoveWindow(edt, contentX + 300, ly, 160, 34, TRUE);
            }
            };
        MoveLabelEdit(4200, IDC_SET_WIN_MAIN_W, 0);
        MoveLabelEdit(4201, IDC_SET_WIN_MAIN_H, 1);
        MoveLabelEdit(4202, IDC_SET_WIN_BROWSER_H, 2);
        MoveLabelEdit(4203, IDC_SET_WIN_EXT_W, 3);
        MoveLabelEdit(4204, IDC_SET_WIN_EXT_H, 4);
        MoveLabelEdit(4205, IDC_SET_WIN_POPUP_W, 5);
        MoveLabelEdit(4206, IDC_SET_WIN_POPUP_H, 6);
        MoveLabelEdit(4207, IDC_SET_WIN_OPT_W, 7);
        MoveLabelEdit(4208, IDC_SET_WIN_OPT_H, 8);
    }

    if (m_pluginCenter.Hwnd()) {
        m_pluginCenter.Move(contentX, cy, contentW, ch - 54);
    }
    HWND tplBtn = GetDlgItem(m_hSettingsWnd, IDC_SET_PLUGIN_TEMPLATE);
    if (tplBtn && IsWindow(tplBtn)) {
        MoveWindow(tplBtn, contentX, cy + ch - 42, 200, 34, TRUE);
    }

    if (m_guiDllStore.Hwnd()) {
        m_guiDllStore.Move(contentX, cy, contentW, ch - 54);
    }
}

void YXGuiPlugin::SettingsOnTabChange() {
    if (!m_hSetTab) return;
    int sel = (int)SendMessageW(m_hSetTab, LB_GETCURSEL, 0, 0);
    if (sel < 0) sel = 0;
    m_settingsCurTab = sel;

    for (HWND h = GetWindow(m_hSettingsWnd, GW_CHILD); h;
        h = GetWindow(h, GW_HWNDNEXT)) {
        int id = GetDlgCtrlID(h);
        if (id == IDC_SET_TAB || id == IDC_SET_OK ||
            id == IDC_SET_CANCEL || id == IDC_SET_APPLY ||
            id == IDC_SET_RESET) continue;
        ShowWindow(h, SW_HIDE);
    }
    m_pluginCenter.Show(false);
    m_guiDllStore.Show(false);

    if (sel == 0) {
        ShowWindow(m_hSetAiList, SW_SHOW);
        int ids[] = { IDC_SET_AI_ADD, IDC_SET_AI_EDIT, IDC_SET_AI_DEL,
                      IDC_SET_AI_UP, IDC_SET_AI_DOWN };
        for (int id : ids) {
            HWND b = GetDlgItem(m_hSettingsWnd, id);
            if (b) ShowWindow(b, SW_SHOW);
        }
        RefreshSettingsAiList();
    }
    else if (sel == 1) {
        ShowWindow(m_hSetTbList, SW_SHOW);
        int ids[] = { IDC_SET_TB_UP, IDC_SET_TB_DOWN, IDC_SET_TB_TOGGLE };
        for (int id : ids) {
            HWND b = GetDlgItem(m_hSettingsWnd, id);
            if (b) ShowWindow(b, SW_SHOW);
        }
        RefreshSettingsTbList();
    }
    else if (sel == 2) {
        ShowWindow(m_hSetWinMainW, SW_SHOW);
        ShowWindow(m_hSetWinMainH, SW_SHOW);
        ShowWindow(m_hSetWinBrowserH, SW_SHOW);
        ShowWindow(m_hSetWinExtW, SW_SHOW);
        ShowWindow(m_hSetWinExtH, SW_SHOW);
        ShowWindow(m_hSetWinPopupW, SW_SHOW);
        ShowWindow(m_hSetWinPopupH, SW_SHOW);
        ShowWindow(m_hSetWinOptW, SW_SHOW);
        ShowWindow(m_hSetWinOptH, SW_SHOW);

        for (HWND h = GetWindow(m_hSettingsWnd, GW_CHILD); h;
            h = GetWindow(h, GW_HWNDNEXT)) {
            wchar_t cls[64] = { 0 };
            GetClassNameW(h, cls, 64);
            if (_wcsicmp(cls, L"Static") == 0) {
                int id = GetDlgCtrlID(h);
                if (id >= 4200 && id < 4210) ShowWindow(h, SW_SHOW);
            }
        }
        RefreshSettingsWindowValues();
    }
    else if (sel == 3) {
        m_pluginCenter.Show(true);
        RefreshPluginCenter();
        HWND tplBtn = GetDlgItem(m_hSettingsWnd, IDC_SET_PLUGIN_TEMPLATE);
        if (tplBtn) ShowWindow(tplBtn, SW_SHOW);
    }
    else if (sel == 4) {
        m_guiDllStore.Show(true);
        m_guiDllStore.Refresh();
    }

    InvalidateRect(m_hSettingsWnd, nullptr, FALSE);
}

void YXGuiPlugin::RefreshSettingsAiList() {
    if (!m_hSetAiList) return;
    SendMessageW(m_hSetAiList, LB_RESETCONTENT, 0, 0);
    for (const auto& item : m_config.aiButtons) {
        std::wstring line = item.name + L"  —  " + item.url;
        SendMessageW(m_hSetAiList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
}

void YXGuiPlugin::RefreshSettingsTbList() {
    if (!m_hSetTbList) return;
    SendMessageW(m_hSetTbList, LB_RESETCONTENT, 0, 0);
    for (const auto& item : m_config.toolbarOrder) {
        std::wstring line = ToolbarDisplayName(item.id);
        line += item.visible ? L"  [显示]" : L"  [隐藏]";
        SendMessageW(m_hSetTbList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
}

void YXGuiPlugin::RefreshSettingsWindowValues() {
    wchar_t buf[32];
    auto setEdit = [&](HWND h, int v) {
        if (h) { swprintf_s(buf, L"%d", v); SetWindowTextW(h, buf); }
        };
    setEdit(m_hSetWinMainW, m_config.mainW);
    setEdit(m_hSetWinMainH, m_config.mainH);
    setEdit(m_hSetWinBrowserH, m_config.browserH);
    setEdit(m_hSetWinExtW, m_config.extMgrW);
    setEdit(m_hSetWinExtH, m_config.extMgrH);
    setEdit(m_hSetWinPopupW, m_config.popupW);
    setEdit(m_hSetWinPopupH, m_config.popupH);
    setEdit(m_hSetWinOptW, m_config.optionsW);
    setEdit(m_hSetWinOptH, m_config.optionsH);
}

void YXGuiPlugin::ApplySettingsWindowValues() {
    auto getInt = [&](HWND h, int defVal, int minVal, int maxVal) -> int {
        if (!h) return defVal;
        wchar_t buf[32] = { 0 };
        GetWindowTextW(h, buf, 32);
        try {
            int v = std::stoi(buf);
            if (v < minVal) v = minVal;
            if (v > maxVal) v = maxVal;
            return v;
        }
        catch (...) { return defVal; }
        };
    m_config.mainW = getInt(m_hSetWinMainW, m_config.mainW, 900, 4096);
    m_config.mainH = getInt(m_hSetWinMainH, m_config.mainH, 600, 4096);
    m_config.browserH = getInt(m_hSetWinBrowserH, m_config.browserH,
        YXStyle::BROWSER_MIN_H, YXStyle::BROWSER_MAX_H);
    m_config.extMgrW = getInt(m_hSetWinExtW, m_config.extMgrW, 760, 1200);
    m_config.extMgrH = getInt(m_hSetWinExtH, m_config.extMgrH, 500, 900);
    m_config.popupW = getInt(m_hSetWinPopupW, m_config.popupW, 200, 4096);
    m_config.popupH = getInt(m_hSetWinPopupH, m_config.popupH, 200, 4096);
    m_config.optionsW = getInt(m_hSetWinOptW, m_config.optionsW, 400, 4096);
    m_config.optionsH = getInt(m_hSetWinOptH, m_config.optionsH, 300, 4096);
}

LRESULT CALLBACK YXGuiPlugin::SettingsWndProc(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pThis = (YXGuiPlugin*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        if (!pThis) return -1;

        pThis->m_hSetTab = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
            LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_TABSTOP,
            14, 104, 188, 400, hWnd, (HMENU)IDC_SET_TAB,
            pThis->m_hInstance, NULL);
        SendMessageW(pThis->m_hSetTab, LB_SETITEMHEIGHT, 0, 48);
        const wchar_t* navItems[] = {
            L"AI 快捷按钮", L"工具栏", L"窗口尺寸", L"插件中心", L"插件商店"
        };
        for (const wchar_t* item : navItems) {
            SendMessageW(pThis->m_hSetTab, LB_ADDSTRING, 0, (LPARAM)item);
        }
        SendMessageW(pThis->m_hSetTab, LB_SETCURSEL, 0, 0);

        pThis->m_hSetAiList = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
            LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_TABSTOP,
            30, 120, 540, 300, hWnd, (HMENU)IDC_SET_AI_LIST,
            pThis->m_hInstance, NULL);
        SendMessageW(pThis->m_hSetAiList, LB_SETITEMHEIGHT, 0, 48);
        SendMessageW(pThis->m_hSetAiList, WM_SETFONT,
            (WPARAM)pThis->m_hFontUI, TRUE);

        struct BtnDef { int id; const wchar_t* text; int w; };
        BtnDef aiBtns[] = {
            { IDC_SET_AI_ADD,  L"添加", 92 },
            { IDC_SET_AI_EDIT, L"编辑", 92 },
            { IDC_SET_AI_DEL,  L"删除", 92 },
            { IDC_SET_AI_UP,   L"上移", 92 },
            { IDC_SET_AI_DOWN, L"下移", 92 },
        };
        for (const auto& b : aiBtns) {
            CreateWindowExW(0, L"BUTTON", b.text,
                WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
                0, 0, b.w, 34, hWnd, (HMENU)(INT_PTR)b.id,
                pThis->m_hInstance, NULL);
        }

        pThis->m_hSetTbList = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
            LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_TABSTOP,
            30, 120, 540, 300, hWnd, (HMENU)IDC_SET_TB_LIST,
            pThis->m_hInstance, NULL);
        SendMessageW(pThis->m_hSetTbList, LB_SETITEMHEIGHT, 0, 42);
        SendMessageW(pThis->m_hSetTbList, WM_SETFONT,
            (WPARAM)pThis->m_hFontUI, TRUE);

        BtnDef tbBtns[] = {
            { IDC_SET_TB_UP,     L"上移",      100 },
            { IDC_SET_TB_DOWN,   L"下移",      100 },
            { IDC_SET_TB_TOGGLE, L"显示/隐藏", 100 },
        };
        for (const auto& b : tbBtns) {
            CreateWindowExW(0, L"BUTTON", b.text,
                WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
                0, 0, b.w, 34, hWnd, (HMENU)(INT_PTR)b.id,
                pThis->m_hInstance, NULL);
        }

        struct {
            const wchar_t* label;
            HWND* edit;
            int id;
            int labelId;
        } winRows[] = {
            { L"主窗口 宽 (px)",        &pThis->m_hSetWinMainW,    IDC_SET_WIN_MAIN_W,   4200 },
            { L"主窗口 高 (px)",        &pThis->m_hSetWinMainH,    IDC_SET_WIN_MAIN_H,   4201 },
            { L"浏览器面板 高 (px)",    &pThis->m_hSetWinBrowserH, IDC_SET_WIN_BROWSER_H,4202 },
            { L"扩展管理器 宽 (px)",    &pThis->m_hSetWinExtW,     IDC_SET_WIN_EXT_W,    4203 },
            { L"扩展管理器 高 (px)",    &pThis->m_hSetWinExtH,     IDC_SET_WIN_EXT_H,    4204 },
            { L"扩展 Popup 宽 (px)",    &pThis->m_hSetWinPopupW,   IDC_SET_WIN_POPUP_W,  4205 },
            { L"扩展 Popup 高 (px)",    &pThis->m_hSetWinPopupH,   IDC_SET_WIN_POPUP_H,  4206 },
            { L"扩展 Options 宽 (px)",  &pThis->m_hSetWinOptW,     IDC_SET_WIN_OPT_W,    4207 },
            { L"扩展 Options 高 (px)",  &pThis->m_hSetWinOptH,     IDC_SET_WIN_OPT_H,    4208 },
        };
        for (auto& row : winRows) {
            CreateWindowExW(0, L"STATIC", row.label,
                WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
                30, 120, 240, 24, hWnd,
                (HMENU)(INT_PTR)row.labelId, pThis->m_hInstance, NULL);
            *row.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP,
                270, 120, 160, 34, hWnd,
                (HMENU)(INT_PTR)row.id, pThis->m_hInstance, NULL);
        }

        CreateWindowExW(0, L"BUTTON", L"＋ 生成 API 插件模板",
            WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
            30, 120, 200, 34, hWnd,
            (HMENU)IDC_SET_PLUGIN_TEMPLATE, pThis->m_hInstance, NULL);

        CreateWindowExW(0, L"BUTTON", L"恢复默认",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 108, 36, hWnd, (HMENU)IDC_SET_RESET,
            pThis->m_hInstance, NULL);
        CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 96, 36, hWnd, (HMENU)IDC_SET_CANCEL,
            pThis->m_hInstance, NULL);
        CreateWindowExW(0, L"BUTTON", L"应用",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 96, 36, hWnd, (HMENU)IDC_SET_APPLY,
            pThis->m_hInstance, NULL);
        CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 96, 36, hWnd, (HMENU)IDC_SET_OK,
            pThis->m_hInstance, NULL);

        {
            HFONT hFont = pThis->m_hFontUI;
            for (HWND h = GetWindow(hWnd, GW_CHILD); h;
                h = GetWindow(h, GW_HWNDNEXT)) {
                SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
            }
        }

        pThis->m_pluginCenter.Create(hWnd, pThis->m_hInstance,
            IDC_SET_PLUGIN_CENTER);
        pThis->m_pluginCenter.SetToggleCallback(
            [pThis](const std::wstring& id, bool enabled) {
                pThis->HandlePluginCenterToggle(id, enabled);
            });
        pThis->m_pluginCenter.SetActionCallback(
            [pThis](const std::wstring& id, const std::wstring& action) {
                pThis->HandlePluginCenterAction(id, action);
            });
        pThis->RefreshPluginCenter();

        pThis->m_guiDllStore.Create(hWnd, pThis->m_hInstance,
            IDC_SET_GUI_DLL_STORE);

        pThis->LayoutSettingsControls();
        pThis->SettingsOnTabChange();
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 860;
        mmi->ptMinTrackSize.y = 560;
        return 0;
    }

    case WM_SIZE:
        if (pThis) pThis->LayoutSettingsControls();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        if (pThis) {
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
            pThis->DrawSettingsBackground(memDC, rc);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
        if (!pThis) break;
        if (lpdis->CtlID == IDC_SET_TAB) {
            pThis->DrawSettingsNav(lpdis);
            return TRUE;
        }
        if (lpdis->CtlID == IDC_SET_AI_LIST) {
            HDC hdc = lpdis->hDC;
            RECT rc = lpdis->rcItem;
            bool selected = (lpdis->itemState & ODS_SELECTED) != 0;
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::SolidBrush bg(selected
                ? Gdiplus::Color(255, 233, 241, 253)
                : Gdiplus::Color(255, 255, 255, 255));
            g.FillRectangle(&bg, Gdiplus::Rect(rc.left, rc.top,
                rc.right - rc.left, rc.bottom - rc.top));
            int idx = (int)lpdis->itemID;
            if (idx >= 0 && idx < (int)pThis->m_config.aiButtons.size()) {
                const auto& item = pThis->m_config.aiButtons[idx];
                Gdiplus::Font nameFont(L"Microsoft YaHei", 13,
                    Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::Font urlFont(L"Microsoft YaHei", 10,
                    Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush ink(Gdiplus::Color(255, 30, 41, 59));
                Gdiplus::SolidBrush muted(Gdiplus::Color(255, 100, 116, 139));
                Gdiplus::StringFormat fmt;
                fmt.SetAlignment(Gdiplus::StringAlignmentNear);
                fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                g.DrawString(item.name.c_str(), -1, &nameFont,
                    Gdiplus::RectF((Gdiplus::REAL)(rc.left + 14),
                        (Gdiplus::REAL)(rc.top + 6),
                        (Gdiplus::REAL)(rc.right - rc.left - 28), 22),
                    &fmt, &ink);
                g.DrawString(item.url.c_str(), -1, &urlFont,
                    Gdiplus::RectF((Gdiplus::REAL)(rc.left + 14),
                        (Gdiplus::REAL)(rc.top + 26),
                        (Gdiplus::REAL)(rc.right - rc.left - 28), 18),
                    &fmt, &muted);
            }
            return TRUE;
        }
        if (lpdis->CtlID == IDC_SET_TB_LIST) {
            HDC hdc = lpdis->hDC;
            RECT rc = lpdis->rcItem;
            bool selected = (lpdis->itemState & ODS_SELECTED) != 0;
            Gdiplus::Graphics g(hdc);
            Gdiplus::SolidBrush bg(selected
                ? Gdiplus::Color(255, 233, 241, 253)
                : Gdiplus::Color(255, 255, 255, 255));
            g.FillRectangle(&bg, Gdiplus::Rect(rc.left, rc.top,
                rc.right - rc.left, rc.bottom - rc.top));
            int idx = (int)lpdis->itemID;
            if (idx >= 0 && idx < (int)pThis->m_config.toolbarOrder.size()) {
                const auto& item = pThis->m_config.toolbarOrder[idx];
                std::wstring name = pThis->ToolbarDisplayName(item.id);
                Gdiplus::Font nameFont(L"Microsoft YaHei", 13,
                    Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush ink(Gdiplus::Color(255, 30, 41, 59));
                Gdiplus::SolidBrush muted(Gdiplus::Color(255, 100, 116, 139));
                Gdiplus::StringFormat fmt;
                fmt.SetAlignment(Gdiplus::StringAlignmentNear);
                fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                g.DrawString(name.c_str(), -1, &nameFont,
                    Gdiplus::RectF((Gdiplus::REAL)(rc.left + 14),
                        (Gdiplus::REAL)rc.top,
                        (Gdiplus::REAL)(rc.right - rc.left - 130),
                        (Gdiplus::REAL)(rc.bottom - rc.top)),
                    &fmt, &ink);
                g.DrawString(item.visible ? L"显示" : L"隐藏", -1, &nameFont,
                    Gdiplus::RectF((Gdiplus::REAL)(rc.right - 96),
                        (Gdiplus::REAL)rc.top, 70,
                        (Gdiplus::REAL)(rc.bottom - rc.top)),
                    &fmt, &muted);
            }
            return TRUE;
        }
        pThis->DrawSettingsButton(lpdis);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(51, 65, 85));
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        HWND list = (HWND)lParam;
        if (pThis && list == pThis->m_hSetTab) {
            static HBRUSH navBrush = CreateSolidBrush(RGB(30, 41, 59));
            SetBkColor(hdc, RGB(30, 41, 59));
            SetTextColor(hdc, RGB(203, 213, 225));
            return (LRESULT)navBrush;
        }
        SetBkColor(hdc, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(255, 255, 255));
        SetTextColor(hdc, RGB(30, 41, 59));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }

    case WM_COMMAND: {
        if (!pThis) break;
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_SET_TAB && code == LBN_SELCHANGE) {
            pThis->SettingsOnTabChange();
            return 0;
        }

        if (id == IDC_SET_OK || id == IDC_SET_APPLY) {
            pThis->ApplySettingsWindowValues();
            pThis->SaveConfig();
            pThis->ApplyConfig();
            if (id == IDC_SET_OK) DestroyWindow(hWnd);
            return 0;
        }
        else if (id == IDC_SET_CANCEL) {
            DestroyWindow(hWnd);
            return 0;
        }
        else if (id == IDC_SET_RESET) {
            if (MessageBoxW(hWnd, L"确定恢复默认配置？\n当前所有配置将被覆盖。",
                L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                pThis->ResetConfigToDefault();
                pThis->m_aiEnabled = pThis->m_config.aiEnabled;
                pThis->SaveConfig();
                pThis->ApplyConfig();
                pThis->RefreshSettingsAiList();
                pThis->RefreshSettingsTbList();
                pThis->RefreshSettingsWindowValues();
                pThis->RefreshPluginCenter();
                pThis->RefreshSidebar();
                pThis->LayoutSettingsControls();
            }
            return 0;
        }
        else if (id == IDC_SET_PLUGIN_TEMPLATE && code == BN_CLICKED) {
            std::wstring name = L"MyApiPlugin";
            if (ShowInputDialog(pThis->m_hInstance, hWnd,
                L"生成 API 插件模板",
                L"插件名称（生成 api_<名称>.dll 的完整工程）", name)) {
                for (wchar_t& ch : name) {
                    bool ok = (ch >= L'a' && ch <= L'z') ||
                        (ch >= L'A' && ch <= L'Z') ||
                        (ch >= L'0' && ch <= L'9') ||
                        ch == L'_' || ch == L'-';
                    if (!ok) ch = L'_';
                }
                if (!name.empty()) {
                    std::string cmd = "createplugin " + WideToUtf8(name) + " -api";
                    if (pThis->m_commandHandler) {
                        pThis->m_commandHandler(cmd);
                        MessageBoxW(hWnd,
                            L"模板生成命令已发送，请查看主界面控制台输出。\n"
                            L"生成位置：程序目录\\api_templates\\",
                            L"提示", MB_OK | MB_ICONINFORMATION);
                    }
                    else {
                        MessageBoxW(hWnd, L"命令通道未初始化，无法生成模板。",
                            L"提示", MB_OK | MB_ICONWARNING);
                    }
                }
            }
            return 0;
        }
        else if (id == IDC_SET_AI_ADD && code == BN_CLICKED) {
            AiButtonConfig cfg;
            cfg.name = L"新按钮";
            cfg.url = L"https://";
            if (pThis->ShowAiEditDialog(cfg, L"添加 AI 按钮", true)) {
                if (!cfg.name.empty()) {
                    pThis->m_config.aiButtons.push_back(cfg);
                    pThis->SaveConfig();
                    pThis->ApplyConfig();
                    pThis->RefreshSettingsAiList();
                }
            }
            return 0;
        }
        else if (id == IDC_SET_AI_EDIT && code == BN_CLICKED) {
            int sel = (int)SendMessageW(pThis->m_hSetAiList, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)pThis->m_config.aiButtons.size()) {
                MessageBoxW(hWnd, L"请先选择一个 AI 按钮", L"提示", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            AiButtonConfig cfg = pThis->m_config.aiButtons[sel];
            if (pThis->ShowAiEditDialog(cfg, L"编辑 AI 按钮", false)) {
                pThis->m_config.aiButtons[sel] = cfg;
                pThis->SaveConfig();
                pThis->ApplyConfig();
                pThis->RefreshSettingsAiList();
                SendMessageW(pThis->m_hSetAiList, LB_SETCURSEL, sel, 0);
            }
            return 0;
        }
        else if (id == IDC_SET_AI_DEL && code == BN_CLICKED) {
            int sel = (int)SendMessageW(pThis->m_hSetAiList, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)pThis->m_config.aiButtons.size()) {
                MessageBoxW(hWnd, L"请先选择一个 AI 按钮", L"提示", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            if (MessageBoxW(hWnd, L"确定删除此 AI 按钮？", L"确认",
                MB_YESNO | MB_ICONQUESTION) == IDYES) {
                pThis->m_config.aiButtons.erase(pThis->m_config.aiButtons.begin() + sel);
                pThis->SaveConfig();
                pThis->ApplyConfig();
                pThis->RefreshSettingsAiList();
                if (sel < (int)pThis->m_config.aiButtons.size())
                    SendMessageW(pThis->m_hSetAiList, LB_SETCURSEL, sel, 0);
            }
            return 0;
        }
        else if (id == IDC_SET_AI_UP && code == BN_CLICKED) {
            int sel = (int)SendMessageW(pThis->m_hSetAiList, LB_GETCURSEL, 0, 0);
            if (sel <= 0) return 0;
            std::swap(pThis->m_config.aiButtons[sel], pThis->m_config.aiButtons[sel - 1]);
            pThis->SaveConfig();
            pThis->ApplyConfig();
            pThis->RefreshSettingsAiList();
            SendMessageW(pThis->m_hSetAiList, LB_SETCURSEL, sel - 1, 0);
            return 0;
        }
        else if (id == IDC_SET_AI_DOWN && code == BN_CLICKED) {
            int sel = (int)SendMessageW(pThis->m_hSetAiList, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)pThis->m_config.aiButtons.size() - 1) return 0;
            std::swap(pThis->m_config.aiButtons[sel], pThis->m_config.aiButtons[sel + 1]);
            pThis->SaveConfig();
            pThis->ApplyConfig();
            pThis->RefreshSettingsAiList();
            SendMessageW(pThis->m_hSetAiList, LB_SETCURSEL, sel + 1, 0);
            return 0;
        }
        else if (id == IDC_SET_TB_UP && code == BN_CLICKED) {
            int sel = (int)SendMessageW(pThis->m_hSetTbList, LB_GETCURSEL, 0, 0);
            if (sel <= 0) return 0;
            std::swap(pThis->m_config.toolbarOrder[sel],
                pThis->m_config.toolbarOrder[sel - 1]);
            pThis->SaveConfig();
            pThis->ApplyToolbarLayout();
            pThis->RefreshSettingsTbList();
            SendMessageW(pThis->m_hSetTbList, LB_SETCURSEL, sel - 1, 0);
            return 0;
        }
        else if (id == IDC_SET_TB_DOWN && code == BN_CLICKED) {
            int sel = (int)SendMessageW(pThis->m_hSetTbList, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)pThis->m_config.toolbarOrder.size() - 1) return 0;
            std::swap(pThis->m_config.toolbarOrder[sel],
                pThis->m_config.toolbarOrder[sel + 1]);
            pThis->SaveConfig();
            pThis->ApplyToolbarLayout();
            pThis->RefreshSettingsTbList();
            SendMessageW(pThis->m_hSetTbList, LB_SETCURSEL, sel + 1, 0);
            return 0;
        }
        else if (id == IDC_SET_TB_TOGGLE && code == BN_CLICKED) {
            int sel = (int)SendMessageW(pThis->m_hSetTbList, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)pThis->m_config.toolbarOrder.size()) return 0;
            pThis->m_config.toolbarOrder[sel].visible =
                !pThis->m_config.toolbarOrder[sel].visible;
            pThis->SaveConfig();
            pThis->ApplyToolbarLayout();
            pThis->RefreshSettingsTbList();
            SendMessageW(pThis->m_hSetTbList, LB_SETCURSEL, sel, 0);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        if (pThis) {
            pThis->m_hSettingsWnd = nullptr;
            pThis->m_hSetTab = nullptr;
            pThis->m_hSetAiList = nullptr;
            pThis->m_hSetTbList = nullptr;
            pThis->m_hSetWinMainW = nullptr;
            pThis->m_hSetWinMainH = nullptr;
            pThis->m_hSetWinBrowserH = nullptr;
            pThis->m_hSetWinExtW = nullptr;
            pThis->m_hSetWinExtH = nullptr;
            pThis->m_hSetWinPopupW = nullptr;
            pThis->m_hSetWinPopupH = nullptr;
            pThis->m_hSetWinOptW = nullptr;
            pThis->m_hSetWinOptH = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void YXGuiPlugin::ShowSettingsDialog() {
    if (m_hSettingsWnd && IsWindow(m_hSettingsWnd)) {
        SetForegroundWindow(m_hSettingsWnd);
        return;
    }

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = m_hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = SETTINGS_CLASS;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        reg = true;
    }

    RECT rcMain;
    GetWindowRect(m_hWnd, &rcMain);
    int w = 1000, h = 700;
    int x = rcMain.left + ((rcMain.right - rcMain.left) - w) / 2;
    int y = rcMain.top + ((rcMain.bottom - rcMain.top) - h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    m_hSettingsWnd = CreateWindowExW(
        0, SETTINGS_CLASS, L"设置 - Light",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME,
        x, y, w, h,
        m_hWnd, NULL, m_hInstance, this);

    if (!m_hSettingsWnd) return;

    ShowWindow(m_hSettingsWnd, SW_SHOW);
    UpdateWindow(m_hSettingsWnd);
}


// ============================================================
// 绘制 AI 按钮（SaveDC + IntersectClipRect 防溢出）
// ============================================================
void YXGuiPlugin::DrawAiButton(LPDRAWITEMSTRUCT lpdis, int id) {
    if (id < 0 || id >= (int)m_config.aiButtons.size()) return;

    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem;
    bool pressed = (lpdis->itemState & ODS_SELECTED) != 0;
    bool hovered = (id < (int)m_aiHovered.size()) && m_aiHovered[id];

    int saved = SaveDC(hdc);
    IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::RectF card(
        (Gdiplus::REAL)rc.left + 5.0f,
        (Gdiplus::REAL)rc.top + 5.0f,
        (Gdiplus::REAL)(rc.right - rc.left - 10),
        (Gdiplus::REAL)(rc.bottom - rc.top - 10));

    Gdiplus::GraphicsPath shadow;
    const float shadowR = 15.0f;
    shadow.AddArc(card.X + 1, card.Y + 3, shadowR, shadowR, 180, 90);
    shadow.AddArc(card.X + card.Width - shadowR + 1, card.Y + 3, shadowR, shadowR, 270, 90);
    shadow.AddArc(card.X + card.Width - shadowR + 1, card.Y + card.Height - shadowR + 3, shadowR, shadowR, 0, 90);
    shadow.AddArc(card.X + 1, card.Y + card.Height - shadowR + 3, shadowR, shadowR, 90, 90);
    shadow.CloseFigure();
    Gdiplus::SolidBrush shadowBrush(hovered
        ? Gdiplus::Color(40, 36, 55, 84)
        : Gdiplus::Color(24, 36, 55, 78));
    g.FillPath(&shadowBrush, &shadow);

    Gdiplus::GraphicsPath path;
    const float radius = 14.0f;
    path.AddArc(card.X, card.Y, radius, radius, 180, 90);
    path.AddArc(card.X + card.Width - radius, card.Y, radius, radius, 270, 90);
    path.AddArc(card.X + card.Width - radius, card.Y + card.Height - radius, radius, radius, 0, 90);
    path.AddArc(card.X, card.Y + card.Height - radius, radius, radius, 90, 90);
    path.CloseFigure();

    Gdiplus::SolidBrush cardBrush(hovered || pressed
        ? Gdiplus::Color(255, 255, 255, 255)
        : Gdiplus::Color(255, 248, 250, 253));
    g.FillPath(&cardBrush, &path);

    Gdiplus::Pen cardPen(
        pressed ? Gdiplus::Color(255, 53, 104, 188)
                : hovered ? Gdiplus::Color(255, 146, 178, 224)
                          : Gdiplus::Color(255, 220, 227, 237),
        pressed ? 2.0f : 1.0f);
    g.DrawPath(&cardPen, &path);

    int hash = 0;
    for (wchar_t c : m_config.aiButtons[id].name) hash = hash * 31 + (int)c;
    BYTE rr = (BYTE)(61 + (hash & 55));
    BYTE gg = (BYTE)(96 + ((hash >> 8) & 70));
    BYTE bb = (BYTE)(162 + ((hash >> 16) & 52));

    Gdiplus::SolidBrush accent(Gdiplus::Color(255, rr, gg, bb));
    g.FillRectangle(&accent,
        (Gdiplus::REAL)(card.X + 8.0f),
        (Gdiplus::REAL)(card.Y + 14.0f),
        4.0f,
        (Gdiplus::REAL)(card.Height - 28.0f));

    Gdiplus::RectF badge(card.X + 20, card.Y + 16, 40, 40);
    Gdiplus::SolidBrush badgeBrush(Gdiplus::Color(255, rr, gg, bb));
    g.FillEllipse(&badgeBrush, badge);

    std::wstring initial = m_config.aiButtons[id].name.empty()
        ? L"?" : m_config.aiButtons[id].name.substr(0, 1);
    Gdiplus::Font badgeFont(L"Microsoft YaHei", 17,
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::StringFormat center;
    center.SetAlignment(Gdiplus::StringAlignmentCenter);
    center.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(initial.c_str(), -1, &badgeFont, badge, &center, &white);

    Gdiplus::Font labelFont(L"Microsoft YaHei", 13,
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 37, 47, 61));
    Gdiplus::StringFormat labelFmt;
    labelFmt.SetAlignment(Gdiplus::StringAlignmentNear);
    labelFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    labelFmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    labelFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    Gdiplus::RectF labelRect(card.X + 72, card.Y + 12,
        card.Width - 84, card.Height - 24);
    g.DrawString(m_config.aiButtons[id].name.c_str(), -1, &labelFont,
        labelRect, &labelFmt, &labelBrush);

    RestoreDC(hdc, saved);
}

// ============================================================
// 绘制工具栏按钮
// ============================================================
void YXGuiPlugin::DrawToolbarButton(LPDRAWITEMSTRUCT lpdis, int id) {
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem;
    bool pressed = (lpdis->itemState & ODS_SELECTED) != 0;
    bool disabled = (lpdis->itemState & ODS_DISABLED) != 0;
    bool hovered = (id == m_toolbarHoveredId);
    const bool isSettings = (id == IDC_TOPBAR_SETTINGS);

    int saved = SaveDC(hdc);
    IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::RectF r(
        (Gdiplus::REAL)rc.left + 2,
        (Gdiplus::REAL)rc.top + (isSettings ? 4 : 3),
        (Gdiplus::REAL)(rc.right - rc.left - 4),
        (Gdiplus::REAL)(rc.bottom - rc.top - (isSettings ? 8 : 6)));

    Gdiplus::GraphicsPath path;
    const float radius = isSettings ? 11.0f : 9.0f;
    path.AddArc(r.X, r.Y, radius, radius, 180, 90);
    path.AddArc(r.X + r.Width - radius, r.Y, radius, radius, 270, 90);
    path.AddArc(r.X + r.Width - radius, r.Y + r.Height - radius, radius, radius, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - radius, radius, radius, 90, 90);
    path.CloseFigure();

    Gdiplus::Color fill;
    if (isSettings) {
        fill = pressed || hovered
            ? Gdiplus::Color(255, 255, 255, 255)
            : Gdiplus::Color(255, 240, 245, 252);
    }
    else {
        fill = disabled
            ? Gdiplus::Color(150, 232, 239, 248)
            : pressed
            ? Gdiplus::Color(255, 214, 228, 248)
            : hovered
            ? Gdiplus::Color(255, 240, 247, 255)
            : Gdiplus::Color(210, 225, 236, 249);
    }
    Gdiplus::SolidBrush brush(fill);
    g.FillPath(&brush, &path);

    if (hovered || isSettings) {
        Gdiplus::Pen pen(
            isSettings
                ? Gdiplus::Color(255, 207, 219, 236)
                : Gdiplus::Color(255, 154, 184, 224),
            1.0f);
        g.DrawPath(&pen, &path);
    }

    wchar_t text[32] = { 0 };
    GetWindowTextW(lpdis->hwndItem, text, 32);

    Gdiplus::Font font(
        isSettings ? L"Microsoft YaHei" : L"Segoe UI Symbol",
        (Gdiplus::REAL)(isSettings ? 13 : 17),
        isSettings ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
        Gdiplus::UnitPixel);

    Gdiplus::SolidBrush textBrush(
        disabled ? Gdiplus::Color(255, 160, 170, 182)
                 : isSettings ? Gdiplus::Color(255, 48, 75, 112)
                              : Gdiplus::Color(255, 40, 67, 105));

    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

    g.DrawString(text, -1, &font, r, &fmt, &textBrush);

    RestoreDC(hdc, saved);
}

// ============================================================
// 绘制 AI 滚动条
// ============================================================
void YXGuiPlugin::DrawAiScrollBar(HDC hdc, const RECT& rcClient) {
    if (m_browserExpanded) return;

    int contentTop = YXStyle::TOPBAR_H + 2;
    int contentH = rcClient.bottom - m_browserHeight - YXStyle::SPLITTER_H - contentTop;
    if (contentH < 100) contentH = 100;

    int viewportTop = contentTop + YXStyle::PANEL_HEADER_H;
    int viewportH = contentH - YXStyle::PANEL_HEADER_H;
    if (viewportH < 60) viewportH = 60;

    int total = (int)m_config.aiButtons.size() *
        (YXStyle::AI_BTN_SIZE + YXStyle::AI_BTN_GAP) + YXStyle::AI_BTN_GAP;
    if (total <= viewportH) return;

    int trackX = rcClient.right - YXStyle::AI_SCROLLBAR_W - 4;
    int trackY = viewportTop + 2;
    int trackH = viewportH - 4;

    HBRUSH trackBrush = CreateSolidBrush(RGB(48, 48, 54));
    RECT trackRect = { trackX, trackY, trackX + YXStyle::AI_SCROLLBAR_W, trackY + trackH };
    FillRect(hdc, &trackRect, trackBrush);
    DeleteObject(trackBrush);

    int thumbH = (std::max)(30, trackH * viewportH / total);
    int maxScroll = total - viewportH;
    if (maxScroll <= 0) return;
    int thumbY = trackY + (trackH - thumbH) * m_aiScrollY / maxScroll;

    HBRUSH thumbBrush = CreateSolidBrush(
        m_aiDraggingScroll ? RGB(160, 180, 220) : RGB(110, 125, 160));
    RECT thumbRect = { trackX, thumbY, trackX + YXStyle::AI_SCROLLBAR_W, thumbY + thumbH };
    FillRect(hdc, &thumbRect, thumbBrush);
    DeleteObject(thumbBrush);
}

// ============================================================
// 重建 AI 按钮
// ============================================================
void YXGuiPlugin::RebuildAiButtons() {
    for (HWND h : m_aiButtons) {
        if (h && IsWindow(h)) DestroyWindow(h);
    }
    m_aiButtons.clear();
    m_aiHovered.clear();

    for (size_t i = 0; i < m_config.aiButtons.size(); i++) {
        HWND hBtn = CreateWindowExW(0, L"BUTTON",
            m_config.aiButtons[i].name.c_str(),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, YXStyle::AI_BTN_SIZE, YXStyle::AI_BTN_SIZE,
            m_hWnd, (HMENU)(INT_PTR)(IDC_AI_BASE + i),
            m_hInstance, NULL);
        if (hBtn) {
            SetWindowSubclass(hBtn, AiButtonSubclassProc, 2, (DWORD_PTR)this);
            m_aiButtons.push_back(hBtn);
            m_aiHovered.push_back(false);
        }
    }

    if (m_hWnd) {
        RECT rc; GetClientRect(m_hWnd, &rc);
        OnSize(rc.right, rc.bottom);
    }
}

// ============================================================
// 创建主窗口
// ============================================================
HWND YXGuiPlugin::CreateCustomWindow(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    INITCOMMONCONTROLSEX icc = {
        sizeof(icc),
        ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_WIN95_CLASSES
    };
    InitCommonControlsEx(&icc);

    HRESULT hrOle = OleInitialize(NULL);
    m_oleInitialized = SUCCEEDED(hrOle);
    if (!m_oleInitialized) {
        HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hrCo) || hrCo == S_FALSE) {
            m_oleInitialized = true;
        }
    }
    LoadLibraryW(L"Msftedit.dll");

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL) != Gdiplus::Ok)
        m_gdiplusToken = 0;

    m_hbrBackground = CreateSolidBrush(YXStyle::BG);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = m_hbrBackground;
    wc.lpszClassName = L"YXGuiPluginWnd";
    wc.hIcon = LoadIcon(NULL, IDI_SHIELD);
    RegisterClassExW(&wc);

    m_hWnd = CreateWindowExW(
        0, L"YXGuiPluginWnd", L"Light  ·  YXGui 安全核心",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        m_config.mainW, m_config.mainH,
        NULL, NULL, hInstance, this);
    if (!m_hWnd) return nullptr;

    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeFullPath(exePath);
    size_t lastSlash = exeFullPath.find_last_of("\\/");
    std::string programDir = (lastSlash != std::string::npos)
        ? exeFullPath.substr(0, lastSlash) : exeFullPath;

    m_hFontUI = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    m_hFontSidebar = CreateFontW(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    m_hFontConsole = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    m_hFontAddress = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"SimSun");

    m_hTopbarSettingsBtn = CreateWindowExW(0, L"BUTTON", L"⚙  设置",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        8, 4, 118, YXStyle::TOPBAR_H - 8, m_hWnd,
        (HMENU)(INT_PTR)IDC_TOPBAR_SETTINGS, hInstance, NULL);
    SendMessageW(m_hTopbarSettingsBtn, WM_SETFONT, (WPARAM)m_hFontUI, TRUE);
    SetWindowSubclass(m_hTopbarSettingsBtn, ToolbarButtonSubclassProc, 5, (DWORD_PTR)this);

    m_hHelpEdit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 100, 100, m_hWnd, (HMENU)(INT_PTR)IDC_HELP_EDIT,
        hInstance, NULL);
    SendMessageW(m_hHelpEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(248, 250, 253));
    SendMessageW(m_hHelpEdit, WM_SETFONT, (WPARAM)m_hFontUI, TRUE);
    Help_Populate(m_hHelpEdit, programDir);

    m_hConsoleEdit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 100, 100, m_hWnd, (HMENU)(INT_PTR)IDC_CONSOLE_EDIT,
        hInstance, NULL);
    SendMessageW(m_hConsoleEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)YXStyle::CONSOLE_BG);

    m_hPromptStatic = CreateWindowExW(0, L"STATIC", L"<D:\\>",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY,
        0, 0, 100, 24, m_hWnd, (HMENU)(INT_PTR)IDC_PROMPT_STATIC,
        hInstance, NULL);
    SendMessageW(m_hPromptStatic, WM_SETFONT, (WPARAM)m_hFontConsole, TRUE);

    m_hConsoleInput = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 100, 24, m_hWnd, (HMENU)(INT_PTR)IDC_CONSOLE_INPUT,
        hInstance, NULL);
    SendMessageW(m_hConsoleEdit, WM_SETFONT, (WPARAM)m_hFontConsole, TRUE);
    SendMessageW(m_hConsoleInput, WM_SETFONT, (WPARAM)m_hFontConsole, TRUE);
    SetWindowSubclass(m_hConsoleInput, ConsoleInputSubclassProc, 1, (DWORD_PTR)this);

    {
        const wchar_t* welcome =
            L"_________________________________________________________________________________________________\r\n"
            L"===========================================Hello itc!============================================\r\n"
            L"_________________________________________________________________________________________________\r\n\r\n";
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = YXStyle::CONSOLE_TEXT;
        SendMessageW(m_hConsoleEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
        SendMessageW(m_hConsoleEdit, EM_REPLACESEL, FALSE, (LPARAM)welcome);
        SendMessageW(m_hConsoleEdit, EM_SETSEL, 0, 0);
    }

    for (size_t i = 0; i < m_config.aiButtons.size(); i++) {
        HWND hBtn = CreateWindowExW(0, L"BUTTON",
            m_config.aiButtons[i].name.c_str(),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, YXStyle::AI_BTN_SIZE, YXStyle::AI_BTN_SIZE,
            m_hWnd, (HMENU)(INT_PTR)(IDC_AI_BASE + i),
            hInstance, NULL);
        SetWindowSubclass(hBtn, AiButtonSubclassProc, 2, (DWORD_PTR)this);
        m_aiButtons.push_back(hBtn);
        m_aiHovered.push_back(false);
    }

    m_hAddressBar = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        L"https://www.bing.com",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 100, 26, m_hWnd, (HMENU)(INT_PTR)IDC_BROWSER_ADDRESS,
        hInstance, NULL);
    SendMessageW(m_hAddressBar, WM_SETFONT, (WPARAM)m_hFontAddress, TRUE);
    SetWindowSubclass(m_hAddressBar, AddressBarSubclassProc, 3, (DWORD_PTR)this);

    struct { HWND* h; const wchar_t* text; int id; } navBtns[] = {
        { &m_hNavBack,      L"←",   IDC_NAV_BACK },
        { &m_hNavForward,   L"→",   IDC_NAV_FORWARD },
        { &m_hNavRefresh,   L"⟳",   IDC_NAV_REFRESH },
        { &m_hNavHome,      L"🏠",  IDC_NAV_HOME },
        { &m_hNavStar,      L"☆",   IDC_NAV_STAR },
        { &m_hFavBtn,       L"★",   IDC_NAV_FAV },
        { &m_hNavZoomIn,    L"🔍+", IDC_NAV_ZOOM_IN },
        { &m_hNavZoomOut,   L"🔍-", IDC_NAV_ZOOM_OUT },
        { &m_hNavDownload,  L"⬇",   IDC_NAV_DOWNLOAD },
        { &m_hNavExtension, L"插件",  IDC_NAV_EXTENSION },
    };
    for (auto& nb : navBtns) {
        *nb.h = CreateWindowExW(0, L"BUTTON", nb.text,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 30, 30, m_hWnd, (HMENU)(INT_PTR)nb.id,
            hInstance, NULL);
        SetWindowSubclass(*nb.h, ToolbarButtonSubclassProc, 4, (DWORD_PTR)this);
    }

    m_hRestoreBtn = CreateWindowExW(0, L"BUTTON", L"返回工作台",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 86, 26, m_hWnd, (HMENU)(INT_PTR)IDC_BROWSER_RESTORE,
        hInstance, NULL);
    SendMessageW(m_hRestoreBtn, WM_SETFONT, (WPARAM)m_hFontAddress, TRUE);

    {
        static bool tabClassReg = false;
        if (!tabClassReg) {
            WNDCLASSEXW wcTab = { sizeof(wcTab) };
            wcTab.lpfnWndProc = TabBarWndProc;
            wcTab.hInstance = hInstance;
            wcTab.hCursor = LoadCursor(NULL, IDC_ARROW);
            wcTab.hbrBackground = CreateSolidBrush(RGB(30, 30, 36));
            wcTab.lpszClassName = L"YXGuiTabBarWnd";
            RegisterClassExW(&wcTab);
            tabClassReg = true;
        }
        m_hTabBar = CreateWindowExW(0, L"YXGuiTabBarWnd", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, 100, YXStyle::TAB_BAR_H, m_hWnd, NULL, hInstance, this);

        m_hTabContainer = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | SS_NOTIFY,
            0, 0, 100, 100, m_hWnd, NULL, hInstance, NULL);
    }

    // ========== 插件宿主容器（JS 插件用的父窗口） ==========
    m_hPluginHost = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 100, 100, m_hWnd, NULL, hInstance, NULL);
    if (m_hPluginHost) {
        SetWindowLongPtrW(m_hPluginHost, GWLP_USERDATA, (LONG_PTR)this);
        m_oldPluginHostProc = (WNDPROC)SetWindowLongPtrW(m_hPluginHost,
            GWLP_WNDPROC, (LONG_PTR)PluginHostWndProc);
        // v9.1: 接受文件拖放
        DragAcceptFiles(m_hPluginHost, TRUE);
    }
    ShowWindow(m_hPluginHost, SW_HIDE);

    CreateSidebar();

    LoadFavorites();

    m_hTooltip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        m_hWnd, NULL, hInstance, NULL);
    if (m_hTooltip) {
        SetWindowPos(m_hTooltip, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        AddTooltip(m_hNavBack, L"后退");
        AddTooltip(m_hNavForward, L"前进");
        AddTooltip(m_hNavRefresh, L"刷新");
        AddTooltip(m_hNavHome, L"主页");
        AddTooltip(m_hNavStar, L"收藏/取消收藏");
        AddTooltip(m_hFavBtn, L"收藏夹");
        AddTooltip(m_hNavZoomIn, L"放大");
        AddTooltip(m_hNavZoomOut, L"缩小");
        AddTooltip(m_hNavDownload, L"下载");
        AddTooltip(m_hNavExtension, L"扩展管理");
        AddTooltip(m_hAddressBar, L"输入网址后回车");
        AddTooltip(m_hTopbarSettingsBtn, L"打开设置");
    }

    RECT rc;
    GetClientRect(m_hWnd, &rc);
    OnSize(rc.right, rc.bottom);
    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);

    InitWebView();
    return m_hWnd;
}

void YXGuiPlugin::SetCommandHandler(CommandHandler handler) { m_commandHandler = handler; }
void YXGuiPlugin::SetOutputHandler(OutputHandler handler) { m_outputHandler = handler; }

// ============================================================
// 输出
// ============================================================
void YXGuiPlugin::AppendOutput(const std::string& text) {
    if (text.empty()) return;
    if (!m_hWnd || !IsWindow(m_hWnd)) return;
    std::string* pText = new std::string(text);
    if (!PostMessageW(m_hWnd, WM_YXGUI_APPEND_OUTPUT, 0, (LPARAM)pText))
        delete pText;
}

// 把 ANSI SGR 前景色码（30-37 / 90-97）映射为深色底下的 COLORREF。
// 返回 0xFFFFFFFF 表示未知码（调用方保持当前颜色不变）。
static COLORREF AnsiSgrToColor(int code) {
    switch (code) {
    case 0:  return RGB(210, 218, 231);  // 重置 → 默认前景
    case 30: return RGB(105, 115, 128);  // 黑（在深底上给个可见灰）
    case 31: return RGB(220, 95, 95);    // 红
    case 32: return RGB(95, 200, 130);   // 绿
    case 33: return RGB(230, 200, 95);   // 黄
    case 34: return RGB(95, 150, 255);   // 蓝
    case 35: return RGB(200, 130, 255);  // 品红
    case 36: return RGB(95, 215, 235);   // 青
    case 37: return RGB(210, 218, 231);  // 白
    case 90: return RGB(140, 148, 160);  // 亮黑（灰）
    case 91: return RGB(255, 110, 110);  // 亮红
    case 92: return RGB(110, 235, 145);  // 亮绿
    case 93: return RGB(255, 225, 105);  // 亮黄
    case 94: return RGB(110, 170, 255);  // 亮蓝
    case 95: return RGB(230, 150, 255);  // 亮品红
    case 96: return RGB(110, 235, 250);  // 亮青
    case 97: return RGB(245, 250, 255);  // 亮白
    default: return 0xFFFFFFFF;          // 未知，保持当前色
    }
}

// 把一段纯文本（已不含 ANSI 转义）按指定颜色追加到控制台富文本框。
static void AppendConsoleSegment(HWND hEdit, const std::string& text,
    COLORREF color) {
    std::string crlf;
    crlf.reserve(text.size() + 8);
    for (char c : text) {
        if (c == '\r') continue;
        if (c == '\n') crlf += "\r\n";
        else crlf += c;
    }
    if (crlf.empty()) return;
    std::wstring wtext = AnsiToWide(crlf);

    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;

    int textLen = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, textLen, textLen);
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)wtext.c_str());
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
}

void YXGuiPlugin::AppendConsoleText(const std::string& text) {
    if (!m_hConsoleEdit || !IsWindow(m_hConsoleEdit)) return;

    // 逐段解析 ANSI SGR 颜色码（\x1b[<code>m），把控制台的彩色文字映射到富文本框。
    const COLORREF kDefault = RGB(210, 218, 231);
    COLORREF cur = kDefault;

    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\x1b') {
            size_t j = i + 1;
            if (j < text.size() && text[j] == '[') {
                size_t k = j + 1;
                while (k < text.size() &&
                       (isdigit((unsigned char)text[k]) || text[k] == ';')) ++k;
                if (k < text.size() && text[k] == 'm') {
                    std::string p = text.substr(j + 1, k - j - 1);
                    int code = p.empty() ? 0 : atoi(p.c_str());
                    COLORREF c = AnsiSgrToColor(code);
                    if (c != 0xFFFFFFFF) cur = c;
                    i = k + 1;
                    continue;
                }
            }
            // 非 SGR 转义序列：丢弃 ESC 字符继续
            ++i;
            continue;
        }
        size_t start = i;
        while (i < text.size() && text[i] != '\x1b') ++i;
        AppendConsoleSegment(m_hConsoleEdit, text.substr(start, i - start), cur);
    }
}

// ============================================================
// 关闭清理
// ============================================================
void YXGuiPlugin::OnWindowClose() {
    StopCrxWatcher();
    UnloadJSPlugins();

    if (m_popupWebView) { m_popupWebView->Release(); m_popupWebView = nullptr; }
    if (m_popupController) {
        m_popupController->Close();
        m_popupController->Release();
        m_popupController = nullptr;
    }
    if (m_popupEnv) { m_popupEnv->Release(); m_popupEnv = nullptr; }
    m_popupReady = false;

    if (m_optionsWebView) { m_optionsWebView->Release(); m_optionsWebView = nullptr; }
    if (m_optionsController) {
        m_optionsController->Close();
        m_optionsController->Release();
        m_optionsController = nullptr;
    }
    if (m_optionsEnv) { m_optionsEnv->Release(); m_optionsEnv = nullptr; }

    for (auto& t : m_tabs) {
        if (t.controller) {
            t.controller->Close();
            t.controller->Release();
            t.controller = nullptr;
        }
        if (t.webView) { t.webView->Release(); t.webView = nullptr; }
        if (t.hostWnd) { DestroyWindow(t.hostWnd); t.hostWnd = nullptr; }
    }
    m_tabs.clear();

    if (m_webViewEnv) { m_webViewEnv->Release(); m_webViewEnv = nullptr; }
    m_webViewReady = false;
}

// ============================================================
// 功能按钮
// ============================================================
void YXGuiPlugin::ShowDownloadList() {
    std::string dlDir = GetCacheSubDir("Downloads");
    std::wstring wdir = AnsiToWide(dlDir);
    ShellExecuteW(m_hWnd, L"open", wdir.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void YXGuiPlugin::ShowExtensions() {
    std::string extDir = GetCacheSubDir("Extensions");
    std::wstring wext = AnsiToWide(extDir);
    ShellExecuteW(m_hWnd, L"open", wext.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void YXGuiPlugin::ShowSettings() {
    ShowSettingsDialog();
}

void YXGuiPlugin::OpenExtensionStore() {
    ICoreWebView2* wv = GetActiveWebView();
    if (wv) {
        wv->Navigate(L"https://microsoftedge.microsoft.com/addons/Microsoft-Edge-Extensions-Home");
    }
}

// ============================================================
// 主窗口过程
// ============================================================
// ============================================================
// 插件宿主容器窗口过程
// JS 插件 createControl 创建的 Win32 控件（父窗口 = m_hPluginHost）
// 会把 BN_CLICKED / EN_CHANGE 等通知以 WM_COMMAND 发到这里。
// 这里通过控件 HWND 反查归属的 JSPlugin，再转调 JS 全局 onControlEvent。
// ============================================================
LRESULT CALLBACK YXGuiPlugin::PluginHostWndProc(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    // v9.4: 富文本链接点击 → 反查 tag → 回发 onControlEvent(code=0x8000)
    if (msg == WM_NOTIFY && pThis) {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm && nm->code == EN_LINK) {
            ENLINK* el = (ENLINK*)lParam;
            if (el->msg == WM_LBUTTONUP) {
                std::wstring tag = YXGui_QueryRichLinkTag(
                    nm->hwndFrom, el->chrg.cpMin, el->chrg.cpMax);
                if (!tag.empty()) {
                    YXGui_SetPendingLinkTag(tag);

                    // 反查归属插件
                    for (const auto& plugin : pThis->m_jsPlugins) {
                        if (!plugin) continue;
                        for (const auto& kv : plugin->GetControls()) {
                            if (kv.second == nm->hwndFrom) {
                                std::wstring key = kv.first;
                                size_t pos = key.find(L"::");
                                std::wstring ctrlId =
                                    (pos != std::wstring::npos && pos + 2 < key.size())
                                    ? key.substr(pos + 2) : key;
                                plugin->CallOnControlEvent(ctrlId, 0x8000);
                                return 0;
                            }
                        }
                    }
                }
            }
        }
        // 其他 WM_NOTIFY 交给默认
    }

    // v9.1: 文件拖放 → 转发给当前 JS 插件
    if (msg == WM_DROPFILES && pThis) {
        HDROP hDrop = (HDROP)wParam;
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        std::vector<std::wstring> files;
        files.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            UINT len = DragQueryFileW(hDrop, i, NULL, 0);
            std::wstring buf(len, 0);
            DragQueryFileW(hDrop, i, &buf[0], len + 1);
            files.push_back(std::move(buf));
        }
        DragFinish(hDrop);

        JSPlugin* target = nullptr;
        if (pThis->m_currentPluginIndex >= 0 &&
            pThis->m_currentPluginIndex < (int)pThis->m_jsPlugins.size()) {
            target = pThis->m_jsPlugins[pThis->m_currentPluginIndex].get();
        }
        if (target && !files.empty()) {
            std::vector<const wchar_t*> ptrs;
            ptrs.reserve(files.size());
            for (auto& s : files) ptrs.push_back(s.c_str());
            YXGui_PushDroppedFiles(target, ptrs.data(), (int)ptrs.size());
        }
        return 0;
    }

    // 查看背景壁纸（纯色 / 图片 / 动态轮播）
    if (msg == WM_ERASEBKGND && pThis) {
        pThis->PaintPluginHostBackground((HDC)wParam);
        return 1;
    }

    // 控件文字 / 背景色（JS 通过 win32.setControlColor 指定）
    if ((msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN ||
        msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) && pThis) {
        HWND hCtrl = (HWND)lParam;
        // v9.1: WM_CTLCOLORLISTBOX 的 lParam 是 combobox 的下拉列表窗口，
        //       回溯到 combobox 本身才能拿到颜色配置。
        if (msg == WM_CTLCOLORLISTBOX) {
            HWND maybeCombo = GetParent(hCtrl);
            if (maybeCombo) {
                wchar_t cls[64] = { 0 };
                if (GetClassNameW(maybeCombo, cls, 64) &&
                    _wcsicmp(cls, L"ComboBox") == 0) {
                    hCtrl = maybeCombo;
                }
            }
        }
        JSPlugin* owner = nullptr;
        for (const auto& plugin : pThis->m_jsPlugins) {
            if (!plugin) continue;
            if (plugin->GetCtrlColor(hCtrl)) { owner = plugin.get(); break; }
        }
        HDC hdc = (HDC)wParam;
        COLORREF bg = RGB(255, 255, 255);
        COLORREF fg = RGB(40, 48, 60);
        bool hasBg = false, hasFg = false;
        if (owner) {
            const CtrlColor* cc = owner->GetCtrlColor(hCtrl);
            if (cc) {
                if (cc->bg != -1) { bg = (COLORREF)cc->bg; hasBg = true; }
                if (cc->fg != -1) { fg = (COLORREF)cc->fg; hasFg = true; }
            }
        }
        if (msg == WM_CTLCOLORSTATIC && owner) {
            // label 默认透明叠加在壁纸/卡片上
            if (!hasBg) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, hasFg ? fg : RGB(60, 70, 85));
                return (LRESULT)GetStockObject(NULL_BRUSH);
            }
        }
        if (hasFg) SetTextColor(hdc, fg);
        if (hasBg) {
            SetBkColor(hdc, bg);
            return (LRESULT)owner->BrushFor(bg);
        }
        if (msg == WM_CTLCOLORSTATIC) {
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
    }

    if (msg == WM_COMMAND && pThis) {
        HWND hCtrl = (HWND)lParam;
        int  code = HIWORD(wParam);

        // v9: 过滤掉 EDIT 的 EN_CHANGE（code=1），
        // 避免与下拉框的 CBN_SELCHANGE 在 JS 侧冲突。
        if (code == 1 && hCtrl) {
            wchar_t cls[64] = { 0 };
            if (GetClassNameW(hCtrl, cls, 64) && _wcsicmp(cls, L"Edit") == 0) {
                return 0;
            }
        }

        // 反查控件属于哪个 JS 插件，并提取 JS 侧控件 id
        for (const auto& plugin : pThis->m_jsPlugins) {
            if (!plugin) continue;
            for (const auto& kv : plugin->GetControls()) {
                if (kv.second == hCtrl) {
                    std::wstring key = kv.first;              // "pluginId::ctrlId"
                    size_t pos = key.find(L"::");
                    std::wstring ctrlId =
                        (pos != std::wstring::npos && pos + 2 < key.size())
                        ? key.substr(pos + 2) : key;
                    plugin->CallOnControlEvent(ctrlId, code);
                    return 0;
                }
            }
        }
    }

    if (pThis && pThis->m_oldPluginHostProc) {
        return CallWindowProcW(pThis->m_oldPluginHostProc, hWnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK YXGuiPlugin::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    YXGuiPlugin* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pThis = (YXGuiPlugin*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hWnd = hWnd;
    }
    else {
        pThis = (YXGuiPlugin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    }
    if (pThis) return pThis->HandleMessage(hWnd, msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// v9.3: WebView2 覆盖层（登录用）
// ============================================================
extern "C" void YXGui_ShowOverlayWebView(void* self, void* plugin, const wchar_t* url) {
    YXGuiPlugin* p = (YXGuiPlugin*)self;
    JSPlugin* jp = (JSPlugin*)plugin;
    if (p) p->ShowOverlayWebView(jp, url ? url : L"");
}

extern "C" void YXGui_HideOverlayWebView(void* self, void* plugin) {
    YXGuiPlugin* p = (YXGuiPlugin*)self;
    JSPlugin* jp = (JSPlugin*)plugin;
    if (p) p->HideOverlayWebView(jp);
}

void YXGuiPlugin::ShowOverlayWebView(JSPlugin* plugin, const std::wstring& url) {
    if (!m_hWnd || !IsWindow(m_hWnd)) return;

    if (m_hOverlayWnd && IsWindow(m_hOverlayWnd)) {
        DestroyWindow(m_hOverlayWnd);
        m_hOverlayWnd = nullptr;
    }

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = m_hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(250, 251, 253));
        wc.lpszClassName = L"YXGuiOverlayWnd";
        RegisterClassExW(&wc);
        reg = true;
    }

    RECT rc;
    GetClientRect(m_hWnd, &rc);
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    int w = cw * 88 / 100;
    int h = ch * 88 / 100;
    int x = (cw - w) / 2;
    int y = (ch - h) / 2;
    if (w < 480) w = cw - 40;
    if (h < 400) h = ch - 40;

    m_hOverlayWnd = CreateWindowExW(
        WS_EX_TOPMOST,
        L"YXGuiOverlayWnd", L"登录",
        WS_CHILD | WS_VISIBLE | WS_BORDER,
        x, y, w, h,
        m_hWnd, NULL, m_hInstance, this);
    if (!m_hOverlayWnd) return;

    m_ovPlugin = plugin;

    HFONT hFont = m_hFontUI;
    HWND hTip = CreateWindowExW(0, L"STATIC",
        L"请在弹出的页面登录目标网站，完成后点右侧按钮",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        14, 10, w - 140, 24, m_hOverlayWnd, NULL, m_hInstance, NULL);
    if (hFont) SendMessageW(hTip, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hClose = CreateWindowExW(0, L"BUTTON", L"完成 / 关闭",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        w - 120, 6, 108, 30, m_hOverlayWnd, (HMENU)1001, m_hInstance, NULL);
    if (hFont) SendMessageW(hClose, WM_SETFONT, (WPARAM)hFont, TRUE);

    m_hOverlayHost = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE,
        0, 44, w, h - 44, m_hOverlayWnd, NULL, m_hInstance, NULL);

    ShowWindow(m_hOverlayWnd, SW_SHOW);
    UpdateWindow(m_hOverlayWnd);

    std::string userDataFolder = GetCacheSubDir("WebView2_Plugin");
    std::wstring wUserData = AnsiToWide(userDataFolder);

    ICoreWebView2EnvironmentOptions* options = new SimpleEnvOptions();
    if (options) {
        ICoreWebView2EnvironmentOptions6* o6 = nullptr;
        if (SUCCEEDED(options->QueryInterface(
            IID_ICoreWebView2EnvironmentOptions6, (void**)&o6)) && o6) {
            o6->put_AreBrowserExtensionsEnabled(TRUE);
            o6->Release();
        }
    }

    std::wstring wUrl = url;
    HWND hHost = m_hOverlayHost;
    int chh = h - 44;

    auto envCallback = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [this, wUrl, hHost, w, chh]
        (HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env) return result;
            if (m_ovEnv) m_ovEnv->Release();
            m_ovEnv = env;
            m_ovEnv->AddRef();

            env->CreateCoreWebView2Controller(hHost,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, wUrl, w, chh]
                    (HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                        if (FAILED(result) || !controller) return result;
                        if (m_ovController) {
                            m_ovController->Close();
                            m_ovController->Release();
                        }
                        m_ovController = controller;
                        m_ovController->AddRef();

                        if (m_ovWebView) m_ovWebView->Release();
                        m_ovController->get_CoreWebView2(&m_ovWebView);
                        if (m_ovWebView) m_ovWebView->AddRef();

                        ICoreWebView2Controller2* c2 = nullptr;
                        if (SUCCEEDED(m_ovController->QueryInterface(
                            IID_ICoreWebView2Controller2, (void**)&c2)) && c2) {
                            COREWEBVIEW2_COLOR color = { 255, 255, 255, 255 };
                            c2->put_DefaultBackgroundColor(color);
                            c2->Release();
                        }

                        if (m_ovWebView) m_ovWebView->Navigate(wUrl.c_str());

                        RECT rc2 = { 0, 0, w, chh };
                        m_ovController->put_Bounds(rc2);
                        m_ovController->put_IsVisible(TRUE);
                        return S_OK;
                    }).Get());
            return S_OK;
        });

    HRESULT hrCreate = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, wUserData.c_str(), options, envCallback.Get());
    if (hrCreate == E_INVALIDARG) {
        hrCreate = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, wUserData.c_str(), nullptr, envCallback.Get());
    }
    if (options) options->Release();
}

void YXGuiPlugin::HideOverlayWebView(JSPlugin* plugin) {
    (void)plugin;
    if (m_hOverlayWnd && IsWindow(m_hOverlayWnd)) {
        DestroyWindow(m_hOverlayWnd);
        m_hOverlayWnd = nullptr;
    }
}

LRESULT CALLBACK YXGuiPlugin::OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    YXGuiPlugin* pThis = (YXGuiPlugin*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pThis = (YXGuiPlugin*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        return 0;
    }
    case WM_SIZE: {
        if (pThis) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            if (pThis->m_hOverlayHost) {
                MoveWindow(pThis->m_hOverlayHost, 0, 44,
                    rc.right, rc.bottom - 44, TRUE);
            }
            if (pThis->m_ovController) {
                RECT hostRc = { 0, 0, rc.right, rc.bottom - 44 };
                pThis->m_ovController->put_Bounds(hostRc);
            }
            HWND hClose = GetDlgItem(hWnd, 1001);
            if (hClose) {
                MoveWindow(hClose, rc.right - 120, 6, 108, 30, TRUE);
            }
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1001) {
            if (pThis) pThis->HideOverlayWebView(pThis->m_ovPlugin);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (pThis) pThis->HideOverlayWebView(pThis->m_ovPlugin);
        return 0;
    case WM_DESTROY:
        if (pThis) {
            if (pThis->m_ovWebView) {
                pThis->m_ovWebView->Release();
                pThis->m_ovWebView = nullptr;
            }
            if (pThis->m_ovController) {
                pThis->m_ovController->Close();
                pThis->m_ovController->Release();
                pThis->m_ovController = nullptr;
            }
            if (pThis->m_ovEnv) {
                pThis->m_ovEnv->Release();
                pThis->m_ovEnv = nullptr;
            }
            pThis->m_hOverlayWnd = nullptr;
            pThis->m_hOverlayHost = nullptr;
            pThis->m_ovPlugin = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT YXGuiPlugin::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_YXGUI_JS_WEBVIEW: {
        // JS 插件请求控制当前 YXGUI WebView2。
        // Navigate 在 UI 线程同步发起；Cookie / ExecuteScript 通过异步 COM 回调完成，
        // 最终复用 JSPlugin 已有的 Promise resolve 通道。
        auto* req = reinterpret_cast<JSWebViewRequest*>(lParam);
        if (!req) return 0;

        JSPlugin* plugin = req->plugin;
        const uint32_t seq = req->seq;
        const JSWebViewOp op = req->op;
        const bool newTab = req->newTab;
        const std::wstring url = req->url;
        const std::wstring script = req->script;
        delete req;

        auto findLivePlugin = [this, plugin]() -> JSPlugin* {
            if (!plugin) return nullptr;
            for (const auto& p : m_jsPlugins) {
                if (p && p.get() == plugin) return plugin;
            }
            return nullptr;
        };

        // 统一完成桥接 Promise，避免在 YXGuiPlugin 成员函数外直接引用
        // JSPlugin 内部定义的 HttpResult 类型。
        auto complete = [](JSPlugin* live, uint32_t requestSeq,
            int status, const std::string& body, const std::string& error) {
            if (!live) return;

            JSPlugin::HttpTaskDoneMsg msg{};
            msg.seq = requestSeq;
            msg.result.status = status;
            msg.result.body = body;
            msg.result.error = error;
            live->CallHttpDone(requestSeq, msg.result);
        };

        if (op == JSWebViewOp::Navigate) {
            if (url.empty()) return 0;

            if (newTab || !GetActiveWebView()) {
                AddNewTab(url);
            } else {
                ICoreWebView2* wv = GetActiveWebView();
                if (wv) {
                    HRESULT hr = wv->Navigate(url.c_str());
                    if (FAILED(hr)) {
                        OutputDebugStringW(L"[YXGui] JS WebView Navigate 失败\n");
                    }
                }
            }
            return 0;
        }

        ICoreWebView2* wv = GetActiveWebView();
        if (!wv) {
            complete(findLivePlugin(), seq, -1, "",
                "当前没有可用的 WebView2 标签页");
            return 0;
        }

        if (op == JSWebViewOp::GetCookies) {
            ICoreWebView2_2* wv2 = nullptr;
            HRESULT hrQI = wv->QueryInterface(IID_PPV_ARGS(&wv2));
            if (FAILED(hrQI) || !wv2) {
                complete(findLivePlugin(), seq, -1, "",
                    "当前 WebView2 不支持 CookieManager");
                return 0;
            }

            ICoreWebView2CookieManager* cookies = nullptr;
            HRESULT hrCM = wv2->get_CookieManager(&cookies);
            wv2->Release();

            if (FAILED(hrCM) || !cookies) {
                complete(findLivePlugin(), seq, -1, "",
                    "获取 WebView2 CookieManager 失败");
                return 0;
            }

            HRESULT hrCookies = cookies->GetCookies(
                url.c_str(),
                Callback<ICoreWebView2GetCookiesCompletedHandler>(
                    [this, plugin, seq](HRESULT resultCode,
                        ICoreWebView2CookieList* list) -> HRESULT {
                        JSPlugin* live = nullptr;
                        for (const auto& p : m_jsPlugins) {
                            if (p && p.get() == plugin) {
                                live = plugin;
                                break;
                            }
                        }
                        if (!live) return S_OK;

                        JSPlugin::HttpTaskDoneMsg msg{};
                        msg.seq = seq;

                        if (FAILED(resultCode) || !list) {
                            msg.result.status = -1;
                            msg.result.error = "读取 WebView2 Cookie 失败";
                            live->CallHttpDone(seq, msg.result);
                            return S_OK;
                        }

                        UINT count = 0;
                        list->get_Count(&count);

                        std::string cookieHeader;
                        for (UINT i = 0; i < count; ++i) {
                            ICoreWebView2Cookie* cookie = nullptr;
                            if (FAILED(list->GetValueAtIndex(i, &cookie)) || !cookie)
                                continue;

                            LPWSTR name = nullptr;
                            LPWSTR value = nullptr;
                            cookie->get_Name(&name);
                            cookie->get_Value(&value);

                            if (name && value) {
                                if (!cookieHeader.empty()) cookieHeader += "; ";
                                cookieHeader += WideToUtf8(name);
                                cookieHeader += "=";
                                cookieHeader += WideToUtf8(value);
                            }

                            if (name) CoTaskMemFree(name);
                            if (value) CoTaskMemFree(value);
                            cookie->Release();
                        }

                        msg.result.status = 200;
                        msg.result.body = cookieHeader;
                        live->CallHttpDone(seq, msg.result);
                        return S_OK;
                    }).Get());

            cookies->Release();

            if (FAILED(hrCookies)) {
                complete(findLivePlugin(), seq, -1, "",
                    "无法发起 WebView2 Cookie 请求");
            }
            return 0;
        }

        if (op == JSWebViewOp::ExecuteScript) {
            HRESULT hrScript = wv->ExecuteScript(
                script.c_str(),
                Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                    [this, plugin, seq](HRESULT resultCode,
                        LPCWSTR resultObjectAsJson) -> HRESULT {
                        JSPlugin* live = nullptr;
                        for (const auto& p : m_jsPlugins) {
                            if (p && p.get() == plugin) {
                                live = plugin;
                                break;
                            }
                        }
                        if (!live) return S_OK;

                        JSPlugin::HttpTaskDoneMsg msg{};
                        msg.seq = seq;

                        if (FAILED(resultCode)) {
                            msg.result.status = -1;
                            msg.result.error = "WebView2 ExecuteScript 失败";
                        } else {
                            msg.result.status = 200;
                            msg.result.body = resultObjectAsJson
                                ? WideToUtf8(resultObjectAsJson)
                                : "null";
                        }

                        live->CallHttpDone(seq, msg.result);
                        return S_OK;
                    }).Get());

            if (FAILED(hrScript)) {
                complete(findLivePlugin(), seq, -1, "",
                    "无法发起 WebView2 ExecuteScript 请求");
            }
            return 0;
        }

        complete(findLivePlugin(), seq, -1, "", "未知的 WebView2 操作");
        return 0;
    }
    case WM_YXGUI_HTTP_DONE: {
        // JS 插件异步 HTTP 完成（后台线程 PostMessage 投递）
        auto* done = (JSPlugin::HttpTaskDoneMsg*)lParam;
        if (done) {
            for (const auto& plugin : m_jsPlugins) {
                if (plugin) plugin->CallHttpDone(done->seq, done->result);
            }
            delete done;
        }
        return 0;
    }
    case WM_YXGUI_HTTP_STREAM: {
        // v9.2: 流式 chunk —— payload 由 JSPlugin.cpp 侧定义并分配；
        // YXGui 层不感知结构，只负责交由插件层认领。
        void* payload = (void*)lParam;
        if (!payload) return 0;
        if (!YXGui_ProcessStreamChunk(payload)) {
            // 无人认领（插件已卸载等），直接释放
            YXGui_FreeStreamChunk(payload);
        }
        return 0;
    }
    case WM_YXGUI_APPEND_OUTPUT: {
        std::string* pText = (std::string*)lParam;
        if (pText) { AppendConsoleText(*pText); delete pText; }
        return 0;
    }
    case WM_TIMER: {
        if (wParam == 1) {   // JS 插件动态壁纸轮播心跳
            JSPlugin* cur = CurrentJSPlugin();
            if (cur) {
                const PluginWallpaper* wp = cur->GetWallpaper();
                if (wp && wp->mode == 3 && !wp->cycleFiles.empty()) {
                    ULONGLONG now = GetTickCount64();
                    ULONGLONG elapsed = (now >= wp->lastTick)
                        ? (now - wp->lastTick) : 0;
                    if (elapsed >= (ULONGLONG)wp->cycleMs) {
                        cur->CycleNext();
                        if (m_hPluginHost) {
                            InvalidateRect(m_hPluginHost, NULL, TRUE);
                        }
                    }
                }
            }
        }
        return 0;
    }
    case WM_YXGUI_UPDATE_NAV:
        UpdateNavButtons();
        return 0;

    case WM_YXGUI_UPDATE_PROMPT: {
        std::wstring* pPath = (std::wstring*)lParam;
        if (pPath) {
            m_currentPath = *pPath;
            LayoutCdAndInput();
            delete pPath;
        }
        return 0;
    }
    case WM_YXGUI_CRX_DOWNLOADED: {
        std::wstring* pPath = (std::wstring*)lParam;
        if (pPath) {
            HandleDownloadedCrx(*pPath);
            delete pPath;
        }
        return 0;
    }
    case WM_YXGUI_ADD_EXTENSION_TO_PROFILE: {
        std::wstring* pPath = (std::wstring*)lParam;
        if (pPath) {
            AddExtensionToProfileOnUIThread(*pPath);
            delete pPath;
        }
        return 0;
    }
    case WM_YXGUI_CRX_INSTALL_DONE: {
        std::wstring* pMsg = (std::wstring*)lParam;
        if (pMsg) {
            MessageBoxW(hWnd, pMsg->c_str(), L"提示", MB_OK | MB_ICONINFORMATION);
            delete pMsg;
            if (m_hExtMgrWnd && IsWindow(m_hExtMgrWnd)) {
                DestroyWindow(m_hExtMgrWnd);
                ShowExtensionManager();
            }
        }
        return 0;
    }
    case WM_YXGUI_NEW_TAB: {
        std::wstring* pUrl = (std::wstring*)lParam;
        if (pUrl) {
            AddNewTab(*pUrl);
            delete pUrl;
        }
        return 0;
    }
    case WM_YXGUI_CONFIG_CHANGED:
        ApplyConfig();
        return 0;

    case WM_YXGUI_SWITCH_VIEW: {
        int idx = (int)wParam;
        SwitchView(idx);
        return 0;
    }

    case WM_YXGUI_REFRESH_SIDEBAR:
        RefreshSidebar();
        return 0;

    case WM_CTLCOLORSTATIC: {
        HWND hCtl = (HWND)lParam;
        if (hCtl == m_hPromptStatic) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(125, 188, 255));
            SetBkColor(hdc, YXStyle::CONSOLE_IN);
            static HBRUSH hBrPrompt = CreateSolidBrush(YXStyle::CONSOLE_IN);
            return (LRESULT)hBrPrompt;
        }
        break;
    }

    case WM_CTLCOLOREDIT: {
        HWND hCtl = (HWND)lParam;
        if (hCtl == m_hConsoleInput) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(233, 239, 248));
            SetBkColor(hdc, YXStyle::CONSOLE_IN);
            static HBRUSH hBrInput = CreateSolidBrush(YXStyle::CONSOLE_IN);
            return (LRESULT)hBrInput;
        }
        if (hCtl == m_hAddressBar) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(42, 52, 68));
            SetBkColor(hdc, RGB(255, 255, 255));
            static HBRUSH hBrAddress = CreateSolidBrush(RGB(255, 255, 255));
            return (LRESULT)hBrAddress;
        }
        break;
    }

    case WM_SIZE:
        OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 900;
        mmi->ptMinTrackSize.y = 600;
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
        if (lpdis->CtlType == ODT_BUTTON) {
            if (lpdis->CtlID >= IDC_AI_BASE &&
                lpdis->CtlID < IDC_AI_BASE + (UINT)m_config.aiButtons.size()) {
                DrawAiButton(lpdis, (int)lpdis->CtlID - IDC_AI_BASE);
                return TRUE;
            }
            if (lpdis->CtlID == IDC_TOPBAR_SETTINGS ||
                (lpdis->CtlID >= IDC_NAV_BACK && lpdis->CtlID <= IDC_NAV_FAV) ||
                (lpdis->CtlID >= IDC_NAV_ZOOM_IN && lpdis->CtlID <= IDC_NAV_EXTENSION)) {
                DrawToolbarButton(lpdis, (int)lpdis->CtlID);
                return TRUE;
            }
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_TOPBAR_SETTINGS && code == BN_CLICKED) {
            ShowSettings();
            return 0;
        }

        if (id == IDC_BROWSER_RESTORE && code == BN_CLICKED) {
            m_browserExpanded = false;
            RECT rc; GetClientRect(hWnd, &rc);
            OnSize(rc.right, rc.bottom);
            return 0;
        }

        if (code == BN_CLICKED && id >= IDC_AI_BASE &&
            id < IDC_AI_BASE + (int)m_config.aiButtons.size()) {
            NavigateTo(m_config.aiButtons[id - IDC_AI_BASE].url);
            return 0;
        }

        if (code == BN_CLICKED) {
            ICoreWebView2* wv = GetActiveWebView();
            if (id == IDC_NAV_BACK && wv) { wv->GoBack(); return 0; }
            if (id == IDC_NAV_FORWARD && wv) { wv->GoForward(); return 0; }
            if (id == IDC_NAV_REFRESH && wv) { wv->Reload(); return 0; }
            if (id == IDC_NAV_HOME && wv) { NavigateTo(L"https://www.bing.com"); return 0; }
            if (id == IDC_NAV_STAR) { ToggleFavorite(); return 0; }
            if (id == IDC_NAV_FAV) { ShowFavoritesMenu(); return 0; }

            if (id == IDC_NAV_ZOOM_IN) {
                ICoreWebView2Controller* ctrl = GetActiveController();
                if (ctrl) {
                    m_zoomFactor *= 1.25;
                    if (m_zoomFactor > 5.0) m_zoomFactor = 5.0;
                    ctrl->put_ZoomFactor(m_zoomFactor);
                }
                return 0;
            }
            if (id == IDC_NAV_ZOOM_OUT) {
                ICoreWebView2Controller* ctrl = GetActiveController();
                if (ctrl) {
                    m_zoomFactor /= 1.25;
                    if (m_zoomFactor < 0.25) m_zoomFactor = 0.25;
                    ctrl->put_ZoomFactor(m_zoomFactor);
                }
                return 0;
            }
            if (id == IDC_NAV_DOWNLOAD) { ShowDownloadList(); return 0; }
            if (id == IDC_NAV_EXTENSION) { ShowExtensionManager(); return 0; }
        }
        break;
    }

    case WM_CLOSE:
        OnWindowClose();
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_MOUSEWHEEL: {
        if (m_browserExpanded || m_aiButtons.empty()) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT rc; GetClientRect(hWnd, &rc);
        if (pt.x < rc.right - YXStyle::RIGHT_WIDTH) return 0;

        int contentTop = YXStyle::TOPBAR_H + 2;
        int contentH = rc.bottom - m_browserHeight - YXStyle::SPLITTER_H - contentTop;
        if (contentH < 100) contentH = 100;
        int viewportH = contentH - YXStyle::PANEL_HEADER_H;
        if (viewportH < 60) viewportH = 60;
        int total = (int)m_config.aiButtons.size() *
            (YXStyle::AI_BTN_SIZE + YXStyle::AI_BTN_GAP) + YXStyle::AI_BTN_GAP;
        int maxScroll = (total > viewportH) ? (total - viewportH) : 0;
        if (maxScroll <= 0) return 0;

        m_aiScrollY -= GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 60;
        if (m_aiScrollY < 0) m_aiScrollY = 0;
        if (m_aiScrollY > maxScroll) m_aiScrollY = maxScroll;
        OnSize(rc.right, rc.bottom);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            Gdiplus::SolidBrush base(Gdiplus::Color(255, 244, 247, 251));
            g.FillRectangle(&base, 0, 0, rc.right, rc.bottom);

            Gdiplus::LinearGradientBrush top(
                Gdiplus::Point(0, 0),
                Gdiplus::Point(rc.right, 0),
                Gdiplus::Color(255, 30, 57, 94),
                Gdiplus::Color(255, 62, 105, 170));
            g.FillRectangle(&top, 0, 0, rc.right, YXStyle::TOPBAR_H);

            Gdiplus::SolidBrush brandDot(Gdiplus::Color(255, 112, 181, 255));
            g.FillEllipse(&brandDot, SIDEBAR_WIDTH + 18, 11, 18, 18);

            Gdiplus::Font brandFont(L"Segoe UI", 15,
                Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush brandText(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::StringFormat nearFmt;
            nearFmt.SetAlignment(Gdiplus::StringAlignmentNear);
            nearFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(L"YXGui  ·  安全核心", -1, &brandFont,
                Gdiplus::RectF((Gdiplus::REAL)SIDEBAR_WIDTH + 46, 6, 280, 30),
                &nearFmt, &brandText);

            // 插件视图下不画主界面两侧面板
            bool pluginViewActive = (m_currentPluginIndex >= 0 &&
                m_currentPluginIndex < (int)m_jsPlugins.size());

            if (!m_browserExpanded && !pluginViewActive) {
                int contentLeft = SIDEBAR_WIDTH;
                int contentTop = YXStyle::TOPBAR_H + 4;
                int contentH = (std::max)(120,
                    (int)(rc.bottom - m_browserHeight - YXStyle::SPLITTER_H - contentTop));

                Gdiplus::SolidBrush leftPanel(Gdiplus::Color(255, 248, 250, 253));
                g.FillRectangle(&leftPanel, contentLeft, contentTop,
                    YXStyle::LEFT_WIDTH, contentH);

                Gdiplus::SolidBrush rightPanel(Gdiplus::Color(255, 239, 244, 251));
                g.FillRectangle(&rightPanel,
                    rc.right - YXStyle::RIGHT_WIDTH, contentTop,
                    YXStyle::RIGHT_WIDTH, contentH);

                Gdiplus::Pen separator(Gdiplus::Color(255, 218, 225, 235), 1.0f);
                g.DrawLine(&separator, contentLeft + YXStyle::LEFT_WIDTH, contentTop,
                           contentLeft + YXStyle::LEFT_WIDTH, contentTop + contentH);
                g.DrawLine(&separator,
                           rc.right - YXStyle::RIGHT_WIDTH, contentTop,
                           rc.right - YXStyle::RIGHT_WIDTH, contentTop + contentH);

                Gdiplus::Font sectionFont(L"Microsoft YaHei", 12,
                    Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush sectionText(Gdiplus::Color(255, 70, 84, 104));

                g.DrawString(L"使用说明", -1, &sectionFont,
                    Gdiplus::RectF((Gdiplus::REAL)contentLeft + 18,
                                   (Gdiplus::REAL)contentTop + 5, 180, 24),
                    &nearFmt, &sectionText);
                g.DrawString(L"命令控制台", -1, &sectionFont,
                    Gdiplus::RectF((Gdiplus::REAL)contentLeft + YXStyle::LEFT_WIDTH + 18,
                                   (Gdiplus::REAL)contentTop + 5, 180, 24),
                    &nearFmt, &sectionText);
                g.DrawString(L"AI 工具", -1, &sectionFont,
                    Gdiplus::RectF((Gdiplus::REAL)rc.right - YXStyle::RIGHT_WIDTH + 18,
                                   (Gdiplus::REAL)contentTop + 5, 120, 24),
                    &nearFmt, &sectionText);

                Gdiplus::Pen softLine(Gdiplus::Color(255, 229, 234, 241), 1.0f);
                g.DrawLine(&softLine, contentLeft + 16, contentTop + 31,
                           contentLeft + YXStyle::LEFT_WIDTH - 16, contentTop + 31);
                g.DrawLine(&softLine, contentLeft + YXStyle::LEFT_WIDTH + 16, contentTop + 31,
                           rc.right - YXStyle::RIGHT_WIDTH - 16, contentTop + 31);
                g.DrawLine(&softLine, rc.right - YXStyle::RIGHT_WIDTH + 16,
                           contentTop + 31, rc.right - 16, contentTop + 31);

                int splitterY = rc.bottom - m_browserHeight - YXStyle::SPLITTER_H;
                Gdiplus::SolidBrush splitBrush(Gdiplus::Color(255, 222, 229, 239));
                g.FillRectangle(&splitBrush, contentLeft, splitterY,
                                rc.right - contentLeft, YXStyle::SPLITTER_H);

                Gdiplus::SolidBrush gripBrush(Gdiplus::Color(255, 127, 145, 170));
                float cy = (float)splitterY + YXStyle::SPLITTER_H * 0.5f;
                float cx = (float)(rc.right + contentLeft) * 0.5f;
                for (int i = -3; i <= 3; ++i)
                    g.FillEllipse(&gripBrush,
                        (Gdiplus::REAL)(cx + i * 9.0f - 2.0f),
                        (Gdiplus::REAL)(cy - 2.0f), 4.0f, 4.0f);

                DrawAiScrollBar(memDC, rc);
            }
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        RECT rc;
        GetClientRect(m_hWnd, &rc);

        if (x < SIDEBAR_WIDTH) return 0;

        if (m_browserExpanded) {
            if (y < 40) {
                m_browserExpanded = false;
                OnSize(rc.right, rc.bottom);
            }
            return 0;
        }

        int contentTop = YXStyle::TOPBAR_H + 2;
        int contentH = rc.bottom - m_browserHeight - YXStyle::SPLITTER_H - contentTop;
        if (contentH < 100) contentH = 100;
        int total = (int)m_config.aiButtons.size() *
            (YXStyle::AI_BTN_SIZE + YXStyle::AI_BTN_GAP) + YXStyle::AI_BTN_GAP;
        if (total > contentH) {
            int trackX = rc.right - YXStyle::AI_SCROLLBAR_W - 4;
            int trackY = contentTop + YXStyle::PANEL_HEADER_H + 2;
            int viewportH = (std::max)(60, contentH - YXStyle::PANEL_HEADER_H);
            int trackH = viewportH - 4;
            int thumbH = (std::max)(30, trackH * viewportH / total);
            int maxScroll = total - viewportH;
            int thumbY = trackY + (trackH - thumbH) * m_aiScrollY / maxScroll;

            if (x >= trackX && x < trackX + YXStyle::AI_SCROLLBAR_W &&
                y >= trackY && y < trackY + trackH) {
                if (y >= thumbY && y < thumbY + thumbH) {
                    m_aiDraggingScroll = true;
                    m_aiScrollDragStartY = y;
                    m_aiScrollDragStartOffset = m_aiScrollY;
                    SetCapture(hWnd);
                }
                else {
                    if (y < thumbY) m_aiScrollY -= contentH;
                    else m_aiScrollY += contentH;
                    if (m_aiScrollY < 0) m_aiScrollY = 0;
                    if (m_aiScrollY > maxScroll) m_aiScrollY = maxScroll;
                    OnSize(rc.right, rc.bottom);
                }
                return 0;
            }
        }

        int splitterY = rc.bottom - m_browserHeight - YXStyle::SPLITTER_H;
        if (y >= splitterY && y < splitterY + YXStyle::SPLITTER_H) {
            m_draggingSplitter = true;
            m_dragStartY = y;
            m_dragStartH = m_browserHeight;
            SetCapture(hWnd);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (m_aiDraggingScroll) {
            int y = GET_Y_LPARAM(lParam);
            RECT rc;
            GetClientRect(m_hWnd, &rc);
            int contentTop = YXStyle::TOPBAR_H + 2;
            int contentH = rc.bottom - m_browserHeight - YXStyle::SPLITTER_H - contentTop;
            if (contentH < 100) contentH = 100;
            int total = (int)m_config.aiButtons.size() *
                (YXStyle::AI_BTN_SIZE + YXStyle::AI_BTN_GAP) + YXStyle::AI_BTN_GAP;
            int maxScroll = (std::max)(0, total - contentH);
            int viewportH = (std::max)(60, contentH - YXStyle::PANEL_HEADER_H);
            int trackH = viewportH - 4;
            int thumbH = (std::max)(30, trackH * viewportH / (total ? total : 1));
            int delta = y - m_aiScrollDragStartY;
            int scrollRange = trackH - thumbH;
            if (scrollRange > 0) {
                int scrollDelta = delta * maxScroll / scrollRange;
                m_aiScrollY = m_aiScrollDragStartOffset + scrollDelta;
                if (m_aiScrollY < 0) m_aiScrollY = 0;
                if (m_aiScrollY > maxScroll) m_aiScrollY = maxScroll;
                OnSize(rc.right, rc.bottom);
            }
            return 0;
        }
        if (m_draggingSplitter) {
            int y = GET_Y_LPARAM(lParam);
            int delta = m_dragStartY - y;
            int newH = m_dragStartH + delta;
            if (newH < YXStyle::BROWSER_MIN_H) newH = YXStyle::BROWSER_MIN_H;
            if (newH > YXStyle::BROWSER_MAX_H) newH = YXStyle::BROWSER_MAX_H;
            RECT rc;
            GetClientRect(m_hWnd, &rc);
            if (newH > rc.bottom - 200) newH = rc.bottom - 200;
            if (newH != m_browserHeight) {
                m_browserHeight = newH;
                OnSize(rc.right, rc.bottom);
            }
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (m_aiDraggingScroll) {
            m_aiDraggingScroll = false;
            ReleaseCapture();
            RECT rc;
            GetClientRect(m_hWnd, &rc);
            OnSize(rc.right, rc.bottom);
            return 0;
        }
        if (m_draggingSplitter) {
            m_draggingSplitter = false;
            ReleaseCapture();
            RECT rc;
            GetClientRect(m_hWnd, &rc);
            if (m_browserHeight >= rc.bottom - 250) {
                m_browserExpanded = true;
                m_browserHeight = 320;
                OnSize(rc.right, rc.bottom);
            }
            m_config.browserH = m_browserHeight;
            SaveConfig();
        }
        return 0;
    }

    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            RECT rc;
            GetClientRect(hWnd, &rc);
            if (pt.x < SIDEBAR_WIDTH) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
            int splitterY = rc.bottom - m_browserHeight - YXStyle::SPLITTER_H;
            if (!m_browserExpanded && pt.y >= splitterY &&
                pt.y < splitterY + YXStyle::SPLITTER_H) {
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
                return TRUE;
            }
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// OnSize
// ============================================================
void YXGuiPlugin::OnSize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (!m_hWnd) return;

    LayoutSidebar(width, height);

    int contentLeft = SIDEBAR_WIDTH;
    int contentWidth = width - contentLeft;

    bool nativePanelActive = (m_activeNativePanel >= 0 &&
        m_activeNativePanel < (int)m_nativePanels.size());
    bool pluginViewActive = (m_currentPluginIndex >= 0 &&
        m_currentPluginIndex < (int)m_jsPlugins.size()) || nativePanelActive;

    // ==========================================================
    // 插件视图：除侧边栏外全部隐藏，插件容器占满
    // ==========================================================
    if (pluginViewActive && !m_browserExpanded) {
        ShowWindow(m_hTabBar, SW_HIDE);
        ShowWindow(m_hTabContainer, SW_HIDE);
        ShowWindow(m_hAddressBar, SW_HIDE);
        ShowWindow(m_hHelpEdit, SW_HIDE);
        ShowWindow(m_hConsoleEdit, SW_HIDE);
        ShowWindow(m_hConsoleInput, SW_HIDE);
        ShowWindow(m_hPromptStatic, SW_HIDE);
        ShowWindow(m_hRestoreBtn, SW_HIDE);
        for (HWND h : m_aiButtons) {
            if (h && IsWindow(h)) ShowWindow(h, SW_HIDE);
        }
        for (const auto& item : m_config.toolbarOrder) {
            HWND h = GetToolbarHwndById(item.id);
            if (h) ShowWindow(h, SW_HIDE);
        }

        if (m_hTopbarSettingsBtn) {
            ShowWindow(m_hTopbarSettingsBtn, SW_SHOW);
            MoveWindow(m_hTopbarSettingsBtn, width - 128, 4, 120,
                YXStyle::TOPBAR_H - 8, TRUE);
        }

        if (m_hPluginHost && IsWindow(m_hPluginHost)) {
            int px = contentLeft;
            int py = YXStyle::TOPBAR_H + 2;
            int pw = contentWidth;
            int ph = height - YXStyle::TOPBAR_H - 2;
            if (pw < 200) pw = 200;
            if (ph < 200) ph = 200;

            MoveWindow(m_hPluginHost, px, py, pw, ph, TRUE);
            ShowWindow(m_hPluginHost, SW_SHOW);

            if (nativePanelActive && m_nativePanelHwnd && IsWindow(m_nativePanelHwnd)) {
                MoveWindow(m_nativePanelHwnd, 0, 0, pw, ph, TRUE);
            }

            if (m_currentPluginIndex >= 0 &&
                m_currentPluginIndex < (int)m_jsPlugins.size()) {
                m_jsPlugins[m_currentPluginIndex]->CallOnResize(pw, ph);
            }
        }

        InvalidateRect(m_hWnd, NULL, FALSE);
        return;
    }

    // ==========================================================
    // 主界面视图
    // ==========================================================
    if (m_hPluginHost) ShowWindow(m_hPluginHost, SW_HIDE);

    if (m_browserExpanded) {
        ShowWindow(m_hTopbarSettingsBtn, SW_HIDE);
        ShowWindow(m_hHelpEdit, SW_HIDE);
        ShowWindow(m_hConsoleEdit, SW_HIDE);
        ShowWindow(m_hPromptStatic, SW_HIDE);
        ShowWindow(m_hConsoleInput, SW_HIDE);
        for (HWND h : m_aiButtons) ShowWindow(h, SW_HIDE);

        ShowWindow(m_hTabBar, SW_SHOW);
        MoveWindow(m_hTabBar, contentLeft, 0, contentWidth, YXStyle::TAB_BAR_H, TRUE);

        int bx = contentLeft + 10;
        int btnW = 38;
        int toolbarY = YXStyle::TAB_BAR_H;

        for (const auto& item : m_config.toolbarOrder) {
            HWND h = GetToolbarHwndById(item.id);
            if (!h) continue;
            if (item.visible) {
                ShowWindow(h, SW_SHOW);
                MoveWindow(h, bx, toolbarY + 1, btnW, YXStyle::TOOLBAR_H - 2, TRUE);
                bx += btnW + 2;
            }
            else {
                ShowWindow(h, SW_HIDE);
            }
        }

        ShowWindow(m_hAddressBar, SW_SHOW);
        MoveWindow(m_hAddressBar, bx, toolbarY + 2,
            width - bx - 112, YXStyle::TOOLBAR_H - 10, TRUE);

        ShowWindow(m_hRestoreBtn, SW_SHOW);
        MoveWindow(m_hRestoreBtn, width - 104, toolbarY + 5, 98,
            YXStyle::TOOLBAR_H - 10, TRUE);

        ShowWindow(m_hTabContainer, SW_SHOW);
        MoveWindow(m_hTabContainer, contentLeft,
            YXStyle::TAB_BAR_H + YXStyle::TOOLBAR_H + 2,
            contentWidth,
            height - YXStyle::TAB_BAR_H - YXStyle::TOOLBAR_H - 2, TRUE);
        ResizeAllTabs();
    }
    else {
        ShowWindow(m_hTopbarSettingsBtn, SW_SHOW);
        MoveWindow(m_hTopbarSettingsBtn, width - 128, 4, 120,
            YXStyle::TOPBAR_H - 8, TRUE);

        int contentTop = YXStyle::TOPBAR_H + 2;
        int contentH = height - m_browserHeight - YXStyle::SPLITTER_H - contentTop;
        if (contentH < 150) contentH = 150;
        int middleW = contentWidth - YXStyle::LEFT_WIDTH - YXStyle::RIGHT_WIDTH;
        if (middleW < 200) middleW = 200;

        ShowWindow(m_hRestoreBtn, SW_HIDE);
        ShowWindow(m_hTabBar, SW_HIDE);

        ShowWindow(m_hHelpEdit, SW_SHOW);
        int helpY = contentTop + YXStyle::PANEL_HEADER_H;
        int helpH = contentH - YXStyle::PANEL_HEADER_H;
        if (helpH < 80) helpH = 80;
        MoveWindow(m_hHelpEdit, contentLeft, helpY,
            YXStyle::LEFT_WIDTH, helpH, TRUE);

        {
            int bottomRowH = 30;
            int consoleOutH = contentH - bottomRowH;
            if (consoleOutH < 50) consoleOutH = 50;

            int consoleY = contentTop + YXStyle::PANEL_HEADER_H;
            int consoleH = consoleOutH - YXStyle::PANEL_HEADER_H;
            if (consoleH < 50) consoleH = 50;

            ShowWindow(m_hConsoleEdit, SW_SHOW);
            MoveWindow(m_hConsoleEdit, contentLeft + YXStyle::LEFT_WIDTH, consoleY,
                middleW, consoleH, TRUE);

            ShowWindow(m_hPromptStatic, SW_SHOW);
            ShowWindow(m_hConsoleInput, SW_SHOW);
            LayoutCdAndInput();
        }

        {
            int contentWidth2 = YXStyle::RIGHT_WIDTH - YXStyle::AI_SCROLLBAR_W - 12;
            if (contentWidth2 < YXStyle::AI_BTN_SIZE)
                contentWidth2 = YXStyle::AI_BTN_SIZE;

            const int aiTop = contentTop + YXStyle::PANEL_HEADER_H;
            const int aiH = (std::max)(80, contentH - YXStyle::PANEL_HEADER_H);

            for (HWND h : m_aiButtons) ShowWindow(h, SW_HIDE);

            int y = aiTop + YXStyle::AI_BTN_GAP - m_aiScrollY;
            for (size_t i = 0; i < m_aiButtons.size(); i++) {
                int x = width - YXStyle::RIGHT_WIDTH +
                    (contentWidth2 - YXStyle::AI_BTN_SIZE) / 2 + 2;
                if (y + YXStyle::AI_BTN_SIZE > aiTop && y < aiTop + aiH) {
                    MoveWindow(m_aiButtons[i], x, y,
                        YXStyle::AI_BTN_SIZE, YXStyle::AI_BTN_SIZE, TRUE);
                    ShowWindow(m_aiButtons[i], SW_SHOW);
                }
                y += YXStyle::AI_BTN_SIZE + YXStyle::AI_BTN_GAP;
            }
        }

        int bx = contentLeft + 10;
        int btnW = 38;
        int toolbarY = contentTop + contentH + YXStyle::SPLITTER_H + 2;

        for (const auto& item : m_config.toolbarOrder) {
            HWND h = GetToolbarHwndById(item.id);
            if (!h) continue;
            if (item.visible) {
                ShowWindow(h, SW_SHOW);
                MoveWindow(h, bx, toolbarY + 2, btnW, YXStyle::TOOLBAR_H - 8, TRUE);
                bx += btnW + 2;
            }
            else {
                ShowWindow(h, SW_HIDE);
            }
        }

        ShowWindow(m_hAddressBar, SW_SHOW);
        MoveWindow(m_hAddressBar, bx, toolbarY + 5,
            width - bx - 12, YXStyle::TOOLBAR_H - 10, TRUE);

        ShowWindow(m_hTabContainer, SW_SHOW);
        MoveWindow(m_hTabContainer, contentLeft,
            toolbarY + YXStyle::TOOLBAR_H + 2,
            contentWidth, m_browserHeight - YXStyle::TOOLBAR_H - 2, TRUE);

        ResizeAllTabs();
    }

    InvalidateRect(m_hWnd, NULL, FALSE);
}

// ============================================================
// 导出
// ============================================================
static YXGuiPlugin* g_pluginInstance = nullptr;

PLUGIN_API IPlugin* CreatePlugin() {
    if (!g_pluginInstance) g_pluginInstance = new YXGuiPlugin();
    return g_pluginInstance;
}

PLUGIN_API void DestroyPlugin(IPlugin* plugin) {
    if (plugin == g_pluginInstance) {
        delete g_pluginInstance;
        g_pluginInstance = nullptr;
    }
    else {
        delete plugin;
    }
}

PLUGIN_API IGuiProvider* CreateGuiProvider() {
    if (!g_pluginInstance) g_pluginInstance = new YXGuiPlugin();
    return static_cast<IGuiProvider*>(g_pluginInstance);
}

PLUGIN_API void DestroyGuiProvider(IGuiProvider* provider) {
    (void)provider;
}