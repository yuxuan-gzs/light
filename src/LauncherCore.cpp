// LauncherCore.cpp — 启动器核心实现
#include "LauncherCore.h"
#include "Json.h"
#include "Http.h"
#include "Crypto.h"
#include "Zip.h"

#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace mc {

namespace fs = std::filesystem;

// ---------------------------------------------------------------
// 下载源
// ---------------------------------------------------------------
const char* sourceName(DownloadSource s) {
    switch (s) {
        case DownloadSource::Mojang: return "mojang";
        case DownloadSource::Bmclapi: return "bmclapi";
        case DownloadSource::Mcbbs: return "mcbbs";
    }
    return "mojang";
}

std::string sourceBase(DownloadSource s) {
    switch (s) {
        case DownloadSource::Mojang: return "https://launchermeta.mojang.com";
        case DownloadSource::Bmclapi: return "https://bmclapi2.bangbang93.com";
        case DownloadSource::Mcbbs: return "https://download.mcbbs.net";
    }
    return "https://launchermeta.mojang.com";
}

std::string manifestUrl(DownloadSource s) {
    return sourceBase(s) + "/mc/game/version_manifest_v2.json";
}

std::string mirrorUrl(const std::string& url, DownloadSource s) {
    if (s == DownloadSource::Mojang) return url;
    std::string base = sourceBase(s);

    struct Map { const char* from; const char* to; };
    static const Map kMaps[] = {
        {"https://launchermeta.mojang.com/", "/"},
        {"https://launcher.mojang.com/", "/"},
        {"https://piston-meta.mojang.com/", "/"},
        {"https://piston-data.mojang.com/", "/"},
        {"https://libraries.minecraft.net/", "/maven/"},
        {"http://libraries.minecraft.net/", "/maven/"},
        {"https://resources.download.minecraft.net/", "/assets/"},
        {"http://resources.download.minecraft.net/", "/assets/"},
        {"https://files.minecraftforge.net/maven/", "/maven/"},
        {"https://maven.minecraftforge.net/", "/maven/"},
        {"https://bmclapi2.bangbang93.com/", "/"},
        {"https://download.mcbbs.net/", "/"},
    };

    // 若 URL 已经是目标源前缀，直接返回，避免把 base 又拼一层
    if (url.rfind(base, 0) == 0) return url;

    for (const auto& m : kMaps) {
        if (url.rfind(m.from, 0) == 0) {
            std::string rest = url.substr(strlen(m.from));
            return base + m.to + rest;
        }
    }
    return url;
}

// ---------------------------------------------------------------
// 版本清单
// ---------------------------------------------------------------
bool fetchManifest(DownloadSource s, VersionManifest& out, std::string& err) {
    http::Response r = http::Get(manifestUrl(s), 30000);
    if (r.status < 0) { err = "网络错误: " + r.error; return false; }
    if (r.status >= 400) { err = "HTTP " + std::to_string(r.status); return false; }

    json::Value j;
    try { j = json::parse(r.body); }
    catch (const std::exception& e) { err = std::string("JSON 解析失败: ") + e.what(); return false; }

    out.latestRelease = j.get("latest").get("release").asString();
    out.latestSnapshot = j.get("latest").get("snapshot").asString();
    const json::Value& arr = j.get("versions");
    if (arr.isArray()) {
        for (size_t i = 0; i < arr.size(); ++i) {
            const json::Value& e = arr.at(i);
            ManifestVersion mv;
            mv.id = e.get("id").asString();
            mv.type = e.get("type").asString();
            mv.url = e.get("url").asString();
            mv.releaseTime = e.get("releaseTime").asString();
            out.versions.push_back(std::move(mv));
        }
    }
    // 按 releaseTime 降序排序（最新在前），兼容 Mojang/BMCLAPI 等不同源的顺序差异
    std::sort(out.versions.begin(), out.versions.end(),
        [](const ManifestVersion& a, const ManifestVersion& b) {
            return a.releaseTime > b.releaseTime;
        });
    return true;
}

