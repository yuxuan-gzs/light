// LauncherCore.cpp — 启动器核心实现（下载编排 / 版本模型 / 启动参数）
//
// 与 HMCL 对齐的关键点：
//  * 库路径一律以 downloads.artifact.path 为准（maven 坐标只是兜底），
//    否则带 classifier 的库（org.lwjgl:lwjgl:3.3.3:natives-windows）会被写到
//    错误的位置，把主 jar 覆盖成 natives jar —— 这是旧版启动必崩的根因。
//  * natives 的 classifier 里可能含 ${arch}，必须替换成 64/32。
//  * 1.19+ 的 natives 是普通库（downloads.artifact + classifier 名），要进 classpath，
//    不能解压；只有带 natives 字段的老格式才解压到 natives 目录。
//  * 下载失败不再静默：sha1 不匹配/文件损坏会重下，失败项会被汇总上报。
#include "LauncherCore.h"
#include "Json.h"
#include "Http.h"
#include "Crypto.h"
#include "Zip.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace mc {

namespace fs = std::filesystem;

// 并发下载线程数（库/资源分别取值；可用「全局设置 → 下载线程数」调整基础值）
static std::atomic<int> g_dlThreads{ 8 };
static constexpr int kMaxLibWorkers = 24;
static constexpr int kMaxAssetWorkers = 48;
// 单次请求超时：原来是 60s，一个挂住的镜像会让该线程白等 1 分钟（下载慢的主因之一）
static constexpr int kDownloadTimeoutMs = 20000;
static constexpr int kMetaTimeoutMs = 20000;

void setDownloadConcurrency(int n) {
    if (n < 1) n = 1;
    if (n > 32) n = 32;
    g_dlThreads.store(n);
}

static int libWorkers() {
    return std::clamp(g_dlThreads.load(), 4, kMaxLibWorkers);
}
// 资源文件都是几 KB，多开线程收益最大
static int assetWorkers() {
    return std::clamp(g_dlThreads.load() * 2, 8, kMaxAssetWorkers);
}

// ---------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------
static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static bool fileExists(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(fs::u8path(p), ec) && !ec;
}

static int64_t fileSize(const std::string& p) {
    std::error_code ec;
    auto n = fs::file_size(fs::u8path(p), ec);
    return ec ? -1 : (int64_t)n;
}

static std::string pathJoin(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '\\' || last == '/') return a + b;
    return a + "\\" + b;
}

static std::string urlBaseName(const std::string& url) {
    std::string u = url;
    size_t q = u.find('?');
    if (q != std::string::npos) u = u.substr(0, q);
    size_t s = u.find_last_of('/');
    return (s == std::string::npos) ? u : u.substr(s + 1);
}

// 文件是否「完好」：知道大小就先比大小（省掉几十 MB 的哈希，重复安装/修复快很多），
// 不知道大小才退化成全量 sha1
static bool fileLooksOk(const std::string& path, const std::string& sha1, int64_t expectSize = -1) {
    if (!fileExists(path)) return false;
    if (expectSize > 0) {
        if (fileSize(path) != expectSize) return false;
        return true;              // 大小一致视为完好（HMCL 也是这么判断的）
    }
    if (sha1.size() != 40) return true;
    std::string got = crypto::sha1File(path);
    return toLower(got) == toLower(sha1);
}

static void removeQuiet(const std::string& p) {
    std::error_code ec;
    fs::remove(fs::u8path(p), ec);
}

static void ensureParentDir(const std::string& file) {
    std::error_code ec;
    fs::path p = fs::u8path(file);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
}

static void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// ---------------------------------------------------------------
// 下载源
// ---------------------------------------------------------------
const char* sourceName(DownloadSource s) {
    switch (s) {
        case DownloadSource::Auto: return "auto";
        case DownloadSource::Bmclapi: return "bmclapi";
        case DownloadSource::Mojang: return "mojang";
    }
    return "auto";
}

std::string sourceBase(DownloadSource s) {
    switch (s) {
        case DownloadSource::Mojang: return "https://launchermeta.mojang.com";
        case DownloadSource::Auto:
        case DownloadSource::Bmclapi: return "https://bmclapi2.bangbang93.com";
    }
    return "https://bmclapi2.bangbang93.com";
}

std::string manifestUrl(DownloadSource s) {
    return sourceBase(s) + "/mc/game/version_manifest_v2.json";
}

std::string mirrorUrl(const std::string& url, DownloadSource s) {
    if (s == DownloadSource::Mojang) return url;
    const std::string base = sourceBase(s);
    if (url.rfind(base, 0) == 0) return url;

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
    };
    for (const auto& m : kMaps) {
        if (url.rfind(m.from, 0) == 0) {
            return base + m.to + url.substr(strlen(m.from));
        }
    }
    return url;
}

// 生成候选下载 URL（当前源 → BMCLAPI → 官方），去重
static std::vector<std::string> candidateUrls(const std::string& officialUrl, DownloadSource src) {
    std::vector<std::string> out;
    auto add = [&](const std::string& u) {
        if (u.empty()) return;
        for (auto& x : out) if (x == u) return;
        out.push_back(u);
    };
    if (officialUrl.empty()) return out;
    add(mirrorUrl(officialUrl, src));
    add(mirrorUrl(officialUrl, DownloadSource::Bmclapi));
    add(officialUrl);
    return out;
}

// ---------------------------------------------------------------
// 版本清单
// ---------------------------------------------------------------
bool fetchManifest(DownloadSource s, VersionManifest& out, std::string& err) {
    std::string lastErr;
    auto tryOnce = [&](DownloadSource src) -> bool {
        http::Response r = http::Get(manifestUrl(src), 30000);
        if (r.status < 0) { lastErr = "网络错误: " + r.error; return false; }
        if (r.status >= 400) { lastErr = "HTTP " + std::to_string(r.status); return false; }

        json::Value j;
        try { j = json::parse(r.body); }
        catch (const std::exception& e) { lastErr = std::string("JSON 解析失败: ") + e.what(); return false; }

        out.latestRelease = j.get("latest").get("release").asString();
        out.latestSnapshot = j.get("latest").get("snapshot").asString();
        out.versions.clear();
        const json::Value& arr = j.get("versions");
        if (arr.isArray()) {
            for (size_t i = 0; i < arr.size(); ++i) {
                const json::Value& e = arr.at(i);
                ManifestVersion mv;
                mv.id = e.get("id").asString();
                mv.type = e.get("type").asString();
                mv.url = e.get("url").asString();
                mv.releaseTime = e.get("releaseTime").asString();
                if (!mv.id.empty()) out.versions.push_back(std::move(mv));
            }
        }
        std::sort(out.versions.begin(), out.versions.end(),
            [](const ManifestVersion& a, const ManifestVersion& b) {
                return a.releaseTime > b.releaseTime;
            });
        return !out.versions.empty();
    };

    if (tryOnce(s)) return true;
    if (s != DownloadSource::Bmclapi && tryOnce(DownloadSource::Bmclapi)) return true;
    if (s != DownloadSource::Mojang && tryOnce(DownloadSource::Mojang)) return true;
    err = lastErr;
    return false;
}

