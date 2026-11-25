// PluginCenter.cpp
#include "PluginCenter.h"

#include <windowsx.h>
#include <ole2.h>
#include <gdiplus.h>
#include <algorithm>
#include <cwchar>
#include <memory>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace {

constexpr int kPadX = 8;
constexpr int kHeaderH = 30;
constexpr int kCardH = 92;
constexpr int kGap = 10;
constexpr int kScrollW = 8;
constexpr wchar_t kClassName[] = L"LightPluginCenterWnd";

constexpr ARGB kBg = 0xFFF6F8FC;
constexpr ARGB kCard = 0xFFFFFFFF;
constexpr ARGB kCardHover = 0xFFF8FBFF;
constexpr ARGB kBorder = 0xFFE2E8F2;
constexpr ARGB kBorderHover = 0xFFBCD2F5;
constexpr ARGB kText = 0xFF1E293B;
constexpr ARGB kMuted = 0xFF6B7A90;
constexpr ARGB kAccent = 0xFF3B82F6;
constexpr ARGB kAccentSoft = 0xFFE8F1FF;
constexpr ARGB kGreen = 0xFF22C55E;
constexpr ARGB kGreenSoft = 0xFFE1F9EA;
constexpr ARGB kOff = 0xFFCBD5E1;
constexpr ARGB kOffSoft = 0xFFF1F5F9;

void MakeRoundedRect(const RectF& r, REAL radius, GraphicsPath& path) {
    path.Reset();
    const REAL d = radius * 2.0f;
    if (radius <= 0.1f) {
        path.AddRectangle(r);
        return;
    }
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillRounded(Graphics& g, const RectF& r, REAL radius, ARGB color) {
    GraphicsPath path;
    MakeRoundedRect(r, radius, path);
    SolidBrush brush{ Color(color) };
    g.FillPath(&brush, &path);
}

void StrokeRounded(Graphics& g, const RectF& r, REAL radius, ARGB color,
    REAL width = 1.0f) {
    GraphicsPath path;
    MakeRoundedRect(r, radius, path);
    Pen pen(Color(color), width);
    g.DrawPath(&pen, &path);
}

std::wstring Ellipsis(const std::wstring& text, size_t maxChars) {
    if (text.size() <= maxChars) return text;
    if (maxChars <= 1) return text.substr(0, maxChars);
    return text.substr(0, maxChars - 1) + L"…";
}

const wchar_t* IconForKind(const std::wstring& kind) {
    if (kind.find(L"内置") != std::wstring::npos) return L"✨";
    if (kind.find(L"GUI") != std::wstring::npos) return L"🖥";
    if (kind.find(L"API") != std::wstring::npos ||
        kind.find(L"指令") != std::wstring::npos) return L"⌘";
    return L"🧩";
}

void DrawStringSimple(Graphics& g, const std::wstring& text, Font& font,
    const RectF& rect, ARGB color, StringAlignment align = StringAlignmentNear,
    StringAlignment lineAlign = StringAlignmentCenter, bool ellipsis = true) {
    SolidBrush brush{ Color(color) };
    StringFormat fmt;
    fmt.SetAlignment(align);
    fmt.SetLineAlignment(lineAlign);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    fmt.SetTrimming(ellipsis ? StringTrimmingEllipsisCharacter
        : StringTrimmingNone);
    g.DrawString(text.c_str(), -1, &font, rect, &fmt, &brush);
}

} // namespace

PluginCenterPanel::PluginCenterPanel() = default;

PluginCenterPanel::~PluginCenterPanel() {
    // 子窗口由父窗口销毁；这里仅清空句柄，避免再访问已销毁 HWND。
    m_hWnd = nullptr;
}

void PluginCenterPanel::EnsureClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = PluginCenterPanel::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    registered = true;
}

bool PluginCenterPanel::Create(HWND parent, HINSTANCE instance, int controlId) {
    if (m_hWnd && IsWindow(m_hWnd)) return true;
    EnsureClass(instance);

    m_hParent = parent;
    m_hInstance = instance;
    m_controlId = controlId;

    m_hWnd = CreateWindowExW(0, kClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_TABSTOP,
        0, 0, 100, 100, parent, (HMENU)(INT_PTR)controlId,
        instance, this);
    return m_hWnd != nullptr;
}