// ---------------------------------------------------------------
// Maven 名解析
// ---------------------------------------------------------------
bool parseMavenName(const std::string& name, std::string& g, std::string& a, std::string& v) {
    size_t p1 = name.find(':');
    if (p1 == std::string::npos) return false;
    size_t p2 = name.find(':', p1 + 1);
    if (p2 == std::string::npos) return false;
    g = name.substr(0, p1);
    a = name.substr(p1 + 1, p2 - p1 - 1);
    v = name.substr(p2 + 1);
    return true;
}

static std::string mavenDir(const std::string& group) {
    std::string s = group;
    std::replace(s.begin(), s.end(), '.', '/');
    return s;
}

std::string Library::mavenPath() const {
    return mavenDir(group) + "/" + artifactName + "/" + version + "/" +
           artifactName + "-" + version + ".jar";
}

std::string Library::classifierPath(const std::string& cls) const {
    return mavenDir(group) + "/" + artifactName + "/" + version + "/" +
           artifactName + "-" + version + "-" + cls + ".jar";
}

// ---------------------------------------------------------------
// OS 信息与规则匹配
// ---------------------------------------------------------------
static std::string osName() { return "windows"; }

static bool is64Bit() {
#if defined(_WIN64)
    return true;
#else
    BOOL wow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &wow64);
    return wow64 != FALSE;
#endif
}
static std::string osArch() { return is64Bit() ? "x86_64" : "x86"; }

static std::string osVersionString() {
    // RtlGetVersion 不受 manifest 影响，返回真实内核版本
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion");
        if (fn) {
            OSVERSIONINFOW oi{};
            oi.dwOSVersionInfoSize = sizeof(oi);
            if (fn(&oi) == 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%lu.%lu", oi.dwMajorVersion, oi.dwMinorVersion);
                return buf;
            }
        }
    }
    return "10.0";
}

static bool regexMatch(const std::string& pattern, const std::string& text) {
    // 注意：Mojang 的 os.version 规则（如 "^10\\."）是“部分匹配”语义（等价 JS RegExp.test），
    // 必须用 regex_search 而非 regex_match（后者要求整串完全匹配）。
    try { return std::regex_search(text, std::regex(pattern)); }
    catch (...) { return false; }
}

bool Library::appliesTo(const std::string& os, const std::string& arch) const {
    if (rules.empty()) return true;
    for (const auto& r : rules) {
        bool match = true;
        if (!r.osName.empty()) match = match && (r.osName == os);
        if (!r.osArch.empty()) match = match && (r.osArch == arch);
        if (!r.osVersion.empty()) match = match && regexMatch(r.osVersion, osVersionString());
        if (match) return r.action == "allow";
    }
    return false; // 有规则但都不匹配 → 不放行
}