// ---------------------------------------------------------------
// Maven 名解析
// ---------------------------------------------------------------
bool parseMavenName(const std::string& name, std::string& g, std::string& a, std::string& v) {
    std::string cls;
    parseMavenArtifact(name, g, a, v, cls);
    return !g.empty() && !a.empty() && !v.empty();
}

void parseMavenArtifact(const std::string& name, std::string& g, std::string& a,
                        std::string& v, std::string& classifier) {
    g.clear(); a.clear(); v.clear(); classifier.clear();
    // 去掉可能的 @ext 后缀
    std::string n = name;
    size_t at = n.find('@');
    if (at != std::string::npos) n = n.substr(0, at);

    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t p = n.find(':', start);
        if (p == std::string::npos) { parts.push_back(n.substr(start)); break; }
        parts.push_back(n.substr(start, p - start));
        start = p + 1;
    }
    if (parts.size() < 3) return;
    g = parts[0];
    a = parts[1];
    v = parts[2];
    if (parts.size() >= 4) classifier = parts[3];
}

static std::string mavenDir(const std::string& group) {
    std::string s = group;
    std::replace(s.begin(), s.end(), '.', '/');
    return s;
}

std::string Library::mavenPath() const {
    std::string file = artifactName + "-" + version;
    if (!nameClassifier.empty()) file += "-" + nameClassifier;
    return mavenDir(group) + "/" + artifactName + "/" + version + "/" + file + ".jar";
}

std::string Library::classifierPath(const std::string& cls) const {
    return mavenDir(group) + "/" + artifactName + "/" + version + "/" +
           artifactName + "-" + version + "-" + cls + ".jar";
}

std::string Library::filePath() const {
    // downloads.artifact.path 是唯一权威路径（含 classifier、含 lzma 之类的非标准命名）
    if (!artifact.path.empty()) return artifact.path;
    if (group.empty() || artifactName.empty() || version.empty()) return std::string();
    return mavenPath();
}

// natives jar 的实际相对路径（classifiers 里给了 path 就优先用它）
static std::string nativeJarRel(const Library& lib) {
    auto it = lib.classifiers.find(lib.nativeClassifier);
    if (it != lib.classifiers.end() && !it->second.path.empty()) return it->second.path;
    return lib.classifierPath(lib.nativeClassifier);
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
static std::string osBits() { return is64Bit() ? "64" : "32"; }

static std::string osVersionString() {
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
    try { return std::regex_search(text, std::regex(pattern)); }
    catch (...) { return false; }
}

// 启动器的「能力」，决定 JSON 里 features 规则是否命中（对应 HMCL 的 getFeatures()）：
//   has_custom_resolution = 我们总能给出宽高（设置里一定有值）
//   demo / quickPlay*      = 不支持，必须为 false ——
//   否则 1.20.2+ 的 --quickPlaySingleplayer/--quickPlayMultiplayer/--quickPlayRealms
//   会同时进入命令行，客户端直接抛 "Only one quick play option can be specified"。
static bool featureEnabled(const std::string& name) {
    static const std::map<std::string, bool> kFeat = {
        { "is_demo_user", false },
        { "has_custom_resolution", true },
        { "has_quick_plays_support", false },
        { "is_quick_play_singleplayer", false },
        { "is_quick_play_multiplayer", false },
        { "is_quick_play_realms", false },
    };
    auto it = kFeat.find(name);
    return it != kFeat.end() && it->second;
}

static bool ruleMatches(const LibRule& r, const std::string& os, const std::string& arch,
                        const std::string& ver) {
    if (!r.osName.empty() && r.osName != os) return false;
    if (!r.osArch.empty() && r.osArch != arch) return false;
    if (!r.osVersion.empty() && !regexMatch(r.osVersion, ver)) return false;
    for (const auto& kv : r.features) {
        if (featureEnabled(kv.first) != kv.second) return false;
    }
    return true;
}

bool Library::appliesTo(const std::string& os, const std::string& arch) const {
    if (rules.empty()) return true;
    // 规则是「按顺序取第一条命中的」；都没命中 → 不允许
    const std::string ver = osVersionString();
    for (const auto& r : rules) {
        if (ruleMatches(r, os, arch, ver)) return r.action == "allow";
    }
    return false;
}

// ---------------------------------------------------------------
// 版本 JSON 解析 + 继承合并
// ---------------------------------------------------------------
namespace {

void parseArtifact(const json::Value& j, Artifact& a) {
    if (!j.isObject()) return;
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
        const json::Value& feats = r.get("features");
        if (feats.isObject()) {
            for (const auto& kv : feats.obj) lr.features[kv.first] = kv.second.asBool();
        }
        rules.push_back(std::move(lr));
    }
}

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
            std::vector<LibRule> rules;
            parseRules(e.get("rules"), rules);
            bool allowed = true;
            if (!rules.empty()) {
                allowed = false;
                for (const auto& r : rules) {
                    if (ruleMatches(r, os, arch, ver)) { allowed = (r.action == "allow"); break; }
                }
            }
            if (!allowed) continue;
            const json::Value& v = e.get("value");
            if (v.isString()) out.push_back(v.asString());
            else if (v.isArray()) for (size_t k = 0; k < v.size(); ++k) out.push_back(v.at(k).asString());
        }
    }
}