void PluginCenterPanel::Destroy() {
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
    }
    m_hWnd = nullptr;
}

void PluginCenterPanel::SetItems(std::vector<PluginCenterItem> items) {
    m_items = std::move(items);
    m_scrollY = 0;
    m_hoverItem = -1;
    m_hoverButton = -1;
    m_pressedItem = -1;
    m_pressedButton = -1;
    ClampScroll();
    InvalidateAll();
}

void PluginCenterPanel::Move(int x, int y, int width, int height) {
    if (m_hWnd && IsWindow(m_hWnd)) {
        MoveWindow(m_hWnd, x, y, width, height, TRUE);
    }
}

void PluginCenterPanel::Show(bool show) {
    if (m_hWnd && IsWindow(m_hWnd)) {
        ShowWindow(m_hWnd, show ? SW_SHOW : SW_HIDE);
    }
}

void PluginCenterPanel::Refresh() {
    ClampScroll();
    InvalidateAll();
}

void PluginCenterPanel::InvalidateAll() {
    if (m_hWnd && IsWindow(m_hWnd)) {
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

int PluginCenterPanel::ContentHeight() const {
    if (m_items.empty()) return kHeaderH + 60;
    return kHeaderH + (int)m_items.size() * (kCardH + kGap) + 6;
}

int PluginCenterPanel::VisibleHeight() const {
    if (!m_hWnd || !IsWindow(m_hWnd)) return 0;
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    return rc.bottom - rc.top;
}

void PluginCenterPanel::ClampScroll() {
    int maxScroll = std::max(0, ContentHeight() - VisibleHeight());
    m_scrollY = std::max(0, std::min(m_scrollY, maxScroll));
}

void PluginCenterPanel::UpdateScrollInfo() {
    ClampScroll();
    InvalidateAll();
}

void PluginCenterPanel::ScrollBy(int delta) {
    m_scrollY += delta;
    ClampScroll();
    InvalidateAll();
}

int PluginCenterPanel::ItemAt(int x, int y) const {
    if (x < kPadX || x > 32767) return -1;
    int localY = y + m_scrollY - kHeaderH;
    if (localY < 0) return -1;
    int index = localY / (kCardH + kGap);
    if (index < 0 || index >= (int)m_items.size()) return -1;
    int inCard = localY % (kCardH + kGap);
    if (inCard > kCardH) return -1;
    return index;
}

int PluginCenterPanel::ButtonAt(int x, int y, int& itemIndex) const {
    itemIndex = ItemAt(x, y);
    if (itemIndex < 0 || itemIndex >= (int)m_items.size()) return -1;

    RECT rc;
    GetClientRect(m_hWnd, &rc);
    int cardTop = kHeaderH + itemIndex * (kCardH + kGap) - m_scrollY;
    int right = rc.right - kPadX - kScrollW - 8;

    const auto& item = m_items[itemIndex];

    // 右侧开关
    RECT toggle = { right - 46, cardTop + 33, right, cardTop + 59 };
    if (x >= toggle.left && x < toggle.right &&
        y >= toggle.top && y < toggle.bottom) {
        return 0;
    }

    if (!item.builtin && item.canReload) {
        RECT reload = { right - 118, cardTop + 34, right - 86, cardTop + 62 };
        if (x >= reload.left && x < reload.right &&
            y >= reload.top && y < reload.bottom) {
            return 2;
        }
    }
    if (!item.builtin && item.canOpenFolder) {
        RECT open = { right - 80, cardTop + 34, right - 50, cardTop + 62 };
        if (x >= open.left && x < open.right &&
            y >= open.top && y < open.bottom) {
            return 1;
        }
    }
    return -1;
}

void PluginCenterPanel::UpdateHover(int x, int y) {
    int buttonIndex = -1;
    int button = ButtonAt(x, y, buttonIndex);
    int item = ItemAt(x, y);

    int newHoverItem = item;
    int newHoverButton = button;
    if (newHoverItem != m_hoverItem || newHoverButton != m_hoverButton) {
        m_hoverItem = newHoverItem;
        m_hoverButton = newHoverButton;
        InvalidateAll();
    }

    if (!m_trackingLeave && m_hWnd) {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&tme);
        m_trackingLeave = true;
    }

    if (m_hWnd && IsWindow(m_hWnd)) {
        SetCursor(LoadCursor(nullptr, (button >= 0) ? IDC_HAND : IDC_ARROW));
    }
}

void PluginCenterPanel::HandleClick(int x, int y) {
    int buttonIndex = -1;
    int button = ButtonAt(x, y, buttonIndex);
    if (button >= 0 && buttonIndex >= 0) {
        if (button == 0) {
            NotifyToggle(buttonIndex);
        }
        else if (button == 1) {
            NotifyAction(buttonIndex, L"open");
        }
        else if (button == 2) {
            NotifyAction(buttonIndex, L"reload");
        }
        return;
    }
}

void PluginCenterPanel::NotifyToggle(int index) {
    if (index < 0 || index >= (int)m_items.size()) return;
    auto& item = m_items[index];
    if (!item.canToggle) return;

    item.enabled = !item.enabled;
    InvalidateAll();

    ToggleCallback cb = m_onToggle;
    std::wstring id = item.id;
    bool enabled = item.enabled;
    if (cb) cb(id, enabled);
}

void PluginCenterPanel::NotifyAction(int index, const wchar_t* action) {
    if (index < 0 || index >= (int)m_items.size()) return;
    ActionCallback cb = m_onAction;
    std::wstring id = m_items[index].id;
    std::wstring act = action ? action : L"";
    if (cb) cb(id, act);
}

void PluginCenterPanel::ShowContextMenu(int index, POINT pt) {
    if (index < 0 || index >= (int)m_items.size()) return;
    const auto& item = m_items[index];

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    if (item.canToggle) {
        AppendMenuW(menu, MF_STRING, 1, item.enabled ? L"停用" : L"启用");
    }
    if (!item.builtin && item.canOpenFolder) {
        AppendMenuW(menu, MF_STRING, 2, L"打开所在目录");
    }
    if (!item.builtin && item.canReload) {
        AppendMenuW(menu, MF_STRING, 3, L"重新加载");
    }

    int selected = (int)TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
    DestroyMenu(menu);

    if (selected == 1) NotifyToggle(index);
    else if (selected == 2) NotifyAction(index, L"open");
    else if (selected == 3) NotifyAction(index, L"reload");
}

void PluginCenterPanel::DrawScrollbar(void* graphicsPtr) {
    Graphics& g = *reinterpret_cast<Graphics*>(graphicsPtr);
    if (!m_hWnd) return;
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    int content = ContentHeight();
    if (content <= rc.bottom) return;

    int trackTop = 4;
    int trackBottom = rc.bottom - 4;
    int trackH = trackBottom - trackTop;
    int thumbH = trackH * rc.bottom / content;
    if (thumbH < 36) thumbH = 36;
    int maxScroll = content - rc.bottom;
    if (maxScroll < 1) maxScroll = 1;
    int thumbTop = trackTop + (trackH - thumbH) * m_scrollY / maxScroll;

    FillRounded(g, RectF((REAL)(rc.right - 6), (REAL)trackTop, 3.0f,
        (REAL)trackH), 1.5f, 0x14000000);
    FillRounded(g, RectF((REAL)(rc.right - 6), (REAL)thumbTop, 3.0f,
        (REAL)thumbH), 1.5f, 0x553B82F6);
}

void PluginCenterPanel::DrawCard(void* graphicsPtr,
    const PluginCenterItem& item, int index, int y) {
    Graphics& g = *reinterpret_cast<Graphics*>(graphicsPtr);
    RECT rc;
    GetClientRect(m_hWnd, &rc);

    const int x = kPadX;
    const int w = rc.right - kPadX * 2 - kScrollW;
    const bool hovered = (index == m_hoverItem);
    const RectF card((REAL)x, (REAL)y, (REAL)w, (REAL)kCardH);

    // 阴影（简单叠加两层，低开销）
    FillRounded(g, RectF(card.X, card.Y + 1.5f, card.Width, card.Height), 14.0f,
        0x0F1E3A5F);
    FillRounded(g, card, 14.0f, hovered ? kCardHover : kCard);
    StrokeRounded(g, card, 14.0f, hovered ? kBorderHover : kBorder);

    // 图标圆角方块
    RectF iconR(card.X + 16, card.Y + 20, 50, 50);
    FillRounded(g, iconR, 14.0f,
        item.builtin ? 0xFFF3E8FF : (item.enabled ? kAccentSoft : kOffSoft));

    Font iconFont(L"Segoe UI Emoji", 20, FontStyleRegular, UnitPixel);
    DrawStringSimple(g, IconForKind(item.kind), iconFont,
        RectF(iconR.X, iconR.Y + 2, iconR.Width, iconR.Height - 2),
        item.builtin ? 0xFF8B5CF6 : kAccent, StringAlignmentCenter);

    const REAL textX = card.X + 80;
    const REAL textW = card.Width - 80 - 150;
    Font nameFont(L"Microsoft YaHei", 15, FontStyleBold, UnitPixel);
    Font metaFont(L"Microsoft YaHei", 11, FontStyleRegular, UnitPixel);
    Font descFont(L"Microsoft YaHei", 11, FontStyleRegular, UnitPixel);

    DrawStringSimple(g, Ellipsis(item.name, 26), nameFont,
        RectF(textX, card.Y + 12, textW, 22), kText);

    std::wstring meta = L"v" + item.version;
    if (!item.author.empty()) meta += L"  ·  " + item.author;
    DrawStringSimple(g, Ellipsis(meta, 48), metaFont,
        RectF(textX, card.Y + 36, textW, 18), kMuted);

    // 类型胶囊
    if (!item.kind.empty()) {
        Font badgeFont(L"Microsoft YaHei", 10, FontStyleBold, UnitPixel);
        RectF badge(textX, card.Y + 60, 66, 20);
        ARGB badgeBg = item.builtin ? 0xFFF3E8FF : kAccentSoft;
        ARGB badgeFg = item.builtin ? 0xFF7C3AED : 0xFF2563EB;
        FillRounded(g, badge, 10.0f, badgeBg);
        DrawStringSimple(g, Ellipsis(item.kind, 8), badgeFont,
            badge, badgeFg, StringAlignmentCenter);
    }

    std::wstring desc = item.description.empty() ? L"没有可用描述" : item.description;
    DrawStringSimple(g, Ellipsis(desc, 40), descFont,
        RectF(textX + 74, card.Y + 60, textW - 74, 20), kMuted);

    const REAL right = card.X + card.Width - 10;

    // 小按钮：打开目录 / 重新加载
    if (!item.builtin) {
        if (item.canReload) {
            RectF reloadR(right - 118, card.Y + 34, 30, 28);
            bool hot = (hovered && m_hoverButton == 2);
            FillRounded(g, reloadR, 8.0f, hot ? kAccentSoft : 0x00000000);
            Font sym(L"Segoe UI Symbol", 12, FontStyleRegular, UnitPixel);
            DrawStringSimple(g, L"⟳", sym, reloadR,
                hot ? kAccent : kMuted, StringAlignmentCenter);
        }
        if (item.canOpenFolder) {
            RectF openR(right - 82, card.Y + 34, 30, 28);
            bool hot = (hovered && m_hoverButton == 1);
            FillRounded(g, openR, 8.0f, hot ? kAccentSoft : 0x00000000);
            Font sym(L"Segoe UI Emoji", 12, FontStyleRegular, UnitPixel);
            DrawStringSimple(g, L"📁", sym, openR,
                hot ? kAccent : kMuted, StringAlignmentCenter);
        }
    }

    // 开关
    if (item.canToggle) {
        RectF track(right - 48, card.Y + 34, 46, 26);
        bool hot = (hovered && m_hoverButton == 0);
        FillRounded(g, track, 13.0f,
            item.enabled ? (hot ? 0xFF2F72E8 : kAccent) : kOff);
        REAL knobX = item.enabled ? (track.X + track.Width - 22)
                                   : (track.X + 4);
        SolidBrush knob{ Color(0xFFFFFFFF) };
        g.FillEllipse(&knob, knobX, track.Y + 4, 18.0f, 18.0f);
        if (!item.enabled) {
            StrokeRounded(g, track, 13.0f, 0xFFE2E8F0);
        }
    }
}

void PluginCenterPanel::Paint(HDC hdc) {
    if (!m_hWnd || !IsWindow(m_hWnd)) return;
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    {
        Graphics g(memDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.SetCompositingQuality(CompositingQualityHighSpeed);

        SolidBrush bg{ Color(kBg) };
        g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);

        // 顶部计数
        Font headFont(L"Microsoft YaHei", 11, FontStyleRegular, UnitPixel);
        int total = (int)m_items.size();
        int enabled = 0;
        for (const auto& item : m_items) if (item.enabled) ++enabled;
        std::wstring summary = L"共 " + std::to_wstring(total) + L" 项 · 已启用 " +
            std::to_wstring(enabled);
        DrawStringSimple(g, summary, headFont,
            RectF(kPadX + 4, 2, rc.right - 100, 24), kMuted,
            StringAlignmentNear);

        if (m_items.empty()) {
            Font emptyFont(L"Microsoft YaHei", 13, FontStyleRegular, UnitPixel);
            DrawStringSimple(g, L"还没有安装插件，将 api_*.dll 放到 light.exe 目录即可自动识别。",
                emptyFont, RectF(20, 70, rc.right - 40, 28), kMuted,
                StringAlignmentCenter);
        }

        for (int i = 0; i < (int)m_items.size(); ++i) {
            int y = kHeaderH + i * (kCardH + kGap) - m_scrollY;
            if (y + kCardH < 0) continue;
            if (y > rc.bottom) break;
            DrawCard(&g, m_items[i], i, y);
        }

        DrawScrollbar(&g);
    }

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

LRESULT CALLBACK PluginCenterPanel::WndProc(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    PluginCenterPanel* self = (PluginCenterPanel*)GetWindowLongPtrW(
        hWnd, GWLP_USERDATA);

    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        self = (PluginCenterPanel*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->m_hWnd = hWnd;
        return TRUE;
    }

    if (!self) return DefWindowProcW(hWnd, msg, wParam, lParam);
    return self->HandleMessage(hWnd, msg, wParam, lParam);
}

LRESULT PluginCenterPanel::HandleMessage(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        Paint(hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_SIZE:
        ClampScroll();
        InvalidateAll();
        return 0;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        UpdateHover(x, y);
        return 0;
    }

    case WM_MOUSELEAVE:
        m_trackingLeave = false;
        m_hoverItem = -1;
        m_hoverButton = -1;
        InvalidateAll();
        return 0;

    case WM_LBUTTONDOWN: {
        SetCapture(hWnd);
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int dummy = -1;
        m_pressedButton = ButtonAt(x, y, dummy);
        m_pressedItem = dummy;
        return 0;
    }

    case WM_LBUTTONUP: {
        ReleaseCapture();
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int itemIndex = -1;
        int button = ButtonAt(x, y, itemIndex);
        if (button >= 0 && itemIndex == m_pressedItem &&
            button == m_pressedButton) {
            HandleClick(x, y);
        }
        m_pressedButton = -1;
        m_pressedItem = -1;
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int index = ItemAt(x, y);
        if (index >= 0 && index < (int)m_items.size() &&
            !m_items[index].builtin && m_items[index].canOpenFolder) {
            NotifyAction(index, L"open");
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int index = ItemAt(x, y);
        if (index >= 0) {
            POINT pt = { x, y };
            ClientToScreen(hWnd, &pt);
            ShowContextMenu(index, pt);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        ScrollBy(-delta / WHEEL_DELTA * 64);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_DOWN) { ScrollBy(32); return 0; }
        if (wParam == VK_UP)   { ScrollBy(-32); return 0; }
        if (wParam == VK_PRIOR) { ScrollBy(-VisibleHeight() + 40); return 0; }
        if (wParam == VK_NEXT)  { ScrollBy(VisibleHeight() - 40); return 0; }
        break;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;

    case WM_DESTROY:
        m_hWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