// ---------------------------------------------------------------
// 版本 JSON 解析 + 继承合并
// ---------------------------------------------------------------
namespace {

void parseArtifact(const json::Value& j, Artifact& a) {
    a.path = j.get("path").asString();
    a.url = j.get("url").asString();
    a.sha1 = j.get("sha1").asString();
    if (j.has("size")) a.size = j.get("size").asInt();
}

void parseRules(const json::Value& j, std::vector<LibRule>& rules) {
    if (!j.isArray()) return;
    for (size_t i = 0; i < j.size(); ++i) {
        const json::Value& r = j.at(i);
        LibRule lr;
        lr.action = r.get("action").asString();
        const json::Value& os = r.get("os");
        if (os.isObject()) {
            lr.osName = os.get("name").asString();
            lr.osArch = os.get("arch").asString();
            lr.osVersion = os.get("version").asString();
        }
        rules.push_back(std::move(lr));
    }
}

// 解析 arguments.game / arguments.jvm（元素可为字符串或 {rules, value}）
void parseArgList(const json::Value& arr, std::vector<std::string>& out) {
    if (!arr.isArray()) return;
    const std::string os = osName();
    const std::string arch = osArch();
    const std::string ver = osVersionString();
    for (size_t i = 0; i < arr.size(); ++i) {
        const json::Value& e = arr.at(i);
        if (e.isString()) {
            out.push_back(e.asString());
        } else if (e.isObject()) {
            // 规则判断
            std::vector<LibRule> rules;
            parseRules(e.get("rules"), rules);
            bool allowed = true;
            if (!rules.empty()) {
                allowed = false;
                for (const auto& r : rules) {
                    bool match = true;
                    if (!r.osName.empty()) match = match && (r.osName == os);
                    if (!r.osArch.empty()) match = match && (r.osArch == arch);
                    if (!r.osVersion.empty()) match = match && regexMatch(r.osVersion, ver);
                    if (match) { allowed = (r.action == "allow"); break; }
                }
            }
            if (!allowed) continue;
            const json::Value& v = e.get("value");
            if (v.isString()) out.push_back(v.asString());
            else if (v.isArray()) for (size_t k = 0; k < v.size(); ++k) out.push_back(v.at(k).asString());
        }
    }
}

// 从本地 JSON 文件解析单个版本（不展开继承）
bool parseVersionFile(const std::string& path, Version& v, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "无法读取版本 JSON: " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();

    json::Value j;
    try { j = json::parse(ss.str()); }
    catch (const std::exception& e) { err = std::string("JSON 解析失败: ") + e.what(); return false; }

    v.id = j.get("id").asString();
    v.inheritsFrom = j.get("inheritsFrom").asString();
    v.mainClass = j.get("mainClass").asString();
    v.assets = j.get("assets").asString();
    v.minecraftArguments = j.get("minecraftArguments").asString();

    const json::Value& args = j.get("arguments");
    if (args.isObject()) {
        parseArgList(args.get("game"), v.gameArgs);
        parseArgList(args.get("jvm"), v.jvmArgs);
    }

    const json::Value& client = j.get("downloads").get("client");
    if (client.isObject()) parseArtifact(client, v.clientJar);

    const json::Value& ai = j.get("assetIndex");
    if (ai.isObject()) {
        v.assetIndexUrl = ai.get("url").asString();
        v.assetIndexSha1 = ai.get("sha1").asString();
    }

    const json::Value& jv = j.get("javaVersion");
    if (jv.isObject() && jv.has("majorVersion")) v.javaMajor = (int)jv.get("majorVersion").asInt();

    const json::Value& libs = j.get("libraries");
    if (libs.isArray()) {
        for (size_t i = 0; i < libs.size(); ++i) {
            const json::Value& lj = libs.at(i);
            Library lib;
            lib.name = lj.get("name").asString();
            if (!parseMavenName(lib.name, lib.group, lib.artifactName, lib.version)) continue;
            parseRules(lj.get("rules"), lib.rules);

            const json::Value& natives = lj.get("natives");
            if (natives.isObject()) lib.nativesKey = natives.get("windows").asString();

            const json::Value& ex = lj.get("extract").get("exclude");
            if (ex.isArray()) for (size_t k = 0; k < ex.size(); ++k) lib.extractExclude.push_back(ex.at(k).asString());

            const json::Value& dl = lj.get("downloads");
            if (dl.isObject()) {
                const json::Value& art = dl.get("artifact");
                if (art.isObject()) parseArtifact(art, lib.artifact);
                const json::Value& cls = dl.get("classifiers");
                if (cls.isObject()) {
                    for (const auto& [key, val] : cls.obj) {
                        Artifact a; parseArtifact(val, a);
                        lib.classifiers[key] = a;
                    }
                }
            }
            v.libraries.push_back(std::move(lib));
        }
    }

    const json::Value& logc = j.get("logging").get("client").get("file");
    if (logc.isObject()) {
        v.loggingClientUrl = logc.get("url").asString();
        v.loggingClientSha1 = logc.get("sha1").asString();
    }
    return true;
}

// 递归读取并合并继承
bool loadVersionRec(const std::string& versionsDir, const std::string& id,
                    Version& out, std::string& err, int depth) {
    if (depth > 16) { err = "继承层级过深"; return false; }
    fs::path jsonPath = fs::u8path(versionsDir) / fs::u8path(id) / fs::u8path(id + ".json");

    Version cur;
    if (!parseVersionFile(jsonPath.u8string(), cur, err)) return false;

    if (cur.inheritsFrom.empty()) {
        out = std::move(cur);
        out.clientJarId = out.id;
        return true;
    }

    Version parent;
    if (!loadVersionRec(versionsDir, cur.inheritsFrom, parent, err, depth + 1)) return false;

    // 合并：以 parent 为基础，cur 覆盖
    out = std::move(parent);
    out.id = cur.id;
    out.inheritsFrom = cur.inheritsFrom;

    if (!cur.mainClass.empty()) out.mainClass = cur.mainClass;
    if (!cur.assets.empty()) out.assets = cur.assets;
    if (!cur.minecraftArguments.empty() || !cur.gameArgs.empty() || !cur.jvmArgs.empty()) {
        out.minecraftArguments = cur.minecraftArguments;
        out.gameArgs = cur.gameArgs;
        out.jvmArgs = cur.jvmArgs;
    }
    if (!cur.assetIndexUrl.empty()) { out.assetIndexUrl = cur.assetIndexUrl; out.assetIndexSha1 = cur.assetIndexSha1; }
    if (cur.javaMajor > 0) out.javaMajor = cur.javaMajor;
    if (!cur.clientJar.url.empty()) { out.clientJar = cur.clientJar; out.clientJarId = cur.id; }
    if (!cur.loggingClientUrl.empty()) { out.loggingClientUrl = cur.loggingClientUrl; out.loggingClientSha1 = cur.loggingClientSha1; }
    // libraries 追加
    for (auto& l : cur.libraries) out.libraries.push_back(std::move(l));
    return true;
}

} // namespace

