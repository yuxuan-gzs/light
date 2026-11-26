// LauncherApp.cpp — 启动器状态机实现（实例化模型）
#include "LauncherApp.h"
#include "Json.h"
#include "Http.h"
#include "Crypto.h"
#include "MlcData.h"

#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace launcher {

namespace fs = std::filesystem;

// ============================================================
// 构造 / 配置
// ============================================================
App& App::Instance() {
    static App inst;
    return inst;
}

App::App() {
    loadConfig();
    loadAccountFromStore();
}

App::~App() {
    abortInstall();
    if (m_installThread.joinable()) m_installThread.join();
    if (m_waitThread.joinable()) m_waitThread.join();
    if (m_hProcess) { CloseHandle(m_hProcess); m_hProcess = nullptr; }
}

void App::loadConfig() {
    std::string s = mlc::Store::I().spec().source;
    setSource(s);
    // 下载并发（基础值，库/资源各自换算）
    mc::setDownloadConcurrency(mlc::Store::I().spec().downloadThreads);
    // 微软登录应用 ID（空 = 内置 legacy 应用）
    msauth::SetClientId(mlc::Store::I().spec().msClientId);
}

void App::setSource(const std::string& name) {
    // "mcbbs" 是旧配置里的名字（那个镜像已随站点下线），统一当「自动」处理
    if (name == "mojang") m_source = mc::DownloadSource::Mojang;
    else if (name == "bmclapi") m_source = mc::DownloadSource::Bmclapi;
    else m_source = mc::DownloadSource::Auto;
}

void App::emit(Ev type, const std::string& data) {
    if (m_cb) m_cb(type, data);
}

// ============================================================
// 版本清单
// ============================================================
mc::VersionManifest App::manifest() const {
    std::lock_guard<std::mutex> lk(m_manifestMu);
    return m_manifest;
}

bool App::refreshRemote(std::string& err) {
    mc::VersionManifest mf;
    if (!mc::fetchManifest(m_source, mf, err)) return false;
    {
        std::lock_guard<std::mutex> lk(m_manifestMu);
        m_manifest = std::move(mf);
        m_manifestLoaded = true;
    }
    return true;
}

bool App::ensureManifest(std::string& err) {
    if (m_manifestLoaded) return true;
    if (!refreshRemote(err)) return false;
    return true;
}

// ============================================================
// 安装
// ============================================================
mc::InstallProgress App::installProgress() {
    std::lock_guard<std::mutex> lk(m_installMu);
    return m_installProgress;
}

void App::startThread(std::thread& t, std::function<void()> fn) {
    if (t.joinable()) t.join();
    t = std::thread(std::move(fn));
}

void App::startInstall(const std::string& folder, const std::string& versionId,
                       int loaderType, const std::string& loaderVersion,
                       const std::string& displayName) {
    if (m_installing.load()) { emit(Ev::Info, "已有下载任务在进行中"); return; }
    startThread(m_installThread, [this, folder, versionId, loaderType, loaderVersion, displayName]() {
        installWorker(folder, versionId, loaderType, loaderVersion, displayName);
    });
}

// 收集 folder 下新增的版本（加载器安装后用于定位新版本 id）
static std::set<std::string> versionSet(const std::string& folder) {
    std::set<std::string> s;
    std::error_code ec;
    fs::path vd = fs::u8path(mlc::JoinPath(folder, "versions"));
    if (!fs::is_directory(vd, ec)) return s;
    for (auto& e : fs::directory_iterator(vd, ec)) {
        if (e.is_directory(ec)) s.insert(mlc::WToUtf8(e.path().filename().wstring()));
    }
    return s;
}

