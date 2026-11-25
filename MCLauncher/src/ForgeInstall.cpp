// ForgeInstall.cpp — HMCL 式 Forge / NeoForge 安装
//
// 和 HMCL 一样，**不运行 installer 的 main**（那套会自己联网、对 Java 大版本敏感、
// 失败时只留一个退出码，国内基本不可用）。改成：
//   * 读 installer 内的 install_profile.json
//   * 新格式（含 "spec"）：复制 installer 内 maven/ 里的库 → 下载其余库 →
//     按 processors 逐个 java -cp 执行（客户端只跑 sides 含 client 或未标注的）
//   * 旧格式（含 "install" + "versionInfo"）：从 installer 内解压 universal jar →
//     直接把 versionInfo 写成版本 JSON（完全不需要 Java）
// 版本 JSON 来源：新格式取 installer 内的 version.json，旧格式取 install_profile.versionInfo。
#include "LoaderInstall.h"
#include "Json.h"
#include "Http.h"
#include "Zip.h"
#include "Crypto.h"
#include "JavaDetect.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace loader {

namespace {

// ---------------------------------------------------------------
// maven 坐标 → 本地相对路径
// ---------------------------------------------------------------
struct Coord {
    std::string group, artifact, version, classifier, ext = "jar";
};

bool parseCoord(const std::string& s, Coord& c) {
    if (s.empty()) return false;
    std::string t = s;
    size_t at = t.find('@');
    if (at != std::string::npos) {
        c.ext = t.substr(at + 1);
        t = t.substr(0, at);
    }
    std::vector<std::string> p;
    size_t pos = 0;
    while (true) {
        size_t n = t.find(':', pos);
        if (n == std::string::npos) { p.push_back(t.substr(pos)); break; }
        p.push_back(t.substr(pos, n - pos));
        pos = n + 1;
    }
    if (p.size() < 3) return false;
    c.group = p[0];
    c.artifact = p[1];
    c.version = p[2];
    if (p.size() >= 4) c.classifier = p[3];
    if (c.ext.empty()) c.ext = "jar";
    return true;
}

std::string coordPath(const Coord& c) {
    std::string g = c.group;
    std::replace(g.begin(), g.end(), '.', '/');
    std::string file = c.artifact + "-" + c.version +
                       (c.classifier.empty() ? "" : "-" + c.classifier) + "." + c.ext;
    return g + "/" + c.artifact + "/" + c.version + "/" + file;
}

std::string coordPathOf(const std::string& desc) {
    Coord c;
    if (!parseCoord(desc, c)) return "";
    return coordPath(c);
}

// ---------------------------------------------------------------
// 文件 / 目录
// ---------------------------------------------------------------
std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "\\" + b;
}

bool fileExists(const std::string& p, int64_t* sizeOut = nullptr) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(p), ec) || ec) return false;
    if (sizeOut) {
        auto sz = fs::file_size(fs::u8path(p), ec);
        *sizeOut = ec ? 0 : (int64_t)sz;
    }
    return true;
}

bool writeFile(const std::string& path, const std::string& data) {
    std::error_code ec;
    fs::create_directories(fs::u8path(path).parent_path(), ec);
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty()) f.write(data.data(), (std::streamsize)data.size());
    return f.good();
}

// 从 installer 里取出 maven/<path> 写到 dest（installer 自带一部分库，不用下载）
bool copyFromInstallerMaven(const std::string& installer, const std::string& relPath,
                           const std::string& dest, std::string& err) {
    std::string data;
    if (!zip::ReadEntry(installer, "maven/" + relPath, data) || data.empty()) return false;
    if (!writeFile(dest, data)) { err = "写入失败: " + dest; return false; }
    return true;
}

