// StartupDialog.cpp
// 启动弹窗：创建项目 / 打开最近项目
#include "StartupDialog.h"
#include "core/util.h"
#include "core/ProjectStore.h"

namespace vcb {

namespace {
enum { ID_NAME = 2001, ID_LANG = 2002, ID_LIST = 2003, ID_CREATE = 2004, ID_OPEN = 2005, ID_CANCEL = 2006 };
const wchar_t* kClass = L"VCBlocksStartup";

struct Ctx {
    std::vector<std::string> recent;
    StartupResult result;
    bool done = false;
    HWND edit = nullptr;
    HWND combo = nullptr;
    HWND list = nullptr;
    HWND btnOpen = nullptr;
};
}

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Ctx* c = (Ctx*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_CREATE) {
        c = (Ctx*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)c);
        HINSTANCE inst = ((CREATESTRUCTW*)l)->hInstance;
        c->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"MyProject",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 14, 14, 260, 26, h, (HMENU)ID_NAME, inst, nullptr);
        SendMessageW(c->edit, EM_SETSEL, 0, -1);
        c->combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            14, 54, 260, 300, h, (HMENU)ID_LANG, inst, nullptr);
        SendMessageW(c->combo, CB_ADDSTRING, 0, (LPARAM)L"C (内置编译器)");
        SendMessageW(c->combo, CB_ADDSTRING, 0, (LPARAM)L"C++ (MSVC)");
        SendMessageW(c->combo, CB_ADDSTRING, 0, (LPARAM)L"Java (JDK)");
        SendMessageW(c->combo, CB_SETCURSEL, 0, 0);

        // 最近项目（若有）
        if (!c->recent.empty()) {
            CreateWindowExW(0, L"STATIC", L"最近项目:", WS_CHILD | WS_VISIBLE, 14, 92, 260, 18, h, (HMENU)0, inst, nullptr);
            c->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                14, 112, 260, 90, h, (HMENU)ID_LIST, inst, nullptr);
            SendMessageW(c->list, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            for (const auto& p : c->recent) {
                SendMessageW(c->list, LB_ADDSTRING, 0, (LPARAM)Utf8ToWide(p).c_str());
            }
        }

        c->btnOpen = CreateWindowExW(0, L"BUTTON", L"打开所选", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            14, 214, 88, 28, h, (HMENU)ID_OPEN, inst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"创建项目", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            112, 214, 88, 28, h, (HMENU)ID_CREATE, inst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
            210, 214, 64, 28, h, (HMENU)ID_CANCEL, inst, nullptr);
        SendMessageW(c->btnOpen, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        for (int id : { ID_CREATE, ID_CANCEL }) {
            SendMessageW(GetDlgItem(h, id), WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        }
        if (c->recent.empty()) EnableWindow(c->btnOpen, FALSE);
        SetFocus(c->edit);
        return 0;
    }
    if (!c) return DefWindowProcW(h, m, w, l);
    if (m == WM_COMMAND) {
        int id = LOWORD(w);
        if (id == ID_CANCEL || id == IDOK) { c->result.ok = false; DestroyWindow(h); return 0; }
        if (id == ID_CREATE) {
            wchar_t buf[512]; GetWindowTextW(c->edit, buf, 512);
            c->result.projectName = WideToUtf8(buf);
            int l = (int)SendMessageW(c->combo, CB_GETCURSEL, 0, 0);
            c->result.lang = (l == 1) ? TargetLang::Cpp : (l == 2) ? TargetLang::Java : TargetLang::C;
            c->result.createNew = true;
            c->result.ok = true;
            DestroyWindow(h);
            return 0;
        }
        if (id == ID_OPEN) {
            int sel = (int)SendMessageW(c->list, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)c->recent.size()) {
                c->result.projectName = c->recent[sel];
                c->result.createNew = false;
                c->result.ok = true;
                DestroyWindow(h);
            }
            return 0;
        }
    }
    if (m == WM_CLOSE) { c->result.ok = false; DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) { c->done = true; return 0; }
    return DefWindowProcW(h, m, w, l);
}

StartupResult StartupDialog::Show(HINSTANCE inst, HWND owner, const std::vector<std::string>& recent) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = inst;
        wc.lpszClassName = kClass; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        RegisterClassW(&wc); reg = true;
    }
    Ctx ctx; ctx.recent = recent;
    ctx.result.location = ProjectStore::RootDir();

    int w = 292, ht = recent.empty() ? 286 : 288;
    HWND h = CreateWindowExW(0, kClass, L"VCBlocks - 项目", WS_CAPTION | WS_SYSMENU | WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, w, ht, owner, nullptr, inst, &ctx);
    if (!h) { ctx.result.ok = false; return ctx.result; }
    if (owner) {
        RECT pr, wr; GetWindowRect(owner, &pr); GetWindowRect(h, &wr);
        SetWindowPos(h, nullptr, (pr.left + pr.right) / 2 - (wr.right - wr.left) / 2,
            (pr.top + pr.bottom) / 2 - (wr.bottom - wr.top) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(h, SW_SHOW);
    EnableWindow(owner, FALSE);
    MSG msg;
    while (!ctx.done && GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    EnableWindow(owner, TRUE);
    if (owner) SetForegroundWindow(owner);
    return ctx.result;
}

} // namespace vcb
