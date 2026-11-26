// UiActions.cpp — 交互逻辑：按钮、卡片、下拉、异步任务、文件选择
#include "Ui.h"
#include "UiNav.h"
#include "Theme.h"
#include "Background.h"
#include "LauncherApp.h"
#include "ContentLib.h"
#include "ModManager.h"
#include "Json.h"
#include "Skin.h"
#include "SkinNet.h"

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <set>
#include <thread>

namespace ui {

namespace fs = std::filesystem;

// 内容（模组/资源包/光影/世界）下载时用的「游戏版本号」：
// 实例可能是 fabric-loader-0.16.5-1.20.1，直接拿去请求 Modrinth 会一条都没有，
// 必须沿 inheritsFrom 回溯到原版版本号（和 HMCL 一样用实例的 MC 版本）。
static std::string mcBaseVerOf(const mlc::Instance* in) {
    if (!in) return std::string();
    std::string v = mc::baseVersionId(mlc::JoinPath(in->folder, "versions"), in->versionId);
    return v.empty() ? in->versionId : v;
}

// ---------------------------------------------------------------
// 文件选择
// ---------------------------------------------------------------
static bool PickFile(HWND owner, const wchar_t* filter, std::wstring& out) {
    wchar_t path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return false;
    out = path;
    return true;
}

static bool PickSaveFile(HWND owner, const std::wstring& defaultName,
                         const std::wstring& ext, std::wstring& out) {
    wchar_t path[MAX_PATH] = { 0 };
    wcsncpy(path, defaultName.c_str(), MAX_PATH - 1);
    std::wstring filter;
    if (!ext.empty()) {
        filter += L"文件 (*"; filter += ext; filter += L")"; filter.push_back(0);
        filter += L"*"; filter += ext; filter.push_back(0);
    }
    filter += L"所有文件"; filter.push_back(0);
    filter += L"*.*"; filter.push_back(0); filter.push_back(0);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = ext.empty() ? nullptr : ext.c_str() + 1;
    if (!GetSaveFileNameW(&ofn)) return false;
    out = path;
    return true;
}

static bool PickFolder(HWND owner, std::wstring& out) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"选择游戏文件夹（.minecraft 目录或其上级）";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t path[MAX_PATH] = { 0 };
    bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    if (ok) out = path;
    return ok;
}

static int parseInt(const std::wstring& s, int def) {
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }
}

static std::vector<std::string> splitArgs(const std::wstring& s) {
    std::vector<std::string> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t') {
            if (!cur.empty()) { out.push_back(mlc::WToUtf8(cur)); cur.clear(); }
        } else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(mlc::WToUtf8(cur));
    return out;
}

// ---------------------------------------------------------------
// 卡片网格命中
// ---------------------------------------------------------------
int Ui::HitGridIndex(const Widget& w, int x, int y) {
    if (x < w.rc.left || x >= w.rc.right || y < w.rc.top || y >= w.rc.bottom) return -1;
    int colW = w.cardW + w.gap;
    int rowH = w.cardH + w.gap;
    if (colW <= 0 || rowH <= 0) return -1;
    int relX = x - w.rc.left, relY = y - w.rc.top;
    int col = relX / colW;
    int vrow = relY / rowH;
    int ix = relX - col * colW;
    int iy = relY - vrow * rowH;
    if (col >= w.cols || ix > w.cardW || iy > w.cardH) return -1;
    int row = vrow + w.gscroll;
    int idx = row * w.cols + col;
    if (idx < 0 || idx >= (int)w.items2.size()) return -1;
    return idx;
}

bool Ui::HitGridButton(const Widget& w, int idx, int x, int y) {
    if (idx < 0 || idx >= (int)w.items2.size()) return false;
    const GridItem& it = w.items2[idx];
    int col = idx % w.cols;
    int row = idx / w.cols;
    float cx = (float)(w.rc.left + col * (w.cardW + w.gap));
    float cy = (float)(w.rc.top + (row - w.gscroll) * (w.cardH + w.gap));
    float bx = cx + w.cardW - 88;
    float by = cy + w.cardH - 32;
    if (it.toggleable) bx = cx + w.cardW - 52;
    if (it.toggleable || !it.action.empty()) {
        
    mlc::Log("[HitGridButton] mouse=(" + std::to_string(x) + "," + std::to_string(y) + ") btn=(" + std::to_string((int)bx) + "," + std::to_string((int)by) + ") size=(" + std::to_string((int)(cx + w.cardW - 6)) + "," + std::to_string((int)(cy + w.cardH - 4)) + ") idx=" + std::to_string(idx));
    return x >= bx && x <= cx + w.cardW - 6 && y >= by && y <= cy + w.cardH - 4;
    }
    return false;
}

// ---- 平铺列表命中 ----
int Ui::HitRowIndex(const Widget& w, int x, int y) {
    if (x < w.rc.left || x >= w.rc.right || y < w.rc.top || y >= w.rc.bottom) return -1;
    int rowH = std::max(28, w.rowH);
    int vrow = (y - w.rc.top) / rowH;
    int within = (y - w.rc.top) - vrow * rowH;
    if (within > rowH - 4) return -1;              // 行与行之间的空隙
    int idx = w.gscroll + vrow;
    if (idx < 0 || idx >= (int)w.items2.size()) return -1;
    return idx;
}

bool Ui::HitRowButton(const Widget& w, int idx, int x, int y) {
    if (idx < 0 || idx >= (int)w.items2.size()) return false;
    if (w.items2[idx].action.empty()) return false;
    int rowH = std::max(28, w.rowH);
    int vrow = idx - w.gscroll;
    float rowRight = (float)w.rc.left + (float)(w.rc.right - w.rc.left) - 6.0f;
    float bx = rowRight - 74.0f - 8.0f;
    float by = (float)w.rc.top + (float)(vrow * rowH) + (float)((rowH - 4 - 26) / 2);
    
    mlc::Log("[HitRowButton] mouse=(" + std::to_string(x) + "," + std::to_string(y) + ") btn=(" + std::to_string((int)bx) + "," + std::to_string((int)by) + ") idx=" + std::to_string(idx));
    return x >= bx && x <= rowRight - 8 && y >= by && y <= by + 26;
}

// ---------------------------------------------------------------
// 数据刷新（首次进入时同步扫描一次，之后走缓存）
// ---------------------------------------------------------------
void Ui::RefreshMods(bool force) {
    mlc::Instance* in = CurInst();
    if (!in) return;
    if (m_cacheInstId != mlc::Utf8ToW(in->id)) {
        m_cacheInstId = mlc::Utf8ToW(in->id);
        m_modsLoaded = m_packsLoaded = m_worldsLoaded = m_schemsLoaded = m_shadersLoaded = false;
    }
    if (m_modsLoaded && !force) return;
    m_mods = content::Scan(in->modsDir(), content::Kind::Mods);
    m_modsLoaded = true;
}

void Ui::RefreshPacks(bool force) {
    mlc::Instance* in = CurInst();
    if (!in) return;
    if (m_cacheInstId != mlc::Utf8ToW(in->id)) {
        m_cacheInstId = mlc::Utf8ToW(in->id);
        m_modsLoaded = m_packsLoaded = m_worldsLoaded = m_schemsLoaded = m_shadersLoaded = false;
    }
    if (m_packsLoaded && !force) return;
    m_packs = content::Scan(in->resourcePacksDir(), content::Kind::ResourcePacks);
    m_packsLoaded = true;
}

void Ui::RefreshShaders(bool force) {
    mlc::Instance* in = CurInst();
    if (!in) return;
    if (m_cacheInstId != mlc::Utf8ToW(in->id)) {
        m_cacheInstId = mlc::Utf8ToW(in->id);
        m_modsLoaded = m_packsLoaded = m_worldsLoaded = m_schemsLoaded = m_shadersLoaded = false;
    }
    if (m_shadersLoaded && !force) return;
    m_shaders = content::Scan(in->shaderPacksDir(), content::Kind::Shaders);
    m_shadersLoaded = true;
}

void Ui::RefreshWorlds(bool force) {
    mlc::Instance* in = CurInst();
    if (!in) return;
    if (m_cacheInstId != mlc::Utf8ToW(in->id)) {
        m_cacheInstId = mlc::Utf8ToW(in->id);
        m_modsLoaded = m_packsLoaded = m_worldsLoaded = m_schemsLoaded = m_shadersLoaded = false;
    }
    if (m_worldsLoaded && !force) return;
    m_worlds = content::Scan(in->savesDir(), content::Kind::Worlds);
    m_worldsLoaded = true;
}

void Ui::RefreshSchematics(bool force) {
    mlc::Instance* in = CurInst();
    if (!in) return;
    if (m_cacheInstId != mlc::Utf8ToW(in->id)) {
        m_cacheInstId = mlc::Utf8ToW(in->id);
        m_modsLoaded = m_packsLoaded = m_worldsLoaded = m_schemsLoaded = m_shadersLoaded = false;
    }
    if (m_schemsLoaded && !force) return;
    m_schems = content::Scan(in->schematicsDir(), content::Kind::Schematics);
    m_schemsLoaded = true;
}

void Ui::RefreshInstances() {
    m_dlFolders.clear();
    for (auto& f : mlc::Store::I().folders()) m_dlFolders.push_back(mlc::Utf8ToW(f));
    Rebuild();
}

// ---------------------------------------------------------------
// 加载器版本
// ---------------------------------------------------------------
void Ui::RefreshLoaderVersions() {
    mlc::Instance* in = CurInst();
    if (m_loaderFetching) return;
    std::string mc;
    if (m_page == Download && !m_dlVersionId.empty()) mc = mlc::WToUtf8(m_dlVersionId);
    else if (in) {
        // 实例可能本身就是加载器版本（fabric-loader-0.16.5-1.20.1），
        // meta / Forge 接口只认原版 id —— 先沿 inheritsFrom 回溯到原版。
        mc = mc::baseVersionId(mlc::JoinPath(in->folder, "versions"), in->versionId);
        if (mc.empty()) mc = in->versionId;
    }
    if (mc.empty()) return;
    int type = m_loaderSel;   // 0 Fabric 1 Quilt 2 Forge 3 NeoForge
    m_loaderFetching = true;
    m_loaderStatus = L"正在获取加载器版本…";
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    auto sink = m_sink;
    std::thread([sink, mc, type]() {
        std::vector<std::wstring> vers;
        std::string err;
        if (type == 0 || type == 1) {
            auto v = loader::FetchVersions(mc, type == 1, err);
            for (auto& x : v) vers.push_back(mlc::Utf8ToW(x.version));
        } else if (type == 2) {
            auto v = loader::FetchForgeVersions(mc, err);
            for (auto& x : v) vers.push_back(mlc::Utf8ToW(x));
        } else {
            auto v = loader::FetchNeoForgeVersions(mc, err);
            for (auto& x : v) vers.push_back(mlc::Utf8ToW(x));
        }
        if (vers.size() > 60) vers.resize(60);   // 下拉太长不好选
        // 结果通过事件回到 UI 线程再写入成员（避免跨线程读写 vector）
        json::Value arr = json::makeArray();
        for (auto& v : vers) arr.arr.push_back(json::makeString(mlc::WToUtf8(v)));
        std::string payload = "__loaderver__:" + json::dump(arr);
        if (!err.empty() && vers.empty()) payload += "|" + err;
        sink->post((int)launcher::Ev::InstallDone, payload);
    }).detach();
}