void App::installWorker(std::string folder, std::string versionId, int loaderType,
                        std::string loaderVersion, std::string displayName) {
    m_installing.store(true);
    m_abort.store(false);
    {
        std::lock_guard<std::mutex> lk(m_installMu);
        m_installProgress = mc::InstallProgress{};
    }
    mlc::Log("开始安装 " + versionId + " -> " + folder);

    std::string err;
    if (!ensureManifest(err)) {
        m_installing.store(false);
        emit(Ev::InstallError, err.empty() ? "无法获取版本清单" : err);
        return;
    }
    mc::VersionManifest mf = manifest();

    auto progress = [this](const mc::InstallProgress& p) -> bool {
        if (m_abort.load()) return false;
        {
            std::lock_guard<std::mutex> lk(m_installMu);
            m_installProgress = p;
        }
        json::Value v = json::makeObject();
        v.obj["phase"] = json::makeString(p.phase);
        v.obj["done"] = json::makeInt(p.done);
        v.obj["total"] = json::makeInt(p.total);
        v.obj["current"] = json::makeString(p.current);
        emit(Ev::InstallProgress, json::dump(v));
        return true;
    };

    bool ok = mc::installVersion(mlc::JoinPath(folder, "versions"),
                                 mlc::JoinPath(folder, "libraries"),
                                 mlc::JoinPath(folder, "assets"),
                                 mf, versionId, m_source, progress, err);
    if (!ok) {
        m_installing.store(false);
        emit(Ev::InstallError, err.empty() ? "安装失败" : err);
        return;
    }

    std::string finalVersionId = versionId;

    // 加载器（1 Fabric 2 Quilt 3 Forge 4 NeoForge）
    if (loaderType >= 1 && loaderType <= 4) {
        emit(Ev::Info, "原版安装完成，正在安装加载器…");
        // installer 用哪个 Java 由目标 MC 版本决定（1.12 → 8，1.21 → 21）
        int needJava = 17;
        {
            mc::Version baseV;
            std::string verr;
            if (mc::loadVersion(mlc::JoinPath(folder, "versions"), versionId, baseV, verr)) {
                if (baseV.javaMajor > 0) needJava = baseV.javaMajor;
            }
        }
        std::string javaPath = resolveJava(mlc::Instance{}, needJava, err);
        if (loaderType == 1 || loaderType == 2) {
            bool quilt = (loaderType == 2);
            std::string lv = loaderVersion;
            if (lv.empty()) {
                auto vers = loader::FetchVersions(versionId, quilt, err);
                for (auto& v : vers) if (v.stable) { lv = v.version; break; }
                if (lv.empty() && !vers.empty()) lv = vers[0].version;
            }
            if (lv.empty()) {
                m_installing.store(false);
                emit(Ev::InstallError, err.empty() ? "无可用加载器版本" : err);
                return;
            }
            std::string id = loader::Install(versionId, lv, quilt,
                mlc::JoinPath(folder, "versions"), mlc::JoinPath(folder, "libraries"),
                [this](const std::string& msg) { emit(Ev::Log, msg); }, err);
            if (id.empty()) {
                m_installing.store(false);
                emit(Ev::InstallError, err.empty() ? "安装加载器失败" : err);
                return;
            }
            finalVersionId = id;
        } else {
            std::set<std::string> before = versionSet(folder);
            bool ok2 = false;
            if (loaderType == 3) {
                std::string fv = loaderVersion;
                if (fv.empty()) {
                    auto vers = loader::FetchForgeVersions(versionId, err);
                    if (vers.empty()) {
                        m_installing.store(false);
                        emit(Ev::InstallError, err.empty() ? "无可用 Forge 版本" : err);
                        return;
                    }
                    fv = vers[0];
                }
                ok2 = loader::InstallForge(fv, folder, javaPath,
                    [this](const std::string& msg) { emit(Ev::Log, msg); }, err);
            } else {
                std::string nv = loaderVersion;
                if (nv.empty()) {
                    auto vers = loader::FetchNeoForgeVersions(versionId, err);
                    if (vers.empty()) {
                        m_installing.store(false);
                        emit(Ev::InstallError, err.empty() ? "无可用 NeoForge 版本" : err);
                        return;
                    }
                    nv = vers[0];
                }
                ok2 = loader::InstallNeoForge(nv, folder, javaPath,
                    [this](const std::string& msg) { emit(Ev::Log, msg); }, err);
            }
            if (!ok2) {
                m_installing.store(false);
                emit(Ev::InstallError, err.empty() ? "安装加载器失败" : err);
                return;
            }
            // 找到 installer 新建的版本目录
            std::set<std::string> after = versionSet(folder);
            const char* tag = (loaderType == 3) ? "forge" : "neoforge";
            for (const auto& id : after) {
                if (before.count(id)) continue;
                std::string low = id;
                std::transform(low.begin(), low.end(), low.begin(),
                               [](unsigned char c) { return (char)std::tolower(c); });
                if (low.find(tag) != std::string::npos) { finalVersionId = id; break; }
            }
        }
    }

    // 注册实例（照 HMCL：一个版本目录就是一个游戏；加载器是「装进」这个游戏，
    // 新版本覆盖旧版本，不会再多出第二个游戏）
    mlc::Store& store = mlc::Store::I();
    std::string iid;
    if (finalVersionId != versionId && !finalVersionId.empty()) {
        iid = store.AdoptLoaderVersion(folder, finalVersionId);
    }
    if (iid.empty()) iid = store.EnsureInstance(folder, finalVersionId);
    if (!iid.empty() && !displayName.empty()) {
        if (mlc::Instance* in = store.Find(iid)) {
            std::wstring w = mlc::Utf8ToW(displayName);
            if (!mlc::IsValidGameName(w)) w = mlc::SanitizeGameName(w);
            if (!w.empty()) { in->name = mlc::WToUtf8(w); store.SaveInstances(); }
        }
    }
    store.EnsureFolder(folder);
    store.SetCurrentFolder(folder);
    store.SetCurrent(iid);

    m_installing.store(false);
    {
        std::lock_guard<std::mutex> lk(m_installMu);
        m_installProgress.finished = true;
        m_installProgress.error.clear();
    }
    mlc::Log("安装完成 " + finalVersionId + " (实例 " + iid + ")");
    emit(Ev::InstallDone, iid);
}

void App::startInstallLoader(const std::string& folder, const std::string& mcVersion,
                             const std::string& loaderVersion, bool quilt) {
    if (m_installing.load()) { emit(Ev::Info, "已有任务在进行中"); return; }
    startThread(m_installThread, [this, folder, mcVersion, loaderVersion, quilt]() {
        m_installing.store(true);
        m_abort.store(false);
        std::string err;
        std::string id = loader::Install(mcVersion, loaderVersion, quilt,
            mlc::JoinPath(folder, "versions"), mlc::JoinPath(folder, "libraries"),
            [this](const std::string& msg) { emit(Ev::Log, msg); }, err);
        m_installing.store(false);
        if (!id.empty()) {
            // 装进当前游戏：把原版实例改成指向加载器版本（同一个游戏，不新增）
            std::string iid = mlc::Store::I().AdoptLoaderVersion(folder, id);
            if (!iid.empty()) mlc::Store::I().SetCurrent(iid);
            emit(Ev::InstallDone, id);
        } else {
            emit(Ev::InstallError, err.empty() ? "安装加载器失败" : err);
        }
    });
}

