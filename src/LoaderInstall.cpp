// LoaderInstall.cpp — Fabric / Quilt / Forge 加载器安装器实现
#include "LoaderInstall.h"
#include "Http.h"
#include "Json.h"
#include "LauncherCore.h"
#include "JavaDetect.h"

#include <filesystem>
#include <fstream>
#include <sstream>
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
        {"https://bmclapi2.bangbang93.com/maven/net/neoforged/neoforge/", "https://maven.neoforged.net/releases/net/neoforged/neoforge/"},
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
    std::string g, a, v;
    if (!mc::parseMavenName(name, g, a, v)) return "";
    std::replace(g.begin(), g.end(), '.', '/');
    return g + "/" + a + "/" + v + "/" + a + "-" + v + ".jar";
}

std::string Install(const std::string& mcVersion, const std::string& loaderVersion,
                    bool quilt, const std::string& versionsDir,
                    const std::string& librariesDir,
                    const std::function<void(const std::string&)>& log,
                    std::string& err) {
    auto info = [&](const std::string& m) { if (log) log(m); };

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
static std::string RunJavaAndWait(const std::string& javaPath,
                                  const std::vector<std::string>& args,
                                  const std::string& workDir, std::string& err) {
    auto quote = [](const std::string& a) {
        if (a.find_first_of(" \t\n\"") == std::string::npos) return a;
        std::string o = "\"";
        for (char c : a) { if (c == '"') o += "\\\""; else o += c; }
        o += "\"";
        return o;
    };
    std::string cmdline = quote(javaPath);
    for (const auto& a : args) cmdline += " " + quote(a);

    std::wstring app = http::Utf8ToWide(javaPath);
    std::wstring cl = http::Utf8ToWide(cmdline);
    std::wstring wd = http::Utf8ToWide(workDir);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(app.c_str(), cl.data(), nullptr, nullptr,
                             FALSE, CREATE_NO_WINDOW, nullptr,
                             wd.empty() ? nullptr : wd.c_str(), &si, &pi);
    if (!ok) { err = "CreateProcess 失败，错误码 " + std::to_string(GetLastError()); return ""; }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return std::to_string(code);
}

std::vector<std::string> FetchForgeVersions(const std::string& mcVersion,
                                            std::string& err) {
    std::vector<std::string> out;
    std::string url = "https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml";
    http::Response r = http::Get(url, 30000);
    if (r.status != 200) {
        err = "获取 Forge 版本失败 (HTTP " + std::to_string(r.status) + "): " + r.error;
        return out;
    }
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

bool InstallForge(const std::string& fullVersion, const std::string& gameDir,
                  const std::string& javaPath,
                  const std::function<void(const std::string&)>& log,
                  std::string& err) {
    auto info = [&](const std::string& m) { if (log) log(m); };

    // 1. 下载 installer jar
    std::string installerUrl = "https://maven.minecraftforge.net/net/minecraftforge/forge/" +
                               fullVersion + "/forge-" + fullVersion + "-installer.jar";
    fs::path installer = fs::u8path(gameDir) / fs::u8path("forge-" + fullVersion + "-installer.jar");
    if (!fileExists(installer.u8string())) {
        info("下载 Forge installer...");
        std::string derr;
        if (!downloadFallback(installerUrl, installer.u8string(), nullptr, derr)) { err = "下载 installer 失败: " + derr; return false; }
    }

    // 2. 确定 Java
    std::string java = javaPath;
    if (java.empty()) {
        auto jr = java::DetectBest(17);
        java = jr.path;
    }
    if (java.empty()) { err = "未找到 Java，请先安装 Java 或在设置中指定"; return false; }

    // 3. 运行 installer
    info("运行 Forge installer（可能需要 1-2 分钟）...");
    std::vector<std::string> args = { "-jar", installer.u8string(), "--installClient", gameDir };
    std::string code = RunJavaAndWait(java, args, gameDir, err);
    if (code.empty()) { err = "运行 installer 失败: " + err; return false; }
    if (code != "0") { err = "installer 退出码 " + code; return false; }

    info("Forge 安装完成，请刷新版本列表");
    return true;
}

std::vector<std::string> FetchNeoForgeVersions(const std::string& mcVersion,
                                               std::string& err) {
    std::vector<std::string> out;
    std::string url = std::string(kNeoForgeMaven) + "maven-metadata.xml";
    http::Response r = http::Get(url, 30000);
    if (r.status != 200) {
        err = "获取 NeoForge 版本失败 (HTTP " + std::to_string(r.status) + "): " + r.error;
        return out;
    }
    // 版本 key：去掉 "1." 前缀（1.21.4 → 21.4；26.3 保持 26.3）
    std::string key = mcVersion;
    if (key.rfind("1.", 0) == 0) key = key.substr(2);

    size_t pos = 0;
    while ((pos = r.body.find("<version>", pos)) != std::string::npos) {
        size_t e = r.body.find("</version>", pos);
        if (e == std::string::npos) break;
        std::string v = r.body.substr(pos + 9, e - pos - 9);
        // 精确前缀匹配：v == key 或 v 以 key + "." 开头（避免 21.4 误匹配 21.41）
        if (v.rfind(key, 0) == 0 &&
            (v.size() == key.size() || v[key.size()] == '.')) {
            out.push_back(v);
        }
        pos = e + 10;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

bool InstallNeoForge(const std::string& fullVersion, const std::string& gameDir,
                     const std::string& javaPath,
                     const std::function<void(const std::string&)>& log,
                     std::string& err) {
    auto info = [&](const std::string& m) { if (log) log(m); };

    std::string installerUrl = std::string(kNeoForgeMaven) +
                               fullVersion + "/neoforge-" + fullVersion + "-installer.jar";
    fs::path installer = fs::u8path(gameDir) / fs::u8path("neoforge-" + fullVersion + "-installer.jar");
    if (!fileExists(installer.u8string())) {
        info("下载 NeoForge installer...");
        std::string derr;
        if (!downloadFallback(installerUrl, installer.u8string(), nullptr, derr)) { err = "下载 installer 失败: " + derr; return false; }
    }

    std::string java = javaPath;
    if (java.empty()) { auto jr = java::DetectBest(17); java = jr.path; }
    if (java.empty()) { err = "未找到 Java，请先安装 Java 或在设置中指定"; return false; }

    info("运行 NeoForge installer（可能需要 1-2 分钟）...");
    std::vector<std::string> args = { "-jar", installer.u8string(), "--installClient", gameDir };
    std::string code = RunJavaAndWait(java, args, gameDir, err);
    if (code.empty()) { err = "运行 installer 失败: " + err; return false; }
    if (code != "0") { err = "installer 退出码 " + code; return false; }

    info("NeoForge 安装完成，请刷新版本列表");
    return true;
}

} // namespace loader