// ---------------------------------------------------------------
// 下载（官方 → BMCLAPI 镜像）
// ---------------------------------------------------------------
bool downloadWithMirror(const std::string& url, const std::string& dest,
                        const std::string* sha1, std::string& err) {
    std::vector<std::string> urls{ url };
    static const struct { const char* from; const char* to; } maps[] = {
        { "https://maven.minecraftforge.net/", "https://bmclapi2.bangbang93.com/maven/" },
        { "https://libraries.minecraft.net/", "https://bmclapi2.bangbang93.com/maven/" },
        { "https://maven.neoforged.net/releases/", "https://bmclapi2.bangbang93.com/maven/" },
        { "https://piston-data.mojang.com/", "https://bmclapi2.bangbang93.com/" },
        { "https://launchermeta.mojang.com/", "https://bmclapi2.bangbang93.com/" },
    };
    for (const auto& m : maps) {
        if (url.rfind(m.from, 0) == 0) {
            urls.push_back(std::string(m.to) + url.substr(strlen(m.from)));
            break;
        }
    }
    std::error_code ec;
    fs::create_directories(fs::u8path(dest).parent_path(), ec);
    std::string last;
    for (const auto& u : urls) {
        http::DownloadResult r = http::Download(u, dest, nullptr, 180000, sha1);
        if (r.ok) return true;
        last = r.error;
    }
    err = last.empty() ? "下载失败" : last;
    return false;
}

// ---------------------------------------------------------------
// 跑 java（输出追加到日志文件，失败时把尾部带回）
// ---------------------------------------------------------------
std::string tailOfFile(const std::string& path, size_t maxBytes = 4096) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return "";
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    std::streamoff start = (std::streamoff)maxBytes < n ? n - (std::streamoff)maxBytes : 0;
    f.seekg(start, std::ios::beg);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}