void App::startInstallForge(const std::string& folder, const std::string& fullVersion) {
    if (m_installing.load()) { emit(Ev::Info, "已有任务在进行中"); return; }
    startThread(m_installThread, [this, folder, fullVersion]() {
        m_installing.store(true);
        m_abort.store(false);
        std::string err;
        // 用户显式指定的 Java 才用它；否则交给 InstallForge 按 MC 版本挑（1.12.2 必须 Java 8，
        // 用 17/21 跑 installer 会直接崩 —— 之前这里硬塞 17 是安装失败的主因之一）。
        std::string javaPath = mlc::Store::I().spec().javaPath;
        std::set<std::string> before = versionSet(folder);
        bool ok = loader::InstallForge(fullVersion, folder, javaPath,
            [this](const std::string& msg) { emit(Ev::Log, msg); }, err);
        m_installing.store(false);
        if (!ok) { emit(Ev::InstallError, err.empty() ? "安装 Forge 失败" : err); return; }
        std::set<std::string> after = versionSet(folder);
        std::string id;
        for (const auto& x : after) {
            if (before.count(x)) continue;
            std::string low = x;
            std::transform(low.begin(), low.end(), low.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (low.find("forge") != std::string::npos) { id = x; break; }
        }
        if (!id.empty()) {
            std::string iid = mlc::Store::I().AdoptLoaderVersion(folder, id);
            if (!iid.empty()) mlc::Store::I().SetCurrent(iid);
        }
        emit(Ev::InstallDone, id.empty() ? fullVersion : id);
    });
}

void App::startInstallNeoForge(const std::string& folder, const std::string& fullVersion) {
    if (m_installing.load()) { emit(Ev::Info, "已有任务在进行中"); return; }
    startThread(m_installThread, [this, folder, fullVersion]() {
        m_installing.store(true);
        m_abort.store(false);
        std::string err;
        std::string javaPath = mlc::Store::I().spec().javaPath;
        std::set<std::string> before = versionSet(folder);
        bool ok = loader::InstallNeoForge(fullVersion, folder, javaPath,
            [this](const std::string& msg) { emit(Ev::Log, msg); }, err);
        m_installing.store(false);
        if (!ok) { emit(Ev::InstallError, err.empty() ? "安装 NeoForge 失败" : err); return; }
        std::set<std::string> after = versionSet(folder);
        std::string id;
        for (const auto& x : after) {
            if (before.count(x)) continue;
            std::string low = x;
            std::transform(low.begin(), low.end(), low.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (low.find("neoforge") != std::string::npos) { id = x; break; }
        }
        if (!id.empty()) {
            std::string iid = mlc::Store::I().AdoptLoaderVersion(folder, id);
            if (!iid.empty()) mlc::Store::I().SetCurrent(iid);
        }
        emit(Ev::InstallDone, id.empty() ? fullVersion : id);
    });
}

bool App::removeLoaderVersion(const std::string& folder, const std::string& versionId,
                              std::string& err) {
    if (versionId.empty()) { err = "版本 id 为空"; return false; }
    // 禁止删除原版版本
    std::string low = versionId;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (low.find("fabric") == std::string::npos && low.find("quilt") == std::string::npos &&
        low.find("forge") == std::string::npos) {
        err = "该版本不是加载器版本，无法卸载";
        return false;
    }
    fs::path dir = fs::u8path(mlc::JoinPath(mlc::JoinPath(folder, "versions"), versionId));
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) { err = "版本目录不存在"; return false; }

    // 卸载加载器 = 把这个游戏退回原版（照 HMCL：组件从实例上摘掉，游戏本身还在）
    std::string parent = mlc::Store::ParentOf(folder, versionId);
    if (!parent.empty() &&
        !mlc::FileExists(mlc::JoinPath(mlc::JoinPath(mlc::JoinPath(folder, "versions"), parent),
                                       parent + ".json"))) {
        parent.clear();
    }

    fs::remove_all(dir, ec);
    if (ec) { err = "删除失败: " + ec.message(); return false; }

    mlc::Store& store = mlc::Store::I();
    if (!parent.empty()) {
        for (auto& in : store.list()) {
            if (in.folder != folder || in.versionId != versionId) continue;
            in.versionId = parent;
            mlc::Store::DetectLoader(in);
            store.SaveInstances();
            store.SaveInstanceSettings(in);
            break;
        }
    }
    store.SyncWithDisk();
    return true;
}

// ============================================================
// 完整性
// ============================================================
mc::IntegrityReport App::checkInstance(const mlc::Instance& inst, std::string& err) {
    mc::IntegrityReport rep;
    mc::checkIntegrity(inst.versionsDir(), inst.librariesDir(), inst.assetsDir(),
                       inst.versionId, rep, err);
    return rep;
}

void App::startIntegrityCheck(const std::string& instanceId, bool repair) {
    if (m_installing.load()) { emit(Ev::Info, "已有任务在进行中"); return; }
    startThread(m_installThread, [this, instanceId, repair]() {
        integrityWorker(instanceId, repair);
    });
}

void App::integrityWorker(std::string instanceId, bool repair) {
    m_installing.store(true);
    m_abort.store(false);
    mlc::Instance* inst = mlc::Store::I().Find(instanceId);
    if (!inst) {
        m_installing.store(false);
        emit(Ev::InstallError, "实例不存在");
        return;
    }
    // 拷贝一份，避免后续列表变动
    mlc::Instance snap = *inst;

    std::string err;
    mc::IntegrityReport rep = checkInstance(snap, err);
    emit(Ev::Info, "完整性检查：" + rep.detail);

    if (!rep.ok && repair) {
        emit(Ev::Info, "发现缺失文件，正在自动修复…");
        {
            auto progress = [this](const mc::InstallProgress& p) -> bool {
                if (m_abort.load()) return false;
                {
                    std::lock_guard<std::mutex> lk(m_installMu);
                    m_installProgress = p;
                }
                json::Value v = json::makeObject();
                v.obj["phase"] = json::makeString(p.phase);
                v.obj["done"] = json::makeInt(p.done);
                v.obj["total"] = json::makeInt(p.total);
                v.obj["current"] = json::makeString(p.current);
                emit(Ev::InstallProgress, json::dump(v));
                return true;
            };
            // 直接用本地版本 JSON 补齐文件（加载器版本也能修，不依赖版本清单）
            std::string ferr;
            mc::repairVersionFiles(snap.versionsDir(), snap.librariesDir(), snap.assetsDir(),
                                   snap.versionId, m_source, progress, ferr);
            if (!ferr.empty()) emit(Ev::Info, "修复提示：" + ferr);
            rep = checkInstance(snap, err);
        }
    }

    m_installing.store(false);
    json::Value j = json::makeObject();
    j.obj["instance"] = json::makeString(instanceId);
    j.obj["ok"] = json::makeBool(rep.ok);
    j.obj["detail"] = json::makeString(rep.detail);
    j.obj["missing"] = json::makeInt((int64_t)(
        rep.missingLibs + rep.missingAssets + rep.missingNatives +
        (rep.jsonMissing ? 1 : 0) + (rep.clientJarMissing ? 1 : 0)));
    emit(Ev::IntegrityDone, json::dump(j));
}

// ============================================================
// 账号
// ============================================================
void App::loadAccountFromStore() {
    mlc::AccountRec* a = mlc::Store::I().CurrentAccount();
    if (!a) {
        // 切换账号存储位置后可能一个账号都没有：清空内存里的账号，避免用到旧数据
        std::lock_guard<std::mutex> lk(m_accountMu);
        m_account = msauth::Account{};
        m_accountType = "offline";
        return;
    }
    msauth::Account acc;
    acc.name = a->name;
    acc.uuid = a->uuid;
    acc.accessToken = a->accessToken;
    acc.refreshToken = a->refreshToken;
    acc.expiresAt = a->expiresAt;
    acc.ok = !acc.name.empty();
    {
        std::lock_guard<std::mutex> lk(m_accountMu);
        m_account = acc;
        m_accountType = a->type.empty() ? "offline" : a->type;
    }
}

std::string App::accountKey() const {
    std::lock_guard<std::mutex> lk(m_accountMu);
    return m_account.uuid.empty() ? m_account.name : m_account.uuid;
}

bool App::selectAccount(const std::string& key, std::string& err) {
    mlc::Store& store = mlc::Store::I();
    mlc::AccountRec* a = store.FindAccount(key);
    if (!a) { err = "账号不存在"; return false; }
    store.SetAccountKey(a->uuid.empty() ? a->name : a->uuid);
    msauth::Account acc;
    acc.name = a->name;
    acc.uuid = a->uuid;
    acc.accessToken = a->accessToken;
    acc.refreshToken = a->refreshToken;
    acc.expiresAt = a->expiresAt;
    acc.ok = true;
    {
        std::lock_guard<std::mutex> lk(m_accountMu);
        m_account = acc;
        m_accountType = a->type.empty() ? "offline" : a->type;
    }
    emit(Ev::AccountChange);
    return true;
}

bool App::refreshAccount(std::string& err) {
    msauth::Account acc;
    std::string type;
    {
        std::lock_guard<std::mutex> lk(m_accountMu);
        acc = m_account;
        type = m_accountType;
    }
    if (type != "msa") { err = "离线账号无需续期"; return false; }
    if (acc.refreshToken.empty()) { err = "该账号没有 refresh_token，请重新登录微软账号"; return false; }
    if (!msauth::Refresh(acc, err)) return false;

    {
        std::lock_guard<std::mutex> lk(m_accountMu);
        m_account.accessToken = acc.accessToken;
        m_account.refreshToken = acc.refreshToken;
        m_account.expiresAt = acc.expiresAt;
    }
    mlc::AccountRec rec;
    rec.type = "msa";
    rec.name = acc.name;
    rec.uuid = acc.uuid;
    rec.accessToken = acc.accessToken;
    rec.refreshToken = acc.refreshToken;
    rec.expiresAt = acc.expiresAt;
    mlc::Store::I().UpsertAccount(rec);
    emit(Ev::AccountChange);
    return true;
}

msauth::Account App::account() const {
    std::lock_guard<std::mutex> lk(m_accountMu);
    return m_account;
}

std::string App::accountType() const {
    std::lock_guard<std::mutex> lk(m_accountMu);
    return m_accountType;
}

void App::authOffline(const std::string& name) {
    std::string uuid = crypto::offlineUuid(name);
    msauth::Account acc;
    acc.name = name; acc.uuid = uuid; acc.accessToken = "0"; acc.ok = true;
    {
        std::lock_guard<std::mutex> lk(m_accountMu);
        m_account = acc;
        m_accountType = "offline";
    }
    mlc::AccountRec rec;
    rec.type = "offline";
    rec.name = name;
    rec.uuid = uuid;
    rec.accessToken = "0";
    mlc::Store::I().UpsertAccount(rec);
    emit(Ev::AccountChange);
}

bool App::authMsStart(msauth::DeviceCode& dc, std::string& err) {
    if (!msauth::StartDeviceCodeFlow(dc, err)) return false;
    m_deviceCode = dc;
    return true;
}

int App::authMsPoll(msauth::Account& acc, std::string& err) {
    int r = msauth::PollForToken(m_deviceCode, acc, err);
    if (r == 1) {
        {
            std::lock_guard<std::mutex> lk(m_accountMu);
            m_account = acc;
            m_accountType = "msa";
        }
        mlc::AccountRec rec;
        rec.type = "msa";
        rec.name = acc.name;
        rec.uuid = acc.uuid;
        rec.accessToken = acc.accessToken;
        rec.refreshToken = acc.refreshToken;
        rec.expiresAt = acc.expiresAt;
        mlc::Store::I().UpsertAccount(rec);
        emit(Ev::AccountChange);
    }
    return r;
}

bool App::authMsAutoLogin(const std::function<void(const msauth::DeviceCode&)>& onCode,
                          std::string& err) {
    msauth::Account acc;
    if (!msauth::FullLogin(onCode, acc, err)) return false;
    {
        std::lock_guard<std::mutex> lk(m_accountMu);
        m_account = acc;
        m_accountType = "msa";
    }
    mlc::AccountRec rec;
    rec.type = "msa";
    rec.name = acc.name;
    rec.uuid = acc.uuid;
    rec.accessToken = acc.accessToken;
    rec.refreshToken = acc.refreshToken;
    rec.expiresAt = acc.expiresAt;
    mlc::Store::I().UpsertAccount(rec);
    emit(Ev::AccountChange);
    mlc::Log("微软登录成功: " + acc.name);
    return true;
}

// ============================================================
// Java 解析（替换原来的「全局 Java 目录」逻辑）
// ============================================================
static std::string normalizeJavaPath(std::string p) {
    if (p.empty()) return p;
    std::error_code ec;
    if (fs::is_directory(fs::u8path(p), ec)) {
        fs::path a = fs::u8path(p) / "bin" / "java.exe";
        fs::path b = fs::u8path(p) / "bin" / "javaw.exe";
        if (fs::exists(a, ec)) return a.u8string();
        if (fs::exists(b, ec)) return b.u8string();
    }
    std::string low = p;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (low.size() >= 10 && low.substr(low.size() - 10) == "javaw.exe") {
        return p.substr(0, p.size() - 10) + "java.exe";
    }
    return p;
}

std::string App::resolveJava(const mlc::Instance& inst, int neededMajor, std::string& err,
                             bool* satisfied) {
    (void)err;
    if (satisfied) *satisfied = false;

    int major = neededMajor > 0 ? neededMajor : mlc::Store::I().spec().javaMajor;
    if (major <= 0) major = 17;

    // 1) 单局设置 / 全局设置里明确指定的 Java —— 用户的选择，视为满足
    std::string java = inst.settings.javaPath;
    if (java.empty()) java = mlc::Store::I().spec().javaPath;
    if (!java.empty()) {
        std::string n = normalizeJavaPath(java);
        std::error_code ec;
        if (fs::exists(fs::u8path(n), ec)) {
            if (satisfied) *satisfied = true;
            return n;
        }
        // 指定的 Java 不在了：不要死用它，继续往下找
    }

    // 2) 之前自动下载过的官方运行时（精确匹配版本）
    std::string cached = java::FindRuntime(major);
    if (!cached.empty()) {
        if (satisfied) *satisfied = true;
        return cached;
    }

    // 3) 系统里检测：按版本 JSON 的 javaVersion.majorVersion 挑
    bool ok = false;
    java::JavaRuntime jr = java::DetectBest(major, &ok);
    if (jr.path.empty()) {
        auto all = java::DetectAll();
        if (!all.empty()) jr = all.front();
    }
    if (satisfied) *satisfied = ok && !jr.path.empty();
    return normalizeJavaPath(jr.path);
}

// ============================================================
// 启动
// ============================================================
bool App::launchInstance(const std::string& instanceId, std::string& err) {
    mlc::Store& store = mlc::Store::I();
    mlc::Instance* pin = store.Find(instanceId);
    if (!pin) {
        mlc::Instance* cur = store.Current();
        if (!cur) { err = "还没有添加任何游戏，请先在「版本选择」中添加文件夹"; return false; }
        pin = cur;
    }
    mlc::Instance inst = *pin;   // 拷贝，避免线程间被改
    const mlc::Settings& gs = store.spec();

    // ---- 1. 完整性检查 ----
    bool check = inst.settings.checkIntegrity == 1
                 || (inst.settings.checkIntegrity == -1 && gs.checkIntegrity);
    mc::Version v;
    if (!mc::loadVersion(inst.versionsDir(), inst.versionId, v, err)) {
        emit(Ev::InstallError, "无法读取版本：" + err);
        return false;
    }

    if (check) {
        emit(Ev::Info, "正在检查游戏完整性…");
        std::string cerr;
        mc::IntegrityReport rep = checkInstance(inst, cerr);
        if (rep.ok) {
            emit(Ev::Info, "完整性检查通过");
        } else {
            emit(Ev::Info, "文件不完整（" + rep.detail + "）");
            if (gs.autoRepair) {
                emit(Ev::Info, "正在自动修复缺失文件…");
                auto progress = [this](const mc::InstallProgress& p) -> bool {
                    if (m_abort.load()) return false;
                    {
                        std::lock_guard<std::mutex> lk(m_installMu);
                        m_installProgress = p;
                    }
                    json::Value jv = json::makeObject();
                    jv.obj["phase"] = json::makeString(p.phase);
                    jv.obj["done"] = json::makeInt(p.done);
                    jv.obj["total"] = json::makeInt(p.total);
                    jv.obj["current"] = json::makeString(p.current);
                    emit(Ev::InstallProgress, json::dump(jv));
                    return true;
                };
                std::string ferr;
                mc::repairVersionFiles(inst.versionsDir(), inst.librariesDir(),
                                       inst.assetsDir(), inst.versionId, m_source,
                                       progress, ferr);
                if (!ferr.empty()) emit(Ev::Info, "修复提示：" + ferr);
                rep = checkInstance(inst, cerr);
            }
            if (!rep.ok) {
                // 关键：缺文件不再一刀切拦死启动（HMCL 也是先让你玩，崩了再修）
                emit(Ev::Info, "仍有缺失（" + rep.detail +
                               "）；先尝试启动，若报错请到「版本设置 → 立即检查并修复」");
            } else {
                emit(Ev::Info, "修复完成，继续启动");
            }
        }
    }

    // ---- 2. 自动备份存档（可选）----
    if (gs.backupSaves) {
        std::error_code ec;
        fs::path saves = fs::u8path(inst.savesDir());
        if (fs::is_directory(saves, ec)) {
            SYSTEMTIME st; GetLocalTime(&st);
            char ts[64];
            snprintf(ts, sizeof(ts), "%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth,
                     st.wDay, st.wHour, st.wMinute, st.wSecond);
            fs::path dst = fs::u8path(mlc::WToUtf8(mlc::BackupsDir())) /
                           mlc::Utf8ToW(inst.id) / (L"saves_" + mlc::Utf8ToW(ts));
            fs::copy(saves, dst, fs::copy_options::recursive |
                                 fs::copy_options::skip_existing, ec);
            if (!ec) emit(Ev::Info, "已备份存档到 Data\\mlc\\backups\\" + inst.id);
        }
    }

    // ---- 3. Java ----
    bool suitable = false;
    std::string java = resolveJava(inst, v.javaMajor, err, &suitable);
    if (!suitable) {
        // 像 HMCL 一样：没有合适的 Java 就问一句，同意就自动下载官方运行时
        const int need = v.javaMajor > 0 ? v.javaMajor : 17;
        std::wstring msg =
            L"没有找到合适的 Java（当前版本需要 Java " + std::to_wstring(need) + L" 或更高）。\n\n"
            L"是否自动下载一个官方 Java " + std::to_wstring(need) + L"？\n"
            L"（约 40–60MB，国内走 BMCLAPI 镜像，装到 Data\\mlc\\java）\n\n"
            L"选「否」可以稍后自己安装 Java，再到「全局设置 → Java」里指定。";
        HWND owner = GetForegroundWindow();
        if (MessageBoxW(owner, msg.c_str(), L"缺少 Java", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            emit(Ev::Info, "正在下载 Java " + std::to_string(need) + " 运行时…");
            auto prog = [this](const std::string& stage, int64_t done, int64_t total) -> bool {
                if (m_abort.load()) return false;
                mc::InstallProgress p;
                p.phase = "java";
                p.current = stage;
                p.done = done;
                p.total = total;
                {
                    std::lock_guard<std::mutex> lk(m_installMu);
                    m_installProgress = p;
                }
                json::Value jv = json::makeObject();
                jv.obj["phase"] = json::makeString(p.phase);
                jv.obj["done"] = json::makeInt(p.done);
                jv.obj["total"] = json::makeInt(p.total);
                jv.obj["current"] = json::makeString(p.current);
                emit(Ev::InstallProgress, json::dump(jv));
                return true;
            };
            std::string got;
            std::string jerr;
            if (java::InstallRuntime(need, prog, got, jerr)) {
                java = got;
                suitable = true;
                emit(Ev::Info, "Java " + std::to_string(need) + " 已就绪，继续启动");
            } else {
                emit(Ev::Info, "Java 下载失败：" + jerr);
            }
        }
        if (!suitable) {
            err = "未找到合适的 Java " + std::to_string(need) +
                  "。可以到「全局设置 → Java」里指定，或重新安装 Java 后再启动。";
            emit(Ev::InstallError, err);
            return false;
        }
    }
    if (java.empty()) {
        err = "未找到可用的 Java，请安装 Java 或在「全局设置」中指定 java.exe 路径";
        emit(Ev::InstallError, err);
        return false;
    }

    // 正版账号 token 快过期时先续期（refresh_token 免登录）
    {
        msauth::Account a0 = account();
        if (accountType() == "msa" && a0.expiresAt > 0 &&
            a0.expiresAt < (int64_t)time(nullptr) + 60) {
            emit(Ev::Info, "正版账号令牌已过期，正在续期…");
            std::string rerr;
            if (!refreshAccount(rerr)) {
                if (a0.accessToken.empty() || a0.refreshToken.empty()) {
                    err = "正版令牌已过期，且这种登录方式无法自动续期。请到「账号」页重新点「微软登录」。";
                    emit(Ev::InstallError, err);
                    return false;
                }
                emit(Ev::Info, "续期失败：" + rerr + "（继续用旧令牌启动）");
            }
        }
    }
    msauth::Account acc = account();
    std::string playerName = inst.settings.nickname.empty() ? acc.name : inst.settings.nickname;
    if (playerName.empty()) {
        err = "请先在「账号」页登录（离线登录也可）";
        emit(Ev::InstallError, err);
        return false;
    }

    // ---- 4. 参数 ----
    mc::LaunchOptions opt;
    opt.gameDir = inst.folder;
    opt.dataDir = inst.dataDir();
    // 与 HMCL 一致：natives 解压到实例目录下的 natives-windows，启动时清空重解压
    opt.nativesDir = mc::defaultNativesDir(opt.dataDir);
    opt.javaPath = java;
    opt.authName = playerName;
    opt.authUuid = acc.uuid.empty() ? crypto::offlineUuid(playerName) : acc.uuid;
    opt.authToken = acc.accessToken.empty() ? "0" : acc.accessToken;
    opt.userType = (accountType() == "msa") ? "msa" : "legacy";
    opt.versionType = "release";

    int w = inst.settings.width > 0 ? inst.settings.width : gs.width;
    int h = inst.settings.height > 0 ? inst.settings.height : gs.height;
    if (w <= 0) w = 854;
    if (h <= 0) h = 480;
    opt.width = w;
    opt.height = h;

    int mem = inst.settings.maxMemoryMB > 0 ? inst.settings.maxMemoryMB : gs.maxMemoryMB;
    if (mem < 256) mem = 2048;
    opt.maxMemoryMB = mem;
    opt.extraJvmArgs = inst.settings.jvmArgs;
    opt.extraGameArgs = inst.settings.gameArgs;
    if (inst.settings.fullscreen) opt.extraGameArgs.push_back("--fullscreen");

    if (!inst.settings.server.empty()) {
        opt.server = inst.settings.server;
        opt.serverPort = inst.settings.serverPort > 0 ? inst.settings.serverPort : 25565;
    } else if (!gs.directServer.empty()) {
        opt.server = gs.directServer;
        opt.serverPort = gs.directPort > 0 ? gs.directPort : 25565;
    }

    std::vector<std::string> args;
    if (!mc::buildLaunchCommand(v, opt, args, err)) {
        emit(Ev::InstallError, err);
        return false;
    }

    auto quote = [](const std::string& a) {
        // 空参数必须写成 ""，否则会被命令行吞掉，导致 --clientId 把 --xuid 当值
        if (a.empty()) return std::string("\"\"");
        if (a.find_first_of(" \t\n\"") == std::string::npos) return a;
        std::string o = "\"";
        for (char c : a) { if (c == '"') o += "\\\""; else o += c; }
        o += "\"";
        return o;
    };

    std::error_code ec;
    fs::path logDir = fs::u8path(inst.folder) / "logs";
    fs::create_directories(logDir, ec);
    m_logPath = (logDir / "yxg-launcher.log").u8string();

    std::string cmdline = quote(java);
    std::string argsStr;
    for (const auto& a : args) argsStr += quote(a) + " ";

    // 超长命令行改用 @argfile（只有 Java 9+ 支持；Java 8 会把它当成普通参数）
    if (argsStr.size() > 20000 && v.javaMajor >= 9) {
        fs::path argsFile = logDir / "jvm_args.txt";
        std::ofstream f(argsFile, std::ios::binary | std::ios::trunc);
        if (f) {
            f << argsStr;
            f.close();
            cmdline += " @\"" + argsFile.u8string() + "\"";
        } else {
            cmdline += " " + argsStr;
        }
    } else {
        cmdline += " " + argsStr;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hLog = CreateFileW(mlc::Utf8ToW(m_logPath).c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hLog;
    si.hStdError = hLog;

    PROCESS_INFORMATION pi{};
    std::wstring app = mlc::Utf8ToW(java);
    std::wstring cl = mlc::Utf8ToW(cmdline);
    std::wstring wd = mlc::Utf8ToW(inst.dataDir());
    fs::create_directories(fs::u8path(inst.dataDir()), ec);

    BOOL ok = CreateProcessW(app.c_str(), cl.data(), nullptr, nullptr,
                             hLog != INVALID_HANDLE_VALUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             nullptr, wd.c_str(), &si, &pi);
    if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
    if (!ok) {
        err = "启动进程失败，错误码 " + std::to_string(GetLastError());
        emit(Ev::InstallError, err);
        return false;
    }

    CloseHandle(pi.hThread);
    {
        std::lock_guard<std::mutex> lk(m_gameMu);
        if (m_hProcess) CloseHandle(m_hProcess);
        m_hProcess = pi.hProcess;
        m_pid = pi.dwProcessId;
        m_gameExit.store(-1);
    }
    store.Touch(inst.id);
    store.SetCurrent(inst.id);

    if (m_waitThread.joinable()) m_waitThread.join();
    m_waitThread = std::thread(&App::waitWorker, this);

    // 进程建起来了但还不算「启动成功」：等游戏窗口真正出现再报成功
    emit(Ev::GameLaunching, std::to_string(pi.dwProcessId));
    return true;
}

// 该进程是否有可见的顶层窗口（GLFW 的窗口出现 = 游戏真的起来了）
static bool HasVisibleMainWindow(DWORD pid) {
    struct Ctx { DWORD pid; bool found; };
    Ctx ctx{ pid, false };
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        Ctx* c = (Ctx*)lp;
        DWORD wpid = 0;
        GetWindowThreadProcessId(h, &wpid);
        if (wpid != c->pid) return TRUE;
        if (!IsWindowVisible(h)) return TRUE;
        RECT r{};
        if (!GetWindowRect(h, &r)) return TRUE;
        if (r.right - r.left < 64 || r.bottom - r.top < 64) return TRUE;  // 跳过 0×0 / 工具窗口
        c->found = true;
        return FALSE;
    }, (LPARAM)&ctx);
    return ctx.found;
}

// 从启动器日志尾部捞一句崩溃原因，用于「启动失败」提示
static std::string crashHint(const std::string& logPath) {
    if (logPath.empty()) return std::string();
    std::ifstream f(fs::u8path(logPath), std::ios::binary);
    if (!f) return std::string();
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size <= 0) return std::string();
    std::streamoff take = std::min<std::streamoff>(size, 64 * 1024);
    f.seekg(size - take);
    std::string buf((size_t)take, '\0');
    f.read(&buf[0], take);

    std::string desc, exc;
    size_t pos = 0;
    while (pos < buf.size()) {
        size_t e = buf.find('\n', pos);
        std::string line = buf.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (desc.empty() && line.rfind("Description: ", 0) == 0) desc = line.substr(13);
        if (exc.empty() && line.find("Exception") != std::string::npos &&
            line.rfind("at ", 0) != 0 && line.rfind("\tat ", 0) != 0) {
            exc = line;
        }
        pos = (e == std::string::npos) ? buf.size() : e + 1;
    }
    std::string h = !desc.empty() ? desc : exc;
    if (h.size() > 120) h = h.substr(0, 120) + "…";
    return h.empty() ? std::string() : ("：" + h);
}

void App::waitWorker() {
    HANDLE h = nullptr;
    DWORD pid = 0;
    { std::lock_guard<std::mutex> lk(m_gameMu); h = m_hProcess; pid = m_pid; }
    if (!h) { emit(Ev::GameExit, "-1"); return; }

    // ---- 阶段 1：等游戏窗口出现（或进程提前退出）----
    const ULONGLONG startTick = GetTickCount64();
    const ULONGLONG kWindowTimeoutMs = 180000;   // 整合包加载慢也够用
    bool windowSeen = false;
    bool exitedEarly = false;
    while (true) {
        if (WaitForSingleObject(h, 100) == WAIT_OBJECT_0) { exitedEarly = true; break; }
        if (HasVisibleMainWindow(pid)) { windowSeen = true; break; }
        if (GetTickCount64() - startTick > kWindowTimeoutMs) break;
        if (m_gameExit.load() >= 0) { exitedEarly = true; break; }
    }

    if (windowSeen) {
        emit(Ev::GameStart, std::to_string(pid));
    } else if (exitedEarly) {
        // 没等到窗口就退了 → 这算「启动失败」，把崩溃原因带出来
        DWORD code = 0;
        GetExitCodeProcess(h, &code);
        std::string hint = crashHint(m_logPath);
        emit(Ev::InstallError,
             "游戏启动失败（退出码 " + std::to_string((int)code) + "）" +
             (hint.empty() ? "，详见游戏目录 logs\\yxg-launcher.log" : hint));
    } else {
        // 超时但进程还活着：可能还在加载，按「运行中」处理，避免误判失败
        emit(Ev::Info, "游戏窗口还没出现，但进程仍在运行（可能还在加载）");
        emit(Ev::GameStart, std::to_string(pid));
    }

    // ---- 阶段 2：等游戏退出 ----
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    m_gameExit.store((int)code);
    { std::lock_guard<std::mutex> lk(m_gameMu); CloseHandle(h); m_hProcess = nullptr; m_pid = 0; }
    emit(Ev::GameExit, std::to_string(m_gameExit.load()));
}

bool App::gameRunning() const {
    std::lock_guard<std::mutex> lk(m_gameMu);
    return m_hProcess != nullptr;
}

void App::stopGame() {
    std::lock_guard<std::mutex> lk(m_gameMu);
    if (m_hProcess) TerminateProcess(m_hProcess, 1);
}

std::string App::gameLogTail(size_t maxBytes) const {
    if (m_logPath.empty()) return "";
    std::ifstream f(m_logPath, std::ios::binary | std::ios::ate);
    if (!f) return "";
    std::streamoff size = f.tellg();
    std::streamoff read = (std::streamoff)std::min<size_t>((size_t)size, maxBytes);
    if (read <= 0) return "";
    f.seekg(size - read);
    std::string out((size_t)read, '\0');
    f.read(&out[0], read);
    return out;
}

std::vector<java::JavaRuntime> App::detectJava() {
    return java::DetectAll();
}

// ============================================================
// 服务器
// ============================================================
std::vector<std::pair<std::string, int>> App::servers() const {
    return mlc::Store::I().spec().servers;
}

void App::addServer(const std::string& address, int port) {
    if (address.empty()) return;
    auto& sv = mlc::Store::I().spec().servers;
    for (auto& s : sv) if (s.first == address && s.second == port) return;
    sv.push_back({ address, port > 0 ? port : 25565 });
    mlc::Store::I().SaveSettings();
}

void App::removeServer(size_t index) {
    auto& sv = mlc::Store::I().spec().servers;
    if (index < sv.size()) sv.erase(sv.begin() + (ptrdiff_t)index);
    mlc::Store::I().SaveSettings();
}

void App::setDirectServer(const std::string& address, int port) {
    auto& s = mlc::Store::I().spec();
    s.directServer = address;
    s.directPort = port > 0 ? port : 25565;
    mlc::Store::I().SaveSettings();
}

} // namespace launcher
