// UiPages.cpp — 各页面布局构建
#include "Ui.h"
#include "UiNav.h"
#include "Theme.h"
#include "Background.h"
#include "LauncherApp.h"
#include "Json.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace ui {

namespace fs = std::filesystem;

// 主行动色（固定深绿，避免壁纸主题色过浅导致按钮看不清）
static constexpr COLORREF kPrimary = RGB(46, 125, 50);

using nav::CX;
using nav::CT;
using nav::GAP;

// ---------------------------------------------------------------
// 控件构造小工具
// ---------------------------------------------------------------
static Widget L(const wchar_t* id, const std::wstring& text, int x, int y, int w, int h,
                int style = 0, COLORREF color = 0) {
    Widget o;
    o.type = Widget::Label;
    o.id = id;
    o.text = text;
    o.rc = { x, y, x + w, y + h };
    o.style = style;
    o.color = color;
    return o;
}

static Widget B(const wchar_t* id, const std::wstring& text, int x1, int y1, int x2, int y2,
                COLORREF accent = 0, bool active = false) {
    Widget o;
    o.type = Widget::Btn;
    o.id = id;
    o.text = text;
    o.rc = { x1, y1, x2, y2 };
    o.accent = accent;
    o.active = active;
    return o;
}

static Widget E(const wchar_t* id, const std::wstring& text, int x1, int y1, int x2, int y2,
                const std::wstring& ph = L"") {
    Widget o;
    o.type = Widget::Edit;
    o.id = id;
    o.editText = text;
    o.placeholder = ph;
    o.rc = { x1, y1, x2, y2 };
    return o;
}

static Widget CB(const wchar_t* id, const std::vector<std::wstring>& items, int sel,
                 int x1, int y1, int x2, int y2) {
    Widget o;
    o.type = Widget::Combo;
    o.id = id;
    o.items = items;
    o.sel = sel;
    o.rc = { x1, y1, x2, y2 };
    return o;
}

static Widget CK(const wchar_t* id, const std::wstring& text, int x1, int y1, int x2, int y2,
                 bool checked) {
    Widget o;
    o.type = Widget::Check;
    o.id = id;
    o.text = text;
    o.checked = checked;
    o.rc = { x1, y1, x2, y2 };
    return o;
}

static Widget P(const wchar_t* id, int x1, int y1, int x2, int y2) {
    Widget o;
    o.type = Widget::Panel;
    o.id = id;
    o.rc = { x1, y1, x2, y2 };
    return o;
}

static Widget PG(const wchar_t* id, int x1, int y1, int x2, int y2, float p) {
    Widget o;
    o.type = Widget::Progress;
    o.id = id;
    o.progress = p;
    o.rc = { x1, y1, x2, y2 };
    return o;
}

static Widget GD(const wchar_t* id, std::vector<GridItem> items, int x1, int y1, int x2, int y2,
                 int cols, int cardW, int cardH) {
    Widget o;
    o.type = Widget::Grid;
    o.id = id;
    o.items2 = std::move(items);
    o.rc = { x1, y1, x2, y2 };
    o.cols = std::max(1, cols);
    o.cardW = cardW;
    o.cardH = cardH;
    o.gap = 12;
    return o;
}

// 版本渠道颜色
static COLORREF channelColor(mlc::Channel c) {
    switch (c) {
        case mlc::Channel::Release: return RGB(67, 160, 71);
        case mlc::Channel::PreRelease: return RGB(239, 140, 0);
        case mlc::Channel::Snapshot: return RGB(66, 133, 244);
        case mlc::Channel::AprilFools: return RGB(216, 79, 178);
        case mlc::Channel::OldBeta: return RGB(141, 110, 99);
        case mlc::Channel::OldAlpha: return RGB(121, 85, 72);
    }
    return RGB(120, 120, 130);
}