std::string firstInterestingLines(const std::string& text) {
    std::string out;
    size_t pos = 0;
    int picked = 0;
    while (pos < text.size() && picked < 3) {
        size_t nl = text.find('\n', pos);
        std::string line = (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? text.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        bool bad = line.find("Exception") != std::string::npos ||
                   line.find("Caused by") != std::string::npos ||
                   line.find("Error") != std::string::npos ||
                   line.find("error") != std::string::npos ||
                   line.find("Failed") != std::string::npos ||
                   line.find("failed") != std::string::npos ||
                   line.find("Could not") != std::string::npos;
        if (bad) {
            if (!out.empty()) out += " | ";
            out += line.substr(0, 240);
            ++picked;
        }
    }
    return out;
}

bool runJava(const std::string& exe, const std::vector<std::string>& args,
             const std::string& workDir, const std::string& logFile,
             std::string& err) {
    auto quote = [](const std::string& a) {
        if (a.empty()) return std::string("\"\"");
        if (a.find_first_of(" \t\n\"") == std::string::npos) return a;
        std::string o = "\"";
        for (char c : a) { if (c == '"') o += "\\\""; else o += c; }
        o += "\"";
        return o;
    };
    std::string cmdline = quote(exe);
    for (const auto& a : args) cmdline += " " + quote(a);

    std::error_code ec;
    fs::create_directories(fs::u8path(logFile).parent_path(), ec);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    std::wstring wlog = http::Utf8ToWide(logFile);
    HANDLE hLog = CreateFileW(wlog.c_str(), FILE_APPEND_DATA | FILE_READ_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hLog == INVALID_HANDLE_VALUE) hLog = nullptr;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (hLog) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = hLog;
        si.hStdError = hLog;
    }

    std::wstring app = http::Utf8ToWide(exe);
    std::wstring cl = http::Utf8ToWide(cmdline);
    std::wstring wd = http::Utf8ToWide(workDir);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(app.c_str(), cl.data(), nullptr, nullptr,
                             hLog ? TRUE : FALSE, CREATE_NO_WINDOW, nullptr,
                             wd.empty() ? nullptr : wd.c_str(), &si, &pi);
    if (!ok) {
        DWORD e = GetLastError();
        if (hLog) CloseHandle(hLog);
        err = "启动 java 失败，错误码 " + std::to_string(e);
        return false;
    }
    CloseHandle(pi.hThread);
    DWORD wait = WaitForSingleObject(pi.hProcess, 10 * 60 * 1000);
    DWORD code = 0;
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (hLog) CloseHandle(hLog);

    if (wait == WAIT_TIMEOUT) { err = "安装步骤超时（10 分钟）"; return false; }
    if (code != 0) {
        std::string detail = firstInterestingLines(tailOfFile(logFile));
        err = "安装步骤退出码 " + std::to_string(code) +
              (detail.empty() ? "" : "：" + detail);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------
// 参数模板：{VAR} / 'literal' / [maven 坐标] / 含 {VAR} 的文本
// ---------------------------------------------------------------
std::string replaceTokens(const std::string& in, const std::map<std::string, std::string>& vars) {
    std::string out;
    out.reserve(in.size() + 32);
    size_t i = 0;
    while (i < in.size()) {
        if (in[i] == '\\' && i + 1 < in.size()) { out += in[i + 1]; i += 2; continue; }
        if (in[i] == '{') {
            size_t e = in.find('}', i + 1);
            if (e == std::string::npos) { out += in.substr(i); break; }
            std::string key = in.substr(i + 1, e - i - 1);
            auto it = vars.find(key);
            out += (it == vars.end() ? std::string("{") + key + "}" : it->second);
            i = e + 1;
            continue;
        }
        out += in[i++];
    }
    return out;
}

// libDir 用于把 [maven] 展开成本地绝对路径
std::string parseLiteral(const std::string& lit, const std::map<std::string, std::string>& vars,
                         const std::string& libDir) {
    if (lit.size() >= 2) {
        if (lit.front() == '{' && lit.back() == '}') {
            std::string key = lit.substr(1, lit.size() - 2);
            auto it = vars.find(key);
            return it == vars.end() ? std::string() : it->second;
        }
        if (lit.front() == '\'' && lit.back() == '\'') return lit.substr(1, lit.size() - 2);
        if (lit.front() == '[' && lit.back() == ']') {
            std::string rel = coordPathOf(lit.substr(1, lit.size() - 2));
            return rel.empty() ? std::string() : joinPath(libDir, rel);
        }
    }
    return replaceTokens(lit, vars);
}

// 读 jar 的 MANIFEST.MF 里的 Main-Class
std::string mainClassOf(const std::string& jar) {
    std::string mf;
    if (!zip::ReadEntry(jar, "META-INF/MANIFEST.MF", mf)) return "";
    size_t pos = 0;
    while (pos < mf.size()) {
        size_t nl = mf.find('\n', pos);
        std::string line = (nl == std::string::npos) ? mf.substr(pos) : mf.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? mf.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("Main-Class:", 0) == 0) {
            std::string v = line.substr(11);
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
            return v;
        }
    }
    return "";
}

// 下载库（先看 installer 内有没有）
bool prepareLibraries(const std::string& installer, const json::Value& libs,
                      const std::string& libDir,
                      const std::function<void(const std::string&)>& log, std::string& err) {
    int total = (int)libs.size();
    int done = 0;
    for (size_t i = 0; i < libs.size(); ++i) {
        const json::Value& l = libs.at(i);
        std::string rel = l.get("downloads").get("artifact").get("path").asString();
        if (rel.empty()) rel = coordPathOf(l.get("name").asString());
        if (rel.empty()) continue;

        std::string dest = joinPath(libDir, rel);
        int64_t sz = 0;
        if (fileExists(dest, &sz) && sz > 0) { ++done; continue; }

        // 1) installer 自带（forge 的 maven/ 目录）
        if (copyFromInstallerMaven(installer, rel, dest, err)) { ++done; continue; }

        // 2) 从网上下
        std::string url = l.get("downloads").get("artifact").get("url").asString();
        std::string sha1 = l.get("downloads").get("artifact").get("sha1").asString();
        if (url.empty()) { ++done; continue; }
        std::string derr;
        const std::string* shaPtr = sha1.size() == 40 ? &sha1 : nullptr;
        if (!downloadWithMirror(url, dest, shaPtr, derr)) {
            err = "下载 " + rel + " 失败：" + derr;
            return false;
        }
        ++done;
        if (log && (done % 5 == 0 || done == total))
            log("库文件 " + std::to_string(done) + " / " + std::to_string(total));
    }
    return true;
}

// DOWNLOAD_MOJMAPS 是唯一一个"纯下载"的 processor，它自己直连 Mojang 会卡很久
// （实测官方 installer 有 8 分钟里大半耗在这）。像 HMCL 一样改成从版本 JSON 的
// downloads.client_mappings 拿地址，走 BMCLAPI 镜像下载。
// 返回 false 表示出错；handled 表示这个 processor 已经被我们处理完。
bool patchMojmaps(const std::string& gameDir, const std::string& mcVersion,
                  const std::vector<std::string>& args, bool& handled, std::string& err) {
    handled = false;
    std::string task, output, ver;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--task") task = args[i + 1];
        else if (args[i] == "--output") output = args[i + 1];
        else if (args[i] == "--version") ver = args[i + 1];
    }
    if (task != "DOWNLOAD_MOJMAPS" || output.empty()) return true;
    if (ver.empty()) ver = mcVersion;

    handled = true;
    int64_t sz = 0;
    if (fileExists(output, &sz) && sz > 0) return true;   // 已经有了

    std::string vjsonPath = joinPath(joinPath(joinPath(gameDir, "versions"), ver),
                                     ver + ".json");
    std::string text;
    {
        std::ifstream f(fs::u8path(vjsonPath), std::ios::binary);
        if (!f) { handled = false; return true; }          // 没有版本 JSON：交回原 processor
        text.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    std::string url, sha1;
    try {
        json::Value j = json::parse(text);
        const json::Value& cm = j.get("downloads").get("client_mappings");
        url = cm.get("url").asString();
        sha1 = cm.get("sha1").asString();
    } catch (...) {}
    if (url.empty()) { handled = false; return true; }

    const std::string* shaPtr = sha1.size() == 40 ? &sha1 : nullptr;
    if (!downloadWithMirror(url, output, shaPtr, err)) {
        err = "下载 mojmaps 失败：" + err;
        return false;
    }
    return true;
}

// 新格式：按 processors 跑 java
bool runProcessors(const std::string& installer, const json::Value& profile,
                   const std::string& gameDir, const std::string& libDir,
                   const std::string& javaPath, const std::string& mcVersion,
                   const std::string& logFile,
                   const std::function<void(const std::string&)>& log, std::string& err) {
    // ---- 1. data → vars（/xxx 是 installer 内的文件，要解压到临时目录）----
    std::map<std::string, std::string> vars;
    std::wstring tmpRoot;
    {
        wchar_t buf[MAX_PATH] = { 0 };
        GetTempPathW(MAX_PATH, buf);
        tmpRoot = std::wstring(buf) + L"yxg_forge_" + std::to_wstring(GetTickCount64() % 1000000);
        std::error_code ec;
        fs::create_directories(fs::u8path(http::WideToUtf8(tmpRoot)), ec);
    }
    std::vector<std::string> tmpFiles;

    const json::Value& data = profile.get("data");
    for (const auto& kv : data.obj) {
        const json::Value& v = kv.second;
        // 值是 {client:..., server:...} 或直接字符串
        std::string raw = v.isObject() ? v.get("client").asString() : v.asString();
        if (raw.empty()) continue;

        if (raw.front() == '/') {
            // installer 内路径 → 解压到临时目录
            std::string inner = raw.substr(1);
            std::string bytes;
            if (!zip::ReadEntry(installer, inner, bytes) || bytes.empty()) {
                err = "installer 内缺少 " + inner;
                return false;
            }
            std::string name = inner;
            std::replace(name.begin(), name.end(), '/', '_');
            std::string dest = http::WideToUtf8(tmpRoot) + "\\" + name;
            if (!writeFile(dest, bytes)) { err = "解压失败: " + inner; return false; }
            tmpFiles.push_back(dest);
            vars[kv.first] = dest;
        } else {
            vars[kv.first] = parseLiteral(raw, {}, libDir);
        }
    }

    // ---- 2. 固定变量 ----
    vars["SIDE"] = "client";
    vars["MINECRAFT_VERSION"] = mcVersion;
    vars["ROOT"] = gameDir;
    vars["INSTALLER"] = installer;
    vars["LIBRARY_DIR"] = libDir;
    std::string mcJar = joinPath(joinPath(joinPath(gameDir, "versions"), mcVersion),
                                 mcVersion + ".jar");
    if (!fileExists(mcJar)) {
        err = "缺少原版客户端 " + mcVersion + ".jar，请先安装原版 " + mcVersion;
        return false;
    }
    vars["MINECRAFT_JAR"] = mcJar;

    // ---- 3. 逐个 processor ----
    const json::Value& procs = profile.get("processors");
    int total = (int)procs.size();
    int done = 0;
    for (size_t i = 0; i < procs.size(); ++i) {
        const json::Value& pr = procs.at(i);
        ++done;

        // sides 过滤：只跑 client（未标注 sides 的两边都跑）
        const json::Value& sides = pr.get("sides");
        if (sides.size() > 0) {
            bool hasClient = false;
            for (size_t s = 0; s < sides.size(); ++s)
                if (sides.at(s).asString() == "client") hasClient = true;
            if (!hasClient) continue;
        }

        // outputs 检查：产物都在就跳过这个 processor（重复安装时很省时间）
        const json::Value& outputs = pr.get("outputs");
        if (outputs.isObject() && !outputs.obj.empty()) {
            bool allOk = true;
            for (const auto& kv : outputs.obj) {
                std::string path = parseLiteral(kv.first, vars, libDir);
                int64_t sz = 0;
                if (path.empty() || !fileExists(path, &sz) || sz == 0) { allOk = false; break; }
            }
            if (allOk) continue;
        }

        std::string jarRel = coordPathOf(pr.get("jar").asString());
        if (jarRel.empty()) { err = "processor 的 jar 坐标无效"; return false; }
        std::string jar = joinPath(libDir, jarRel);

        // classpath = 依赖 + 自己
        std::vector<std::string> cp;
        const json::Value& cls = pr.get("classpath");
        for (size_t c = 0; c < cls.size(); ++c) {
            std::string rel = coordPathOf(cls.at(c).asString());
            if (rel.empty()) continue;
            std::string p = joinPath(libDir, rel);
            if (!fileExists(p)) { err = "缺少 processor 依赖：" + rel; return false; }
            cp.push_back(p);
        }
        cp.push_back(jar);

        // args 先解析：纯下载类 processor（DOWNLOAD_MOJMAPS）要先用镜像下掉
        std::vector<std::string> args;
        const json::Value& prArgs = pr.get("args");
        for (size_t a = 0; a < prArgs.size(); ++a) {
            args.push_back(parseLiteral(prArgs.at(a).asString(), vars, libDir));
        }
        {
            bool handled = false;
            if (!patchMojmaps(gameDir, mcVersion, args, handled, err)) return false;
            if (handled) {
                if (log) log("已下载 mojmaps（镜像）");
                continue;
            }
        }

        if (!fileExists(jar)) { err = "缺少 processor 依赖：" + jarRel; return false; }

        std::string cpStr;
        for (size_t c = 0; c < cp.size(); ++c) {
            if (c) cpStr += ";";
            cpStr += cp[c];
        }

        std::string mainClass = mainClassOf(jar);
        if (mainClass.empty()) { err = "processor jar 没有 Main-Class：" + jarRel; return false; }

        if (log) {
            log("安装步骤 " + std::to_string(done) + " / " + std::to_string(total) +
                "：" + mainClass);
        }

        std::vector<std::string> cmd = { "-cp", cpStr, mainClass };
        cmd.insert(cmd.end(), args.begin(), args.end());
        std::string rerr;
        if (!runJava(javaPath, cmd, gameDir, logFile, rerr)) {
            err = "第 " + std::to_string(done) + " 步失败：" + rerr;
            return false;
        }
    }
    return true;
}

// 旧格式（≤1.12.2）：解压 universal jar + 写 versionInfo
bool installLegacy(const std::string& installer, const json::Value& profile,
                   const std::string& gameDir, const std::string& libDir,
                   const std::function<void(const std::string&)>& log,
                   std::string& verId, std::string& err) {
    const json::Value& install = profile.get("install");
    std::string filePath = install.get("filePath").asString();   // forge-1.12.2-...-universal.jar
    std::string coord = install.get("path").asString();          // net.minecraftforge:forge:...
    if (filePath.empty() || coord.empty()) { err = "旧格式 install_profile 缺少 install 字段"; return false; }

    std::string rel = coordPathOf(coord);
    if (rel.empty()) { err = "install.path 坐标无效: " + coord; return false; }
    std::string dest = joinPath(libDir, rel);
    std::string bytes;
    if (!zip::ReadEntry(installer, filePath, bytes) || bytes.empty()) {
        err = "installer 内缺少 " + filePath;
        return false;
    }
    if (!writeFile(dest, bytes)) { err = "写入 " + rel + " 失败"; return false; }
    if (log) log("已解压 " + filePath);

    const json::Value& vi = profile.get("versionInfo");
    if (!vi.isObject()) { err = "旧格式 install_profile 缺少 versionInfo"; return false; }
    verId = vi.get("id").asString();
    if (verId.empty()) { err = "versionInfo 没有 id"; return false; }

    std::string vdir = joinPath(joinPath(gameDir, "versions"), verId);
    if (!writeFile(joinPath(vdir, verId + ".json"), json::dumpPretty(vi))) {
        err = "写入版本 JSON 失败";
        return false;
    }
    if (log) log("已写入版本 " + verId);
    return true;
}

} // namespace

