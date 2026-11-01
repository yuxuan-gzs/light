// Ui.cpp — 自绘主窗口实现
#include "Ui.h"
#include "Theme.h"
#include "Background.h"
#include "LauncherApp.h"
#include "ModManager.h"
#include "Json.h"

#include <filesystem>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <thread>

namespace ui {

namespace fs = std::filesystem;

static constexpr UINT WM_APP_EVENT = WM_APP + 10;
static Ui* g_ui = nullptr;

// 异步事件回调（后台线程）：入队 + 通知 UI 线程
static void AsyncEvent(launcher::Ev type, const std::string& data) {
    if (!g_ui) return;
    g_ui->OnAsyncEvent((int)type, data);
}

Ui::~Ui() {
    if (g_ui == this) g_ui = nullptr;
}

// ============================================================
// 窗口过程
// ============================================================
LRESULT CALLBACK Ui::WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    Ui* self = (Ui*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        self = (Ui*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->m_hwnd = h;
    }
    if (!self) return DefWindowProcW(h, msg, w, l);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        self->Paint(hdc, ps.rcPaint);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        self->OnMouseMove((short)LOWORD(l), (short)HIWORD(l)); return 0;
    case WM_LBUTTONDOWN:
        SetFocus(h); self->OnLButtonDown((short)LOWORD(l), (short)HIWORD(l)); return 0;
    case WM_LBUTTONUP:
        self->OnLButtonUp((short)LOWORD(l), (short)HIWORD(l)); return 0;
    case WM_CHAR:
        self->OnChar((wchar_t)w); return 0;
    case WM_KEYDOWN:
        self->OnKeyDown(w); return 0;
    case WM_MOUSEWHEEL:
        self->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(w), (short)LOWORD(l), (short)HIWORD(l)); return 0;
    case WM_TIMER:
        if (w == 1) self->OnTimerTick(); return 0;
    case WM_APP_EVENT:
        self->OnTimerTick(); return 0;  // 事件到达后刷新状态
    case WM_SIZE: {
        int nw = LOWORD(l), nh = HIWORD(l);
        if (nw != self->m_width || nh != self->m_height) {
            self->m_width = nw;
            self->m_height = nh;
            self->Rebuild();
        }
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, 1);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

// ============================================================
// 创建 / 页面
// ============================================================
bool Ui::Create(HINSTANCE hInstance, HWND parent) {
    m_hInstance = hInstance;
    g_ui = this;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Ui::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"YXG_Launcher_MainWnd";
    RegisterClassExW(&wc);

    // 子窗口：填满父窗口（内容区）客户区
    RECT rc{ 0, 0, 1100, 720 };
    if (parent) GetClientRect(parent, &rc);
    m_width = rc.right - rc.left;
    m_height = rc.bottom - rc.top;
    if (m_width < 200) m_width = 1100;
    if (m_height < 200) m_height = 720;

    m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"我的世界启动器",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, m_width, m_height,
        parent, nullptr, hInstance, this);
    if (!m_hwnd) return false;

    SetTimer(m_hwnd, 1, 100, nullptr);
    Rebuild();

    // 注册 launcher 事件回调
    launcher::App::Instance().setEventCallback(AsyncEvent);
    return true;
}

void Ui::SetPage(Page p) {
    m_page = p;
    Rebuild();
    // 首次进入下载页时自动拉取版本清单（无需手动点"刷新"）
    if (p == Download && !launcher::App::Instance().manifestLoaded() && !m_manifestRequested) {
        m_manifestRequested = true;
        std::thread([this]() {
            std::string err;
            bool ok = launcher::App::Instance().refreshRemote(err);
            std::lock_guard<std::mutex> lk(m_evMu);
            m_evQueue.push_back({ ok ? (int)launcher::Ev::InstallDone : (int)launcher::Ev::InstallError,
                ok ? std::string("__manifest__") : err });
            PostMessageW(m_hwnd, WM_APP_EVENT, 0, 0);
        }).detach();
    }
}

void Ui::Rebuild() {
    m_widgets.clear();
    switch (m_page) {
        case Home: BuildHome(); break;
        case Versions: BuildVersions(); break;
        case Download: BuildDownload(); break;
        case Account: BuildAccount(); break;
        case Mods: BuildMods(); break;
        case Saves: BuildSaves(); break;
        case Settings: BuildSettings(); break;
        case VersionSettings: BuildVersionSettings(); break;
    }
}

Widget* Ui::Find(const std::wstring& id) {
    for (auto& w : m_widgets) if (w.id == id) return &w;
    return nullptr;
}

// ============================================================
// 各页面布局（内容区从 x=232 开始）
// ============================================================
void Ui::BuildHome() {
    launcher::App& app = launcher::App::Instance();

    // HMCL 式启动页：右下角大启动按钮 + 版本下拉（HMCL 布局）
    int right = m_width - 40;    // 内容区右边界（留 40 边距）
    int bottom = m_height - 40;  // 内容区下边界

    // 启动按钮（右下角，大，绿色，HMCL 风格）
    Widget launch; launch.type = Widget::Btn; launch.id = L"home.launch";
    launch.text = m_currentGameVersion.empty() ? L"启动游戏" : (L"启动游戏 · " + m_currentGameVersion);
    launch.accent = theme::GREEN;
    launch.rc = { right - 300, bottom - 76, right, bottom };
    m_widgets.push_back(launch);

    // 版本选择下拉框（启动按钮上方）
    Widget combo; combo.type = Widget::Combo; combo.id = L"home.version";
    combo.text = L"选择版本";
    combo.rc = { right - 300, bottom - 116, right, bottom - 84 };
    {
        std::error_code ec;
        fs::path vd = fs::u8path(app.gameDir()) / "versions";
        if (fs::exists(vd, ec)) {
            for (auto& e : fs::directory_iterator(vd, ec)) {
                if (e.is_directory(ec)) combo.items.push_back(e.path().filename().wstring());
            }
        }
        std::sort(combo.items.begin(), combo.items.end());
    }
    m_widgets.push_back(combo);

    // 状态提示（版本下拉上方，用于进度/错误反馈）
    Widget ptext; ptext.type = Widget::Label; ptext.id = L"home.ptext"; ptext.text = m_progressText;
    ptext.rc = { right - 600, bottom - 140, right, bottom - 122 };
    m_widgets.push_back(ptext);
}

void Ui::BuildVersions() {
    int x = 232, y = 40;
    launcher::App& app = launcher::App::Instance();

    Widget t; t.type = Widget::Label; t.id = L"ver.title"; t.text = L"版本管理";
    t.rc = { x, y, x + 300, y + 36 }; m_widgets.push_back(t);

    // 已安装版本列表（本地）
    Widget l1; l1.type = Widget::Label; l1.id = L"ver.l1"; l1.text = L"已安装版本";
    l1.rc = { x, y + 48, x + 300, y + 66 }; m_widgets.push_back(l1);
    Widget local; local.type = Widget::List; local.id = L"ver.local";
    local.rc = { x, y + 70, x + 640, y + 470 };
    std::error_code ec;
    fs::path vd = fs::u8path(app.gameDir()) / "versions";
    if (fs::exists(vd, ec)) {
        for (auto& e : fs::directory_iterator(vd, ec)) {
            if (e.is_directory(ec)) {
                std::wstring id = e.path().filename().wstring();
                bool hasJar = fs::exists(e.path() / (id + L".jar"), ec);
                local.listItems.push_back(id);
                local.subItems.push_back(hasJar ? L"已就绪" : L"缺少客户端");
            }
        }
    }
    m_widgets.push_back(local);

    // 操作按钮
    Widget launch; launch.type = Widget::Btn; launch.id = L"ver.launch"; launch.text = L"▶ 启动";
    launch.accent = theme::GREEN;
    launch.rc = { x, y + 484, x + 160, y + 520 }; m_widgets.push_back(launch);
    Widget vset; vset.type = Widget::Btn; vset.id = L"ver.verset"; vset.text = L"⚙ 版本设置";
    vset.rc = { x + 176, y + 484, x + 340, y + 520 }; m_widgets.push_back(vset);
    Widget del; del.type = Widget::Btn; del.id = L"ver.del"; del.text = L"🗑 删除";
    del.accent = theme::RED;
    del.rc = { x + 356, y + 484, x + 500, y + 520 }; m_widgets.push_back(del);

    Widget ptext; ptext.type = Widget::Label; ptext.id = L"ver.ptext"; ptext.text = m_progressText;
    ptext.rc = { x, y + 534, x + 840, y + 556 }; m_widgets.push_back(ptext);
}