static GridItem itemToCard(const content::Item& it, bool allowToggle) {
    GridItem g;
    g.key = mlc::Utf8ToW(it.key);
    g.title = mlc::Utf8ToW(it.title);
    g.subtitle = mlc::Utf8ToW(it.desc);
    g.badge = mlc::Utf8ToW(it.badge);
    g.cover = mlc::Utf8ToW(it.cover);
    g.on = it.enabled;
    g.toggleable = allowToggle && it.toggleable;
    std::string b = it.badge;
    std::transform(b.begin(), b.end(), b.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (b == "fabric") g.loaderIcon = 1;
    else if (b == "quilt") g.loaderIcon = 2;
    else if (b == "forge") g.loaderIcon = 3;
    else if (b == "neoforge") g.loaderIcon = 4;
    return g;
}


// 空列表提示（画在网格左上角）
static void pushEmptyHint(std::vector<Widget>& ws, const RECT& rc, const wchar_t* text) {
    Widget l;
    l.type = Widget::Label;
    l.id = L"empty.hint";
    l.text = text;
    l.style = 0;
    l.rc = { rc.left + 6, rc.top + 6, rc.right - 6, rc.top + 32 };
    ws.push_back(l);
}

static int gridCols(int width, int cardW) {
    return std::max(1, (width + 12) / (cardW + 12));
}

// ============================================================
// 首页
// ============================================================
void Ui::BuildHome() {
    launcher::App& app = launcher::App::Instance();
    mlc::Store& store = mlc::Store::I();
    int x = CX, y = CT;
    int right = m_width - 32;
    int cw = right - x;

    mlc::Instance* cur = store.Current();
    m_widgets.push_back(L(L"home.title", L"我的世界", x, y, 400, 34, 2));

    std::wstring sub = cur ? (L"当前游戏：" + mlc::Utf8ToW(cur->name))
                           : L"还没有游戏。到「下载」安装一个，或在「版本选择」里添加已有文件夹。";
    m_widgets.push_back(L(L"home.sub", sub, x, y + 38, cw - 340, 24, 0));

    // 启动按钮（右上，大）
    std::wstring launchText = m_running ? L"■ 结束游戏"
                                        : (m_launching ? L"启动中…" : L"▶  启动游戏");
    COLORREF launchColor = m_running ? theme::RED
                                     : (m_launching ? theme::LINE : kPrimary);
    Widget launch = B(L"home.launch", launchText,
                      right - 270, y + 8, right, y + 62, launchColor);
    m_widgets.push_back(launch);
    m_widgets.push_back(B(L"home.verset", L"版本设置", right - 270, y + 70,
                          right - 130, y + 102, 0));
    m_widgets.push_back(B(L"home.gosel", L"版本选择", right - 122, y + 70,
                          right, y + 102, 0));

    // 当前实例信息
    int iy = y + 120;
    m_widgets.push_back(L(L"home.instname",
        cur ? mlc::Utf8ToW(cur->name) : L"未选择游戏", x, iy, 420, 30, 1));
    std::wstring line2 = L"版本 " + (cur ? mlc::Utf8ToW(cur->versionId) : L"—");
    if (cur && !cur->loader.empty())
        line2 += L"   ·   加载器 " + mlc::Utf8ToW(cur->loader) + L" " +
                 mlc::Utf8ToW(cur->loaderVersion);
    line2 += L"   ·   隔离 " + std::wstring(cur && cur->isolateEnabled() ? L"开" : L"关");
    m_widgets.push_back(L(L"home.instline", line2, x, iy + 34, 620, 24, 0));
    std::wstring dirLine = cur ? (L"目录 " + mlc::Utf8ToW(cur->folder)) : L"";
    m_widgets.push_back(L(L"home.instdir", dirLine, x, iy + 60, cw - 40, 22, 0));

    m_widgets.push_back(L(L"home.status", m_progressText.empty() ? L"就绪" : m_progressText,
                          x, iy + 84, cw - 40, 22, 0));
    m_widgets.push_back(PG(L"home.progress", x, iy + 110, right, iy + 122, m_progress));

    // 我的游戏
    int gy = iy + 152;
    m_widgets.push_back(L(L"home.mygames", L"我的游戏", x, gy, 300, 26, 1));
    m_widgets.push_back(B(L"home.refresh", L"⟳ 刷新", right - 96, gy, right, gy + 26, 0));

    std::vector<GridItem> cards;
    for (auto& in : store.list()) {
        GridItem g;
        g.key = mlc::Utf8ToW(in.id);
        g.title = mlc::Utf8ToW(in.name);
        g.subtitle = mlc::Utf8ToW(in.versionId);
        mlc::Channel ch = mlc::ClassifyVersion(in.versionId, "release");
        g.tint = channelColor(ch);
        g.badge = in.loader.empty() ? L"" : mlc::Utf8ToW(in.loader);
        g.loaderIcon = in.loader == "fabric" ? 1 : (in.loader == "quilt" ? 2 :
                      (in.loader == "forge" ? 3 : (in.loader == "neoforge" ? 4 : 0)));
        g.action = L"启动";
        cards.push_back(std::move(g));
    }
    int cardW = 192, cardH = 200;
    int cols = gridCols(cw, cardW);
    Widget grid = GD(L"home.grid", cards, x, gy + 32, right, m_height - 24, cols, cardW, cardH);
    if (cur) {
        for (size_t i = 0; i < grid.items2.size(); ++i)
            if (grid.items2[i].key == mlc::Utf8ToW(cur->id)) grid.selIdx = (int)i;
    }
    m_widgets.push_back(grid);
}

// ============================================================
// 版本管理
// ============================================================
void Ui::BuildVersions() {
    launcher::App& app = launcher::App::Instance();
    mlc::Store& store = mlc::Store::I();
    int x = CX, y = CT;
    int right = m_width - 32;
    int cw = right - x;

    // 当前实例（管理对象）
    if (m_verInstId.empty() && store.Current()) m_verInstId = mlc::Utf8ToW(store.Current()->id);
    mlc::Instance* inst = store.Find(mlc::WToUtf8(m_verInstId));
    if (!inst) {
        inst = store.Current();
        m_verInstId = inst ? mlc::Utf8ToW(inst->id) : L"";
    }
    if (m_cacheInstId != m_verInstId) {
        m_modsLoaded = m_packsLoaded = m_worldsLoaded = m_schemsLoaded = m_shadersLoaded = false;
        m_cacheInstId = m_verInstId;
    }

    m_widgets.push_back(L(L"ver.title", L"版本管理", x, y, 260, 32, 2));

    // 实例选择下拉
    std::vector<std::wstring> names;
    int sel = 0;
    for (size_t i = 0; i < store.list().size(); ++i) {
        names.push_back(mlc::Utf8ToW(store.list()[i].name));
        if (store.list()[i].id == mlc::WToUtf8(m_verInstId)) sel = (int)i;
    }
    if (names.empty()) names.push_back(L"（无游戏）");
    m_widgets.push_back(CB(L"ver.inst", names, sel, x + 270, y + 2, x + 530, y + 32));
    m_widgets.push_back(B(L"ver.open", L"打开目录", x + 540, y + 2, x + 660, y + 32, 0));
    if (inst) {
        m_widgets.push_back(L(L"ver.path", mlc::Utf8ToW(inst->folder) + L"  ·  " +
                              mlc::Utf8ToW(inst->versionId), x + 670, y + 2,
                              std::max(120, right - (x + 670)), 30, 0));
    }

    // 标签页
    static const wchar_t* kTabs[] = { L"模组", L"加载器", L"⚙ 版本设置",
                                      L"资源包", L"光影", L"世界", L"原理图" };
    int tx = x;
    for (int i = 0; i < 7; ++i) {
        int w = 108;
        m_widgets.push_back(B((std::wstring(L"ver.tab") + std::to_wstring(i)).c_str(), kTabs[i],
                              tx, y + 46, tx + w, y + 78,
                              m_verTab == i ? theme::ACCENT : 0, m_verTab == i));
        tx += w + 6;
    }

    int cy = y + 90;
    int bottom = m_height - 24;

    if (!inst) {
        m_widgets.push_back(L(L"ver.empty",
            L"还没有游戏。先在「版本选择」添加文件夹，或到「下载」安装一个版本。",
            x, cy + 20, cw, 26, 1));
        return;
    }

    switch (m_verTab) {
    // ---------------- 模组 ----------------
    case TabMods: {
        RefreshMods(false);
        m_widgets.push_back(B(L"mod.refresh", L"⟳ 刷新", x, cy, x + 96, cy + 30, 0));
        m_widgets.push_back(B(L"mod.opendir", L"打开 mods", x + 104, cy, x + 236, cy + 30, 0));
        m_widgets.push_back(B(L"mod.detect", L"检测前置", x + 244, cy, x + 376, cy + 30, 0));
        m_widgets.push_back(B(L"mod.fix", L"补前置", x + 384, cy, x + 496, cy + 30, theme::ACCENT));
        m_widgets.push_back(B(L"mod.godl", L"去下载模组", x + 504, cy, x + 648, cy + 30, 0));
        m_widgets.push_back(L(L"mod.count",
            L"已安装 " + std::to_wstring((int)m_mods.size()) + L" 个（点卡片右下角开关启用/停用）",
            x + 660, cy, cw - 430, 30, 0));

        std::vector<GridItem> cards;
        for (auto& it : m_mods) cards.push_back(itemToCard(it, true));
        int cardW = 176, cardH = 212;
        int cols = gridCols(cw, cardW);
        m_widgets.push_back(GD(L"mod.grid", cards, x, cy + 38, right, bottom, cols, cardW, cardH));
        if (cards.empty()) pushEmptyHint(m_widgets, m_widgets.back().rc,
            L"mods 目录里还没有模组：点上方「去下载模组」，或把 .jar 放进 mods 目录。");
        break;
    }
    // ---------------- 加载器 ----------------
    case TabLoader: {
        static const wchar_t* kNames[] = { L"Fabric", L"Quilt", L"Forge", L"NeoForge" };
        static const wchar_t* kDescs[] = {
            L"轻量、更新快，兼容大多数优化模组",
            L"Fabric 的社区分支，生态较小",
            L"老牌加载器，模组数量最多",
            L"1.20.2+ 的新一代 Forge",
        };
        std::vector<GridItem> cards;
        for (int i = 0; i < 4; ++i) {
            GridItem g;
            g.key = std::to_wstring(i);
            g.title = kNames[i];
            g.subtitle = kDescs[i];
            g.loaderIcon = i + 1;
            g.tint = i == 0 ? RGB(219, 206, 183) : (i == 1 ? RGB(167, 139, 250) :
                     (i == 2 ? RGB(219, 120, 60) : RGB(80, 140, 220)));
            // inst->loader 存的是小写（fabric / quilt / forge / neoforge）
            std::string wantName = mlc::WToUtf8(kNames[i]);
            std::transform(wantName.begin(), wantName.end(), wantName.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            bool installed = (inst->loader == wantName);
            if (installed) {
                g.badge = L"已安装 " + mlc::Utf8ToW(inst->loaderVersion);
                g.subtitle = L"当前版本：" + mlc::Utf8ToW(inst->versionId);
                g.action = L"卸载";
                g.actionDanger = true;
            } else {
                g.action = L"安装";
            }
            cards.push_back(std::move(g));
        }
        int cardW = 208, cardH = 220;
        int cols = gridCols(cw, cardW);
        Widget grid = GD(L"loader.grid", cards, x, cy, right, cy + 240, cols, cardW, cardH);
        grid.selIdx = m_loaderSel;
        m_widgets.push_back(grid);

        int by = cy + 256;
        m_widgets.push_back(L(L"loader.lbl", L"目标版本", x, by + 4, 80, 26, 0));
        m_widgets.push_back(L(L"loader.mc", inst ? mlc::Utf8ToW(inst->versionId) : L"—",
                              x + 78, by + 4, 200, 26, 1));

        std::vector<std::wstring> vers;
        vers.push_back(L"自动（最新稳定）");
        for (auto& v : m_loaderVersions) vers.push_back(v);
        m_widgets.push_back(CB(L"loader.ver", vers, 0, x, by + 34, x + 260, by + 66));
        m_widgets.push_back(B(L"loader.install", L"安装 / 更新", x + 272, by + 34,
                              x + 440, by + 66, kPrimary));
        m_widgets.push_back(B(L"loader.uninstall", L"卸载加载器", x + 448, by + 34,
                              x + 596, by + 66, theme::RED));
        m_widgets.push_back(B(L"loader.fetch", L"⟳ 获取版本列表", x + 604, by + 34,
                              x + 748, by + 66, 0));
        m_widgets.push_back(L(L"loader.status",
            m_loaderStatus.empty()
                ? L"安装会在 versions\\ 下新建一个「原版 + 加载器」版本（原版文件保留不动），装完自动切换过去。"
                : m_loaderStatus,
            x, by + 76, cw, 26, 0));
        break;
    }
    // ---------------- 版本设置 ----------------
    case TabVerSet: {
        mlc::InstanceSettings s = inst->settings;
        auto& gs = store.spec();
        int ry = cy - m_scrollY;
        int colW = std::min(560, cw - 20);

        auto rowLabel = [&](const std::wstring& text) {
            m_widgets.push_back(L(L"vs.lbl", text, x, ry, colW, 20, 0));
            ry += 22;
        };
        auto rowEdit = [&](const wchar_t* id, const std::wstring& val, const std::wstring& label,
                           const std::wstring& ph = L"") {
            rowLabel(label);
            m_widgets.push_back(E(id, val, x, ry, x + 420, ry + 30, ph));
            ry += 40;
        };

        m_widgets.push_back(L(L"vs.head", L"单局游戏设置", x, ry, 400, 28, 1));
        ry += 34;

        rowEdit(L"vs.java", mlc::Utf8ToW(s.javaPath), L"Java 路径（留空 = 自动检测；可留空，仅特殊需要时指定）",
                L"例如 D:\\Java\\jdk17\\bin\\java.exe");
        rowEdit(L"vs.mem", s.maxMemoryMB > 0 ? std::to_wstring(s.maxMemoryMB) : L"",
                L"最大内存 MB（留空 = 用全局 " + std::to_wstring(gs.maxMemoryMB) + L" MB）");
        rowLabel(L"窗口大小（留空 = 用全局默认）");
        m_widgets.push_back(E(L"vs.width", s.width > 0 ? std::to_wstring(s.width) : L"",
                              x, ry, x + 120, ry + 30, L"宽"));
        m_widgets.push_back(E(L"vs.height", s.height > 0 ? std::to_wstring(s.height) : L"",
                              x + 130, ry, x + 250, ry + 30, L"高"));
        m_widgets.push_back(CK(L"vs.fullscreen", L"全屏启动", x + 270, ry, x + 420, ry + 30,
                               s.fullscreen));
        ry += 40;

        rowLabel(L"版本隔离（独立 mods / saves，避免不同版本互相污染）");
        m_widgets.push_back(CB(L"vs.isolate",
            { L"跟随全局", L"关闭", L"开启" }, s.isolate < 0 ? 0 : s.isolate,
            x, ry, x + 250, ry + 30));
        ry += 40;

        rowLabel(L"启动时检查游戏完整性");
        m_widgets.push_back(CB(L"vs.check",
            { L"跟随全局", L"关闭", L"开启" }, s.checkIntegrity < 0 ? 0 : s.checkIntegrity,
            x, ry, x + 250, ry + 30));
        m_widgets.push_back(B(L"vs.checknow", L"立即检查并修复", x + 262, ry, x + 420,
                              ry + 30, theme::ACCENT));
        ry += 40;

        rowEdit(L"vs.nick", mlc::Utf8ToW(s.nickname), L"该实例使用的玩家名（留空 = 用账号名）");
        rowEdit(L"vs.server", mlc::Utf8ToW(s.server.empty() ? "" :
                (s.server + (s.serverPort != 25565 ? ":" + std::to_string(s.serverPort) : ""))),
                L"启动后直连服务器（空 = 不直连，格式 host 或 host:port）");
        rowEdit(L"vs.jvm", [&] {
            std::wstring o;
            for (auto& a : s.jvmArgs) { if (!o.empty()) o += L" "; o += mlc::Utf8ToW(a); }
            return o;
        }(), L"额外 JVM 参数（空格分隔）");
        rowEdit(L"vs.game", [&] {
            std::wstring o;
            for (auto& a : s.gameArgs) { if (!o.empty()) o += L" "; o += mlc::Utf8ToW(a); }
            return o;
        }(), L"额外游戏参数（空格分隔）");

        ry += 4;
        m_widgets.push_back(B(L"vs.save", L"保存设置", x, ry, x + 150, ry + 36, kPrimary));
        m_widgets.push_back(B(L"vs.reset", L"重置为全局默认", x + 162, ry, x + 322, ry + 36, 0));
        m_widgets.push_back(B(L"vs.opendir", L"打开实例目录", x + 334, ry, x + 494, ry + 36, 0));
        m_widgets.push_back(L(L"vs.status", L"", x + 506, ry, cw - 520, 36, 0));
        break;
    }
    // ---------------- 资源包 ----------------
    case TabPacks: {
        RefreshPacks(false);
        m_widgets.push_back(B(L"pack.refresh", L"⟳ 刷新", x, cy, x + 96, cy + 30, 0));
        m_widgets.push_back(B(L"pack.open", L"打开目录", x + 104, cy, x + 236, cy + 30, 0));
        m_widgets.push_back(B(L"pack.import", L"＋ 导入资源包", x + 244, cy, x + 396, cy + 30, 0));
        m_widgets.push_back(B(L"pack.godl", L"去下载资源包", x + 404, cy, x + 570, cy + 30, 0));
        m_widgets.push_back(B(L"pack.up", L"↑ 上移", x + 580, cy, x + 664, cy + 30, 0));
        m_widgets.push_back(B(L"pack.down", L"↓ 下移", x + 672, cy, x + 756, cy + 30, 0));
        m_widgets.push_back(L(L"pack.hint", L"在 options.txt 里调整优先级（越靠上优先级越高）",
                              x + 764, cy, cw - 774, 30, 0));
        std::vector<GridItem> cards;
        for (auto& it : m_packs) cards.push_back(itemToCard(it, true));
        int cardW = 176, cardH = 212;
        m_widgets.push_back(GD(L"pack.grid", cards, x, cy + 38, right, bottom,
                               gridCols(cw, cardW), cardW, cardH));
        if (cards.empty()) pushEmptyHint(m_widgets, m_widgets.back().rc,
            L"还没有资源包：点「导入资源包」，或到「下载 → 资源包」安装。");
        break;
    }
    // ---------------- 光影 ----------------
    case TabShaders: {
        RefreshShaders(false);
        m_widgets.push_back(B(L"shd.refresh", L"⟳ 刷新", x, cy, x + 96, cy + 30, 0));
        m_widgets.push_back(B(L"shd.open", L"打开目录", x + 104, cy, x + 236, cy + 30, 0));
        m_widgets.push_back(B(L"shd.import", L"＋ 导入光影包", x + 244, cy, x + 396, cy + 30, 0));
        m_widgets.push_back(L(L"shd.hint",
            L"光影由游戏内 options.txt 的 shaderPack 选择；这里显示已安装的包（和 HMCL 一样不在此启停）",
            x + 404, cy, cw - 414, 30, 0));
        std::vector<GridItem> cards;
        for (auto& it : m_shaders) cards.push_back(itemToCard(it, false));
        int cardW = 176, cardH = 212;
        m_widgets.push_back(GD(L"shd.grid", cards, x, cy + 38, right, bottom,
                               gridCols(cw, cardW), cardW, cardH));
        if (cards.empty()) pushEmptyHint(m_widgets, m_widgets.back().rc,
            L"还没有光影包：点「导入光影包」，或把 .zip 放进 shaderpacks 目录。");
        break;
    }
    // ---------------- 世界 ----------------
    case TabWorlds: {
        RefreshWorlds(false);
        m_widgets.push_back(B(L"world.refresh", L"⟳ 刷新", x, cy, x + 96, cy + 30, 0));
        m_widgets.push_back(B(L"world.open", L"打开目录", x + 104, cy, x + 236, cy + 30, 0));
        m_widgets.push_back(B(L"world.import", L"＋ 导入世界(zip)", x + 244, cy, x + 400, cy + 30, 0));
        m_widgets.push_back(B(L"world.backup", L"备份选中", x + 408, cy, x + 540, cy + 30, theme::ACCENT));
        m_widgets.push_back(B(L"world.del", L"删除选中", x + 548, cy, x + 668, cy + 30, theme::RED));
        m_widgets.push_back(L(L"world.hint", L"共 " + std::to_wstring((int)m_worlds.size()) + L" 个世界",
                              x + 680, cy, 200, 30, 0));
        std::vector<GridItem> cards;
        for (auto& it : m_worlds) {
            GridItem g = itemToCard(it, false);
            g.action = L"备份";
            cards.push_back(std::move(g));
        }
        int cardW = 176, cardH = 212;
        m_widgets.push_back(GD(L"world.grid", cards, x, cy + 38, right, bottom,
                               gridCols(cw, cardW), cardW, cardH));
        if (cards.empty()) pushEmptyHint(m_widgets, m_widgets.back().rc,
            L"还没有世界：进入游戏生成一个，或用「导入世界(zip)」。");
        break;
    }
    // ---------------- 原理图 ----------------
    case TabSchematics: {
        RefreshSchematics(false);
        m_widgets.push_back(B(L"sch.refresh", L"⟳ 刷新", x, cy, x + 96, cy + 30, 0));
        m_widgets.push_back(B(L"sch.open", L"打开目录", x + 104, cy, x + 236, cy + 30, 0));
        m_widgets.push_back(B(L"sch.import", L"＋ 导入原理图", x + 244, cy, x + 384, cy + 30, 0));
        m_widgets.push_back(B(L"sch.del", L"删除选中", x + 392, cy, x + 512, cy + 30, theme::RED));
        m_widgets.push_back(L(L"sch.hint", L"支持 .schematic / .litematic / .schem / .nbt",
                              x + 524, cy, cw - 530, 30, 0));
        std::vector<GridItem> cards;
        for (auto& it : m_schems) {
            GridItem g = itemToCard(it, false);
            g.action = L"删除";
            g.actionDanger = true;
            cards.push_back(std::move(g));
        }
        int cardW = 176, cardH = 200;
        m_widgets.push_back(GD(L"sch.grid", cards, x, cy + 38, right, bottom,
                               gridCols(cw, cardW), cardW, cardH));
        if (cards.empty()) pushEmptyHint(m_widgets, m_widgets.back().rc,
            L"还没有原理图：点「导入原理图」添加 .schematic / .litematic 文件。");
        break;
    }
    default: break;
    }
}

// ============================================================
// 版本选择（文件夹注册表 + 一键切换）
// ============================================================
void Ui::BuildGameSelect() {
    mlc::Store& store = mlc::Store::I();
    int x = CX, y = CT;
    int right = m_width - 32;
    int cw = right - x;

    m_widgets.push_back(L(L"gs.title", L"版本选择", x, y, 260, 32, 2));
    m_widgets.push_back(L(L"gs.sub",
        L"游戏文件夹可以先添加、后下载；一个文件夹放多个版本，可随时一键切换。",
        x, y + 36, cw, 24, 0));

    m_widgets.push_back(B(L"gs.add", L"＋ 添加文件夹", x, y + 66, x + 156, y + 100, kPrimary));
    m_widgets.push_back(B(L"gs.removefolder", L"移除该文件夹", x + 164, y + 66, x + 316,
                          y + 100, 0));
    m_widgets.push_back(B(L"gs.openfolder", L"打开该文件夹", x + 324, y + 66, x + 470,
                          y + 100, 0));
    m_widgets.push_back(B(L"gs.del", L"卸载选中游戏", x + 478, y + 66, x + 632, y + 100,
                          theme::RED));
    m_widgets.push_back(B(L"gs.delfile", L"连同文件删除", x + 640, y + 66, x + 784, y + 100, 0));
    m_widgets.push_back(B(L"gs.checkall", L"检查完整性", x + 792, y + 66, x + 920, y + 100, 0));

    auto& folders = store.folders();
    if (m_gsFolderSel > (int)folders.size()) m_gsFolderSel = 0;

    // 文件夹切换
    std::vector<std::wstring> opts;
    opts.push_back(L"全部文件夹（" + std::to_wstring((int)store.list().size()) + L" 个游戏）");
    for (size_t i = 0; i < folders.size(); ++i) {
        int n = store.CountInstancesInFolder(folders[i]);
        opts.push_back(fs::path(fs::u8path(folders[i])).filename().u8string().empty()
                       ? mlc::Utf8ToW(folders[i])
                       : mlc::Utf8ToW(fs::path(fs::u8path(folders[i])).filename().u8string())
                         + L"（" + std::to_wstring(n) + L" 个）");
    }
    m_widgets.push_back(L(L"gs.folderlbl", L"当前文件夹", x, y + 112, 108, 28, 1));
    m_widgets.push_back(CB(L"gs.folder", opts, m_gsFolderSel, x + 112, y + 110, x + 480,
                           y + 140));

    std::string curFolder = (m_gsFolderSel > 0 && m_gsFolderSel <= (int)folders.size())
                            ? folders[m_gsFolderSel - 1] : store.CurrentFolder();
    m_widgets.push_back(L(L"gs.folderpath", mlc::Utf8ToW(curFolder),
                          x + 490, y + 112, std::max(120, right - (x + 490)), 28, 0));

    // 网格 → 平铺列表（HMCL 式整行）
    std::vector<GridItem> rows;
    std::string curId = store.CurrentId();
    int sel = -1;
    for (auto& in : store.list()) {
        if (m_gsFolderSel > 0 && m_gsFolderSel <= (int)folders.size() &&
            in.folder != folders[m_gsFolderSel - 1]) continue;
        GridItem g;
        g.key = mlc::Utf8ToW(in.id);
        g.title = mlc::Utf8ToW(in.name);
        std::wstring folderName = mlc::Utf8ToW(
            fs::path(fs::u8path(in.folder)).filename().u8string());
        g.subtitle = mlc::Utf8ToW(in.versionId) + L"   ·   文件夹 " + folderName;
        mlc::Channel ch = mlc::ClassifyVersion(in.versionId, "release");
        g.tint = channelColor(ch);
        g.badge = in.loader.empty() ? mlc::ChannelName(ch) : mlc::Utf8ToW(in.loader);
        g.loaderIcon = in.loader == "fabric" ? 1 : (in.loader == "quilt" ? 2 :
                      (in.loader == "forge" ? 3 : (in.loader == "neoforge" ? 4 : 0)));
        g.action = L"启动";
        if (in.id == curId) { sel = (int)rows.size(); }
        rows.push_back(std::move(g));
    }

    Widget list;
    list.type = Widget::Rows;
    list.id = L"gs.rows";
    list.items2 = std::move(rows);
    list.rc = { x, y + 148, right, m_height - 24 };
    list.rowH = 62;
    list.selIdx = sel;
    m_widgets.push_back(list);

    if (list.items2.empty()) {
        pushEmptyHint(m_widgets, list.rc,
            L"这个文件夹还没有游戏。到「下载 → 游戏」安装时，把上面的「安装到」选成这个文件夹即可。");
    }
}

// ============================================================
// 下载
// ============================================================
void Ui::BuildDownload() {
    launcher::App& app = launcher::App::Instance();
    mlc::Store& store = mlc::Store::I();
    int x = CX, y = CT;
    int right = m_width - 32;
    int cw = right - x;

    m_widgets.push_back(L(L"dl.title", L"下载", x, y, 200, 32, 2));

    // 分类
    static const wchar_t* kCats[] = { L"游戏", L"模组", L"资源包", L"光影", L"世界" };
    int tx = x;
    for (int i = 0; i < 5; ++i) {
        m_widgets.push_back(B((std::wstring(L"dl.cat") + std::to_wstring(i)).c_str(), kCats[i],
                              tx, y + 38, tx + 102, y + 72,
                              m_dlCat == i ? theme::ACCENT : 0, m_dlCat == i));
        tx += 108;
    }

    // 目标文件夹（所有分类共用）
    if (m_dlFolders.empty()) {
        // 来自「文件夹注册表」：空文件夹也能作为安装目标
        for (auto& f : store.folders()) m_dlFolders.push_back(mlc::Utf8ToW(f));
        if (m_dlFolders.empty()) {
            std::string def = mlc::WToUtf8(mlc::BaseDir()) + "\\games\\default";
            std::string derr;
            store.AddFolderPath(def, derr);
            m_dlFolders.push_back(mlc::Utf8ToW(def));
        }
        std::wstring cur = mlc::Utf8ToW(store.CurrentFolder());
        for (size_t i = 0; i < m_dlFolders.size(); ++i)
            if (m_dlFolders[i] == cur) m_dlFolderSel = (int)i;
    }
    if (m_dlFolderSel >= (int)m_dlFolders.size()) m_dlFolderSel = 0;
    m_widgets.push_back(L(L"dl.folderlbl", L"安装到", x, y + 78, 60, 28, 0));
    m_widgets.push_back(CB(L"dl.folder", m_dlFolders, m_dlFolderSel,
                           x + 66, y + 76, x + 470, y + 106));
    m_widgets.push_back(L(L"dl.folderhint", L"（更多文件夹在「版本选择」里添加）",
                          x + 480, y + 78, std::max(120, right - (x + 480)), 28, 0));

    int cy = y + 116;

    if (m_dlCat == 4) {
        // ---- 世界：CurseForge 在线 / 本地存档 ----
        m_widgets.push_back(B(L"world.src0", L"CurseForge 在线", x, cy, x + 152, cy + 30,
                              m_worldSource == 0 ? theme::ACCENT : 0, m_worldSource == 0));
        m_widgets.push_back(B(L"world.src1", L"本地存档", x + 158, cy, x + 268, cy + 30,
                              m_worldSource == 1 ? theme::ACCENT : 0, m_worldSource == 1));

        if (m_worldSource == 0) {
            // 在线：搜索 + 翻页
            m_widgets.push_back(E(L"dl.csearch", m_dlSearch, x + 278, cy, x + 618, cy + 32,
                                  L"搜索世界/地图（中文会自动翻成英文）"));
            m_widgets.push_back(B(L"cf.search", L"搜索", x + 624, cy, x + 704, cy + 32, kPrimary));
            m_widgets.push_back(B(L"cf.prev", L"上一页", x + 712, cy, x + 792, cy + 32, 0));
            m_widgets.push_back(B(L"cf.next", L"下一页", x + 798, cy, x + 878, cy + 32, 0));

            m_widgets.push_back(B(L"world.open", L"打开存档目录", x, cy + 40, x + 152, cy + 70, 0));
            m_widgets.push_back(B(L"dl.import", L"＋ 导入本地 zip", x + 160, cy + 40,
                                  x + 320, cy + 70, 0));
            m_widgets.push_back(L(L"cf.status",
                m_cfSearching ? L"正在搜索 CurseForge…"
                              : (m_cfStatus.empty() ? L"点「搜索」看热门世界，或输入关键词"
                                                    : m_cfStatus),
                x + 330, cy + 40, std::max(140, right - (x + 330)), 30, 0));

            std::vector<GridItem> cards;
            int sel = -1;
            for (size_t i = 0; i < m_cfHits.size(); ++i) {
                const auto& h = m_cfHits[i];
                GridItem g;
                g.key = std::to_wstring(h.modId);
                g.title = mlc::Utf8ToW(h.name);
                g.subtitle = mlc::Utf8ToW(h.summary);
                g.badge = mlc::Utf8ToW(h.gameVersion);
                g.cover = mlc::Utf8ToW(h.cover);
                g.tint = RGB(88, 101, 242);
                g.action = L"下载";
                cards.push_back(std::move(g));
            }
            int cardW = 180, cardH = 214;
            Widget grid = GD(L"cf.grid", cards, x, cy + 78, right, m_height - 24,
                             gridCols(cw, cardW), cardW, cardH);
            grid.selIdx = sel;
            m_widgets.push_back(grid);
            if (cards.empty())
                pushEmptyHint(m_widgets, grid.rc,
                    L"还没有结果：点「搜索」按热度拉取 CurseForge 世界，或输入英文/中文关键词。");
            return;
        }

        // 本地存档
        RefreshWorlds(false);
        m_widgets.push_back(B(L"dl.import", L"＋ 导入世界 (.zip)", x, cy + 40, x + 180,
                              cy + 70, kPrimary));
        m_widgets.push_back(B(L"dl.worldrefresh", L"刷新", x + 188, cy + 40, x + 284, cy + 70, 0));
        m_widgets.push_back(B(L"world.open", L"打开存档目录", x + 292, cy + 40, x + 444,
                              cy + 70, 0));
        m_widgets.push_back(L(L"dl.worldhint",
            L"导入 zip 会解压到 saves；备份写入 Data\\mlc\\backups",
            x + 456, cy + 40, std::max(120, right - (x + 456)), 30, 0));

        std::vector<GridItem> cards;
        for (auto& it : m_worlds) {
            GridItem g = itemToCard(it, false);
            g.action = L"备份";
            cards.push_back(std::move(g));
        }
        int cardW = 180, cardH = 214;
        m_widgets.push_back(GD(L"world.grid", cards, x, cy + 78, right, m_height - 90,
                               gridCols(cw, cardW), cardW, cardH));
        if (cards.empty()) pushEmptyHint(m_widgets, m_widgets.back().rc,
            L"还没有世界存档：点「导入世界 (.zip)」把本地世界装进来。");
        m_widgets.push_back(L(L"dl.status", m_progressText.empty()
                              ? (L"共 " + std::to_wstring((int)m_worlds.size()) + L" 个世界")
                              : m_progressText,
                              x, m_height - 76, cw, 40, 0));
        return;
    }

    if (m_dlCat == 0) {
        // ---- 游戏 ----
        static const wchar_t* kCh[] = { L"正式版", L"测试版", L"快照版", L"愚人节", L"远古版" };
        int cx0 = x;
        for (int i = 0; i < 5; ++i) {
            m_widgets.push_back(B((std::wstring(L"dl.ch") + std::to_wstring(i)).c_str(), kCh[i],
                                  cx0, cy, cx0 + 84, cy + 28,
                                  m_dlChannel == i ? kPrimary : 0, m_dlChannel == i));
            cx0 += 90;
        }
        m_widgets.push_back(E(L"dl.search", m_dlSearch, x, cy + 36, x + 300, cy + 66,
                              L"搜索版本号，如 1.20"));
        m_widgets.push_back(B(L"dl.refresh", L"⟳ 刷新清单", x + 308, cy + 36, x + 440, cy + 66, 0));
        m_widgets.push_back(L(L"dl.count",
            launcher::App::Instance().manifestLoaded()
                ? (L"可下载版本：" + std::to_wstring((int)launcher::App::Instance().manifest().versions.size()))
                : L"版本清单未加载，点「刷新清单」",
            x + 452, cy + 36, cw - 460, 30, 0));

        // 右侧面板宽度
        int panelW = 320;
        int gridRight = right - panelW - 16;

        std::vector<GridItem> cards;
        auto mf = app.manifest();
        std::wstring filter = m_dlSearch;
        static const mlc::Channel kOrder[] = { mlc::Channel::Release, mlc::Channel::PreRelease,
                                               mlc::Channel::Snapshot, mlc::Channel::AprilFools,
                                               mlc::Channel::OldBeta };
        mlc::Channel want = kOrder[std::min(4, std::max(0, m_dlChannel))];
        int sel = -1;

        // 目标文件夹里已装的版本 → 行尾角标提示「已安装」
        std::string targetFolder;
        if (m_dlFolderSel >= 0 && m_dlFolderSel < (int)m_dlFolders.size())
            targetFolder = mlc::WToUtf8(m_dlFolders[m_dlFolderSel]);

        for (const auto& v : mf.versions) {
            mlc::Channel ch = mlc::ClassifyVersion(v.id, v.type, v.releaseTime);
            bool match = (want == mlc::Channel::OldBeta)
                ? (ch == mlc::Channel::OldBeta || ch == mlc::Channel::OldAlpha)
                : (ch == want);
            if (!match) continue;
            std::wstring id = mlc::Utf8ToW(v.id);
            if (!filter.empty() && id.find(filter) == std::wstring::npos) continue;
            if (cards.size() >= 300) break;

            bool installed = false;
            if (!targetFolder.empty()) {
                installed = mlc::FileExists(mlc::JoinPath(
                    mlc::JoinPath(mlc::JoinPath(targetFolder, "versions"), v.id),
                    v.id + ".json"));
            }

            GridItem g;
            g.key = id;
            g.title = id;
            g.subtitle = mlc::Utf8ToW(v.releaseTime.substr(0, 10)) + L"   ·   " +
                         mlc::ChannelName(ch);
            g.badge = installed ? L"已安装" : L"";
            g.tint = channelColor(ch);
            g.action = (id == m_dlVersionId) ? L"已选" : L"选择";
            if (id == m_dlVersionId) sel = (int)cards.size();
            cards.push_back(std::move(g));
        }

        Widget list;
        list.type = Widget::Rows;
        list.id = L"dl.vers";
        list.items2 = std::move(cards);
        list.rc = { x, cy + 76, gridRight, m_height - 24 };
        list.rowH = 58;
        list.selIdx = sel;
        m_widgets.push_back(list);
        if (list.items2.empty())
            pushEmptyHint(m_widgets, list.rc, L"没有匹配的版本：换个分类，或清空搜索框。");

        // 右侧安装面板
        int px = gridRight + 16;
        int pw = right - px;
        m_widgets.push_back(L(L"dl.paneltitle", L"安装游戏", px, cy + 76, pw, 26, 1));

        int ry = cy + 108;
        m_widgets.push_back(L(L"dl.sellbl", L"已选版本", px, ry, 100, 22, 0));
        m_widgets.push_back(L(L"dl.selver",
            m_dlVersionId.empty() ? L"（点左边卡片选择）" : m_dlVersionId,
            px + 90, ry, pw - 90, 22, 1));
        ry += 30;

        m_widgets.push_back(L(L"dl.namelbl", L"游戏名称", px, ry, 100, 22, 0));
        ry += 22;
        m_widgets.push_back(E(L"dl.name", m_dlName.empty() ? m_dlVersionId : m_dlName,
                              px, ry, px + pw, ry + 30, L"任意语言，仅允许下划线"));
        ry += 36;
        m_widgets.push_back(L(L"dl.namehint", L"可用中文；符号只允许下划线 _", px, ry - 4, pw, 20, 0));
        ry += 22;

        static const wchar_t* kLoaders[] = { L"原版", L"Fabric", L"Quilt", L"Forge", L"NeoForge" };
        m_widgets.push_back(L(L"dl.loaderlbl", L"加载器（点图标选择）", px, ry, pw, 22, 0));
        ry += 22;
        {
            std::vector<GridItem> chips;
            for (int i = 0; i <= 4; ++i) {
                GridItem g;
                g.key = std::to_wstring(i);
                g.title = kLoaders[i];
                g.loaderIcon = i;   // 0 = 原版（画纯色块）
                g.tint = i == 0 ? RGB(140, 146, 158) : 0;
                chips.push_back(std::move(g));
            }
            int chipW = std::max(44, (pw - 4 * 12) / 5);
            Widget lg = GD(L"dl.loadergrid", chips, px, ry, px + pw, ry + 66, 5, chipW, 64);
            lg.selIdx = m_dlLoader;
            m_widgets.push_back(lg);
            ry += 82;
        }

        std::vector<std::wstring> lvers;
        lvers.push_back(L"自动（最新稳定）");
        for (auto& v : m_loaderVersions) lvers.push_back(v);
        int lvSel = 0;
        for (size_t i = 0; i < m_loaderVersions.size(); ++i)
            if (m_loaderVersions[i] == m_dlLoaderVer) lvSel = (int)i + 1;
        m_widgets.push_back(L(L"dl.loaderverlbl", L"加载器版本", px, ry, pw, 22, 0));
        ry += 22;
        m_widgets.push_back(CB(L"dl.loaderver", lvers, lvSel, px, ry, px + pw, ry + 30));
        ry += 42;

        m_widgets.push_back(B(L"dl.start", L"开始下载安装", px, ry, px + pw, ry + 44,
                              kPrimary));
        ry += 54;
        m_widgets.push_back(PG(L"dl.progress", px, ry, px + pw, ry + 10, m_progress));
        ry += 16;
        m_widgets.push_back(L(L"dl.status", m_progressText, px, ry, pw, 40, 0));
        return;
    }

    if (m_dlCat == 1) {
        BuildModDownload();
        return;
    }

    // ---- 内容（资源包 / 光影 / 世界）----
    static const wchar_t* kPh[] = { L"", L"搜索模组（支持中文，如 优化 / 小地图）",
                                    L"搜索资源包（支持中文）", L"搜索光影（支持中文）",
                                    L"搜索世界/地图" };
    m_widgets.push_back(E(L"dl.csearch", m_dlSearch, x, cy, x + 380, cy + 32, kPh[m_dlCat]));
    m_widgets.push_back(B(L"dl.csearchbtn", L"搜索", x + 388, cy, x + 500, cy + 32,
                          theme::ACCENT));
    m_widgets.push_back(L(L"dl.langhint", L"中英双语检索", x + 508, cy, 160, 32, 0));
    m_widgets.push_back(B(L"dl.import", L"＋ 从本地导入", x + 640, cy, x + 780, cy + 32, 0));
    if (m_dlSearch.empty()) {
        // HMCL 式浏览：没有搜索词时按热度列出全部，每页 20 条
        m_widgets.push_back(B(L"dl.prev", L"← 上一页", x + 788, cy, x + 888, cy + 32, 0));
        m_widgets.push_back(B(L"dl.next", L"下一页 →", x + 896, cy, x + 996, cy + 32, 0));
    }

    std::vector<GridItem> cards;
    int sel = -1;
    for (size_t i = 0; i < m_hits.size(); ++i) {
        const auto& h = m_hits[i];
        GridItem g;
        g.key = mlc::Utf8ToW(h.slug);
        g.title = mlc::Utf8ToW(h.title);
        g.subtitle = mlc::Utf8ToW(h.desc);
        g.cover = mlc::Utf8ToW(h.cover);
        g.badge = mlc::Utf8ToW(h.author);
        g.tint = theme::ACCENT;
        cards.push_back(std::move(g));
    }
    int cardW = 180, cardH = 214;
    Widget grid = GD(L"dl.results", cards, x, cy + 42, right, m_height - 110,
                     gridCols(cw, cardW), cardW, cardH);
    m_widgets.push_back(grid);
    if (cards.empty())
        pushEmptyHint(m_widgets, grid.rc,
                      m_dlSearch.empty()
                          ? L"正在加载全部热门项目…（每页 20 条，可用「下一页」翻页）"
                          : L"没有找到匹配结果。试试英文关键词，或换个分类。");

    m_widgets.push_back(B(L"dl.cdownload", L"下载选中", x, m_height - 96, x + 200,
                          m_height - 58, kPrimary));
    m_widgets.push_back(B(L"dl.copenfolder", L"打开目标目录", x + 208, m_height - 96,
                          x + 380, m_height - 58, 0));
    std::wstring dlStatus = m_dlSearching ? L"正在搜索…" : m_progressText;
    if (m_dlSearch.empty() || !m_hits.empty())
        dlStatus += L"  ·  第 " + std::to_wstring(m_dlOffset / 20 + 1) + L" 页";
    m_widgets.push_back(L(L"dl.status", dlStatus,
        x + 392, m_height - 96, cw - 400, 38, 0));
}

// ============================================================
// 模组下载：完全照 HMCL DownloadListPage / instances.DownloadPage 的布局
// ============================================================
namespace {

// 版本号倒序（1.21 > 1.20.1 > 1.19.4）
bool versionGreater(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& s) {
        std::vector<int> v;
        std::string cur;
        for (char c : s) {
            if (c >= '0' && c <= '9') cur.push_back(c);
            else if (!cur.empty()) { v.push_back(atoi(cur.c_str())); cur.clear(); }
        }
        if (!cur.empty()) v.push_back(atoi(cur.c_str()));
        return v;
    };
    auto x = parts(a), y = parts(b);
    for (size_t i = 0; i < x.size() && i < y.size(); ++i)
        if (x[i] != y[i]) return x[i] > y[i];
    return x.size() > y.size();
}

std::wstring channelName(const std::string& t) {
    if (t == "release") return L"稳定版本";
    if (t == "beta") return L"测试版本";
    if (t == "alpha") return L"快照版本";
    return L"";
}

COLORREF channelColorFor(const std::string& t) {
    if (t == "release") return RGB(67, 160, 71);
    if (t == "beta") return RGB(239, 140, 0);
    if (t == "alpha") return RGB(216, 79, 178);
    return RGB(120, 120, 130);
}

} // namespace

void Ui::BuildModDownload() {
    int x = CX, y = CT;
    int right = m_width - 32;
    int cw = right - x;
    int cy = y + 116;

    // 版本确认弹窗（Modal）：只构建弹窗自己的控件
    if (m_addonShowDlg) {
        int dw = std::min(880, m_width - 80), dh = std::min(620, m_height - 60);
        int dx = (m_width - dw) / 2, dy = (m_height - dh) / 2;
        m_widgets.push_back(L(L"ad.title",
            L"模组下载 - " + mlc::Utf8ToW(m_addonVersion.name),
            dx + 24, dy + 16, dw - 48, 30, 2));

        GridItem head;
        head.title = mlc::Utf8ToW(m_addonVersion.name);
        head.subtitle = mlc::Utf8ToW(m_addonVersion.datePublished.substr(0, 10)) + L"  ·  " +
                        channelName(m_addonVersion.versionType);
        head.cover = mlc::Utf8ToW(m_addonProject.cover);
        for (auto& l : m_addonVersion.loaders) head.tags.push_back(mlc::Utf8ToW(addon::TranslateLoader(l)));
        head.tagColor = channelColorFor(m_addonVersion.versionType);
        Widget hw;
        hw.type = Widget::Rows;
        hw.id = L"ad.head";
        hw.items2.push_back(head);
        hw.rc = { dx + 16, dy + 54, dx + dw - 16, dy + 130 };
        hw.rowH = 72;
        m_widgets.push_back(hw);

        m_widgets.push_back(B(L"ad.changelog", L"更新日志", dx + 16, dy + 140,
                              dx + 132, dy + 172));
        m_widgets.push_back(B(L"ad.page", L"官方页面", dx + 140, dy + 140,
                              dx + 256, dy + 172));
        m_widgets.push_back(L(L"ad.depslbl", L"前置内容", dx + 16, dy + 184, 200, 22, 1));

        std::vector<GridItem> deps;
        std::string lastType;
        for (const auto& d : m_addonDeps) {
            if (d.type == addon::Dependency::Type::Incompatible) continue;
            std::string key = d.type == addon::Dependency::Type::Required ? "required"
                            : d.type == addon::Dependency::Type::Optional ? "optional"
                            : d.type == addon::Dependency::Type::Embedded ? "embedded" : "broken";
            if (key != lastType) {
                GridItem sec;
                sec.section = true;
                sec.title = key == "required" ? L"必需的前置内容"
                          : key == "optional" ? L"可选的前置内容"
                          : key == "embedded" ? L"内置的前置内容" : L"损坏的前置内容";
                deps.push_back(sec);
                lastType = key;
            }
            GridItem g;
            g.key = mlc::Utf8ToW(d.projectId);
            g.title = mlc::Utf8ToW(d.name.empty() ? d.projectId : d.name);
            g.subtitle = L"点击查看详情";
            deps.push_back(g);
        }
        if (m_addonDepsLoading)
            deps.push_back({});
        Widget dl;
        dl.type = Widget::Rows;
        dl.id = L"ad.deps";
        dl.items2 = std::move(deps);
        dl.rc = { dx + 16, dy + 210, dx + dw - 16, dy + dh - 150 };
        dl.rowH = 54;
        m_widgets.push_back(dl);

        m_widgets.push_back(L(L"ad.namelbl", L"文件名", dx + 16, dy + dh - 142, 70, 22, 0));
        m_widgets.push_back(E(L"ad.fname", m_addonFileName, dx + 88, dy + dh - 146,
                              dx + dw - 16, dy + dh - 114, L""));
        m_widgets.push_back(L(L"ad.status",
            m_addonDepsLoading ? L"正在加载前置内容…" : L"确认后下载到当前实例的 mods 目录",
            dx + 16, dy + dh - 104, dw - 32, 22, 0));

        m_widgets.push_back(B(L"ad.install", L"安装到当前实例", dx + dw - 446, dy + dh - 66,
                              dx + dw - 264, dy + dh - 26, kPrimary));
        m_widgets.push_back(B(L"ad.saveas", L"下载到本地文件夹", dx + dw - 256, dy + dh - 66,
                              dx + dw - 108, dy + dh - 26));
        m_widgets.push_back(B(L"ad.cancel", L"取消", dx + dw - 100, dy + dh - 66,
                              dx + dw - 16, dy + dh - 26));
        return;
    }

    if (m_addonShowChangelog) {
        int dw = std::min(880, m_width - 80), dh = std::min(620, m_height - 60);
        int dx = (m_width - dw) / 2, dy = (m_height - dh) / 2;
        m_widgets.push_back(L(L"ac.title",
            L"更新日志 - " + mlc::Utf8ToW(m_addonVersion.name),
            dx + 24, dy + 16, dw - 48, 30, 2));
        m_widgets.push_back(L(L"ac.text", m_addonChangelog.empty()
                              ? L"暂无更新日志" : m_addonChangelog,
                              dx + 24, dy + 60, dw - 48, dh - 130, 0));
        m_widgets.push_back(B(L"ac.close", L"确定", dx + dw - 144, dy + dh - 56,
                              dx + dw - 24, dy + dh - 20, kPrimary));
        return;
    }

    if (m_addonDetail) {
        // ---- 项目详情：头部 + 推荐版本 + 按 MC 版本分组的版本列表 ----
        m_widgets.push_back(B(L"ad.back", L"← 返回搜索", x, cy, x + 110, cy + 30));
        m_widgets.push_back(B(L"ad.page", L"官方页面", right - 132, cy, right, cy + 30));

        GridItem head;
        head.title = addon::LocalizedTitle(m_addonProject.projectId, m_addonProject.title);
        head.subtitle = m_addonProject.desc;
        head.cover = mlc::Utf8ToW(m_addonProject.cover);
        for (const auto& c : m_addonProject.categories) {
            std::wstring t = mlc::Utf8ToW(addon::LocalizedCategory(c));
            if (t != L"minecraft") head.tags.push_back(t);
        }
        Widget hw;
        hw.type = Widget::Rows;
        hw.id = L"ad.project";
        hw.items2.push_back(head);
        hw.rc = { x, cy + 38, right, cy + 122 };
        hw.rowH = 80;
        m_widgets.push_back(hw);

        m_widgets.push_back(L(L"ad.status",
            m_addonVersionsLoading ? L"正在加载版本…" :
            (m_addonVersions.empty() ? L"下载失败，点击「重试」" : L""),
            x + 4, cy + 128, cw - 120, 24, 0));
        m_widgets.push_back(B(L"ad.retry", L"重试", right - 100, cy + 124, right, cy + 154));

        std::vector<GridItem> rows;
        mlc::Instance* in = CurInst();
        std::string curVer = in ? in->versionId : mlc::WToUtf8(m_addonMcVer);
        std::string curLoader = in ? in->loader : "";
        auto supported = addon::SupportedLoaders(curLoader, curVer, in ? in->modsDir() : "");
        auto loaderMatch = [&](const addon::Version& v) {
            for (const auto& l : v.loaders)
                for (const auto& s : supported)
                    if (addon::IsKnownLoader(l) && l == s) return true;
            return false;
        };

        // HMCL: 推荐版本 = 当前 MC 版本 + 当前加载器匹配的第一个（已按时间倒序）
        int recommended = -1;
        if (!curVer.empty()) {
            for (size_t i = 0; i < m_addonVersions.size(); ++i) {
                const auto& v = m_addonVersions[i];
                if (std::find(v.gameVersions.begin(), v.gameVersions.end(), curVer) == v.gameVersions.end()) continue;
                if (!loaderMatch(v)) continue;
                recommended = (int)i;
                break;
            }
        }
        auto makeVersionRow = [](const addon::Version& v) {
            GridItem g;
            g.key = mlc::Utf8ToW(v.id);
            g.title = mlc::Utf8ToW(v.name);
            g.subtitle = mlc::Utf8ToW(v.datePublished.substr(0, 10)) + L"  ·  " +
                         channelName(v.versionType);
            for (const auto& l : v.loaders)
                g.tags.push_back(mlc::Utf8ToW(addon::TranslateLoader(l)));
            g.tagColor = channelColorFor(v.versionType);
            return g;
        };
        if (recommended >= 0) {
            GridItem sec;
            sec.section = true;
            sec.title = L"推荐版本 - Minecraft " + mlc::Utf8ToW(curVer);
            rows.push_back(sec);
            rows.push_back(makeVersionRow(m_addonVersions[recommended]));
        }

        std::map<std::string, std::vector<const addon::Version*>, std::greater<std::string>> grouped;
        for (const auto& v : m_addonVersions)
            for (const auto& gv : v.gameVersions) grouped[gv].push_back(&v);
        for (auto& kv : grouped) {
            bool snap = addon::IsSnapshotVersion(kv.first);
            GridItem sec;
            sec.section = true;
            sec.title = L"Minecraft " + mlc::Utf8ToW(kv.first) + (snap ? L" (快照)" : L"");
            rows.push_back(sec);
            for (const addon::Version* v : kv.second) rows.push_back(makeVersionRow(*v));
        }

        const bool noRows = rows.empty();
        Widget vw;
        vw.type = Widget::Rows;
        vw.id = L"ad.versions";
        vw.items2 = std::move(rows);
        vw.rc = { x, cy + 162, right, m_height - 20 };
        vw.rowH = 62;
        m_widgets.push_back(vw);
        if (noRows && !m_addonVersionsLoading)
            pushEmptyHint(m_widgets, vw.rc, L"暂无可用版本");
        return;
    }

    // ---- 搜索列表：HMCL DownloadListPage 的 GridPane 搜索卡片 + ListView ----
    m_widgets.push_back(P(L"ad.searchcard", x, cy, right, cy + 128));
    m_widgets.push_back(L(L"ad.namelbl", L"名称", x + 16, cy + 14, 70, 24, 0));
    m_widgets.push_back(E(L"dl.csearch", m_dlSearch, x + 84, cy + 8, x + 470, cy + 40,
                          L"支持中英文搜索"));
    m_widgets.push_back(L(L"ad.verlbl", L"游戏版本", x + 490, cy + 14, 80, 24, 0));
    m_widgets.push_back(E(L"dl.mcver", m_addonMcVer, x + 570, cy + 8, right - 16, cy + 40,
                          L"可在此处输入（留空 = 全部）"));
    m_widgets.push_back(L(L"ad.catlbl", L"类别", x + 16, cy + 52, 70, 24, 0));
    if (m_addonCategoryNames.empty()) m_addonCategoryNames.push_back(L"全部");
    m_widgets.push_back(CB(L"ad.category", m_addonCategoryNames,
                           std::clamp(m_addonCategorySel, 0, (int)m_addonCategoryNames.size() - 1),
                           x + 84, cy + 46, x + 470, cy + 78));
    static const wchar_t* kSorts[] = { L"相关度", L"热度", L"创建日期", L"最近更新", L"下载量" };
    m_widgets.push_back(L(L"ad.sortlbl", L"排序", x + 490, cy + 52, 80, 24, 0));
    m_widgets.push_back(CB(L"ad.sort", std::vector<std::wstring>(kSorts, kSorts + 5),
                           std::clamp(m_addonSort, 0, 4),
                           x + 570, cy + 46, right - 16, cy + 78));

    // HMCL 的翻页条：第一页 / 上一页 / 页码 / 下一页 / 最后一页 / … / 搜索
    int by = cy + 88;
    m_widgets.push_back(B(L"addon.first", L"第一页", x + 16, by, x + 104, by + 30));
    m_widgets.push_back(B(L"addon.prev", L"上一页", x + 112, by, x + 200, by + 30));
    m_widgets.push_back(L(L"addon.pages",
        L"第 " + std::to_wstring(m_addonPage + 1) + L" / " +
        (m_addonPages <= 0 ? L"-" : std::to_wstring(m_addonPages)) + L" 页",
        x + 208, by, 108, 30, 0));
    m_widgets.push_back(B(L"addon.next", L"下一页", x + 324, by, x + 412, by + 30));
    m_widgets.push_back(B(L"addon.last", L"最后一页", x + 420, by, x + 524, by + 30));
    m_widgets.push_back(B(L"addon.search", L"搜索", right - 124, by, right - 16, by + 30,
                          kPrimary));

    std::vector<GridItem> cards;
    for (const auto& h : m_addonHits) {
        GridItem g;
        g.key = mlc::Utf8ToW(h.projectId);
        g.title = addon::LocalizedTitle(h.projectId, h.title);
        g.subtitle = h.desc;
        g.cover = mlc::Utf8ToW(h.cover);
        for (const auto& c : h.categories) {
            std::wstring t = mlc::Utf8ToW(addon::LocalizedCategory(c));
            if (t != L"minecraft") g.tags.push_back(t);
        }
        cards.push_back(std::move(g));
    }
    Widget list;
    list.type = Widget::Rows;
    list.id = L"addon.results";
    list.items2 = std::move(cards);
    list.rc = { x, cy + 138, right, m_height - 20 };
    list.rowH = 74;
    m_widgets.push_back(list);
    m_widgets.push_back(L(L"addon.status",
        m_dlSearching ? L"正在搜索…"
                      : (m_addonHits.empty() ? L"搜索无结果" : L""),
        x + 4, m_height - 32, cw - 8, 24, 0));
}

// ============================================================
// 账号（多账号保存 + 选择 + 正版续期）
// ============================================================
void Ui::BuildAccount() {
    launcher::App& app = launcher::App::Instance();
    mlc::Store& store = mlc::Store::I();
    int x = CX, y = CT;
    int right = m_width - 32;
    int cw = right - x;

    m_widgets.push_back(L(L"acc.title", L"账号", x, y, 200, 32, 2));

    // ---- 离线账号创建弹窗（模态，只构建弹窗自己的控件）----
    if (m_showOfflineDlg) {
        m_skinBox = { 0, 0, 0, 0 };
        int dw = 420, dh = 210;
        int dx = (m_width - dw) / 2, dy = (m_height - dh) / 2;
        m_widgets.push_back(L(L"off.title", L"创建离线账号", dx + 24, dy + 18, 260, 28, 2));
        m_widgets.push_back(L(L"off.lbl", L"玩家名", dx + 24, dy + 58, 120, 22, 0));
        m_widgets.push_back(E(L"off.name", m_loginName.empty() ? L"Steve" : m_loginName,
                              dx + 24, dy + 84, dx + dw - 24, dy + 116, L"1–16 位字母/数字/下划线"));
        m_widgets.push_back(L(L"off.tip",
            L"离线 UUID = MD5(\"OfflinePlayer:\" + 名字)，与 HMCL / 正版 ID 规则一致。",
            dx + 24, dy + 122, dw - 48, 20, 0));
        m_widgets.push_back(L(L"off.uuid", L"登录后可在这里给这个账号设置皮肤。",
                              dx + 24, dy + 144, dw - 48, 20, 0));
        m_widgets.push_back(B(L"off.cancel", L"取消", dx + dw - 200, dy + dh - 56,
                              dx + dw - 116, dy + dh - 20, 0));
        m_widgets.push_back(B(L"off.ok", L"创建", dx + dw - 108, dy + dh - 56,
                              dx + dw - 24, dy + dh - 20, kPrimary));
        return;
    }

    auto acc = app.account();
    std::string curKey = store.AccountKey();
    std::wstring who = acc.name.empty()
        ? L"当前未登录"
        : (L"当前账号：" + mlc::Utf8ToW(acc.name) + L"  ·  " +
           (app.accountType() == "msa" ? L"微软正版" : L"离线"));
    m_widgets.push_back(L(L"acc.cur", who, x, y + 38, cw, 28, 1));

    // 账号的「便携 / 全局」由每个账号自己决定（照 HMCL），列表每行的副标题里标出来，
    // 选中账号后点下面的「移到全局 / 移到便携」即可单个迁移。

    // ---- 已保存账号列表（左侧，带皮肤头像）----
    int listW = std::min(560, std::max(320, cw - 320));
    m_widgets.push_back(L(L"acc.listlbl", L"已保存账号（点一行选中）",
                          x, y + 88, listW, 22, 1));
    {
        Widget lst;
        lst.type = Widget::List;
        lst.id = L"acc.list";
        lst.rc = { x, y + 114, x + listW, y + 114 + 34 * 5 };
        int curSel = -1;
        auto& list = store.accounts();
        for (size_t i = 0; i < list.size(); ++i) {
            lst.listItems.push_back(mlc::Utf8ToW(list[i].name));
            std::wstring sub = list[i].type == "msa" ? L"微软正版" : L"离线";
            if (list[i].type != "msa" && list[i].uuid.size() >= 8) {
                sub += L" · UUID " + mlc::Utf8ToW(list[i].uuid.substr(0, 8)) + L"…";
            }
            if (list[i].type == "msa" && list[i].expiresAt > 0) {
                int64_t now = (int64_t)time(nullptr);
                sub += list[i].expiresAt > now ? L" · 令牌有效" : L" · 令牌过期（需重新登录）";
            }
            if ((list[i].uuid.empty() ? list[i].name : list[i].uuid) == curKey)
                sub += L" · 当前";
            // 照 HMCL：副标题里标出这个账号存在哪一侧
            sub += list[i].portable ? L" · 便携" : L" · 全局";
            lst.subItems.push_back(sub);
            lst.rowIcons.push_back(store.EffectiveSkin(list[i]));
            if ((list[i].uuid.empty() ? list[i].name : list[i].uuid) == curKey) curSel = (int)i;
        }
        // 选中项：优先保留用户点的那一行（m_accSel），无效时才回到「当前账号」。
        // 之前每次 Rebuild 都强制 = 当前账号，导致点了别的账号又被弹回去、切不动。
        if (m_accSel < 0 || m_accSel >= (int)list.size()) m_accSel = curSel;
        lst.selIdx = m_accSel;
        m_widgets.push_back(lst);

        int by = y + 114 + 34 * 5 + 10;
        // 三个按钮平分列表宽度（右侧要留给皮肤区，不能超出 listW）
        int bx1 = x;
        int bx2 = x + listW * 38 / 100;
        int bx3 = x + listW * 66 / 100;
        int bx4 = x + listW - 8;
        m_widgets.push_back(B(L"acc.use", L"使用选中", bx1, by, bx2 - 6, by + 32, kPrimary));
        m_widgets.push_back(B(L"acc.del", L"删除", bx2, by, bx3 - 6, by + 32, theme::RED));
        // 单个账号在「便携 / 全局」之间迁移（HMCL 的按钮语义）
        {
            mlc::AccountRec* sel = (m_accSel >= 0 && m_accSel < (int)list.size())
                                   ? &list[m_accSel] : nullptr;
            bool portable = sel ? sel->portable : true;
            m_widgets.push_back(B(L"acc.move",
                portable ? L"移到全局" : L"移到便携",
                bx3, by, bx4, by + 32, 0));
        }
        if (list.empty()) {
            m_widgets.push_back(L(L"acc.empty",
                L"还没有账号：下面点「离线模式」建一个，或「微软登录」登正版。",
                x + 12, y + 128, listW - 24, 24, 0));
        }
    }

    // ---- 右侧：皮肤（3D 预览 + 操作，方式对齐 HMCL）----
    int selIdx = -1;
    if (Widget* w = Find(L"acc.list")) selIdx = w->selIdx;
    auto& accs = store.accounts();
    mlc::AccountRec* pick = (selIdx >= 0 && selIdx < (int)accs.size()) ? &accs[selIdx] : nullptr;
    {
        int skinX = x + listW + 20;
        int skinW = right - skinX;
        if (skinW >= 176) {
            int skinTop = y + 88;
            int skinH = 258;
            m_skinBox = { skinX, skinTop, skinX + skinW, skinTop + skinH };

            const int half = (skinW - 8) / 2;
            const int rx2 = skinX + half + 8;
            int by = skinTop + skinH + 10;

            m_widgets.push_back(B(L"acc.skinpick", L"选择皮肤", skinX, by, skinX + half,
                                  by + 32, 0));
            m_widgets.push_back(B(L"acc.skindel", L"移除", rx2, by, skinX + skinW, by + 32,
                                  theme::RED));

            by += 38;
            bool slim = pick ? store.IsSlim(*pick) : false;
            m_widgets.push_back(B(L"acc.model.classic", L"经典", skinX, by, skinX + half,
                                  by + 32, 0, !slim));
            m_widgets.push_back(B(L"acc.model.slim", L"纤细", rx2, by, skinX + skinW,
                                  by + 32, 0, slim));

            by += 38;
            m_widgets.push_back(B(L"acc.skinpath", L"皮肤路径", skinX, by, skinX + half,
                                  by + 32, 0));
            m_widgets.push_back(B(L"acc.capepick", L"披风…", rx2, by, skinX + skinW,
                                  by + 32, 0));

            by += 38;
            if (pick && pick->type == "msa") {
                m_widgets.push_back(B(L"acc.skinup", L"上传正版", skinX, by, skinX + half,
                                      by + 32, theme::ACCENT));
                m_widgets.push_back(B(L"acc.skinpull", L"正版同步", rx2, by, skinX + skinW,
                                      by + 32, 0));
            } else {
                m_widgets.push_back(L(L"acc.skinoffline",
                    L"离线账号：皮肤只在本机显示（上传 / 同步仅正版可用）",
                    skinX, by, skinW, 32, 0));
            }

            by += 36;
            m_widgets.push_back(L(L"acc.skinmsg", m_skinMsg, skinX, by, skinW, 34, 0));
        } else {
            m_skinBox = { 0, 0, 0, 0 };
        }
    }

    // ---- 底部：离线模式 / 微软登录（宽度限制在列表列内，右侧让给皮肤区）----
    int ry = y + 114 + 34 * 5 + 52;
    int noteW = std::max(120, listW - 340);
    m_widgets.push_back(B(L"acc.offline", L"离线模式", x, ry, x + 150, ry + 44, kPrimary));
    m_widgets.push_back(B(L"acc.mslogin",
        m_msPending ? L"登录中…（重新开始）" : L"微软登录",
        x + 158, ry, x + 318, ry + 44, theme::ACCENT));
    m_widgets.push_back(L(L"acc.offlinetip",
        L"「离线模式」建一个不需要正版的本地账号（玩单人/离线服）。",
        x + 330, ry + 4, noteW, 20, 0));
    m_widgets.push_back(L(L"acc.msstatus",
        m_msStatus.empty()
            ? L"「微软登录」用设备码授权，不需要密码。"
            : m_msStatus,
        x + 330, ry + 24, noteW, 20, 0));

    std::wstring codeText;
    if (!m_msUserCode.empty()) {
        codeText = L"在 " + (m_msVerifyUri.empty() ? L"https://www.microsoft.com/link" : m_msVerifyUri) +
                   L" 输入代码：" + m_msUserCode;
    }
    m_widgets.push_back(L(L"acc.code", codeText, x, ry + 56, listW, 26, 1));
    m_widgets.push_back(L(L"acc.tip",
        msauth::UsingCustomClient()
            ? L"已配置自定义 Azure 应用 ID：登录会保存 refresh_token，令牌过期自动续期。"
            : L"内置应用 ID 可正常登录并游玩正版；微软不允许它申请 offline_access，"
              L"令牌约 24 小时后过期，需要再点一次「微软登录」（想自动续期可在全局设置里填自己的 Azure 应用 ID）。",
        x, ry + 86, listW, 44, 0));

    // ---- 预览正版账号且本地还没皮肤时，自动从 Mojang 拉一次 ----
    // 每个账号每次运行只自动试一次，避免无网络时反复请求；用户仍可手动点「正版同步」。
    if (pick) {
        std::wstring key = mlc::Utf8ToW(pick->uuid.empty() ? pick->name : pick->uuid);
        m_skinSelKey = key;
        if (pick->type == "msa" && !m_skinBusy && m_skinAutoKey != key &&
            store.EffectiveSkin(*pick).empty()) {
            m_skinAutoKey = key;
            if (m_sink) {
                m_sink->post((int)launcher::Ev::Info, "__skinfetch__:" + mlc::WToUtf8(key));
            }
        }
    } else {
        m_skinSelKey.clear();
    }
}

// ============================================================
// 全局设置
// ============================================================
void Ui::BuildSettings() {
    launcher::App& app = launcher::App::Instance();
    mlc::Store& store = mlc::Store::I();
    mlc::Settings& gs = store.spec();
    bg::Manager& bm = bg::Manager::Instance();

    int x = CX;
    int right = m_width - 32;
    int cw = right - x;
    int ry = CT - m_scrollY;

    m_widgets.push_back(L(L"set.title", L"全局游戏设置", x, ry, 400, 32, 2));
    ry += 40;
    m_widgets.push_back(L(L"set.hint",
        L"游戏目录与 Java 已改为自动：游戏在「版本选择」里按文件夹管理，Java 自动检测并在每个实例里可覆盖。",
        x, ry, cw, 24, 0));
    ry += 34;

    // 内存滑块
    m_widgets.push_back(L(L"set.memlbl", L"全局最大内存：" + std::to_wstring(gs.maxMemoryMB) + L" MB",
                          x, ry, 420, 22, 1));
    ry += 24;
    {
        Widget s;
        s.type = Widget::Slider;
        s.id = L"set.mem";
        s.vmin = 512;
        s.vmax = 32768;
        s.value = gs.maxMemoryMB;
        s.rc = { x, ry, x + 520, ry + 26 };
        m_widgets.push_back(s);
    }
    ry += 40;

    // 下载源（MCBBS 镜像已随站点下线；现在给「自动 / BMCLAPI / 官方」三条，和 HMCL 一致）
    m_widgets.push_back(L(L"set.srclbl", L"下载源", x, ry, 200, 22, 0));
    ry += 22;
    std::vector<std::wstring> srcs = { L"自动（镜像优先，失败回落官方）",
                                       L"BMCLAPI 镜像",
                                       L"Mojang 官方" };
    int srcSel = (gs.source == "mojang") ? 2 : (gs.source == "bmclapi" ? 1 : 0);
    m_widgets.push_back(CB(L"set.source", srcs, srcSel, x, ry, x + 300, ry + 30));
    // 下载线程数
    {
        std::vector<std::wstring> th = { L"8 线程", L"16 线程（推荐）", L"24 线程", L"32 线程" };
        static const int kTh[] = { 8, 16, 24, 32 };
        int tsel = 1;
        for (int i = 0; i < 4; ++i) if (gs.downloadThreads == kTh[i]) tsel = i;
        m_widgets.push_back(L(L"set.thlbl", L"下载线程数（资源按 2 倍并发）", x + 316, ry - 22, 320, 22, 0));
        m_widgets.push_back(CB(L"set.threads", th, tsel, x + 316, ry, x + 500, ry + 30));
    }
    ry += 42;

    // 开关
    m_widgets.push_back(CK(L"set.isolate", L"版本隔离（每个版本独立的 mods / saves）",
                           x, ry, x + 520, ry + 28, gs.isolate));
    ry += 34;
    m_widgets.push_back(CK(L"set.integrity", L"启动时检查游戏完整性", x, ry, x + 520, ry + 28,
                           gs.checkIntegrity));
    ry += 34;
    m_widgets.push_back(CK(L"set.repair", L"检查到缺失文件时自动修复（会重新下载）",
                           x, ry, x + 620, ry + 28, gs.autoRepair));
    ry += 34;
    m_widgets.push_back(CK(L"set.backsave", L"启动前自动备份存档到 Data\\mlc\\backups",
                           x, ry, x + 620, ry + 28, gs.backupSaves));
    ry += 38;

    // 默认分辨率
    m_widgets.push_back(L(L"set.sizelbl", L"默认窗口大小", x, ry, 200, 22, 0));
    ry += 22;
    m_widgets.push_back(E(L"set.width", std::to_wstring(gs.width), x, ry, x + 110, ry + 30, L"宽"));
    m_widgets.push_back(E(L"set.height", std::to_wstring(gs.height), x + 120, ry, x + 230,
                          ry + 30, L"高"));
    ry += 42;

    // Java（自动检测）
    m_widgets.push_back(L(L"set.javalbl", L"Java（自动检测）", x, ry, 260, 22, 1));
    ry += 22;
    {
        if (!m_javasLoaded) { m_javas = app.detectJava(); m_javasLoaded = true; }
        auto& javas = m_javas;
        std::vector<std::wstring> items;
        items.push_back(L"自动选择（推荐）");
        int sel = 0;
        for (size_t i = 0; i < javas.size(); ++i) {
            std::wstring t = mlc::Utf8ToW(javas[i].version) + L"  " +
                             mlc::Utf8ToW(javas[i].path);
            items.push_back(t);
            if (!gs.javaPath.empty() && javas[i].path == gs.javaPath) sel = (int)i + 1;
        }
        if (javas.empty()) items.push_back(L"未检测到 Java，请安装 JDK 17 或 8");
        m_widgets.push_back(CB(L"set.java", items, sel, x, ry, x + 520, ry + 30));
        m_widgets.push_back(B(L"set.javaredetect", L"⟳ 重新检测", x + 530, ry, x + 660, ry + 30, 0));
        ry += 38;
    }

    // 微软登录应用 ID（可选）
    m_widgets.push_back(L(L"set.msidlbl", L"微软登录应用 ID（可选）", x, ry, 500, 22, 0));
    ry += 22;
    m_widgets.push_back(E(L"set.msid", mlc::Utf8ToW(gs.msClientId), x, ry, x + 420, ry + 30,
                          L"Azure 应用(客户端)ID，留空 = 内置"));
    m_widgets.push_back(L(L"set.msidtip",
                          L"填自己的公共客户端 ID 才能保存 refresh_token 并自动续期",
                          x + 430, ry, 470, 30, 0));
    ry += 40;

    // 背景
    m_widgets.push_back(L(L"set.bglbl", L"背景", x, ry, 200, 22, 1));
    ry += 22;
    std::vector<std::wstring> modes = { L"纯色", L"内置壁纸 2021-08-26", L"内置壁纸 2016-02-25",
                                        L"内置壁纸 2015-06-22", L"自定义图片", L"目录轮播" };
    int msel = 0;
    if (bm.Cfg().mode == 0) msel = 0;
    else if (bm.Cfg().mode == 1) msel = 1 + bm.Cfg().builtinIndex;
    else if (bm.Cfg().mode == 2) msel = 4;
    else msel = 5;
    m_widgets.push_back(CB(L"set.bgmode", modes, msel, x, ry, x + 300, ry + 30));
    m_widgets.push_back(E(L"set.bgimage", bm.Cfg().image, x + 310, ry, x + 700, ry + 30,
                          L"背景图片路径"));
    m_widgets.push_back(B(L"set.bgpick", L"选择图片", x + 708, ry, x + 808, ry + 30, 0));
    ry += 38;
    m_widgets.push_back(E(L"set.bgdir", bm.Cfg().cycleDir, x, ry, x + 390, ry + 30,
                          L"轮播目录"));
    m_widgets.push_back(B(L"set.bgdirpick", L"选择目录", x + 398, ry, x + 498, ry + 30, 0));
    m_widgets.push_back(CK(L"set.blur", L"磨砂卡片", x + 510, ry, x + 640, ry + 30, bm.Cfg().blur));
    ry += 40;

    // 服务器
    m_widgets.push_back(L(L"set.srvlbl", L"服务器直连（实例未单独指定时生效）", x, ry, 500, 22, 1));
    ry += 24;
    {
        Widget lst;
        lst.type = Widget::List;
        lst.id = L"set.srvlist";
        lst.rc = { x, ry, x + 380, ry + 96 };
        for (auto& s : app.servers()) {
            lst.listItems.push_back(mlc::Utf8ToW(s.first));
            lst.subItems.push_back(std::to_wstring(s.second));
        }
        m_widgets.push_back(lst);
        m_widgets.push_back(E(L"set.srvaddr", L"", x + 392, ry, x + 660, ry + 30,
                              L"host 或 host:port"));
        m_widgets.push_back(B(L"set.srvadd", L"＋ 添加", x + 668, ry, x + 768, ry + 30, 0));
        m_widgets.push_back(B(L"set.srvdel", L"删除选中", x + 392, ry + 38, x + 498, ry + 68, 0));
        m_widgets.push_back(B(L"set.srvdir", L"设为直连", x + 506, ry + 38, x + 612, ry + 68,
                              theme::ACCENT));
        m_widgets.push_back(B(L"set.srvdotsu", L"取消直连", x + 620, ry + 38, x + 726, ry + 68, 0));
        ry += 106;
    }

    // CurseForge（世界在线下载）
    m_widgets.push_back(L(L"set.cflbl", L"CurseForge（世界在线下载）", x, ry, 500, 22, 1));
    ry += 22;
    m_widgets.push_back(L(L"set.cfhint",
        L"默认走公共镜像 mod.mcimirror.top（免 Key）；改用官方地址需填 API Key（console.curseforge.com 免费申请）",
        x, ry, cw, 22, 0));
    ry += 26;
    m_widgets.push_back(E(L"set.cfbase", mlc::Utf8ToW(gs.cfBase), x, ry, x + 520, ry + 30,
                          L"https://api.curseforge.com/v1"));
    m_widgets.push_back(B(L"set.cfreset", L"恢复默认镜像", x + 530, ry, x + 680, ry + 30, 0));
    ry += 38;
    m_widgets.push_back(E(L"set.cfkey", mlc::Utf8ToW(gs.cfApiKey), x, ry, x + 520, ry + 30,
                          L"x-api-key（可选，公共镜像不需要）"));
    ry += 42;

    m_widgets.push_back(B(L"set.save", L"保存设置", x, ry, x + 170, ry + 40, kPrimary));
    m_widgets.push_back(B(L"set.opendata", L"打开 Data\\mlc", x + 182, ry, x + 360, ry + 40, 0));
    m_widgets.push_back(L(L"set.status", L"", x + 372, ry, cw - 380, 40, 0));
}

} // namespace ui