// ---------------------------------------------------------------
// 对外：从 installer jar 安装（新/旧格式自动判断）
// ---------------------------------------------------------------
bool InstallFromInstaller(const std::string& installerPath, const std::string& gameDir,
                          const std::string& javaPath,
                          const std::function<void(const std::string&)>& log,
                          std::string& outVersionId, std::string& err) {
    auto info = [&](const std::string& m) { if (log) log(m); };

    std::string profileText;
    if (!zip::ReadEntry(installerPath, "install_profile.json", profileText) || profileText.empty()) {
        err = "这个 installer 里没有 install_profile.json";
        return false;
    }

    json::Value profile;
    try {
        profile = json::parse(profileText);
    } catch (const std::exception& e) {
        err = std::string("install_profile.json 解析失败：") + e.what();
        return false;
    }

    const std::string libDir = joinPath(gameDir, "libraries");
    const std::string logFile = joinPath(joinPath(gameDir, "logs"), "loader-install.log");

    // ---- 旧格式 ----
    if (profile.has("install") && profile.has("versionInfo")) {
        info("识别为旧格式 installer（≤1.12.2），直接解压安装");
        return installLegacy(installerPath, profile, gameDir, libDir, log, outVersionId, err);
    }

    // ---- 新格式 ----
    if (!profile.has("spec")) {
        err = "无法识别的 installer 格式（既没有 spec 也没有 install）";
        return false;
    }

    const std::string mcVersion = profile.get("minecraft").asString();
    if (mcVersion.empty()) { err = "install_profile 缺少 minecraft 字段"; return false; }

    if (javaPath.empty()) { err = "没有可用的 Java 运行时"; return false; }

    // 1. 版本 JSON
    std::string jsonEntry = profile.get("json").asString();
    if (!jsonEntry.empty() && jsonEntry.front() == '/') jsonEntry = jsonEntry.substr(1);
    std::string verJson;
    if (!jsonEntry.empty() && zip::ReadEntry(installerPath, jsonEntry, verJson) && !verJson.empty()) {
        json::Value vj;
        try { vj = json::parse(verJson); } catch (...) {}
        outVersionId = vj.get("id").asString();
        if (outVersionId.empty()) outVersionId = profile.get("version").asString();
        if (!outVersionId.empty()) {
            std::string vdir = joinPath(joinPath(gameDir, "versions"), outVersionId);
            if (!writeFile(joinPath(vdir, outVersionId + ".json"), verJson)) {
                err = "写入版本 JSON 失败";
                return false;
            }
            info("已写入版本 " + outVersionId);
        }
    } else {
        outVersionId = profile.get("version").asString();
    }

    // 2. 库：先复制 installer 自带的 maven/，其余下载
    info("准备安装库…");
    if (!prepareLibraries(installerPath, profile.get("libraries"), libDir, log, err)) return false;

    // 3. 主 jar（profile.path 指向的 shim/universal）通常也在 maven/ 里
    {
        std::string mainRel = coordPathOf(profile.get("path").asString());
        if (!mainRel.empty()) {
            std::string dest = joinPath(libDir, mainRel);
            int64_t sz = 0;
            if (!fileExists(dest, &sz) || sz == 0) {
                std::string derr;
                copyFromInstallerMaven(installerPath, mainRel, dest, derr);
            }
        }
    }

    // 4. processors
    const json::Value& procs = profile.get("processors");
    if (procs.size() == 0) {
        info("没有需要执行的安装步骤，完成");
        return true;
    }
    info("执行安装步骤（" + std::to_string((int)procs.size()) + " 个）…");
    if (!runProcessors(installerPath, profile, gameDir, libDir, javaPath, mcVersion,
                       logFile, log, err)) {
        return false;
    }

    info("安装步骤全部完成");
    return true;
}