bool loadVersion(const std::string& versionsDir, const std::string& id,
                 Version& out, std::string& err) {
    return loadVersionRec(versionsDir, id, out, err, 0);
}

// ---------------------------------------------------------------
// 参数替换与命令构建
// ---------------------------------------------------------------
std::string substituteArg(const std::string& in, const Version& v, const LaunchOptions& opt,
                          const std::string& classpath, const std::string& nativesDir) {
    std::string s = in;
    auto rep = [&](const char* key, const std::string& val) {
        std::string k = std::string("${") + key + "}";
        size_t pos = 0;
        while ((pos = s.find(k, pos)) != std::string::npos) {
            s.replace(pos, k.size(), val);
            pos += val.size();
        }
    };
    std::string assetsRoot = (fs::u8path(opt.gameDir) / "assets").u8string();
    std::string libDir = (fs::u8path(opt.gameDir) / "libraries").u8string();
    std::string dataDir = opt.dataDir.empty() ? opt.gameDir : opt.dataDir;

    rep("auth_player_name", opt.authName);
    rep("version_name", v.id);
    rep("game_directory", dataDir);
    rep("assets_root", assetsRoot);
    rep("game_assets", assetsRoot);
    rep("assets_index_name", v.assets);
    rep("auth_uuid", opt.authUuid);
    rep("auth_access_token", opt.authToken);
    rep("auth_session", "token:" + opt.authToken + ":" + opt.authUuid);
    rep("user_type", opt.userType);
    rep("version_type", opt.versionType);
    rep("user_properties", "{}");
    rep("resolution_width", std::to_string(opt.width));
    rep("resolution_height", std::to_string(opt.height));
    rep("natives_directory", nativesDir);
    rep("classpath", classpath);
    rep("classpath_separator", ";");
    rep("launcher_name", "YXG Minecraft Launcher");
    rep("launcher_version", "1.0.0");
    rep("library_directory", libDir);
    rep("clientid", "");
    rep("auth_xuid", "");
    return s;
}

std::string buildClasspath(const Version& v, const std::string& gameDir) {
    const std::string os = osName();
    const std::string arch = osArch();
    std::string cp;
    fs::path libDir = fs::u8path(gameDir) / "libraries";
    fs::path verDir = fs::u8path(gameDir) / "versions";

    for (const auto& lib : v.libraries) {
        if (!lib.appliesTo(os, arch)) continue;
        if (!lib.hasMainArtifact()) continue; // 纯 natives 库不进 classpath
        fs::path p = libDir / fs::u8path(lib.mavenPath());
        if (!cp.empty()) cp += ";";
        cp += p.u8string();
    }
    // client jar
    fs::path jar = verDir / fs::u8path(v.clientJarId) / fs::u8path(v.clientJarId + ".jar");
    if (!cp.empty()) cp += ";";
    cp += jar.u8string();
    return cp;
}

