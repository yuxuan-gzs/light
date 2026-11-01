// Theme.h
// 现代化 GDI+ 主题：颜色、圆角、阴影、文本、网格等绘制辅助
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <cstdint>

namespace vcb {
namespace theme {

// 主题色（浅色）
namespace color {
    inline constexpr uint32_t WindowBg   = 0xFFF5F6F8;
    inline constexpr uint32_t PanelBg    = 0xFFFFFFFF;
    inline constexpr uint32_t CanvasBg   = 0xFFF0F2F5;
    inline constexpr uint32_t GridDot    = 0xFFD9DDE1;
    inline constexpr uint32_t Border     = 0xFFE3E6EA;
    inline constexpr uint32_t Text       = 0xFF1F2328;
    inline constexpr uint32_t TextDim    = 0xFF6B7280;
    inline constexpr uint32_t TextFaint  = 0xFF9AA1A9;
    inline constexpr uint32_t Accent     = 0xFF2D7FF9;
    inline constexpr uint32_t AccentHov  = 0xFF1E6FE0;
    inline constexpr uint32_t Selection  = 0xFFFFC53D;
    inline constexpr uint32_t HoverTint  = 0x14000000;
    inline constexpr uint32_t Danger     = 0xFFE5484D;
}

Gdiplus::Color C(uint32_t argb);
uint32_t Blend(uint32_t base, uint32_t overlay); // overlay 叠加到 base（带 alpha）

void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float radius);
void FillRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const Gdiplus::Brush& b);
void DrawRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const Gdiplus::Pen& p, float penWidth);

void DrawText(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font,
              Gdiplus::Color color, const Gdiplus::RectF& r,
              Gdiplus::StringAlignment halign = Gdiplus::StringAlignmentNear,
              Gdiplus::StringAlignment valign = Gdiplus::StringAlignmentCenter);

Gdiplus::RectF MeasureText(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font);

void DrawShadow(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, int alpha, int offsetY = 2);
void DrawGrid(Gdiplus::Graphics& g, const Gdiplus::RectF& r, int spacing);

// 创建字体（调用方负责 delete）
Gdiplus::Font* MakeFont(const wchar_t* family, float size, int style = Gdiplus::FontStyleRegular);

} // namespace theme
} // namespace vcb