void Ui::BuildDownload() {
    int x = 232, y = 40;
    launcher::App& app = launcher::App::Instance();

    Widget t; t.type = Widget::Label; t.id = L"dl.title"; t.text = L"下载";
    t.rc = { x, y, x + 300, y + 36 }; m_widgets.push_back(t);

    // 分类 Tab（HMCL 式：游戏/模组/资源包/光影/世界）
    static const wchar_t* kCats[] = { L"游戏", L"模组", L"资源包", L"光影", L"世界" };
    for (int i = 0; i < 5; ++i) {
        Widget c; c.type = Widget::Btn; c.id = std::wstring(L"dl.cat") + std::to_wstring(i);
        c.text = kCats[i];
        if (m_downloadCategory == i) c.accent = theme::ACCENT;
        c.rc = { x + i * 76, y + 44, x + i * 76 + 70, y + 72 };
        m_widgets.push_back(c);
    }

    if (m_downloadCategory == 0) {
        // ---- 游戏分类 ----
        Widget search; search.type = Widget::Edit; search.id = L"ver.search";
        search.editText = m_versionFilter;
        search.rc = { x, y + 84, x + 320, y + 112 }; m_widgets.push_back(search);
        Widget refresh; refresh.type = Widget::Btn; refresh.id = L"ver.refresh"; refresh.text = L"⟳ 刷新";
        refresh.rc = { x + 336, y + 84, x + 460, y + 112 }; m_widgets.push_back(refresh);

        Widget l2; l2.type = Widget::Label; l2.id = L"ver.l2"; l2.text = L"可下载版本";
        l2.rc = { x, y + 120, x + 300, y + 138 }; m_widgets.push_back(l2);
        Widget remote; remote.type = Widget::List; remote.id = L"ver.list";
        remote.rc = { x, y + 140, x + 420, y + 490 };
        {
            auto mf = app.manifest();
            std::wstring filter = m_versionFilter;
            int n = 0;
            for (const auto& v : mf.versions) {
                std::wstring id = theme::Utf8ToW(v.id);
                if (!filter.empty() && id.find(filter) == std::wstring::npos) continue;
                if (n >= 300) break;
                remote.listItems.push_back(id);
                remote.subItems.push_back(theme::Utf8ToW(v.type));
                if (id == m_selectedVersion) remote.selIdx = (int)remote.listItems.size() - 1;
                ++n;
            }
        }
        m_widgets.push_back(remote);

        Widget lsel; lsel.type = Widget::Combo; lsel.id = L"ver.loadertype";
        lsel.items = { L"仅原版", L"Fabric", L"Quilt", L"Forge", L"NeoForge" };
        lsel.sel = m_selectedLoaderType;
        lsel.rc = { x, y + 500, x + 160, y + 528 }; m_widgets.push_back(lsel);
        Widget lver; lver.type = Widget::Combo; lver.id = L"ver.loaderver";
        lver.items.push_back(L"自动(最新)");
        for (auto& v : m_loaderVersions) lver.items.push_back(v);
        lver.sel = 0;
        lver.rc = { x + 176, y + 500, x + 400, y + 528 }; m_widgets.push_back(lver);

        Widget dl; dl.type = Widget::Btn; dl.id = L"ver.download"; dl.text = L"⬇ 下载";
        dl.accent = theme::ACCENT;
        dl.rc = { x, y + 540, x + 240, y + 580 }; m_widgets.push_back(dl);

        Widget prog; prog.type = Widget::Progress; prog.id = L"ver.progress";
        prog.progress = m_progress;
        prog.rc = { x, y + 594, x + 840, y + 608 }; m_widgets.push_back(prog);
        Widget ptext; ptext.type = Widget::Label; ptext.id = L"ver.ptext"; ptext.text = m_progressText;
        ptext.rc = { x, y + 614, x + 840, y + 636 }; m_widgets.push_back(ptext);
    } else {
        // ---- 内容分类（模组/资源包/光影/世界）：搜索 + 结果 + 下载 ----
        Widget search; search.type = Widget::Edit; search.id = L"dl.csearch";
        search.rc = { x, y + 84, x + 420, y + 112 }; m_widgets.push_back(search);
        Widget sbtn; sbtn.type = Widget::Btn; sbtn.id = L"dl.csearchbtn"; sbtn.text = L"🔍 搜索";
        sbtn.accent = theme::ACCENT;
        sbtn.rc = { x + 436, y + 84, x + 560, y + 112 }; m_widgets.push_back(sbtn);

        Widget results; results.type = Widget::List; results.id = L"dl.cresults";
        results.rc = { x, y + 120, x + 640, y + 480 };
        for (auto& r : m_dlResults) {
            results.listItems.push_back(theme::Utf8ToW(r.title));
            results.subItems.push_back(theme::Utf8ToW(r.description));
        }
        m_widgets.push_back(results);

        Widget dlbtn; dlbtn.type = Widget::Btn; dlbtn.id = L"dl.cdownload"; dlbtn.text = L"⬇ 下载选中";
        dlbtn.accent = theme::GREEN;
        dlbtn.rc = { x, y + 490, x + 240, y + 526 }; m_widgets.push_back(dlbtn);

        Widget ptext; ptext.type = Widget::Label; ptext.id = L"ver.ptext"; ptext.text = m_progressText;
        ptext.rc = { x, y + 536, x + 840, y + 558 }; m_widgets.push_back(ptext);
    }
}

void Ui::BuildAccount() {
    int x = 232, y = 40;
    Widget t; t.type = Widget::Label; t.id = L"acc.title"; t.text = L"账号";
    t.rc = { x, y, x + 300, y + 40 }; m_widgets.push_back(t);

    Widget l1; l1.type = Widget::Label; l1.id = L"acc.l1"; l1.text = L"离线登录（无需正版）";
    l1.rc = { x, y + 56, x + 400, y + 76 }; m_widgets.push_back(l1);
    Widget name; name.type = Widget::Edit; name.id = L"acc.name"; name.text = L"输入玩家名";
    name.editText = L"Steve";
    name.rc = { x, y + 82, x + 360, y + 116 }; m_widgets.push_back(name);
    Widget off; off.type = Widget::Btn; off.id = L"acc.offline"; off.text = L"离线登录";
    off.rc = { x + 376, y + 82, x + 520, y + 116 }; m_widgets.push_back(off);

    Widget l2; l2.type = Widget::Label; l2.id = L"acc.l2"; l2.text = L"微软正版登录";
    l2.rc = { x, y + 150, x + 400, y + 170 }; m_widgets.push_back(l2);
    Widget ms; ms.type = Widget::Btn; ms.id = L"acc.msstart"; ms.text = L"获取设备码";
    ms.accent = theme::ACCENT;
    ms.rc = { x, y + 176, x + 300, y + 208 }; m_widgets.push_back(ms);
    Widget mspoll; mspoll.type = Widget::Btn; mspoll.id = L"acc.mspoll"; mspoll.text = L"轮询登录结果";
    mspoll.rc = { x + 316, y + 176, x + 520, y + 208 }; m_widgets.push_back(mspoll);

    Widget code; code.type = Widget::Label; code.id = L"acc.code";
    code.text = m_msUserCode.empty() ? L"" : (L"请在浏览器打开 " + m_msVerifyUri + L" 并输入代码：" + m_msUserCode);
    code.rc = { x, y + 224, x + 800, y + 260 }; m_widgets.push_back(code);

    Widget acc; acc.type = Widget::Label; acc.id = L"acc.cur";
    auto a = launcher::App::Instance().account();
    acc.text = a.name.empty() ? L"当前未登录" : (L"当前账号：" + theme::Utf8ToW(a.name) + L"  (" + theme::Utf8ToW(launcher::App::Instance().accountType()) + L")");
    acc.rc = { x, y + 280, x + 800, y + 304 }; m_widgets.push_back(acc);
}

void Ui::BuildMods() {
    int x = 232, y = 40;
    Widget t; t.type = Widget::Label; t.id = L"mod.title"; t.text = L"Mod 管理（前置补充）";
    t.rc = { x, y, x + 400, y + 40 }; m_widgets.push_back(t);

    Widget l1; l1.type = Widget::Label; l1.id = L"mod.l1"; l1.text = L"已安装 Mod";
    l1.rc = { x, y + 56, x + 300, y + 74 }; m_widgets.push_back(l1);
    Widget list; list.type = Widget::List; list.id = L"mod.list";
    list.rc = { x, y + 80, x + 520, y + 340 };
    // 填充 mod 列表（缓存，避免每次重建都解析 jar）
    if (!m_modCacheValid) {
        m_modCache = modmgr::ScanMods((fs::u8path(launcher::App::Instance().gameDir()) / L"mods").wstring());
        m_modCacheValid = true;
    }
    for (auto& m : m_modCache) {
        list.listItems.push_back(m.file);
        list.subItems.push_back(theme::Utf8ToW(m.loader.empty() ? m.name : (m.name + " [" + m.loader + "]")));
    }
    m_widgets.push_back(list);

    Widget l2; l2.type = Widget::Label; l2.id = L"mod.l2"; l2.text = L"缺失的前置";
    l2.rc = { x + 540, y + 56, x + 800, y + 74 }; m_widgets.push_back(l2);
    Widget missing; missing.type = Widget::List; missing.id = L"mod.missing";
    missing.rc = { x + 540, y + 80, x + 840, y + 260 };
    for (auto& m : m_missingMods) { missing.listItems.push_back(theme::Utf8ToW(m)); }
    m_widgets.push_back(missing);

    Widget detect; detect.type = Widget::Btn; detect.id = L"mod.detect"; detect.text = L"🔍 检测前置";
    detect.rc = { x + 540, y + 270, x + 700, y + 302 }; m_widgets.push_back(detect);
    Widget fix; fix.type = Widget::Btn; fix.id = L"mod.fix"; fix.text = L"⬇ 自动补充前置";
    fix.accent = theme::ACCENT;
    fix.rc = { x + 716, y + 270, x + 840, y + 302 }; m_widgets.push_back(fix);

    Widget status; status.type = Widget::Label; status.id = L"mod.status"; status.text = m_modStatus;
    status.rc = { x, y + 360, x + 840, y + 400 }; m_widgets.push_back(status);

    // ---- 下载 Mod（Modrinth 搜索，HMCL 式）----
    Widget dl; dl.type = Widget::Label; dl.id = L"mod.dl"; dl.text = L"下载 Mod（Modrinth 搜索）";
    dl.rc = { x, y + 408, x + 500, y + 428 }; m_widgets.push_back(dl);

    Widget search; search.type = Widget::Edit; search.id = L"mod.search";
    search.rc = { x, y + 432, x + 420, y + 464 }; m_widgets.push_back(search);
    Widget sbtn; sbtn.type = Widget::Btn; sbtn.id = L"mod.searchbtn"; sbtn.text = L"🔍 搜索";
    sbtn.accent = theme::ACCENT;
    sbtn.rc = { x + 436, y + 432, x + 560, y + 464 }; m_widgets.push_back(sbtn);

    Widget results; results.type = Widget::List; results.id = L"mod.results";
    results.rc = { x, y + 470, x + 640, y + 650 };
    for (auto& r : m_searchResults) {
        results.listItems.push_back(theme::Utf8ToW(r.title));
        results.subItems.push_back(theme::Utf8ToW(r.description));
    }
    m_widgets.push_back(results);

    Widget dlbtn; dlbtn.type = Widget::Btn; dlbtn.id = L"mod.dlbtn"; dlbtn.text = L"⬇ 下载选中";
    dlbtn.accent = theme::GREEN;
    dlbtn.rc = { x + 656, y + 470, x + 840, y + 502 }; m_widgets.push_back(dlbtn);
}