// ---------------------------------------------------------------
// Forge / NeoForge 入口：下载 installer → 交给通用安装
// ---------------------------------------------------------------
bool InstallForge(const std::string& fullVersion, const std::string& gameDir,
                  const std::string& javaPath,
                  const std::function<void(const std::string&)>& log,
                  std::string& err) {
    auto info = [&](const std::string& m) { if (log) log(m); };

    std::string mcPart = fullVersion;
    {
        size_t p = fullVersion.find('-');
        if (p != std::string::npos && p > 0) mcPart = fullVersion.substr(0, p);
    }

    std::string java = javaPath;
    if (java.empty()) {
        java = PickInstallerJava(mcPart, "", err);
        if (java.empty()) return false;
    }

    std::string url = "https://maven.minecraftforge.net/net/minecraftforge/forge/" +
                      fullVersion + "/forge-" + fullVersion + "-installer.jar";
    std::string installer = joinPath(gameDir, "forge-" + fullVersion + "-installer.jar");
    int64_t sz = 0;
    if (!fileExists(installer, &sz) || sz < 100000) {
        info("下载 Forge installer…");
        std::string derr;
        if (!downloadWithMirror(url, installer, nullptr, derr)) {
            err = "下载 installer 失败：" + derr;
            return false;
        }
    }

    std::string verId;
    if (!InstallFromInstaller(installer, gameDir, java, log, verId, err)) return false;
    info("Forge 安装完成：" + verId);
    return true;
}

