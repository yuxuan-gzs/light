// UiPaint.cpp — 绘制：页面、控件、卡片网格、侧边栏、弹窗
#include "Ui.h"
#include "UiNav.h"
#include "Theme.h"
#include "Background.h"
#include "LauncherApp.h"
#include "LoaderIcons.h"
#include "Skin.h"

#include <windows.h>
#include <algorithm>
#include <cstring>
#include <map>

namespace ui {

using nav::CX;
using nav::Entry;
using nav::kCount;
using nav::kItems;
using nav::kSidebarW;
using nav::Kind;

// ---------------------------------------------------------------
// 图片缓存
// ---------------------------------------------------------------
static std::map<std::wstring, Gdiplus::Image*>& ImageCache() {
    static std::map<std::wstring, Gdiplus::Image*> m;
    return m;
}

static Gdiplus::Image* LoadImg(const std::wstring& path) {
    if (path.empty()) return nullptr;
    auto& cache = ImageCache();
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    Gdiplus::Image* img = Gdiplus::Image::FromFile(path.c_str());
    if (img && img->GetLastStatus() != Gdiplus::Ok) { delete img; img = nullptr; }
    cache[path] = img;
    return img;
}

void ClearImageCache() {
    for (auto& kv : ImageCache()) delete kv.second;
    ImageCache().clear();
}

// 缩放绘制（等比填充 + 居中裁切）
static void DrawImageCover(Gdiplus::Graphics& g, Gdiplus::Image* img, const Gdiplus::RectF& rc,
                           float radius) {
    if (!img) return;
    float iw = (float)img->GetWidth(), ih = (float)img->GetHeight();
    if (iw <= 0 || ih <= 0) return;
    float scale = std::max(rc.Width / iw, rc.Height / ih);
    float dw = iw * scale, dh = ih * scale;
    float dx = rc.X - (dw - rc.Width) / 2.0f;
    float dy = rc.Y - (dh - rc.Height) / 2.0f;

    Gdiplus::GraphicsState st = g.Save();
    Gdiplus::GraphicsPath* clip = theme::RoundRectPath(rc.X, rc.Y, rc.Width, rc.Height, radius);
    g.SetClip(clip);
    g.DrawImage(img, Gdiplus::RectF(dx, dy, dw, dh));
    g.Restore(st);
    delete clip;
}

// 内嵌的 HMCL 官方 PNG 图标（解码一次后缓存；IStream 必须保活到 Image 销毁）
static Gdiplus::Image* EmbeddedLoaderIcon(int type) {
    static Gdiplus::Image* cache[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
    static IStream* keepAlive[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
    if (type < 1 || type > 4) return nullptr;
    if (cache[type]) return cache[type];

    size_t n = 0;
    const unsigned char* png = icons::loaderPng(type, n);
    if (!png || n == 0) return nullptr;

    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!h) return nullptr;
    void* dst = GlobalLock(h);
    if (!dst) { GlobalFree(h); return nullptr; }
    memcpy(dst, png, n);
    GlobalUnlock(h);

    IStream* s = nullptr;
    if (FAILED(CreateStreamOnHGlobal(h, TRUE, &s)) || !s) { GlobalFree(h); return nullptr; }
    Gdiplus::Image* img = Gdiplus::Image::FromStream(s);
    if (img && img->GetLastStatus() != Gdiplus::Ok) { delete img; img = nullptr; }
    if (!img) { s->Release(); return nullptr; }
    keepAlive[type] = s;      // 故意不释放：GDI+ 可能在绘制时还会回读流
    cache[type] = img;
    return img;
}

// 加载器 logo（矢量绘制，形状对齐各家品牌；也支持用户在 cache\covers 放 loader_<n>.png 覆盖）
// 统一画成「居中正方形」：卡片 / chip 无论多宽，图标都不会被拉成色块横条。
static void DrawLoaderLogo(Gdiplus::Graphics& g, const Gdiplus::RectF& rcIn, int type) {
    if (type <= 0) return;
    if (rcIn.Width < 8 || rcIn.Height < 8) return;

    float side = std::min(rcIn.Width, rcIn.Height);
    if (side > 56.0f) side *= 0.84f;          // 大卡片留白，小 chip 顶满
    Gdiplus::RectF rc(rcIn.X + (rcIn.Width - side) / 2.0f,
                      rcIn.Y + (rcIn.Height - side) / 2.0f, side, side);
    float radius = side * 0.24f;

    // 1) 用户自备 logo 优先
    {
        std::wstring p = mlc::BaseDir() + L"\\cache\\covers\\loader_" +
                         std::to_wstring(type) + L".png";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            Gdiplus::Image* img = LoadImg(p);
            if (img) { DrawImageCover(g, img, rc, radius); return; }
        }
    }

    // 2) HMCL 官方 PNG（内嵌在 DLL 里，等比居中）
    if (Gdiplus::Image* png = EmbeddedLoaderIcon(type)) {
        float iw = (float)png->GetWidth(), ih = (float)png->GetHeight();
        if (iw > 0 && ih > 0) {
            float scale = std::min(rc.Width / iw, rc.Height / ih);
            float dw = iw * scale, dh = ih * scale;
            g.DrawImage(png, Gdiplus::RectF(rc.X + (rc.Width - dw) / 2.0f,
                                            rc.Y + (rc.Height - dh) / 2.0f, dw, dh));
            return;
        }
    }

    // 3) 兜底：程序化绘制
    auto X = [&](float t) { return rc.X + side * t; };
    auto Y = [&](float t) { return rc.Y + side * t; };

    if (type == 1) {
        // Fabric：米色底 + 线轴（上下两块木色挡板 + 中间斜绕的线）
        theme::FillRoundRect(g, rc, radius, Gdiplus::Color(255, 247, 241, 227));
        theme::StrokeRoundRect(g, rc, radius, Gdiplus::Color(255, 226, 214, 193), 1.0f);
        COLORREF wood = RGB(138, 106, 70);
        theme::FillRoundRect(g, Gdiplus::RectF(X(0.20f), Y(0.26f), side * 0.60f, side * 0.09f),
                             side * 0.04f, theme::C(wood, 255));
        theme::FillRoundRect(g, Gdiplus::RectF(X(0.20f), Y(0.65f), side * 0.60f, side * 0.09f),
                             side * 0.04f, theme::C(wood, 255));
        theme::FillRoundRect(g, Gdiplus::RectF(X(0.28f), Y(0.35f), side * 0.44f, side * 0.30f),
                             side * 0.03f, Gdiplus::Color(255, 214, 189, 148));
        Gdiplus::Pen thread(Gdiplus::Color(200, 120, 90, 58), std::max(1.4f, side * 0.045f));
        for (int i = 0; i < 3; ++i) {
            float t = 0.40f + 0.09f * i;
            g.DrawLine(&thread, X(0.29f), Y(t), X(0.71f), Y(t + 0.06f));
        }
        return;
    }

    if (type == 2) {
        // Quilt：紫色底 + 2×2 拼布 + 针脚
        theme::FillRoundRect(g, rc, radius, Gdiplus::Color(255, 139, 92, 246));
        float pad = side * 0.16f, gap = side * 0.05f;
        float cell = (side - pad * 2 - gap) / 2.0f;
        Gdiplus::Color white(255, 255, 255, 255);
        Gdiplus::Color lite(255, 233, 213, 255);
        theme::FillRoundRect(g, Gdiplus::RectF(X(0.16f), Y(0.16f), cell, cell), side*0.05f, white);
        theme::FillRoundRect(g, Gdiplus::RectF(X(0.16f) + cell + gap, Y(0.16f), cell, cell), side*0.05f, lite);
        theme::FillRoundRect(g, Gdiplus::RectF(X(0.16f), Y(0.16f) + cell + gap, cell, cell), side*0.05f, lite);
        theme::FillRoundRect(g, Gdiplus::RectF(X(0.16f) + cell + gap, Y(0.16f) + cell + gap, cell, cell), side*0.05f, white);
        return;
    }

    // Forge / NeoForge：铁砧
    const bool neo = (type == 4);
    theme::FillRoundRect(g, rc, radius, neo ? Gdiplus::Color(255, 234, 242, 251)
                                            : Gdiplus::Color(255, 255, 243, 228));
    theme::StrokeRoundRect(g, rc, radius,
                           neo ? Gdiplus::Color(255, 200, 220, 240)
                               : Gdiplus::Color(255, 240, 220, 198), 1.0f);
    COLORREF iron = neo ? RGB(44, 95, 148) : RGB(224, 118, 31);
    // 顶面（含左侧砧角）
    theme::FillRoundRect(g, Gdiplus::RectF(X(0.16f), Y(0.27f), side * 0.68f, side * 0.14f),
                         side * 0.05f, theme::C(iron, 255));
    Gdiplus::SolidBrush br(theme::C(iron, 255));
    Gdiplus::PointF horn[4] = {
        Gdiplus::PointF(X(0.16f), Y(0.41f)),
        Gdiplus::PointF(X(0.34f), Y(0.41f)),
        Gdiplus::PointF(X(0.30f), Y(0.50f)),
        Gdiplus::PointF(X(0.18f), Y(0.50f)) };
    g.FillPolygon(&br, horn, 4);
    // 颈 + 底座
    theme::FillRoundRect(g, Gdiplus::RectF(X(0.42f), Y(0.41f), side * 0.16f, side * 0.20f),
                         side * 0.03f, theme::C(iron, 255));
    theme::FillRoundRect(g, Gdiplus::RectF(X(0.26f), Y(0.61f), side * 0.48f, side * 0.13f),
                         side * 0.04f, theme::C(iron, 255));
    if (neo) {
        // NeoForge：底座下方一道青蓝高光
        theme::FillRoundRect(g, Gdiplus::RectF(X(0.26f), Y(0.76f), side * 0.48f, side * 0.06f),
                             side * 0.03f, Gdiplus::Color(255, 56, 189, 248));
    }
}

// ---------------------------------------------------------------
// 页面绘制
// ---------------------------------------------------------------
void Ui::Paint(HDC hdc, const RECT& client) {
    int w = client.right - client.left, h = client.bottom - client.top;
    if (w <= 0 || h <= 0) return;

    Gdiplus::Bitmap bmp(w, h, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    bg::Manager::Instance().Paint(g, Gdiplus::RectF(0, 0, (float)w, (float)h));
    DrawSidebar(g);

    switch (m_page) {
        case Home: PaintHome(g); break;
        case Versions: PaintVersions(g); break;
        case GameSelect: PaintGameSelect(g); break;
        case Download: PaintDownload(g); break;
        case Account: PaintAccount(g); break;
        case Settings: PaintSettings(g); break;
    }

    for (auto& wd : m_widgets) {
        if (wd.type == Widget::Combo && wd.dropped) DrawDropdown(g, wd);
    }

    DrawDownloadDialog(g);
    DrawToast(g);

    Gdiplus::Graphics out(hdc);
    out.DrawImage(&bmp, 0, 0);
}

void Ui::PaintHome(Gdiplus::Graphics& g) {
    // 当前实例背景卡
    Gdiplus::RectF card((float)CX - 12, (float)26, (float)(m_width - 32 - CX + 12), 262);
    theme::FillRoundRect(g, card, 16, Gdiplus::Color(230, 255, 255, 255));
    theme::StrokeRoundRect(g, card, 16, theme::C(theme::LINE_SOFT), 1.0f);
    for (auto& w : m_widgets) DrawWidget(g, w);
}

void Ui::PaintVersions(Gdiplus::Graphics& g) {
    for (auto& w : m_widgets) DrawWidget(g, w);
}

void Ui::PaintGameSelect(Gdiplus::Graphics& g) {
    for (auto& w : m_widgets) DrawWidget(g, w);
}

void Ui::PaintDownload(Gdiplus::Graphics& g) {
    if (m_addonShowDlg || m_addonShowChangelog) DrawAddonDialog(g);
    for (auto& w : m_widgets) DrawWidget(g, w);
}

void Ui::DrawAddonDialog(Gdiplus::Graphics& g) {
    Gdiplus::SolidBrush mask(Gdiplus::Color(110, 0, 0, 0));
    g.FillRectangle(&mask, Gdiplus::RectF(0, 0, (float)m_width, (float)m_height));
    int dw = std::min(880, m_width - 80), dh = std::min(620, m_height - 60);
    int dx = (m_width - dw) / 2, dy = (m_height - dh) / 2;
    Gdiplus::RectF card((float)dx, (float)dy, (float)dw, (float)dh);
    theme::FillRoundRect(g, card, 16, Gdiplus::Color(255, 255, 255, 255));
    theme::StrokeRoundRect(g, card, 16, theme::C(theme::LINE_SOFT), 1.0f);
}

void Ui::PaintAccount(Gdiplus::Graphics& g) {
    // 卡片高度跟着窗口走：皮肤区按钮到 y+520 左右，窗口小的时候也要能看全
    float cardH = (float)(m_height - 36);
    if (cardH < 470) cardH = 470;
    Gdiplus::RectF card((float)CX - 12, 18, (float)(m_width - 32 - CX + 12), cardH);
    theme::FillRoundRect(g, card, 16, Gdiplus::Color(235, 255, 255, 255));
    theme::StrokeRoundRect(g, card, 16, theme::C(theme::LINE_SOFT), 1.0f);

    // ---- 右侧皮肤预览框（选中账号；可拖动旋转 / 滚轮缩放）----
    if (!m_showOfflineDlg && m_skinBox.right > m_skinBox.left) {
        Gdiplus::RectF box((float)m_skinBox.left, (float)m_skinBox.top,
                           (float)(m_skinBox.right - m_skinBox.left),
                           (float)(m_skinBox.bottom - m_skinBox.top));
        theme::FillRoundRect(g, box, 14, Gdiplus::Color(255, 246, 248, 252));
        theme::StrokeRoundRect(g, box, 14, theme::C(theme::LINE_SOFT), 1.0f);

        mlc::Store& store = mlc::Store::I();
        std::wstring skinPath, capePath;
        bool slim = false;
        Widget* lst = Find(L"acc.list");
        int sel = lst ? lst->selIdx : -1;
        auto& accs = store.accounts();
        const mlc::AccountRec* target = nullptr;
        if (sel >= 0 && sel < (int)accs.size()) target = &accs[sel];
        else target = store.CurrentAccount();
        if (target) {
            skinPath = store.EffectiveSkin(*target);
            capePath = store.EffectiveCape(*target);
            slim = store.IsSlim(*target);
        }

        const theme::Fonts& fonts = theme::FontsCache();
        const skin::Model* model = skinPath.empty() ? nullptr : skin::Get(skinPath, slim);
        const skin::Model* cape = capePath.empty() ? nullptr : skin::GetCape(capePath);
        if (model) {
            Gdiplus::GraphicsState st = g.Save();
            Gdiplus::GraphicsPath* clip = theme::RoundRectPath(box.X + 2, box.Y + 2,
                                                               box.Width - 4, box.Height - 4, 12);
            g.SetClip(clip);
            skin::DrawCharacter(g, model, cape,
                                Gdiplus::RectF(box.X + 10, box.Y + 10, box.Width - 20,
                                               box.Height - 20),
                                m_skinYaw, m_skinPitch, m_skinZoom);
            g.Restore(st);
            delete clip;
            // 角标：模型 + 是否有披风
            {
                std::wstring tag = slim ? L"纤细 (slim)" : L"经典 (classic)";
                if (cape) tag += L" · 披风";
                theme::DrawTextLeft(g, tag, fonts.smallFont, theme::C(theme::MUTED),
                                    Gdiplus::RectF(box.X + 10, box.Y + 4, box.Width - 20, 18), 0);
            }
        } else {
            theme::DrawTextCenter(g, m_skinBusy ? L"正在从正版拉取皮肤…" : L"未设置皮肤",
                                  fonts.body, theme::C(theme::MUTED), box);
        }
    }

    for (auto& w : m_widgets) DrawWidget(g, w);
    DrawOfflineDialog(g);
}

// 创建离线账号的模态弹窗（遮罩 + 面板；控件由 BuildAccount 构建）
void Ui::DrawOfflineDialog(Gdiplus::Graphics& g) {
    if (!m_showOfflineDlg) return;
    Gdiplus::SolidBrush mask(Gdiplus::Color(130, 0, 0, 0));
    g.FillRectangle(&mask, Gdiplus::RectF(0, 0, (float)m_width, (float)m_height));

    int dw = 420, dh = 210;
    int dx = (m_width - dw) / 2, dy = (m_height - dh) / 2;
    Gdiplus::RectF card((float)dx, (float)dy, (float)dw, (float)dh);
    theme::FillRoundRect(g, card, 16, Gdiplus::Color(255, 255, 255, 255));
    theme::StrokeRoundRect(g, card, 16, theme::C(theme::LINE_SOFT), 1.0f);
}

void Ui::PaintSettings(Gdiplus::Graphics& g) {
    for (auto& w : m_widgets) DrawWidget(g, w);
}

// ---------------------------------------------------------------
// 侧边栏
// ---------------------------------------------------------------
void Ui::DrawSidebar(Gdiplus::Graphics& g) {
    Gdiplus::SolidBrush sb(theme::C(theme::BG, 200));
    g.FillRectangle(&sb, Gdiplus::RectF(0, 0, (float)kSidebarW, (float)m_height));
    Gdiplus::Pen divPen(theme::C(theme::LINE_SOFT), 1.0f);
    g.DrawLine(&divPen, (float)(kSidebarW - 1), 0.0f, (float)(kSidebarW - 1), (float)m_height);

    const theme::Fonts& fonts = theme::FontsCache();
    Gdiplus::Font emojiFont(L"Segoe UI Emoji", 14, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    launcher::App& app = launcher::App::Instance();
    mlc::Store& store = mlc::Store::I();
    auto acc = app.account();

    for (int i = 0; i < kCount; ++i) {
        const Entry& e = kItems[i];
        if (e.kind == Kind::Header) {
            theme::DrawTextLeft(g, e.label, fonts.smallFont, theme::C(theme::MUTED),
                                Gdiplus::RectF(18, (float)e.y, 180, (float)e.h), 0);
            continue;
        }
        if (e.kind == Kind::Account) {
            Gdiplus::RectF avatar(18, (float)e.y, 40, 40);
            // 有皮肤就用皮肤头像，否则用首字圆形占位
            std::string skinFile;
            if (mlc::AccountRec* ca = store.CurrentAccount()) skinFile = store.SkinFileOf(*ca);
            const skin::Model* sm = skinFile.empty() ? nullptr : skin::Get(mlc::Utf8ToW(skinFile));
            if (sm) {
                skin::DrawAvatar(g, sm, avatar, 10);
            } else {
                theme::FillRoundRect(g, avatar, 20, theme::C(theme::ACCENT, 255));
                std::wstring initial = L"未";
                if (!acc.name.empty()) {
                    std::wstring nm = mlc::Utf8ToW(acc.name);
                    initial = nm.empty() ? L"?" : nm.substr(0, 1);
                }
                theme::DrawTextCenter(g, initial, fonts.head,
                                      Gdiplus::Color(255, 255, 255, 255), avatar);
            }
            std::wstring name = acc.name.empty() ? L"未登录账号" : mlc::Utf8ToW(acc.name);
            theme::DrawTextLeft(g, name, fonts.head, theme::C(theme::TEXT),
                                Gdiplus::RectF(66, (float)e.y + 2, 140, 22), 0);
            theme::DrawTextLeft(g, app.accountType() == "msa" ? L"微软正版" : L"离线账号",
                                fonts.smallFont, theme::C(theme::MUTED),
                                Gdiplus::RectF(66, (float)e.y + 22, 140, 18), 0);
            continue;
        }
        if (e.kind == Kind::Current) {
            Gdiplus::RectF rc(12, (float)e.y, 198, (float)e.h);
            bool active = (m_page == Home);
            theme::FillRoundRect(g, rc, 12, active ? theme::C(theme::ACCENT, 46)
                                                   : theme::C(theme::CARD2, 120));
            mlc::Instance* cur = store.Current();
            std::wstring nm = cur ? mlc::Utf8ToW(cur->name) : L"未选择游戏";
            std::wstring ver = cur ? mlc::Utf8ToW(cur->versionId) : L"点「版本选择」添加";
            theme::DrawTextCenter(g, m_running ? L"▶" : L"■", &emojiFont,
                m_running ? theme::C(theme::GREEN) : theme::C(theme::MUTED),
                Gdiplus::RectF(rc.X + 10, rc.Y, 22, rc.Height));
            theme::DrawTextLeft(g, nm, fonts.head,
                active ? theme::C(theme::ACCENT) : theme::C(theme::TEXT),
                Gdiplus::RectF(rc.X + 36, rc.Y + 4, rc.Width - 46, 22), 0);
            theme::DrawTextLeft(g, ver, fonts.smallFont, theme::C(theme::MUTED),
                Gdiplus::RectF(rc.X + 36, rc.Y + 26, rc.Width - 46, 18), 0);
            continue;
        }
        // 普通项
        Gdiplus::RectF rc(12, (float)e.y, 198, (float)e.h);
        bool active = (m_page == e.page) &&
                      !(e.page == Versions && e.tab >= 0 && m_verTab != e.tab);
        if (active) theme::FillRoundRect(g, rc, 10, theme::C(theme::ACCENT, 255));
        Gdiplus::Color fg = active ? Gdiplus::Color(255, 255, 255, 255) : theme::C(theme::TEXT);
        Gdiplus::RectF dot(rc.X + 14, rc.Y + rc.Height / 2 - 3, 6, 6);
        if (active) theme::FillRoundRect(g, dot, 3, Gdiplus::Color(255, 255, 255, 255));
        else theme::StrokeRoundRect(g, dot, 3, theme::C(theme::LINE), 1.2f);
        theme::DrawTextLeft(g, e.label, active ? fonts.head : fonts.body, fg,
                            Gdiplus::RectF(rc.X + 28, rc.Y, rc.Width - 34, rc.Height), 0);
    }

    theme::DrawTextCenter(g, L"YXG Minecraft Launcher", fonts.smallFont, theme::C(theme::MUTED),
                          Gdiplus::RectF(0, (float)m_height - 34, 220, 20));
}

// ---------------------------------------------------------------
// 控件
// ---------------------------------------------------------------
void Ui::DrawWidget(Gdiplus::Graphics& g, const Widget& w) {
    const theme::Fonts& fonts = theme::FontsCache();
    Gdiplus::RectF rc((float)w.rc.left, (float)w.rc.top,
                      (float)(w.rc.right - w.rc.left), (float)(w.rc.bottom - w.rc.top));

    switch (w.type) {
    case Widget::Label: {
        Gdiplus::Font* f = w.style >= 2 ? fonts.title : (w.style == 1 ? fonts.head : fonts.body);
        Gdiplus::Color c = w.color ? theme::C(w.color)
                                   : (w.style >= 1 ? theme::C(theme::TEXT)
                                                   : theme::C(theme::MUTED));
        theme::DrawTextLeft(g, w.text, f, c, rc, 2);
        break;
    }
    case Widget::Btn: {
        COLORREF base = w.accent ? w.accent : theme::CARD2;
        if (w.accent == 0 && w.active) base = theme::ACCENT;
        COLORREF col = base;
        if (w.pressed) col = theme::Darken(base, 24);
        else if (w.hover) col = theme::Lighten(base, 14);
        theme::FillRoundRect(g, rc, 10, theme::C(col, 255));
        int lum = (GetRValue(col) * 299 + GetGValue(col) * 587 + GetBValue(col) * 114) / 1000;
        Gdiplus::Color tc = (lum > 152) ? theme::C(theme::TEXT)
                                        : Gdiplus::Color(255, 255, 255, 255);
        theme::DrawTextCenter(g, w.text, fonts.body, tc, rc);
        break;
    }
    case Widget::Edit: {
        theme::FillRoundRect(g, rc, 9, Gdiplus::Color(250, 255, 255, 255));
        theme::StrokeRoundRect(g, rc, 9, theme::C(w.focused ? theme::ACCENT : theme::LINE),
                               w.focused ? 1.6f : 1.0f);
        std::wstring t = w.editText;
        Gdiplus::Color c = theme::C(theme::TEXT);
        if (t.empty() && !w.placeholder.empty()) { t = w.placeholder; c = Gdiplus::Color(255, 150, 152, 165); }
        theme::DrawTextLeft(g, t, fonts.body, c, rc, 10);
        break;
    }
    case Widget::Combo: {
        theme::FillRoundRect(g, rc, 9, Gdiplus::Color(250, 255, 255, 255));
        theme::StrokeRoundRect(g, rc, 9, theme::C(w.dropped ? theme::ACCENT : theme::LINE), 1.0f);
        std::wstring t = (w.sel >= 0 && w.sel < (int)w.items.size()) ? w.items[w.sel]
                                                                     : (w.text.empty() ? L"" : w.text);
        theme::DrawTextLeft(g, t, fonts.body, theme::C(theme::TEXT), rc, 10);
        theme::DrawTextRight(g, L"▼", fonts.smallFont, theme::C(theme::MUTED), rc, 10);
        break;
    }
    case Widget::List: {
        theme::FillRoundRect(g, rc, 10, Gdiplus::Color(245, 255, 255, 255));
        theme::StrokeRoundRect(g, rc, 10, theme::C(theme::LINE_SOFT), 1.0f);
        Gdiplus::GraphicsState st = g.Save();
        g.SetClip(Gdiplus::RectF(rc.X, rc.Y, rc.Width, rc.Height));
        int rowH = 34;
        int visible = (int)(rc.Height / rowH);
        int start = std::max(0, w.scroll);
        const bool hasIcons = !w.rowIcons.empty();
        for (int i = start; i < (int)w.listItems.size() && i < start + visible; ++i) {
            float yy = rc.Y + (float)((i - start) * rowH);
            Gdiplus::RectF ir(rc.X, yy, rc.Width, (float)rowH);
            if (i == w.selIdx) theme::FillRoundRect(g, ir, 6, theme::C(theme::ACCENT, 40));
            else if (i == w.hot) theme::FillRoundRect(g, ir, 6, theme::C(theme::CARD2, 90));

            float textX = ir.X + 12;
            if (hasIcons) {
                std::wstring path = (i < (int)w.rowIcons.size()) ? w.rowIcons[i] : L"";
                Gdiplus::RectF ar(ir.X + 8, ir.Y + 5, 24, 24);
                const skin::Model* m = path.empty() ? nullptr : skin::Get(path);
                if (m) {
                    skin::DrawAvatar(g, m, ar, 6);
                } else {
                    theme::FillRoundRect(g, ar, 6, theme::C(theme::CARD2, 255));
                    std::wstring initial = w.listItems[i].empty() ? L"?" : w.listItems[i].substr(0, 1);
                    theme::DrawTextCenter(g, initial, fonts.smallFont, theme::C(theme::MUTED), ar);
                }
                textX = ir.X + 40;
            }

            Gdiplus::Color tc = (i == w.selIdx) ? theme::C(theme::ACCENT) : theme::C(theme::TEXT);
            theme::DrawTextLeft(g, w.listItems[i], fonts.body, tc,
                                Gdiplus::RectF(textX, ir.Y, ir.X + ir.Width - textX - 110,
                                               ir.Height), 0);
            if (i < (int)w.subItems.size()) {
                theme::DrawTextRight(g, w.subItems[i], fonts.smallFont, theme::C(theme::MUTED),
                                     ir, 12);
            }
        }
        g.Restore(st);
        break;
    }
    case Widget::Panel:
        theme::FillRoundRect(g, rc, 12, Gdiplus::Color(250, 255, 255, 255));
        theme::StrokeRoundRect(g, rc, 12, theme::C(theme::LINE_SOFT), 1.0f);
        break;
    case Widget::Progress: {
        theme::FillRoundRect(g, rc, 5, theme::C(theme::LINE_SOFT, 255));
        float pw = rc.Width * std::clamp(w.progress, 0.0f, 1.0f);
        if (pw > 2) theme::FillRoundRect(g, Gdiplus::RectF(rc.X, rc.Y, pw, rc.Height), 5,
                                         theme::C(theme::ACCENT, 255));
        break;
    }
    case Widget::Slider: {
        float ratio = (w.vmax > w.vmin) ? (float)(w.value - w.vmin) / (float)(w.vmax - w.vmin) : 0;
        float cy = rc.Y + rc.Height / 2;
        theme::FillRoundRect(g, Gdiplus::RectF(rc.X, cy - 3, rc.Width, 6), 3,
                             theme::C(theme::LINE, 255));
        float filled = rc.Width * std::clamp(ratio, 0.0f, 1.0f);
        if (filled > 0) theme::FillRoundRect(g, Gdiplus::RectF(rc.X, cy - 3, filled, 6), 3,
                                             theme::C(theme::ACCENT, 255));
        theme::FillRoundRect(g, Gdiplus::RectF(rc.X + filled - 7, cy - 9, 14, 18), 7,
                             Gdiplus::Color(255, 255, 255, 255));
        break;
    }
    case Widget::Check: {
        float s = 18;
        Gdiplus::RectF box(rc.X, rc.Y + (rc.Height - s) / 2, s, s);
        theme::FillRoundRect(g, box, 5, Gdiplus::Color(250, 255, 255, 255));
        theme::StrokeRoundRect(g, box, 5, theme::C(w.checked ? theme::ACCENT : theme::LINE), 1.2f);
        if (w.checked) {
            theme::FillRoundRect(g, Gdiplus::RectF(box.X + 4, box.Y + 4, s - 8, s - 8), 3,
                                 theme::C(theme::ACCENT, 255));
        }
        theme::DrawTextLeft(g, w.text, fonts.body, theme::C(theme::TEXT),
                            Gdiplus::RectF(rc.X + s + 10, rc.Y, rc.Width - s - 10, rc.Height), 0);
        break;
    }
    case Widget::Grid:
        DrawGrid(g, w);
        break;
    case Widget::Rows:
        DrawRows(g, w);
        break;
    default: break;
    }
}

void Ui::DrawGrid(Gdiplus::Graphics& g, const Widget& w) {
    Gdiplus::RectF rc((float)w.rc.left, (float)w.rc.top,
                      (float)(w.rc.right - w.rc.left), (float)(w.rc.bottom - w.rc.top));
    Gdiplus::GraphicsState st = g.Save();
    g.SetClip(rc);

    int cols = std::max(1, w.cols);
    int rowH = w.cardH + w.gap;
    int total = (int)w.items2.size();
    int startRow = std::max(0, w.gscroll);
    int visibleRows = (int)(rc.Height / rowH) + 1;

    for (int r = startRow; r < startRow + visibleRows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            if (idx >= total) break;
            float cx = rc.X + (float)(c * (w.cardW + w.gap));
            float cy = rc.Y + (float)((r - startRow) * rowH);
            Gdiplus::RectF card(cx, cy, (float)w.cardW, (float)w.cardH);
            DrawCard(g, card, w.items2[idx], idx == w.hotIdx, idx == w.selIdx);
        }
    }
    g.Restore(st);

    // 滚动条
    int rows = (total + cols - 1) / cols;
    if (rows > visibleRows && visibleRows > 0) {
        float trackH = rc.Height;
        float barH = std::max(24.0f, trackH * (float)visibleRows / (float)rows);
        float maxScroll = (float)std::max(1, rows - visibleRows);
        float y = rc.Y + (trackH - barH) * ((float)w.gscroll / maxScroll);
        theme::FillRoundRect(g, Gdiplus::RectF(rc.X + rc.Width - 5, y, 4, barH), 2,
                             theme::C(theme::LINE, 160));
    }
}

// ---------------------------------------------------------------
// 平铺列表（HMCL 式整行）：图标 + 标题 + 副标题 + 角标 + 操作按钮
// ---------------------------------------------------------------
void Ui::DrawRows(Gdiplus::Graphics& g, const Widget& w) {
    const theme::Fonts& fonts = theme::FontsCache();
    Gdiplus::RectF rc((float)w.rc.left, (float)w.rc.top,
                      (float)(w.rc.right - w.rc.left), (float)(w.rc.bottom - w.rc.top));
    if (rc.Width < 40 || rc.Height < 20) return;

    const int rowH = std::max(28, w.rowH);
    const int total = (int)w.items2.size();
    const int start = std::max(0, w.gscroll);
    const int visible = (int)(rc.Height / rowH) + 1;

    Gdiplus::GraphicsState st = g.Save();
    g.SetClip(rc);

    for (int i = start; i < total && i < start + visible; ++i) {
        const GridItem& it = w.items2[i];

        Gdiplus::RectF row(rc.X, rc.Y + (float)((i - start) * rowH), rc.Width - 6.0f,
                           (float)rowH - 4.0f);

        // HMCL ComponentSublist 的分组标题
        if (it.section) {
            theme::FillRoundRect(g, row, 8, theme::C(theme::ACCENT, 26));
            theme::DrawTextLeft(g, it.title, fonts.head, theme::C(theme::ACCENT),
                                Gdiplus::RectF(row.X + 14, row.Y, row.Width - 28, row.Height), 0);
            continue;
        }

        const bool sel = (i == w.selIdx);
        const bool hot = (i == w.hotIdx);

        theme::FillRoundRect(g, row, 10, sel ? Gdiplus::Color(255, 232, 244, 233)
                                             : Gdiplus::Color(255, 252, 253, 255));
        if (sel) theme::StrokeRoundRect(g, row, 10, theme::C(theme::ACCENT, 255), 1.6f);
        else theme::StrokeRoundRect(g, row, 10, theme::C(theme::LINE_SOFT, 255), 1.0f);
        if (hot && !sel) theme::FillRoundRect(g, row, 10, Gdiplus::Color(14, 0, 0, 0));

        // 左侧图标（HMCL TwoLineListItem / ImageContainer(40)）
        const float iconS = std::min(40.0f, (float)rowH - 14.0f);
        Gdiplus::RectF icon(row.X + 10, row.Y + (row.Height - iconS) / 2.0f, iconS, iconS);
        Gdiplus::Image* img = LoadImg(it.cover);
        if (img) {
            DrawImageCover(g, img, icon, iconS * 0.28f);
        } else if (it.loaderIcon > 0) {
            theme::FillRoundRect(g, icon, iconS * 0.24f, Gdiplus::Color(255, 246, 247, 250));
            DrawLoaderLogo(g, icon, it.loaderIcon);
        } else {
            COLORREF tint = it.tint ? it.tint : theme::ACCENT;
            theme::FillRoundRect(g, icon, iconS * 0.24f, theme::C(tint, 255));
            std::wstring initial = it.title.empty() ? L"?" : it.title.substr(0, 1);
            theme::DrawTextCenter(g, initial, fonts.head,
                                  Gdiplus::Color(255, 255, 255, 255), icon);
        }

        // 右侧：角标 + 操作按钮
        const float btnW = it.action.empty() ? 0.0f : 74.0f;
        const float btnH = 26.0f;
        Gdiplus::RectF btn(row.X + row.Width - btnW - 8, row.Y + (row.Height - btnH) / 2,
                           btnW, btnH);
        float badgeRight = it.action.empty() ? (row.X + row.Width - 12) : (btn.X - 8);
        if (!it.badge.empty()) {
            float bw = std::min(140.0f, 14.0f + (float)it.badge.size() * 11.0f);
            Gdiplus::RectF br(badgeRight - bw, row.Y + (row.Height - 20) / 2, bw, 20);
            theme::FillRoundRect(g, br, 10, theme::C(it.tint ? it.tint : theme::ACCENT, 46));
            theme::DrawTextCenter(g, it.badge, fonts.smallFont,
                                  theme::C(theme::Darken(it.tint ? it.tint : theme::ACCENT, 30)),
                                  br);
            badgeRight = br.X - 8;
        }

        // 文本：标题 + HMCL 式类别/渠道标签，第二行是简介
        const float textX = icon.X + iconS + 12.0f;
        const float textW = std::max(40.0f, badgeRight - textX - 6.0f);
        theme::DrawTextLeft(g, it.title, fonts.head, theme::C(theme::TEXT),
                            Gdiplus::RectF(textX, row.Y + 6, textW, 22), 0);

        if (!it.tags.empty()) {
            Gdiplus::RectF measured;
            g.MeasureString(it.title.c_str(), -1, fonts.head,
                            Gdiplus::PointF(textX, row.Y), &measured);
            float tx = textX + measured.Width + 12.0f;
            const COLORREF tint = it.tagColor ? it.tagColor : theme::ACCENT;
            for (size_t k = 0; k < it.tags.size() && k < 5; ++k) {
                const std::wstring& tag = it.tags[k];
                float tw = 14.0f;
                for (wchar_t c : tag) tw += (c > 255) ? 12.0f : 7.0f;
                if (tx + tw > badgeRight) break;
                Gdiplus::RectF tr(tx, row.Y + 8, tw, 18);
                theme::FillRoundRect(g, tr, 9, theme::C(tint, 42));
                theme::DrawTextCenter(g, tag, fonts.smallFont,
                                      theme::C(theme::Darken(tint, 35)), tr);
                tx += tw + 6.0f;
            }
        }
        theme::DrawTextLeft(g, it.subtitle, fonts.smallFont, theme::C(theme::MUTED),
                            Gdiplus::RectF(textX, row.Y + 29, textW, 19), 0);

        if (!it.action.empty()) {
            theme::FillRoundRect(g, btn, 8,
                                 theme::C(it.actionDanger ? theme::RED : theme::ACCENT, 235));
            theme::DrawTextCenter(g, it.action, fonts.smallFont,
                                  Gdiplus::Color(255, 255, 255, 255), btn);
        }
    }
    g.Restore(st);

    // 滚动条
    if (total > visible - 1 && visible > 0) {
        int rowsTotal = total;
        float trackH = rc.Height;
        float barH = std::max(28.0f, trackH * (float)visible / (float)rowsTotal);
        float maxScroll = (float)std::max(1, rowsTotal - visible + 1);
        float y = rc.Y + (trackH - barH) * std::min(1.0f, (float)w.gscroll / maxScroll);
        theme::FillRoundRect(g, Gdiplus::RectF(rc.X + rc.Width - 5, y, 4, barH), 2,
                             theme::C(theme::LINE, 160));
    }
}

void Ui::DrawCard(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, const GridItem& it,
                  bool hot, bool sel) {    const theme::Fonts& fonts = theme::FontsCache();

    // 紧凑模式（小方块，如加载器选择）：只画封面 + 名字
    const bool compact = rc.Height < 140;

    theme::FillRoundRect(g, rc, 14, Gdiplus::Color(252, 255, 255, 255));
    theme::StrokeRoundRect(g, rc, 14, theme::C(sel ? theme::ACCENT : theme::LINE_SOFT),
                           sel ? 2.0f : 1.0f);
    if (hot) theme::FillRoundRect(g, rc, 14, Gdiplus::Color(12, 0, 0, 0));

    if (compact) {
        Gdiplus::RectF cover(rc.X + 4, rc.Y + 4, rc.Width - 8, rc.Height - 22);
        Gdiplus::Image* img = LoadImg(it.cover);
        if (img) DrawImageCover(g, img, cover, 10);
        else if (it.loaderIcon > 0) DrawLoaderLogo(g, cover, it.loaderIcon);
        else theme::FillRoundRect(g, cover, 10, theme::C(it.tint ? it.tint : theme::ACCENT, 255));
        theme::DrawTextCenter(g, it.title, fonts.smallFont,
                              theme::C(sel ? theme::ACCENT : theme::TEXT),
                              Gdiplus::RectF(rc.X, rc.Y + rc.Height - 20, rc.Width, 18));
        return;
    }

    // 封面
    float pad = 10;
    float coverH = rc.Height - 92;
    Gdiplus::RectF cover(rc.X + pad, rc.Y + pad, rc.Width - pad * 2, coverH);
    Gdiplus::Image* img = LoadImg(it.cover);
    if (img) {
        DrawImageCover(g, img, cover, 10);
    } else if (it.loaderIcon > 0) {
        // 加载器 logo：淡淡的地板 + 居中图标（不再铺满整张封面）
        theme::FillRoundRect(g, cover, 10, Gdiplus::Color(255, 246, 247, 250));
        DrawLoaderLogo(g, cover, it.loaderIcon);
    } else {
        COLORREF tint = it.tint ? it.tint : theme::ACCENT;
        theme::FillRoundRect(g, cover, 10, theme::C(theme::Lighten(tint, 30), 255));
        // 占位图案：斜纹
        Gdiplus::Pen pen(Gdiplus::Color(46, 255, 255, 255), 8.0f);
        for (float x = cover.X - cover.Height; x < cover.X + cover.Width; x += 46)
            g.DrawLine(&pen, x, cover.Y + cover.Height, x + cover.Height, cover.Y);
        std::wstring initial = it.title.empty() ? L"?" : it.title.substr(0, 1);
        Gdiplus::Font f(L"Microsoft YaHei UI", 30, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        theme::DrawTextCenter(g, initial, &f, Gdiplus::Color(230, 255, 255, 255), cover);
    }

    // 角标
    if (!it.badge.empty()) {
        float bw = std::min(cover.Width - 12, 26.0f + (float)it.badge.size() * 12.0f);
        Gdiplus::RectF br(cover.X + cover.Width - bw - 6, cover.Y + 6, bw, 20);
        theme::FillRoundRect(g, br, 10, Gdiplus::Color(190, 20, 20, 26));
        theme::DrawTextCenter(g, it.badge, fonts.smallFont,
                              Gdiplus::Color(255, 255, 255, 255), br);
    }

    // 标题 / 副标题
    theme::DrawTextLeft(g, it.title, fonts.head, theme::C(theme::TEXT),
        Gdiplus::RectF(rc.X + pad, cover.Y + cover.Height + 6, rc.Width - pad * 2, 22), 2);
    theme::DrawTextLeft(g, it.subtitle, fonts.smallFont,
        theme::C(it.on ? theme::MUTED : theme::LINE),
        Gdiplus::RectF(rc.X + pad, cover.Y + cover.Height + 28, rc.Width - pad * 2, 18), 2);

    // 底部交互
    float by = rc.Y + rc.Height - 28;
    if (it.toggleable) {
        Gdiplus::RectF sw(rc.X + rc.Width - 46, by + 2, 36, 18);
        // 用固定绿色，避免壁纸主题色过浅导致看不清
        theme::FillRoundRect(g, sw, 9, theme::C(it.on ? RGB(76, 175, 80) : theme::LINE, 255));
        float kx = it.on ? sw.X + sw.Width - 16 : sw.X + 2;
        theme::FillRoundRect(g, Gdiplus::RectF(kx, sw.Y + 2, 14, 14), 7,
                             Gdiplus::Color(255, 255, 255, 255));
        theme::DrawTextLeft(g, it.on ? L"已启用" : L"已停用", fonts.smallFont,
                            theme::C(it.on ? RGB(46, 125, 50) : theme::MUTED),
                            Gdiplus::RectF(rc.X + pad, by, rc.Width - 56, 22), 2);
    } else if (!it.action.empty()) {
        Gdiplus::RectF btn(rc.X + rc.Width - 78, by, 68, 24);
        theme::FillRoundRect(g, btn, 8, theme::C(it.actionDanger ? theme::RED : theme::ACCENT,
                                                 235));
        theme::DrawTextCenter(g, it.action, fonts.smallFont,
                              Gdiplus::Color(255, 255, 255, 255), btn);
    }
}

void Ui::DrawDropdown(Gdiplus::Graphics& g, const Widget& w) {
    const theme::Fonts& fonts = theme::FontsCache();
    Gdiplus::RectF rc((float)w.rc.left, (float)w.rc.top,
                      (float)(w.rc.right - w.rc.left), (float)(w.rc.bottom - w.rc.top));
    float ih = 30.0f * (float)w.items.size();
    // 超过可视高度时限制
    float maxH = (float)(m_height - w.rc.top - 50);
    if (ih > maxH) ih = maxH;
    Gdiplus::RectF dr(rc.X, rc.Y + rc.Height + 2, rc.Width, ih);
    theme::FillRoundRect(g, dr, 9, Gdiplus::Color(255, 255, 255, 255));
    theme::StrokeRoundRect(g, dr, 9, theme::C(theme::LINE), 1.0f);
    Gdiplus::GraphicsState st = g.Save();
    g.SetClip(dr);
    int n = std::min((int)w.items.size(), (int)(ih / 30));
    for (int i = 0; i < n; ++i) {
        Gdiplus::RectF ir(dr.X, dr.Y + i * 30.0f, dr.Width, 30);
        if (i == w.sel) theme::FillRoundRect(g, ir, 6, theme::C(theme::ACCENT, 32));
        theme::DrawTextLeft(g, w.items[i], fonts.body,
            i == w.sel ? theme::C(theme::ACCENT) : theme::C(theme::TEXT), ir, 10);
    }
    g.Restore(st);
}

// ---------------------------------------------------------------
// 下载进度弹窗
// ---------------------------------------------------------------
void Ui::DrawDownloadDialog(Gdiplus::Graphics& g) {
    if (!m_showDlDialog) return;
    const theme::Fonts& fonts = theme::FontsCache();

    Gdiplus::SolidBrush mask(Gdiplus::Color(120, 0, 0, 0));
    g.FillRectangle(&mask, Gdiplus::RectF(0, 0, (float)m_width, (float)m_height));

    int cw = 520, chh = 260;
    int cx = (m_width - cw) / 2, cy = (m_height - chh) / 2;

    Gdiplus::RectF card((float)cx, (float)cy, (float)cw, (float)chh);
    theme::FillRoundRect(g, card, 16, Gdiplus::Color(255, 255, 255, 255));
    theme::StrokeRoundRect(g, card, 16, theme::C(theme::LINE_SOFT), 1.0f);

    theme::DrawTextLeft(g, L"正在下载 " + m_dlDialogTitle, fonts.head, theme::C(theme::TEXT),
        Gdiplus::RectF((float)cx + 24, (float)cy + 18, (float)cw - 48, 28), 0);

    Gdiplus::RectF bar((float)cx + 24, (float)cy + 74, (float)(cw - 48), 12);
    theme::FillRoundRect(g, bar, 6, theme::C(theme::LINE_SOFT, 255));
    float fw = bar.Width * std::clamp(m_progress, 0.0f, 1.0f);
    if (fw > 1.0f) {
        theme::FillRoundRect(g, Gdiplus::RectF(bar.X, bar.Y, fw, bar.Height), 6,
                             theme::C(m_installing ? theme::ACCENT : theme::GREEN));
    }
    theme::DrawTextLeft(g, std::to_wstring((int)(m_progress * 100)) + L"%", fonts.body,
                        theme::C(theme::MUTED),
        Gdiplus::RectF((float)cx + 24, (float)cy + 92, 80, 20), 0);
    theme::DrawTextLeft(g, m_progressText, fonts.body, theme::C(theme::MUTED),
        Gdiplus::RectF((float)cx + 24, (float)cy + 112, (float)cw - 48, 70), 0);

    int btw = 120, bth = 40;
    Gdiplus::RectF btn((float)(cx + cw - btw - 24), (float)(cy + chh - bth - 18),
                       (float)btw, (float)bth);
    theme::FillRoundRect(g, btn, 10, theme::C(m_installing ? theme::RED : theme::ACCENT));
    theme::DrawTextCenter(g, m_installing ? L"取消" : L"关闭", fonts.body,
                          Gdiplus::Color(255, 255, 255, 255), btn);
}

// ---------------------------------------------------------------
// 提示条
// ---------------------------------------------------------------
void Ui::DrawToast(Gdiplus::Graphics& g) {
    if (m_toast.empty()) return;
    const theme::Fonts& fonts = theme::FontsCache();
    float tw = std::min((float)m_width - 80, 26.0f + (float)m_toast.size() * 12.0f);
    Gdiplus::RectF rc((m_width - tw) / 2.0f, (float)m_height - 56, tw, 34);
    theme::FillRoundRect(g, rc, 17, theme::C(m_toastError ? theme::RED : RGB(40, 42, 50), 235));
    theme::DrawTextCenter(g, m_toast, fonts.body, Gdiplus::Color(255, 255, 255, 255), rc);
}

} // namespace ui
