// JdkCompiler.cpp
// Java 编译后端：检测本机 JDK，调用 javac/java 编译与运行。
#include "JdkCompiler.h"
#include "util.h"
#include <windows.h>

namespace vcb {

namespace {

std::wstring g_javac;
std::wstring g_java;

// 判断文件是否存在
bool FileExistsW(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// 在指定目录下找 javac.exe（bin 子目录）
std::wstring JavacIn(const std::wstring& dir) {
    std::wstring p = dir;
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    if (FileExistsW(p + L"javac.exe")) return p + L"javac.exe";
    if (FileExistsW(p + L"bin\\javac.exe")) return p + L"bin\\javac.exe";
    return L"";
}

// 列出某目录下所有 jdk-* 子目录，逐个尝试
std::wstring FindJdkUnder(const std::wstring& base) {
    std::wstring search = base;
    if (!search.empty() && search.back() != L'\\') search += L'\\';
    search += L"*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring found;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring d = base + L"\\" + n;
        std::wstring j = JavacIn(d);
        if (!j.empty()) { found = j; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

// 在 PATH 中查找可执行文件
std::wstring FindOnPath(const wchar_t* name) {
    wchar_t buf[MAX_PATH];
    DWORD n = SearchPathW(nullptr, name, nullptr, MAX_PATH, buf, nullptr);
    if (n > 0 && n < MAX_PATH) return std::wstring(buf);
    return L"";
}

bool DetectJdk() {
    if (!g_javac.empty()) return true;

    // 1. JAVA_HOME 环境变量
    wchar_t envBuf[4096];
    DWORD n = GetEnvironmentVariableW(L"JAVA_HOME", envBuf, 4096);
    if (n > 0 && n < 4096) {
        std::wstring j = JavacIn(envBuf);
        if (!j.empty()) {
            g_javac = j;
            std::wstring java = j;
            size_t pos = java.find(L"javac.exe");
            if (pos != std::wstring::npos) java.replace(pos, 9, L"java.exe");
            if (FileExistsW(java)) g_java = java;
            return true;
        }
    }

    // 2. PATH 中的 javac/java（Oracle javapath shim）
    std::wstring jc = FindOnPath(L"javac.exe");
    std::wstring jv = FindOnPath(L"java.exe");
    if (!jc.empty()) {
        g_javac = jc;
        g_java = jv.empty() ? L"" : jv;
        return true;
    }

    // 3. 常见 JDK 安装目录
    const wchar_t* bases[] = {
        L"C:\\Program Files\\Java",
        L"C:\\Program Files (x86)\\Java",
        L"C:\\Program Files\\Eclipse Adoptium",
        L"C:\\Program Files\\Microsoft",
        L"C:\\Program Files\\Zulu",
    };
    for (auto b : bases) {
        std::wstring j = FindJdkUnder(b);
        if (!j.empty()) {
            g_javac = j;
            std::wstring java = j;
            size_t pos = java.find(L"javac.exe");
            if (pos != std::wstring::npos) java.replace(pos, 9, L"java.exe");
            if (FileExistsW(java)) g_java = java;
            else g_java = FindOnPath(L"java.exe");
            return true;
        }
    }

    return false;
}

// 通用进程执行：捕获 stdout+stderr（合并），按 ANSI(ACP) 解码为 UTF-8
int RunProcess(const std::wstring& exe, const std::wstring& args,
               const std::wstring& cwd, std::string& outUtf8) {
    outUtf8.clear();

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = L"\"" + exe + L"\"";
    if (!args.empty()) cmd += L" " + args;

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (!ok) {
        CloseHandle(rd); CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);

    // 读取输出
    std::string raw;
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(rd, buf, sizeof(buf), &read, nullptr) && read > 0) {
        raw.append(buf, read);
    }
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    outUtf8 = WideToUtf8(AnsiToWide(raw));
    return (int)code;
}

} // namespace

bool JdkCompiler::Init(std::string& err) {
    if (DetectJdk()) return true;
    err = "未检测到 JDK（javac）。请安装 JDK 或将 JAVA_HOME 指向 JDK 目录。";
    return false;
}

bool JdkCompiler::IsAvailable() { return !g_javac.empty() || DetectJdk(); }
std::wstring JdkCompiler::JavacPath() { return g_javac; }
std::wstring JdkCompiler::JavaPath() { return g_java; }

bool JdkCompiler::Compile(const std::wstring& srcPath, const std::wstring& outDir, std::string& log) {
    if (g_javac.empty() && !DetectJdk()) {
        log = "未检测到 JDK（javac）。";
        return false;
    }
    std::wstring args = L"-encoding UTF-8 -d \"" + outDir + L"\" \"" + srcPath + L"\"";
    std::string outp;
    int code = RunProcess(g_javac, args, L"", outp);
    log = outp;
    return code == 0;
}

bool JdkCompiler::Run(const std::wstring& classDir, const std::string& className, std::string& log) {
    if (g_java.empty()) g_java = FindOnPath(L"java.exe");
    if (g_java.empty()) {
        log = "未检测到 java 运行环境。";
        return false;
    }
    std::wstring args = L"-cp \"" + classDir + L"\" " + Utf8ToWide(className);
    std::string outp;
    int code = RunProcess(g_java, args, L"", outp);
    log = outp;
    return code == 0;
}

} // namespace vcb