// ---------------------------------------------------------------
// CurseForge 世界搜索 / 下载
// ---------------------------------------------------------------
// 封面抓取（独立函数，避免 MSVC 对深嵌套 lambda 报 C1001）
static void FetchCfCovers(std::shared_ptr<EventSink> sink, std::vector<std::pair<int, std::string>> items) {
    if (items.empty()) return;
    std::atomic<size_t> next{ 0 };
    std::mutex mu;
    std::vector<std::pair<int, std::string>> pending;

    auto flush = [&]() {
        std::vector<std::pair<int, std::string>> batch;
        {
            std::lock_guard<std::mutex> lk(mu);
            if (pending.empty()) return;
            batch.swap(pending);
        }
        json::Value covers = json::makeArray();
        for (auto& kv : batch) {
            json::Value e = json::makeArray();
            e.arr.push_back(json::makeInt(kv.first));
            e.arr.push_back(json::makeString(kv.second));
            covers.arr.push_back(e);
        }
        if (sink) sink->post((int)launcher::Ev::Info, "__cfcovers__:" + json::dump(covers));
    };

    auto worker = [&]() {
        for (;;) {
            size_t i = next.fetch_add(1);
            if (i >= items.size()) return;
            const auto& it = items[i];
            if (it.second.empty()) continue;
            std::string p = content::CacheCover(it.second, "cf_world_" + std::to_string(it.first));
            if (p.empty()) continue;
            bool enough = false;
            {
                std::lock_guard<std::mutex> lk(mu);
                pending.push_back({ it.first, p });
                enough = pending.size() >= 4;
            }
            if (enough) flush();
        }
    };

    std::vector<std::thread> pool;
    for (int i = 0; i < 4; ++i) pool.emplace_back(worker);
    for (auto& th : pool) if (th.joinable()) th.join();
    flush();
}

void Ui::StartCfSearch(int page) {
    if (m_cfSearching) return;
    if (page < 0) page = 0;
    std::string q = mlc::WToUtf8(m_dlSearch);
    const auto& gs = mlc::Store::I().spec();
    std::string base = gs.cfBase;
    std::string key = gs.cfApiKey;

    m_cfSearching = true;
    m_cfPage = page;
    m_cfStatus.clear();
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    auto sink = m_sink;
    std::thread([sink, q, base, key, page]() {
        mlc::Log("[cf] search begin q='" + q + "' base=" + base);
        std::string err;
        auto hits = content::cf::SearchWorlds(q, page * 12, 12, base, key, err);
        mlc::Log("[cf] search done hits=" + std::to_string(hits.size()) + " err=" + err);

        json::Value arr = json::makeArray();
        std::vector<std::pair<int, std::string>> covers;
        for (auto& h : hits) {
            json::Value e = json::makeObject();
            e.obj["modId"] = json::makeInt(h.modId);
            e.obj["fileId"] = json::makeInt(h.fileId);
            e.obj["name"] = json::makeString(h.name);
            e.obj["summary"] = json::makeString(h.summary);
            e.obj["fileName"] = json::makeString(h.fileName);
            e.obj["gameVersion"] = json::makeString(h.gameVersion);
            e.obj["author"] = json::makeString(h.author);
            e.obj["downloads"] = json::makeInt(h.downloads);
            e.obj["cover"] = json::makeString("");
            arr.arr.push_back(e);
            covers.push_back({ h.modId, h.coverUrl });
        }
        json::Value root = json::makeObject();
        root.obj["hits"] = arr;
        root.obj["err"] = json::makeString(err);
        sink->post((int)launcher::Ev::InstallDone, "__cfsearch__:" + json::dump(root));

        // 封面另起线程慢慢补，不阻塞结果展示
        std::thread(FetchCfCovers, sink, std::move(covers)).detach();
    }).detach();
}

void Ui::StartCfDownload(const content::cf::WorldHit& hit) {
    mlc::Instance* in = CurInst();
    if (!in) { ShowToast(L"请先添加一个游戏：世界会装进当前游戏的 saves 目录", true); return; }
    const auto& gs = mlc::Store::I().spec();
    std::string base = gs.cfBase;
    std::string key = gs.cfApiKey;
    std::string saves = in->savesDir();
    std::wstring title = mlc::Utf8ToW(hit.name);

    m_cfStatus = L"正在下载 " + title + L"…";
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    auto sink = m_sink;
    std::thread([sink, hit, saves, base, key]() {
        std::string out, err;
        bool ok = content::cf::InstallWorld(hit, saves, base, key, out, err);
        if (ok) {
            sink->post((int)launcher::Ev::InstallDone,
                       "__content__:" + mlc::WToUtf8(L"已安装世界：" + mlc::Utf8ToW(out)));
        } else {
            sink->post((int)launcher::Ev::InstallError, err);
        }
    }).detach();
}

// ---------------------------------------------------------------
// 启动 / 完整性
// ---------------------------------------------------------------
void Ui::StartLaunch(const std::wstring& instanceId) {
    launcher::App& app = launcher::App::Instance();
    if (app.gameRunning()) { app.stopGame(); return; }
    if (m_launching) { ShowToast(L"游戏正在启动中，请稍候…"); return; }
    if (app.installing()) { ShowToast(L"正在下载/安装中，请稍候"); return; }

    std::string iid = mlc::WToUtf8(instanceId);
    if (iid.empty()) {
        mlc::Instance* in = CurInst();
        if (!in) { ShowToast(L"还没有游戏：请先在「版本选择」添加", true); return; }
        iid = in->id;
    }
    m_launching = true;
    m_installing = true;
    m_progress = 0.0f;
    m_progressText = L"准备启动…";
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    auto sink = m_sink;
    std::thread([sink, iid]() {
        std::string err;
        bool ok = launcher::App::Instance().launchInstance(iid, err);
        if (!ok) sink->post((int)launcher::Ev::InstallError, err);
    }).detach();
}