bool buildLaunchCommand(const Version& v, const LaunchOptions& opt,
                        std::vector<std::string>& outArgs, std::string& err) {
    fs::path nativesDir = fs::u8path(opt.gameDir) / "versions" / fs::u8path(v.id) / "natives";
    std::string classpath = buildClasspath(v, opt.gameDir);
    std::string natives = nativesDir.u8string();

    std::vector<std::string> jvm;
    // 内存
    int minMem = std::min(opt.maxMemoryMB, 512);
    if (minMem < 128) minMem = 128;
    jvm.push_back("-Xmx" + std::to_string(opt.maxMemoryMB) + "M");
    jvm.push_back("-Xms" + std::to_string(minMem) + "M");
    for (const auto& a : opt.extraJvmArgs) jvm.push_back(a);
    jvm.push_back("-Djava.library.path=" + natives);

    bool modern = !v.jvmArgs.empty() || !v.gameArgs.empty();
    if (modern) {
        for (const auto& a : v.jvmArgs) jvm.push_back(substituteArg(a, v, opt, classpath, natives));
    } else {
        jvm.push_back("-cp");
        jvm.push_back(classpath);
        jvm.push_back("-Dminecraft.launcher.brand=${launcher_name}");
        jvm.push_back("-Dminecraft.launcher.version=${launcher_version}");
        // 上面两条也要替换
        for (auto& a : jvm) a = substituteArg(a, v, opt, classpath, natives);
    }

    std::vector<std::string> game;
    if (modern) {
        for (const auto& a : v.gameArgs) game.push_back(substituteArg(a, v, opt, classpath, natives));
    } else if (!v.minecraftArguments.empty()) {
        std::istringstream iss(v.minecraftArguments);
        std::string tok;
        while (iss >> tok) game.push_back(substituteArg(tok, v, opt, classpath, natives));
    }
    for (const auto& a : opt.extraGameArgs) game.push_back(a);
    if (!opt.server.empty()) {
        game.push_back("--server");
        game.push_back(opt.server);
        game.push_back("--port");
        game.push_back(std::to_string(opt.serverPort));
    }

    if (v.mainClass.empty()) { err = "版本缺少 mainClass"; return false; }

    outArgs.clear();
    for (auto& a : jvm) outArgs.push_back(std::move(a));
    outArgs.push_back(v.mainClass);
    for (auto& a : game) outArgs.push_back(std::move(a));
    return true;
}

// ---------------------------------------------------------------
// 资源索引
// ---------------------------------------------------------------
bool parseAssetIndex(const std::string& jsonText, AssetIndex& out) {
    json::Value j;
    try { j = json::parse(jsonText); }
    catch (...) { return false; }
    const json::Value& objs = j.get("objects");
    if (!objs.isObject()) return false;
    for (const auto& [name, val] : objs.obj) {
        AssetObject o;
        o.hash = val.get("hash").asString();
        o.size = val.get("size").asInt();
        out[name] = o;
    }
    return true;
}

