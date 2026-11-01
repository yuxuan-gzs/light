// Theme.cpp
#include "Theme.h"

namespace vcb {
namespace theme {

Gdiplus::Color C(uint32_t argb) { return Gdiplus::Color(argb); }

uint32_t Blend(uint32_t base, uint32_t overlay) {
    int ao = (overlay >> 24) & 0xFF;
    int ab = (base >> 24) & 0xFF;
    int ar = (overlay >> 16) & 0xFF;
    int ag = (overlay >> 8) & 0xFF;
    int aab = (overlay) & 0xFF;
    int br = (base >> 16) & 0xFF;
    int bg = (base >> 8) & 0xFF;
    int bb = (base) & 0xFF;
    (void)ab; (void)aab;
    int rr = (ar * ao + br * (255 - ao)) / 255;
    int gg = (ag * ao + bg * (255 - ao)) / 255;
    int bbb = (aab * ao + bb * (255 - ao)) / 255;
    return 0xFF000000u | (rr << 16) | (gg << 8) | bbb;
}

void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float radius) {
    float d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

void FillRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const Gdiplus::Brush& b) {
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, r, radius);
    g.FillPath(&b, &path);
}

void DrawRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const Gdiplus::Pen& p, float penWidth) {
    Gdiplus::Color c;
    p.GetColor(&c);
    Gdiplus::Pen pen(c, penWidth);
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, r, radius);
    g.DrawPath(&pen, &path);
}

void DrawText(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font,
              Gdiplus::Color color, const Gdiplus::RectF& r,
              Gdiplus::StringAlignment halign, Gdiplus::StringAlignment valign) {
    if (text.empty()) return;
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(halign);
    fmt.SetLineAlignment(valign);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::SolidBrush br(color);
    g.DrawString(text.c_str(), (INT)text.size(), &font, r, &fmt, &br);
}

Gdiplus::RectF MeasureText(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font) {
    Gdiplus::RectF out;
    g.MeasureString(text.c_str(), (INT)text.size(), &font, Gdiplus::PointF(0, 0), &out);
    return out;
}

void DrawShadow(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, int alpha, int offsetY) {
    for (int i = 3; i >= 1; --i) {
        int a = alpha / 4 * (4 - i) + 2;
        Gdiplus::RectF rr(r.X + i, r.Y + offsetY + i, r.Width, r.Height);
        Gdiplus::SolidBrush br(Gdiplus::Color((BYTE)(a < 0 ? 0 : a), 0, 0, 0));
        FillRoundRect(g, rr, radius, br);
    }
}

void DrawGrid(Gdiplus::Graphics& g, const Gdiplus::RectF& r, int spacing) {
    Gdiplus::SolidBrush dot(C(color::GridDot));
    for (float y = r.Y; y < r.Y + r.Height; y += (float)spacing) {
        for (float x = r.X; x < r.X + r.Width; x += (float)spacing) {
            g.FillEllipse(&dot, x - 1.0f, y - 1.0f, 2.0f, 2.0f);
        }
    }
}

Gdiplus::Font* MakeFont(const wchar_t* family, float size, int style) {
    return new Gdiplus::Font(family, size, style, Gdiplus::UnitPixel);
}

} // namespace theme
} // namespace vcb