bool InstallNeoForge(const std::string& fullVersion, const std::string& gameDir,
                     const std::string& javaPath,
                     const std::function<void(const std::string&)>& log,
                     std::string& err) {
    auto info = [&](const std::string& m) { if (log) log(m); };

    // 1.20.1 的 NeoForge 在 net/neoforged/forge 下，artifact 名也是 forge
    const bool legacy = fullVersion.rfind("1.", 0) == 0;
    const std::string base = legacy
        ? "https://maven.neoforged.net/releases/net/neoforged/forge/"
        : "https://maven.neoforged.net/releases/net/neoforged/neoforge/";
    const std::string art = legacy ? "forge" : "neoforge";

    std::string mcPart;
    if (legacy) {
        size_t p = fullVersion.find('-');
        mcPart = (p == std::string::npos || p == 0) ? fullVersion : fullVersion.substr(0, p);
    } else {
        int a = 0, b = 0, c = 0;
        if (sscanf(fullVersion.c_str(), "%d.%d.%d", &a, &b, &c) >= 2)
            mcPart = "1." + std::to_string(a) + "." + std::to_string(b);
    }
    if (mcPart.empty()) mcPart = "1.21";

    std::string java = javaPath;
    if (java.empty()) {
        java = PickInstallerJava(mcPart, "", err);
        if (java.empty()) return false;
    }

    std::string url = base + fullVersion + "/" + art + "-" + fullVersion + "-installer.jar";
    std::string installer = joinPath(gameDir, "neoforge-" + fullVersion + "-installer.jar");
    int64_t sz = 0;
    if (!fileExists(installer, &sz) || sz < 100000) {
        info("下载 NeoForge installer…");
        std::string derr;
        if (!downloadWithMirror(url, installer, nullptr, derr)) {
            err = "下载 installer 失败：" + derr;
            return false;
        }
    }

    std::string verId;
    if (!InstallFromInstaller(installer, gameDir, java, log, verId, err)) return false;
    info("NeoForge 安装完成：" + verId);
    return true;
}

} // namespace loader
