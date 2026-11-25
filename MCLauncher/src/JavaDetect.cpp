// JavaDetect.cpp — Java 运行时检测（注册表 + JAVA_HOME + PATH）+ 官方运行时下载
#include "JavaDetect.h"
#include "Json.h"
#include "Http.h"
#include "MlcData.h"

#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace java {

namespace fs = std::filesystem;

namespace {

// 读取某个注册表键下所有子键名
std::vector<std::string> enumSubKeys(HKEY root, const char* subKey) {
    std::vector<std::string> out;
    HKEY h = nullptr;
    if (RegOpenKeyExA(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) return out;
    DWORD i = 0;
    char name[256];
    DWORD nameLen = sizeof(name);
    while (RegEnumKeyExA(h, i++, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        out.push_back(name);
        nameLen = sizeof(name);
    }
    RegCloseKey(h);
    return out;
}

std::string queryValue(HKEY root, const char* subKey, const char* value) {
    HKEY h = nullptr;
    if (RegOpenKeyExA(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) return "";
    char buf[1024];
    DWORD size = sizeof(buf), type = 0;
    LONG r = RegQueryValueExA(h, value, nullptr, &type, (LPBYTE)buf, &size);
    RegCloseKey(h);
    if (r == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) return buf;
    return "";
}

// 从 java.exe 解析版本号
bool probeJava(const std::string& javaExe, JavaRuntime& out) {
    if (!fs::exists(fs::u8path(javaExe))) return false;

    out.path = javaExe;
    fs::path p = fs::u8path(javaExe);
    out.home = p.parent_path().parent_path().u8string();

    // 尝试读取 release 文件（Java 9+）
    fs::path release = fs::u8path(out.home) / "release";
    if (fs::exists(release)) {
        std::ifstream f(release);
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("JAVA_VERSION=", 0) == 0) {
                out.version = line.substr(13);
                // 去掉引号
                if (!out.version.empty() && out.version.front() == '"') out.version = out.version.substr(1, out.version.size() - 2);
                break;
            }
        }
    }

    // 解析主版本号
    out.major = 0;
    if (!out.version.empty()) {
        // "1.8.0_401" → 8；"17.0.10" → 17；"1.7" → 7
        if (out.version.rfind("1.", 0) == 0) {
            out.major = atoi(out.version.c_str() + 2);
        } else {
            out.major = atoi(out.version.c_str());
        }
    }

    // 判断是否 64 位：读取 PE 头
    out.is64 = false;
    {
        HANDLE hFile = CreateFileA(javaExe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            // DOS header e_lfanew @0x3C
            DWORD read = 0;
            unsigned char dos[64];
            if (ReadFile(hFile, dos, 64, &read, nullptr) && read >= 64) {
                DWORD peOff = dos[0x3C] | (dos[0x3D] << 8) | (dos[0x3E] << 16) | (dos[0x3F] << 24);
                SetFilePointer(hFile, peOff + 4, nullptr, FILE_BEGIN); // 跳到 COFF 头 machine 字段
                unsigned char machine[2];
                if (ReadFile(hFile, machine, 2, &read, nullptr) && read == 2) {
                    unsigned short m = machine[0] | (machine[1] << 8);
                    out.is64 = (m == 0x8664); // IMAGE_FILE_MACHINE_AMD64
                }
            }
            CloseHandle(hFile);
        }
    }
    return true;
}

} // namespace

std::vector<JavaRuntime> DetectAll() {
    std::map<std::string, JavaRuntime> found; // 按 home 去重

    auto addFromHome = [&](const std::string& home) {
        if (home.empty()) return;
        for (const char* exe : {"javaw.exe", "java.exe"}) {
            std::string path = home + "\\bin\\" + exe;
            JavaRuntime jr;
            if (probeJava(path, jr)) {
                found[jr.home] = jr;
                break;
            }
        }
    };

    // 1. 注册表 JRE
    for (const auto& ver : enumSubKeys(HKEY_LOCAL_MACHINE, "SOFTWARE\\JavaSoft\\Java Runtime Environment")) {
        std::string home = queryValue(HKEY_LOCAL_MACHINE,
            ("SOFTWARE\\JavaSoft\\Java Runtime Environment\\" + ver).c_str(), "JavaHome");
        addFromHome(home);
    }
    // 2. 注册表 JDK
    for (const auto& ver : enumSubKeys(HKEY_LOCAL_MACHINE, "SOFTWARE\\JavaSoft\\Java Development Kit")) {
        std::string home = queryValue(HKEY_LOCAL_MACHINE,
            ("SOFTWARE\\JavaSoft\\Java Development Kit\\" + ver).c_str(), "JavaHome");
        addFromHome(home);
    }
    // 3. 32 位视图（WOW6432Node）
    for (const auto& ver : enumSubKeys(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\JavaSoft\\Java Runtime Environment")) {
        std::string home = queryValue(HKEY_LOCAL_MACHINE,
            ("SOFTWARE\\WOW6432Node\\JavaSoft\\Java Runtime Environment\\" + ver).c_str(), "JavaHome");
        addFromHome(home);
    }
    for (const auto& ver : enumSubKeys(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\JavaSoft\\Java Development Kit")) {
        std::string home = queryValue(HKEY_LOCAL_MACHINE,
            ("SOFTWARE\\WOW6432Node\\JavaSoft\\Java Development Kit\\" + ver).c_str(), "JavaHome");
        addFromHome(home);
    }
    // 4. JAVA_HOME
    {
        const char* jh = std::getenv("JAVA_HOME");
        if (jh && *jh) addFromHome(jh);
    }
    // 5. PATH 中的 java.exe
    {
        const char* pathEnv = std::getenv("PATH");
        if (pathEnv) {
            std::string paths = pathEnv;
            size_t start = 0;
            while (start <= paths.size()) {
                size_t semi = paths.find(';', start);
                std::string dir = (semi == std::string::npos) ? paths.substr(start) : paths.substr(start, semi - start);
                start = (semi == std::string::npos) ? paths.size() + 1 : semi + 1;
                if (dir.empty()) continue;
                JavaRuntime jr;
                if (probeJava(dir + "\\java.exe", jr) || probeJava(dir + "\\javaw.exe", jr)) {
                    found[jr.home] = jr;
                }
            }
        }
    }

    std::vector<JavaRuntime> out;
    out.reserve(found.size());
    for (auto& [k, v] : found) out.push_back(v);

    // 排序：64 位优先，其次主版本号降序
    std::sort(out.begin(), out.end(), [](const JavaRuntime& a, const JavaRuntime& b) {
        if (a.is64 != b.is64) return a.is64 > b.is64;
        return a.major > b.major;
    });
    return out;
}

JavaRuntime DetectBest(int minMajor, bool* satisfied) {
    if (satisfied) *satisfied = false;
    auto all = DetectAll();
    for (const auto& j : all) {
        if (j.is64 && j.major >= minMajor) {
            if (satisfied) *satisfied = true;
            return j;
        }
    }
    for (const auto& j : all) {
        if (j.major >= minMajor) {
            if (satisfied) *satisfied = true;
            return j;
        }
    }
    if (!all.empty()) return all.front();
    return JavaRuntime{};
}

bool Probe(const std::string& exePath, JavaRuntime& out) {
    return probeJava(exePath, out);
}

JavaRuntime DetectExact(int major) {
    if (major <= 0) return JavaRuntime{};
    // 先看下载过的那份，再扫本机（两边都精确匹配）
    std::string local = FindRuntime(major);
    if (!local.empty()) {
        JavaRuntime jr;
        if (probeJava(local, jr) && jr.major == major) return jr;
    }
    auto all = DetectAll();
    for (const auto& j : all) if (j.is64 && j.major == major) return j;
    for (const auto& j : all) if (j.major == major) return j;
    return JavaRuntime{};
}

bool IsValidJava(const std::string& path) {
    if (path.empty()) return true;  // 空 = 自动检测，视为有效
    fs::path p = fs::u8path(path);
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        // 目录：JRE/JDK 根目录，检查 bin/java.exe 或 bin/javaw.exe
        return fs::exists(p / "bin" / "java.exe", ec) || fs::exists(p / "bin" / "javaw.exe", ec);
    }
    // 文件：直接检查存在
    return fs::exists(p, ec);
}

// ---------------------------------------------------------------
// 下载官方运行时（Mojang java-runtime 清单；国内优先 BMCLAPI 镜像）
// ---------------------------------------------------------------
std::wstring RuntimeRootW() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    cached = mlc::ExeDir() + L"\\Data\\mlc\\java";
    return cached;
}

namespace {

// root 下（含一层子目录）找 javaw.exe
std::string findJavaw(const std::string& root) {
    std::error_code ec;
    fs::path r = fs::u8path(root);
    if (!fs::is_directory(r, ec)) return "";
    for (const char* rel : { "bin\\javaw.exe", "bin\\java.exe" }) {
        fs::path p = r / fs::u8path(rel);
        if (fs::is_regular_file(p, ec)) return p.u8string();
    }
    for (auto& e : fs::directory_iterator(r, ec)) {
        if (!e.is_directory(ec)) continue;
        for (const char* rel : { "bin\\javaw.exe", "bin\\java.exe" }) {
            fs::path p = e.path() / fs::u8path(rel);
            if (fs::is_regular_file(p, ec)) return p.u8string();
        }
    }
    return "";
}

std::string mirrorOf(const std::string& url) {
    static const char* hosts[] = { "https://launchermeta.mojang.com/",
                                   "https://piston-meta.mojang.com/",
                                   "https://piston-data.mojang.com/" };
    for (const char* h : hosts) {
        if (url.rfind(h, 0) == 0)
            return "https://bmclapi2.bangbang93.com/" + url.substr(strlen(h));
    }
    return url;
}

} // namespace

std::string FindRuntime(int major) {
    return findJavaw(mlc::WToUtf8(RuntimeRootW()) + "\\" + std::to_string(major));
}

bool InstallRuntime(int major, const RuntimeProgress& progress, std::string& outPath,
                    std::string& err) {
    outPath.clear();
    const std::string root = mlc::WToUtf8(RuntimeRootW());
    const std::string dest = root + "\\" + std::to_string(major);
    std::error_code ec;
    fs::create_directories(fs::u8path(dest), ec);

    if (progress && !progress("获取 Java 运行时清单…", 0, 1)) { err = "已取消"; return false; }

    // 1) all.json（列出各组件在各平台的最新版本）
    const std::string kAll = "/v1/products/java-runtime/"
                             "2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";
    std::string allBody;
    for (const char* base : { "https://bmclapi2.bangbang93.com",
                              "https://launchermeta.mojang.com" }) {
        http::Response r = http::Get(std::string(base) + kAll, 30000);
        if (r.status == 200 && !r.body.empty()) { allBody = r.body; break; }
    }
    if (allBody.empty()) { err = "无法获取 Java 运行时清单（网络或镜像不可用）"; return false; }

    // 2) 在 windows-x64 里挑 major 匹配的最新组件
    std::string manifestUrl, bestVer;
    try {
        json::Value j = json::parse(allBody);
        const json::Value& win = j.get("windows-x64");
        for (const auto& kv : win.obj) {
            const json::Value& arr = kv.second;
            for (size_t i = 0; i < arr.size(); ++i) {
                const json::Value& e = arr.at(i);
                std::string vname = e.get("version").get("name").asString();
                if (vname.empty()) continue;
                int mj = (vname.rfind("1.", 0) == 0) ? atoi(vname.c_str() + 2) : atoi(vname.c_str());
                if (mj != major) continue;
                std::string url = e.get("manifest").get("url").asString();
                if (url.empty()) continue;
                if (manifestUrl.empty() || vname > bestVer) { manifestUrl = url; bestVer = vname; }
            }
        }
    } catch (...) {
        err = "Java 运行时清单解析失败";
        return false;
    }
    if (manifestUrl.empty()) {
        err = "官方没有 Java " + std::to_string(major) + " 的运行时（该版本可能需要手动安装）";
        return false;
    }

    // 3) 该组件的文件清单
    std::string fileBody;
    for (const std::string& u : { mirrorOf(manifestUrl), manifestUrl }) {
        http::Response r = http::Get(u, 30000);
        if (r.status == 200 && !r.body.empty()) { fileBody = r.body; break; }
    }
    if (fileBody.empty()) { err = "无法获取 Java 文件清单"; return false; }

    struct Task { std::string url, dest, sha1; };
    std::vector<Task> tasks;
    try {
        json::Value j = json::parse(fileBody);
        const json::Value& files = j.get("files");
        for (const auto& kv : files.obj) {
            const json::Value& f = kv.second;
            if (f.get("type").asString() != "file") continue;
            const json::Value& raw = f.get("downloads").get("raw");
            std::string url = raw.get("url").asString();
            if (url.empty()) continue;
            std::string rel = kv.first;
            for (char& c : rel) if (c == '/') c = '\\';
            tasks.push_back({ url, dest + "\\" + rel, raw.get("sha1").asString() });
        }
    } catch (...) {
        err = "Java 文件清单解析失败";
        return false;
    }
    if (tasks.empty()) { err = "Java 文件清单为空"; return false; }

    // 4) 并发下载
    std::atomic<size_t> next{ 0 };
    std::atomic<int64_t> done{ 0 };
    std::atomic<bool> aborted{ false };
    std::mutex mu;
    std::string firstErr;

    auto worker = [&]() {
        while (!aborted.load()) {
            size_t i = next.fetch_add(1);
            if (i >= tasks.size()) return;
            const Task& t = tasks[i];

            std::error_code e2;
            auto fileOk = [&](const std::string& p) {
                std::error_code e3;
                return fs::is_regular_file(fs::u8path(p), e3);
            };
            if (!fileOk(t.dest)) {
                fs::path dp = fs::u8path(t.dest);
                fs::create_directories(dp.parent_path(), e2);
                const std::string* sha = t.sha1.empty() ? nullptr : &t.sha1;
                bool ok = false;
                for (const std::string& u : { mirrorOf(t.url), t.url }) {
                    http::DownloadResult r = http::Download(u, t.dest, nullptr, 30000, sha);
                    if (r.ok) { ok = true; break; }
                }
                if (!ok) {
                    std::lock_guard<std::mutex> lk(mu);
                    if (firstErr.empty()) firstErr = "下载失败: " + t.dest;
                    aborted = true;
                    return;
                }
            }
            int64_t d = ++done;
            if (progress && !progress("下载 Java 运行时 " + std::to_string(major),
                                      d, (int64_t)tasks.size())) {
                aborted = true;
                return;
            }
        }
    };

    std::vector<std::thread> threads;
    int n = std::min<int>(16, (int)tasks.size());
    for (int i = 0; i < n; ++i) threads.emplace_back(worker);
    for (auto& th : threads) if (th.joinable()) th.join();

    if (aborted.load()) {
        err = firstErr.empty() ? "已取消" : firstErr;
        return false;
    }

    // 5) 定位 javaw.exe
    outPath = findJavaw(dest);
    if (outPath.empty()) {
        err = "Java 下载完成但没找到 bin\\javaw.exe（清单结构可能变了）";
        return false;
    }
    if (progress) progress("Java 运行时就绪", (int64_t)tasks.size(), (int64_t)tasks.size());
    return true;
}

} // namespace java
