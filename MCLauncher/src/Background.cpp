// Background.cpp — 背景设置实现
#include "Background.h"
#include "Theme.h"
#include "Json.h"
#include "Wallpapers.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <shlobj.h>

namespace bg {

namespace fs = std::filesystem;

static std::wstring configPath() {
    wchar_t appdata[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata) == S_OK) {
        return std::wstring(appdata) + L"\\YXGLauncher\\config.json";
    }
    return L"yxg-launcher-config.json";
}

Manager& Manager::Instance() {
    static Manager inst;
    return inst;
}

Manager::Manager() {
    Load();
}

void Manager::Load() {
    std::ifstream f(configPath(), std::ios::binary);
    if (!f.is_open()) {
        // 首次运行：默认内置壁纸（HMCL 默认 2021-08-26，单张非轮播）
        m_cfg.mode = 1;
        m_cfg.builtinIndex = 0;
        return;
    }
    std::stringstream ss; ss << f.rdbuf();
    try {
        json::Value j = json::parse(ss.str());
        const json::Value& b = j.get("background");
        if (b.isObject()) {
            m_cfg.mode = (int)b.get("mode").asInt();
            m_cfg.color = (COLORREF)b.get("color").asInt();
            m_cfg.builtinIndex = (int)b.get("builtinIndex").asInt();
            m_cfg.image = theme::Utf8ToW(b.get("image").asString());
            m_cfg.cycleDir = theme::Utf8ToW(b.get("cycleDir").asString());
            m_cfg.cycleMs = (int)b.get("cycleMs").asInt();
            if (m_cfg.cycleMs <= 0) m_cfg.cycleMs = 8000;
            m_cfg.blur = b.get("blur").isNull() ? true : b.get("blur").asBool();
            m_cfg.blurStrength = (int)b.get("blurStrength").asInt();
            if (m_cfg.blurStrength <= 0) m_cfg.blurStrength = 160;
        } else {
            // config 存在但未保存过背景设置 → 默认内置壁纸
            m_cfg.mode = 1;
            m_cfg.builtinIndex = 0;
        }
    } catch (...) {}
}

void Manager::Save() {
    // 关键：必须读回现有 config.json，只改 background 字段再写回，
    // 否则会覆盖掉 App::saveConfig() 写入的 launcher 字段（游戏目录/Java/内存/服务器等）。
    json::Value j;
    {
        std::ifstream f(configPath(), std::ios::binary);
        if (f.is_open()) {
            std::stringstream ss; ss << f.rdbuf();
            try { j = json::parse(ss.str()); } catch (...) { j = json::makeObject(); }
        } else {
            j = json::makeObject();
        }
    }

    json::Value b = json::makeObject();
    b.obj["mode"] = json::makeInt(m_cfg.mode);
    b.obj["color"] = json::makeInt(m_cfg.color);
    b.obj["builtinIndex"] = json::makeInt(m_cfg.builtinIndex);
    b.obj["image"] = json::makeString(theme::WToUtf8(m_cfg.image));
    b.obj["cycleDir"] = json::makeString(theme::WToUtf8(m_cfg.cycleDir));
    b.obj["cycleMs"] = json::makeInt(m_cfg.cycleMs);
    b.obj["blur"] = json::makeBool(m_cfg.blur);
    b.obj["blurStrength"] = json::makeInt(m_cfg.blurStrength);
    j.obj["background"] = b;

    std::wstring p = configPath();
    std::error_code ec;
    fs::path parent = fs::path(p).parent_path();
    fs::create_directories(parent, ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (f.is_open()) f << json::dumpPretty(j);
}

void Manager::SetMode(int mode) { m_cfg.mode = mode; m_cachedPath.clear(); m_dirty = true; }
void Manager::SetBuiltinIndex(int idx) { m_cfg.builtinIndex = idx; m_cachedPath.clear(); m_dirty = true; }
void Manager::SetColor(COLORREF c) { m_cfg.color = c; m_dirty = true; }
void Manager::SetImage(const std::wstring& path) { m_cfg.image = path; m_cachedPath.clear(); m_dirty = true; }
void Manager::SetCycleDir(const std::wstring& dir) {
    m_cfg.cycleDir = dir; m_files.clear(); m_index = 0; m_cachedPath.clear(); m_dirty = true;
}
void Manager::SetBlur(bool on) { m_cfg.blur = on; m_dirty = true; }
void Manager::SetBlurStrength(int v) { m_cfg.blurStrength = v; m_dirty = true; }

static bool isImageFile(const std::wstring& name) {
    std::wstring n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::towlower);
    return n.size() >= 4 &&
        (n.ends_with(L".png") || n.ends_with(L".jpg") || n.ends_with(L".jpeg") ||
         n.ends_with(L".bmp") || n.ends_with(L".gif") || n.ends_with(L".webp"));
}

