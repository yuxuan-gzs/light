// LoaderInstall.cpp — Fabric / Quilt / Forge 加载器安装器实现
#include "LoaderInstall.h"
#include "Http.h"
#include "Json.h"
#include "LauncherCore.h"
#include "JavaDetect.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <windows.h>

namespace fs = std::filesystem;

namespace loader {

static const char* kFabricMeta = "https://meta.fabricmc.net/v2";
static const char* kQuiltMeta  = "https://meta.quiltmc.org/v3";
static const char* kFabricMaven = "https://maven.fabricmc.net/";
static const char* kQuiltMaven  = "https://maven.quiltmc.org/repository/release/";
// NeoForge 官方源 maven.neoforged.net 在国内常不可达，改用 BMCLAPI 镜像
static const char* kNeoForgeMaven = "https://bmclapi2.bangbang93.com/maven/net/neoforged/neoforge/";

static const char* metaBase(bool quilt) { return quilt ? kQuiltMeta : kFabricMeta; }
static const char* mavenBase(bool quilt) { return quilt ? kQuiltMaven : kFabricMaven; }

// 该 MC 版本装加载器需要的 Java 主版本。
// 1.16.5 及以下 → 8（installer 用 17+ 跑会 UnsupportedClassVersionError / 直接崩）
// 1.17 ~ 1.20.4 → 17；1.20.5+ → 21
int JavaMajorForMc(const std::string& mcVersion) {
    int minor = 0, patch = 0;
    int n = sscanf(mcVersion.c_str(), "1.%d.%d", &minor, &patch);
    if (n < 1 || minor <= 0) return 17;      // 快照等认不出的按现代版本处理
    if (minor >= 21) return 21;
    if (minor == 20 && patch >= 5) return 21;
    if (minor >= 17) return 17;
    return 8;
}

// 给 installer 挑 Java：大版本不对宁可直接报错，也不要拿 21 去跑 1.12.2 的 installer
std::string PickInstallerJava(const std::string& mcPart, const std::string& prefer,
                              std::string& err) {
    if (!prefer.empty()) return prefer;
    const int need = JavaMajorForMc(mcPart);
    if (need <= 8) {
        auto j8 = java::DetectExact(8);
        if (!j8.path.empty()) return j8.path;
        err = "安装 " + mcPart + " 的加载器需要 Java 8（用 17/21 运行 installer 会直接崩），"
              "本机没找到 Java 8。请到「全局设置 → Java」指定一个 Java 8 的 javaw.exe。";
        return "";
    }
    bool satisfied = false;
    auto jr = java::DetectBest(need, &satisfied);
    // 装了正好这个版本的 Java 就优先用它（installer 对过高版本也可能不兼容，21 别用 25）
    auto exact = java::DetectExact(need);
    if (!exact.path.empty()) return exact.path;
    if (satisfied && !jr.path.empty()) return jr.path;
    err = "安装 " + mcPart + " 的加载器需要 Java " + std::to_string(need) +
          " 或更高版本，本机没找到。请到「全局设置 → Java」指定 Java 路径。";
    return "";
}

// 简单 URL 编码（保留 RFC3986 非保留字符）
static std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::vector<LoaderVersion> FetchVersions(const std::string& mcVersion,
                                         bool quilt, std::string& err) {
    std::vector<LoaderVersion> out;
    std::string url = std::string(metaBase(quilt)) + "/versions/loader/" + urlEncode(mcVersion);
    http::Response r = http::Get(url, 30000);
    if (r.status != 200) {
        err = "获取加载器版本失败 (HTTP " + std::to_string(r.status) + "): " + r.error;
        return out;
    }
    try {
        json::Value arr = json::parse(r.body);
        for (size_t i = 0; i < arr.size(); ++i) {
            const json::Value& e = arr.at(i);
            const json::Value& lv = e.get("loader");
            LoaderVersion v;
            if (lv.isObject()) {          // Fabric: {loader:{version,stable},...}
                v.version = lv.get("version").asString();
                v.stable = lv.get("stable").asBool();
            } else {                       // Quilt 直接: {version,stable,...}
                v.version = e.get("version").asString();
                v.stable = e.get("stable").asBool();
            }
            if (!v.version.empty()) out.push_back(v);
        }
    } catch (const std::exception& e) {
        err = std::string("解析加载器版本失败: ") + e.what();
        out.clear();
    }
    return out;
}

std::string VersionId(const std::string& mcVersion,
                      const std::string& loaderVersion, bool quilt) {
    return (quilt ? "quilt-loader-" : "fabric-loader-") + loaderVersion + "-" + mcVersion;
}

static bool fileExists(const std::string& p) {
    std::error_code ec;
    return fs::exists(fs::u8path(p), ec) && !ec;
}

// 下载，失败自动回退到 BMCLAPI 镜像（HMCL 式多源）
static bool downloadFallback(const std::string& url, const std::string& dest,
                             const std::string* sha1, std::string& err) {
    std::vector<std::string> urls;
    urls.push_back(url);
    static const struct { const char* from; const char* to; } maps[] = {
        {"https://maven.fabricmc.net/", "https://bmclapi2.bangbang93.com/maven/"},
        {"https://maven.quiltmc.org/repository/release/", "https://bmclapi2.bangbang93.com/maven/"},
        {"https://maven.minecraftforge.net/", "https://bmclapi2.bangbang93.com/maven/"},
        {"https://maven.neoforged.net/releases/", "https://bmclapi2.bangbang93.com/maven/"},
        {"https://bmclapi2.bangbang93.com/maven/net/neoforged/neoforge/", "https://maven.neoforged.net/releases/net/neoforged/neoforge/"},
        {"https://bmclapi2.bangbang93.com/maven/net/neoforged/forge/", "https://maven.neoforged.net/releases/net/neoforged/forge/"},
    };
    for (auto& m : maps) {
        if (url.rfind(m.from, 0) == 0) {
            urls.push_back(m.to + url.substr(strlen(m.from)));
            break;
        }
    }
    for (auto& u : urls) {
        http::DownloadResult r = http::Download(u, dest, nullptr, 120000, sha1);
        if (r.ok) return true;
        err = r.error;
    }
    return false;
}

static std::string mavenPathOf(const std::string& name) {
    std::string g, a, v, cls;
    mc::parseMavenArtifact(name, g, a, v, cls);
    if (g.empty() || a.empty() || v.empty()) return "";
    std::replace(g.begin(), g.end(), '.', '/');
    std::string file = a + "-" + v + (cls.empty() ? "" : "-" + cls) + ".jar";
    return g + "/" + a + "/" + v + "/" + file;
}

// Forge / NeoForge 的 installer 会检查 <gameDir>\launcher_profiles.json，
// 没有就直接报「There is no minecraft launcher profile」并退出 —— 这里补一个最小可用的。
std::string Install(const std::string& mcVersion, const std::string& loaderVersionIn,
                    bool quilt, const std::string& versionsDir,
                    const std::string& librariesDir,
                    const std::function<void(const std::string&)>& log,
                    std::string& err) {
    auto info = [&](const std::string& m) { if (log) log(m); };

    // 版本为空（用户没先点「获取版本列表」）时自己取：否则 profile URL 会变成
    // .../loader/1.21.11//profile/json → 必然 404，表现就是「安装永远失败」。
    std::string loaderVersion = loaderVersionIn;
    if (loaderVersion.empty()) {
        info("未指定加载器版本，正在获取最新" + std::string(quilt ? " Quilt" : " Fabric") + " 版本…");
        std::string ferr;
        auto list = FetchVersions(mcVersion, quilt, ferr);
        if (list.empty()) {
            err = ferr.empty() ? ("没有找到 " + mcVersion + " 可用的加载器版本") : ferr;
            return "";
        }
        for (const auto& v : list) {
            if (v.stable) { loaderVersion = v.version; break; }
        }
        if (loaderVersion.empty()) loaderVersion = list.front().version;
        info("使用加载器版本 " + loaderVersion);
    }

    std::string id = VersionId(mcVersion, loaderVersion, quilt);
    info(std::string("安装 ") + (quilt ? "Quilt" : "Fabric") + " " + loaderVersion +
         " → " + id);

    // 1. 下载 profile JSON（现成的 launcher version JSON）
    std::string profileUrl = std::string(metaBase(quilt)) + "/versions/loader/" +
                             urlEncode(mcVersion) + "/" + urlEncode(loaderVersion) +
                             "/profile/json";
    http::Response r = http::Get(profileUrl, 30000);
    if (r.status != 200) {
        err = "获取加载器 profile 失败 (HTTP " + std::to_string(r.status) + "): " + r.error;
        return "";
    }
    std::string profileJson = r.body;

    // 2. 写 version JSON
    fs::path jsonPath = fs::u8path(versionsDir) / fs::u8path(id) / fs::u8path(id + ".json");
    {
        std::error_code ec;
        fs::create_directories(jsonPath.parent_path(), ec);
        std::ofstream f(jsonPath, std::ios::binary | std::ios::trunc);
        if (!f) { err = "无法写入版本 JSON: " + id; return ""; }
        f.write(profileJson.data(), (std::streamsize)profileJson.size());
    }
    info("已写入版本 JSON");

    // 3. 解析并下载 libraries
    std::string mavenDefault = mavenBase(quilt);
    try {
        json::Value root = json::parse(profileJson);
        const json::Value& libs = root.get("libraries");
        int total = (int)libs.size();
        int done = 0;
        int failed = 0;
        for (size_t i = 0; i < libs.size(); ++i) {
            const json::Value& lib = libs.at(i);
            std::string name = lib.get("name").asString();
            if (name.empty()) continue;  // Mojang downloads 格式（少见），跳过

            std::string mp = mavenPathOf(name);
            if (mp.empty()) { ++failed; continue; }

            std::string base = lib.get("url").asString();
            if (base.empty()) base = mavenDefault;
            if (base.back() != '/') base += '/';
            std::string url = base + mp;
            fs::path dest = fs::u8path(librariesDir) / fs::u8path(mp);

            ++done;
            if (fileExists(dest.u8string())) { continue; }

            std::string sha1 = lib.get("sha1").asString();
            const std::string* shaPtr = sha1.empty() ? nullptr : &sha1;

            std::error_code ec;
            fs::create_directories(dest.parent_path(), ec);
            std::string derr;
            if (!downloadFallback(url, dest.u8string(), shaPtr, derr)) {
                ++failed;
                info("下载失败: " + mp + " (" + derr + ")");
            }
        }
        info("库下载完成: " + std::to_string(done) + " 个" +
             (failed ? ("（失败 " + std::to_string(failed) + " 个）") : ""));
    } catch (const std::exception& e) {
        err = std::string("解析 profile 失败: ") + e.what();
        return "";
    }

    return id;
}

// ============================================================
// Forge（运行 installer jar）
// ============================================================
// 版本列表：优先用 JSON 接口（HMCL 也是 JSON），XML(maven-metadata) 只做兜底。
//  1) BMCLAPI：https://bmclapi2.bangbang93.com/forge/minecraft/<mcver>  → 数组
//     每项 { build, mcversion, version, branch?, modified, files[] }
//  2) 官方 promotions_slim.json（只有 latest / recommended 两个）
//  3) maven-metadata.xml（老办法）
static std::vector<std::string> fetchForgeFromBmclJson(const std::string& mcVersion) {
    std::vector<std::string> out;
    http::Response r = http::Get("https://bmclapi2.bangbang93.com/forge/minecraft/" + mcVersion, 30000);
    if (r.status != 200) return out;
    try {
        json::Value arr = json::parse(r.body);
        std::vector<std::pair<int64_t, std::string>> tmp;
        for (size_t i = 0; i < arr.size(); ++i) {
            const json::Value& e = arr.at(i);
            std::string ver = e.get("version").asString();
            if (ver.empty()) continue;
            std::string mc = e.get("mcversion").asString();
            if (mc.empty()) mc = mcVersion;
            std::string branch = e.get("branch").asString();
            // 1.7.10 的 10.13.4.1614 这类，maven 坐标要带 branch 后缀
            std::string full = mc + "-" + ver;
            if (!branch.empty()) full += "-" + branch;
            tmp.push_back({ e.get("build").asInt(), full });
        }
        std::sort(tmp.begin(), tmp.end(),
                  [](const std::pair<int64_t, std::string>& a,
                     const std::pair<int64_t, std::string>& b) { return a.first > b.first; });
        for (auto& p : tmp) out.push_back(p.second);
    } catch (...) { out.clear(); }
    return out;
}

static std::vector<std::string> fetchForgeFromPromotions(const std::string& mcVersion) {
    std::vector<std::string> out;
    http::Response r = http::Get(
        "https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json", 30000);
    if (r.status != 200) return out;
    try {
        json::Value root = json::parse(r.body);
        const json::Value& promos = root.get("promos");
        std::string latest = promos.get((mcVersion + "-latest").c_str()).asString();
        std::string recommended = promos.get((mcVersion + "-recommended").c_str()).asString();
        if (!latest.empty()) out.push_back(mcVersion + "-" + latest);
        if (!recommended.empty() && recommended != latest)
            out.push_back(mcVersion + "-" + recommended);
    } catch (...) { out.clear(); }
    return out;
}

static std::vector<std::string> fetchForgeFromXml(const std::string& mcVersion) {
    std::vector<std::string> out;
    http::Response r = http::Get(
        "https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml", 30000);
    if (r.status != 200) return out;
    std::string prefix = mcVersion + "-";
    size_t pos = 0;
    while ((pos = r.body.find("<version>", pos)) != std::string::npos) {
        size_t e = r.body.find("</version>", pos);
        if (e == std::string::npos) break;
        std::string v = r.body.substr(pos + 9, e - pos - 9);
        if (v.rfind(prefix, 0) == 0) out.push_back(v);
        pos = e + 10;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<std::string> FetchForgeVersions(const std::string& mcVersion,
                                            std::string& err) {
    std::vector<std::string> out = fetchForgeFromBmclJson(mcVersion);
    if (out.empty()) out = fetchForgeFromPromotions(mcVersion);
    if (out.empty()) out = fetchForgeFromXml(mcVersion);
    if (out.empty()) err = "没有找到 " + mcVersion + " 的 Forge 版本（网络或镜像不可用）";
    return out;
}

// NeoForge 版本列表：优先 maven.neoforged.net 的 JSON API（HMCL 同款），XML 兜底。
//   新 group：net/neoforged/neoforge   → 版本形如 20.2.88 / 21.1.0
//   旧 group：net/neoforged/forge      → 只有 1.20.1，版本形如 1.20.1-47.1.106
static std::vector<std::string> fetchNeoFromJson(const std::string& mcVersion) {
    std::vector<std::string> out;
    const bool legacy = (mcVersion == "1.20.1");
    const char* artifact = legacy ? "forge" : "neoforge";
    const std::string urls[] = {
        std::string("https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/") + artifact,
        std::string("https://neoforged.forgecdn.net/api/maven/versions/releases/net/neoforged/") + artifact,
    };
    std::string key = mcVersion;
    if (key.rfind("1.", 0) == 0) key = key.substr(2);

    for (const auto& url : urls) {
        http::Response r = http::Get(url, 30000);
        if (r.status != 200) continue;
        try {
            json::Value root = json::parse(r.body);
            const json::Value& vs = root.get("versions");
            for (size_t i = 0; i < vs.size(); ++i) {
                std::string v = vs.at(i).asString();
                if (v.empty()) continue;
                bool ok;
                if (legacy) {
                    ok = v.rfind(mcVersion + "-", 0) == 0;
                } else {
                    ok = v.rfind(key, 0) == 0 &&
                         (v.size() == key.size() || v[key.size()] == '.');
                }
                if (ok) out.push_back(v);
            }
        } catch (...) { out.clear(); }
        if (!out.empty()) break;
    }
    std::reverse(out.begin(), out.end());   // API 是升序，转成新的在前
    return out;
}

std::vector<std::string> FetchNeoForgeVersions(const std::string& mcVersion,
                                               std::string& err) {
    std::vector<std::string> out = fetchNeoFromJson(mcVersion);

    // 兜底：BMCLAPI 的 maven-metadata.xml
    if (out.empty()) {
        std::string url = std::string(kNeoForgeMaven) + "maven-metadata.xml";
        http::Response r = http::Get(url, 30000);
        if (r.status == 200) {
            std::string key = mcVersion;
            if (key.rfind("1.", 0) == 0) key = key.substr(2);
            size_t pos = 0;
            while ((pos = r.body.find("<version>", pos)) != std::string::npos) {
                size_t e = r.body.find("</version>", pos);
                if (e == std::string::npos) break;
                std::string v = r.body.substr(pos + 9, e - pos - 9);
                if (v.rfind(key, 0) == 0 &&
                    (v.size() == key.size() || v[key.size()] == '.')) {
                    out.push_back(v);
                }
                pos = e + 10;
            }
            std::reverse(out.begin(), out.end());
        }
    }

    if (out.empty()) err = "没有找到 " + mcVersion + " 的 NeoForge 版本（网络或镜像不可用）";
    return out;
}

} // namespace loader
