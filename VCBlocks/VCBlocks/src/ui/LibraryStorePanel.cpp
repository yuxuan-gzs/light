// LibraryStorePanel.cpp
// 库商城面板：模态窗口，展示内置标准库（中文翻译）+ 本地库 + 远程库，支持安装/下载。
#include "LibraryStorePanel.h"
#include "core/util.h"
#include "core/ProjectStore.h"

namespace vcb {

namespace {
enum { ID_LIST = 2001, ID_DOWNLOAD = 2002, ID_REFRESH = 2003, ID_CLOSE = 2004, ID_STATUS = 2005 };
const wchar_t* kClassName = L"VCBlocksLibraryStore";
}

void LibraryStorePanel::ShowModal(HINSTANCE inst, HWND owner) {
    m_indexUrl = LibraryStore::DefaultIndexUrl();

    WNDCLASSW wc{};
    wc.lpfnWndProc = LibraryStorePanel::WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(0, kClassName, L"库商城", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 440, owner, nullptr, inst, this);
    if (!m_hwnd) return;

    m_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 100, 100, m_hwnd, (HMENU)ID_LIST, inst, nullptr);
    SendMessageW(m_list, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    auto mkBtn = [&](const wchar_t* text, int id) {
        return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 90, 28, m_hwnd, (HMENU)id, inst, nullptr);
    };
    HWND btnInstall = mkBtn(L"安装/下载", ID_DOWNLOAD);
    HWND btnRefresh = mkBtn(L"刷新", ID_REFRESH);
    HWND btnClose = mkBtn(L"关闭", ID_CLOSE);
    SendMessageW(btnInstall, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessageW(btnRefresh, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessageW(btnClose, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    m_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 20, m_hwnd, (HMENU)ID_STATUS, inst, nullptr);
    SendMessageW(m_status, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    Refresh();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsWindow(m_hwnd)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT CALLBACK LibraryStorePanel::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    LibraryStorePanel* self;
    if (m == WM_NCCREATE) {
        self = (LibraryStorePanel*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (LibraryStorePanel*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (self) return self->HandleMessage(h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

LRESULT LibraryStorePanel::HandleMessage(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE: Layout(LOWORD(l), HIWORD(l)); return 0;
    case WM_COMMAND:
        if (HIWORD(w) == BN_CLICKED && LOWORD(w) == ID_CLOSE) { DestroyWindow(h); return 0; }
        if (HIWORD(w) == BN_CLICKED && LOWORD(w) == ID_REFRESH) { Refresh(); return 0; }
        if (HIWORD(w) == BN_CLICKED && LOWORD(w) == ID_DOWNLOAD) { DownloadSelected(); return 0; }
        if (HIWORD(w) == LBN_DBLCLK && LOWORD(w) == ID_LIST) { DownloadSelected(); return 0; }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void LibraryStorePanel::Layout(int w, int h) {
    int margin = 12;
    int btnW = 100, btnH = 30, gap = 8;
    int btnY = h - margin - btnH;
    MoveWindow(m_list, margin, margin, w - margin * 2, btnY - margin * 2, TRUE);
    MoveWindow(GetDlgItem(m_hwnd, ID_DOWNLOAD), w - margin - btnW * 3 - gap * 2, btnY, btnW, btnH, TRUE);
    MoveWindow(GetDlgItem(m_hwnd, ID_REFRESH), w - margin - btnW * 2 - gap, btnY, btnW, btnH, TRUE);
    MoveWindow(GetDlgItem(m_hwnd, ID_CLOSE), w - margin - btnW, btnY, btnW, btnH, TRUE);
    MoveWindow(m_status, margin, h - margin - btnH - 24, w - margin * 2, 20, TRUE);
}

void LibraryStorePanel::SetStatus(const std::wstring& s) {
    if (m_status) SetWindowTextW(m_status, s.c_str());
}

void LibraryStorePanel::Refresh() {
    SendMessageW(m_list, LB_RESETCONTENT, 0, 0);
    m_libs.clear();

    // 1. 内置标准库（中文翻译，不依赖网络）
    auto builtins = LibraryStore::BuiltinLibraries();
    for (auto& lib : builtins) m_libs.push_back(lib);

    // 2. 远程库（可选，拉取失败不阻塞）
    std::vector<StoreLibrary> remotes;
    std::string rerr;
    bool remoteOk = LibraryStore::FetchList(m_indexUrl, remotes, rerr);
    for (auto& lib : remotes) m_libs.push_back(lib);

    for (const auto& lib : m_libs) {
        std::wstring tag = lib.builtin ? L"[内置]" : L"[远程]";
        std::wstring line = Utf8ToWide(lib.lang) + L"  " + tag + L"  " + Utf8ToWide(lib.name);
        if (!lib.description.empty()) line += L"  —  " + Utf8ToWide(lib.description);
        SendMessageW(m_list, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }

    std::wstring status = L"共 " + std::to_wstring(m_libs.size()) + L" 个库（内置 " +
                          std::to_wstring(builtins.size()) + L" 个，双击安装）";
    if (!remoteOk) status += L"  |  远程仓库不可用: " + Utf8ToWide(rerr);
    SetStatus(status);
}

void LibraryStorePanel::DownloadSelected() {
    int idx = (int)SendMessageW(m_list, LB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= (int)m_libs.size()) {
        SetStatus(L"请先选择一个库");
        return;
    }
    const StoreLibrary& lib = m_libs[idx];
    SetStatus(L"正在安装 " + Utf8ToWide(lib.name) + L" ...");
    std::string err;
    bool ok = lib.builtin ? LibraryStore::InstallBuiltin(lib, err) : LibraryStore::Download(lib, err);
    if (!ok) {
        SetStatus(L"失败: " + Utf8ToWide(err));
        MessageBoxW(m_hwnd, Utf8ToWide(err).c_str(), L"安装失败", MB_ICONERROR);
        return;
    }
    std::wstring dir = ProjectStore::LibsDirById(lib.lang);
    SetStatus(L"已安装到 " + dir);
    MessageBoxW(m_hwnd, (L"库「" + Utf8ToWide(lib.name) + L"」已安装到：\n\n" + dir).c_str(),
        L"完成", MB_OK | MB_ICONINFORMATION);
}

} // namespace vcb