std::vector<std::wstring> Manager::CycleFiles() const {
    std::vector<std::wstring> out;
    if (m_cfg.cycleDir.empty()) return out;
    std::error_code ec;
    if (!fs::exists(m_cfg.cycleDir, ec)) return out;
    for (auto& e : fs::directory_iterator(m_cfg.cycleDir, ec)) {
        if (e.is_regular_file(ec) && isImageFile(e.path().filename().wstring())) {
            out.push_back(e.path().wstring());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void Manager::Next() {
    if (m_cfg.mode != (int)Mode::Cycle) return;
    auto files = CycleFiles();
    if (files.empty()) return;
    m_index = (m_index + 1) % files.size();
    m_cachedPath.clear();
    m_dirty = true;
}

bool Manager::ConsumeDirty() {
    bool d = m_dirty;
    m_dirty = false;
    return d;
}

static Gdiplus::Image* loadImage(const std::wstring& path) {
    if (path.empty()) return nullptr;
    std::error_code ec;
    if (!fs::exists(path, ec)) return nullptr;
    auto* img = Gdiplus::Image::FromFile(path.c_str());
    if (img && img->GetLastStatus() != Gdiplus::Ok) { delete img; return nullptr; }
    return img;
}

// 从内存字节创建 GDI+ Image（用于内置壁纸）
static Gdiplus::Image* loadImageFromMemory(const unsigned char* data, size_t size) {
    if (!data || size == 0) return nullptr;
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hGlobal) return nullptr;
    void* p = GlobalLock(hGlobal);
    if (!p) { GlobalFree(hGlobal); return nullptr; }
    memcpy(p, data, size);
    GlobalUnlock(hGlobal);
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hGlobal, TRUE, &stream) != S_OK || !stream) {
        GlobalFree(hGlobal);
        return nullptr;
    }
    Gdiplus::Image* img = Gdiplus::Image::FromStream(stream);
    stream->Release();
    if (img && img->GetLastStatus() != Gdiplus::Ok) { delete img; return nullptr; }
    return img;
}

// 从图片提取主色（对齐 HMCL WallpaperColorExtractor：缩到 1x1 取平均色）
// 带路径缓存，避免每帧重复读盘解码。返回 0 表示提取失败。
static COLORREF extractSeedFromImage(const std::wstring& path) {
    static std::wstring s_path;
    static COLORREF s_seed = 0;
    if (path == s_path) return s_seed;
    s_path = path;
    s_seed = 0;

    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) return 0;
    Gdiplus::Image* img = Gdiplus::Image::FromFile(path.c_str());
    if (!img || img->GetLastStatus() != Gdiplus::Ok) { if (img) delete img; return 0; }
    Gdiplus::Bitmap bmp(1, 1, PixelFormat24bppRGB);
    Gdiplus::Graphics g(&bmp);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(img, 0, 0, 1, 1);
    Gdiplus::Color c;
    bmp.GetPixel(0, 0, &c);
    delete img;
    s_seed = RGB(c.GetRed(), c.GetGreen(), c.GetBlue());
    return s_seed;
}

COLORREF Manager::CurrentThemeSeed() const {
    switch (m_cfg.mode) {
        case (int)Mode::Builtin:
            return theme::BuiltinSeed(m_cfg.builtinIndex);
        case (int)Mode::Cycle: {
            if (m_cfg.cycleDir.empty()) {
                // 内置壁纸轮播
                return theme::BuiltinSeed((int)(m_index % 3));
            }
            auto files = CycleFiles();
            if (files.empty()) return 0;
            return extractSeedFromImage(files[m_index % files.size()]);
        }
        case (int)Mode::Image:
            return extractSeedFromImage(m_cfg.image);
        default:
            return 0;  // 纯色 → 使用默认主题
    }
}

// 按比例拉伸填满（居中裁剪）绘制图片
static void drawCentered(Gdiplus::Graphics& g, Gdiplus::Image* img, const Gdiplus::RectF& rect) {
    float iw = (float)img->GetWidth();
    float ih = (float)img->GetHeight();
    float scale = std::max(rect.Width / iw, rect.Height / ih);
    float dw = iw * scale, dh = ih * scale;
    float dx = rect.X + (rect.Width - dw) / 2.0f;
    float dy = rect.Y + (rect.Height - dh) / 2.0f;
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(img, Gdiplus::RectF(dx, dy, dw, dh), 0, 0, iw, ih, Gdiplus::UnitPixel);
}

