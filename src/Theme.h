// Theme.h — HMCL 风格主题（Monet 蓝紫色 Material Design 3）+ GDI+ 绘制辅助
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")

namespace theme {

// ---- 颜色（RGB，HMCL blue.css Monet 蓝紫配色） ----
// 中性 surface 色（浅色主题下固定，不随壁纸主题色变化）
constexpr COLORREF BG        = RGB(251, 248, 255);  // #FBF8FF surface/background
constexpr COLORREF CARD      = RGB(255, 255, 255);  // #FFFFFF surface-container-lowest
constexpr COLORREF CARD2     = RGB(239, 237, 245);  // #EFEDF5 surface-container
constexpr COLORREF RED       = RGB(186, 26, 26);    // #BA1A1A error
constexpr COLORREF TEXT      = RGB(27, 27, 33);     // #1B1B21 on-surface
constexpr COLORREF MUTED     = RGB(69, 70, 81);     // #454651 on-surface-variant
constexpr COLORREF LINE      = RGB(198, 197, 211);  // #C6C5D3 outline-variant
constexpr COLORREF LINE_SOFT = RGB(233, 231, 239);  // #E9E7EF surface-container-high

// ---- 主题色（随壁纸 seed 动态变化，见 SetSeed） ----
// 这些是运行时全局变量（定义在 Theme.cpp），HMCL 中由 MonetFX 从壁纸 seed 生成。
extern COLORREF ACCENT;      // primary（选中态/强调）
extern COLORREF ACCENT_DK;   // primary 深色（悬停）
extern COLORREF ACCENT_CONT; // primary-container
extern COLORREF GREEN;       // 启动按钮 = primary-container
extern COLORREF GREEN_DK;    // 启动按钮深色
extern COLORREF ORANGE;      // tertiary

// 从 seed 色生成主题调色板（Material You 简化版，HSL 派生）。
// 对齐 HMCL 的 MonetFX：seed → primary/primary-container/secondary/tertiary。
// 相同 seed 会被缓存，重复调用开销极小。
void SetSeed(COLORREF seed);

// 恢复默认蓝紫主题（HMCL blue #5C6BC0）
void ResetSeed();

// 内置壁纸绑定的 seed 主题色（对齐 HMCL BuiltinBackground）。
// 0=2021-08-26(#3F6AA2 蓝) 1=2016-02-25(#354264 深蓝) 2=2015-06-22(#FBC578 暖黄)
COLORREF BuiltinSeed(int index);

// GDI+ 颜色
inline Gdiplus::Color C(COLORREF c, BYTE a = 255) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

// 变亮/变暗（用于悬停态）
inline COLORREF Lighten(COLORREF c, int amount) {
    int r = std::min(255, GetRValue(c) + amount);
    int g = std::min(255, GetGValue(c) + amount);
    int b = std::min(255, GetBValue(c) + amount);
    return RGB(r, g, b);
}
inline COLORREF Darken(COLORREF c, int amount) {
    int r = std::max(0, GetRValue(c) - amount);
    int g = std::max(0, GetGValue(c) - amount);
    int b = std::max(0, GetBValue(c) - amount);
    return RGB(r, g, b);
}

// ---- GDI+ 绘制辅助 ----
inline Gdiplus::GraphicsPath* RoundRectPath(float x, float y, float w, float h, float r) {
    auto* path = new Gdiplus::GraphicsPath();
    float d = r * 2.0f;
    path->AddArc(x, y, d, d, 180, 90);
    path->AddArc(x + w - d, y, d, d, 270, 90);
    path->AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path->AddArc(x, y + h - d, d, d, 90, 90);
    path->CloseFigure();
    return path;
}

inline void FillRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, float r,
                          const Gdiplus::Color& c) {
    Gdiplus::SolidBrush br(c);
    Gdiplus::GraphicsPath* path = RoundRectPath(rc.X, rc.Y, rc.Width, rc.Height, r);
    g.FillPath(&br, path);
    delete path;
}

inline void StrokeRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, float r,
                            const Gdiplus::Color& c, float width = 1.0f) {
    Gdiplus::Pen pen(c, width);
    Gdiplus::GraphicsPath* path = RoundRectPath(rc.X, rc.Y, rc.Width, rc.Height, r);
    g.DrawPath(&pen, path);
    delete path;
}

// 绘制文本（水平左对齐、垂直居中，带省略号）
inline void DrawTextLeft(Gdiplus::Graphics& g, const std::wstring& text,
                         Gdiplus::Font* font, const Gdiplus::Color& color,
                         const Gdiplus::RectF& rc, float padLeft = 10.0f) {
    Gdiplus::SolidBrush br(color);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentNear);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::RectF r = rc;
    r.X += padLeft;
    r.Width -= padLeft * 2;
    g.DrawString(text.c_str(), -1, font, r, &sf, &br);
}

inline void DrawTextCenter(Gdiplus::Graphics& g, const std::wstring& text,
                           Gdiplus::Font* font, const Gdiplus::Color& color,
                           const Gdiplus::RectF& rc) {
    Gdiplus::SolidBrush br(color);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    g.DrawString(text.c_str(), -1, font, rc, &sf, &br);
}

inline void DrawTextRight(Gdiplus::Graphics& g, const std::wstring& text,
                          Gdiplus::Font* font, const Gdiplus::Color& color,
                          const Gdiplus::RectF& rc, float padRight = 10.0f) {
    Gdiplus::SolidBrush br(color);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentFar);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::RectF r = rc;
    r.X += 4;
    r.Width -= padRight;
    g.DrawString(text.c_str(), -1, font, r, &sf, &br);
}

// 字体缓存（微软雅黑）
struct Fonts {
    Gdiplus::Font* title = nullptr;   // 大标题
    Gdiplus::Font* head = nullptr;    // 小标题
    Gdiplus::Font* body = nullptr;    // 正文
    Gdiplus::Font* smallFont = nullptr; // 小字（注意：不能叫 small，是 RPC 宏）
    Fonts() {
        title = new Gdiplus::Font(L"Microsoft YaHei UI", 20, Gdiplus::FontStyleBold);
        head  = new Gdiplus::Font(L"Microsoft YaHei UI", 13, Gdiplus::FontStyleBold);
        body  = new Gdiplus::Font(L"Microsoft YaHei UI", 10);
        smallFont = new Gdiplus::Font(L"Microsoft YaHei UI", 9);
    }
    ~Fonts() {
        delete title; delete head; delete body; delete smallFont;
    }
};

// 字体全局缓存：GDI+ Font 构造涉及字体查找，代价高，切勿每次绘制新建。
// 采用「泄漏式」单例（new 后不 delete），避免 DLL 卸载 GdiplusShutdown 之后
// 静态对象析构再 delete Font 导致的崩溃。
inline const Fonts& FontsCache() {
    static Fonts* f = new Fonts();
    return *f;
}

// UTF-8 <-> 宽字符
std::wstring Utf8ToW(const std::string& s);
std::string WToUtf8(const std::wstring& s);

} // namespace theme
