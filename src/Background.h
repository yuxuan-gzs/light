// Background.h — 背景设置（PCL2 风格：纯色/图片/轮播 + 磨砂卡片）
#pragma once

#include "Theme.h"
#include <string>
#include <vector>
#include <mutex>

namespace bg {

// 背景模式
enum class Mode { Solid = 0, Builtin = 1, Image = 2, Cycle = 3 };

struct Config {
    int mode = 1;                 // 0 纯色 1 内置壁纸 2 图片 3 轮播（HMCL: PAINT/BUILTIN/CUSTOM）
    int color = RGB(251, 248, 255);  // 纯色时的背景色（HMCL surface #FBF8FF）
    int builtinIndex = 0;         // 内置壁纸索引：0=2021-08-26 1=2016-02-25 2=2015-06-22
    std::wstring image;           // 图片路径
    std::wstring cycleDir;        // 轮播目录
    int cycleMs = 8000;           // 轮播间隔（毫秒）
    bool blur = true;             // 磨砂卡片效果
    int blurStrength = 255;       // 卡片不透明度（0-255，浅色卡片用高不透明度）
};

class Manager {
public:
    static Manager& Instance();

    const Config& Cfg() const { return m_cfg; }
    void SetMode(int mode);
    void SetColor(COLORREF c);
    void SetBuiltinIndex(int idx);
    void SetImage(const std::wstring& path);
    void SetCycleDir(const std::wstring& dir);
    void SetBlur(bool on);
    void SetBlurStrength(int v);

    // 扫描轮播目录，返回图片列表
    std::vector<std::wstring> CycleFiles() const;
    // 手动切换下一张（轮播模式下）
    void Next();

    // 绘制背景到指定 Graphics（rect 为客户区）
    void Paint(Gdiplus::Graphics& g, const Gdiplus::RectF& rect);

    // 返回当前背景对应的主题 seed 色（HMCL：背景决定主题色；纯色时返回 0 表示用默认）
    COLORREF CurrentThemeSeed() const;

    // 绘制磨砂卡片（半透明深色 + 圆角），在背景之上、控件之下调用
    void PaintCard(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius);

    // 加载/保存配置
    void Load();
    void Save();

    // 通知 UI 刷新（轮播定时器触发时置 true，由 UI 消费）
    bool ConsumeDirty();

private:
    Manager();
    Config m_cfg;
    std::mutex m_mu;
    std::vector<std::wstring> m_files;
    size_t m_index = 0;
    ULONGLONG m_lastTick = 0;
    Gdiplus::Image* m_cached = nullptr;   // 当前加载的图片
    std::wstring m_cachedPath;
    bool m_dirty = false;
};

} // namespace bg