void Manager::Paint(Gdiplus::Graphics& g, const Gdiplus::RectF& rect) {
    // 同步主题色（HMCL：背景决定 MonetFX seed）。纯色返回 0 → 恢复默认蓝紫。
    COLORREF seed = CurrentThemeSeed();
    if (seed) theme::SetSeed(seed);
    else theme::ResetSeed();

    if (m_cfg.mode == (int)Mode::Solid) {
        Gdiplus::SolidBrush br(theme::C(m_cfg.color));
        g.FillRectangle(&br, rect);
        return;
    }

    // 内置壁纸（单张，HMCL BUILTIN 模式，默认 2021-08-26）
    if (m_cfg.mode == (int)Mode::Builtin) {
        auto& wps = wallpaper::Wallpapers();
        if (wps.empty()) {
            Gdiplus::SolidBrush br(theme::C(theme::BG));
            g.FillRectangle(&br, rect);
            return;
        }
        int idx = m_cfg.builtinIndex % (int)wps.size();
        const auto& e = wps[idx];
        std::wstring key = theme::Utf8ToW(e.name);
        if (key != m_cachedPath) {
            delete m_cached;
            m_cached = loadImageFromMemory(e.data, e.size);
            m_cachedPath = key;
        }
        if (!m_cached) {
            Gdiplus::SolidBrush br(theme::C(theme::BG));
            g.FillRectangle(&br, rect);
            return;
        }
        drawCentered(g, m_cached, rect);
        Gdiplus::SolidBrush overlay(Gdiplus::Color(110, 255, 255, 255));
        g.FillRectangle(&overlay, rect);
        return;
    }

    // 内置壁纸轮播（mode==Cycle 且 cycleDir 空）
    if (m_cfg.mode == (int)Mode::Cycle && m_cfg.cycleDir.empty()) {
        auto& wps = wallpaper::Wallpapers();
        if (wps.empty()) {
            Gdiplus::SolidBrush br(theme::C(theme::BG));
            g.FillRectangle(&br, rect);
            return;
        }
        ULONGLONG now = GetTickCount64();
        if (now - m_lastTick > (ULONGLONG)m_cfg.cycleMs) {
            m_index = (m_index + 1) % wps.size();
            m_lastTick = now;
            m_cachedPath.clear();
            m_dirty = true;
        }
        const auto& e = wps[m_index % wps.size()];
        std::wstring key = theme::Utf8ToW(e.name);
        if (key != m_cachedPath) {
            delete m_cached;
            m_cached = loadImageFromMemory(e.data, e.size);
            m_cachedPath = key;
        }
        if (!m_cached) {
            Gdiplus::SolidBrush br(theme::C(theme::BG));
            g.FillRectangle(&br, rect);
            return;
        }
        drawCentered(g, m_cached, rect);
        Gdiplus::SolidBrush overlay(Gdiplus::Color(110, 255, 255, 255));
        g.FillRectangle(&overlay, rect);
        return;
    }

    // 外部图片 / 目录轮播
    std::wstring path = m_cfg.image;
    if (m_cfg.mode == (int)Mode::Cycle) {
        auto files = CycleFiles();
        if (files.empty()) {
            Gdiplus::SolidBrush br(theme::C(theme::BG));
            g.FillRectangle(&br, rect);
            return;
        }
        ULONGLONG now = GetTickCount64();
        if (now - m_lastTick > (ULONGLONG)m_cfg.cycleMs) {
            m_index = (m_index + 1) % files.size();
            m_lastTick = now;
            m_cachedPath.clear();
            m_dirty = true;
        }
        path = files[m_index];
    }

    if (path != m_cachedPath) {
        delete m_cached;
        m_cached = loadImage(path);
        m_cachedPath = path;
    }

    if (!m_cached) {
        Gdiplus::SolidBrush br(theme::C(theme::BG));
        g.FillRectangle(&br, rect);
        return;
    }

    drawCentered(g, m_cached, rect);

    // 浅色主题：图片上叠一层白色柔化，保证前景深色文字可读
    Gdiplus::SolidBrush overlay(Gdiplus::Color(110, 255, 255, 255));
    g.FillRectangle(&overlay, rect);
}

void Manager::PaintCard(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius) {
    // 浅色卡片：白色半透明 + 浅灰描边（HMCL 风格）
    theme::FillRoundRect(g, rect, radius, Gdiplus::Color((BYTE)m_cfg.blurStrength, 255, 255, 255));
    theme::StrokeRoundRect(g, rect, radius, theme::C(theme::LINE), 1.0f);
}

} // namespace bg