// 解析单个 library（含 natives 分类器、规则、下载信息）
Library parseLibrary(const json::Value& lj) {
    Library lib;
    lib.name = lj.get("name").asString();
    parseMavenArtifact(lib.name, lib.group, lib.artifactName, lib.version, lib.nameClassifier);
    parseRules(lj.get("rules"), lib.rules);

    const json::Value& dl = lj.get("downloads");
    if (dl.isObject()) {
        parseArtifact(dl.get("artifact"), lib.artifact);
        const json::Value& cls = dl.get("classifiers");
        if (cls.isObject()) {
            for (const auto& kv : cls.obj) {
                Artifact a;
                parseArtifact(kv.second, a);
                if (a.url.empty() && a.path.empty()) continue;
                lib.classifiers[kv.first] = a;
            }
        }
    }

    const json::Value& ex = lj.get("extract").get("exclude");
    if (ex.isArray()) for (size_t k = 0; k < ex.size(); ++k) lib.extractExclude.push_back(ex.at(k).asString());

    // ---- natives 分类器 ----
    const json::Value& natives = lj.get("natives");
    if (natives.isObject()) {
        std::string key = natives.get("windows").asString();
        if (!key.empty()) {
            replaceAll(key, "${arch}", osBits());
            lib.nativesKey = key;
            // 取实际的 classifier：优先原值，其次去掉 arch 后缀，最后找一个 native* 开头的
            if (lib.classifiers.count(key)) {
                lib.nativeClassifier = key;
            } else {
                std::string stripped = key;
                replaceAll(stripped, "-" + osBits(), "");
                replaceAll(stripped, "natives-", "natives-");
                if (lib.classifiers.count(stripped)) lib.nativeClassifier = stripped;
                if (lib.nativeClassifier.empty()) {
                    for (const auto& kv : lib.classifiers) {
                        if (kv.first.rfind("native", 0) == 0) { lib.nativeClassifier = kv.first; break; }
                    }
                }
                if (lib.nativeClassifier.empty()) lib.nativeClassifier = key;  // 无 classifiers 的老 JSON，按名字拼
            }
        }
    } else if (!lib.classifiers.empty()) {
        // 少数整合包把 natives 放在 classifiers 里但没写 natives 字段
        for (const auto& kv : lib.classifiers) {
            std::string k = kv.first;
            if (k.find("windows") != std::string::npos && k.rfind("native", 0) == 0) {
                lib.nativeClassifier = k;
                lib.nativesKey = k;
                break;
            }
        }
    }
    return lib;
}

bool parseVersionFile(const std::string& path, Version& v, std::string& err) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
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

    parseArtifact(j.get("downloads").get("client"), v.clientJar);

    const json::Value& ai = j.get("assetIndex");
    if (ai.isObject()) {
        v.assetIndexUrl = ai.get("url").asString();
        v.assetIndexSha1 = ai.get("sha1").asString();
        if (v.assets.empty()) v.assets = ai.get("id").asString();
    }

    const json::Value& jv = j.get("javaVersion");
    if (jv.isObject() && jv.has("majorVersion")) {
        int mj = (int)jv.get("majorVersion").asInt();
        if (mj > 0) v.javaMajor = mj;
    }

    const json::Value& libs = j.get("libraries");
    if (libs.isArray()) {
        for (size_t i = 0; i < libs.size(); ++i) {
            Library lib = parseLibrary(libs.at(i));
            if (lib.group.empty() || lib.artifactName.empty() || lib.version.empty()) continue;
            v.libraries.push_back(std::move(lib));
        }
    }

    const json::Value& logc = j.get("logging").get("client").get("file");
    if (logc.isObject()) {
        v.loggingClientUrl = logc.get("url").asString();
        v.loggingClientSha1 = logc.get("sha1").asString();
        std::string nm = urlBaseName(v.loggingClientUrl);
        if (!nm.empty()) v.loggingClientPath = "log_configs/" + nm;
    }
    return true;
}

bool loadVersionRec(const std::string& versionsDir, const std::string& id,
                    Version& out, std::string& err, int depth) {
    if (depth > 16) { err = "继承层级过深（可能有循环 inheritsFrom）"; return false; }
    fs::path jsonPath = fs::u8path(versionsDir) / fs::u8path(id) / fs::u8path(id + ".json");

    Version cur;
    if (!parseVersionFile(jsonPath.u8string(), cur, err)) return false;

    if (cur.inheritsFrom.empty()) {
        out = std::move(cur);
        if (out.id.empty()) out.id = id;
        out.clientJarId = out.id;
        return true;
    }

    Version parent;
    if (!loadVersionRec(versionsDir, cur.inheritsFrom, parent, err, depth + 1)) return false;

    out = std::move(parent);
    out.id = cur.id.empty() ? id : cur.id;
    out.inheritsFrom = cur.inheritsFrom;

    if (!cur.mainClass.empty()) out.mainClass = cur.mainClass;
    if (!cur.assets.empty()) out.assets = cur.assets;
    // 子版本一旦给了自己的参数，就整体覆盖（Mojang 规范）
    if (!cur.minecraftArguments.empty() || !cur.gameArgs.empty() || !cur.jvmArgs.empty()) {
        out.minecraftArguments = cur.minecraftArguments;
        out.gameArgs = cur.gameArgs;
        out.jvmArgs = cur.jvmArgs;
    }
    if (!cur.assetIndexUrl.empty()) {
        out.assetIndexUrl = cur.assetIndexUrl;
        out.assetIndexSha1 = cur.assetIndexSha1;
    }
    if (cur.javaMajor > 0) out.javaMajor = cur.javaMajor;
    if (!cur.clientJar.url.empty() || !cur.clientJar.path.empty()) {
        out.clientJar = cur.clientJar;
        out.clientJarId = out.id;
    }
    if (!cur.loggingClientUrl.empty()) {
        out.loggingClientUrl = cur.loggingClientUrl;
        out.loggingClientSha1 = cur.loggingClientSha1;
        out.loggingClientPath = cur.loggingClientPath;
    }
    // 子版本库排在父版本库之后（覆盖同名库时后者生效 → 这里保持父在前、子在后的语义）
    for (auto& l : cur.libraries) out.libraries.push_back(std::move(l));
    return true;
}

} // namespace

bool loadVersion(const std::string& versionsDir, const std::string& id,
                 Version& out, std::string& err) {
    out = Version{};
    return loadVersionRec(versionsDir, id, out, err, 0);
}

