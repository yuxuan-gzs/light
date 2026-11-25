// UiNav.h — 侧边栏导航表与内容区布局常量（Ui.cpp / UiPaint.cpp / UiPages.cpp 共用）
#pragma once

#include "Ui.h"

namespace ui {

// 异步事件窗口消息
inline constexpr UINT WM_APP_EVENT = WM_APP + 10;

namespace nav {

enum class Kind { Account, Header, Current, Item };

struct Entry {
    Kind kind;
    const wchar_t* emoji;
    const wchar_t* label;
    Ui::Page page;
    int y;
    int h;
    int tab;      // >=0 时切到该版本管理标签页
};

inline const Entry kItems[] = {
    { Kind::Account, L"",   L"",         Ui::Page::Account,    14, 56, -1 },
    { Kind::Header,  L"",   L"实例",      Ui::Page::Home,       82, 20, -1 },
    { Kind::Current, L"",   L"",         Ui::Page::Home,      104, 52, -1 },
    { Kind::Item,    L"", L"版本管理",  Ui::Page::Versions,  166, 38, -1 },
    { Kind::Item,    L"", L"版本选择",  Ui::Page::GameSelect, 208, 38, -1 },
    { Kind::Item,    L"",  L"下载",      Ui::Page::Download,  250, 38, -1 },
    { Kind::Item,    L"", L"模组",      Ui::Page::Versions,  292, 38, Ui::TabMods },
    { Kind::Item,    L"", L"世界",      Ui::Page::Versions,  334, 38, Ui::TabWorlds },
    { Kind::Header,  L"",   L"启动器设置", Ui::Page::Home,      382, 20, -1 },
    { Kind::Item,    L"⚙",  L"全局设置",  Ui::Page::Settings,  404, 38, -1 },
};
inline constexpr int kCount = (int)(sizeof(kItems) / sizeof(kItems[0]));

constexpr int kSidebarW = 224;
constexpr int kNavLeft = 14;
constexpr int kNavRight = 210;

// 内容区
constexpr int CX = 248;
constexpr int CT = 26;
constexpr int GAP = 14;

} // namespace nav
} // namespace ui
