// MsvcCompiler.cpp
// C++ 编译后端：检测本机 MSVC，调用 cl.exe 编译 C++ 代码为 exe。
#include "MsvcCompiler.h"
#include "util.h"
#include <windows.h>
#include <algorithm>

namespace vcb {

namespace {

std::wstring g_cl;        // cl.exe 完整路径
std::wstring g_ts;        // 工具集根（...\VC\Tools\MSVC\<ver>）
std::wstring g_kit;       // Windows Kits 根（...\Windows Kits\10）
std::wstring g_sdk;       // SDK 版本（如 10.0.26100.0）

bool FileExistsW(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool DirExistsW(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// 在 dir 下（有限深度）查找 VC\Tools\MSVC\<ver>\bin\Hostx64\x64\cl.exe
std::wstring FindClRecursive(const std::wstring& dir, int depth) {
    if (depth <= 0 || !DirExistsW(dir)) return L"";

    // 检查 dir\VC\Tools\MSVC\* 下的 cl.exe
    std::wstring vcTools = dir + L"\\VC\\Tools\\MSVC";
    if (DirExistsW(vcTools)) {
        std::wstring search = vcTools + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(search.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                std::wstring n = fd.cFileName;
                if (n == L"." || n == L"..") continue;
                std::wstring cl = vcTools + L"\\" + n + L"\\bin\\Hostx64\\x64\\cl.exe";
                if (FileExistsW(cl)) { FindClose(h); return cl; }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    // 递归子目录
    std::wstring search = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring found;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        found = FindClRecursive(dir + L"\\" + n, depth - 1);
        if (!found.empty()) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

// 遍历盘符，在 X:\Microsoft Visual Studio 下找 cl.exe
std::wstring FindCl() {
    for (char d = 'A'; d <= 'Z'; ++d) {
        std::wstring root = std::wstring(1, (wchar_t)d) + L":\\";
        std::wstring cl = FindClRecursive(root + L"Microsoft Visual Studio", 6);
        if (!cl.empty()) return cl;
        cl = FindClRecursive(root + L"Program Files\\Microsoft Visual Studio", 7);
        if (!cl.empty()) return cl;
    }
    return L"";
}

// 遍历盘符，找 Windows Kits 10 与最新 SDK 版本
void FindKit() {
    for (char d = 'A'; d <= 'Z'; ++d) {
        std::wstring kit = std::wstring(1, (wchar_t)d) + L":\\Windows Kits\\10";
        std::wstring inc = kit + L"\\Include";
        if (!DirExistsW(inc)) continue;
        // 枚举版本目录，取字母序最大的（即最新）
        std::wstring best;
        std::wstring search = inc + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(search.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                std::wstring n = fd.cFileName;
                if (n == L"." || n == L"..") continue;
                // 只选 SDK 版本目录（形如 10.0.xxxxx.0），忽略 wdf 等其它目录
                if (n.size() >= 3 && n[0] == L'1' && n[1] == L'0' && n[2] == L'.') {
                    if (n > best) best = n;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (!best.empty()) { g_kit = kit; g_sdk = best; return; }
    }
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
    if (!ok) { CloseHandle(rd); CloseHandle(wr); return -1; }
    CloseHandle(wr);

    std::string raw;
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(rd, buf, sizeof(buf), &read, nullptr) && read > 0) raw.append(buf, read);
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

bool MsvcCompiler::Init(std::string& err) {
    if (!g_cl.empty()) return true;

    g_cl = FindCl();
    if (g_cl.empty()) { err = "未检测到 MSVC（cl.exe）。"; return false; }

    // 工具集根 = cl 路径去掉 \bin\Hostx64\x64\cl.exe
    std::wstring suffix = L"\\bin\\Hostx64\\x64\\cl.exe";
    if (g_cl.size() > suffix.size() &&
        g_cl.compare(g_cl.size() - suffix.size(), suffix.size(), suffix) == 0) {
        g_ts = g_cl.substr(0, g_cl.size() - suffix.size());
    } else {
        g_ts = g_cl.substr(0, g_cl.find_last_of(L"\\"));
    }

    FindKit();
    if (g_kit.empty()) { err = "未检测到 Windows SDK（Windows Kits）。"; g_cl.clear(); return false; }
    return true;
}

bool MsvcCompiler::IsAvailable() {
    if (!g_cl.empty()) return true;
    std::string e;
    return Init(e);
}
std::wstring MsvcCompiler::ClPath() { return g_cl; }

bool MsvcCompiler::CompileFile(const std::wstring& srcPath, const std::wstring& outExe, std::string& log,
                               const std::string& extraFlags) {
    if (g_cl.empty()) {
        std::string e;
        if (!Init(e)) { log = e; return false; }
    }

    // 构造 INCLUDE / LIB
    std::wstring inc = g_ts + L"\\include;" + g_ts + L"\\atlmfc\\include;" +
                       g_kit + L"\\Include\\" + g_sdk + L"\\ucrt;" +
                       g_kit + L"\\Include\\" + g_sdk + L"\\shared;" +
                       g_kit + L"\\Include\\" + g_sdk + L"\\um;" +
                       g_kit + L"\\Include\\" + g_sdk + L"\\winrt;" +
                       g_kit + L"\\Include\\" + g_sdk + L"\\cppwinrt";
    std::wstring lib = g_ts + L"\\lib\\x64;" + g_ts + L"\\atlmfc\\lib\\x64;" +
                       g_kit + L"\\Lib\\" + g_sdk + L"\\ucrt\\x64;" +
                       g_kit + L"\\Lib\\" + g_sdk + L"\\um\\x64";
    std::wstring msvcBin = g_cl.substr(0, g_cl.find_last_of(L"\\"));
    std::wstring kitBin = g_kit + L"\\bin\\" + g_sdk + L"\\x64";

    // 保存旧环境
    wchar_t oldInc[8192] = {0}, oldLib[8192] = {0}, oldPath[8192] = {0};
    wchar_t oldTmp[512] = {0}, oldTemp[512] = {0};
    GetEnvironmentVariableW(L"INCLUDE", oldInc, 8192);
    GetEnvironmentVariableW(L"LIB", oldLib, 8192);
    GetEnvironmentVariableW(L"PATH", oldPath, 8192);
    GetEnvironmentVariableW(L"TMP", oldTmp, 512);
    GetEnvironmentVariableW(L"TEMP", oldTemp, 512);

    SetEnvironmentVariableW(L"INCLUDE", inc.c_str());
    SetEnvironmentVariableW(L"LIB", lib.c_str());
    std::wstring newPath = msvcBin + L";" + kitBin + L";" + std::wstring(oldPath);
    SetEnvironmentVariableW(L"PATH", newPath.c_str());

    // 编译
    std::string flags = extraFlags.empty() ? "/MT /W3" : extraFlags;
    std::wstring args = L"/nologo /EHsc /utf-8 " + Utf8ToWide(flags) + L" /Fe:\"" + outExe + L"\" \"" + srcPath + L"\"";
    std::wstring cwd = outExe;
    size_t pos = cwd.find_last_of(L"\\");
    if (pos != std::wstring::npos) cwd = cwd.substr(0, pos);
    // 确保 TMP/TEMP 指向可写目录（cl 链接时需创建响应文件）
    SetEnvironmentVariableW(L"TMP", cwd.c_str());
    SetEnvironmentVariableW(L"TEMP", cwd.c_str());

    std::string outp;
    int code = RunProcess(g_cl, args, cwd, outp);
    log = outp;

    // 恢复环境
    SetEnvironmentVariableW(L"INCLUDE", oldInc);
    SetEnvironmentVariableW(L"LIB", oldLib);
    SetEnvironmentVariableW(L"PATH", oldPath);
    SetEnvironmentVariableW(L"TMP", oldTmp);
    SetEnvironmentVariableW(L"TEMP", oldTemp);

    return code == 0;
}

} // namespace vcb