std::string baseVersionId(const std::string& versionsDir, const std::string& id) {
    auto readAll = [](const std::string& path) {
        std::ifstream f(std::wstring(http::Utf8ToWide(path)), std::ios::binary);
        if (!f) return std::string();
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return s;
    };
    std::string cur = id;
    for (int depth = 0; depth < 16; ++depth) {
        fs::path p = fs::u8path(versionsDir) / fs::u8path(cur) / fs::u8path(cur + ".json");
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) return depth == 0 ? std::string() : cur;
        std::string text = readAll(p.u8string());
        if (text.empty()) return depth == 0 ? std::string() : cur;
        std::string parent;
        try {
            json::Value j = json::parse(text);
            parent = j.get("inheritsFrom").asString();
        } catch (...) {
            return depth == 0 ? std::string() : cur;
        }
        if (parent.empty()) return cur;
        cur = parent;
    }
    return cur;
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
    fs::path assetsRootP = fs::u8path(opt.gameDir) / "assets";
    std::string assetsRoot = assetsRootP.u8string();
    std::string libDir = (fs::u8path(opt.gameDir) / "libraries").u8string();
    std::string dataDir = opt.dataDir.empty() ? opt.gameDir : opt.dataDir;

    // 1.5/1.6 的虚拟资源目录（存在才用，避免把正常版本带偏）
    std::string gameAssets = assetsRoot;
    if (!v.assets.empty()) {
        std::error_code ec;
        fs::path virt = assetsRootP / "virtual" / fs::u8path(v.assets);
        if (fs::is_directory(virt, ec)) gameAssets = virt.u8string();
    }

    rep("auth_player_name", opt.authName);
    rep("version_name", v.id);
    rep("profile_name", v.id);
    rep("game_directory", dataDir);
    rep("assets_root", assetsRoot);
    rep("game_assets", gameAssets);
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
    // 1.20.2+ 的 quickPlay 占位符（features 关掉时这些参数不会出现，这里只是兜底）
    rep("quickPlayPath", "");
    rep("quickPlaySingleplayer", "");
    rep("quickPlayMultiplayer", "");
    rep("quickPlayRealms", "");
    // ${path} = log4j 配置（1.7+）
    rep("path", v.loggingClientPath.empty()
                    ? std::string()
                    : (assetsRootP / fs::u8path(v.loggingClientPath)).u8string());
    return s;
}

// 去重加入 classpath
static void addClasspathEntry(std::string& cp, std::set<std::string>& seen, const std::string& p) {
    if (p.empty()) return;
    if (!seen.insert(toLower(p)).second) return;
    if (!cp.empty()) cp += ";";
    cp += p;
}

std::string buildClasspath(const Version& v, const std::string& gameDir) {
    const std::string os = osName();
    const std::string arch = osArch();
    std::string cp;
    std::set<std::string> seen;
    fs::path libDir = fs::u8path(gameDir) / "libraries";
    fs::path verDir = fs::u8path(gameDir) / "versions";

    for (const auto& lib : v.libraries) {
        if (!lib.appliesTo(os, arch)) continue;
        if (lib.isNative()) continue;              // 老格式 natives：解压到 natives 目录，不进 classpath
        if (!lib.hasMainArtifact()) continue;      // 纯 classifier / 无主 jar
        std::string rel = lib.filePath();
        if (rel.empty()) continue;
        fs::path p = libDir / fs::u8path(rel);
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) addClasspathEntry(cp, seen, p.u8string());
    }

    // client jar 放最后（HMCL 也一样）
    std::string jarId = v.clientJarId.empty() ? v.id : v.clientJarId;
    fs::path jar = verDir / fs::u8path(jarId) / fs::u8path(jarId + ".jar");
    std::error_code ec;
    if (fs::is_regular_file(jar, ec)) addClasspathEntry(cp, seen, jar.u8string());
    return cp;
}

std::string defaultNativesDir(const std::string& dataDir) {
    if (dataDir.empty()) return std::string("natives-windows");
    return (fs::u8path(dataDir) / "natives-windows").u8string();
}

bool decompressNatives(const Version& v, const std::string& librariesDir,
                       const std::string& nativesDir, std::string& err) {
    struct Job {
        std::string jar;
        std::vector<std::string> exclude;
    };
    std::vector<Job> jobs;
    for (const auto& lib : v.libraries) {
        if (!lib.appliesTo(osName(), osArch())) continue;
        if (!lib.isNative()) continue;
        fs::path p = fs::u8path(librariesDir) / fs::u8path(nativeJarRel(lib));
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) continue;   // 缺 jar 由完整性检查负责报告
        jobs.push_back({ p.u8string(), lib.extractExclude });
    }

    std::error_code ec;
    fs::path dir = fs::u8path(nativesDir);
    if (!jobs.empty()) fs::remove_all(dir, ec);      // HMCL: cleanDirectoryQuietly
    fs::create_directories(dir, ec);
    if (ec) { err = "无法创建 natives 目录: " + ec.message(); return false; }
    if (jobs.empty()) return true;                    // 1.19+ 没有要解压的 natives

    bool anyFail = false;
    std::string lastFail;
    for (const auto& j : jobs) {
        bool ok = zip::ExtractFiltered(
            j.jar, dir.u8string(),
            [&](const std::string& rel) {
                if (rel.rfind("META-INF/", 0) == 0) return false;
                size_t dot = rel.find_last_of('.');
                if (dot != std::string::npos) {
                    std::string ext = toLower(rel.substr(dot + 1));
                    if (ext == "sha1" || ext == "git") return false;
                }
                for (const auto& ex : j.exclude) {
                    if (!ex.empty() && rel.rfind(ex, 0) == 0) return false;
                }
                return true;
            },
            true);
        if (!ok) { anyFail = true; lastFail = urlBaseName(j.jar); }
    }
    if (anyFail) {
        // 常见原因：同一目录下的游戏正在运行，dll 被锁住。此时目录里已有旧副本，
        // 内容通常完全一样，继续启动比直接报错更友好（HMCL 会直接抛异常）。
        bool hasAny = false;
        std::error_code ec2;
        for (auto& e : fs::directory_iterator(dir, ec2)) {
            if (e.is_regular_file(ec2)) { hasAny = true; break; }
        }
        if (!hasAny) {
            err = "解压 natives 失败: " + lastFail;
            return false;
        }
    }
    return true;
}

