// Ui.cpp — 自绘主窗口：窗口过程 / 输入事件 / 异步事件 / 页面调度
#include "Ui.h"
#include "UiNav.h"
#include "Theme.h"
#include "Background.h"
#include "LauncherApp.h"
#include "Json.h"
#include "Skin.h"
#include "SkinNet.h"

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <map>
#include <thread>

namespace ui {

namespace fs = std::filesystem;

static Ui* g_ui = nullptr;
static WNDPROC g_editOldProc = nullptr;
static bool g_committing = false;

static void AsyncEvent(launcher::Ev type, const std::string& data) {
    if (!g_ui) return;
    g_ui->OnAsyncEvent((int)type, data);
}

Ui::~Ui() {
    if (g_ui == this) g_ui = nullptr;
    if (m_sink) { m_sink->alive.store(false); m_sink->hwnd = nullptr; }
    if (m_editOverlay && IsWindow(m_editOverlay)) DestroyWindow(m_editOverlay);
    m_editOverlay = nullptr;
    if (m_hwnd && IsWindow(m_hwnd)) {
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// ============================================================
// 原生编辑框覆盖（支持中文输入法）
// ============================================================
static LRESULT CALLBACK OverlayEditProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    Ui* self = (Ui*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (self && !g_committing) {
        if (msg == WM_KEYDOWN) {
            if (w == VK_RETURN || w == VK_TAB) { self->CommitEdit(); return 0; }
            if (w == VK_ESCAPE) {
                g_committing = true;
                ShowWindow(h, SW_HIDE);
                g_committing = false;
                SetFocus(GetParent(h));
                return 0;
            }
        }
        if (msg == WM_KILLFOCUS) {
            g_committing = true;
            if (IsWindowVisible(h)) self->CommitEdit();
            g_committing = false;
        }
    }
    return CallWindowProcW(g_editOldProc, h, msg, w, l);
}

// ============================================================
// 窗口过程
// ============================================================
LRESULT CALLBACK Ui::WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    Ui* self = (Ui*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        self = (Ui*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->m_hwnd = h;
    }
    if (!self) return DefWindowProcW(h, msg, w, l);

    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        self->Paint(hdc, ps.rcPaint);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        self->OnMouseMove((int)(short)LOWORD(l), (int)(short)HIWORD(l));
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(h);
        self->OnLButtonDown((int)(short)LOWORD(l), (int)(short)HIWORD(l));
        return 0;
    case WM_LBUTTONUP:
        self->OnLButtonUp((int)(short)LOWORD(l), (int)(short)HIWORD(l));
        return 0;
    case WM_CHAR:
        self->OnChar((wchar_t)w);
        return 0;
    case WM_KEYDOWN:
        self->OnKeyDown(w);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt{ (int)(short)LOWORD(l), (int)(short)HIWORD(l) };
        ScreenToClient(h, &pt);
        self->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(w), pt.x, pt.y);
        return 0;
    }
    case WM_TIMER:
        if (w == 1) self->OnTimerTick();
        return 0;
    case WM_APP_EVENT:
        self->OnTimerTick();
        return 0;
    case WM_SIZE: {
        int nw = LOWORD(l), nh = HIWORD(l);
        if (nw != self->m_width || nh != self->m_height) {
            self->m_width = nw;
            self->m_height = nh;
            self->Rebuild();
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, 1);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

// ============================================================
// 创建
// ============================================================
bool Ui::Create(HINSTANCE hInstance, HWND parent) {
    m_hInstance = hInstance;
    g_ui = this;
    mlc::EnsureDirs();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Ui::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"YXG_Launcher_MainWnd";
    RegisterClassExW(&wc);

    RECT rc{ 0, 0, 1180, 760 };
    if (parent) GetClientRect(parent, &rc);
    m_width = rc.right - rc.left;
    m_height = rc.bottom - rc.top;
    if (m_width < 320) m_width = 1180;
    if (m_height < 240) m_height = 760;

    m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"我的世界启动器",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, m_width, m_height, parent, nullptr, hInstance, this);
    if (!m_hwnd) return false;

    // 原生编辑框（隐藏，聚焦输入框时浮出）——支持中文输入法
    m_editOverlay = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | ES_AUTOHSCROLL | ES_LEFT,
        0, 0, 10, 10, m_hwnd, (HMENU)1001, hInstance, nullptr);
    if (m_editOverlay) {
        SendMessageW(m_editOverlay, WM_SETFONT,
                     (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SetWindowLongPtrW(m_editOverlay, GWLP_USERDATA, (LONG_PTR)this);
        g_editOldProc = (WNDPROC)SetWindowLongPtrW(m_editOverlay, GWLP_WNDPROC,
                                                   (LONG_PTR)OverlayEditProc);
    }

    if (m_sink) m_sink->hwnd = m_hwnd;
    SetTimer(m_hwnd, 1, 100, nullptr);

    // 首次进入自动加载版本清单（下载页需要）
    if (!launcher::App::Instance().manifestLoaded() && !m_manifestRequested) {
        m_manifestRequested = true;
        auto sink = m_sink;
        std::thread([sink]() {
            std::string err;
            bool ok = launcher::App::Instance().refreshRemote(err);
            sink->post(ok ? (int)launcher::Ev::InstallDone
                          : (int)launcher::Ev::InstallError,
                       ok ? std::string("__manifest__") : err);
        }).detach();
    }

    Rebuild();
    launcher::App::Instance().setEventCallback(AsyncEvent);
    return true;
}

// ============================================================
// 页面
// ============================================================
void Ui::SetPage(Page p) {
    if (m_page != p) m_scrollY = 0;
    m_page = p;
    m_showOfflineDlg = false;
    m_showLaunchDlg = false;
    m_skinDrag = false;
    if (p == Versions) {
        mlc::Instance* in = CurInst();
        std::wstring iid = in ? mlc::Utf8ToW(in->id) : L"";
        if (iid != m_cacheInstId) {
            m_modsLoaded = m_packsLoaded = m_worldsLoaded = m_schemsLoaded = m_shadersLoaded = false;
            m_cacheInstId = iid;
        }
    }
    if (p == Download) {
        m_dlFolders.clear();
        // 从「模组/资源包页 → 去下载」跳进来时，也像 HMCL 一样先显示一页全部
        if (m_dlCat == 1 && m_addonHits.empty() && !m_dlSearching) {
            StartAddonSearch(0);
        } else if (m_dlCat >= 2 && m_dlCat <= 3 && m_hits.empty() && !m_dlSearching) {
            StartDownloadSearch();
        }
    }
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Ui::Rebuild() {
    // 每次重建前先和磁盘对账（照 HMCL：实例列表就是 versions\* 的扫描结果）。
    // 这样在资源管理器里手动删掉版本目录后，「已安装」和列表项会立刻消失。
    mlc::Store::I().SyncWithDisk();

    // 重建前记住各滚动控件的偏移（按 id），否则点一下列表就跳回顶部
    std::map<std::wstring, int> scrollKeep;
    for (auto& w : m_widgets) {
        if (w.id.empty()) continue;
        if (w.type == Widget::Grid || w.type == Widget::Rows) scrollKeep[w.id] = w.gscroll;
        else if (w.type == Widget::List) scrollKeep[w.id] = w.scroll;
    }

    if (m_editOverlay && IsWindowVisible(m_editOverlay)) {
        g_committing = true;
        ShowWindow(m_editOverlay, SW_HIDE);
        g_committing = false;
        m_editTargetId.clear();
    }
    m_widgets.clear();
    m_widgets.reserve(160);
    switch (m_page) {
        case Home: BuildHome(); break;
        case Versions: BuildVersions(); break;
        case GameSelect: BuildGameSelect(); break;
        case Download: BuildDownload(); break;
        case Account: BuildAccount(); break;
        case Settings: BuildSettings(); break;
    }

    for (auto& w : m_widgets) {
        auto it = scrollKeep.find(w.id);
        if (it == scrollKeep.end()) continue;
        if (w.type == Widget::Grid || w.type == Widget::Rows) w.gscroll = it->second;
        else if (w.type == Widget::List) w.scroll = it->second;
    }
}

Widget* Ui::Find(const std::wstring& id) {
    for (auto& w : m_widgets) if (w.id == id) return &w;
    return nullptr;
}

mlc::Instance* Ui::CurInst() {
    return mlc::Store::I().Current();
}

std::wstring Ui::CurVerIdW() {
    mlc::Instance* in = CurInst();
    return in ? mlc::Utf8ToW(in->versionId) : L"";
}

// ============================================================
// 侧边栏（导航表见 UiNav.h）
// ============================================================
using nav::Entry;
using nav::Kind;
using nav::kCount;
using nav::kItems;
using nav::kNavLeft;
using nav::kNavRight;
using nav::kSidebarW;

// ============================================================
// 鼠标事件
// ============================================================
static bool inRect(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

void Ui::OnMouseMove(int x, int y) {
    bool dirty = false;
    // 拖动皮肤预览：水平转 yaw，垂直转 pitch
    if (m_skinDrag) {
        m_skinYaw += (float)(x - m_dragX) * 0.7f;
        while (m_skinYaw > 360.0f) m_skinYaw -= 360.0f;
        while (m_skinYaw < -360.0f) m_skinYaw += 360.0f;
        m_skinPitch = std::clamp(m_skinPitch - (float)(y - m_dragY) * 0.5f, -55.0f, 55.0f);
        m_dragX = x;
        m_dragY = y;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    for (auto& w : m_widgets) {
        bool h = inRect(w.rc, x, y);
        if (h != w.hover) { w.hover = h; dirty = true; }
        if (w.type == Widget::Grid) {
            int idx = HitGridIndex(w, x, y);
            if (idx != w.hotIdx) { w.hotIdx = idx; dirty = true; }
        }
        if (w.type == Widget::Rows) {
            int idx = HitRowIndex(w, x, y);
            if (idx != w.hotIdx) { w.hotIdx = idx; dirty = true; }
        }
        if (w.type == Widget::List) {
            int rowH = 34;
            int idx = (int)(w.scroll + (y - w.rc.top) / rowH);
            if (inRect(w.rc, x, y) && idx >= 0 && idx < (int)w.listItems.size()) {
                if (w.hot != idx) { w.hot = idx; dirty = true; }
            } else if (w.hot != -1) { w.hot = -1; dirty = true; }
        }
    }
    if (dirty) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Ui::OnLButtonDown(int x, int y) {
    // 下载进度弹窗
    if (m_showDlDialog) {
        int cw = 520, chh = 260;
        int cx = (m_width - cw) / 2, cy = (m_height - chh) / 2;
        int btw = 120, bth = 40;
        int btx = cx + cw - btw - 24, bty = cy + chh - bth - 18;
        if (x >= btx && x < btx + btw && y >= bty && y < bty + bth) {
            m_showDlDialog = false;
            if (m_installing) launcher::App::Instance().abortInstall();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    // 皮肤预览框：按住拖动旋转（弹窗打开时不抢点击）
    if (m_page == Account && !m_showOfflineDlg && m_skinBox.right > m_skinBox.left &&
        inRect(m_skinBox, x, y)) {
        m_skinDrag = true;
        m_dragX = x;
        m_dragY = y;
        SetCapture(m_hwnd);
        return;
    }

    // 侧边栏（模组版本弹窗打开时不响应，避免点穿到页面切换）
    const bool addonModal = m_addonShowDlg || m_addonShowChangelog;
    if (!addonModal && x < kSidebarW) {
        for (int i = 0; i < kCount; ++i) {
            const Entry& e = kItems[i];
            if (e.kind == Kind::Header) continue;
            int left = (e.kind == Kind::Account) ? 18 : kNavLeft;
            int right = (e.kind == Kind::Account) ? 206 : kNavRight;
            if (x >= left && x < right && y >= e.y && y < e.y + e.h) {
                if (e.page == Versions && e.tab >= 0) m_verTab = e.tab;
                SetPage(e.page);
                return;
            }
        }
        return;
    }

    // 展开的下拉先吃掉点击
    for (auto& w : m_widgets) {
        if (w.type != Widget::Combo || !w.dropped) continue;
        float ih = 30.0f * (float)w.items.size();
        float ry = (float)w.rc.bottom + 2;
        if (x >= w.rc.left && x < w.rc.right && y >= ry && y < ry + ih) {
            w.sel = (int)((y - ry) / 30);
            w.dropped = false;
            HandleComboSelect(w);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        w.dropped = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    Widget* hit = nullptr;
    for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it) {
        auto& w = *it;
        // 跳过 Label 和 Panel（Panel 只做背景，不能拦截点击）
        if (w.type == Widget::Label || w.type == Widget::Panel) continue;
        if (inRect(w.rc, x, y)) { hit = &w; break; }
    }

    // 点了空白：提交编辑并收起
    if (!hit) {
        if (m_editOverlay && IsWindowVisible(m_editOverlay)) CommitEdit();
        return;
    }

    switch (hit->type) {
    case Widget::Btn:
        if (m_editOverlay && IsWindowVisible(m_editOverlay)) CommitEdit();
        hit->pressed = true;
        // [AutoFix] 拦截顶部下载分类按钮，切换后自动搜索
        if (hit->id.rfind(L"dl.cat", 0) == 0) {
            int newCat = _wtoi(hit->id.c_str() + 6);
            if (newCat >= 0 && newCat <= 4 && m_dlCat != newCat) {
                m_dlCat = newCat;
                // 切换分类时强制清空并重新搜索（如果为空）
                if (m_dlCat == 1) {
                    m_addonHits.clear();
                    if (!m_dlSearching) StartAddonSearch(0);
                } else if (m_dlCat >= 2 && m_dlCat <= 3) {
                    m_hits.clear();
                    if (!m_dlSearching) StartDownloadSearch();
                }
                Rebuild();
            } else if (newCat >= 0 && newCat <= 4 && m_dlCat == newCat) {
                // 如果点的是当前分类，且列表为空，也触发一次搜索（防止之前失败后无法重试）
                if (m_dlCat == 1 && m_addonHits.empty() && !m_dlSearching) StartAddonSearch(0);
                else if (m_dlCat >= 2 && m_dlCat <= 3 && m_hits.empty() && !m_dlSearching) StartDownloadSearch();
            }
        }
        break;
    case Widget::Combo:
        if (m_editOverlay && IsWindowVisible(m_editOverlay)) CommitEdit();
        for (auto& w : m_widgets) if (w.type == Widget::Combo) w.dropped = false;
        hit->dropped = !hit->dropped;
        break;
    case Widget::Edit: {
        // 激活原生编辑框
        for (auto& w : m_widgets) if (w.type == Widget::Edit) w.focused = false;
        hit->focused = true;
        m_editTargetId = hit->id;
        if (m_editOverlay) {
            RECT r = hit->rc;
            int h = r.bottom - r.top;
            int wpx = r.right - r.left;
            SetWindowPos(m_editOverlay, HWND_TOP, r.left + 10, r.top + (h - 22) / 2,
                         wpx - 20, 22, SWP_SHOWWINDOW);
            SetWindowTextW(m_editOverlay, hit->editText.c_str());
            SendMessageW(m_editOverlay, EM_SETSEL, (WPARAM)hit->editText.size(),
                         (LPARAM)hit->editText.size());
            SetFocus(m_editOverlay);
        }
        break;
    }
    case Widget::Check:
        hit->checked = !hit->checked;
        break;
    case Widget::List: {
        int rowH = 34;
        int idx = hit->scroll + (y - hit->rc.top) / rowH;
        if (idx >= 0 && idx < (int)hit->listItems.size()) {
            bool changed = (hit->selIdx != idx);
            hit->selIdx = idx;
            // 账号列表：记住用户选的行（Rebuild 会用它恢复选中），并清掉上一个账号的皮肤提示
            if (hit->id == L"acc.list") {
                m_accSel = idx;
                if (changed) {
                    m_skinMsg.clear();
                    m_skinAutoKey.clear();
                    Rebuild();
                }
            }
        }
        break;
    }
    case Widget::Slider: {
        float ratio = (float)(x - hit->rc.left) / (float)std::max(1, (int)(hit->rc.right - hit->rc.left));
        hit->value = hit->vmin + (int)(ratio * (hit->vmax - hit->vmin));
        hit->value = std::clamp(hit->value, hit->vmin, hit->vmax);
        break;
    }
    case Widget::Grid: {
        int idx = HitGridIndex(*hit, x, y);
        if (idx >= 0) {
            bool inBtn = HitGridButton(*hit, idx, x, y);
            HandleGridClick(*hit, idx, inBtn);
        }
        break;
    }
    case Widget::Rows: {
        int idx = HitRowIndex(*hit, x, y);
        if (idx >= 0) {
            bool inBtn = HitRowButton(*hit, idx, x, y);
            HandleGridClick(*hit, idx, inBtn);
        }
        break;
    }
    default: break;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Ui::OnLButtonUp(int x, int y) {
    if (m_skinDrag) {
        m_skinDrag = false;
        ReleaseCapture();
        return;
    }
    for (auto& w : m_widgets) {
        if (w.type == Widget::Btn && w.pressed) {
            w.pressed = false;
            if (inRect(w.rc, x, y)) HandleButton(w.id);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
}

void Ui::OnChar(wchar_t c) {
    // 有原生编辑框时由它接管
    if (m_editOverlay && IsWindowVisible(m_editOverlay)) return;
    for (auto& w : m_widgets) {
        if (w.type == Widget::Edit && w.focused) {
            if (c == L'\b') { if (!w.editText.empty()) w.editText.pop_back(); }
            else if (c >= 0x20) w.editText.push_back(c);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
}

void Ui::OnKeyDown(WPARAM vk) {
    if (vk == VK_ESCAPE) {
        if (m_addonShowChangelog) {
            m_addonShowChangelog = false;
            Rebuild();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (m_addonShowDlg) {
            m_addonShowDlg = false;
            m_addonShowChangelog = false;
            Rebuild();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (m_addonDetail) {
            m_addonDetail = false;
            Rebuild();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (m_showOfflineDlg) {
            m_showOfflineDlg = false;
            Rebuild();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        for (auto& w : m_widgets) if (w.type == Widget::Combo) w.dropped = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (vk == VK_F5) { Rebuild(); InvalidateRect(m_hwnd, nullptr, FALSE); return; }
}

void Ui::OnMouseWheel(int delta, int x, int y) {
    // 皮肤预览：滚轮缩放
    if (m_page == Account && !m_showOfflineDlg && m_skinBox.right > m_skinBox.left &&
        inRect(m_skinBox, x, y)) {
        m_skinZoom = std::clamp(m_skinZoom + (delta > 0 ? 0.1f : -0.1f), 0.5f, 2.5f);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    for (auto& w : m_widgets) {
        if (w.type == Widget::Grid && inRect(w.rc, x, y)) {
            int cardH = w.cardH + w.gap;
            int rows = ((int)w.items2.size() + w.cols - 1) / std::max(1, w.cols);
            int visible = std::max(1, (int)((w.rc.bottom - w.rc.top) / cardH));
            int maxScroll = std::max(0, rows - visible);
            w.gscroll -= delta / 120;
            w.gscroll = std::clamp(w.gscroll, 0, maxScroll);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (w.type == Widget::Rows && inRect(w.rc, x, y)) {
            int rowH = std::max(28, w.rowH);
            int visible = std::max(1, (int)((w.rc.bottom - w.rc.top) / rowH));
            int maxScroll = std::max(0, (int)w.items2.size() - visible);
            w.gscroll = std::clamp(w.gscroll - delta / 120 * 2, 0, maxScroll);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (w.type == Widget::List && inRect(w.rc, x, y)) {
            int rowH = 34;
            int maxScroll = (int)w.listItems.size() - (int)((w.rc.bottom - w.rc.top) / rowH);
            w.scroll -= delta / 120;
            w.scroll = std::clamp(w.scroll, 0, std::max(0, maxScroll));
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
    ApplyPageScroll(delta / 120);
}

void Ui::ApplyPageScroll(int delta) {
    int maxScroll = 0;
    if (m_page == Settings) maxScroll = 1700;
    else if (m_page == Versions && m_verTab == TabVerSet) maxScroll = 900;
    if (maxScroll <= 0) return;
    int before = m_scrollY;
    m_scrollY = std::clamp(m_scrollY - delta * 48, 0, maxScroll);
    if (m_scrollY != before) {
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

// ============================================================
// 异步事件
// ============================================================
void EventSink::post(int type, const std::string& data) {
    if (!alive.load()) return;             // UI 已销毁，丢弃即可
    {
        std::lock_guard<std::mutex> lk(mu);
        queue.push_back({ type, data });
    }
    HWND h = hwnd;
    if (h && IsWindow(h)) PostMessageW(h, WM_APP_EVENT, 0, 0);
}

void Ui::OnAsyncEvent(int type, const std::string& data) {
    PostEvent(type, data);
}

void Ui::PostEvent(int type, const std::string& data) {
    if (m_sink) m_sink->post(type, data);
}

void Ui::ShowToast(const std::wstring& text, bool error) {
    m_toast = text;
    m_toastError = error;
    m_toastUntil = GetTickCount64() + 5000;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Ui::CommitEdit() {
    if (!m_editOverlay || m_editTargetId.empty()) return;
    wchar_t buf[512] = { 0 };
    GetWindowTextW(m_editOverlay, buf, 512);
    g_committing = true;
    ShowWindow(m_editOverlay, SW_HIDE);
    g_committing = false;
    std::wstring id = m_editTargetId;
    m_editTargetId.clear();
    Widget* w = Find(id);
    if (w) {
        w->editText = buf;
        w->focused = false;
        if (id == L"dl.name") {
            // 名称规则：任意语言字母数字，符号只允许下划线
            if (!mlc::IsValidGameName(w->editText)) {
                std::wstring s = mlc::SanitizeGameName(w->editText);
                if (s != w->editText && !s.empty()) {
                    w->editText = s;
                    m_toast = L"名称只能包含文字、数字和下划线 _，已自动修正";
                    m_toastError = true;
                    m_toastUntil = GetTickCount64() + 4000;
                }
            }
            m_dlName = w->editText;
        } else if (id == L"dl.search" || id == L"dl.csearch") {
            m_dlSearch = w->editText;
        } else if (id == L"dl.mcver") {
            m_addonMcVer = w->editText;
        } else if (id == L"ad.fname") {
            m_addonFileName = w->editText;
        } else if (id == L"acc.name") {
            m_loginName = w->editText;
        }
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
    SetFocus(m_hwnd);
}

void Ui::OnTimerTick() {
    std::vector<std::pair<int, std::string>> evs;
    {
        std::lock_guard<std::mutex> lk(m_sink->mu);
        evs.swap(m_sink->queue);
    }
    bool redraw = false;
    for (auto& [t, d] : evs) {
        if (t == (int)launcher::Ev::InstallProgress) {
            try {
                auto j = json::parse(d);
                m_installing = true;
                std::string phase = j.get("phase").asString();
                std::string cur = j.get("current").asString();
                int64_t total = j.get("total").asInt(), done = j.get("done").asInt();
                std::wstring label;
                if (phase == "assets") {
                    label = L"下载资源文件 " + std::to_wstring(done) + L" / " +
                            std::to_wstring(total);
                } else if (phase == "libraries") {
                    label = L"下载库文件 " + std::to_wstring(done) + L" / " +
                            std::to_wstring(total);
                } else if (phase == "client") {
                    label = L"下载 " + mlc::Utf8ToW(cur);
                } else if (phase == "extract") {
                    label = L"解压 " + mlc::Utf8ToW(cur);
                } else if (phase == "java") {
                    label = mlc::Utf8ToW(cur) + L"（" + std::to_wstring(done) + L" / " +
                            std::to_wstring(total) + L"）";
                } else if (phase == "version") {
                    label = L"准备版本 " + mlc::Utf8ToW(cur);
                } else {
                    label = mlc::Utf8ToW(phase + (cur.empty() ? "" : "  " + cur));
                }
                m_progressText = label;
                m_progress = total > 0 ? (float)((double)done / (double)total) : 0.0f;
            } catch (...) {}
            redraw = true;
        } else if (t == (int)launcher::Ev::InstallDone) {
            m_installing = false;
            m_progress = 1.0f;
            if (d == "__manifest__") {
                m_progressText = L"版本清单已更新";
                Rebuild();
            } else if (d.rfind("__loaderver__:", 0) == 0) {
                m_loaderFetching = false;
                m_loaderVersions.clear();
                std::string body = d.substr(14);
                std::string errMsg;
                size_t bar = body.find('|');
                if (bar != std::string::npos) { errMsg = body.substr(bar + 1); body = body.substr(0, bar); }
                try {
                    json::Value arr = json::parse(body);
                    for (size_t i = 0; i < arr.size(); ++i)
                        m_loaderVersions.push_back(mlc::Utf8ToW(arr.at(i).asString()));
                } catch (...) {}
                if (m_loaderVersions.empty())
                    m_loaderStatus = errMsg.empty() ? L"该版本没有可用的加载器版本"
                                                    : mlc::Utf8ToW("获取失败：" + errMsg);
                else
                    m_loaderStatus = L"加载器版本：" + std::to_wstring((int)m_loaderVersions.size()) + L" 个";
                Rebuild();
            } else if (d.rfind("__cfsearch__:", 0) == 0) {
                m_cfSearching = false;
                m_cfHits.clear();
                std::string errMsg;
                try {
                    json::Value root = json::parse(d.substr(13));
                    errMsg = root.get("err").asString();
                    const json::Value& arr = root.get("hits");
                    for (size_t i = 0; i < arr.size(); ++i) {
                        const json::Value& e = arr.at(i);
                        content::cf::WorldHit h;
                        h.modId = (int)e.get("modId").asInt();
                        h.fileId = (int)e.get("fileId").asInt();
                        h.name = e.get("name").asString();
                        h.summary = e.get("summary").asString();
                        h.fileName = e.get("fileName").asString();
                        h.gameVersion = e.get("gameVersion").asString();
                        h.author = e.get("author").asString();
                        h.downloads = e.get("downloads").asInt();
                        h.cover = e.get("cover").asString();
                        m_cfHits.push_back(std::move(h));
                    }
                } catch (...) {}
                if (m_cfHits.empty()) {
                    m_cfStatus = errMsg.empty() ? L"没有结果：换个关键词，或检查网络 / CurseForge 接口设置"
                                                : mlc::Utf8ToW(errMsg);
                } else {
                    m_cfStatus = L"第 " + std::to_wstring(m_cfPage + 1) + L" 页 · " +
                                 std::to_wstring((int)m_cfHits.size()) + L" 个结果 · CurseForge";
                }
                Rebuild();
            } else if (d.rfind("__addoncategories__:", 0) == 0) {
                m_addonCategoryNames.clear();
                m_addonCategoryIds.clear();
                m_addonCategoryNames.push_back(L"全部");
                m_addonCategoryIds.push_back("");
                try {
                    json::Value arr = json::parse(d.substr(20));
                    for (size_t i = 0; i < arr.size(); ++i) {
                        m_addonCategoryIds.push_back(arr.at(i).get("id").asString());
                        m_addonCategoryNames.push_back(mlc::Utf8ToW(arr.at(i).get("name").asString()));
                    }
                } catch (...) {}
                m_addonCategorySel = 0;
                Rebuild();
            } else if (d.rfind("__addonsearch__:", 0) == 0) {
                m_dlSearching = false;
                try {
                    json::Value root = json::parse(d.substr(16));
                    if ((unsigned long long)root.get("seq").asInt() != m_addonSearchSeq) {
                        // seq 不匹配：说明这是旧请求的结果，直接丢弃
                        // 但必须确保 m_dlSearching 已经被重置（上面已重置）
                        mlc::Log("[addon] stale search result ignored, seq mismatch");
                        Rebuild();
                        redraw = true;
                        continue;
                    }
                    m_addonPages = (int)root.get("pages").asInt();
                    m_addonHits.clear();
                    const json::Value& arr = root.get("hits");
                    for (size_t i = 0; i < arr.size(); ++i) {
                        const json::Value& e = arr.at(i);
                        addon::Hit h;
                        h.slug = e.get("slug").asString();
                        h.projectId = e.get("project_id").asString();
                        if (h.projectId.empty()) h.projectId = e.get("projectId").asString();
                        h.title = e.get("title").asString();
                        h.desc = e.get("desc").asString();
                        h.iconUrl = e.get("iconUrl").asString();
                        h.author = e.get("author").asString();
                        h.cover = e.get("cover").asString();
                        h.pageUrl = e.get("pageUrl").asString();
                        const json::Value& cats = e.get("categories");
                        for (size_t k = 0; k < cats.size(); ++k) h.categories.push_back(cats.at(k).asString());
                        m_addonHits.push_back(std::move(h));
                    }
                    std::string err = root.get("err").asString();
                    m_progressText = m_addonHits.empty() && !err.empty()
                        ? mlc::Utf8ToW("搜索失败：" + err)
                        : L"找到 " + std::to_wstring((int)m_addonHits.size()) + L" 个结果";
                } catch (...) {}
                Rebuild();
            } else if (d.rfind("__addonversions__:", 0) == 0) {
                m_addonVersionsLoading = false;
                m_addonVersions.clear();
                try {
                    json::Value root = json::parse(d.substr(18));
                    const json::Value& arr = root.get("versions");
                    for (size_t i = 0; i < arr.size(); ++i) {
                        const json::Value& e = arr.at(i);
                        addon::Version v;
                        v.id = e.get("id").asString();
                        v.projectId = e.get("projectId").asString();
                        v.name = e.get("name").asString();
                        v.versionNumber = e.get("versionNumber").asString();
                        v.datePublished = e.get("datePublished").asString();
                        v.versionType = e.get("versionType").asString();
                        v.fileName = e.get("fileName").asString();
                        v.url = e.get("url").asString();
                        v.md5 = e.get("md5").asString();
                        v.sha1 = e.get("sha1").asString();
                        v.sha256 = e.get("sha256").asString();
                        v.sha512 = e.get("sha512").asString();
                        const json::Value& gvs = e.get("gameVersions");
                        for (size_t k = 0; k < gvs.size(); ++k) v.gameVersions.push_back(gvs.at(k).asString());
                        const json::Value& ls = e.get("loaders");
                        for (size_t k = 0; k < ls.size(); ++k) v.loaders.push_back(ls.at(k).asString());
                        const json::Value& ds = e.get("dependencies");
                        for (size_t k = 0; k < ds.size(); ++k) {
                            addon::Dependency dep;
                            std::string t = ds.at(k).get("type").asString();
                            if (t == "required") dep.type = addon::Dependency::Type::Required;
                            else if (t == "optional") dep.type = addon::Dependency::Type::Optional;
                            else if (t == "embedded") dep.type = addon::Dependency::Type::Embedded;
                            else if (t == "incompatible") dep.type = addon::Dependency::Type::Incompatible;
                            else dep.type = addon::Dependency::Type::Broken;
                            dep.projectId = ds.at(k).get("projectId").asString();
                            dep.versionId = ds.at(k).get("versionId").asString();
                            v.dependencies.push_back(std::move(dep));
                        }
                        m_addonVersions.push_back(std::move(v));
                    }
                } catch (...) {}
                Rebuild();
            } else if (d.rfind("__addondeps__:", 0) == 0) {
                m_addonDepsLoading = false;
                m_addonDeps.clear();
                try {
                    json::Value arr = json::parse(d.substr(14));
                    for (size_t i = 0; i < arr.size(); ++i) {
                        addon::Dependency dep;
                        std::string t = arr.at(i).get("type").asString();
                        if (t == "required") dep.type = addon::Dependency::Type::Required;
                        else if (t == "optional") dep.type = addon::Dependency::Type::Optional;
                        else if (t == "embedded") dep.type = addon::Dependency::Type::Embedded;
                        else if (t == "incompatible") dep.type = addon::Dependency::Type::Incompatible;
                        else dep.type = addon::Dependency::Type::Broken;
                        dep.projectId = arr.at(i).get("projectId").asString();
                        dep.versionId = arr.at(i).get("versionId").asString();
                        dep.name = arr.at(i).get("name").asString();
                        m_addonDeps.push_back(std::move(dep));
                    }
                } catch (...) {}
                Rebuild();
            } else if (d.rfind("__addonchangelog__:", 0) == 0) {
                try {
                    json::Value o = json::parse(d.substr(19));
                    m_addonChangelog = mlc::Utf8ToW(o.get("text").asString());
                    m_addonShowChangelog = true;
                } catch (...) {}
                Rebuild();
            } else if (d.rfind("__search__:", 0) == 0) {
                m_dlSearching = false;
                m_hits.clear();
                try {
                    json::Value arr = json::parse(d.substr(11));
                    for (size_t i = 0; i < arr.size(); ++i) {
                        content::Hit h;
                        h.slug = arr.at(i).get("slug").asString();
                        h.title = arr.at(i).get("title").asString();
                        h.desc = arr.at(i).get("desc").asString();
                        h.author = arr.at(i).get("author").asString();
                        h.cover = arr.at(i).get("cover").asString();
                        h.downloads = arr.at(i).get("downloads").asInt();
                        m_hits.push_back(std::move(h));
                    }
                } catch (...) {}
                m_progressText = L"找到 " + std::to_wstring((int)m_hits.size()) + L" 个结果";
                Rebuild();
            } else if (d.rfind("__content__:", 0) == 0) {
                m_progressText = mlc::Utf8ToW(d.substr(12));
                m_cfStatus = m_progressText;
                ShowToast(m_progressText);
                m_modsLoaded = m_packsLoaded = m_shadersLoaded = m_worldsLoaded = m_schemsLoaded = false;
                Rebuild();
            } else if (d.rfind("__launched__", 0) == 0) {
                // 游戏已启动（GameStart 另发）
            } else {
                // 加载器安装会新建一个版本（原版 + 加载器），提示里把这件事说清楚
                const bool isLoader = d.rfind("fabric-loader-", 0) == 0 ||
                                      d.rfind("quilt-loader-", 0) == 0 ||
                                      d.find("-forge-") != std::string::npos ||
                                      d.find("neoforge-") != std::string::npos;
                if (isLoader) {
                    // 照 HMCL：加载器是装进这个游戏的组件，不是「又装了一个游戏」
                    m_progressText = L"加载器已装入当前游戏（版本 " + mlc::Utf8ToW(d) + L"）";
                } else {
                    m_progressText = L"安装完成";
                }
                m_toast = m_progressText;
                m_dlFolders.clear();   // 重新收集安装目录（新文件夹已登记）
                m_toastUntil = GetTickCount64() + 4000;
                m_modsLoaded = m_packsLoaded = m_shadersLoaded = m_worldsLoaded = m_schemsLoaded = false;
                Rebuild();
            }
            redraw = true;
        } else if (t == (int)launcher::Ev::InstallError) {
            m_installing = false;
            m_launching = false;
            m_progress = 0.0f;
            m_dlSearching = false;
            m_cfSearching = false;
            m_progressText = mlc::Utf8ToW("失败：" + d);
            ShowToast(m_progressText, true);
            Rebuild();
            redraw = true;
        } else if (t == (int)launcher::Ev::GameLaunching) {
            // 进程起来了，但窗口还没出现 —— 这段时间只能算「启动中」
            m_launching = true;
            m_running = false;
            m_launchTick = GetTickCount64();
            m_pid = (DWORD)atoi(d.c_str());
            m_progressText = L"启动中…（等待游戏窗口出现）";
            Rebuild();
            redraw = true;
        } else if (t == (int)launcher::Ev::GameStart) {
            m_launching = false;
            m_launchTick = 0;
            m_running = true;
            m_installing = false;
            m_progress = 0.0f;
            m_pid = (DWORD)atoi(d.c_str());
            m_progressText = L"游戏运行中";
            ShowToast(L"游戏已启动 (PID " + mlc::Utf8ToW(d) + L")");
            Rebuild();
            redraw = true;
        } else if (t == (int)launcher::Ev::GameExit) {
            bool wasLaunching = m_launching;
            m_launching = false;
            m_launchTick = 0;
            m_running = false;
            m_pid = 0;
            int code = atoi(d.c_str());
            // 窗口都没出来就退了：InstallError 已经报过原因，这里不再重复弹
            if (!wasLaunching) {
                if (code != 0) {
                    ShowToast(L"游戏异常退出（code " + mlc::Utf8ToW(d) +
                              L"）· 日志：游戏目录 logs\\yxg-launcher.log", true);
                } else {
                    ShowToast(L"游戏已退出");
                }
            }
            m_progressText = L"就绪";
            Rebuild();
            redraw = true;
        } else if (d.rfind("__cfcovers__:", 0) == 0) {
            // CurseForge 封面增量到达：只更新已缓存路径，不重建整页
            int applied = 0;
            try {
                json::Value arr = json::parse(d.substr(13));
                for (size_t i = 0; i < arr.size(); ++i) {
                    int modId = (int)arr.at(i).at(0).asInt();
                    std::string path = arr.at(i).at(1).asString();
                    for (auto& h : m_cfHits) {
                        if (h.modId == modId) { h.cover = path; ++applied; break; }
                    }
                }
            } catch (const std::exception& e) {
                mlc::Log(std::string("[cf] covers parse fail: ") + e.what());
            }
            mlc::Log("[cf] covers applied=" + std::to_string(applied));
            Rebuild();
            redraw = true;
        } else if (t == (int)launcher::Ev::Info || t == (int)launcher::Ev::Log) {
            if (d == "__autosearch_addon__") {
                StartAddonSearch(0);
            } else if (d.rfind("__mscode__:", 0) == 0) {
                // 设备码到达：userCode|verificationUri
                std::string payload = d.substr(11);
                size_t bar = payload.find('|');
                std::string code = bar == std::string::npos ? payload : payload.substr(0, bar);
                std::string uri = bar == std::string::npos ? "" : payload.substr(bar + 1);
                m_msUserCode = mlc::Utf8ToW(code);
                m_msVerifyUri = mlc::Utf8ToW(uri.empty() ? "https://www.microsoft.com/link" : uri);
                m_msPending = true;
                m_msStatus = L"已打开浏览器，请在页面里输入上面的代码（约 15 分钟内有效）…";
                ShellExecuteW(nullptr, L"open", m_msVerifyUri.c_str(), nullptr, nullptr, SW_SHOW);
                Rebuild();
            } else if (d.rfind("__msok__:", 0) == 0) {
                m_msPending = false;
                m_msStatus = L"登录成功，已保存为当前账号。";
                m_msUserCode.clear();
                m_msVerifyUri.clear();
                m_installing = false;
                m_accSel = -1;             // 新登录的账号成为当前账号，列表选中跟随它
                ShowToast(L"微软账号登录成功");
                Rebuild();
            } else if (d.rfind("__msfail__:", 0) == 0) {
                m_msPending = false;
                std::wstring err = mlc::Utf8ToW(d.substr(11));
                m_msStatus = L"登录失败：" + err;
                ShowToast(L"微软登录失败：" + err, true);
                Rebuild();
            } else if (d.rfind("__skinfetch__:", 0) == 0) {
                // 触发一次「从正版拉皮肤」（每个账号会话内只自动来一次）
                std::string key = d.substr(14);
                mlc::AccountRec* rec = mlc::Store::I().FindAccount(key);
                if (rec && rec->type == "msa" && !rec->uuid.empty()) {
                    std::string uuid = rec->uuid;
                    // 落盘文件名要和「选择皮肤」一致（都用带横线的 uuid），否则同一个账号
                    // 会存出两份皮肤，迁移账号时只搬走其中一份。
                    std::wstring base = mlc::Store::I().SkinsDirFor(rec->portable);
                    std::wstring stem = mlc::Utf8ToW(uuid.empty() ? rec->name : uuid);
                    std::wstring skinDest = base + L"\\" + stem + L".png";
                    std::wstring capeDest = base + L"\\" + stem + L"-cape.png";
                    m_skinBusy = true;
                    m_skinMsg = L"正在从正版拉取皮肤…";
                    Rebuild();
                    auto sink = m_sink;
                    std::thread([sink, uuid, key, skinDest, capeDest]() {
                        bool slim = false, hasCape = false;
                        std::string err;
                        bool ok = skin::FetchFromMojang(uuid, skinDest, capeDest,
                                                        &slim, &hasCape, err);
                        std::string payload = key + "|" + (ok ? "1" : "0") + "|" +
                                              (slim ? "1" : "0") + "|" +
                                              (hasCape ? "1" : "0") + "|" + err;
                        sink->post((int)launcher::Ev::Info, "_skinres:" + payload);
                    }).detach();
                }
            } else if (d.rfind("_skinres:", 0) == 0) {
                // 拉取结果：key|ok|slim|hasCape|err
                std::string p = d.substr(9);
                std::vector<std::string> seg;
                size_t pos = 0;
                while (seg.size() < 5) {
                    size_t bar = p.find('|', pos);
                    if (bar == std::string::npos) { seg.push_back(p.substr(pos)); break; }
                    seg.push_back(p.substr(pos, bar - pos));
                    pos = bar + 1;
                }
                while (seg.size() < 5) seg.push_back("");
                const std::string& key = seg[0];
                const bool ok = seg[1] == "1";
                const bool slim = seg[2] == "1";
                const bool hasCape = seg[3] == "1";
                m_skinBusy = false;
                mlc::AccountRec* rec = mlc::Store::I().FindAccount(key);
                if (ok && rec) {
                    // 与拉取时写下的文件名保持一致（带横线的 uuid / 名字）
                    std::string stem = rec->uuid.empty() ? rec->name : rec->uuid;
                    rec->skin = stem + ".png";
                    rec->skinPath.clear();               // 远程皮肤优先于旧的自定义路径
                    rec->skinModel = slim ? "slim" : "classic";
                    rec->skinRemoteAt = (int64_t)time(nullptr);
                    if (hasCape) {
                        rec->cape = stem + "-cape.png";
                        rec->capePath.clear();
                    }
                    mlc::Store::I().SaveAccounts();
                    skin::Clear();
                    m_skinMsg = hasCape ? L"已从正版同步（皮肤 + 披风）" : L"已从正版同步皮肤";
                } else {
                    m_skinMsg = L"正版同步失败：" + mlc::Utf8ToW(seg[4]);
                }
                Rebuild();
            } else if (d.rfind("_skinup:", 0) == 0) {
                // 上传结果：key|ok|err
                std::string p = d.substr(8);
                size_t b1 = p.find('|');
                std::string key = (b1 == std::string::npos) ? p : p.substr(0, b1);
                std::string rest = (b1 == std::string::npos) ? "" : p.substr(b1 + 1);
                size_t b2 = rest.find('|');
                bool ok = ((b2 == std::string::npos) ? rest : rest.substr(0, b2)) == "1";
                std::string err = (b2 == std::string::npos) ? "" : rest.substr(b2 + 1);
                m_skinBusy = false;
                if (ok) {
                    m_skinMsg = L"皮肤已上传到正版账号（重进游戏或服务器后生效）";
                    ShowToast(L"皮肤已上传到 Mojang");
                    if (mlc::AccountRec* rec = mlc::Store::I().FindAccount(key)) {
                        rec->skinRemoteAt = (int64_t)time(nullptr);
                        mlc::Store::I().SaveAccounts();
                    }
                } else {
                    m_skinMsg = L"上传失败：" + mlc::Utf8ToW(err);
                    ShowToast(m_skinMsg, true);
                }
                Rebuild();
            } else {
                m_progressText = mlc::Utf8ToW(d);
            }
            redraw = true;
        } else if (t == (int)launcher::Ev::IntegrityDone) {
            try {
                auto j = json::parse(d);
                bool ok = j.get("ok").asBool();
                std::wstring detail = mlc::Utf8ToW(j.get("detail").asString());
                ShowToast(ok ? (L"完整性检查通过 · " + detail)
                             : (L"完整性检查未通过 · " + detail), !ok);
            } catch (...) {}
            m_installing = false;
            redraw = true;
        } else if (t == (int)launcher::Ev::AccountChange) {
            Rebuild();
            redraw = true;
        }
    }

    if (Widget* w = Find(L"dl.progress")) w->progress = m_progress;
    if (Widget* w = Find(L"home.progress")) w->progress = m_progress;
    // 「启动中」时把已等待秒数显示出来，避免看起来像卡死
    if (m_launching && m_launchTick) {
        int sec = (int)((GetTickCount64() - m_launchTick) / 1000);
        if (sec >= 0) {
            m_progressText = L"启动中…（等待游戏窗口出现，已 " + std::to_wstring(sec) + L" 秒）";
            redraw = true;
        }
    }
    if (Widget* w = Find(L"home.status")) w->text = m_progressText;
    if (Widget* w = Find(L"dl.status")) w->text = m_progressText;

    if (!m_toast.empty() && GetTickCount64() > m_toastUntil) {
        m_toast.clear();
        redraw = true;
    }
    if (bg::Manager::Instance().ConsumeDirty()) redraw = true;
    if (redraw) InvalidateRect(m_hwnd, nullptr, FALSE);
}

} // namespace ui