// ---------------------------------------------------------------
// 安装编排
// ---------------------------------------------------------------
namespace {

bool fileExists(const std::string& p) {
    std::error_code ec;
    return fs::exists(fs::u8path(p), ec) && !ec;
}

// 生成候选下载 URL（HMCL 式多源回退：当前源 + BMCLAPI 镜像 + 官方）
static std::vector<std::string> candidateUrls(const std::string& officialUrl, DownloadSource src) {
    std::vector<std::string> out;
    auto add = [&](const std::string& u) {
        if (u.empty()) return;
        for (auto& x : out) if (x == u) return;
        out.push_back(u);
    };
    add(mirrorUrl(officialUrl, src));                          // 当前源
    if (src != DownloadSource::Bmclapi) add(mirrorUrl(officialUrl, DownloadSource::Bmclapi)); // BMCLAPI 回退
    if (src != DownloadSource::Mojang) add(officialUrl);       // 官方回退
    return out;
}

// 下载单文件（已存在则跳过；失败自动回退到镜像/官方，HMCL 式）
bool downloadOne(const std::string& officialUrl, DownloadSource src, const std::string& dest,
                 const std::string* sha1, const std::function<bool(const InstallProgress&)>& progress,
                 InstallProgress& p, std::string& err) {
    if (fileExists(dest)) return true;
    auto urls = candidateUrls(officialUrl, src);
    std::string lastErr;
    for (const auto& u : urls) {
        http::DownloadResult r = http::Download(u, dest, nullptr, 120000, sha1);
        if (r.ok) return true;
        lastErr = r.error;
    }
    err = "下载失败 " + dest + ": " + lastErr;
    return false;
}

// 递归确保版本 JSON 已下载（处理 inheritsFrom）
bool ensureVersionJson(const std::string& versionsDir, const VersionManifest& mf,
                       const std::string& id, DownloadSource src,
                       const std::function<bool(const InstallProgress&)>& progress,
                       InstallProgress& p, std::string& err) {
    fs::path jsonPath = fs::u8path(versionsDir) / fs::u8path(id) / fs::u8path(id + ".json");
    if (!fileExists(jsonPath.u8string())) {
        // 找 manifest 条目
        const ManifestVersion* entry = nullptr;
        for (const auto& e : mf.versions) if (e.id == id) { entry = &e; break; }
        if (!entry) { err = "版本清单中不存在: " + id; return false; }

        p.phase = "version";
        p.current = id + ".json";
        if (progress && !progress(p)) { err = "已取消"; return false; }

        std::error_code ec;
        fs::create_directories(jsonPath.parent_path(), ec);
        auto urls = candidateUrls(entry->url, src);
        http::DownloadResult r;
        bool ok = false;
        for (const auto& u : urls) {
            r = http::Download(u, jsonPath.u8string(), nullptr, 60000, nullptr);
            if (r.ok) { ok = true; break; }
        }
        if (!ok) { err = "下载版本 JSON 失败: " + r.error; return false; }
    }

    // 检查 inheritsFrom
    std::ifstream f(jsonPath, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    try {
        json::Value j = json::parse(ss.str());
        std::string parent = j.get("inheritsFrom").asString();
        if (!parent.empty()) return ensureVersionJson(versionsDir, mf, parent, src, progress, p, err);
    } catch (const std::exception& e) { err = std::string("JSON 解析失败: ") + e.what(); return false; }
    return true;
}

} // namespace

bool installVersion(const std::string& versionsDir, const std::string& librariesDir,
                    const std::string& assetsDir, const VersionManifest& mf,
                    const std::string& versionId, DownloadSource src,
                    const std::function<bool(const InstallProgress&)>& progress,
                    std::string& err) {
    InstallProgress p;
    p.phase = "version";

    // 1. 确保版本 JSON 链下载
    if (!ensureVersionJson(versionsDir, mf, versionId, src, progress, p, err)) return false;

    // 2. 解析合并后的版本
    Version v;
    if (!loadVersion(versionsDir, versionId, v, err)) return false;

    // 3. 下载 client jar
    if (!v.clientJar.url.empty()) {
        fs::path jarPath = fs::u8path(versionsDir) / fs::u8path(v.clientJarId) / fs::u8path(v.clientJarId + ".jar");
        p.phase = "version";
        p.current = v.clientJarId + ".jar";
        if (progress && !progress(p)) { err = "已取消"; return false; }
        std::error_code ec; fs::create_directories(jarPath.parent_path(), ec);
        if (!downloadOne(v.clientJar.url, src, jarPath.u8string(),
                         v.clientJar.sha1.empty() ? nullptr : &v.clientJar.sha1,
                         progress, p, err)) return false;
    }

    // 4. 下载 libraries
    const std::string os = osName();
    const std::string arch = osArch();
    std::vector<Library> applicable;
    for (const auto& l : v.libraries) if (l.appliesTo(os, arch)) applicable.push_back(l);

    p.phase = "libraries";
    p.total = (int64_t)applicable.size();
    p.done = 0;
    for (const auto& lib : applicable) {
        ++p.done;
        p.current = lib.artifactName + "-" + lib.version;
        if (progress && !progress(p)) { err = "已取消"; return false; }

        // 主 jar
        if (lib.hasMainArtifact()) {
            std::string officialUrl = lib.artifact.url.empty()
                ? ("https://libraries.minecraft.net/" + lib.mavenPath())
                : lib.artifact.url;
            fs::path dest = fs::u8path(librariesDir) / fs::u8path(lib.mavenPath());
            std::error_code ec; fs::create_directories(dest.parent_path(), ec);
            if (!downloadOne(officialUrl, src, dest.u8string(),
                             lib.artifact.sha1.empty() ? nullptr : &lib.artifact.sha1,
                             progress, p, err)) return false;
        }

        // natives classifier
        if (!lib.nativesKey.empty()) {
            auto it = lib.classifiers.find(lib.nativesKey);
            if (it != lib.classifiers.end() || !lib.group.empty()) {
                std::string officialUrl = (it != lib.classifiers.end() && !it->second.url.empty())
                    ? it->second.url
                    : ("https://libraries.minecraft.net/" + lib.classifierPath(lib.nativesKey));
                const std::string* sha = (it != lib.classifiers.end() && !it->second.sha1.empty())
                    ? &it->second.sha1 : nullptr;
                fs::path dest = fs::u8path(librariesDir) / fs::u8path(lib.classifierPath(lib.nativesKey));
                std::error_code ec; fs::create_directories(dest.parent_path(), ec);
                if (!downloadOne(officialUrl, src, dest.u8string(), sha, progress, p, err)) return false;

                // 解压到 natives 目录
                fs::path nativesDir = fs::u8path(versionsDir) / fs::u8path(v.id) / "natives";
                fs::create_directories(nativesDir, ec);
                std::string exclude = "META-INF/";
                if (!zip::ExtractAll(dest.u8string(), nativesDir.u8string(), exclude)) {
                    // 解压失败不致命（老版本可能无 natives jar 内容），记录但不中断
                }
            }
        }
    }

    // 5. 下载 asset index + assets
    if (!v.assets.empty()) {
        fs::path indexPath = fs::u8path(assetsDir) / "indexes" / fs::u8path(v.assets + ".json");
        p.phase = "assets";
        p.current = "asset index " + v.assets;
        if (progress && !progress(p)) { err = "已取消"; return false; }
        std::error_code ec; fs::create_directories(indexPath.parent_path(), ec);

        if (!fileExists(indexPath.u8string()) && !v.assetIndexUrl.empty()) {
            if (!downloadOne(v.assetIndexUrl, src, indexPath.u8string(),
                             v.assetIndexSha1.empty() ? nullptr : &v.assetIndexSha1,
                             progress, p, err)) return false;
        }

        AssetIndex ai;
        if (fileExists(indexPath.u8string())) {
            std::ifstream f(indexPath, std::ios::binary);
            std::stringstream ss; ss << f.rdbuf();
            parseAssetIndex(ss.str(), ai);
        }

        p.total = (int64_t)ai.size();
        p.done = 0;
        for (const auto& [name, obj] : ai) {
            ++p.done;
            p.current = name;
            if (progress && !progress(p)) { err = "已取消"; return false; }
            std::string h = obj.hash;
            if (h.size() < 2) continue;
            std::string sub = h.substr(0, 2);
            fs::path dest = fs::u8path(assetsDir) / "objects" / fs::u8path(sub) / fs::u8path(h);
            std::error_code ec2; fs::create_directories(dest.parent_path(), ec2);
            if (fileExists(dest.u8string())) continue;
            std::string officialUrl = "https://resources.download.minecraft.net/" + sub + "/" + h;
            if (!downloadOne(officialUrl, src, dest.u8string(), &h, progress, p, err)) return false;
        }
    }

    p.finished = true;
    if (progress) progress(p);
    return true;
}

} // namespace mc