// 把 "a b "c d"" 这类参数串切成 token（支持双引号）
static std::vector<std::string> splitCommandLine(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') { inQuote = !inQuote; continue; }
        if (!inQuote && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool buildLaunchCommand(const Version& v, const LaunchOptions& opt,
                        std::vector<std::string>& outArgs, std::string& err) {
    if (v.mainClass.empty()) { err = "版本缺少 mainClass"; return false; }

    const std::string dataDir = opt.dataDir.empty() ? opt.gameDir : opt.dataDir;
    fs::path nativesDir = fs::u8path(opt.nativesDir.empty() ? defaultNativesDir(dataDir)
                                                            : opt.nativesDir);
    std::string natives = nativesDir.u8string();

    // 与 HMCL 一致：每次启动都清空并重新解压 natives（避免残留 dll 混用）
    {
        std::string nerr;
        if (!decompressNatives(v, (fs::u8path(opt.gameDir) / "libraries").u8string(), natives, nerr)) {
            err = nerr + "。请到「版本设置 → 立即检查并修复」补全文件后再启动";
            return false;
        }
    }

    std::string classpath = buildClasspath(v, opt.gameDir);
    if (classpath.empty()) {
        err = "classpath 为空：版本库文件缺失，请到「版本设置 → 立即检查并修复」或重新下载该版本";
        return false;
    }

    const int xmx = opt.maxMemoryMB > 0 ? opt.maxMemoryMB : 2048;
    int xms = std::min(xmx, 512);
    if (xms < 128) xms = std::min(xmx, 128);

    std::vector<std::string> jvm;
    jvm.push_back("-Xmx" + std::to_string(xmx) + "M");
    jvm.push_back("-Xms" + std::to_string(xms) + "M");
    // log4j RCE 缓解（1.7 ~ 1.18 必带；其他版本 Java 会忽略未知系统属性）
    jvm.push_back("-Dlog4j2.formatMsgNoLookups=true");

    const bool modern = !v.jvmArgs.empty();
    if (modern) {
        for (const auto& a : v.jvmArgs) jvm.push_back(substituteArg(a, v, opt, classpath, natives));
        // 兜底：极少数 JSON 有 jvm 参数但没有 -cp / -classpath
        bool hasCp = false;
        for (const auto& a : jvm) if (a == "-cp" || a == "-classpath") { hasCp = true; break; }
        if (!hasCp) {
            jvm.push_back("-cp");
            jvm.push_back(classpath);
        }
    } else {
        for (const auto& a : v.jvmArgs) jvm.push_back(substituteArg(a, v, opt, classpath, natives));
        jvm.push_back("-Djava.library.path=" + natives);
        jvm.push_back("-cp");
        jvm.push_back(classpath);
        jvm.push_back("-Dminecraft.launcher.brand=${launcher_name}");
        jvm.push_back("-Dminecraft.launcher.version=${launcher_version}");
        for (auto& a : jvm) a = substituteArg(a, v, opt, classpath, natives);
    }

    // 兜底：某些 JSON 的 jvm 参数里没有 java.library.path（或只有指向子目录的写法）
    bool hasLibPath = false;
    for (const auto& a : jvm) {
        if (a.rfind("-Djava.library.path=", 0) == 0) { hasLibPath = true; break; }
    }
    if (!hasLibPath) jvm.push_back("-Djava.library.path=" + natives);

    // 用户自定义 JVM 参数放最后（可覆盖 -Xmx 等）
    for (const auto& a : opt.extraJvmArgs) jvm.push_back(a);

    std::vector<std::string> game;
    if (!v.gameArgs.empty()) {
        for (const auto& a : v.gameArgs) game.push_back(substituteArg(a, v, opt, classpath, natives));
    } else if (!v.minecraftArguments.empty()) {
        for (const auto& tok : splitCommandLine(v.minecraftArguments))
            game.push_back(substituteArg(tok, v, opt, classpath, natives));
    }
    for (const auto& a : opt.extraGameArgs) game.push_back(a);
    if (!opt.server.empty()) {
        game.push_back("--server");
        game.push_back(opt.server);
        game.push_back("--port");
        game.push_back(std::to_string(opt.serverPort > 0 ? opt.serverPort : 25565));
    }

    // 兜底：1.20.2+ 只允许一个 --quickPlay*（整合包 JSON 偶尔会无条件带多个，会直接崩）
    {
        int qp = 0;
        std::vector<std::string> filtered;
        filtered.reserve(game.size());
        for (size_t i = 0; i < game.size(); ++i) {
            if (game[i].rfind("--quickPlay", 0) == 0) {
                if (++qp > 1) {
                    if (i + 1 < game.size() && game[i + 1].rfind("--", 0) != 0) ++i;
                    continue;
                }
            }
            filtered.push_back(game[i]);
        }
        if (qp > 1) game.swap(filtered);
    }

    outArgs.clear();
    outArgs.reserve(jvm.size() + game.size() + 1);
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
    for (const auto& kv : objs.obj) {
        AssetObject o;
        o.hash = kv.second.get("hash").asString();
        o.size = kv.second.get("size").asInt();
        if (!o.hash.empty()) out[kv.first] = o;
    }
    return true;
}

bool assetIndexIsVirtual(const std::string& jsonText, std::string& indexName) {
    json::Value j;
    try { j = json::parse(jsonText); }
    catch (...) { return false; }
    indexName = j.get("id").asString();
    bool virt = j.get("virtual").asBool() || j.get("map_to_resources").asBool();
    return virt;
}

// ---------------------------------------------------------------
// 并发下载
// ---------------------------------------------------------------
namespace {

struct DlTask {
    std::string officialUrl;
    std::string dest;       // 最终绝对路径
    std::string sha1;       // 空 = 不校验
    int64_t size = -1;
};

struct BatchResult {
    bool ok = false;
    std::string error;
    int64_t done = 0;
    int64_t total = 0;
};

using BatchProgressFn = std::function<bool(int64_t done, int64_t total, const std::string& current)>;

// 下载单文件（多源、sha1 校验、失败重试一次）；ok=false 时 err 为原因
bool downloadFileTo(const std::string& officialUrl, DownloadSource src, const std::string& dest,
                    const std::string& sha1, std::string& err) {
    auto urls = candidateUrls(officialUrl, src);
    if (urls.empty()) { err = "没有可用的下载地址"; return false; }
    std::error_code ec;
    fs::path dp = fs::u8path(dest);
    if (dp.has_parent_path()) fs::create_directories(dp.parent_path(), ec);

    std::string lastErr;
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool worthRetry = false;
        for (const auto& u : urls) {
            const std::string* shaPtr = sha1.empty() ? nullptr : &sha1;
            http::DownloadResult r = http::Download(u, dest, nullptr, kDownloadTimeoutMs, shaPtr);
            if (r.ok) return true;
            lastErr = r.error;

            bool is4xx = r.error.rfind("http 4", 0) == 0;
            bool isThrottle = r.error.find("429") != std::string::npos;
            if (isThrottle) {
                // 被镜像限流：换源没意义（官方在国内更慢），退避后重试同一个
                worthRetry = true;
                break;
            }
            if (!is4xx) worthRetry = true;    // 超时 / 5xx 值得再来一次
        }
        if (!worthRetry) break;               // 全是 404/403，重试没意义
        if (attempt == 0) Sleep(200);
    }
    err = lastErr.empty() ? "下载失败" : lastErr;
    return false;
}

BatchResult downloadBatch(const std::vector<DlTask>& tasks, DownloadSource src, int workers,
                          const BatchProgressFn& progress) {
    BatchResult res;
    res.total = (int64_t)tasks.size();
    res.done = 0;
    if (tasks.empty()) { res.ok = true; return res; }

    std::atomic<size_t> next{ 0 };
    std::atomic<int64_t> done{ 0 };
    std::atomic<bool> aborted{ false };
    std::mutex mu;
    std::vector<std::string> errors;

    struct Ev { int64_t done; std::string current; };
    std::vector<Ev> evs;

    auto worker = [&]() {
        while (!aborted.load()) {
            size_t i = next.fetch_add(1);
            if (i >= tasks.size()) return;
            const DlTask& t = tasks[i];

            if (fileLooksOk(t.dest, t.sha1, t.size)) {
                int64_t d = ++done;
                { std::lock_guard<std::mutex> lk(mu); evs.push_back({ d, t.dest }); }
                continue;
            }
            if (fileExists(t.dest)) removeQuiet(t.dest);   // 损坏/半截文件，删掉重下

            std::string err;
            if (!downloadFileTo(t.officialUrl, src, t.dest, t.sha1, err)) {
                {
                    std::lock_guard<std::mutex> lk(mu);
                    if (errors.size() < 12) errors.push_back(urlBaseName(t.dest) + " (" + err + ")");
                }
                // 单个文件失败不整体中止：最后统一汇总（资产数量上千，一个 404 不该全废）
                ++done;
                continue;
            }
            int64_t d = ++done;
            { std::lock_guard<std::mutex> lk(mu); evs.push_back({ d, t.dest }); }
        }
    };

    std::vector<std::thread> threads;
    int n = std::min<int>(std::max(1, workers), (int)tasks.size());
    for (int i = 0; i < n; ++i) threads.emplace_back(worker);

    while (done.load() < res.total && !aborted.load()) {
        std::vector<Ev> local;
        {
            std::lock_guard<std::mutex> lk(mu);
            local.swap(evs);
        }
        for (auto& e : local) {
            if (progress && !progress(e.done, res.total, e.current)) { aborted = true; break; }
        }
        if (aborted.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    for (auto& th : threads) if (th.joinable()) th.join();

    {
        std::vector<Ev> local;
        std::lock_guard<std::mutex> lk(mu);
        local.swap(evs);
        for (auto& e : local) if (progress) progress(e.done, res.total, e.current);
    }

    res.done = done.load();
    if (aborted.load()) {
        res.ok = false;
        res.error = "已取消";
        return res;
    }
    {
        std::lock_guard<std::mutex> lk(mu);
        if (!errors.empty()) {
            std::string msg = "有 " + std::to_string(errors.size()) + " 个文件下载失败：";
            for (size_t i = 0; i < errors.size(); ++i) {
                if (i) msg += "；";
                msg += errors[i];
            }
            res.ok = false;
            res.error = msg;
            return res;
        }
    }
    res.ok = true;
    return res;
}

// 下载版本 JSON（递归补齐 inheritsFrom 链）
bool ensureVersionJson(const std::string& versionsDir, const VersionManifest& mf,
                       const std::string& id, DownloadSource src,
                       const std::function<bool(const InstallProgress&)>& progress,
                       InstallProgress& p, std::string& err) {
    fs::path jsonPath = fs::u8path(versionsDir) / fs::u8path(id) / fs::u8path(id + ".json");
    if (!fileExists(jsonPath.u8string())) {
        const ManifestVersion* entry = nullptr;
        for (const auto& e : mf.versions) if (e.id == id) { entry = &e; break; }
        if (!entry) { err = "版本清单中不存在: " + id; return false; }

        p.phase = "version";
        p.current = id + ".json";
        if (progress && !progress(p)) { err = "已取消"; return false; }

        ensureParentDir(jsonPath.u8string());
        std::vector<std::string> urls = candidateUrls(entry->url, src);
        // BMCLAPI 的短路径兜底
        std::string shortUrl = "https://bmclapi2.bangbang93.com/version/" + id + "/json";
        if (std::find(urls.begin(), urls.end(), shortUrl) == urls.end()) urls.push_back(shortUrl);

        std::string lastErr;
        bool ok = false;
        for (const auto& u : urls) {
            http::DownloadResult r = http::Download(u, jsonPath.u8string(), nullptr, 30000, nullptr);
            if (!r.ok) { lastErr = r.error; continue; }
            // 必须能解析，否则删掉重试（避免缓存了一个错误页面）
            std::string txt;
            { std::ifstream f(jsonPath, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); txt = ss.str(); }
            try { json::parse(txt); ok = true; break; }
            catch (...) { removeQuiet(jsonPath.u8string()); lastErr = "返回内容不是合法 JSON"; }
        }
        if (!ok) { err = "下载版本 JSON 失败: " + lastErr; return false; }
    }

    // 递归父版本
    std::string txt;
    { std::ifstream f(jsonPath, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); txt = ss.str(); }
    try {
        json::Value j = json::parse(txt);
        std::string parent = j.get("inheritsFrom").asString();
        if (!parent.empty()) return ensureVersionJson(versionsDir, mf, parent, src, progress, p, err);
    } catch (const std::exception& e) {
        err = std::string("版本 JSON 解析失败: ") + e.what();
        return false;
    }
    return true;
}

// 老版本（1.5/1.6 之前）需要把资源「摆」成 assets/virtual/<index>/<name>
void materializeVirtualAssets(const std::string& assetsDir, const std::string& indexName,
                              const std::string& indexJson, const std::string& gameDir) {
    AssetIndex ai;
    if (!parseAssetIndex(indexJson, ai)) return;

    json::Value j;
    bool mapToResources = false;
    try {
        j = json::parse(indexJson);
        mapToResources = j.get("map_to_resources").asBool();
    } catch (...) {}

    std::string root = mapToResources ? (fs::u8path(gameDir) / "resources").u8string()
                                      : (fs::u8path(assetsDir) / "virtual" / fs::u8path(indexName)).u8string();
    std::error_code ec;
    fs::create_directories(fs::u8path(root), ec);
    if (ec) return;

    for (const auto& kv : ai) {
        const std::string& name = kv.first;
        const std::string& hash = kv.second.hash;
        if (hash.size() < 2 || name.empty()) continue;
        fs::path src = fs::u8path(assetsDir) / "objects" / fs::u8path(hash.substr(0, 2)) / fs::u8path(hash);
        fs::path dst = fs::u8path(root) / fs::u8path(name);
        if (fileExists(dst.u8string())) continue;
        fs::create_directories(dst.parent_path(), ec);
        std::error_code ec2;
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec2);
    }
}

// 真正的文件编排：client jar / logging / libraries / natives / assets
bool installFiles(const std::string& versionsDir, const std::string& librariesDir,
                  const std::string& assetsDir, const std::string& gameDir,
                  const Version& v, DownloadSource src,
                  const std::function<bool(const InstallProgress&)>& progress,
                  std::string& err) {
    InstallProgress p;
    p.phase = "version";
    p.current = v.id;
    p.total = 1;
    p.done = 0;
    if (progress && !progress(p)) { err = "已取消"; return false; }

    // ---- 1. client jar ----
    std::string jarId = v.clientJarId.empty() ? v.id : v.clientJarId;
    if (!v.clientJar.url.empty()) {
        fs::path jarPath = fs::u8path(versionsDir) / fs::u8path(jarId) / fs::u8path(jarId + ".jar");
        p.phase = "client";
        p.current = jarId + ".jar";
        p.total = 1;
        p.done = 0;
        if (progress && !progress(p)) { err = "已取消"; return false; }
        if (!fileLooksOk(jarPath.u8string(), v.clientJar.sha1, v.clientJar.size)) {
            removeQuiet(jarPath.u8string());
            std::string derr;
            if (!downloadFileTo(v.clientJar.url, src, jarPath.u8string(), v.clientJar.sha1, derr)) {
                err = "下载 client jar 失败: " + derr;
                return false;
            }
        }
        p.done = 1;
        if (progress) progress(p);
    }

    // ---- 2. log4j 配置（1.7+）----
    if (!v.loggingClientUrl.empty() && !v.loggingClientPath.empty()) {
        fs::path lp = fs::u8path(assetsDir) / fs::u8path(v.loggingClientPath);
        p.phase = "client";
        p.current = urlBaseName(v.loggingClientUrl);
        p.total = 1;
        p.done = 0;
        if (progress && !progress(p)) { err = "已取消"; return false; }
        if (!fileLooksOk(lp.u8string(), v.loggingClientSha1)) {
            removeQuiet(lp.u8string());
            std::string derr;
            if (!downloadFileTo(v.loggingClientUrl, src, lp.u8string(), v.loggingClientSha1, derr)) {
                // 日志配置缺失不致命，记一条不中断
                if (progress) { p.current = "日志配置下载失败（不影响启动）"; progress(p); }
            }
        }
        p.done = 1;
        if (progress) progress(p);
    }

    // ---- 3. libraries + natives ----
    const std::string os = osName();
    const std::string arch = osArch();
    std::vector<DlTask> libTasks;
    std::set<std::string> libSeen;

    for (const auto& lib : v.libraries) {
        if (!lib.appliesTo(os, arch)) continue;

        // 主 jar
        if (lib.hasMainArtifact() && !lib.filePath().empty()) {
            std::string rel = lib.filePath();
            DlTask t;
            t.officialUrl = lib.artifact.url.empty()
                ? ("https://libraries.minecraft.net/" + rel)
                : lib.artifact.url;
            t.dest = (fs::u8path(librariesDir) / fs::u8path(rel)).u8string();
            t.sha1 = lib.artifact.sha1;
            t.size = lib.artifact.size;
            if (libSeen.insert(toLower(t.dest)).second) libTasks.push_back(std::move(t));
        }

        // 需要解压的原生库
        if (lib.isNative() && !lib.nativeClassifier.empty()) {
            auto it = lib.classifiers.find(lib.nativeClassifier);
            std::string rel = nativeJarRel(lib);
            DlTask t;
            t.officialUrl = (it != lib.classifiers.end() && !it->second.url.empty())
                ? it->second.url
                : ("https://libraries.minecraft.net/" + rel);
            t.dest = (fs::u8path(librariesDir) / fs::u8path(rel)).u8string();
            t.sha1 = (it != lib.classifiers.end()) ? it->second.sha1 : std::string();
            if (libSeen.insert(toLower(t.dest)).second) libTasks.push_back(std::move(t));
        }
    }

    p.phase = "libraries";
    p.total = (int64_t)libTasks.size();
    p.done = 0;
    if (progress && !progress(p)) { err = "已取消"; return false; }

    BatchResult lr = downloadBatch(libTasks, src, libWorkers(),
        [&](int64_t done, int64_t total, const std::string& current) -> bool {
            p.done = done;
            p.total = total;
            p.current = urlBaseName(current);
            return progress ? progress(p) : true;
        });
    if (!lr.ok) { err = lr.error; return false; }

    // natives 不在安装阶段解压：与 HMCL 一致，启动时解压到 <gameDir>\natives-windows
    // （decompressNatives 会先清空目录，避免旧版本残留 dll 混用）

    // ---- 5. assets ----
    if (!v.assets.empty()) {
        fs::path indexPath = fs::u8path(assetsDir) / "indexes" / fs::u8path(v.assets + ".json");
        p.phase = "assets";
        p.current = "资源索引 " + v.assets;
        p.total = 1;
        p.done = 0;
        if (progress && !progress(p)) { err = "已取消"; return false; }

        // 索引地址：JSON 里有就用，没有就按官方规律拼（老版本常见）
        std::string indexUrl = v.assetIndexUrl;
        if (indexUrl.empty()) {
            indexUrl = "https://launchermeta.mojang.com/mc/assets/" + v.assets + "/" +
                       v.assets + ".json";
        }
        if (!fileLooksOk(indexPath.u8string(), v.assetIndexSha1)) {
            removeQuiet(indexPath.u8string());
            std::string derr;
            if (!downloadFileTo(indexUrl, src, indexPath.u8string(), v.assetIndexSha1, derr)) {
                err = "下载资源索引失败: " + derr;
                return false;
            }
        }

        std::string indexJson;
        { std::ifstream f(indexPath, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); indexJson = ss.str(); }

        AssetIndex ai;
        if (!parseAssetIndex(indexJson, ai)) {
            err = "资源索引内容不合法: " + v.assets;
            return false;
        }

        std::vector<DlTask> assetTasks;
        assetTasks.reserve(ai.size());
        for (const auto& kv : ai) {
            const std::string& h = kv.second.hash;
            if (h.size() < 2) continue;
            std::string sub = h.substr(0, 2);
            DlTask t;
            t.officialUrl = "https://resources.download.minecraft.net/" + sub + "/" + h;
            t.dest = (fs::u8path(assetsDir) / "objects" / fs::u8path(sub) / fs::u8path(h)).u8string();
            t.sha1 = h;
            t.size = kv.second.size;
            assetTasks.push_back(std::move(t));
        }

        p.total = (int64_t)assetTasks.size();
        p.done = 0;
        if (progress && !progress(p)) { err = "已取消"; return false; }

        BatchResult ar = downloadBatch(assetTasks, src, assetWorkers(),
            [&](int64_t done, int64_t total, const std::string& current) -> bool {
                p.done = done;
                p.total = total;
                p.current = urlBaseName(current);
                return progress ? progress(p) : true;
            });
        if (!ar.ok) { err = ar.error; return false; }

        // 1.5/1.6 之前：需要把 objects 摊平成 virtual/<index>/...
        std::string idxId;
        if (assetIndexIsVirtual(indexJson, idxId)) {
            p.phase = "extract";
            p.current = "生成虚拟资源目录 " + v.assets;
            p.total = 1;
            p.done = 0;
            if (progress && !progress(p)) { err = "已取消"; return false; }
            materializeVirtualAssets(assetsDir, v.assets, indexJson, gameDir);
        }
    }

    p.phase = "done";
    p.current.clear();
    p.done = 1;
    p.total = 1;
    p.finished = true;
    if (progress) progress(p);
    return true;
}

} // namespace

// ---------------------------------------------------------------
// 安装入口
// ---------------------------------------------------------------
bool installVersion(const std::string& versionsDir, const std::string& librariesDir,
                    const std::string& assetsDir, const VersionManifest& mf,
                    const std::string& versionId, DownloadSource src,
                    const std::function<bool(const InstallProgress&)>& progress,
                    std::string& err) {
    InstallProgress p;
    p.phase = "version";

    if (!ensureVersionJson(versionsDir, mf, versionId, src, progress, p, err)) return false;

    Version v;
    if (!loadVersion(versionsDir, versionId, v, err)) return false;

    std::string gameDir = fs::u8path(versionsDir).parent_path().u8string();
    return installFiles(versionsDir, librariesDir, assetsDir, gameDir, v, src, progress, err);
}

bool repairVersionFiles(const std::string& versionsDir, const std::string& librariesDir,
                        const std::string& assetsDir, const std::string& versionId,
                        DownloadSource src,
                        const std::function<bool(const InstallProgress&)>& progress,
                        std::string& err) {
    Version v;
    if (!loadVersion(versionsDir, versionId, v, err)) return false;
    std::string gameDir = fs::u8path(versionsDir).parent_path().u8string();
    return installFiles(versionsDir, librariesDir, assetsDir, gameDir, v, src, progress, err);
}

// ---------------------------------------------------------------
// 完整性检查
// ---------------------------------------------------------------
bool checkIntegrity(const std::string& versionsDir, const std::string& librariesDir,
                    const std::string& assetsDir, const std::string& versionId,
                    IntegrityReport& rep, std::string& err) {
    rep = IntegrityReport{};
    fs::path jsonPath = fs::u8path(versionsDir) / fs::u8path(versionId) / fs::u8path(versionId + ".json");
    if (!fileExists(jsonPath.u8string())) {
        rep.jsonMissing = true;
        rep.detail = "缺少版本 JSON：" + jsonPath.u8string();
        rep.missing.push_back(jsonPath.u8string());
        return true;   // 检查完成（结果是「不完整」）
    }

    Version v;
    if (!loadVersion(versionsDir, versionId, v, err)) {
        rep.detail = "版本 JSON 解析失败：" + err;
        rep.jsonMissing = true;
        return true;
    }

    // client jar
    std::string jarId = v.clientJarId.empty() ? v.id : v.clientJarId;
    fs::path jar = fs::u8path(versionsDir) / fs::u8path(jarId) / fs::u8path(jarId + ".jar");
    if (!fileExists(jar.u8string())) {
        rep.clientJarMissing = true;
        rep.missing.push_back(jar.u8string());
    }

    // libraries + natives（natives 解压发生在启动时，这里只检查 jar 是否齐全）
    const std::string os = osName();
    const std::string arch = osArch();
    for (const auto& lib : v.libraries) {
        if (!lib.appliesTo(os, arch)) continue;
        if (lib.hasMainArtifact() && !lib.filePath().empty()) {
            ++rep.totalLibs;
            fs::path p = fs::u8path(librariesDir) / fs::u8path(lib.filePath());
            if (!fileExists(p.u8string())) {
                ++rep.missingLibs;
                if (rep.missing.size() < 40) rep.missing.push_back(p.u8string());
            }
        }
        if (lib.isNative() && !lib.nativeClassifier.empty()) {
            fs::path p = fs::u8path(librariesDir) / fs::u8path(nativeJarRel(lib));
            if (!fileExists(p.u8string())) {
                ++rep.missingNatives;
                if (rep.missing.size() < 40) rep.missing.push_back(p.u8string());
            }
        }
    }

    // assets
    if (!v.assets.empty()) {
        fs::path indexPath = fs::u8path(assetsDir) / "indexes" / fs::u8path(v.assets + ".json");
        if (!fileExists(indexPath.u8string())) {
            ++rep.missingAssets;
            rep.totalAssets = 1;
            if (rep.missing.size() < 40) rep.missing.push_back(indexPath.u8string());
        } else {
            std::ifstream f(indexPath, std::ios::binary);
            std::stringstream ss; ss << f.rdbuf();
            AssetIndex ai;
            if (parseAssetIndex(ss.str(), ai)) {
                rep.totalAssets = (int)ai.size();
                fs::path objects = fs::u8path(assetsDir) / "objects";
                int checked = 0;
                for (const auto& kv : ai) {
                    const std::string& h = kv.second.hash;
                    if (h.size() < 2) continue;
                    fs::path p = objects / fs::u8path(h.substr(0, 2)) / fs::u8path(h);
                    std::error_code ec;
                    if (!fs::exists(p, ec)) {
                        ++rep.missingAssets;
                        if (rep.missing.size() < 40) rep.missing.push_back(p.u8string());
                    }
                    if (++checked > 20000) break;
                }
            }
        }
    }

    rep.ok = !rep.jsonMissing && !rep.clientJarMissing && rep.missingLibs == 0 &&
             rep.missingNatives == 0 && rep.missingAssets == 0;

    char buf[320];
    snprintf(buf, sizeof(buf), "版本:%s 库:%d/%d 缺失 资源:%d/%d 缺失 原生库缺失:%d",
             rep.jsonMissing ? "缺失" : "正常",
             rep.missingLibs, rep.totalLibs,
             rep.missingAssets, rep.totalAssets,
             rep.missingNatives);
    rep.detail = buf;
    return true;
}

} // namespace mc