void Ui::StartIntegrityCheck(bool repair) {
    mlc::Instance* in = CurInst();
    if (!in) { ShowToast(L"请先选择一个游戏", true); return; }
    m_installing = true;
    m_progressText = L"正在检查完整性…";
    launcher::App::Instance().startIntegrityCheck(in->id, repair);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------
// 内容搜索
// ---------------------------------------------------------------
void Ui::StartDownloadSearch() {
    if (m_dlSearching) return;
    mlc::Instance* in = CurInst();

    std::string type;
    switch (m_dlCat) {
        case 1: type = "mod"; break;
        case 2: type = "resourcepack"; break;
        case 3: type = "shader"; break;
        default: return;
    }
    std::string loader, mcver;
    if (m_dlCat == 1 && in) {
        loader = in->loader.empty() ? "fabric" : in->loader;
        if (loader == "neoforge") loader = "neoforge";
        mcver = mcBaseVerOf(in);
    }
    std::string q = mlc::WToUtf8(m_dlSearch);
    // 空关键词 = 像 HMCL 一样列出该分类的全部热门项目（每页 20 条）

    m_dlSearching = true;
    m_hits.clear();
    m_progressText = q.empty() ? L"正在加载全部…" : (L"正在搜索 " + m_dlSearch);
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    auto sink = m_sink;
    const int offset = m_dlOffset;
    std::thread([sink, q, type, loader, mcver, offset]() {
        std::string err;
        auto hits = content::Search(q, type, loader, mcver, 20, offset, err);
        // 抓封面到本地缓存
        for (auto& h : hits) {
            if (h.iconUrl.empty()) continue;
            std::string key = "mr_" + h.slug;
            h.cover = content::CacheCover(h.iconUrl, key);
        }
        json::Value arr = json::makeArray();
        for (auto& h : hits) {
            json::Value e = json::makeObject();
            e.obj["slug"] = json::makeString(h.slug);
            e.obj["title"] = json::makeString(h.title);
            e.obj["desc"] = json::makeString(h.desc);
            e.obj["author"] = json::makeString(h.author);
            e.obj["cover"] = json::makeString(h.cover);
            e.obj["downloads"] = json::makeInt(h.downloads);
            arr.arr.push_back(e);
        }
        if (hits.empty() && !err.empty())
            sink->post((int)launcher::Ev::InstallError, err);
        else
            sink->post((int)launcher::Ev::InstallDone, "__search__:" + json::dump(arr));
    }).detach();
}

// ---------------------------------------------------------------
// HMCL 式模组下载：搜索 → 详情 → 版本 → 安装
// ---------------------------------------------------------------
static json::Value hitJson(const addon::Hit& h, const std::string& cover) {
    json::Value o = json::makeObject();
    o.obj["slug"] = json::makeString(h.slug);
    o.obj["projectId"] = json::makeString(h.projectId);
    o.obj["title"] = json::makeString(h.title);
    o.obj["desc"] = json::makeString(h.desc);
    o.obj["iconUrl"] = json::makeString(h.iconUrl);
    o.obj["author"] = json::makeString(h.author);
    o.obj["cover"] = json::makeString(cover);
    o.obj["pageUrl"] = json::makeString(h.pageUrl);
    json::Value cats = json::makeArray();
    for (const auto& c : h.categories) cats.arr.push_back(json::makeString(c));
    o.obj["categories"] = cats;
    return o;
}

void Ui::StartAddonCategories() {
    auto sink = m_sink;
    std::thread([sink]() {
        std::string err;
        auto cats = addon::GetCategories(err);
        json::Value arr = json::makeArray();
        for (const auto& c : cats) {
            json::Value o = json::makeObject();
            o.obj["id"] = json::makeString(c.id);
            o.obj["name"] = json::makeString(c.localized);
            arr.arr.push_back(o);
        }
        sink->post((int)launcher::Ev::InstallDone, "__addoncategories__:" + json::dump(arr));
    }).detach();
}

void Ui::StartAddonSearch(int page) {
    // 如果正在搜索，说明用户可能在等结果，或者上一次请求卡住了。
    // 这里我们不再直接 return，而是允许重新发起搜索（通过 ++m_addonSearchSeq 让旧请求作废）
    if (m_dlCat != 1) return;
    m_dlSearching = true;
    m_addonHits.clear();
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    // 注意：下面原来的代码会再次设置 m_dlSearching = true 并 clear，
    // 但这无伤大雅，关键是不要因为 m_dlSearching 为 true 就提前 return。
    m_addonPage = std::max(0, page);
    if (m_addonPages > 0) m_addonPage = std::min(m_addonPage, m_addonPages - 1);

    std::string q = mlc::WToUtf8(m_dlSearch);
    std::string gv = mlc::WToUtf8(m_addonMcVer);
    std::string cat;
    int sel = m_addonCategorySel;
    if (sel > 0 && sel < (int)m_addonCategoryIds.size()) cat = m_addonCategoryIds[sel];
    addon::Sort sort = (addon::Sort)std::clamp(m_addonSort, 0, 4);
    const unsigned long long seq = ++m_addonSearchSeq;

    m_dlSearching = true;
    m_addonHits.clear();
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    auto sink = m_sink;
    std::thread([sink, seq, q, gv, cat, sort, page = m_addonPage]() {
        std::string err;
        addon::SearchResult r = addon::Search(q, gv, cat, page, sort, err);
        json::Value root = json::makeObject();
        root.obj["seq"] = json::makeInt((int64_t)seq);
        root.obj["pages"] = json::makeInt(r.totalPages);
        root.obj["err"] = json::makeString(err);
        json::Value arr = json::makeArray();
        for (auto& h : r.hits) {
            std::string cover;
            if (!h.iconUrl.empty()) cover = content::CacheCover(h.iconUrl, "mr_" + h.slug);
            arr.arr.push_back(hitJson(h, cover));
        }
        root.obj["hits"] = arr;
        sink->post((int)launcher::Ev::InstallDone, "__addonsearch__:" + json::dump(root));
    }).detach();
}

void Ui::StartAddonVersions(const std::string& projectId) {
    if (projectId.empty()) return;
    m_addonVersionsLoading = true;
    m_addonVersions.clear();
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    auto sink = m_sink;
    std::thread([sink, projectId]() {
        std::string err;
        auto versions = addon::GetVersions(projectId, err);
        json::Value root = json::makeObject();
        root.obj["err"] = json::makeString(err);
        json::Value arr = json::makeArray();
        for (const auto& v : versions) {
            json::Value o = json::makeObject();
            o.obj["id"] = json::makeString(v.id);
            o.obj["projectId"] = json::makeString(v.projectId);
            o.obj["name"] = json::makeString(v.name);
            o.obj["versionNumber"] = json::makeString(v.versionNumber);
            o.obj["datePublished"] = json::makeString(v.datePublished);
            o.obj["versionType"] = json::makeString(v.versionType);
            o.obj["fileName"] = json::makeString(v.fileName);
            o.obj["url"] = json::makeString(v.url);
            o.obj["md5"] = json::makeString(v.md5);
            o.obj["sha1"] = json::makeString(v.sha1);
            o.obj["sha256"] = json::makeString(v.sha256);
            o.obj["sha512"] = json::makeString(v.sha512);
            json::Value gvs = json::makeArray(), ls = json::makeArray(), ds = json::makeArray();
            for (const auto& s : v.gameVersions) gvs.arr.push_back(json::makeString(s));
            for (const auto& s : v.loaders) ls.arr.push_back(json::makeString(s));
            for (const auto& d : v.dependencies) {
                json::Value x = json::makeObject();
                x.obj["type"] = json::makeString(
                    d.type == addon::Dependency::Type::Required ? "required" :
                    d.type == addon::Dependency::Type::Optional ? "optional" :
                    d.type == addon::Dependency::Type::Embedded ? "embedded" :
                    d.type == addon::Dependency::Type::Incompatible ? "incompatible" : "broken");
                x.obj["projectId"] = json::makeString(d.projectId);
                x.obj["versionId"] = json::makeString(d.versionId);
                ds.arr.push_back(x);
            }
            o.obj["gameVersions"] = gvs;
            o.obj["loaders"] = ls;
            o.obj["dependencies"] = ds;
            arr.arr.push_back(o);
        }
        root.obj["versions"] = arr;
        sink->post((int)launcher::Ev::InstallDone, "__addonversions__:" + json::dump(root));
    }).detach();
}

void Ui::StartAddonDependencies() {
    m_addonDepsLoading = true;
    m_addonDeps.clear();
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    auto sink = m_sink;
    auto deps = m_addonVersion.dependencies;
    std::thread([sink, deps]() {
        json::Value arr = json::makeArray();
        for (const auto& d : deps) {
            if (d.type == addon::Dependency::Type::Incompatible) continue;
            json::Value o = json::makeObject();
            o.obj["type"] = json::makeString(
                d.type == addon::Dependency::Type::Required ? "required" :
                d.type == addon::Dependency::Type::Optional ? "optional" :
                d.type == addon::Dependency::Type::Embedded ? "embedded" : "broken");
            o.obj["projectId"] = json::makeString(d.projectId);
            o.obj["versionId"] = json::makeString(d.versionId);
            o.obj["name"] = json::makeString(addon::ResolveProjectName(d.projectId));
            arr.arr.push_back(o);
        }
        sink->post((int)launcher::Ev::InstallDone, "__addondeps__:" + json::dump(arr));
    }).detach();
}

void Ui::StartAddonChangelog() {
    auto sink = m_sink;
    std::string id = m_addonVersion.id;
    std::thread([sink, id]() {
        std::string err;
        std::string text = addon::GetChangelog(id, err);
        json::Value o = json::makeObject();
        o.obj["text"] = json::makeString(text);
        o.obj["err"] = json::makeString(err);
        sink->post((int)launcher::Ev::InstallDone, "__addonchangelog__:" + json::dump(o));
    }).detach();
}

void Ui::ShowAddonVersionDialog() {
    m_addonShowDlg = true;
    m_addonShowChangelog = false;
    m_addonFileName = mlc::Utf8ToW(m_addonVersion.fileName);
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    StartAddonDependencies();
}

void Ui::StartAddonInstall(bool saveAs) {
    mlc::Instance* in = CurInst();
    if (!in && !saveAs) { ShowToast(L"请先选择一个游戏实例", true); return; }
    std::wstring name = m_addonFileName;
    if (Widget* w = Find(L"ad.fname")) name = w->editText;
    if (name.empty()) { ShowToast(L"文件名不能为空", true); return; }
    if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
        ShowToast(L"文件名包含非法字符", true); return;
    }

    std::string dest;
    if (saveAs) {
        std::wstring picked;
        size_t dot = name.rfind(L'.');
        std::wstring ext = dot == std::wstring::npos ? L"" : name.substr(dot);
        if (!PickSaveFile(m_hwnd, name, ext, picked)) return;
        dest = mlc::WToUtf8(picked);
    } else {
        std::string dir = in->modsDir();
        dest = mlc::JoinPath(dir, mlc::WToUtf8(name));
        if (mlc::FileExists(dest)) { ShowToast(L"目标文件已存在，请换一个文件名", true); return; }
    }

    addon::Version v = m_addonVersion;
    m_addonShowDlg = false;
    m_addonShowChangelog = false;
    m_showDlDialog = true;
    m_dlDialogTitle = mlc::Utf8ToW(v.name);
    m_progress = 0.0f;
    m_installing = true;
    m_progressText = L"正在下载 " + m_dlDialogTitle;
    Rebuild();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    auto sink = m_sink;
    std::thread([sink, v, dest]() {
        std::string err;
        bool ok = addon::Download(v, dest, err, [sink](int64_t got, int64_t total) {
            json::Value p = json::makeObject();
            p.obj["phase"] = json::makeString("mod");
            p.obj["current"] = json::makeString("");
            p.obj["total"] = json::makeInt(total);
            p.obj["done"] = json::makeInt(got);
            sink->post((int)launcher::Ev::InstallProgress, json::dump(p));
            return true;
        });
        if (ok) {
            sink->post((int)launcher::Ev::InstallDone,
                       "__content__:" + mlc::WToUtf8(L"已下载 " + mlc::Utf8ToW(dest)));
        } else {
            sink->post((int)launcher::Ev::InstallError, err);
        }
    }).detach();
}