void Ui::BuildSaves() {
    int x = 232, y = 40;
    launcher::App& app = launcher::App::Instance();

    Widget t; t.type = Widget::Label; t.id = L"save.title"; t.text = L"存档管理";
    t.rc = { x, y, x + 300, y + 40 }; m_widgets.push_back(t);

    Widget list; list.type = Widget::List; list.id = L"save.list";
    list.rc = { x, y + 56, x + 540, y + 380 };
    std::error_code ec;
    fs::path savesDir = fs::u8path(app.gameDir()) / "saves";
    if (fs::exists(savesDir, ec)) {
        for (auto& e : fs::directory_iterator(savesDir, ec)) {
            if (e.is_directory(ec)) {
                list.listItems.push_back(e.path().filename().wstring());
                list.subItems.push_back(L"存档");
            }
        }
    }
    m_widgets.push_back(list);

    Widget back; back.type = Widget::Btn; back.id = L"save.backup"; back.text = L"💾 备份选中";
    back.rc = { x + 560, y + 56, x + 760, y + 90 }; m_widgets.push_back(back);
    Widget del; del.type = Widget::Btn; del.id = L"save.del"; del.text = L"🗑 删除选中";
    del.accent = theme::RED;
    del.rc = { x + 560, y + 98, x + 760, y + 132 }; m_widgets.push_back(del);
    Widget open; open.type = Widget::Btn; open.id = L"save.open"; open.text = L"📂 打开存档目录";
    open.rc = { x + 560, y + 140, x + 760, y + 174 }; m_widgets.push_back(open);

    Widget status; status.type = Widget::Label; status.id = L"save.status"; status.text = m_progressText;
    status.rc = { x, y + 400, x + 840, y + 424 }; m_widgets.push_back(status);
}

void Ui::BuildSettings() {
    int x = 232, y = 40;
    launcher::App& app = launcher::App::Instance();
    bg::Manager& bm = bg::Manager::Instance();

    Widget t; t.type = Widget::Label; t.id = L"set.title"; t.text = L"设置";
    t.rc = { x, y, x + 300, y + 36 }; m_widgets.push_back(t);

    auto row = [&](int& ry, const wchar_t* label, const wchar_t* wid, int wlen, bool isCombo, bool isSlider, bool isCheck) {
        Widget l; l.type = Widget::Label; l.id = std::wstring(wid) + L".lbl"; l.text = label;
        l.rc = { x, ry, x + 300, ry + 16 }; m_widgets.push_back(l);
        Widget c;
        c.type = isCombo ? Widget::Combo : (isSlider ? Widget::Slider : (isCheck ? Widget::Check : Widget::Edit));
        c.id = wid;
        c.rc = { x, ry + 18, x + wlen, ry + 42 };
        if (isSlider) { c.vmin = 0; c.vmax = 100; }
        m_widgets.push_back(c);
        ry += 44;
    };

    int ry = y + 46;
    row(ry, L"游戏目录（子目录 versions/libraries/assets/mods/saves）", L"set.gamedir", 640, false, false, false);
    row(ry, L"下载源", L"set.source", 300, true, false, false);
    row(ry, L"Java 路径（留空自动检测）", L"set.java", 640, false, false, false);
    row(ry, L"内存（MB）", L"set.mem", 400, false, true, false);
    row(ry, L"版本隔离（每版本独立 mods/存档）", L"set.isolation", 300, false, false, true);
    row(ry, L"背景模式", L"set.bgmode", 300, true, false, false);
    row(ry, L"背景图片路径", L"set.bgimage", 640, false, false, false);
    row(ry, L"背景轮播目录", L"set.bgdir", 640, false, false, false);
    row(ry, L"磨砂卡片效果", L"set.blur", 300, false, false, true);
    row(ry, L"磨砂不透明度", L"set.blurstr", 400, false, true, false);

    // 填充值
    if (Widget* w = Find(L"set.gamedir")) w->editText = theme::Utf8ToW(app.gameDir());
    if (Widget* w = Find(L"set.java")) w->editText = theme::Utf8ToW(app.javaPath());
    if (Widget* w = Find(L"set.source")) { w->items = { L"BMCLAPI（推荐）", L"MCBBS 镜像", L"Mojang 官方" }; w->sel = (int)app.source(); }
    if (Widget* w = Find(L"set.mem")) { w->vmin = 256; w->vmax = 8192; w->value = app.maxMemory(); }
    if (Widget* w = Find(L"set.bgmode")) {
        w->items = { L"纯色", L"内置壁纸 2021-08-26", L"内置壁纸 2016-02-25", L"内置壁纸 2015-06-22", L"图片", L"轮播" };
        int m = bm.Cfg().mode;
        if (m == 0) w->sel = 0;
        else if (m == 1) w->sel = 1 + bm.Cfg().builtinIndex;
        else if (m == 2) w->sel = 4;
        else w->sel = 5;
    }
    if (Widget* w = Find(L"set.bgimage")) w->editText = bm.Cfg().image;
    if (Widget* w = Find(L"set.bgdir")) w->editText = bm.Cfg().cycleDir;
    if (Widget* w = Find(L"set.blur")) w->checked = bm.Cfg().blur;
    if (Widget* w = Find(L"set.blurstr")) { w->vmin = 80; w->vmax = 255; w->value = bm.Cfg().blurStrength; }
    if (Widget* w = Find(L"set.isolation")) w->checked = app.versionIsolation();

    // ---- 服务器直连（启动时可直连）----
    ry += 2;
    Widget sl; sl.type = Widget::Label; sl.id = L"set.srvlbl"; sl.text = L"服务器直连";
    sl.rc = { x, ry, x + 300, ry + 16 }; m_widgets.push_back(sl);
    ry += 18;
    Widget slist; slist.type = Widget::List; slist.id = L"set.srvlist";
    slist.rc = { x, ry, x + 440, ry + 64 };
    for (auto& s : app.servers()) {
        slist.listItems.push_back(theme::Utf8ToW(s.first));
        slist.subItems.push_back(std::to_wstring(s.second));
    }
    m_widgets.push_back(slist);
    ry += 70;
    Widget saddr; saddr.type = Widget::Edit; saddr.id = L"set.srvaddr"; saddr.editText = L"";
    saddr.rc = { x, ry, x + 300, ry + 28 }; m_widgets.push_back(saddr);
    Widget sadd; sadd.type = Widget::Btn; sadd.id = L"set.srvadd"; sadd.text = L"＋添加";
    sadd.rc = { x + 312, ry, x + 440, ry + 28 }; m_widgets.push_back(sadd);
    ry += 34;
    Widget sdel; sdel.type = Widget::Btn; sdel.id = L"set.srvdel"; sdel.text = L"删除选中";
    sdel.rc = { x, ry, x + 140, ry + 28 }; m_widgets.push_back(sdel);
    Widget sdir; sdir.type = Widget::Btn; sdir.id = L"set.srvdir"; sdir.text = L"设为直连";
    sdir.accent = theme::ACCENT;
    sdir.rc = { x + 152, ry, x + 292, ry + 28 }; m_widgets.push_back(sdir);
    Widget sstat; sstat.type = Widget::Label; sstat.id = L"set.srvstat";
    sstat.text = app.directServer().empty()
        ? L"当前：不直连（单人游戏）"
        : (L"当前直连：" + theme::Utf8ToW(app.directServer()) + L":" + std::to_wstring(app.directPort()));
    sstat.rc = { x + 304, ry, x + 660, ry + 28 }; m_widgets.push_back(sstat);

    ry += 34;
    Widget save; save.type = Widget::Btn; save.id = L"set.save"; save.text = L"保存设置";
    save.accent = theme::GREEN;
    save.rc = { x, ry, x + 160, ry + 32 }; m_widgets.push_back(save);

    Widget browse; browse.type = Widget::Btn; browse.id = L"set.browse"; browse.text = L"选择目录/图片";
    browse.rc = { x + 172, ry, x + 320, ry + 32 }; m_widgets.push_back(browse);

    Widget sstat2; sstat2.type = Widget::Label; sstat2.id = L"set.status"; sstat2.text = m_progressText;
    sstat2.rc = { x + 332, ry, x + 840, ry + 32 }; m_widgets.push_back(sstat2);
}

void Ui::BuildVersionSettings() {
    int x = 232, y = 40;
    launcher::App& app = launcher::App::Instance();
    launcher::VersionSettings vs = app.versionSettings(theme::WToUtf8(m_editVersionId));

    Widget t; t.type = Widget::Label; t.id = L"vs.title";
    t.text = L"版本设置 - " + m_editVersionId;
    t.rc = { x, y, x + 640, y + 36 }; m_widgets.push_back(t);

    auto row = [&](int& ry, const wchar_t* label, const wchar_t* wid, int wlen, bool isCheck) {
        Widget l; l.type = Widget::Label; l.id = std::wstring(wid) + L".lbl"; l.text = label;
        l.rc = { x, ry, x + 320, ry + 16 }; m_widgets.push_back(l);
        Widget c;
        c.type = isCheck ? Widget::Check : Widget::Edit;
        c.id = wid;
        c.rc = { x, ry + 18, x + wlen, ry + 42 };
        m_widgets.push_back(c);
        ry += 44;
    };

    int ry = y + 46;
    row(ry, L"Java 路径（留空用全局）", L"vs.java", 640, false);
    row(ry, L"内存覆盖（MB，0=用全局）", L"vs.mem", 300, false);
    {
        Widget l; l.type = Widget::Label; l.id = L"vs.size.lbl"; l.text = L"窗口大小（宽 x 高）";
        l.rc = { x, ry, x + 320, ry + 16 }; m_widgets.push_back(l);
        Widget w1; w1.type = Widget::Edit; w1.id = L"vs.width";
        w1.rc = { x, ry + 18, x + 120, ry + 42 }; m_widgets.push_back(w1);
        Widget w2; w2.type = Widget::Edit; w2.id = L"vs.height";
        w2.rc = { x + 140, ry + 18, x + 260, ry + 42 }; m_widgets.push_back(w2);
        Widget fs; fs.type = Widget::Check; fs.id = L"vs.fullscreen"; fs.text = L"全屏";
        fs.rc = { x + 300, ry + 18, x + 420, ry + 42 }; m_widgets.push_back(fs);
        ry += 44;
    }
    row(ry, L"JVM 参数（空格分隔）", L"vs.jvmargs", 640, false);
    row(ry, L"游戏参数（空格分隔）", L"vs.gameargs", 640, false);
    row(ry, L"直连服务器（空=不直连，格式 host 或 host:port）", L"vs.server", 480, false);

    // 填充值
    if (Widget* w = Find(L"vs.java")) w->editText = theme::Utf8ToW(vs.javaPath);
    if (Widget* w = Find(L"vs.mem")) w->editText = vs.maxMemoryMB > 0 ? std::to_wstring(vs.maxMemoryMB) : L"0";
    if (Widget* w = Find(L"vs.width")) w->editText = std::to_wstring(vs.width);
    if (Widget* w = Find(L"vs.height")) w->editText = std::to_wstring(vs.height);
    if (Widget* w = Find(L"vs.fullscreen")) w->checked = vs.fullscreen;
    if (Widget* w = Find(L"vs.jvmargs")) {
        std::wstring s;
        for (auto& a : vs.jvmArgs) { if (!s.empty()) s += L" "; s += theme::Utf8ToW(a); }
        w->editText = s;
    }
    if (Widget* w = Find(L"vs.gameargs")) {
        std::wstring s;
        for (auto& a : vs.gameArgs) { if (!s.empty()) s += L" "; s += theme::Utf8ToW(a); }
        w->editText = s;
    }
    if (Widget* w = Find(L"vs.server")) {
        w->editText = vs.server.empty() ? L""
            : (theme::Utf8ToW(vs.server) + (vs.serverPort != 25565 ? (L":" + std::to_wstring(vs.serverPort)) : L""));
    }

    ry += 8;
    Widget save; save.type = Widget::Btn; save.id = L"vs.save"; save.text = L"保存";
    save.accent = theme::GREEN;
    save.rc = { x, ry, x + 160, ry + 34 }; m_widgets.push_back(save);
    Widget back; back.type = Widget::Btn; back.id = L"vs.back"; back.text = L"← 返回版本页";
    back.rc = { x + 176, ry, x + 340, ry + 34 }; m_widgets.push_back(back);
    Widget status; status.type = Widget::Label; status.id = L"vs.status"; status.text = m_progressText;
    status.rc = { x + 356, ry, x + 840, ry + 34 }; m_widgets.push_back(status);
}

