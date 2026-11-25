// src/PermissionManager.cpp
// ============================================================================
// winsock2.h 保护：必须在 windows.h 之前包含 winsock2.h，
// 否则 winsock.h 会被 windows.h 先包含，导致 WSA* / sockaddr 等重定义。
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "PermissionManager.h"
#include <fstream>
#include <sstream>
#include <shlobj.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

static std::wstring GetGuiDir() {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring dir(exePath);
    size_t p = dir.find_last_of(L"\\/");
    if (p != std::wstring::npos) dir = dir.substr(0, p);
    dir += L"\\Cache\\gui";
    SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    return dir;
}

PermissionManager& PermissionManager::Instance() {
    static PermissionManager inst;
    return inst;
}

void PermissionManager::SetStorePath(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_storePath = path;
}

void PermissionManager::Load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_granted.clear();
    if (m_storePath.empty()) m_storePath = GetGuiDir() + L"\\cf.m";

    std::ifstream f(m_storePath);
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::string k, v;
        size_t sep = line.find('=');
        if (sep == std::string::npos) continue;
        k = line.substr(0, sep);
        v = line.substr(sep + 1);

        auto trimA = [](std::string& s) {
            size_t b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) { s.clear(); return; }
            size_t e = s.find_last_not_of(" \t\r\n");
            s = s.substr(b, e - b + 1);
            };
        trimA(k); trimA(v);

        int lenK = MultiByteToWideChar(CP_UTF8, 0, k.c_str(), (int)k.size(), NULL, 0);
        std::wstring wk(lenK, 0);
        MultiByteToWideChar(CP_UTF8, 0, k.c_str(), (int)k.size(), &wk[0], lenK);

        m_granted[wk] = (v == "1");
    }
}

void PermissionManager::Save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_storePath.empty()) m_storePath = GetGuiDir() + L"\\cf.m";

    std::ofstream f(m_storePath, std::ios::trunc);
    if (!f.is_open()) return;

    f << "# YXGui Plugin Permission Store\n";
    f << "# 格式：<pluginId>=1 表示永久授权，=0 表示未授权\n";
    f << "# 该文件由 YXGui 插件自动管理，删除后所有插件需重新授权\n\n";

    for (const auto& kv : m_granted) {
        std::string key;
        int len = WideCharToMultiByte(CP_UTF8, 0, kv.first.c_str(),
            (int)kv.first.size(), NULL, 0, NULL, NULL);
        key.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, kv.first.c_str(),
            (int)kv.first.size(), &key[0], len, NULL, NULL);

        f << key << "=" << (kv.second ? "1" : "0") << "\n";
    }
}

bool PermissionManager::IsPermanentlyGranted(const std::wstring& pluginId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_granted.find(pluginId);
    return it != m_granted.end() && it->second;
}

void PermissionManager::SetPermanentlyGranted(const std::wstring& pluginId, bool granted) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_granted[pluginId] = granted;
    }
    Save();
}

void PermissionManager::Revoke(const std::wstring& pluginId) {
    SetPermanentlyGranted(pluginId, false);
}

std::vector<std::wstring> PermissionManager::ListGranted() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::wstring> result;
    for (const auto& kv : m_granted) {
        if (kv.second) result.push_back(kv.first);
    }
    return result;
}

// ============================================================
// 权限弹窗
// ============================================================
struct PermissionDlgData {
    std::wstring pluginId;
    std::wstring pluginName;
    std::wstring actionDesc;
    int result = 0;
};

static LRESULT CALLBACK PermissionDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PermissionDlgData* p = (PermissionDlgData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        p = (PermissionDlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)p);
        if (!p) return -1;

        HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

        std::wstring title = L"插件「" + p->pluginName + L"」请求调用 Light 权限";
        HWND hTitle = CreateWindowExW(0, L"STATIC", title.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 18, 540, 26, hWnd, NULL, NULL, NULL);
        HFONT hTitleFont = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

        std::wstring body = L"动作：" + p->actionDesc +
            L"\r\n\r\n允许后该插件可通过 Light 主程序执行相应操作。";
        HWND hBody = CreateWindowExW(0, L"STATIC", body.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 54, 540, 100, hWnd, NULL, NULL, NULL);
        SendMessageW(hBody, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hCheck = CreateWindowExW(0, L"BUTTON",
            L"以后不再询问该插件的任何权限",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            20, 164, 540, 24, hWnd, (HMENU)1001, NULL, NULL);
        SendMessageW(hCheck, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hAllow = CreateWindowExW(0, L"BUTTON", L"允许",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            300, 200, 120, 34, hWnd, (HMENU)IDOK, NULL, NULL);
        SendMessageW(hAllow, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hDeny = CreateWindowExW(0, L"BUTTON", L"拒绝",
            WS_CHILD | WS_VISIBLE,
            440, 200, 120, 34, hWnd, (HMENU)IDCANCEL, NULL, NULL);
        SendMessageW(hDeny, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetFocus(hAllow);
        return 0;
    }
    case WM_COMMAND:
        if (!p) break;
        if (LOWORD(wParam) == IDOK) {
            bool remember = (SendDlgItemMessageW(hWnd, 1001, BM_GETCHECK, 0, 0) == BST_CHECKED);
            p->result = remember ? 2 : 1;
            DestroyWindow(hWnd);
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            p->result = 0;
            DestroyWindow(hWnd);
        }
        return 0;
    case WM_CLOSE:
        if (p) p->result = 0;
        DestroyWindow(hWnd);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int PermissionManager::Prompt(HWND owner,
    const std::wstring& pluginId,
    const std::wstring& pluginName,
    const std::wstring& actionDesc) {

    if (IsPermanentlyGranted(pluginId)) return 2;

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = PermissionDlgProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(248, 250, 253));
        wc.lpszClassName = L"YXGuiPermissionDlg";
        RegisterClassExW(&wc);
        reg = true;
    }

    PermissionDlgData data;
    data.pluginId = pluginId;
    data.pluginName = pluginName;
    data.actionDesc = actionDesc;

    RECT rcP = { 0, 0, 800, 600 };
    if (owner) GetWindowRect(owner, &rcP);
    int w = 600, h = 280;
    int x = rcP.left + ((rcP.right - rcP.left) - w) / 2;
    int y = rcP.top + ((rcP.bottom - rcP.top) - h) / 2;

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"YXGuiPermissionDlg", L"Light 权限请求",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, owner, NULL, GetModuleHandle(NULL), &data);
    if (!hDlg) return 0;

    ShowWindow(hDlg, SW_SHOW);
    SetForegroundWindow(hDlg);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (data.result == 2) {
        SetPermanentlyGranted(pluginId, true);
    }
    return data.result;
}
