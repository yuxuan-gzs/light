// JavaDetect.cpp — Java 运行时检测（注册表 + JAVA_HOME + PATH）
#include "JavaDetect.h"

#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <map>
#include <cstdlib>
#include <fstream>
#include <string>

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

JavaRuntime DetectBest(int minMajor) {
    auto all = DetectAll();
    for (const auto& j : all) {
        if (j.is64 && j.major >= minMajor) return j;
    }
    for (const auto& j : all) {
        if (j.major >= minMajor) return j;
    }
    if (!all.empty()) return all.front();
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

} // namespace java