// ---------------------------------------------------------------
// 下拉选择
// ---------------------------------------------------------------
void Ui::HandleComboSelect(Widget& w) {
    mlc::Store& store = mlc::Store::I();
    launcher::App& app = launcher::App::Instance();

    if (w.id == L"ldlg.acc") {
        // 启动弹窗里直接切账号（和账号页「使用选中账号」等价）
        if (w.sel >= 0 && w.sel < (int)m_launchAccKeys.size()) {
            std::string key = m_launchAccKeys[w.sel];
            std::string err;
            if (!app.selectAccount(key, err)) ShowToast(L"切换账号失败：" + mlc::Utf8ToW(err), true);
        }
        return;
    }
    if (w.id == L"set.source") {
        store.spec().source = (w.sel == 2) ? "mojang" : (w.sel == 1 ? "bmclapi" : "auto");
        store.SaveSettings();
        app.loadConfig();
        ShowToast(L"下载源已切换");
    } else if (w.id == L"set.threads") {
        static const int kTh[] = { 8, 16, 24, 32 };
        store.spec().downloadThreads = kTh[std::clamp(w.sel, 0, 3)];
        store.SaveSettings();
        app.loadConfig();
        ShowToast(L"下载线程数：" + std::to_wstring(store.spec().downloadThreads));
    } else if (w.id == L"set.bgmode") {
        bg::Manager& bm = bg::Manager::Instance();
        int sel = w.sel;
        if (sel == 0) bm.SetMode(0);
        else if (sel >= 1 && sel <= 3) { bm.SetMode(1); bm.SetBuiltinIndex(sel - 1); }
        else if (sel == 4) bm.SetMode(2);
        else bm.SetMode(3);
    } else if (w.id == L"set.java") {
        auto javas = app.detectJava();
        if (w.sel == 0 || w.sel > (int)javas.size()) store.spec().javaPath.clear();
        else store.spec().javaPath = javas[w.sel - 1].path;
        store.SaveSettings();
        ShowToast(store.spec().javaPath.empty() ? L"已改为自动选择 Java"
                                               : L"已指定 Java：" + mlc::Utf8ToW(store.spec().javaPath));
    } else if (w.id == L"gs.folder") {
        m_gsFolderSel = w.sel;
        if (w.sel > 0 && w.sel <= (int)store.folders().size())
            store.SetCurrentFolder(store.folders()[w.sel - 1]);
        Rebuild();
    } else if (w.id == L"dl.inst") {
        // 内容下载：切到这一行对应的实例，文件随它的 mods/resourcepacks/shaderpacks/saves 走
        if (w.sel >= 0 && w.sel < (int)m_dlInstIds.size()) {
            store.SetCurrent(m_dlInstIds[w.sel]);
            m_modsLoaded = m_packsLoaded = m_shadersLoaded = m_worldsLoaded = false;
            m_cfStatus.clear();
            // 游戏版本过滤跟着换到新实例的原版版本号（HMCL 的下载页也是实例驱动）
            if (mlc::Instance* ni = CurInst()) m_addonMcVer = mlc::Utf8ToW(mcBaseVerOf(ni));
        }
        Rebuild();
    } else if (w.id == L"dl.folder") {
        m_dlFolderSel = w.sel;
        if (w.sel >= 0 && w.sel < (int)m_dlFolders.size())
            store.SetCurrentFolder(mlc::WToUtf8(m_dlFolders[w.sel]));
    } else if (w.id == L"ad.category") {
        // HMCL：任何筛选项变化都回到第一页并重新搜索
        m_addonCategorySel = w.sel;
        StartAddonSearch(0);
    } else if (w.id == L"ad.sort") {
        m_addonSort = w.sel;
        StartAddonSearch(0);
    } else if (w.id == L"ver.inst") {
        if (w.sel >= 0 && w.sel < (int)store.list().size()) {
            m_verInstId = mlc::Utf8ToW(store.list()[w.sel].id);
            store.SetCurrent(store.list()[w.sel].id);
            m_modsLoaded = m_packsLoaded = m_worldsLoaded = m_schemsLoaded = false;
            m_loaderVersions.clear();
            m_loaderStatus.clear();
        }
        Rebuild();
    } else if (w.id == L"dl.loader") {
        m_dlLoader = w.sel;
        m_loaderVersions.clear();
        if (w.sel > 0) {
            // 复用加载器版本获取
            int saved = m_loaderSel;
            m_loaderSel = w.sel - 1;
            RefreshLoaderVersions();
            m_loaderSel = saved;
        }
        Rebuild();
    } else if (w.id == L"dl.loaderver") {
        m_dlLoaderVer = (w.sel == 0) ? L"" : w.items[w.sel];
    } else if (w.id == L"loader.ver") {
        // 安装时读取
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------
// 卡片点击
// ---------------------------------------------------------------
void Ui::HandleGridClick(Widget& w, int idx, bool inButton) {
    mlc::Store& store = mlc::Store::I();
    if (idx < 0 || idx >= (int)w.items2.size()) return;
    const GridItem& it = w.items2[idx];
    if (it.section) return;

    if (w.id == L"addon.results") {
        if (idx >= 0 && idx < (int)m_addonHits.size()) {
            m_addonProject = m_addonHits[idx];
            m_addonDetail = true;
            m_addonShowDlg = m_addonShowChangelog = false;
            StartAddonVersions(m_addonProject.projectId);
        }
        return;
    }
    if (w.id == L"ad.versions") {
        std::string vid = mlc::WToUtf8(it.key);
        for (const auto& v : m_addonVersions) {
            if (v.id != vid) continue;
            m_addonVersion = v;
            ShowAddonVersionDialog();
            return;
        }
        return;
    }
    if (w.id == L"ad.deps") {
        std::string pid = mlc::WToUtf8(it.key);
        addon::Hit h;
        h.projectId = pid;
        h.slug = pid;
        h.title = mlc::WToUtf8(it.title);
        m_addonProject = h;
        m_addonShowDlg = m_addonShowChangelog = false;
        m_addonDetail = true;
        StartAddonVersions(pid);
        return;
    }

    if (w.id == L"gs.folders") {
        // HMCL 的目录抽屉：整行可点
        if (idx <= 0) {
            m_gsFolderSel = 0;
        } else {
            m_gsFolderSel = idx;
            if (idx - 1 < (int)store.folders().size())
                store.SetCurrentFolder(store.folders()[idx - 1]);
        }
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (w.id == L"gs.rows") {
        store.SetCurrent(mlc::WToUtf8(it.key));
        if (inButton) StartLaunch(it.key);
        else ShowToast(L"已选择 " + it.title);
        Rebuild();
        return;
    }
    if (w.id == L"ldlg.rows") {
        store.SetCurrent(mlc::WToUtf8(it.key));
        w.selIdx = idx;
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (w.id == L"home.grid" || w.id == L"gs.grid") {
        if (inButton) {
            store.SetCurrent(mlc::WToUtf8(it.key));
            if (w.id == L"home.grid") StartLaunch(it.key);
            else { ShowToast(L"已选择 " + it.title); Rebuild(); }
            return;
        }
        w.selIdx = idx;
        store.SetCurrent(mlc::WToUtf8(it.key));
        Rebuild();
        return;
    }
    if (w.id == L"dl.vers") {
        m_dlVersionId = it.key;
        // 名称为空 = 使用版本 id 作为默认名（用户改动后才按规则校验）
        m_dlName.clear();
        Rebuild();
        return;
    }
    if (w.id == L"dl.results") {
        w.selIdx = idx;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (w.id == L"cf.grid") {
        if (idx >= 0 && idx < (int)m_cfHits.size()) {
            if (inButton) StartCfDownload(m_cfHits[idx]);
            else { w.selIdx = idx; InvalidateRect(m_hwnd, nullptr, FALSE); }
        }
        return;
    }
    if (w.id == L"loader.grid") {
        m_loaderSel = idx;
        m_loaderVersions.clear();
        m_loaderStatus.clear();
        if (inButton) {
            // 按钮 = 安装 / 卸载（卡片右下角）
            if (it.actionDanger) HandleButton(L"loader.uninstall");
            else HandleButton(L"loader.install");
            return;
        }
        Rebuild();
        return;
    }
    if (w.id == L"dl.loadergrid") {
        m_dlLoader = idx;
        m_loaderVersions.clear();
        if (idx > 0) {
            int saved = m_loaderSel;
            m_loaderSel = idx - 1;          // 0 Fabric 1 Quilt 2 Forge 3 NeoForge
            RefreshLoaderVersions();
            m_loaderSel = saved;
        }
        Rebuild();
        return;
    }
    if (w.id == L"mod.grid") {
        if (inButton) {
            mlc::Instance* in = CurInst();
            if (!in) return;
            std::string key = mlc::WToUtf8(it.key);
            std::string err;
            if (content::Toggle(in->modsDir(), content::Kind::Mods, key, !it.on, err)) {
                ShowToast(it.on ? (L"已停用 " + it.title) : (L"已启用 " + it.title));
                m_modsLoaded = false;
                Rebuild();
            } else ShowToast(L"操作失败：" + mlc::Utf8ToW(err), true);
        }
        return;
    }
    if (w.id == L"pack.grid") {
        if (inButton) {
            mlc::Instance* in = CurInst();
            if (!in) return;
            std::string key = mlc::WToUtf8(it.key);
            std::string err;
            if (content::Toggle(in->resourcePacksDir(), content::Kind::ResourcePacks, key,
                                !it.on, err)) {
                ShowToast(it.on ? (L"已启用 " + it.title) : (L"已停用 " + it.title));
                m_packsLoaded = false;
                Rebuild();
            } else ShowToast(L"操作失败：" + mlc::Utf8ToW(err), true);
        }
        return;
    }
    if (w.id == L"world.grid") {
        mlc::Instance* in = CurInst();
        if (!in) return;
        if (inButton) {
            std::string dir = mlc::JoinPath(in->savesDir(), mlc::WToUtf8(it.key));
            std::string out, err;
            if (content::BackupWorld(dir, in->id, out, err))
                ShowToast(L"已备份到 " + mlc::Utf8ToW(out));
            else ShowToast(L"备份失败：" + mlc::Utf8ToW(err), true);
        } else {
            w.selIdx = idx;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }
    if (w.id == L"sch.grid") {
        if (inButton) {
            mlc::Instance* in = CurInst();
            if (!in) return;
            std::string err;
            if (content::Remove(in->schematicsDir(), content::Kind::Schematics,
                                mlc::WToUtf8(it.key), err)) {
                ShowToast(L"已删除 " + it.title);
                m_schemsLoaded = false;
                Rebuild();
            } else ShowToast(L"删除失败：" + mlc::Utf8ToW(err), true);
        }
        return;
    }
}

// ---------------------------------------------------------------
// 按钮分发
// ---------------------------------------------------------------
void Ui::HandleButton(const std::wstring& id) {
    launcher::App& app = launcher::App::Instance();
    mlc::Store& store = mlc::Store::I();
    bg::Manager& bm = bg::Manager::Instance();
    mlc::Instance* in = CurInst();

    // ---------- 首页 ----------
    if (id == L"home.launch") {
        if (app.gameRunning()) { app.stopGame(); return; }
        if (m_launching) { ShowToast(L"游戏正在启动中，请稍候…"); return; }
        if (store.list().empty()) {
            ShowToast(L"还没有游戏：先去「下载」安装一个版本", true);
            SetPage(Download);
            return;
        }
        // 照 HMCL：一个启动按钮 → 弹窗里选实例 / 账号，再启动
        m_showLaunchDlg = true;
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id == L"ldlg.cancel") {
        m_showLaunchDlg = false;
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id == L"ldlg.start") {
        if (app.gameRunning()) { app.stopGame(); m_showLaunchDlg = false; Rebuild(); return; }
        mlc::Instance* cur = store.Current();
        if (!cur) { ShowToast(L"请先选择一个游戏", true); return; }
        std::string iid = cur->id;
        m_showLaunchDlg = false;
        StartLaunch(mlc::Utf8ToW(iid));
        return;
    }
    if (id == L"home.verset") { SetPage(Versions); m_verTab = TabVerSet; Rebuild(); return; }
    if (id == L"home.gosel") { SetPage(GameSelect); return; }
    if (id == L"home.refresh") { Rebuild(); InvalidateRect(m_hwnd, nullptr, FALSE); return; }

    // ---------- 版本管理：标签 ----------
    for (int i = 0; i < 6; ++i) {
        if (id == (std::wstring(L"ver.tab") + std::to_wstring(i))) {
            m_verTab = i;
            m_scrollY = 0;
            Rebuild();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
    if (id == L"ver.open") {
        if (in) content::OpenFolder(in->dataDir());
        return;
    }

    // ---------- 模组 ----------
    if (id == L"mod.refresh") {
        m_modsLoaded = false;
        RefreshMods(true);
        ShowToast(L"已刷新模组列表");
        Rebuild();
        return;
    }
    if (id == L"mod.opendir") {
        if (in) content::OpenFolder(in->modsDir());
        return;
    }
    if (id == L"mod.detect") {
        if (!in) return;
        auto missing = modmgr::DetectMissing(mlc::Utf8ToW(in->modsDir()));
        if (missing.empty()) ShowToast(L"未发现缺失的前置依赖");
        else {
            std::wstring t = L"缺失前置：" ;
            for (size_t i = 0; i < missing.size() && i < 6; ++i) {
                if (i) t += L", ";
                t += mlc::Utf8ToW(missing[i]);
            }
            ShowToast(t, true);
        }
        return;
    }
    if (id == L"mod.fix") {
        if (!in) return;
        std::string modsDir = in->modsDir();
        std::string ver = in->versionId;
        std::string ld = in->loader.empty() ? "fabric" : in->loader;
        ShowToast(L"正在补充缺失前置…");
        auto sink = m_sink;
        std::thread([sink, modsDir, ver, ld]() {
            auto missing = modmgr::DetectMissing(mlc::Utf8ToW(modsDir));
            int ok = 0, fail = 0;
            for (auto& mid : missing) {
                // DetectMissing 返回的是 mod id，需要先在 Modrinth 搜到项目再下载
                std::string serr;
                auto hits = content::Search(mid, "mod", ld, ver, 5, 0, serr);
                if (hits.empty()) { ++fail; continue; }
                std::string out, derr;
                if (content::Download(hits[0].slug, content::Kind::Mods, modsDir, ld, ver,
                                      out, derr)) ++ok;
                else ++fail;
            }
            std::wstring msg = L"前置补充完成：成功 " + std::to_wstring(ok) +
                               L"，失败 " + std::to_wstring(fail);
            sink->post((int)launcher::Ev::InstallDone,
                                      "__content__:" + mlc::WToUtf8(msg));
        }).detach();
        return;
    }
    if (id == L"mod.godl") {
        if (m_dlCat != 1) {
            m_dlCat = 1;
            m_dlSearch.clear();
            m_addonPage = 0;
            m_addonDetail = m_addonShowDlg = m_addonShowChangelog = false;
            if (in) m_addonMcVer = mlc::Utf8ToW(mcBaseVerOf(in));
            if (m_addonCategoryNames.empty()) {
                m_addonCategoryNames.push_back(L"全部");
                StartAddonCategories();
            }
            StartAddonSearch(0);
        }
        SetPage(Download);
        return;
    }

    // ---------- 加载器 ----------
    if (id == L"loader.fetch") { RefreshLoaderVersions(); return; }
    if (id == L"loader.install") {
        if (!in) return;
        std::string folder = in->folder;
        // 同上：安装时必须用原版版本号，否则 Fabric/Quilt 的 profile 与 Forge 的版本列表都会 404
        std::string mc = mc::baseVersionId(mlc::JoinPath(folder, "versions"), in->versionId);
        if (mc.empty()) mc = in->versionId;
        std::string lv;
        if (Widget* w = Find(L"loader.ver")) {
            if (w->sel > 0 && w->sel < (int)w->items.size()) lv = mlc::WToUtf8(w->items[w->sel]);
        }
        int type = m_loaderSel;
        if (type < 0 || type > 3) type = 0;
        // 没选具体版本 = 自动取最新的
        // Fabric/Quilt：可以留空，installer 内部会去 meta 取最新稳定版
        // Forge/NeoForge：必须先有列表（否则不知道完整的版本号）
        if (lv.empty() && !m_loaderVersions.empty()) lv = mlc::WToUtf8(m_loaderVersions.front());
        if (lv.empty() && type >= 2) {
            RefreshLoaderVersions();
            ShowToast(L"正在获取 " + std::wstring(type == 2 ? L"Forge" : L"NeoForge") +
                      L" 版本列表，稍后再点一次「安装」");
            return;
        }
        static const wchar_t* kLoaderNames[] = { L"Fabric", L"Quilt", L"Forge", L"NeoForge" };
        m_showDlDialog = true;
        m_dlDialogTitle = std::wstring(kLoaderNames[type]) + L" " +
                          (lv.empty() ? L"（最新稳定版）" : mlc::Utf8ToW(lv));
        m_progress = 0;
        m_installing = true;
        if (type == 0) app.startInstallLoader(folder, mc, lv, false);
        else if (type == 1) app.startInstallLoader(folder, mc, lv, true);
        else if (type == 2) app.startInstallForge(folder, lv);
        else app.startInstallNeoForge(folder, lv);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id == L"loader.uninstall") {
        if (!in) return;
        std::string err;
        if (launcher::App::removeLoaderVersion(in->folder, in->versionId, err)) {
            ShowToast(L"已卸载加载器版本 " + mlc::Utf8ToW(in->versionId));
            m_modsLoaded = m_packsLoaded = false;
            Rebuild();
        } else {
            ShowToast(L"卸载失败：" + mlc::Utf8ToW(err), true);
        }
        return;
    }

    // ---------- 版本设置 ----------
    if (id == L"vs.save") {
        if (!in) return;
        mlc::InstanceSettings s;
        if (Widget* w = Find(L"vs.java")) s.javaPath = mlc::WToUtf8(w->editText);
        if (Widget* w = Find(L"vs.mem")) s.maxMemoryMB = parseInt(w->editText, 0);
        if (Widget* w = Find(L"vs.width")) s.width = parseInt(w->editText, 0);
        if (Widget* w = Find(L"vs.height")) s.height = parseInt(w->editText, 0);
        if (Widget* w = Find(L"vs.fullscreen")) s.fullscreen = w->checked;
        if (Widget* w = Find(L"vs.isolate")) s.isolate = w->sel - 1;
        if (Widget* w = Find(L"vs.check")) s.checkIntegrity = w->sel - 1;
        if (Widget* w = Find(L"vs.nick")) s.nickname = mlc::WToUtf8(w->editText);
        if (Widget* w = Find(L"vs.server")) {
            std::string a = mlc::WToUtf8(w->editText);
            if (!a.empty()) {
                int port = 25565;
                size_t p = a.rfind(':');
                if (p != std::string::npos && p + 1 < a.size()) {
                    int v = atoi(a.substr(p + 1).c_str());
                    if (v > 0 && v < 65536) { port = v; a = a.substr(0, p); }
                }
                s.server = a;
                s.serverPort = port;
            }
        }
        if (Widget* w = Find(L"vs.jvm")) s.jvmArgs = splitArgs(w->editText);
        if (Widget* w = Find(L"vs.game")) s.gameArgs = splitArgs(w->editText);

        in->settings = s;
        store.SaveInstanceSettings(*in);
        if (Widget* w = Find(L"vs.status"))
            w->text = L"已保存到 Data\\mlc\\games\\" + mlc::Utf8ToW(in->id) + L"\\settings.json";
        ShowToast(L"单局设置已保存");
        return;
    }
    if (id == L"vs.reset") {
        if (!in) return;
        in->settings = mlc::InstanceSettings{};
        store.SaveInstanceSettings(*in);
        ShowToast(L"已重置为全局默认");
        Rebuild();
        return;
    }
    if (id == L"vs.opendir") {
        if (in) content::OpenFolder(in->dataDir());
        return;
    }
    if (id == L"vs.checknow") { StartIntegrityCheck(true); return; }

    // ---------- 资源包 / 世界 / 原理图 ----------
    if (id == L"pack.refresh") { RefreshPacks(true); ShowToast(L"已刷新"); Rebuild(); return; }
    if (id == L"pack.up" || id == L"pack.down") {
        mlc::Instance* in = CurInst();
        if (!in) return;
        Widget* w = Find(L"pack.grid");
        if (!w || w->selIdx < 0 || w->selIdx >= (int)m_packs.size()) {
            ShowToast(L"请先选中一个资源包", true);
            return;
        }
        std::string err;
        if (content::Move(in->resourcePacksDir(), content::Kind::ResourcePacks,
                          m_packs[w->selIdx].key, id == L"pack.up", err)) {
            RefreshPacks(true);
            Rebuild();
        } else {
            ShowToast(mlc::Utf8ToW(err), true);
        }
        return;
    }
    if (id == L"pack.open") { if (in) content::OpenFolder(in->resourcePacksDir()); return; }
    if (id == L"pack.import") {
        if (!in) return;
        std::wstring f;
        if (PickFile(m_hwnd, L"资源包\0*.zip\0所有文件\0*.*\0", f)) {
            std::string key, err;
            if (content::Import(in->resourcePacksDir(), content::Kind::ResourcePacks, f, key, err)) {
                ShowToast(L"已导入 " + mlc::Utf8ToW(key));
                m_packsLoaded = false;
                Rebuild();
            } else ShowToast(L"导入失败：" + mlc::Utf8ToW(err), true);
        }
        return;
    }
    if (id == L"pack.godl") { m_dlCat = 2; SetPage(Download); return; }

    // 光影包（HMCL 里也是只浏览目录，不由启动器做启停）
    if (id == L"shd.refresh") { RefreshShaders(true); ShowToast(L"已刷新光影包列表"); Rebuild(); return; }
    if (id == L"shd.open") { if (in) content::OpenFolder(in->shaderPacksDir()); return; }
    if (id == L"shd.import") {
        if (!in) return;
        std::wstring f;
        if (!PickFile(m_hwnd, L"光影包\0*.zip\0所有文件\0*.*\0", f)) return;
        std::string key, err;
        if (content::Import(in->shaderPacksDir(), content::Kind::Shaders, f, key, err)) {
            ShowToast(L"已导入光影包 " + mlc::Utf8ToW(key));
            m_shadersLoaded = false;
            Rebuild();
        } else ShowToast(L"导入失败：" + mlc::Utf8ToW(err), true);
        return;
    }

    if (id == L"world.refresh" || id == L"dl.worldrefresh") {
        RefreshWorlds(true);
        ShowToast(L"已刷新世界列表");
        Rebuild();
        return;
    }
    if (id == L"world.open") { if (in) content::OpenFolder(in->savesDir()); return; }
    if (id == L"world.import" || id == L"dl.import") {
        if (!in) return;
        std::wstring f;
        const wchar_t* filter = (m_page == Download && m_dlCat == 4)
            ? L"世界压缩包\0*.zip\0所有文件\0*.*\0"
            : L"全部支持的文件\0*.zip;*.jar;*.litematic;*.schematic;*.schem\0所有文件\0*.*\0";
        if (!PickFile(m_hwnd, filter, f)) return;
        if (m_page == Download) {
            std::string key, err;
            if (content::Import(in->savesDir(), content::Kind::Worlds, f, key, err)) {
                ShowToast(L"已导入世界 " + mlc::Utf8ToW(key));
                m_worldsLoaded = false;
                Rebuild();
            } else ShowToast(L"导入失败：" + mlc::Utf8ToW(err), true);
            return;
        }
        // 版本管理：按标签决定目标
        content::Kind kind = content::Kind::ResourcePacks;
        std::string dir = in->resourcePacksDir();
        if (m_verTab == TabWorlds) { kind = content::Kind::Worlds; dir = in->savesDir(); }
        else if (m_verTab == TabShaders) { kind = content::Kind::Shaders; dir = in->shaderPacksDir(); }
        else if (m_verTab == TabSchematics) { kind = content::Kind::Schematics; dir = in->schematicsDir(); }
        std::string key, err;
        if (content::Import(dir, kind, f, key, err)) {
            ShowToast(L"已导入 " + mlc::Utf8ToW(key));
            m_packsLoaded = m_worldsLoaded = m_schemsLoaded = m_shadersLoaded = false;
            Rebuild();
        } else ShowToast(L"导入失败：" + mlc::Utf8ToW(err), true);
        return;
    }
    if (id == L"world.backup") {
        if (!in) return;
        if (Widget* w = Find(L"world.grid")) {
            if (w->selIdx >= 0 && w->selIdx < (int)m_worlds.size()) {
                std::string dir = mlc::JoinPath(in->savesDir(), m_worlds[w->selIdx].key);
                std::string out, err;
                if (content::BackupWorld(dir, in->id, out, err))
                    ShowToast(L"已备份到 " + mlc::Utf8ToW(out));
                else ShowToast(L"备份失败：" + mlc::Utf8ToW(err), true);
            } else ShowToast(L"请先选中一个世界");
        }
        return;
    }
    if (id == L"world.del") {
        if (!in) return;
        if (Widget* w = Find(L"world.grid")) {
            if (w->selIdx >= 0 && w->selIdx < (int)m_worlds.size()) {
                std::string err;
                std::string key = m_worlds[w->selIdx].key;
                if (content::Remove(in->savesDir(), content::Kind::Worlds, key, err)) {
                    ShowToast(L"已删除世界 " + mlc::Utf8ToW(key));
                    m_worldsLoaded = false;
                    Rebuild();
                } else ShowToast(L"删除失败：" + mlc::Utf8ToW(err), true);
            } else ShowToast(L"请先选中一个世界");
        }
        return;
    }

    // ---------- 世界：CurseForge 在线 ----------
    if (id == L"world.src0") { m_worldSource = 0; Rebuild(); return; }
    if (id == L"world.src1") {
        m_worldSource = 1;
        RefreshWorlds(true);
        Rebuild();
        return;
    }
    if (id == L"cf.search") {
        if (Widget* w = Find(L"dl.csearch")) m_dlSearch = w->editText;
        StartCfSearch(0);
        return;
    }
    if (id == L"cf.prev") { StartCfSearch(m_cfPage - 1); return; }
    if (id == L"cf.next") { StartCfSearch(m_cfPage + 1); return; }
    if (id == L"cf.download") {
        if (Widget* g = Find(L"cf.grid")) {
            if (g->selIdx >= 0 && g->selIdx < (int)m_cfHits.size())
                StartCfDownload(m_cfHits[g->selIdx]);
            else ShowToast(L"请先选中一个世界", true);
        }
        return;
    }

    if (id == L"sch.refresh") { RefreshSchematics(true); ShowToast(L"已刷新"); Rebuild(); return; }
    if (id == L"sch.open") { if (in) content::OpenFolder(in->schematicsDir()); return; }
    if (id == L"sch.import") {
        if (!in) return;
        std::wstring f;
        if (PickFile(m_hwnd, L"原理图\0*.schematic;*.litematic;*.schem;*.nbt\0所有文件\0*.*\0", f)) {
            std::string key, err;
            if (content::Import(in->schematicsDir(), content::Kind::Schematics, f, key, err)) {
                ShowToast(L"已导入 " + mlc::Utf8ToW(key));
                m_schemsLoaded = false;
                Rebuild();
            } else ShowToast(L"导入失败：" + mlc::Utf8ToW(err), true);
        }
        return;
    }
    if (id == L"sch.del") {
        if (!in) return;
        if (Widget* w = Find(L"sch.grid")) {
            if (w->selIdx >= 0 && w->selIdx < (int)m_schems.size()) {
                std::string err;
                if (content::Remove(in->schematicsDir(), content::Kind::Schematics,
                                    m_schems[w->selIdx].key, err)) {
                    ShowToast(L"已删除");
                    m_schemsLoaded = false;
                    Rebuild();
                } else ShowToast(L"删除失败：" + mlc::Utf8ToW(err), true);
            } else ShowToast(L"请先选中一个原理图");
        }
        return;
    }

    // ---------- 版本选择 ----------
    if (id == L"gs.add") {
        std::wstring folder;
        if (!PickFolder(m_hwnd, folder)) return;
        std::string err;
        int added = store.AddFolder(folder, err);   // 空文件夹也会登记为下载目标
        m_dlFolders.clear();
        m_gsFolderSel = 0;
        if (added > 0)
            ShowToast(L"已添加文件夹，并导入 " + std::to_wstring(added) + L" 个已有游戏");
        else
            ShowToast(L"已添加文件夹（可到「下载」把游戏装进这里）");
        Rebuild();
        return;
    }
    if (id == L"gs.removefolder") {
        std::string cur = (m_gsFolderSel > 0 && m_gsFolderSel <= (int)store.folders().size())
                          ? store.folders()[m_gsFolderSel - 1] : store.CurrentFolder();
        if (cur.empty()) { ShowToast(L"还没有添加文件夹", true); return; }
        std::string err;
        if (store.RemoveFolderPath(cur, err)) {
            m_gsFolderSel = 0;
            m_dlFolders.clear();
            ShowToast(L"已移除文件夹（文件未删除）");
            Rebuild();
        } else {
            ShowToast(L"移除失败：" + mlc::Utf8ToW(err), true);
        }
        return;
    }
    if (id == L"gs.openfolder") {
        std::string cur = (m_gsFolderSel > 0 && m_gsFolderSel <= (int)store.folders().size())
                          ? store.folders()[m_gsFolderSel - 1] : store.CurrentFolder();
        if (cur.empty()) { ShowToast(L"还没有添加文件夹", true); return; }
        content::OpenFolder(cur);
        return;
    }
    if (id == L"gs.del") {
        mlc::Instance* cur = store.Current();
        if (!cur) { ShowToast(L"请先选中一个游戏", true); return; }
        std::string iid = cur->id, nm = cur->name;
        store.RemoveInstance(iid);
        ShowToast(L"已从列表移除（文件保留，下次扫描会重新出现）：" + mlc::Utf8ToW(nm));
        Rebuild();
        return;
    }
    if (id == L"gs.delfile") {
        mlc::Instance* cur = store.Current();
        if (!cur) { ShowToast(L"请先选中一个游戏", true); return; }
        std::string iid = cur->id, vname = cur->versionId;
        if (MessageBoxW(m_hwnd,
                (L"确定删除版本目录？\n\n" + mlc::Utf8ToW(vname) +
                 L"\n\n只删 versions 下这个版本目录，存档不会被删除。").c_str(),
                L"连同文件删除", MB_YESNO | MB_ICONWARNING) != IDYES) return;
        std::string err;
        if (store.DeleteInstanceFiles(iid, err)) {
            ShowToast(L"已删除版本文件 " + mlc::Utf8ToW(vname));
            Rebuild();
        } else {
            ShowToast(L"删除失败：" + mlc::Utf8ToW(err), true);
        }
        return;
    }
    if (id == L"gs.launch") {
        mlc::Instance* cur = store.Current();
        if (!cur) { ShowToast(L"请先选中一个游戏", true); return; }
        StartLaunch(mlc::Utf8ToW(cur->id));
        return;
    }
    if (id == L"gs.opendir") {
        mlc::Instance* cur = store.Current();
        if (!cur) { ShowToast(L"请先选中一个游戏", true); return; }
        content::OpenFolder(cur->dataDir());
        return;
    }
    if (id == L"gs.searchbtn") {
        if (Widget* w = Find(L"gs.search")) m_gsSearch = w->editText;
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id == L"gs.showparents") {
        m_showHiddenVersions = !m_showHiddenVersions;
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id == L"gs.opendata") {
        content::OpenFolder(mlc::WToUtf8(mlc::BaseDir()));
        return;
    }
    if (id == L"gs.checkall") {
        StartIntegrityCheck(true);
        return;
    }

    // ---------- 下载 ----------
    if (id.rfind(L"dl.cat", 0) == 0) {
        int cat = atoi(mlc::WToUtf8(id.substr(6)).c_str());
        if (cat != m_dlCat) {
            m_dlCat = cat;
            m_hits.clear();
            m_addonDetail = m_addonShowDlg = m_addonShowChangelog = false;
            if (cat == 1) {
                // HMCL：进入模组下载页时自动加载第一页（空查询，按相关度）
                m_dlSearch.clear();
                m_addonPage = 0;
                m_addonHits.clear();
                if (mlc::Instance* cur = CurInst()) m_addonMcVer = mlc::Utf8ToW(mcBaseVerOf(cur));
                if (m_addonCategoryNames.empty()) {
                    m_addonCategoryNames.push_back(L"全部");
                    StartAddonCategories();
                }
                StartAddonSearch(0);
                
                // 不要直接 return，让后面的 m_scrollY = 0; Rebuild(); 执行
                m_scrollY = 0;
                Rebuild();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            // 和世界的「首次进入自动拉一页」一致：资源包/光影也直接显示全部热门
            if (cat >= 2 && cat <= 3) {
                m_dlSearch.clear();
                m_dlOffset = 0;
                StartDownloadSearch();
                return;
            }
            if (cat == 4 && m_worldSource == 0 && m_cfHits.empty() && !m_cfSearching) {
                // 首次进入世界在线页：自动拉一页热门
                m_cfSearching = false;
                StartCfSearch(0);
                return;
            }
            if (cat == 4) RefreshWorlds(false);
        }
        m_scrollY = 0;
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id.rfind(L"dl.ch", 0) == 0) {
        m_dlChannel = atoi(mlc::WToUtf8(id.substr(5)).c_str());
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id == L"dl.refresh") {
        m_progressText = L"正在拉取版本清单…";
        Rebuild();
        auto sink = m_sink;
        std::thread([sink]() {
            std::string err;
            bool ok = launcher::App::Instance().refreshRemote(err);
            sink->post(ok ? (int)launcher::Ev::InstallDone
                          : (int)launcher::Ev::InstallError,
                       ok ? std::string("__manifest__") : err);
        }).detach();
        return;
    }
    if (id == L"dl.start") {
        if (m_dlVersionId.empty()) { ShowToast(L"请先在左侧选一个版本", true); return; }
        // 名称：没改过就留空（= 直接用版本号 1.20.1 原样），改过才按规则校验/清洗
        std::wstring name;
        if (!m_dlName.empty()) {
            name = mlc::IsValidGameName(m_dlName) ? m_dlName : mlc::SanitizeGameName(m_dlName);
        }

        std::wstring folder;
        if (Widget* w = Find(L"dl.folder")) {
            if (w->sel >= 0 && w->sel < (int)w->items.size()) folder = w->items[w->sel];
        }
        if (folder.empty()) { ShowToast(L"请选择安装目录（可在「版本选择」添加文件夹）", true); return; }

        std::string lver;
        if (Widget* w = Find(L"dl.loaderver")) {
            if (w->sel > 0 && w->sel < (int)w->items.size()) lver = mlc::WToUtf8(w->items[w->sel]);
        }
        m_dlDialogTitle = m_dlVersionId;
        m_showDlDialog = true;
        m_progress = 0.0f;
        m_installing = true;
        m_progressText = L"准备下载 " + m_dlVersionId;
        app.startInstall(mlc::WToUtf8(folder), mlc::WToUtf8(m_dlVersionId), m_dlLoader, lver,
                         mlc::WToUtf8(name));
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    // ---------- 下载：模组（HMCL 式） ----------
    if (id == L"addon.search") {
        if (Widget* w = Find(L"dl.csearch")) m_dlSearch = w->editText;
        if (Widget* w = Find(L"dl.mcver")) m_addonMcVer = w->editText;
        if (Widget* w = Find(L"ad.sort")) m_addonSort = w->sel;
        if (Widget* w = Find(L"ad.category")) m_addonCategorySel = w->sel;
        StartAddonSearch(0);
        return;
    }
    if (id == L"addon.first") { StartAddonSearch(0); return; }
    if (id == L"addon.prev") { StartAddonSearch(m_addonPage - 1); return; }
    if (id == L"addon.next") { StartAddonSearch(m_addonPage + 1); return; }
    if (id == L"addon.last") { StartAddonSearch(m_addonPages - 1); return; }
    if (id == L"ad.back") {
        m_addonDetail = false;
        m_addonShowDlg = m_addonShowChangelog = false;
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id == L"ad.retry") { StartAddonVersions(m_addonProject.projectId); return; }
    if (id == L"ad.changelog") {
        StartAddonChangelog();
        return;
    }
    if (id == L"ad.page") {
        std::string url = m_addonProject.pageUrl;
        if (url.empty()) url = std::string("https://modrinth.com/mod/") + m_addonProject.projectId;
        ShellExecuteW(nullptr, L"open", mlc::Utf8ToW(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (id == L"ad.install") { StartAddonInstall(false); return; }
    if (id == L"ad.saveas") { StartAddonInstall(true); return; }
    if (id == L"ad.cancel") {
        m_addonShowDlg = false;
        m_addonShowChangelog = false;
        m_addonDetail = true;
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id == L"ac.close") {
        m_addonShowChangelog = false;
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (id == L"dl.csearchbtn") {
        if (Widget* w = Find(L"dl.csearch")) m_dlSearch = w->editText;
        m_dlOffset = 0;                       // 换关键词回到第一页
        StartDownloadSearch();
        return;
    }
    if (id == L"dl.prev" || id == L"dl.next") {
        m_dlOffset = std::max(0, m_dlOffset + (id == L"dl.next" ? 20 : -20));
        StartDownloadSearch();
        return;
    }
    if (id == L"dl.cdownload") {
        if (!in) { ShowToast(L"请先添加游戏", true); return; }
        Widget* g = Find(L"dl.results");
        if (!g || g->selIdx < 0 || g->selIdx >= (int)m_hits.size()) {
            ShowToast(L"请先选中一个结果", true);
            return;
        }
        content::Hit h = m_hits[g->selIdx];
        content::Kind kind = m_dlCat == 1 ? content::Kind::Mods
                           : (m_dlCat == 2 ? content::Kind::ResourcePacks : content::Kind::Shaders);
        std::string dir = m_dlCat == 1 ? in->modsDir()
                        : (m_dlCat == 2 ? in->resourcePacksDir() : in->shaderPacksDir());
        std::string loader = in->loader.empty() ? "fabric" : in->loader;
        if (m_dlCat != 1) loader.clear();
        std::string ver = mcBaseVerOf(in);
        m_progressText = L"正在下载 " + mlc::Utf8ToW(h.title) + L"…";
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        auto sink = m_sink;
        std::thread([sink, h, kind, dir, loader, ver]() {
            std::string out, err;
            bool ok = content::Download(h.slug, kind, dir, loader, ver, out, err);
            std::wstring msg = ok ? (L"已下载 " + mlc::Utf8ToW(out)) : (L"下载失败：" + mlc::Utf8ToW(err));
            if (ok) sink->post((int)launcher::Ev::InstallDone,
                               "__content__:" + mlc::WToUtf8(msg));
            else sink->post((int)launcher::Ev::InstallError, err);
        }).detach();
        return;
    }
    if (id == L"dl.copenfolder") {
        if (!in) return;
        content::OpenFolder(m_dlCat == 1 ? in->modsDir()
                          : (m_dlCat == 2 ? in->resourcePacksDir() : in->shaderPacksDir()));
        return;
    }

    // ---------- 账号 ----------
    if (id == L"acc.use") {
        Widget* w = Find(L"acc.list");
        auto& list = store.accounts();
        if (!w || w->selIdx < 0 || w->selIdx >= (int)list.size()) {
            ShowToast(L"请先选中一个账号", true);
            return;
        }
        const auto& a = list[w->selIdx];
        std::string key = a.uuid.empty() ? a.name : a.uuid;
        std::string name = a.name;
        std::string err;
        m_accSel = w->selIdx;
        if (app.selectAccount(key, err)) {
            ShowToast(L"已切换账号：" + mlc::Utf8ToW(name));
            Rebuild();
        } else {
            ShowToast(L"切换失败：" + mlc::Utf8ToW(err), true);
        }
        return;
    }
    if (id == L"acc.del") {
        Widget* w = Find(L"acc.list");
        auto& list = store.accounts();
        if (!w || w->selIdx < 0 || w->selIdx >= (int)list.size()) {
            ShowToast(L"请先选中一个账号", true);
            return;
        }
        std::string key = list[w->selIdx].uuid.empty() ? list[w->selIdx].name
                                                       : list[w->selIdx].uuid;
        std::string nm = list[w->selIdx].name;
        store.RemoveAccount(key);
        app.loadAccountFromStore();
        m_accSel = -1;                       // 行号变了，交回「当前账号」
        ShowToast(L"已删除账号：" + mlc::Utf8ToW(nm));
        Rebuild();
        return;
    }
    // ---- 账号存储位置：单个账号在「便携 / 全局」之间迁移（照 HMCL）----
    if (id == L"acc.move") {
        Widget* w = Find(L"acc.list");
        auto& list = store.accounts();
        if (!w || w->selIdx < 0 || w->selIdx >= (int)list.size()) {
            ShowToast(L"请先选中一个账号", true);
            return;
        }
        mlc::AccountRec& rec = list[w->selIdx];
        std::string key = rec.uuid.empty() ? rec.name : rec.uuid;
        std::string name = rec.name;
        bool toPortable = !rec.portable;
        std::string err;
        if (store.MoveAccount(key, toPortable, err)) {
            skin::Clear();
            ShowToast((toPortable ? L"已移到便携：账号和皮肤跟随启动器目录（" : L"已移到全局：账号和皮肤存到 %APPDATA%\\YXGLauncher（") +
                      mlc::Utf8ToW(name) + L"）");
            // 当前账号若就是这个，App 里的缓存也要跟着刷
            if (store.AccountKey() == key) app.loadAccountFromStore();
            Rebuild();
        } else {
            ShowToast(L"迁移失败：" + mlc::Utf8ToW(err), true);
        }
        return;
    }

    // ---- 皮肤：选择 / 引用路径 / 移除 / 模型 / 上传 / 同步（作用于列表里选中的账号）----
    if (id == L"acc.skinpick" || id == L"acc.skinpath" || id == L"acc.skindel" ||
        id == L"acc.capepick" || id == L"acc.model.classic" || id == L"acc.model.slim" ||
        id == L"acc.skinup" || id == L"acc.skinpull") {
        Widget* w = Find(L"acc.list");
        auto& list = store.accounts();
        if (!w || w->selIdx < 0 || w->selIdx >= (int)list.size()) {
            ShowToast(L"请先在上面的列表里选中一个账号", true);
            return;
        }
        mlc::AccountRec& rec = list[w->selIdx];
        std::string key = rec.uuid.empty() ? rec.name : rec.uuid;
        auto safeName = [](const std::string& s) {
            std::string o = s;
            for (char& c : o) {
                if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
                    c == '"' || c == '<' || c == '>' || c == '|') c = '_';
            }
            return o;
        };
        std::wstring keyW = mlc::Utf8ToW(safeName(key));

        if (id == L"acc.skindel") {
            rec.skin.clear();
            rec.skinPath.clear();
            rec.cape.clear();
            rec.capePath.clear();
            store.SaveAccounts();
            skin::Clear();
            ShowToast(L"已移除皮肤和披风");
            Rebuild();
            return;
        }
        if (id == L"acc.model.classic" || id == L"acc.model.slim") {
            rec.skinModel = (id == L"acc.model.slim") ? "slim" : "classic";
            store.SaveAccounts();
            skin::Clear();
            ShowToast(rec.skinModel == "slim" ? L"皮肤模型：纤细（Alex，3px 手臂）"
                                              : L"皮肤模型：经典（Steve，4px 手臂）");
            Rebuild();
            return;
        }
        if (id == L"acc.skinup") {
            if (rec.type != "msa") {
                ShowToast(L"只有微软正版账号能把皮肤上传到服务器", true);
                return;
            }
            std::wstring src = store.EffectiveSkin(rec);
            if (src.empty()) {
                ShowToast(L"这个账号还没有皮肤：先点「选择皮肤」选一张 PNG", true);
                return;
            }
            bool slim = store.IsSlim(rec);
            std::string token = rec.accessToken;
            int64_t exp = rec.expiresAt;
            // 令牌过期/快过期且是当前账号：先自动续期，避免必然 401
            if ((token.empty() || exp == 0 || exp < (int64_t)time(nullptr) + 60) &&
                key == app.accountKey()) {
                m_skinMsg = L"正在续期登录令牌…";
                Rebuild();
                std::string rerr;
                if (app.refreshAccount(rerr)) {
                    if (mlc::AccountRec* r2 = store.FindAccount(key)) token = r2->accessToken;
                }
            }
            if (token.empty()) {
                ShowToast(L"没有可用的 Minecraft 令牌：请重新点「微软登录」", true);
                return;
            }
            std::wstring path = src;
            m_skinBusy = true;
            m_skinMsg = L"正在上传皮肤到 Mojang…";
            Rebuild();
            auto sink = m_sink;
            std::thread([sink, token, path, slim, key]() {
                std::string err;
                bool ok = skin::UploadToMojang(token, path, slim, err);
                sink->post((int)launcher::Ev::Info,
                           "_skinup:" + key + "|" + (ok ? "1" : "0") + "|" + err);
            }).detach();
            return;
        }
        if (id == L"acc.skinpull") {
            if (rec.type != "msa" || rec.uuid.empty()) {
                ShowToast(L"只有微软正版账号能同步服务器上的皮肤", true);
                return;
            }
            m_skinAutoKey = keyW;          // 手动同步后不再自动触发
            if (m_sink) m_sink->post((int)launcher::Ev::Info, "__skinfetch__:" + key);
            return;
        }

        // 以下都需要选文件
        std::wstring path;
        bool isCape = (id == L"acc.capepick");
        if (!PickFile(m_hwnd, isCape ? L"披风 PNG (*.png)\0*.png\0所有文件\0*.*\0"
                                     : L"皮肤 PNG (*.png)\0*.png\0所有文件\0*.*\0", path))
            return;
        if (isCape) {
            if (!skin::ProbeCape(path)) {
                ShowToast(L"这不像披风图：需要 64×32 的 PNG（可为整数倍）", true);
                return;
            }
            // 引用原路径，不复制（用户可把披风放在任何位置）
            rec.capePath = mlc::WToUtf8(path);
            rec.cape.clear();
            store.SaveAccounts();
            skin::Clear();
            ShowToast(L"披风已设置（引用原文件路径）");
            Rebuild();
            return;
        }

        if (!skin::Probe(path)) {
            ShowToast(L"这不像 Minecraft 皮肤：需要 64×64 或 64×32 的 PNG（高清皮肤可为整数倍）", true);
            return;
        }
        if (id == L"acc.skinpath") {
            // 直接引用任意位置的 PNG（不复制）
            rec.skinPath = mlc::WToUtf8(path);
            rec.skin.clear();
            store.SaveAccounts();
            skin::Clear();
            ShowToast(L"皮肤已引用该文件路径（原文件被移动/删除会失效）");
            Rebuild();
            return;
        }
        // 选择皮肤：复制到账号目录托管，换皮肤直接覆盖
        std::wstring dst = store.SkinsDirFor(rec.portable) + L"\\" + keyW + L".png";
        std::error_code ec;
        fs::copy_file(fs::u8path(mlc::WToUtf8(path)), fs::u8path(mlc::WToUtf8(dst)),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            ShowToast(L"复制皮肤失败：" + mlc::Utf8ToW(ec.message()), true);
            return;
        }
        rec.skin = safeName(key) + ".png";
        rec.skinPath.clear();
        store.SaveAccounts();
        skin::Clear();
        ShowToast(rec.type == "msa"
                      ? L"皮肤已设置（点头像右侧「上传正版」才会同步到服务器）"
                      : L"皮肤已设置（离线账号仅本机显示）");
        Rebuild();
        return;
    }

    // ---- 创建离线账号弹窗 ----
    if (id == L"acc.offline") {
        m_showOfflineDlg = true;
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id == L"off.cancel") {
        m_showOfflineDlg = false;
        Rebuild();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (id == L"off.ok") {
        std::wstring name = L"Steve";
        if (Widget* w = Find(L"off.name")) name = w->editText;
        if (name.empty()) name = L"Steve";
        if (!mlc::IsValidPlayerName(name)) {
            ShowToast(L"名字只能是 1–16 位字母、数字或下划线（与正版 ID 规则相同）", true);
            return;
        }
        m_loginName = name;
        app.authOffline(mlc::WToUtf8(name));
        m_showOfflineDlg = false;
        m_accSel = -1;                 // 新账号已设为当前，列表选中跟随它
        ShowToast(L"已创建离线账号：" + name);
        Rebuild();
        return;
    }
    if (id == L"acc.mslogin") {
        if (m_msPending) {
            // 重新来一次：清掉旧的等待状态即可（旧线程会自己超时退出）
            m_msUserCode.clear();
            m_msVerifyUri.clear();
        }
        m_msPending = true;
        m_msStatus = L"正在获取设备码…";
        m_progressText = L"正在向微软申请设备码…";
        Rebuild();
        auto sink = m_sink;
        std::thread([sink]() {
            std::string err;
            auto onCode = [sink](const msauth::DeviceCode& dc) {
                sink->post((int)launcher::Ev::Info,
                           "__mscode__:" + dc.userCode + "|" + dc.verificationUri);
            };
            bool ok = launcher::App::Instance().authMsAutoLogin(onCode, err);
            sink->post((int)launcher::Ev::Info,
                       ok ? std::string("__msok__:") : ("__msfail__:" + err));
        }).detach();
        return;
    }

    // ---------- 全局设置 ----------
    if (id == L"set.save") {
        mlc::Settings& gs = store.spec();
        if (Widget* w = Find(L"set.mem")) gs.maxMemoryMB = w->value;
        if (Widget* w = Find(L"set.msid")) gs.msClientId = mlc::WToUtf8(w->editText);
        if (Widget* w = Find(L"set.isolate")) gs.isolate = w->checked;
        if (Widget* w = Find(L"set.integrity")) gs.checkIntegrity = w->checked;
        if (Widget* w = Find(L"set.repair")) gs.autoRepair = w->checked;
        if (Widget* w = Find(L"set.backsave")) gs.backupSaves = w->checked;
        if (Widget* w = Find(L"set.width")) {
            int v = parseInt(w->editText, gs.width);
            gs.width = v >= 320 ? v : 854;
        }
        if (Widget* w = Find(L"set.height")) {
            int v = parseInt(w->editText, gs.height);
            gs.height = v >= 240 ? v : 480;
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
        if (Widget* w = Find(L"set.cfbase")) {
            std::string base = mlc::WToUtf8(w->editText);
            gs.cfBase = base.empty() ? "https://mod.mcimirror.top/curseforge/v1" : base;
        }
        if (Widget* w = Find(L"set.cfkey")) gs.cfApiKey = mlc::WToUtf8(w->editText);
        store.SaveSettings();
        bm.Save();
        app.loadConfig();
        ShowToast(L"设置已保存");
        Rebuild();
        return;
    }
    if (id == L"set.opendata") {
        content::OpenFolder(mlc::WToUtf8(mlc::BaseDir()));
        return;
    }
    if (id == L"set.javaredetect") {
        m_javasLoaded = false;
        m_javas.clear();
        ShowToast(L"已重新检测 Java");
        Rebuild();
        return;
    }
    if (id == L"set.bgpick") {
        std::wstring f;
        if (PickFile(m_hwnd, L"图片\0*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.gif\0所有文件\0*.*\0", f)) {
            if (Widget* w = Find(L"set.bgimage")) w->editText = f;
        }
        return;
    }
    if (id == L"set.bgdirpick") {
        std::wstring d;
        if (PickFolder(m_hwnd, d)) {
            if (Widget* w = Find(L"set.bgdir")) w->editText = d;
        }
        return;
    }
    if (id == L"set.cfreset") {
        if (Widget* w = Find(L"set.cfbase"))
            w->editText = L"https://mod.mcimirror.top/curseforge/v1";
        ShowToast(L"已填回默认镜像，记得点「保存设置」");
        return;
    }
    if (id == L"set.srvadd") {
        std::wstring addr;
        if (Widget* w = Find(L"set.srvaddr")) addr = w->editText;
        if (addr.empty()) { ShowToast(L"请输入服务器地址", true); return; }
        std::string a = mlc::WToUtf8(addr);
        int port = 25565;
        size_t p = a.rfind(':');
        if (p != std::string::npos && p + 1 < a.size()) {
            int v = atoi(a.substr(p + 1).c_str());
            if (v > 0 && v < 65536) { port = v; a = a.substr(0, p); }
        }
        app.addServer(a, port);
        ShowToast(L"已添加 " + mlc::Utf8ToW(a));
        Rebuild();
        return;
    }
    if (id == L"set.srvdel") {
        if (Widget* w = Find(L"set.srvlist")) {
            if (w->selIdx >= 0) {
                app.removeServer((size_t)w->selIdx);
                ShowToast(L"已删除服务器");
                Rebuild();
            } else ShowToast(L"请先选中一项");
        }
        return;
    }
    if (id == L"set.srvdir") {
        if (Widget* w = Find(L"set.srvlist")) {
            if (w->selIdx >= 0 && w->selIdx < (int)app.servers().size()) {
                auto s = app.servers()[w->selIdx];
                app.setDirectServer(s.first, s.second);
                ShowToast(L"已设为直连：" + mlc::Utf8ToW(s.first));
                Rebuild();
            } else ShowToast(L"请先选中一项");
        }
        return;
    }
    if (id == L"set.srvdotsu") {
        app.setDirectServer("", 25565);
        ShowToast(L"已取消直连");
        Rebuild();
        return;
    }
}

} // namespace ui



