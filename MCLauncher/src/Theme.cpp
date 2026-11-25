// Theme.cpp — UTF 转换 + 动态主题色实现
#include "Theme.h"

#include <cmath>

namespace theme {

// ---- 主题色全局定义（默认 HMCL blue 蓝紫，可被 SetSeed 覆盖） ----
COLORREF ACCENT      = RGB(67, 82, 165);    // #4352A5 primary
COLORREF ACCENT_DK   = RGB(47, 63, 146);    // #2F3F92
COLORREF ACCENT_CONT = RGB(92, 107, 192);   // #5C6BC0 primary-container
COLORREF GREEN       = RGB(92, 107, 192);   // 启动按钮
COLORREF GREEN_DK    = RGB(72, 88, 171);
COLORREF ORANGE      = RGB(246, 189, 88);   // tertiary

// ---- RGB <-> HSL 转换（r/g/b 与 h/s/l 均为 0..1 浮点） ----
static void RgbToHsl(COLORREF c, double& h, double& s, double& l) {
    double r = GetRValue(c) / 255.0;
    double g = GetGValue(c) / 255.0;
    double b = GetBValue(c) / 255.0;
    double mx = std::max({ r, g, b });
    double mn = std::min({ r, g, b });
    l = (mx + mn) / 2.0;
    if (mx == mn) { h = s = 0.0; return; }
    double d = mx - mn;
    s = (l > 0.5) ? d / (2.0 - mx - mn) : d / (mx + mn);
    if (mx == r)      h = (g - b) / d + (g < b ? 6.0 : 0.0);
    else if (mx == g) h = (b - r) / d + 2.0;
    else              h = (r - g) / d + 4.0;
    h /= 6.0;
}

static double Hue2Rgb(double p, double q, double t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

static COLORREF HslToRgb(double h, double s, double l) {
    if (s == 0.0) {
        int v = (int)std::lround(l * 255.0);
        return RGB(v, v, v);
    }
    double q = (l < 0.5) ? l * (1.0 + s) : l + s - l * s;
    double p = 2.0 * l - q;
    int r = (int)std::lround(Hue2Rgb(p, q, h + 1.0 / 3.0) * 255.0);
    int g = (int)std::lround(Hue2Rgb(p, q, h) * 255.0);
    int b = (int)std::lround(Hue2Rgb(p, q, h - 1.0 / 3.0) * 255.0);
    return RGB(
        (BYTE)std::clamp(r, 0, 255),
        (BYTE)std::clamp(g, 0, 255),
        (BYTE)std::clamp(b, 0, 255));
}

void SetSeed(COLORREF seed) {
    static COLORREF last = 0xFFFFFFFF;
    if (seed == last) return;
    last = seed;

    double h, s, l;
    RgbToHsl(seed, h, s, l);

    // primary：保持色相，明度钳制到中等范围以保证白字（on-primary）可读
    double pl = std::clamp(l, 0.30, 0.46);
    double ps = std::min(s, 0.85);
    ACCENT    = HslToRgb(h, ps, pl);
    ACCENT_DK = HslToRgb(h, ps, std::max(0.20, pl - 0.14));

    // primary-container（启动按钮）：浅色容器，配深字（on-container #1B1B21）
    double cs = std::clamp(s * 0.45, 0.15, 0.55);
    ACCENT_CONT = HslToRgb(h, cs, 0.90);
    GREEN       = ACCENT_CONT;
    GREEN_DK    = HslToRgb(h, cs, 0.82);

    // tertiary：色相偏移约 +40°，用于点缀
    double th = std::fmod(h + 40.0 / 360.0, 1.0);
    ORANGE = HslToRgb(th, std::min(s, 0.70), 0.55);
}

void ResetSeed() {
    ACCENT      = RGB(67, 82, 165);
    ACCENT_DK   = RGB(47, 63, 146);
    ACCENT_CONT = RGB(92, 107, 192);
    GREEN       = RGB(92, 107, 192);
    GREEN_DK    = RGB(72, 88, 171);
    ORANGE      = RGB(246, 189, 88);
}

COLORREF BuiltinSeed(int index) {
    switch (index) {
        case 0:  return RGB(63, 106, 162);   // #3F6AA2 2021-08-26 蓝
        case 1:  return RGB(53, 66, 100);    // #354264 2016-02-25 深蓝
        case 2:  return RGB(251, 197, 120);  // #FBC578 2015-06-22 暖黄
        default: return RGB(63, 106, 162);
    }
}

std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &u[0], n, nullptr, nullptr);
    return u;
}

} // namespace theme