// ============================================================
// 绘制
// ============================================================
void Ui::Paint(HDC hdc, const RECT& client) {
    int w = client.right - client.left, h = client.bottom - client.top;
    if (w <= 0 || h <= 0) return;

    Gdiplus::Bitmap bmp(w, h, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

    bg::Manager::Instance().Paint(g, Gdiplus::RectF(0, 0, (float)w, (float)h));
    DrawSidebar(g);

    switch (m_page) {
        case Home: PaintHome(g); break;
        case Versions: PaintVersions(g); break;
        case Download: PaintDownload(g); break;
        case Account: PaintAccount(g); break;
        case Mods: PaintMods(g); break;
        case Saves: PaintSaves(g); break;
        case Settings: PaintSettings(g); break;
        case VersionSettings: PaintVersionSettings(g); break;
    }

    // 展开的下拉列表最后统一绘制，置于最上层，避免被后续控件遮挡
    for (auto& w : m_widgets) {
        if (w.type == Widget::Combo && w.dropped) DrawDropdown(g, w);
    }

    // 下载进度弹窗（最上层）
    DrawDownloadDialog(g);

    Gdiplus::Graphics out(hdc);
    out.DrawImage(&bmp, 0, 0);
}

// 下载进度弹窗（HMCL 式：遮罩 + 居中卡片 + 进度条 + 关闭/取消）
void Ui::DrawDownloadDialog(Gdiplus::Graphics& g) {
    if (!m_showDlDialog) return;
    const theme::Fonts& fonts = theme::FontsCache();

    // 半透明遮罩
    Gdiplus::SolidBrush mask(Gdiplus::Color(120, 0, 0, 0));
    g.FillRectangle(&mask, Gdiplus::RectF(0, 0, (float)m_width, (float)m_height));

    int cw = 480, chh = 240;
    int cx = (m_width - cw) / 2;
    int cy = (m_height - chh) / 2;

    // 卡片
    Gdiplus::RectF card((float)cx, (float)cy, (float)cw, (float)chh);
    theme::FillRoundRect(g, card, 14, Gdiplus::Color(255, 255, 255, 255));
    theme::StrokeRoundRect(g, card, 14, theme::C(theme::LINE), 1.0f);

    // 标题
    theme::DrawTextLeft(g, L"下载 " + m_dlDialogTitle, fonts.head, theme::C(theme::TEXT),
        Gdiplus::RectF((float)cx + 24, (float)cy + 16, (float)(cx + cw - 24), (float)(cy + 46)), 0);

    // 进度条
    Gdiplus::RectF bar((float)cx + 24, (float)cy + 72, (float)(cw - 48), 12);
    theme::FillRoundRect(g, bar, 6, theme::C(theme::CARD2, 255));
    float fw = bar.Width * std::clamp(m_progress, 0.0f, 1.0f);
    if (fw > 1.0f) {
        COLORREF c = m_installing ? theme::ACCENT
            : (m_progressText.find(L"失败") != std::wstring::npos ? theme::RED : theme::GREEN);
        theme::FillRoundRect(g, Gdiplus::RectF(bar.X, bar.Y, fw, bar.Height), 6, theme::C(c));
    }

    // 状态文字
    theme::DrawTextLeft(g, m_progressText, fonts.body, theme::C(theme::MUTED),
        Gdiplus::RectF((float)cx + 24, (float)cy + 92, (float)(cx + cw - 24), (float)(cy + 160)), 0);

    // 关闭 / 取消按钮
    int btw = 110, bth = 38;
    int btx = cx + cw - btw - 20;
    int bty = cy + chh - bth - 16;
    Gdiplus::RectF btn((float)btx, (float)bty, (float)btw, (float)bth);
    theme::FillRoundRect(g, btn, 8, theme::C(m_installing ? theme::RED : theme::ACCENT));
    theme::DrawTextCenter(g, m_installing ? L"取消" : L"关闭", fonts.body,
        Gdiplus::Color(255, 255, 255, 255), btn);
}

// ============================================================
// 侧边栏导航（HMCL 式：账号 + 实例分类 + 设置分类）
// ============================================================
enum class NavKind { Account, Header, CurrentGame, Item };
struct NavEntry {
    NavKind kind;
    const wchar_t* emoji;
    const wchar_t* label;
    Ui::Page page;
    int y;
    int h;
};
static const NavEntry kNav[] = {
    { NavKind::Account,    L"",     L"",            Ui::Page::Account,   16, 64 },
    { NavKind::Header,     L"",     L"实例",         Ui::Page::Home,      96, 20 },
    { NavKind::CurrentGame,L"",     L"",            Ui::Page::Versions, 120, 48 },
    { NavKind::Item,       L"📦",   L"版本管理",     Ui::Page::Versions, 172, 40 },
    { NavKind::Item,       L"⬇",   L"下载",         Ui::Page::Download, 216, 40 },
    { NavKind::Item,       L"🧩",   L"模组",         Ui::Page::Mods,     260, 40 },
    { NavKind::Item,       L"💾",   L"存档",         Ui::Page::Saves,    304, 40 },
    { NavKind::Header,     L"",     L"启动器设置",   Ui::Page::Home,     360, 20 },
    { NavKind::Item,       L"⚙",   L"全局设置",     Ui::Page::Settings, 384, 40 },
};
static const int kNavCount = sizeof(kNav) / sizeof(kNav[0]);

void Ui::DrawSidebar(Gdiplus::Graphics& g) {
    // 侧边栏背景（HMCL 风格：surface 半透明，见 gray-background = -monet-surface-transparent-50）
    Gdiplus::SolidBrush sb(theme::C(theme::BG, 128));
    g.FillRectangle(&sb, Gdiplus::RectF(0, 0, 220, (float)m_height));
    Gdiplus::Pen divPen(theme::C(theme::LINE), 1.0f);
    g.DrawLine(&divPen, 219.0f, 0.0f, 219.0f, (float)m_height);

    const theme::Fonts& fonts = theme::FontsCache();
    Gdiplus::Font emojiFont(L"Segoe UI Emoji", 15, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    launcher::App& app = launcher::App::Instance();
    auto acc = app.account();

    for (int i = 0; i < kNavCount; ++i) {
        const NavEntry& e = kNav[i];
        if (e.kind == NavKind::Header) {
            theme::DrawTextLeft(g, e.label, fonts.smallFont, theme::C(theme::MUTED),
                Gdiplus::RectF(20, (float)e.y, 180, (float)e.h), 0);
            continue;
        }
        if (e.kind == NavKind::Account) {
            Gdiplus::RectF avatar(20, (float)e.y, 40, 40);
            theme::FillRoundRect(g, avatar, 20, theme::C(theme::ACCENT, 255));
            theme::DrawTextCenter(g, L"👤", &emojiFont, Gdiplus::Color(255, 255, 255, 255), avatar);
            std::wstring name = acc.name.empty() ? L"未登录账号" : theme::Utf8ToW(acc.name);
            theme::DrawTextLeft(g, name, fonts.head, theme::C(theme::TEXT),
                Gdiplus::RectF(72, (float)e.y, 140, (float)e.h), 0);
            continue;
        }
        if (e.kind == NavKind::CurrentGame) {
            Gdiplus::RectF rc(12, (float)e.y, 196, (float)e.h);
            bool active = (m_page == e.page);
            if (active) theme::FillRoundRect(g, rc, 10, theme::C(theme::ACCENT, 40));
            std::wstring ver = m_currentGameVersion.empty() ? L"未选择版本" : m_currentGameVersion;
            theme::DrawTextLeft(g, L"▶", &emojiFont,
                active ? theme::C(theme::ACCENT) : theme::C(theme::MUTED),
                Gdiplus::RectF(rc.X + 12, rc.Y, 24, rc.Height), 0);
            theme::DrawTextLeft(g, ver, fonts.head,
                active ? theme::C(theme::ACCENT) : theme::C(theme::TEXT),
                Gdiplus::RectF(rc.X + 40, rc.Y, rc.Width - 48, rc.Height), 0);
            continue;
        }
        // 普通项
        Gdiplus::RectF rc(16, (float)e.y, 188, (float)e.h);
        bool active = (m_page == e.page);
        Gdiplus::Color fg = active ? Gdiplus::Color(255, 255, 255, 255) : theme::C(theme::TEXT);
        if (active) theme::FillRoundRect(g, rc, 10, theme::C(theme::ACCENT, 255));
        theme::DrawTextLeft(g, e.emoji, &emojiFont, fg,
            Gdiplus::RectF(rc.X + 12, rc.Y, 26, rc.Height), 0);
        theme::DrawTextLeft(g, e.label, active ? fonts.head : fonts.body, fg,
            Gdiplus::RectF(rc.X + 44, rc.Y, rc.Width - 48, rc.Height), 0);
    }

    // 底部标题
    theme::DrawTextCenter(g, L"YXG Minecraft", fonts.smallFont, theme::C(theme::MUTED),
        Gdiplus::RectF(0, (float)m_height - 40, 220, 24));
}

void Ui::DrawWidget(Gdiplus::Graphics& g, const Widget& w) {
    const theme::Fonts& fonts = theme::FontsCache();
    Gdiplus::RectF rc((float)w.rc.left, (float)w.rc.top,
        (float)(w.rc.right - w.rc.left), (float)(w.rc.bottom - w.rc.top));

    switch (w.type) {
    case Widget::Label: {
        theme::DrawTextLeft(g, w.text, fonts.body, theme::C(theme::MUTED), rc, 0);
        break;
    }
    case Widget::Btn: {
        COLORREF base = w.accent ? w.accent : theme::CARD2;
        COLORREF col = base;
        if (w.pressed) col = theme::Darken(base, 30);
        else if (w.hover) col = theme::Lighten(base, 18);
        theme::FillRoundRect(g, rc, 10, theme::C(col, 255));
        Gdiplus::Color tc = (w.accent == theme::GREEN || w.accent == theme::ACCENT)
            ? Gdiplus::Color(255, 255, 255, 255) : theme::C(theme::TEXT);
        theme::DrawTextCenter(g, w.text, fonts.head, tc, rc);
        break;
    }
    case Widget::Edit: {
        theme::FillRoundRect(g, rc, 8, theme::C(theme::CARD2, 235));
        theme::StrokeRoundRect(g, rc, 8, theme::C(w.focused ? theme::ACCENT : theme::LINE), w.focused ? 1.5f : 1.0f);
        std::wstring t = w.editText;
        if (w.focused) t += L"|";
        theme::DrawTextLeft(g, t, fonts.body, theme::C(theme::TEXT), rc, 10);
        break;
    }
    case Widget::Combo: {
        theme::FillRoundRect(g, rc, 8, theme::C(theme::CARD2, 235));
        theme::StrokeRoundRect(g, rc, 8, theme::C(w.dropped ? theme::ACCENT : theme::LINE), 1.0f);
        std::wstring t = (w.sel >= 0 && w.sel < (int)w.items.size()) ? w.items[w.sel] : (w.text.empty() ? L"" : w.text);
        theme::DrawTextLeft(g, t, fonts.body, theme::C(theme::TEXT), rc, 10);
        theme::DrawTextRight(g, L"▼", fonts.body, theme::C(theme::MUTED), rc, 10);
        break;
    }
    case Widget::List: {
        theme::FillRoundRect(g, rc, 10, theme::C(theme::CARD, 220));
        theme::StrokeRoundRect(g, rc, 10, theme::C(theme::LINE), 1.0f);
        Gdiplus::GraphicsState st = g.Save();
        g.SetClip(Gdiplus::RectF(rc.X, rc.Y, rc.Width, rc.Height));
        int rowH = 32;
        int visible = (int)(rc.Height / rowH);
        int start = std::max(0, w.scroll);
        for (int i = start; i < (int)w.listItems.size() && i < start + visible; ++i) {
            float yy = rc.Y + (i - start) * rowH;
            Gdiplus::RectF ir(rc.X, yy, rc.Width, rowH);
            bool hot = (i == w.hot);
            bool sel = (i == w.selIdx);
            if (sel) theme::FillRoundRect(g, ir, 6, theme::C(theme::ACCENT, 40));
            else if (hot) theme::FillRoundRect(g, ir, 6, theme::C(theme::CARD2, 40));
            Gdiplus::Color tc = sel ? theme::C(theme::ACCENT) : theme::C(theme::TEXT);
            theme::DrawTextLeft(g, w.listItems[i], fonts.body, tc, Gdiplus::RectF(ir.X, ir.Y, ir.Width - 140, ir.Height), 12);
            if (i < (int)w.subItems.size()) {
                theme::DrawTextRight(g, w.subItems[i], fonts.smallFont, theme::C(theme::MUTED),
                    Gdiplus::RectF(ir.X, ir.Y, ir.Width, ir.Height), 12);
            }
        }
        g.Restore(st);
        break;
    }
    case Widget::Progress: {
        theme::FillRoundRect(g, rc, 6, theme::C(theme::LINE, 255));
        float pw = rc.Width * std::clamp(w.progress, 0.0f, 1.0f);
        if (pw > 2) theme::FillRoundRect(g, Gdiplus::RectF(rc.X, rc.Y, pw, rc.Height), 6, theme::C(theme::ACCENT, 255));
        break;
    }
    case Widget::Slider: {
        float ratio = (w.vmax > w.vmin) ? (float)(w.value - w.vmin) / (float)(w.vmax - w.vmin) : 0;
        float cy = rc.Y + rc.Height / 2;
        theme::FillRoundRect(g, Gdiplus::RectF(rc.X, cy - 3, rc.Width, 6), 3, theme::C(theme::LINE, 255));
        float filled = rc.Width * std::clamp(ratio, 0.0f, 1.0f);
        if (filled > 0) theme::FillRoundRect(g, Gdiplus::RectF(rc.X, cy - 3, filled, 6), 3, theme::C(theme::ACCENT, 255));
        float kx = rc.X + filled;
        theme::FillRoundRect(g, Gdiplus::RectF(kx - 7, cy - 9, 14, 18), 6, Gdiplus::Color(255, 255, 255, 255));
        break;
    }
    case Widget::Check: {
        float s = 18;
        Gdiplus::RectF box(rc.X, rc.Y + (rc.Height - s) / 2, s, s);
        theme::FillRoundRect(g, box, 4, theme::C(theme::CARD2, 255));
        theme::StrokeRoundRect(g, box, 4, theme::C(theme::LINE), 1.0f);
        if (w.checked) {
            theme::FillRoundRect(g, Gdiplus::RectF(box.X + 4, box.Y + 4, s - 8, s - 8), 3, theme::C(theme::ACCENT, 255));
        }
        theme::DrawTextLeft(g, w.text, fonts.body, theme::C(theme::TEXT),
            Gdiplus::RectF(rc.X + s + 8, rc.Y, rc.Width - s - 8, rc.Height), 0);
        break;
    }
    default: break;
    }
}

void Ui::DrawDropdown(Gdiplus::Graphics& g, const Widget& w) {
    const theme::Fonts& fonts = theme::FontsCache();
    Gdiplus::RectF rc((float)w.rc.left, (float)w.rc.top,
        (float)(w.rc.right - w.rc.left), (float)(w.rc.bottom - w.rc.top));
    float ih = 30.0f * (float)w.items.size();
    Gdiplus::RectF dr(rc.X, rc.Y + rc.Height + 2, rc.Width, ih);
    // 不透明白底，确保完全盖住下方控件
    theme::FillRoundRect(g, dr, 8, Gdiplus::Color(255, 255, 255, 255));
    theme::StrokeRoundRect(g, dr, 8, theme::C(theme::LINE), 1.0f);
    for (size_t i = 0; i < w.items.size(); ++i) {
        Gdiplus::RectF ir(dr.X, dr.Y + i * 30, dr.Width, 30);
        theme::DrawTextLeft(g, w.items[i], fonts.body,
            ((int)i == w.sel) ? theme::C(theme::ACCENT) : theme::C(theme::TEXT), ir, 10);
    }
}

void Ui::PaintHome(Gdiplus::Graphics& g) {
    for (auto& w : m_widgets) DrawWidget(g, w);
}
void Ui::PaintVersions(Gdiplus::Graphics& g) { for (auto& w : m_widgets) DrawWidget(g, w); }
void Ui::PaintDownload(Gdiplus::Graphics& g) { for (auto& w : m_widgets) DrawWidget(g, w); }
void Ui::PaintAccount(Gdiplus::Graphics& g) { for (auto& w : m_widgets) DrawWidget(g, w); }
void Ui::PaintMods(Gdiplus::Graphics& g) { for (auto& w : m_widgets) DrawWidget(g, w); }
void Ui::PaintSaves(Gdiplus::Graphics& g) { for (auto& w : m_widgets) DrawWidget(g, w); }
void Ui::PaintSettings(Gdiplus::Graphics& g) { for (auto& w : m_widgets) DrawWidget(g, w); }
void Ui::PaintVersionSettings(Gdiplus::Graphics& g) { for (auto& w : m_widgets) DrawWidget(g, w); }

// ============================================================
// 输入事件
// ============================================================
static bool inRect(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

void Ui::OnMouseMove(int x, int y) {
    bool dirty = false;
    for (auto& w : m_widgets) {
        bool h = inRect(w.rc, x, y);
        if (h != w.hover) { w.hover = h; dirty = true; }
    }
    if (dirty) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Ui::OnLButtonDown(int x, int y) {
    // 下载弹窗：点击「关闭/取消」关闭弹窗（下载中则中止），其余区域吞掉点击
    if (m_showDlDialog) {
        int cw = 480, chh = 240;
        int cx = (m_width - cw) / 2;
        int cy = (m_height - chh) / 2;
        int btw = 110, bth = 38;
        int btx = cx + cw - btw - 20;
        int bty = cy + chh - bth - 16;
        if (x >= btx && x < btx + btw && y >= bty && y < bty + bth) {
            m_showDlDialog = false;
            if (m_installing) launcher::App::Instance().abortInstall();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }
    // 侧边栏导航（HMCL 式，遍历 kNav）
    if (x < 220) {
        for (int i = 0; i < kNavCount; ++i) {
            const NavEntry& e = kNav[i];
            if (e.kind == NavKind::Header) continue;
            if (y >= e.y && y < e.y + e.h) {
                SetPage(e.page);
                return;
            }
        }
        return;
    }
    // 先处理 Combo 下拉
    for (auto& w : m_widgets) {
        if (w.type == Widget::Combo && w.dropped) {
            float ih = 30.0f * (float)w.items.size();
            float ry = w.rc.bottom + 2;
            if (x >= w.rc.left && x < w.rc.right && y >= ry && y < ry + ih) {
                w.sel = (int)((y - ry) / 30);
                w.dropped = false;
                if (w.id == L"set.source") launcher::App::Instance().setSource(w.sel == 0 ? "bmclapi" : (w.sel == 1 ? "mcbbs" : "mojang"));
                if (w.id == L"set.bgmode") {
                    int sel = w.sel;
                    if (sel == 0) bg::Manager::Instance().SetMode(0);
                    else if (sel >= 1 && sel <= 3) { bg::Manager::Instance().SetMode(1); bg::Manager::Instance().SetBuiltinIndex(sel - 1); }
                    else if (sel == 4) bg::Manager::Instance().SetMode(2);
                    else bg::Manager::Instance().SetMode(3);
                }
                if (w.id == L"ver.loadertype") {
                    m_selectedLoaderType = w.sel;
                    TriggerLoaderVersionFetch();
                }
                if (w.id == L"home.version") {
                    if (w.sel >= 0 && w.sel < (int)w.items.size()) m_currentGameVersion = w.items[w.sel];
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            w.dropped = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
    // 找命中的控件
    Widget* hit = nullptr;
    for (auto& w : m_widgets) {
        if (inRect(w.rc, x, y) && w.type != Widget::Label) { hit = &w; break; }
    }
    if (!hit) return;
    if (hit->type == Widget::Btn) { hit->pressed = true; InvalidateRect(m_hwnd, nullptr, FALSE); }
    if (hit->type == Widget::Combo) { hit->dropped = !hit->dropped; InvalidateRect(m_hwnd, nullptr, FALSE); }
    if (hit->type == Widget::Edit) {
        for (auto& w : m_widgets) if (w.type == Widget::Edit) w.focused = false;
        hit->focused = true;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (hit->type == Widget::Check) { hit->checked = !hit->checked; InvalidateRect(m_hwnd, nullptr, FALSE); }
    if (hit->type == Widget::List) {
        int rowH = 32;
        int idx = hit->scroll + (y - hit->rc.top) / rowH;
        if (idx >= 0 && idx < (int)hit->listItems.size()) {
            hit->selIdx = idx;
            if (hit->id == L"ver.list") {
                m_selectedVersion = hit->listItems[idx];
                TriggerLoaderVersionFetch();
            }
            if (hit->id == L"ver.local") {
                m_currentGameVersion = hit->listItems[idx];
            }
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (hit->type == Widget::Slider) {
        float ratio = (float)(x - hit->rc.left) / (float)(hit->rc.right - hit->rc.left);
        hit->value = hit->vmin + (int)(ratio * (hit->vmax - hit->vmin));
        hit->value = std::clamp(hit->value, hit->vmin, hit->vmax);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Ui::OnLButtonUp(int x, int y) {
    for (auto& w : m_widgets) {
        if (w.type == Widget::Btn && w.pressed) {
            w.pressed = false;
            if (inRect(w.rc, x, y)) HandleButton(w.id);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
}

void Ui::OnChar(wchar_t c) {
    for (auto& w : m_widgets) {
        if (w.type == Widget::Edit && w.focused) {
            if (c == '\b') { if (!w.editText.empty()) w.editText.pop_back(); }
            else if (c >= 0x20) w.editText.push_back(c);
            if (w.id == L"ver.search") {
                m_versionFilter = w.editText;
                Rebuild();
                if (Widget* s = Find(L"ver.search")) s->focused = true;
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
}

void Ui::OnKeyDown(WPARAM vk) {
    if (vk == VK_ESCAPE) {
        for (auto& w : m_widgets) if (w.type == Widget::Combo) w.dropped = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    // 复制 / 粘贴（Ctrl+C / Ctrl+V），作用于聚焦的路径输入框
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    Widget* edit = nullptr;
    for (auto& w : m_widgets) if (w.type == Widget::Edit && w.focused) { edit = &w; break; }
    if (!edit || !ctrl) return;
    if (vk == 'V') {
        if (OpenClipboard(m_hwnd)) {
            HANDLE h = GetClipboardData(CF_UNICODETEXT);
            if (h) {
                const wchar_t* p = (const wchar_t*)GlobalLock(h);
                if (p) { edit->editText += p; GlobalUnlock(h); }
            }
            CloseClipboard();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else if (vk == 'C') {
        if (!edit->editText.empty() && OpenClipboard(m_hwnd)) {
            EmptyClipboard();
            size_t n = (edit->editText.size() + 1) * sizeof(wchar_t);
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
            if (h) {
                memcpy(GlobalLock(h), edit->editText.c_str(), n);
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            }
            CloseClipboard();
        }
    }
}

void Ui::OnMouseWheel(int delta, int x, int y) {
    for (auto& w : m_widgets) {
        if (w.type == Widget::List && inRect(w.rc, x, y)) {
            w.scroll -= delta / 120;
            int maxScroll = (int)w.listItems.size() - (int)((w.rc.bottom - w.rc.top) / 32);
            w.scroll = std::clamp(w.scroll, 0, std::max(0, maxScroll));
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
}

void Ui::OnTimerTick() {
    // 消费异步事件
    std::vector<std::pair<int, std::string>> evs;
    {
        std::lock_guard<std::mutex> lk(m_evMu);
        evs.swap(m_evQueue);
    }
    bool needRedraw = false;
    for (auto& [t, d] : evs) {
        if (t == (int)launcher::Ev::InstallProgress) {
            try {
                auto j = json::parse(d);
                m_installing = true;
                m_progressText = theme::Utf8ToW(j.get("phase").asString() + "  " + j.get("current").asString());
                int64_t total = j.get("total").asInt(), done = j.get("done").asInt();
                m_progress = total > 0 ? (float)((double)done / (double)total) : 0.0f;
            } catch (...) {}
            needRedraw = true;
        } else if (t == (int)launcher::Ev::InstallDone) {
            if (d == "__manifest__") {
                m_progressText = L"版本清单已更新";
                Rebuild();
            } else if (d == "__loaderver__") {
                std::vector<std::wstring> vers;
                {
                    std::lock_guard<std::mutex> lk(m_evMu);
                    vers = m_loaderVersions;
                }
                if (Widget* w = Find(L"ver.loaderver")) {
                    w->items.clear();
                    w->items.push_back(L"自动(最新)");
                    for (auto& v : vers) w->items.push_back(v);
                    w->sel = 0;
                    w->dropped = false;
                }
                m_progressText = vers.empty()
                    ? L"该版本无可用加载器"
                    : (L"加载器版本已加载：" + std::to_wstring((int)vers.size()) + L" 个");
            } else if (d.rfind("__modsearch__:", 0) == 0) {
                try {
                    json::Value arr = json::parse(d.substr(14));
                    m_searchResults.clear();
                    for (size_t i = 0; i < arr.size(); ++i) {
                        modmgr::SearchResult r;
                        r.slug = arr.at(i).get("slug").asString();
                        r.title = arr.at(i).get("title").asString();
                        r.description = arr.at(i).get("description").asString();
                        m_searchResults.push_back(r);
                    }
                    m_modStatus = L"找到 " + std::to_wstring(m_searchResults.size()) + L" 个结果";
                } catch (...) { m_modStatus = L"解析搜索结果失败"; }
                Rebuild();
            } else if (d.rfind("__moddl__:", 0) == 0) {
                m_modStatus = L"已下载: " + theme::Utf8ToW(d.substr(10));
                m_modCacheValid = false;
                Rebuild();
            } else if (d == "__dlsearch__") {
                Rebuild();
            } else if (d.rfind("__dlmod__:", 0) == 0) {
                m_progressText = L"已下载: " + theme::Utf8ToW(d.substr(10));
                Rebuild();
            } else {
                m_installing = false; m_progress = 1.0f; m_progressText = L"安装完成";
            }
            needRedraw = true;
        } else if (t == (int)launcher::Ev::InstallError) {
            m_installing = false; m_progress = 0.0f;
            m_progressText = theme::Utf8ToW("安装失败: " + d);
            needRedraw = true;
        } else if (t == (int)launcher::Ev::GameStart) {
            m_progressText = L"游戏已启动 (PID " + theme::Utf8ToW(d) + L")";
            needRedraw = true;
        } else if (t == (int)launcher::Ev::GameExit) {
            m_progressText = L"游戏已退出 (code " + theme::Utf8ToW(d) + L")";
            needRedraw = true;
        } else if (t == (int)launcher::Ev::AccountChange) {
            Rebuild();
            needRedraw = true;
        }
    }
    // 更新进度控件
    if (Widget* w = Find(L"ver.progress")) w->progress = m_progress;
    if (Widget* w = Find(L"home.ptext")) w->text = m_progressText;
    if (Widget* w = Find(L"ver.ptext")) w->text = m_progressText;
    // 背景轮播
    if (bg::Manager::Instance().ConsumeDirty()) needRedraw = true;
    if (needRedraw) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Ui::OnAsyncEvent(int type, const std::string& data) {
    std::lock_guard<std::mutex> lk(m_evMu);
    m_evQueue.push_back({ type, data });
    if (m_hwnd) PostMessageW(m_hwnd, WM_APP_EVENT, 0, 0);
}

// ============================================================
// 业务逻辑
// ============================================================
void Ui::RefreshVersionsList() {
    // 由 BuildHome / BuildVersions 各自填充
}

void Ui::RefreshModsList() {
    m_modCacheValid = false;  // 失效缓存，强制重新扫描 mods 目录
    m_missingMods = modmgr::DetectMissing((fs::u8path(launcher::App::Instance().gameDir()) / L"mods").wstring());
}

// 选中 MC 版本 + 加载器类型后，后台拉取对应的加载器版本列表
void Ui::TriggerLoaderVersionFetch() {
    std::wstring mc;
    if (Widget* w = Find(L"ver.list")) {
        if (w->selIdx >= 0 && w->selIdx < (int)w->listItems.size()) mc = w->listItems[w->selIdx];
    }
    int ltype = 0;
    if (Widget* w = Find(L"ver.loadertype")) ltype = w->sel;
    if (mc.empty() || ltype == 0) {
        m_loaderVersions.clear();
        if (Widget* w = Find(L"ver.loaderver")) {
            w->items.clear();
            w->items.push_back(L"自动(最新)");
            w->sel = 0;
            w->dropped = false;
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    std::wstring mcv = mc;
    int lt = ltype;
    std::thread([this, mcv, lt]() {
        std::vector<std::wstring> vers;
        std::string err;
        std::string mcStr = theme::WToUtf8(mcv);
        if (lt == 1 || lt == 2) {
            auto v = loader::FetchVersions(mcStr, lt == 2, err);
            for (auto& x : v) vers.push_back(theme::Utf8ToW(x.version));
        } else if (lt == 3) {
            auto v = loader::FetchForgeVersions(mcStr, err);
            for (auto& x : v) vers.push_back(theme::Utf8ToW(x));
        } else if (lt == 4) {
            auto v = loader::FetchNeoForgeVersions(mcStr, err);
            for (auto& x : v) vers.push_back(theme::Utf8ToW(x));
        }
        {
            std::lock_guard<std::mutex> lk(m_evMu);
            m_loaderVersions = vers;
            m_evQueue.push_back({ (int)launcher::Ev::InstallDone, std::string("__loaderver__") });
        }
        PostMessageW(m_hwnd, WM_APP_EVENT, 0, 0);
    }).detach();
}

void Ui::HandleButton(const std::wstring& id) {
    launcher::App& app = launcher::App::Instance();
    bg::Manager& bm = bg::Manager::Instance();

    if (id == L"home.launch") {
        std::wstring vid;
        if (Widget* w = Find(L"home.version")) {
            if (w->sel >= 0 && w->sel < (int)w->items.size()) vid = w->items[w->sel];
        }
        if (vid.empty()) {
            m_progressText = L"请先选择版本";
            if (Widget* w = Find(L"home.ptext")) w->text = m_progressText;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        // 内存走设置页（app.maxMemory），直连服务器走设置页（launch 内部读 m_directServer）
        std::string err;
        if (!app.launch(theme::WToUtf8(vid), 854, 480, app.maxMemory(), err)) {
            m_progressText = theme::Utf8ToW("启动失败: " + err);
        } else {
            m_progressText = L"正在启动 " + vid + L" …";
        }
        if (Widget* w = Find(L"home.ptext")) w->text = m_progressText;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"ver.refresh") {
        m_progressText = L"正在拉取版本清单…";
        InvalidateRect(m_hwnd, nullptr, FALSE);
        std::thread([this]() {
            std::string err;
            bool ok = launcher::App::Instance().refreshRemote(err);
            std::lock_guard<std::mutex> lk(m_evMu);
            m_evQueue.push_back({ ok ? (int)launcher::Ev::InstallDone : (int)launcher::Ev::InstallError,
                ok ? std::string("__manifest__") : err });
            PostMessageW(m_hwnd, WM_APP_EVENT, 0, 0);
        }).detach();
    }
    else if (id == L"ver.download") {
        std::wstring vid;
        if (Widget* w = Find(L"ver.list")) {
            if (w->selIdx >= 0 && w->selIdx < (int)w->listItems.size()) vid = w->listItems[w->selIdx];
        }
        if (vid.empty()) {
            if (Widget* w = Find(L"ver.search")) vid = w->editText;
        }
        if (vid.empty()) { m_progressText = L"请先选择或输入要下载的版本"; InvalidateRect(m_hwnd, nullptr, FALSE); return; }
        int ltype = 0;
        if (Widget* w = Find(L"ver.loadertype")) ltype = w->sel;
        // 加载器版本（自动最新 = 空字符串）
        std::string lver;
        if (Widget* w = Find(L"ver.loaderver")) {
            if (w->sel > 0 && w->sel < (int)w->items.size()) lver = theme::WToUtf8(w->items[w->sel]);
        }
        m_progressText = L"开始安装 " + vid + (ltype > 0 ? L" + 加载器" : L"") + L" …";
        // 弹出下载进度窗口（HMCL 式）
        m_dlDialogTitle = vid;
        m_showDlDialog = true;
        m_progress = 0.0f;
        app.startInstallWithLoader(theme::WToUtf8(vid), ltype, lver);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"ver.verset") {
        std::wstring vid;
        if (Widget* w = Find(L"ver.local")) {
            if (w->selIdx >= 0 && w->selIdx < (int)w->listItems.size()) vid = w->listItems[w->selIdx];
        }
        if (vid.empty()) {
            m_progressText = L"请先在「已安装版本」选中一个版本";
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        m_editVersionId = vid;
        m_progressText = L"";
        SetPage(VersionSettings);
    }
    else if (id == L"ver.launch") {
        std::wstring vid;
        if (Widget* w = Find(L"ver.local")) {
            if (w->selIdx >= 0 && w->selIdx < (int)w->listItems.size()) vid = w->listItems[w->selIdx];
        }
        if (vid.empty()) {
            m_progressText = L"请先选中一个已安装版本";
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        m_currentGameVersion = vid;
        std::string err;
        if (!app.launch(theme::WToUtf8(vid), 854, 480, app.maxMemory(), err)) {
            m_progressText = theme::Utf8ToW("启动失败: " + err);
        } else {
            m_progressText = L"正在启动 " + vid + L" …";
        }
        if (Widget* w = Find(L"ver.ptext")) w->text = m_progressText;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"ver.del") {
        std::wstring vid;
        if (Widget* w = Find(L"ver.local")) {
            if (w->selIdx >= 0 && w->selIdx < (int)w->listItems.size()) vid = w->listItems[w->selIdx];
        }
        if (vid.empty()) {
            m_progressText = L"请先选中一个已安装版本";
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        fs::path vd = fs::u8path(app.gameDir()) / "versions" / fs::u8path(theme::WToUtf8(vid));
        std::error_code ec;
        fs::remove_all(vd, ec);
        m_progressText = ec ? L"删除失败" : (L"已删除 " + vid);
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id.rfind(L"dl.cat", 0) == 0) {
        m_downloadCategory = atoi(theme::WToUtf8(id.substr(6)).c_str());
        m_dlResults.clear();
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"dl.csearchbtn") {
        std::wstring q;
        if (Widget* w = Find(L"dl.csearch")) q = w->editText;
        if (q.empty()) { m_progressText = L"请输入搜索关键词"; InvalidateRect(m_hwnd, nullptr, FALSE); return; }
        static const char* kTypes[] = { "mod", "mod", "resourcepack", "shader", "datapack" };
        std::string type = kTypes[m_downloadCategory];
        m_progressText = L"正在搜索…";
        InvalidateRect(m_hwnd, nullptr, FALSE);
        std::thread([this, q, type]() {
            std::string err;
            auto res = modmgr::SearchModrinth(theme::WToUtf8(q), type, err);
            std::lock_guard<std::mutex> lk(m_evMu);
            m_dlResults = res;
            m_evQueue.push_back({ (int)launcher::Ev::InstallDone, std::string("__dlsearch__") });
            PostMessageW(m_hwnd, WM_APP_EVENT, 0, 0);
        }).detach();
    }
    else if (id == L"dl.cdownload") {
        std::string slug;
        if (Widget* w = Find(L"dl.cresults")) {
            if (w->selIdx >= 0 && w->selIdx < (int)m_dlResults.size()) slug = m_dlResults[w->selIdx].slug;
        }
        if (slug.empty()) { m_progressText = L"请先选中一个结果"; InvalidateRect(m_hwnd, nullptr, FALSE); return; }
        static const wchar_t* kDirs[] = { L"mods", L"mods", L"resourcepacks", L"shaderpacks", L"saves" };
        std::wstring dir = (fs::u8path(app.gameDir()) / kDirs[m_downloadCategory]).wstring();
        m_progressText = L"正在下载…";
        InvalidateRect(m_hwnd, nullptr, FALSE);
        std::string slug2 = slug;
        std::thread([this, slug2, dir]() {
            std::string err;
            std::wstring out;
            bool ok = modmgr::DownloadMod(slug2, dir, "", "", out, err);
            std::lock_guard<std::mutex> lk(m_evMu);
            m_evQueue.push_back({ ok ? (int)launcher::Ev::InstallDone : (int)launcher::Ev::InstallError,
                ok ? ("__dlmod__:" + theme::WToUtf8(out)) : err });
            PostMessageW(m_hwnd, WM_APP_EVENT, 0, 0);
        }).detach();
    }
    else if (id == L"acc.offline") {
        std::wstring name = L"Steve";
        if (Widget* w = Find(L"acc.name")) name = w->editText;
        app.authOffline(theme::WToUtf8(name));
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"acc.msstart") {
        msauth::DeviceCode dc; std::string err;
        if (!app.authMsStart(dc, err)) {
            m_msUserCode = L"失败: " + theme::Utf8ToW(err);
        } else {
            m_msUserCode = theme::Utf8ToW(dc.userCode);
            m_msVerifyUri = theme::Utf8ToW(dc.verificationUri);
            m_msPending = true;
        }
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"acc.mspoll") {
        if (!m_msPending) { m_msUserCode = L"请先获取设备码"; Rebuild(); InvalidateRect(m_hwnd, nullptr, FALSE); return; }
        std::thread([this]() {
            msauth::Account acc; std::string err;
            int r = launcher::App::Instance().authMsPoll(acc, err);
            std::lock_guard<std::mutex> lk(m_evMu);
            if (r == 1) m_evQueue.push_back({ (int)launcher::Ev::AccountChange, "" });
            else m_evQueue.push_back({ (int)launcher::Ev::InstallError, (r == 0 ? "仍在等待授权" : err) });
            PostMessageW(m_hwnd, WM_APP_EVENT, 0, 0);
        }).detach();
    }
    else if (id == L"mod.detect") {
        RefreshModsList();
        m_modStatus = m_missingMods.empty() ? L"未发现缺失的前置 ✅"
            : (L"缺失 " + std::to_wstring(m_missingMods.size()) + L" 个前置");
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"mod.fix") {
        RefreshModsList();
        if (m_missingMods.empty()) { m_modStatus = L"无需补充"; Rebuild(); InvalidateRect(m_hwnd, nullptr, FALSE); return; }
        auto missing = m_missingMods;
        std::thread([this, missing]() {
            std::wstring modsDir = (fs::u8path(launcher::App::Instance().gameDir()) / L"mods").wstring();
            int ok = 0, fail = 0;
            for (auto& mid : missing) {
                std::string err;
                std::vector<modmgr::SearchResult> res = modmgr::SearchModrinth(mid, "mod", err);
                if (res.empty()) { fail++; continue; }
                std::wstring out;
                if (modmgr::DownloadMod(res[0].slug, modsDir, "fabric", "", out, err)) ok++; else fail++;
            }
            std::lock_guard<std::mutex> lk(m_evMu);
            m_evQueue.push_back({ (int)launcher::Ev::InstallDone, "modfix:" + std::to_string(ok) + ":" + std::to_string(fail) });
            PostMessageW(m_hwnd, WM_APP_EVENT, 0, 0);
        }).detach();
        m_modStatus = L"正在补充前置…";
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"mod.searchbtn") {
        std::wstring q;
        if (Widget* w = Find(L"mod.search")) q = w->editText;
        if (q.empty()) { m_modStatus = L"请输入搜索词"; Rebuild(); InvalidateRect(m_hwnd, nullptr, FALSE); return; }
        m_modStatus = L"正在搜索 Modrinth…";
        Rebuild(); InvalidateRect(m_hwnd, nullptr, FALSE);
        std::thread([this, q]() {
            std::string err;
            auto res = modmgr::SearchModrinth(theme::WToUtf8(q), "mod", err);
            json::Value arr = json::makeArray();
            for (auto& r : res) {
                json::Value e = json::makeObject();
                e.obj["slug"] = json::makeString(r.slug);
                e.obj["title"] = json::makeString(r.title);
                e.obj["description"] = json::makeString(r.description);
                arr.arr.push_back(e);
            }
            std::lock_guard<std::mutex> lk(m_evMu);
            if (!err.empty() && res.empty()) {
                m_evQueue.push_back({ (int)launcher::Ev::InstallError, err });
            } else {
                m_evQueue.push_back({ (int)launcher::Ev::InstallDone, "__modsearch__:" + json::dump(arr) });
            }
            PostMessageW(m_hwnd, WM_APP_EVENT, 0, 0);
        }).detach();
    }
    else if (id == L"mod.dlbtn") {
        int sel = -1;
        if (Widget* w = Find(L"mod.results")) sel = w->selIdx;
        if (sel < 0 || sel >= (int)m_searchResults.size()) { m_modStatus = L"请先选择要下载的 Mod"; Rebuild(); InvalidateRect(m_hwnd, nullptr, FALSE); return; }
        std::string slug = m_searchResults[sel].slug;
        m_modStatus = L"正在下载 " + theme::Utf8ToW(m_searchResults[sel].title) + L"…";
        Rebuild(); InvalidateRect(m_hwnd, nullptr, FALSE);
        std::thread([this, slug]() {
            std::wstring modsDir = (fs::u8path(launcher::App::Instance().gameDir()) / L"mods").wstring();
            std::wstring out; std::string err;
            bool ok = modmgr::DownloadMod(slug, modsDir, "", "", out, err);
            std::lock_guard<std::mutex> lk(m_evMu);
            m_evQueue.push_back({ ok ? (int)launcher::Ev::InstallDone : (int)launcher::Ev::InstallError,
                ok ? ("__moddl__:" + theme::WToUtf8(out)) : err });
            PostMessageW(m_hwnd, WM_APP_EVENT, 0, 0);
        }).detach();
    }
    else if (id == L"save.backup" || id == L"save.del" || id == L"save.open") {
        std::wstring name;
        if (Widget* w = Find(L"save.list")) {
            if (w->selIdx >= 0 && w->selIdx < (int)w->listItems.size()) name = w->listItems[w->selIdx];
        }
        if (name.empty()) { m_progressText = L"请先选择一个存档"; Rebuild(); InvalidateRect(m_hwnd, nullptr, FALSE); return; }
        fs::path savesDir = fs::u8path(app.gameDir()) / "saves";
        fs::path src = savesDir / fs::u8path(theme::WToUtf8(name));
        if (id == L"save.backup") {
            fs::path backupsDir = fs::u8path(app.gameDir()) / "backups";
            std::error_code ec; fs::create_directories(backupsDir, ec);
            std::wstring ts = L"_" + std::to_wstring(GetTickCount());
            fs::path dst = backupsDir / fs::u8path(theme::WToUtf8(name) + theme::WToUtf8(ts));
            std::error_code ec2; fs::copy(src, dst, fs::copy_options::recursive, ec2);
            m_progressText = ec2 ? L"备份失败" : (L"已备份到 backups\\" + dst.filename().wstring());
        }
        else if (id == L"save.del") {
            std::error_code ec; fs::remove_all(src, ec);
            m_progressText = ec ? L"删除失败" : (L"已删除 " + name);
        }
        else {
            ShellExecuteW(m_hwnd, L"open", src.wstring().c_str(), nullptr, nullptr, SW_SHOW);
            m_progressText = L"已打开存档目录";
        }
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"set.srvadd") {
        std::wstring addr;
        if (Widget* w = Find(L"set.srvaddr")) addr = w->editText;
        if (addr.empty()) {
            m_progressText = L"请输入服务器地址（如 play.example.com 或 host:25565）";
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        std::string a = theme::WToUtf8(addr);
        int port = 25565;
        size_t c = a.rfind(':');
        if (c != std::string::npos && c + 1 < a.size()) {
            int p = atoi(a.substr(c + 1).c_str());
            if (p > 0 && p < 65536) { port = p; a = a.substr(0, c); }
        }
        app.addServer(a, port);
        m_progressText = L"已添加服务器 " + theme::Utf8ToW(a);
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"set.srvdel") {
        if (Widget* w = Find(L"set.srvlist")) {
            if (w->selIdx >= 0 && w->selIdx < (int)w->listItems.size()) {
                app.removeServer((size_t)w->selIdx);
                m_progressText = L"已删除服务器";
                Rebuild();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        }
    }
    else if (id == L"set.srvdir") {
        if (Widget* w = Find(L"set.srvlist")) {
            if (w->selIdx >= 0 && w->selIdx < (int)w->listItems.size()) {
                auto servers = app.servers();
                app.setDirectServer(servers[w->selIdx].first, servers[w->selIdx].second);
                m_progressText = L"已设为直连：" + theme::Utf8ToW(servers[w->selIdx].first);
                Rebuild();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        }
    }
    else if (id == L"set.save") {
        if (Widget* w = Find(L"set.gamedir")) app.setGameDir(theme::WToUtf8(w->editText));
        if (Widget* w = Find(L"set.java")) {
            std::string jp = theme::WToUtf8(w->editText);
            if (!java::IsValidJava(jp)) {
                m_progressText = L"Java 路径无效（不存在或非 java.exe）：" + w->editText;
                if (Widget* s = Find(L"set.status")) s->text = m_progressText;
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            app.setJavaPath(jp);
        }
        if (Widget* w = Find(L"set.mem")) app.setMaxMemory(w->value);
        if (Widget* w = Find(L"set.source")) {
            if (w->sel == 1) app.setSource("mcbbs");
            else if (w->sel == 2) app.setSource("mojang");
            else app.setSource("bmclapi");
        }
        if (Widget* w = Find(L"set.bgmode")) {
            int sel = w->sel;
            if (sel == 0) bm.SetMode(0);
            else if (sel >= 1 && sel <= 3) { bm.SetMode(1); bm.SetBuiltinIndex(sel - 1); }
            else if (sel == 4) bm.SetMode(2);
            else bm.SetMode(3);
        }
        if (Widget* w = Find(L"set.bgimage")) bm.SetImage(w->editText);
        if (Widget* w = Find(L"set.bgdir")) bm.SetCycleDir(w->editText);
        if (Widget* w = Find(L"set.blur")) bm.SetBlur(w->checked);
        if (Widget* w = Find(L"set.blurstr")) bm.SetBlurStrength(w->value);
        if (Widget* w = Find(L"set.isolation")) app.setVersionIsolation(w->checked);
        app.saveConfig();
        bm.Save();
        m_progressText = L"设置已保存";
        if (Widget* w = Find(L"set.status")) w->text = m_progressText;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"set.browse") {
        // 根据聚焦的输入框类型：Java → 选文件；背景图片 → 选图片；其余 → 选目录
        Widget* javaEdit = Find(L"set.java");
        Widget* imgEdit = Find(L"set.bgimage");
        if (javaEdit && javaEdit->focused) {
            wchar_t path[MAX_PATH] = { 0 };
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = m_hwnd;
            ofn.lpstrFilter = L"Java 可执行文件\0java.exe;javaw.exe\0所有文件\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) javaEdit->editText = path;
        } else if (imgEdit && imgEdit->focused) {
            wchar_t path[MAX_PATH] = { 0 };
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = m_hwnd;
            ofn.lpstrFilter = L"图片文件\0*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.gif\0所有文件\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) imgEdit->editText = path;
        } else {
            BROWSEINFOW bi{};
            bi.hwndOwner = m_hwnd;
            bi.lpszTitle = L"选择目录";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, path)) {
                    if (Widget* w = Find(L"set.gamedir")) if (w->focused) w->editText = path;
                    if (Widget* w = Find(L"set.bgdir")) if (w->focused) w->editText = path;
                }
                CoTaskMemFree(pidl);
            }
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"vs.save") {
        launcher::VersionSettings vs;
        if (Widget* w = Find(L"vs.java")) vs.javaPath = theme::WToUtf8(w->editText);
        if (Widget* w = Find(L"vs.mem")) vs.maxMemoryMB = atoi(theme::WToUtf8(w->editText).c_str());
        if (Widget* w = Find(L"vs.width")) vs.width = atoi(theme::WToUtf8(w->editText).c_str());
        if (Widget* w = Find(L"vs.height")) vs.height = atoi(theme::WToUtf8(w->editText).c_str());
        if (Widget* w = Find(L"vs.fullscreen")) vs.fullscreen = w->checked;
        // 空格分隔拆分 JVM / 游戏参数
        auto splitArgs = [](const std::wstring& s) {
            std::vector<std::string> out;
            std::wstring cur;
            for (wchar_t c : s) {
                if (c == L' ' || c == L'\t') { if (!cur.empty()) { out.push_back(theme::WToUtf8(cur)); cur.clear(); } }
                else cur.push_back(c);
            }
            if (!cur.empty()) out.push_back(theme::WToUtf8(cur));
            return out;
        };
        if (Widget* w = Find(L"vs.jvmargs")) vs.jvmArgs = splitArgs(w->editText);
        if (Widget* w = Find(L"vs.gameargs")) vs.gameArgs = splitArgs(w->editText);
        if (Widget* w = Find(L"vs.server")) {
            std::string a = theme::WToUtf8(w->editText);
            if (!a.empty()) {
                int port = 25565;
                size_t c = a.rfind(':');
                if (c != std::string::npos && c + 1 < a.size()) {
                    int p = atoi(a.substr(c + 1).c_str());
                    if (p > 0 && p < 65536) { port = p; a = a.substr(0, c); }
                }
                vs.server = a;
                vs.serverPort = port;
            }
        }
        if (vs.width <= 0) vs.width = 854;
        if (vs.height <= 0) vs.height = 480;
        app.setVersionSettings(theme::WToUtf8(m_editVersionId), vs);
        m_progressText = L"版本设置已保存";
        if (Widget* w = Find(L"vs.status")) w->text = m_progressText;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (id == L"vs.back") {
        SetPage(Versions);
    }
}

} // namespace ui
