// light.cpp
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <limits>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <functional>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <random>
#include <direct.h>
#include <ctime>
#include <algorithm>
#include <queue>
#include <bitset>
#include <codecvt>
#include <locale>
#include <type_traits>
#include <iomanip>
#include <map>
#include <regex>

// ====== 第三步：Windows 扩展库 ======
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <aclapi.h>
#include <sddl.h>
#include <urlmon.h>
#include <wininet.h>
#include <winhttp.h>
#include <filesystem>
#include <vector>

// ====== 第四步：第三方库 ======
#include "../../../zlib-1.3.2/zlib.h"
#include "../../../zlib-1.3.2/contrib/minizip/unzip.h"
#include "../../../zlib-1.3.2/contrib/minizip/ioapi.h"
#include "..\\..\\..\\lzma\\C\\LzmaLib.h"
#include "../../../zlib-1.3.2/zip.h"

// ====== 第五步：您的头文件（最后） ======
//#include "SecurityCore.h"
#include "NetworkInterceptor.h"
#include "NotifyWindow.h"
#include "PluginManager.h"
#include "PluginInterface.h"
#include "../light/Gui/YXGuiPlugin.h"  // 内置 GUI：编译进 light.exe，不再依赖 YXGuiPlugin.dll
#include "PluginTemplateGenerator.h"
#include "LightApi.h"

// ====== GuiHost 已移除 ======
// GUI 模式下插件窗口直接作为主窗口，不再需要 GuiHost 外层窗口
// （GuiHost.h / GuiHost.cpp 可从工程中删除）

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "D:\\zlib-1.3.2\\build\\Release\\z.lib")
#pragma comment(lib, "D:\\zlib-1.3.2\\contrib\\minizip\\minizip.lib")
#pragma comment(lib, "../../../WinDivert-2.2.2-A/x64/WinDivert.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")

#define COLOR_WHITE  FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
#define COLOR_GREEN  FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define COLOR_RED    FOREGROUND_RED | FOREGROUND_INTENSITY
#define COLOR_YELLOW FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define COLOR_CYAN   FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
#define FOREGROUND_CYAN    (FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_MAGENTA (FOREGROUND_RED | FOREGROUND_BLUE)
#define FOREGROUND_YELLOW  (FOREGROUND_RED | FOREGROUND_GREEN)
#define FOREGROUND_WHITE   (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

using namespace std;
namespace fs = std::filesystem;

HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

atomic<bool> spinnerRunning(false);
thread spinnerThread;
const string PASSWORD = "sr5r4g464dry8k78665sglnjtr4m5z578tk78q";
const string PIPE_KEY = "LightPipeKey2024!@#";
const string SERVICE_NAME = "LightService";
const string SERVICE_DISPLAY = "Light Service";
const string XZYX_INTERNAL_KEY = "LightXZYX2026Secure!@#$%^&*64seressg64regt64ndzwq4tmo5p";
string cX;
string cd;

// ==================== 颜色全局变量 ====================
int L_default = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
int L_prompt = FOREGROUND_CYAN | FOREGROUND_INTENSITY;
int L_path = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
int L_bracket = FOREGROUND_YELLOW | FOREGROUND_INTENSITY;
int L_success = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
int L_error = FOREGROUND_RED | FOREGROUND_INTENSITY;
int L_warning = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
int L_0 = 0;
int L_1 = FOREGROUND_RED | FOREGROUND_INTENSITY;
int L_2 = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
int L_3 = FOREGROUND_YELLOW | FOREGROUND_INTENSITY;
int L_4 = FOREGROUND_BLUE | FOREGROUND_INTENSITY;
int L_5 = FOREGROUND_MAGENTA | FOREGROUND_INTENSITY;
int L_6 = FOREGROUND_CYAN | FOREGROUND_INTENSITY;
int L_7 = FOREGROUND_WHITE | FOREGROUND_INTENSITY;
int L_8 = FOREGROUND_RED;
int L_9 = FOREGROUND_GREEN;
int L_A = FOREGROUND_YELLOW;
int L_B = FOREGROUND_BLUE;
int L_C = FOREGROUND_MAGENTA;
int L_D = FOREGROUND_CYAN;
int L_E = FOREGROUND_WHITE;
int L_F = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;

// ==================== GUI 颜色映射 ====================
// 记录当前线程最近一次 SetConsoleTextAttribute 设置的前景色，
// 供 GuiStreamBuf 在转发文本到 GUI 时注入 ANSI 颜色标记，实现控制台彩色输出映射。
static thread_local int g_consoleFg =
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

// 把控制台 FOREGROUND_* 位组合转换为 ANSI SGR 前景色码（30-37 / 90-97）。
static inline int ConsoleAttrToAnsi(int attr) {
    int a = 0;
    if (attr & FOREGROUND_RED)   a |= 1;   // ANSI 位0 = 红
    if (attr & FOREGROUND_GREEN) a |= 2;   // ANSI 位1 = 绿
    if (attr & FOREGROUND_BLUE)  a |= 4;   // ANSI 位2 = 蓝
    return (attr & FOREGROUND_INTENSITY) ? (90 + a) : (30 + a);
}

// 拦截颜色设置：既设置真实控制台颜色，又记录当前前景色供 GUI 映射。
// 注意：此宏定义在本文件所有 SetConsoleTextAttribute 调用点之前。
#define SetConsoleTextAttribute(hCon, attr) \
    do { \
        ::SetConsoleTextAttribute((hCon), (WORD)(attr)); \
        g_consoleFg = (int)(attr); \
    } while (0)

// ==================== 全局类指针 ====================
class SecurityCore;
SecurityCore* g_SecurityCore = nullptr;
//NetworkInterceptor* g_NetworkInterceptor = nullptr;
//NotifyWindow* g_NotifyWindow = nullptr;
PluginManager* g_PluginManager = nullptr;

// ==================== 新增：GUI 全局状态 ====================
// 用于在主循环里判断当前是否运行在 GUI 模式
static bool g_guiMode = false;
static HWND g_guiMainWnd = nullptr;   // GUI 模式下插件主窗口句柄（替代原 GuiHost 外层窗口）
// GUI 模式下，命令处理完成后需要通知主循环继续
// 这里用一个全局的"待处理命令"队列，让 GUI 线程投递命令，主循环消费
static std::mutex g_guiCmdMutex;
static std::condition_variable g_guiCmdCV;
static std::queue<std::string> g_guiCmdQueue;
static std::atomic<bool> g_guiRunning{ false };

// GUI 模式下的退出标志：命令处理返回 false 时置为 true
static std::atomic<bool> g_guiShouldExit{ false };

IGuiProvider* g_guiProvider = nullptr;

// ==================== 函数声明 ====================
void HandleUnzipCommand(const string& command);
string ReadRegistryFile(const string& filePath);
string FindValueInRegistry(const string& data, const vector<string>& path, const string& valueName, int valueIndex = 0);
string XorEncryptDecrypt(const string& data, const string& key);
string GetCurrentProgramPath();
string GetProgramDir();
string GetParentDir(const string& dir);
string ResolvePath(const string& path);
string GetCacheXzyxPath();
string GetFileNameFromPath(const string& path);
string GetParentDirPath(const string& path);
string GetDownloadFolderPath();
string GetCurrentTimeString();
string GetCurrentDateString();
string UrlEncode(const string& str);
string GetSecurityNeValue();
bool FileExists(const string& filePath);
bool IsRunningAsAdmin();
void ElevateCurrentProgram();
void CheckAndInstallService();
bool StartLightService();
void StopLightService();
bool SendToService(const string& command, string& response);
bool LaunchProgram(const string& exePath, bool waitForCompletion);
bool LockFolderWithTrustedInstaller(const string& folderPath, bool silent = false);
bool Lock_ProtectFolderRecursively(const string& _wjj, bool silent = false);
bool Lock_RestoreFolderRecursively(const string& _wjj);
bool UnlockFolderForAdmin(const string& folderPath, bool silent = false);
bool RestoreFolderPermission(const string& folderPath);
bool EnableSecurityPrivileges();
bool EnablePrivilege(const string& privilegeName);
bool UpdateSecurityNeValue();
bool UpdateSecurityNeValue_it();
bool CheckRemoteFileExists(const string& url_bin);
bool peizhi();
bool uninstall();
void DeleteFilesInDirectory(const string& dirPath);
void LoadColorConfig();
void help();
void about();
void printWithTilde(const string& text);
void PrintPrompt(const string& cd);
void StartSpinner();
void StopSpinner();
void SpinnerAnimation();
vector<string> ParseCommandLine(const string& input);
bool IsValidFileName(const string& fileName);
bool CompressFolderToXZYXSecure(const string& inputPath, const string& outputPath, bool includeFolder = true);
bool ExtractXZYXFileSecure(const string& filePath, const string& outputDir);
bool ListXZYXContent(const string& filePath);
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType);
bool DownloadFileWithProgress(const string& url, const string& savePath);
string HttpGet(const string& url);
string SearchDownloadLink(const string& query);
string GenerateRandomString(int length);
string GenerateCVID();
string GetCurrentTimeString();
void SetColor(int color);
// ==================== 辅助函数声明 ====================
void HandleCdCommand(const string& command, string& cd);
void HandleBlockCommand(const string& command);
void HandleUnblockCommand(const string& command);
void HandleShowBlocksCommand();
void HandleClearBlocksCommand();
void HandleWhitelistCommand(const string& command);
void HandleUnwhitelistCommand(const string& command);
void HandleCELCommand();
void HandleUninstallCommand(const string& folderPath);
void HandleFPNPCTCommand();
void HandleAboutCommand();
void HandleFillCommand(const string& cd, const string& registryPath);
void HandleDumpCommand(const string& cd, const string& registryPath);
void HandleLockCommand(const string& command);
void HandleUnlockCommand(const string& command);
void HandleRegistryCommand();
void HandleBinaryHexEditorCommand();
void HandleRestoreFolderPermissionCommand(const string& folderPath);
void HandleCompressCommand(const string& command);
void HandleExtractCommand(const string& command);
void HandleListCommand(const string& command);
void HandleDownloadCommand(const string& command, SecurityCore* securityCore);
void HandleReloadPluginsCommand(const string& pluginsDir);
void HandleReloadPluginCommand(const string& command);
void HandleLoadPluginCommand(const string& command);
void HandleUnloadPluginCommand(const string& command);
void HandlePluginsCommand();
void HandleCreatePluginCommand(const string& command);
void HandleGuiProviderCommand(const string& command);
bool ExecutePluginCommand(const string& command);

static std::string TrimCopy(const std::string& value);
static std::string GetGuiProviderPreference();
static bool SetGuiProviderPreference(const std::string& value);

// ==================== 基础函数实现 ====================

// 使用 minizip 解压 ZIP 文件
bool ExtractZipWithMinizip(const std::string& zipPath, const std::string& destDir) {
    unzFile zipFile = unzOpen(zipPath.c_str());
    if (!zipFile) {
        std::cerr << "无法打开 ZIP 文件: " << zipPath << std::endl;
        return false;
    }

    // 创建目标目录
    CreateDirectoryA(destDir.c_str(), NULL);

    // 获取 ZIP 文件信息
    unz_global_info globalInfo;
    if (unzGetGlobalInfo(zipFile, &globalInfo) != UNZ_OK) {
        std::cerr << "获取 ZIP 信息失败" << std::endl;
        unzClose(zipFile);
        return false;
    }

    // 遍历所有文件
    for (uLong i = 0; i < globalInfo.number_entry; i++) {
        unz_file_info fileInfo;
        char filename[512] = { 0 };

        if (unzGetCurrentFileInfo(zipFile, &fileInfo, filename, sizeof(filename), NULL, 0, NULL, 0) != UNZ_OK) {
            std::cerr << "获取文件信息失败" << std::endl;
            break;
        }

        std::string entryName = filename;
        std::string outputPath = destDir + "\\" + entryName;

        // 检查是否是目录
        if (entryName.back() == '/' || entryName.back() == '\\') {
            CreateDirectoryA(outputPath.c_str(), NULL);
        }
        else {
            // 递归创建父目录。扩展包通常包含多级目录，CreateDirectoryA 只能创建单级目录。
            std::string parentDir = outputPath;
            size_t lastSlash = parentDir.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                parentDir = parentDir.substr(0, lastSlash);
                if (!parentDir.empty()) {
                    std::error_code ec;
                    fs::create_directories(fs::path(parentDir), ec);
                    if (ec) {
                        std::cerr << "创建解压目录失败: " << parentDir
                            << ", error=" << ec.message() << std::endl;
                        unzCloseCurrentFile(zipFile);
                        if (i < globalInfo.number_entry - 1) unzGoToNextFile(zipFile);
                        continue;
                    }
                }
            }

            // 打开当前文件
            if (unzOpenCurrentFile(zipFile) == UNZ_OK) {
                // 创建输出文件
                FILE* outputFile = fopen(outputPath.c_str(), "wb");
                if (outputFile) {
                    char buffer[8192];
                    int bytesRead;

                    // 读取并写入数据
                    bool writeOk = true;
                    while ((bytesRead = unzReadCurrentFile(zipFile, buffer, sizeof(buffer))) > 0) {
                        if (fwrite(buffer, 1, bytesRead, outputFile) != (size_t)bytesRead) {
                            writeOk = false;
                            break;
                        }
                    }

                    if (bytesRead < 0) writeOk = false;
                    fclose(outputFile);
                    if (!writeOk) {
                        std::cerr << "写入解压文件失败: " << outputPath << std::endl;
                        unzCloseCurrentFile(zipFile);
                        if (i < globalInfo.number_entry - 1) unzGoToNextFile(zipFile);
                        continue;
                    }
                }

                unzCloseCurrentFile(zipFile);
            }
        }

        // 移动到下一个文件
        if (i < globalInfo.number_entry - 1) {
            if (unzGoToNextFile(zipFile) != UNZ_OK) {
                break;
            }
        }
    }

    unzClose(zipFile);
    return true;
}

// 删除文件
bool DeleteFileSecure(const std::string& filePath) {
    // 移除只读属性
    DWORD attributes = GetFileAttributesA(filePath.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesA(filePath.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    }

    // 删除文件
    if (DeleteFileA(filePath.c_str())) {
        return true;
    }

    // 重试
    for (int i = 0; i < 5; i++) {
        Sleep(100);
        if (DeleteFileA(filePath.c_str())) {
            return true;
        }
    }

    return false;
}

// 解压并删除 ZIP 文件
bool ExtractAndDeleteZip(const std::string& zipPath, const std::string& destDir) {
    std::cout << "正在解压: " << zipPath << " 到 " << destDir << std::endl;

    if (!ExtractZipWithMinizip(zipPath, destDir)) {
        std::cerr << "解压失败" << std::endl;
        return false;
    }

    std::cout << "解压完成，正在删除原文件..." << std::endl;

    if (!DeleteFileSecure(zipPath)) {
        std::cerr << "删除文件失败" << std::endl;
        return false;
    }

    std::cout << "原文件已删除" << std::endl;
    return true;
}

string GetCurrentProgramPath() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    string fullPath(buffer);
    size_t lastSlash = fullPath.find_last_of("\\/");
    return (lastSlash != string::npos) ? fullPath.substr(0, lastSlash) : fullPath;
}

string GetProgramDir() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    string fullPath(buffer);
    size_t lastSlash = fullPath.find_last_of("\\/");
    return (lastSlash != string::npos) ? fullPath.substr(0, lastSlash) : fullPath;
}

string GetParentDir(const string& dir) {
    size_t lastSlash = dir.find_last_of("\\/");
    return (lastSlash != string::npos) ? dir.substr(0, lastSlash) : dir;
}

string ResolvePath(const string& path) {
    if (path.empty()) return path;
    if (path.length() >= 2 && path[1] == ':') return path;
    if (path.length() >= 2 && path[0] == '\\' && path[1] == '\\') return path;

    string resolved = cd;
    if (!resolved.empty() && resolved.back() != '\\' && resolved.back() != '/') {
        resolved += '\\';
    }
    string cleanPath = path;
    if (cleanPath.length() >= 2 && cleanPath[0] == '.' && (cleanPath[1] == '\\' || cleanPath[1] == '/')) {
        cleanPath = cleanPath.substr(2);
    }
    resolved += cleanPath;
    return resolved;
}

string GetCacheXzyxPath() {
    string cacheDir = cd + "\\Cache\\ysXzyxs";
    DWORD dwAttrib = GetFileAttributesA(cacheDir.c_str());
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        CreateDirectoryA(cacheDir.c_str(), NULL);
    }
    return cacheDir;
}

string GetFileNameFromPath(const string& path) {
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != string::npos) {
        return path.substr(lastSlash + 1);
    }
    return path;
}

string GetParentDirPath(const string& path) {
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != string::npos) {
        return path.substr(0, lastSlash);
    }
    return "";
}

string GetDownloadFolderPath() {
    string exePath = GetCurrentProgramPath();
    string downloadFolder = exePath + "\\download";
    DWORD dwAttrib = GetFileAttributesA(downloadFolder.c_str());
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) CreateDirectoryA(downloadFolder.c_str(), NULL);
    return downloadFolder;
}

string GetCurrentTimeString() {
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return string(buffer);
}

string GetCurrentDateString() {
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y%m%d", &timeinfo);
    return string(buffer);
}

string UrlEncode(const string& str) {
    ostringstream escaped;
    escaped.fill('0');
    escaped << hex;
    for (char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') { escaped << c; }
        else { escaped << '%' << setw(2) << int((unsigned char)c); }
    }
    return escaped.str();
}

bool FileExists(const string& filePath) {
    DWORD dwAttrib = GetFileAttributesA(filePath.c_str());
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroupSid = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroupSid)) {
        CheckTokenMembership(NULL, adminGroupSid, &isAdmin);
        FreeSid(adminGroupSid);
    }
    return isAdmin == TRUE;
}

void RunWithUACAndExit(const char* exePath) {
    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.lpVerb = "runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    if (ShellExecuteExA(&sei) && sei.hProcess) {
        WaitForInputIdle(sei.hProcess, 5000);
        CloseHandle(sei.hProcess);
    }
    ExitProcess(0);
}

void ElevateCurrentProgram() {
    if (IsRunningAsAdmin()) {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "当前程序已经是管理员权限。" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0) {
        ShellExecuteA(NULL, "runas", NULL, NULL, NULL, SW_SHOWNORMAL);
        ExitProcess(0);
    }
    CheckAndInstallService();
    if (StartLightService()) {
        string response;
        string command = "run:" + string(exePath);
        if (SendToService(command, response) && response == "OK") {
            Sleep(500);
            ExitProcess(0);
        }
    }
    RunWithUACAndExit(exePath);
}

void StartSpinner() {
    if (!spinnerRunning) {
        spinnerRunning = true;
        spinnerThread = thread(SpinnerAnimation);
    }
}

void StopSpinner() {
    spinnerRunning = false;
    if (spinnerThread.joinable()) spinnerThread.join();
}

void SpinnerAnimation() {
    const char chars[] = { '/', '|', '\\', '-' };
    int i = 0;
    while (spinnerRunning) {
        cout << "\r" << chars[i % 4] << " ..." << flush;
        i++;
        Sleep(100);
    }
    cout << "\r                \r" << flush;
}

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
    if (fdwCtrlType == CTRL_C_EVENT) return TRUE;
    return FALSE;
}

void printWithTilde(const string& text) {
    int charCount = 0;
    for (size_t i = 0; i < text.length(); ) {
        unsigned char c = text[i];
        if (c < 0x80) { i += 1; }
        else if (c < 0xE0) { i += 2; }
        else if (c < 0xF0) { i += 3; }
        else { i += 4; }
        charCount++;
    }
    cX.clear();
    for (int i = 0; i < charCount; i++) {
        cX += "~";
    }
}

void PrintPrompt(const string& cd) {
    SetConsoleTextAttribute(hConsole, L_bracket);
    cout << "<";
    SetConsoleTextAttribute(hConsole, L_path);
    cout << cd;
    SetConsoleTextAttribute(hConsole, L_bracket);
    cout << ":> ";
    SetConsoleTextAttribute(hConsole, L_default);
}

vector<string> ParseCommandLine(const string& input) {
    vector<string> result;
    string current;
    bool inQuotes = false;
    for (char c : input) {
        if (c == '"') { inQuotes = !inQuotes; }
        else if (c == ' ' && !inQuotes) {
            if (!current.empty()) { result.push_back(current); current.clear(); }
        }
        else { current += c; }
    }
    if (!current.empty()) result.push_back(current);
    return result;
}

bool IsValidFileName(const string& fileName) {
    if (fileName.empty()) return false;
    const char invalidChars[] = { '\\', '/', ':', '*', '?', '"', '<', '>', '|', '\0' };
    for (char c : fileName) {
        for (int i = 0; invalidChars[i] != '\0'; i++) {
            if (c == invalidChars[i]) return false;
        }
    }
    return true;
}

void SetColor(int color) {
    SetConsoleTextAttribute(hConsole, color);
}

// ==================== 权限管理 ====================

bool EnablePrivilege(const string& privilegeName) {
    HANDLE hToken = NULL;
    TOKEN_PRIVILEGES tp;
    LUID luid;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
    if (!LookupPrivilegeValueA(NULL, privilegeName.c_str(), &luid)) { CloseHandle(hToken); return false; }
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL)) { CloseHandle(hToken); return false; }
    bool success = (GetLastError() != ERROR_NOT_ALL_ASSIGNED);
    CloseHandle(hToken);
    return success;
}

bool EnableSecurityPrivileges() {
    bool allSuccess = true;
    const char* privileges[] = { "SeTakeOwnershipPrivilege", "SeSecurityPrivilege", "SeBackupPrivilege", "SeRestorePrivilege" };
    for (int i = 0; i < 4; i++) { if (!EnablePrivilege(privileges[i])) allSuccess = false; }
    return allSuccess;
}

// ==================== 服务管理 ====================

bool IsServiceInstalled() {
    SC_HANDLE hSCManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCManager) return false;
    SC_HANDLE hService = OpenServiceA(hSCManager, SERVICE_NAME.c_str(), SERVICE_QUERY_STATUS);
    if (hService) { CloseServiceHandle(hService); CloseServiceHandle(hSCManager); return true; }
    CloseServiceHandle(hSCManager);
    return false;
}

bool InstallService(const string& servicePath) {
    if (!IsRunningAsAdmin()) return false;
    SC_HANDLE hSCManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager) return false;
    SC_HANDLE hService = CreateServiceA(hSCManager, SERVICE_NAME.c_str(), SERVICE_DISPLAY.c_str(), SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, servicePath.c_str(),
        NULL, NULL, NULL, NULL, NULL);
    bool success = (hService != NULL);
    if (success) { StartServiceA(hService, 0, NULL); CloseServiceHandle(hService); }
    CloseServiceHandle(hSCManager);
    return success;
}

void CheckAndInstallService() {
    if (IsServiceInstalled()) return;
    if (!IsRunningAsAdmin()) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        RunWithUACAndExit(exePath);
    }
    string dir = GetCurrentProgramPath();
    string servicePath = dir + "\\rppcst\\yuxuangzs-Service-light.exe";
    if (!FileExists(servicePath)) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "服务程序不存在: " << servicePath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    InstallService(servicePath);
}

bool StartLightService() {
    SC_HANDLE hSCManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCManager) return false;
    SC_HANDLE hService = OpenServiceA(hSCManager, SERVICE_NAME.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
    if (!hService) { CloseServiceHandle(hSCManager); return false; }
    SERVICE_STATUS status;
    BOOL result = StartServiceA(hService, 0, NULL);
    DWORD err = GetLastError();
    if (!result && err != ERROR_SERVICE_ALREADY_RUNNING) { CloseServiceHandle(hService); CloseServiceHandle(hSCManager); return false; }
    for (int i = 0; i < 10; i++) { Sleep(300); if (QueryServiceStatus(hService, &status) && status.dwCurrentState == SERVICE_RUNNING) break; }
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    return true;
}

void StopLightService() {
    SC_HANDLE hSCManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager) return;
    SC_HANDLE hService = OpenServiceA(hSCManager, SERVICE_NAME.c_str(), SERVICE_ALL_ACCESS);
    if (hService) { SERVICE_STATUS status; ControlService(hService, SERVICE_CONTROL_STOP, &status); CloseServiceHandle(hService); }
    CloseServiceHandle(hSCManager);
}

string XorPipe(const string& data) {
    string result = data;
    for (size_t i = 0; i < result.size(); ++i) result[i] ^= PIPE_KEY[i % PIPE_KEY.size()];
    return result;
}

bool SendToService(const string& command, string& response) {
    if (!StartLightService()) return false;
    string encrypted = XorPipe(command);
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 20; i++) {
        hPipe = CreateFileA("\\\\.\\pipe\\LightPipe", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPipe != INVALID_HANDLE_VALUE) break;
        Sleep(500);
    }
    if (hPipe == INVALID_HANDLE_VALUE) { StopLightService(); return false; }
    DWORD bytesWritten;
    WriteFile(hPipe, encrypted.c_str(), encrypted.size(), &bytesWritten, NULL);
    char buffer[4096] = { 0 };
    DWORD bytesRead;
    ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL);
    response = XorPipe(string(buffer, bytesRead));
    CloseHandle(hPipe);
    StopLightService();
    return true;
}

bool LaunchProgram(const string& exePath, bool waitForCompletion) {
    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.lpVerb = "open";
    sei.lpFile = exePath.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    if (ShellExecuteExA(&sei)) {
        if (waitForCompletion && sei.hProcess) { WaitForSingleObject(sei.hProcess, INFINITE); CloseHandle(sei.hProcess); }
        return true;
    }
    return false;
}

// ==================== 加密/注册表 ====================

string XorEncryptDecrypt(const string& data, const string& key) {
    if (data.empty() || key.empty()) return data;
    string result = data;
    size_t keyLen = key.size();
    for (size_t i = 0; i < result.size(); ++i) result[i] = result[i] ^ key[i % keyLen];
    return result;
}

string ReadRegistryFile(const string& filePath) {
    ifstream file(filePath, ios::binary);
    if (!file.is_open()) return "";
    file.seekg(0, ios::end);
    size_t size = file.tellg();
    file.seekg(0, ios::beg);
    if (size == 0) { file.close(); return ""; }
    string encrypted(size, '\0');
    file.read(&encrypted[0], size);
    file.close();
    return XorEncryptDecrypt(encrypted, PASSWORD);
}

string FindValueInRegistry(const string& data, const vector<string>& path, const string& valueName, int valueIndex) {
    if (path.empty() || data.empty()) return "";
    size_t currentPos = 0;
    for (const auto& node : path) {
        currentPos = data.find("NODE:" + node, currentPos);
        if (currentPos == string::npos) return "";
    }
    int foundCount = 0;
    size_t valueNodePos = data.find("NODE:" + valueName, currentPos);
    while (valueNodePos != string::npos) {
        size_t iskeyPos = data.find("ISKEY:", valueNodePos);
        if (iskeyPos != string::npos && data.substr(iskeyPos, 7) == "ISKEY:0") {
            if (foundCount == valueIndex) {
                size_t valuePos = data.find("VALUE:", iskeyPos);
                if (valuePos != string::npos) {
                    valuePos += 6;
                    size_t endPos = data.find("\n", valuePos);
                    return (endPos == string::npos) ? data.substr(valuePos) : data.substr(valuePos, endPos - valuePos);
                }
            }
            foundCount++;
        }
        valueNodePos = data.find("NODE:" + valueName, valueNodePos + 5);
    }
    return "";
}

string GetSecurityNeValue() {
    string cd = GetCurrentProgramPath();
    string registryPath = cd + "\\rppcst\\sys.xayx";
    string data = ReadRegistryFile(registryPath);
    if (data.empty()) return "";
    vector<string> path = { "HKEY_LOCAL_MACHINE", "xayx" };
    string securityNe = FindValueInRegistry(data, path, "security_Ne");
    if (securityNe.empty()) return "";
    return securityNe;
}

bool UpdateSecurityNeValue() {
    string cd = GetCurrentProgramPath();
    string registryPath = cd + "\\rppcst\\sys.xayx";
    string data = ReadRegistryFile(registryPath);
    if (data.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[安全配置] 无法读取注册表文件: " << registryPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    vector<string> path = { "HKEY_LOCAL_MACHINE", "xayx" };
    size_t currentPos = 0;
    for (const auto& node : path) {
        currentPos = data.find("NODE:" + node, currentPos);
        if (currentPos == string::npos) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[安全配置] 未找到路径节点: " << node << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
    }
    size_t securityNodePos = data.find("NODE:security_Ne", currentPos);
    if (securityNodePos == string::npos) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[安全配置] 未找到 security_Ne 节点" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    size_t valuePos = data.find("VALUE:", securityNodePos);
    if (valuePos == string::npos) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[安全配置] 未找到 VALUE 字段" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    valuePos += 6;
    size_t endPos = data.find("\n", valuePos);
    if (endPos == string::npos) endPos = data.length();
    string oldValue = data.substr(valuePos, endPos - valuePos);
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "[安全配置] : " << oldValue << endl;
    string security_Ne = GetSecurityNeValue();
    int v1 = 0, v2 = 0, v3 = 0;
    string numbers;
    for (char c : security_Ne) { if (isdigit(c)) numbers += c; }
    if (numbers.length() >= 3) {
        v1 = atoi(numbers.substr(0, 1).c_str());
        v2 = atoi(numbers.substr(1, 1).c_str());
        v3 = atoi(numbers.substr(2).c_str());
    }
    v3 = v3 + 1;
    string newValue = to_string(v1) + "." + to_string(v2) + "." + to_string(v3);
    data.replace(valuePos, endPos - valuePos, newValue);
    string encrypted = XorEncryptDecrypt(data, PASSWORD);
    ofstream outFile(registryPath, ios::binary);
    if (!outFile.is_open()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[安全配置] 无法写入注册表文件: " << registryPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    outFile.write(encrypted.c_str(), encrypted.size());
    outFile.close();
    SetConsoleTextAttribute(hConsole, L_success);
    cout << "[安全配置] security_Ne 已更新" << endl;
    SetConsoleTextAttribute(hConsole, L_default);
    return true;
}

bool UpdateSecurityNeValue_it() {
    string cd = GetCurrentProgramPath();
    string registryPath = cd + "\\rppcst\\sys.xayx";
    string data = ReadRegistryFile(registryPath);
    if (data.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[安全配置] 无法读取注册表文件: " << registryPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    vector<string> path = { "HKEY_LOCAL_MACHINE", "xayx" };
    size_t currentPos = 0;
    for (const auto& node : path) {
        currentPos = data.find("NODE:" + node, currentPos);
        if (currentPos == string::npos) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[安全配置] 未找到路径节点: " << node << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
    }
    size_t securityNodePos = data.find("NODE:security_Ne", currentPos);
    if (securityNodePos == string::npos) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[安全配置] 未找到 security_Ne 节点" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    size_t valuePos = data.find("VALUE:", securityNodePos);
    if (valuePos == string::npos) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[安全配置] 未找到 VALUE 字段" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    valuePos += 6;
    size_t endPos = data.find("\n", valuePos);
    if (endPos == string::npos) endPos = data.length();
    string oldValue = data.substr(valuePos, endPos - valuePos);
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "[安全配置] : " << oldValue << endl;
    string newValue = "1.1.2";
    data.replace(valuePos, endPos - valuePos, newValue);
    string encrypted = XorEncryptDecrypt(data, PASSWORD);
    ofstream outFile(registryPath, ios::binary);
    if (!outFile.is_open()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[安全配置] 无法写入注册表文件: " << registryPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    outFile.write(encrypted.c_str(), encrypted.size());
    outFile.close();
    SetConsoleTextAttribute(hConsole, L_success);
    cout << "[安全配置] security_Ne 已更新" << endl;
    SetConsoleTextAttribute(hConsole, L_default);
    return true;
}

bool CheckRemoteFileExists(const string& url_bin) {
    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return false;
    string host, path;
    if (url_bin.find("https://") == 0) host = url_bin.substr(8);
    else if (url_bin.find("http://") == 0) host = url_bin.substr(7);
    else { WinHttpCloseHandle(hSession); return false; }
    size_t slashPos = host.find('/');
    if (slashPos != string::npos) { path = host.substr(slashPos); host = host.substr(0, slashPos); }
    else path = "/";
    HINTERNET hConnect = WinHttpConnect(hSession, wstring(host.begin(), host.end()).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"HEAD", wstring(path.begin(), path.end()).c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
    if (!WinHttpSendRequest(hRequest, NULL, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
    if (!WinHttpReceiveResponse(hRequest, NULL)) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    bool success = WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &statusCode, &statusCodeSize, NULL);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return success && (statusCode == 200);
}

// ==================== 颜色配置加载 ====================

void LoadColorConfig() {
    string registryPath = cd + "\\rppcst\\sys.xayx";
    string data = ReadRegistryFile(registryPath);
    if (data.empty()) return;

    size_t pos = data.find("NODE:HKEY_LOCAL_MACHINE");
    if (pos == string::npos) return;
    pos = data.find("NODE:xbyx", pos);
    if (pos == string::npos) return;
    pos = data.find("NODE:color", pos);
    if (pos == string::npos) return;

    map<string, int*> colorVarMap = {
        {"default", &L_default}, {"prompt", &L_prompt}, {"path", &L_path},
        {"bracket", &L_bracket}, {"success", &L_success}, {"error", &L_error},
        {"warning", &L_warning}, {"0", &L_0}, {"1", &L_1}, {"2", &L_2},
        {"3", &L_3}, {"4", &L_4}, {"5", &L_5}, {"6", &L_6}, {"7", &L_7},
        {"8", &L_8}, {"9", &L_9}, {"A", &L_A}, {"B", &L_B}, {"C", &L_C},
        {"D", &L_D}, {"E", &L_E}, {"F", &L_F}
    };

    for (const auto& entry : colorVarMap) {
        const string& colorName = entry.first;
        int* varPtr = entry.second;
        size_t nodePos = data.find("NODE:" + colorName, pos);
        if (nodePos == string::npos) continue;
        size_t valuePos = data.find("VALUE:", nodePos);
        if (valuePos != string::npos) {
            valuePos += 6;
            size_t valueEnd = data.find("\n", valuePos);
            if (valueEnd == string::npos) valueEnd = data.length();
            string colorValue = data.substr(valuePos, valueEnd - valuePos);
            colorValue.erase(0, colorValue.find_first_not_of(" \t\r\n"));
            colorValue.erase(colorValue.find_last_not_of(" \t\r\n") + 1);

            int parsedValue = 0;
            string str = colorValue;
            str.erase(remove_if(str.begin(), str.end(), ::isspace), str.end());
            vector<string> parts;
            stringstream ss(str);
            string part;
            while (getline(ss, part, '|')) {
                if (!part.empty()) parts.push_back(part);
            }
            for (const auto& p : parts) {
                string upper = p;
                transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                if (upper == "FOREGROUND_RED") parsedValue |= FOREGROUND_RED;
                else if (upper == "FOREGROUND_GREEN") parsedValue |= FOREGROUND_GREEN;
                else if (upper == "FOREGROUND_BLUE") parsedValue |= FOREGROUND_BLUE;
                else if (upper == "FOREGROUND_INTENSITY") parsedValue |= FOREGROUND_INTENSITY;
                else if (upper == "BACKGROUND_RED") parsedValue |= BACKGROUND_RED;
                else if (upper == "BACKGROUND_GREEN") parsedValue |= BACKGROUND_GREEN;
                else if (upper == "BACKGROUND_BLUE") parsedValue |= BACKGROUND_BLUE;
                else if (upper == "BACKGROUND_INTENSITY") parsedValue |= BACKGROUND_INTENSITY;
            }
            *varPtr = parsedValue;
        }
    }
}

// ==================== 帮助/关于 ====================

static std::string WideToConsole(const std::wstring& w) {
    if (w.empty()) return "";
    UINT cp = GetConsoleOutputCP();
    if (cp == 0) cp = CP_ACP;
    int len = WideCharToMultiByte(cp, 0, w.c_str(), (int)w.size(),
        nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(cp, 0, w.c_str(), (int)w.size(),
        &out[0], len, nullptr, nullptr);
    return out;
}

// 打印所有已加载插件注册的命令（选项 C：help 汇总插件命令）
static void PrintPluginCommandsInHelp() {
    if (!g_PluginManager) return;

    auto allCmds = g_PluginManager->GetAllPluginCommands();
    if (allCmds.empty()) return;

    SetConsoleTextAttribute(hConsole, L_default);
    cout << endl;
    SetConsoleTextAttribute(hConsole, L_6);
    cout << "==================== 插件命令 ====================" << endl;
    SetConsoleTextAttribute(hConsole, L_default);

    // pluginName / commandName / usage / description 全部是 std::string
    std::map<std::string, std::vector<PluginCommandInfo>> grouped;
    for (const auto& cmd : allCmds) {
        std::string key = cmd.pluginName;
        if (key.empty()) key = "(未命名插件)";
        grouped[key].push_back(cmd);
    }

    for (const auto& kv : grouped) {
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[" << kv.first << "]" << endl;
        SetConsoleTextAttribute(hConsole, L_default);

        for (const auto& cmd : kv.second) {
            SetConsoleTextAttribute(hConsole, L_prompt);
            cout << "  " << cmd.commandName;
            SetConsoleTextAttribute(hConsole, L_default);
            if (!cmd.usage.empty()) cout << "  " << cmd.usage;
            cout << endl;
            if (!cmd.description.empty())
                cout << "      " << cmd.description << endl;
        }
        cout << endl;
    }

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "提示：插件命令与内置命令同名时，内置命令优先。" << endl;
    SetConsoleTextAttribute(hConsole, L_default);
}

static bool GetHelpTextFromRegistry(std::string& out) {
    string cd = GetCurrentProgramPath();
    string registryPath = cd + "\\rppcst\\sys.xayx";
    string data = ReadRegistryFile(registryPath);
    if (data.empty()) return false;

    size_t pos = 0;
    vector<string> path = { "HKEY_LOCAL_MACHINE", "xbyx", "xcyx" };
    for (const auto& node : path) {
        pos = data.find("NODE:" + node, pos);
        if (pos == string::npos) return false;
    }

    size_t helpDirPos = data.find("NODE:help\nISKEY:1", pos);
    if (helpDirPos == string::npos)
        helpDirPos = data.find("NODE:help\r\nISKEY:1", pos);
    if (helpDirPos == string::npos) {
        size_t childrenPos = data.find("CHILDREN:1", pos);
        while (childrenPos != string::npos) {
            size_t searchStart = (pos > childrenPos - 50) ? pos : childrenPos - 50;
            size_t helpPos = data.rfind("NODE:help", childrenPos);
            if (helpPos != string::npos && helpPos >= searchStart) {
                helpDirPos = helpPos;
                break;
            }
            childrenPos = data.find("CHILDREN:1", childrenPos + 10);
        }
    }
    if (helpDirPos == string::npos) return false;

    size_t childHelpPos = data.find("NODE:help", helpDirPos + 1);
    if (childHelpPos == string::npos) return false;

    size_t valuePos = data.find("VALUE:", childHelpPos);
    if (valuePos == string::npos) return false;
    valuePos += 6;
    size_t endPos = data.find("\n", valuePos);
    string helpValue = (endPos == string::npos)
        ? data.substr(valuePos)
        : data.substr(valuePos, endPos - valuePos);

    size_t p = 0;
    while ((p = helpValue.find("\\n", p)) != string::npos) {
        helpValue.replace(p, 2, "\n");
        p += 1;
    }
    out = helpValue;
    return true;
}

void help() {
    SetConsoleTextAttribute(hConsole, L_default);

    std::string helpValue;
    if (GetHelpTextFromRegistry(helpValue)) {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << helpValue << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "注册表异常 - 无法读取帮助信息（路径: "
            << GetCurrentProgramPath() << "\\rppcst\\sys.xayx）" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }

    // 选项 C：无论注册表是否正常，都汇总插件命令
    PrintPluginCommandsInHelp();
}

void about() {
    string cd = GetCurrentProgramPath();
    string registryPath = cd + "\\rppcst\\sys.xayx";
    string data = ReadRegistryFile(registryPath);
    SetConsoleTextAttribute(hConsole, L_default);
    if (!data.empty()) {
        vector<string> path = { "HKEY_LOCAL_MACHINE", "xbyx", "xcyx", "about" };
        string aboutValue = FindValueInRegistry(data, path, "about");
        if (!aboutValue.empty()) {
            size_t pos = 0;
            while ((pos = aboutValue.find("\\n", pos)) != string::npos) { aboutValue.replace(pos, 2, "\n"); pos += 1; }
            cout << aboutValue << endl;
        }
        else {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "注册表异常 - 未找到 about 信息" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "注册表异常 - 无法读取注册表文件" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
}

// ==================== 文件夹锁定/解锁 ====================

bool LockFolderWithTrustedInstaller(const string& folderPath, bool silent) {
    EnableSecurityPrivileges();
    string cmd;
    string redirect = silent ? " >nul 2>nul" : "";
    cmd = "icacls \"" + folderPath + "\" /setowner \"NT SERVICE\\TrustedInstaller\" /t /c" + redirect;
    system(cmd.c_str());
    cmd = "icacls \"" + folderPath + "\" /inheritance:r /remove:g \"Everyone\" \"Administrators\" \"Users\" /grant \"SYSTEM\":F /t /q" + redirect;
    system(cmd.c_str());
    SetFileAttributesA(folderPath.c_str(), FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY);
    return true;
}

bool Lock_ProtectFolderRecursively(const string& _wjj, bool silent) {
    EnableSecurityPrivileges();
    string searchPath = _wjj + "\\*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        if (!CreateDirectoryA(_wjj.c_str(), NULL)) return false;
        hFind = FindFirstFileA(searchPath.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE) return false;
    }
    string redirect = silent ? " >nul 2>nul" : "";
    if (!LockFolderWithTrustedInstaller(_wjj, silent)) { FindClose(hFind); return false; }
    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;
        string subPath = _wjj + "\\" + findData.cFileName;
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            Lock_ProtectFolderRecursively(subPath, silent);
        }
        else {
            string fileCmd;
            fileCmd = "icacls \"" + subPath + "\" /setowner \"NT SERVICE\\TrustedInstaller\" /c" + redirect;
            system(fileCmd.c_str());
            fileCmd = "icacls \"" + subPath + "\" /inheritance:r /remove:g \"Everyone\" \"Administrators\" \"Users\" /grant \"SYSTEM\":F /q" + redirect;
            system(fileCmd.c_str());
            SetFileAttributesA(subPath.c_str(), FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY);
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    LockFolderWithTrustedInstaller(_wjj, silent);
    return true;
}

bool UnlockFolderForAdmin(const string& folderPath, bool silent) {
    EnableSecurityPrivileges();
    string cmd;
    string redirect = silent ? " >nul 2>nul" : "";
    cmd = "takeown /f \"" + folderPath + "\" /r /d y" + redirect;
    system(cmd.c_str());
    cmd = "icacls \"" + folderPath + "\" /inheritance:e /grant \"Administrators\":F /t /q" + redirect;
    system(cmd.c_str());
    SetFileAttributesA(folderPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    return true;
}

bool Lock_RestoreFolderRecursively(const string& _wjj) {
    EnableSecurityPrivileges();
    string searchPath = _wjj + "\\*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return false;
    if (!UnlockFolderForAdmin(_wjj)) { FindClose(hFind); return false; }
    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;
        string subPath = _wjj + "\\" + findData.cFileName;
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            Lock_RestoreFolderRecursively(subPath);
        }
        else {
            string fileCmd = "takeown /f \"" + subPath + "\" /d y";
            system(fileCmd.c_str());
            fileCmd = "icacls \"" + subPath + "\" /grant \"Administrators\":F /q";
            system(fileCmd.c_str());
            SetFileAttributesA(subPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    UnlockFolderForAdmin(_wjj);
    return true;
}

bool RestoreFolderPermission(const string& folderPath) {
    return UnlockFolderForAdmin(folderPath);
}

// ==================== 删除文件 ====================

void DeleteFilesInDirectory(const string& dirPath) {
    DWORD dwAttrib = GetFileAttributesA(dirPath.c_str());
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "路径不存在: " << dirPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    if (!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        if (remove(dirPath.c_str()) == 0) {
            SetConsoleTextAttribute(hConsole, L_success);
            cout << "删除文件: " << dirPath << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        else {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "删除失败: " << dirPath << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        return;
    }
    string searchPath = dirPath + "\\*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "无法打开目录: " << dirPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    int count = 0;
    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            string filePath = dirPath + "\\" + findData.cFileName;
            if (remove(filePath.c_str()) == 0) {
                SetConsoleTextAttribute(hConsole, L_success);
                cout << "已删除1个文件: " << findData.cFileName << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                count++;
            }
            else {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "删除1个文件失败: " << findData.cFileName << endl;
                SetConsoleTextAttribute(hConsole, L_default);
            }
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "共删除 " << count << " 个文件" << endl;
    SetConsoleTextAttribute(hConsole, L_default);
}

// ==================== 配置/卸载 ====================

string CopyIconToSystem(const string& sourceIcon) {
    string destDir = "C:\\Program Files\\.light";
    string destIcon = destDir + "\\cmd.ico";
    CreateDirectoryA(destDir.c_str(), NULL);
    if (CopyFileA(sourceIcon.c_str(), destIcon.c_str(), FALSE)) {
        SetFileAttributesA(destIcon.c_str(), FILE_ATTRIBUTE_HIDDEN);
        return destIcon;
    }
    return "";
}

bool RegisterFileType(const string& extension, const string& description, const string& iconPath, const string& openExePath) {
    HKEY hKey;
    LONG result;
    string extKey = "." + extension;
    string progID = "light." + extension;
    RegDeleteKeyA(HKEY_CLASSES_ROOT, extKey.c_str());
    SHDeleteKeyA(HKEY_CLASSES_ROOT, progID.c_str());
    result = RegCreateKeyA(HKEY_CLASSES_ROOT, extKey.c_str(), &hKey);
    if (result != ERROR_SUCCESS) return false;
    RegSetValueA(hKey, NULL, REG_SZ, progID.c_str(), (DWORD)progID.size());
    RegCloseKey(hKey);
    result = RegCreateKeyA(HKEY_CLASSES_ROOT, progID.c_str(), &hKey);
    if (result != ERROR_SUCCESS) return false;
    RegSetValueA(hKey, NULL, REG_SZ, description.c_str(), (DWORD)description.size());
    RegCloseKey(hKey);
    if (!iconPath.empty()) {
        string iconKey = progID + "\\DefaultIcon";
        result = RegCreateKeyA(HKEY_CLASSES_ROOT, iconKey.c_str(), &hKey);
        if (result == ERROR_SUCCESS) { RegSetValueA(hKey, NULL, REG_SZ, iconPath.c_str(), (DWORD)iconPath.size()); RegCloseKey(hKey); }
    }
    if (!openExePath.empty()) {
        string openCmd = "\"" + openExePath + "\" \"%1\"";
        string shellKey = progID + "\\shell\\open\\command";
        result = RegCreateKeyA(HKEY_CLASSES_ROOT, shellKey.c_str(), &hKey);
        if (result == ERROR_SUCCESS) { RegSetValueA(hKey, NULL, REG_SZ, openCmd.c_str(), (DWORD)openCmd.size()); RegCloseKey(hKey); }
        string userChoiceKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\." + extension + "\\UserChoice";
        HKEY hUserKey;
        if (RegCreateKeyA(HKEY_CURRENT_USER, userChoiceKey.c_str(), &hUserKey) == ERROR_SUCCESS) {
            RegSetValueExA(hUserKey, "Progid", 0, REG_SZ, (BYTE*)progID.c_str(), (DWORD)progID.size() + 1);
            RegCloseKey(hUserKey);
        }
    }
    return true;
}

bool RegisterApplication(const string& appName, const string& appPath, const string& uninstallPath, const string& iconPath, const string& version) {
    HKEY hKey;
    LONG result;
    string keyPath = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + appName;
    result = RegCreateKeyA(HKEY_LOCAL_MACHINE, keyPath.c_str(), &hKey);
    if (result != ERROR_SUCCESS) return false;
    RegSetValueExA(hKey, "DisplayName", 0, REG_SZ, (BYTE*)appName.c_str(), (DWORD)appName.size() + 1);
    RegSetValueExA(hKey, "DisplayVersion", 0, REG_SZ, (BYTE*)version.c_str(), (DWORD)version.size() + 1);
    RegSetValueExA(hKey, "Publisher", 0, REG_SZ, (BYTE*)"light", 5);
    RegSetValueExA(hKey, "InstallLocation", 0, REG_SZ, (BYTE*)appPath.c_str(), (DWORD)appPath.size() + 1);
    RegSetValueExA(hKey, "UninstallString", 0, REG_SZ, (BYTE*)uninstallPath.c_str(), (DWORD)uninstallPath.size() + 1);
    if (!iconPath.empty()) { RegSetValueExA(hKey, "DisplayIcon", 0, REG_SZ, (BYTE*)iconPath.c_str(), (DWORD)iconPath.size() + 1); }
    DWORD noModify = 1;
    RegSetValueExA(hKey, "NoModify", 0, REG_DWORD, (BYTE*)&noModify, sizeof(DWORD));
    RegSetValueExA(hKey, "NoRepair", 0, REG_DWORD, (BYTE*)&noModify, sizeof(DWORD));
    RegCloseKey(hKey);
    return true;
}

bool peizhi() {
    SetConsoleTextAttribute(hConsole, L_default);
    string programDir = GetProgramDir();
    string parentDir = GetParentDir(programDir);
    string lightExe = parentDir + "\\light.exe";
    string uninstallExe = parentDir + "\\rppcst\\light_Uninstall.exe";
    string sourceIcon = programDir + "\\rppcst\\favicon.ico";
    string hiddenDir = "C:\\Program Files\\.light";
    string binaryHexEditor = parentDir + "\\BinaryHexEditor.exe";

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "[*] 创建目录..." << endl;
    StartSpinner();
    Sleep(3000);
    if (CreateDirectoryA(hiddenDir.c_str(), NULL)) {
        SetFileAttributesA(hiddenDir.c_str(), FILE_ATTRIBUTE_HIDDEN);
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[OK] 创建目录" << endl;
    }
    else if (GetLastError() == ERROR_ALREADY_EXISTS) {
        SetFileAttributesA(hiddenDir.c_str(), FILE_ATTRIBUTE_HIDDEN);
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[OK] 目录已存在" << endl;
    }
    else {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[X] 创建目录失败，错误码: " << GetLastError() << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    StopSpinner();
    Sleep(3000);

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "[*] 复制图标..." << endl;
    StartSpinner();
    Sleep(3000);
    string iconPath = CopyIconToSystem(sourceIcon);
    if (!iconPath.empty()) {
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[OK] 图标已保存" << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_warning);
        StopSpinner();
        cout << "[!] 未找到 favicon.ico" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    StopSpinner();
    Sleep(3000);

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "[*] 创建加密文件..." << endl;
    StartSpinner();
    Sleep(3000);
    string xbyxFile = hiddenDir + "\\light.xbyx";
    ofstream file(xbyxFile);
    if (file.is_open()) {
        file.close();
        SetFileAttributesA(xbyxFile.c_str(), FILE_ATTRIBUTE_HIDDEN);
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[OK] 创建文件: light.xbyx" << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        StopSpinner();
        cout << "[X] 创建文件失败" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    StopSpinner();
    Sleep(3000);

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "[*] 注册文件类型..." << endl;
    StartSpinner();
    Sleep(3000);

    bool hexEditorExists = (GetFileAttributesA(binaryHexEditor.c_str()) != INVALID_FILE_ATTRIBUTES);
    string openExePath = hexEditorExists ? binaryHexEditor : lightExe;

    bool xayxOk = RegisterFileType("xayx", "light 1级文件", iconPath, openExePath);
    bool xbyxOk = RegisterFileType("xbyx", "light 2级文件", iconPath, openExePath);
    bool xcyxOk = RegisterFileType("xcyx", "light 3级文件", iconPath, openExePath);
    Sleep(3000);
    if (xayxOk) {
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[OK] .xayx - 1级文件 (打开程序: " << openExePath << ")" << endl;
    }
    else {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[X] .xayx - 注册失败" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    Sleep(3000);
    if (xbyxOk) {
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[OK] .xbyx - 2级文件" << endl;
    }
    else {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[X] .xbyx - 注册失败" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    Sleep(3000);
    if (xcyxOk) {
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[OK] .xcyx - 3级文件" << endl;
    }
    else {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[X] .xcyx - 注册失败" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    StopSpinner();
    Sleep(3000);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "[*] 注册应用程序..." << endl;
    StartSpinner();
    Sleep(3000);
    if (RegisterApplication("light", lightExe, uninstallExe, iconPath, "1.0.0")) {
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[OK] 已添加到应用和功能" << endl;
    }
    else {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[X] 注册应用程序失败" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }

    SetConsoleTextAttribute(hConsole, L_6);
    cout << "\n[*] 完成！BinaryHexEditor已设置为默认程序" << endl;
    StopSpinner();
    Sleep(3000);
    SetConsoleTextAttribute(hConsole, L_default);
    return true;
}

bool CreateFolder123(const std::string& basePath, const std::string& wjjmc) {
    std::string folderPath = basePath + "\\" + wjjmc;

    // 使用 CreateDirectoryA 创建文件夹
    if (CreateDirectoryA(folderPath.c_str(), NULL)) {
        return true;
    }

    DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS) {
        return true;  // 已存在，视为成功
    }

    std::cerr << "创建文件夹失败: " << folderPath << " 错误码: " << error << std::endl;
    return false;
}

bool uninstall() {
    string programDir = GetProgramDir();
    string parentDir = GetParentDir(programDir);
    string hiddenDir = "C:\\Program Files\\.light";
    string uninstallExe = parentDir + "\\rppcst\\light_Uninstall.exe";
    DeleteFilesInDirectory(hiddenDir);
    RemoveDirectoryA(hiddenDir.c_str());
    SHDeleteKeyA(HKEY_CLASSES_ROOT, ".xbyx");
    SHDeleteKeyA(HKEY_CLASSES_ROOT, "light.xbyx");
    SHDeleteKeyA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Light");
    LaunchProgram(uninstallExe, true);
    return true;
}

// ==================== Bing搜索 ====================

string GenerateRandomString(int length) {
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    string result;
    result.reserve(length);
    srand((unsigned)time(NULL));
    for (int i = 0; i < length; ++i) result += alphanum[rand() % (sizeof(alphanum) - 1)];
    return result;
}

string GenerateCVID() {
    string cvid;
    cvid.reserve(32);
    srand((unsigned)time(NULL));
    for (int i = 0; i < 32; ++i) cvid += "0123456789abcdef"[rand() % 16];
    return cvid;
}

string HttpGet(const string& url) {
    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return "";
    string host, path;
    if (url.find("https://") == 0) host = url.substr(8);
    else if (url.find("http://") == 0) host = url.substr(7);
    else { WinHttpCloseHandle(hSession); return ""; }
    size_t slashPos = host.find('/');
    if (slashPos != string::npos) { path = host.substr(slashPos); host = host.substr(0, slashPos); }
    else path = "/";
    HINTERNET hConnect = WinHttpConnect(hSession, wstring(host.begin(), host.end()).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wstring(path.begin(), path.end()).c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }
    wstring headers = L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n";
    if (!WinHttpSendRequest(hRequest, headers.c_str(), headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return "";
    }
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return "";
    }
    string response;
    DWORD bytesRead = 0;
    char buffer[8192];
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return response;
}

// ==================== 搜索下载：URL / 系统适配辅助函数 ====================

static string ToLowerAscii(const string& str) {
    string result = str;
    transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

static bool EndsWithAscii(const string& str, const string& suffix) {
    if (str.size() < suffix.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// 百分号解码（%XX -> 对应字符）
static string PercentDecode(const string& str) {
    string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hexVal(str[i + 1]);
            int lo = hexVal(str[i + 2]);
            if (hi >= 0 && lo >= 0) {
                result.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        result.push_back(str[i]);
    }
    return result;
}

// Base64URL 解码（'-' -> '+'，'_' -> '/'，自动补齐 '='）
static string Base64UrlDecode(const string& input) {
    static const string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string stdForm;
    stdForm.reserve(input.size() + 4);
    for (char c : input) {
        if (c == '-') stdForm.push_back('+');
        else if (c == '_') stdForm.push_back('/');
        else stdForm.push_back(c);
    }
    while (stdForm.size() % 4 != 0) stdForm.push_back('=');
    string out;
    int val = 0;
    int valb = -8;
    for (unsigned char c : stdForm) {
        if (c == '=' || c == '\n' || c == '\r') break;
        size_t pos = table.find(static_cast<char>(c));
        if (pos == string::npos) continue;
        val = (val << 6) + static_cast<int>(pos);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// 从 URL 中提取主机名（小写，不含 scheme / 端口 / 用户信息）
static string GetUrlHost(const string& url) {
    string host = url;
    size_t scheme = host.find("://");
    if (scheme != string::npos) host = host.substr(scheme + 3);
    size_t slash = host.find('/');
    if (slash != string::npos) host = host.substr(0, slash);
    size_t at = host.find('@');
    if (at != string::npos) host = host.substr(at + 1);
    size_t colon = host.find(':');
    if (colon != string::npos) host = host.substr(0, colon);
    return ToLowerAscii(host);
}

// 搜索引擎 / 广告 / 社交域名黑名单（按主机名精确或子域匹配，避免子串误伤）
static bool IsBlacklistedHost(const string& host) {
    static const vector<string> blacklist = {
        "bing.com", "bing.net", "microsoft.com", "go.microsoft.com", "msn.com",
        "google.com", "gstatic.com", "googleapis.com", "googlesyndication.com", "doubleclick.net",
        "facebook.com", "twitter.com", "x.com", "youtube.com",
        "baidu.com", "zhihu.com"
    };
    string lower = ToLowerAscii(host);
    for (const auto& domain : blacklist) {
        if (lower == domain) return true;
        if (lower.size() > domain.size() + 1 &&
            lower.compare(lower.size() - domain.size() - 1, domain.size() + 1, "." + domain) == 0) {
            return true;
        }
    }
    return false;
}

// 还原 Bing 跳转链接（ck/a）中的真实 URL
// Bing 会把有机搜索结果包装成 https://www.bing.com/ck/a?...&u=a1<base64url>...
// 真实 URL 以 "a1" 前缀 + Base64URL 形式编码在 u 参数里；不解码就会丢掉几乎全部结果
static string DecodeBingRedirectUrl(const string& url) {
    if (url.find("/ck/a") == string::npos) return url;
    size_t uPos = url.find("&u=");
    if (uPos == string::npos) uPos = url.find("?u=");
    if (uPos == string::npos) return url;
    size_t valStart = uPos + 3;
    size_t valEnd = url.find('&', valStart);
    string val = (valEnd == string::npos) ? url.substr(valStart) : url.substr(valStart, valEnd - valStart);
    if (val.empty()) return url;
    val = PercentDecode(val);
    if (val.size() > 2 && val[0] == 'a' && val[1] == '1') val = val.substr(2);
    string decoded = Base64UrlDecode(val);
    if (decoded.find("http://") == 0 || decoded.find("https://") == 0) return decoded;
    return url;
}

// 从 URL 提取文件名（去掉 query / fragment）
static string ExtractFileNameFromUrl(const string& url) {
    string path = url;
    size_t hash = path.find('#');
    if (hash != string::npos) path = path.substr(0, hash);
    size_t query = path.find('?');
    if (query != string::npos) path = path.substr(0, query);
    size_t slash = path.find_last_of('/');
    if (slash != string::npos) path = path.substr(slash + 1);
    return path;
}

// 获取本机原生架构（不受进程位数影响）：x64 / x86 / arm64
static string GetNativeSystemArch() {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
    case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
    case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
    default: return "x64";
    }
}

// 判断是否为安装包类文件
static bool IsInstallerPackageFile(const string& fileName) {
    string lower = ToLowerAscii(fileName);
    static const vector<string> installerExts = {
        ".exe", ".msi", ".dmg", ".pkg", ".deb", ".rpm", ".apk", ".appimage", ".run"
    };
    for (const auto& ext : installerExts) {
        if (EndsWithAscii(lower, ext)) return true;
    }
    return false;
}

// 文件 / 安装包与当前系统的适配评分
// 返回值：< 0 表示不适配当前系统（应过滤掉）；>= 0 为适配度，越大越优先
static int ScorePackageForSystem(const string& fileName) {
    string arch = GetNativeSystemArch();
    string lower = ToLowerAscii(fileName);

    // ---- 明确属于其它操作系统的包 / 文件 → 过滤 ----
    bool isMac = EndsWithAscii(lower, ".dmg") || EndsWithAscii(lower, ".pkg") ||
        lower.find("macos") != string::npos || lower.find("osx") != string::npos ||
        lower.find("darwin") != string::npos;
    bool isLinux = EndsWithAscii(lower, ".deb") || EndsWithAscii(lower, ".rpm") ||
        EndsWithAscii(lower, ".appimage") || EndsWithAscii(lower, ".run") ||
        lower.find("linux") != string::npos;
    bool isAndroid = EndsWithAscii(lower, ".apk") || lower.find("android") != string::npos;
    if (isMac || isLinux || isAndroid) return -1;

    // ---- 架构标记（先判断 x86_64/amd64，避免被 x86 误伤）----
    bool isArm64 = lower.find("arm64") != string::npos || lower.find("aarch64") != string::npos;
    bool isX64 = lower.find("x86_64") != string::npos || lower.find("x64") != string::npos ||
        lower.find("amd64") != string::npos || lower.find("win64") != string::npos ||
        lower.find("64bit") != string::npos || lower.find("64-bit") != string::npos;
    bool isX86 = lower.find("x86") != string::npos || lower.find("win32") != string::npos ||
        lower.find("32bit") != string::npos || lower.find("32-bit") != string::npos ||
        lower.find("ia32") != string::npos;

    if (arch == "arm64") {
        if (isArm64) return 100;
        if (isX64 || isX86) return -1;
        return 60; // 无架构标记，保留
    }
    if (arch == "x64") {
        if (isArm64) return -1;
        if (isX64) return 100;  // 先判 x64（x86_64 含 x86 子串，避免误伤）
        if (isX86) return 10;   // 32 位包可在 64 位系统运行，但非最优
        return 80;              // 无架构标记，默认可信
    }
    // x86（32 位系统）
    if (isArm64 || isX64) return -1;
    if (isX86) return 100;
    return 60;
}

// 下载页面链接的相关度评分（用于排序，不再做硬性关键词过滤）
static int ScoreDownloadPageLink(const string& url) {
    string lower = ToLowerAscii(url);
    int score = 10; // 有机搜索结果基础分
    static const vector<string> fileExts = {
        ".exe", ".msi", ".zip", ".7z", ".rar", ".tar.gz", ".tgz", ".dmg", ".pkg",
        ".deb", ".rpm", ".apk", ".jar", ".iso", ".bin", ".appimage"
    };
    for (const auto& ext : fileExts) {
        if (lower.find(ext) != string::npos) { score += 100; break; }
    }
    static const vector<string> keywords = {
        "download", "releases", "release", "setup", "install", "get", "latest",
        "stable", "version", "archive", "portable", "下载", "发布", "安装"
    };
    for (const auto& kw : keywords) {
        if (lower.find(kw) != string::npos) { score += 40; break; }
    }
    static const vector<string> trustedHosts = {
        "github.com", "github.io", "gitlab.com", "sourceforge.net", "bitbucket.org",
        "archive.org", "launchpad.net", "fosshub.com", "majorgeeks.com"
    };
    string host = GetUrlHost(url);
    for (const auto& h : trustedHosts) {
        if (host == h || (host.size() > h.size() &&
            host.compare(host.size() - h.size() - 1, h.size() + 1, "." + h) == 0)) {
            score += 30;
            break;
        }
    }
    return score;
}

// HTML 实体解码（&amp; &lt; &gt; &quot; &#39; &#xHH; 等）
static string HtmlEntityDecode(const string& s) {
    string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            size_t semi = s.find(';', i + 1);
            if (semi != string::npos && semi - i <= 10) {
                string ent = s.substr(i + 1, semi - i - 1);
                if (ent == "amp") { out += '&'; i = semi; continue; }
                if (ent == "lt") { out += '<'; i = semi; continue; }
                if (ent == "gt") { out += '>'; i = semi; continue; }
                if (ent == "quot") { out += '"'; i = semi; continue; }
                if (ent == "apos" || ent == "#39") { out += '\''; i = semi; continue; }
                if (!ent.empty() && ent[0] == '#') {
                    int code = 0;
                    bool ok = false;
                    if (ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X')) {
                        try { code = stoi(ent.substr(2), nullptr, 16); ok = true; } catch (...) {}
                    }
                    else if (ent.size() > 1) {
                        try { code = stoi(ent.substr(1), nullptr, 10); ok = true; } catch (...) {}
                    }
                    if (ok && code > 0 && code < 128) { out += static_cast<char>(code); i = semi; continue; }
                }
            }
        }
        out += s[i];
    }
    return out;
}

// 锚点链接（href + 可见文字）
struct AnchorLink {
    string url;
    string text;
};

// 提取 HTML 中所有 <a> 锚点的 href 与可见文字（文字已去标签并压缩空白）
static vector<AnchorLink> ExtractAnchors(const string& html) {
    vector<AnchorLink> anchors;
    size_t pos = 0;
    while ((pos = html.find("<a", pos)) != string::npos) {
        // <a 后必须紧跟空白、'>' 或 '/'，避免误匹配 <article>/<aside> 等
        size_t after = pos + 2;
        if (after >= html.size() || !(isspace((unsigned char)html[after]) || html[after] == '>' || html[after] == '/')) {
            pos = after;
            continue;
        }
        size_t tagEnd = html.find('>', pos);
        if (tagEnd == string::npos) break;
        string tag = html.substr(pos, tagEnd - pos);
        // 提取 href（支持双引号 / 单引号 / 无引号）
        string url;
        size_t hrefPos = tag.find("href=");
        if (hrefPos != string::npos) {
            size_t valStart = hrefPos + 5;
            if (valStart < tag.size() && (tag[valStart] == '"' || tag[valStart] == '\'')) {
                char q = tag[valStart];
                size_t valEnd = tag.find(q, valStart + 1);
                if (valEnd != string::npos) url = tag.substr(valStart + 1, valEnd - valStart - 1);
            }
            else {
                size_t valEnd = valStart;
                while (valEnd < tag.size() && !isspace((unsigned char)tag[valEnd])) valEnd++;
                url = tag.substr(valStart, valEnd - valStart);
            }
        }
        // 提取可见文字（到 </a>，剔除嵌套标签）
        string text;
        size_t closePos = html.find("</a>", tagEnd);
        if (closePos != string::npos) {
            string inner = html.substr(tagEnd + 1, closePos - tagEnd - 1);
            string stripped;
            bool inTag = false;
            for (char c : inner) {
                if (c == '<') inTag = true;
                else if (c == '>') inTag = false;
                else if (!inTag) stripped += c;
            }
            text = stripped;
        }
        if (!url.empty()) {
            AnchorLink a;
            a.url = HtmlEntityDecode(url);
            string t;
            bool lastSpace = true;
            for (char c : text) {
                if (isspace((unsigned char)c)) { if (!lastSpace) { t += ' '; lastSpace = true; } }
                else { t += c; lastSpace = false; }
            }
            while (!t.empty() && t.back() == ' ') t.pop_back();
            a.text = t;
            anchors.push_back(a);
        }
        pos = (closePos != string::npos) ? closePos + 4 : tagEnd + 1;
    }
    return anchors;
}

// URL 是否指向带文件扩展名的直链
static bool IsFileExtensionUrl(const string& url) {
    string lower = ToLowerAscii(url);
    static const vector<string> exts = {
        ".exe", ".msi", ".zip", ".jar", ".dmg", ".pkg", ".deb", ".rpm", ".apk",
        ".tar.gz", ".tgz", ".7z", ".rar", ".iso", ".bin", ".appimage", ".run"
    };
    for (const auto& e : exts) { if (lower.find(e) != string::npos) return true; }
    return false;
}

// 无扩展名的“下载入口” URL 识别（VS Code 的 /sha/download?...、GitHub releases、通用 /download? 等）
static bool IsDownloadEntryUrl(const string& url) {
    string lower = ToLowerAscii(url);
    if (lower.find("/releases/download/") != string::npos) return true;
    if (lower.find("/releases/latest/download/") != string::npos) return true;
    if (lower.find("/archive/refs/tags/") != string::npos) return true;
    if (lower.find("/sha/download") != string::npos) return true;
    // 路径部分以 /download 结尾，或含 /download? /download/
    string path = lower;
    size_t q = path.find('?');
    if (q != string::npos) path = path.substr(0, q);
    size_t h = path.find('#');
    if (h != string::npos) path = path.substr(0, h);
    if (EndsWithAscii(path, "/download")) return true;
    if (lower.find("/download?") != string::npos) return true;
    if (lower.find("/download/") != string::npos) return true;
    return false;
}

// 锚点文字是否提示这是下载链接（"User Installer (x64)"、".zip (x64)"、"下载" 等）
static bool TextSuggestsDownload(const string& text) {
    string lower = ToLowerAscii(text);
    if (lower.empty()) return false;
    static const vector<string> kws = {
        "download", "下载", "获取", "installer", "安装", "setup", "portable", "便携"
    };
    for (const auto& k : kws) { if (lower.find(k) != string::npos) return true; }
    static const vector<string> exts = {
        ".exe", ".zip", ".msi", ".dmg", ".deb", ".rpm", ".tar.gz", ".7z", ".pkg", ".apk"
    };
    for (const auto& e : exts) { if (lower.find(e) != string::npos) return true; }
    return false;
}

// 从展示文字中推断显式扩展名（如 ".zip (x64)" -> ".zip"），推断不出返回空
static string InferExtensionFromText(const string& text) {
    string lower = ToLowerAscii(text);
    static const vector<string> exts = {
        ".tar.gz", ".exe", ".zip", ".msi", ".dmg", ".deb", ".rpm", ".7z", ".pkg", ".apk", ".jar"
    };
    for (const auto& e : exts) { if (lower.find(e) != string::npos) return e; }
    return "";
}

// 清理为合法文件名（替换 Windows 非法字符，去首尾空白和点）
static string SanitizeFileName(const string& name) {
    string out;
    for (char c : name) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == '\r' || c == '\n') {
            out += '_';
        }
        else out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    size_t start = out.find_first_not_of(' ');
    if (start != string::npos) out = out.substr(start);
    if (out.empty()) out = "downloaded_file";
    return out;
}

// 通过 WinHTTP HEAD 读取 Content-Disposition 中的真实文件名（无扩展名下载入口常靠它）
static string GetContentDispositionFileName(const string& url) {
    if (url.find("http://") != 0 && url.find("https://") != 0) return "";
    string host, path;
    if (url.find("https://") == 0) host = url.substr(8);
    else host = url.substr(7);
    size_t slashPos = host.find('/');
    if (slashPos != string::npos) { path = host.substr(slashPos); host = host.substr(0, slashPos); }
    else path = "/";
    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    HINTERNET hConnect = WinHttpConnect(hSession, wstring(host.begin(), host.end()).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"HEAD", wstring(path.begin(), path.end()).c_str(),
        NULL, WINHTTP_NO_REFERER, NULL, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }
    string result;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD dwSize = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"Content-Disposition", WINHTTP_NO_OUTPUT_BUFFER, &dwSize, WINHTTP_HEADER_NAME_BY_INDEX);
        if (dwSize > 0) {
            vector<wchar_t> buffer(dwSize / sizeof(wchar_t) + 1);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"Content-Disposition", buffer.data(), &dwSize, WINHTTP_HEADER_NAME_BY_INDEX)) {
                wstring ws(buffer.data());
                // UTF-16 -> UTF-8（避免窄转换警告与非 ASCII 文件名乱码）
                string s;
                if (!ws.empty()) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), NULL, 0, NULL, NULL);
                    if (len > 0) {
                        s.resize(static_cast<size_t>(len));
                        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), &s[0], len, NULL, NULL);
                    }
                }
                string lower = ToLowerAscii(s);
                size_t fnPos = lower.find("filename=");
                if (fnPos != string::npos) {
                    size_t valStart = fnPos + 9;
                    if (valStart < s.size() && s[valStart] == '"') {
                        size_t valEnd = s.find('"', valStart + 1);
                        if (valEnd != string::npos) result = s.substr(valStart + 1, valEnd - valStart - 1);
                    }
                    else {
                        size_t valEnd = s.find(';', valStart);
                        result = (valEnd == string::npos) ? s.substr(valStart) : s.substr(valStart, valEnd - valStart);
                    }
                }
            }
        }
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    // 去掉路径部分，仅保留文件名
    if (!result.empty()) {
        size_t slash = result.find_last_of("\\/");
        if (slash != string::npos) result = result.substr(slash + 1);
    }
    return result;
}

// 推导下载保存文件名：
//   1) URL 路径末段（带已知扩展名时直接采用）
//   2) 无扩展名时读取 Content-Disposition（真实文件名）
//   3) 再退到锚点展示名（并从文字推断扩展名，如 ".zip (x64)" -> .zip）
//   4) 全部失败用 downloaded_file
static string DeriveDownloadFileName(const string& url, const string& displayName = "") {
    string fileName = ExtractFileNameFromUrl(url);
    if (!fileName.empty() && IsFileExtensionUrl(fileName)) {
        return SanitizeFileName(fileName);
    }
    // 无扩展名：尝试 Content-Disposition
    string cdName = GetContentDispositionFileName(url);
    if (!cdName.empty()) {
        return SanitizeFileName(cdName);
    }
    // 退到展示名
    if (!displayName.empty()) {
        string base = SanitizeFileName(displayName);
        if (!IsFileExtensionUrl(base)) {
            string ext = InferExtensionFromText(displayName);
            if (!ext.empty()) base += ext;
        }
        return base;
    }
    return fileName.empty() ? "downloaded_file" : SanitizeFileName(fileName);
}

// 文件下载链接信息（含系统适配评分）
struct FileLinkInfo {
    string url;
    string fileName;    // URL 中的文件名（无扩展名下载入口可能为空）
    string displayName; // 展示名（优先用锚点文字，如 "User Installer (x64)"）
    int systemScore;    // 系统适配评分
    bool isInstaller;   // 是否安装包
};

// 对文件下载链接统一处理：
//   1) 还原 Bing 跳转链接、提取文件名与展示名（锚点文字）
//   2) 按系统类型过滤不适配的安装包 / 文件（并提示）
//   3) 去掉文件名称相同的下载链接（保留适配度最高的一条，并提示）
//   4) 按系统适配度降序排序
// 架构识别同时看 URL 与锚点文字（如 "?dv=win64user" 搭配 "User Installer (x64)"）
static vector<FileLinkInfo> FilterAndRankFileLinks(const vector<AnchorLink>& links) {
    vector<FileLinkInfo> infos;
    int filteredBySystem = 0;
    for (const auto& anchor : links) {
        string url = DecodeBingRedirectUrl(anchor.url);
        if (url.find("http://") != 0 && url.find("https://") != 0) continue;
        string fileName = ExtractFileNameFromUrl(url);
        string displayName = anchor.text.empty() ? fileName : anchor.text;
        // 架构 / 系统标记可能只在锚点文字里（如 ".zip (x64)"），合并后评分
        string nameForScore = ToLowerAscii(displayName + " " + url);
        int score = ScorePackageForSystem(nameForScore);
        if (score < 0) {
            filteredBySystem++;
            SetConsoleTextAttribute(hConsole, L_warning);
            cout << "[系统过滤] 跳过不适配当前系统(" << GetNativeSystemArch() << ")的文件: "
                 << displayName << "  <-  " << url << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            continue;
        }
        FileLinkInfo info;
        info.url = url;
        info.fileName = fileName;
        info.displayName = displayName.empty() ? url : displayName;
        info.systemScore = score;
        info.isInstaller = IsInstallerPackageFile(nameForScore);
        infos.push_back(info);
    }

    // 同名文件去重：保留系统适配度最高的一条链接
    // 注意：直链（带扩展名）按文件名去重（过滤镜像/多版本同文件）；
    //       无扩展名下载入口（如 /thank-you?dv=win64user）按展示名去重，
    //       否则同一路径段（thank-you）会把不同变体误折叠成一个
    vector<FileLinkInfo> result;
    map<string, size_t> indexByKey;
    int removedDuplicates = 0;
    for (const auto& info : infos) {
        string key = IsFileExtensionUrl(info.url)
            ? ToLowerAscii(info.fileName)
            : ToLowerAscii(info.displayName);
        auto it = indexByKey.find(key);
        if (it == indexByKey.end()) {
            indexByKey[key] = result.size();
            result.push_back(info);
        }
        else {
            size_t idx = it->second;
            removedDuplicates++;
            if (info.systemScore > result[idx].systemScore) {
                SetConsoleTextAttribute(hConsole, L_warning);
                cout << "[同名去重] 文件 \"" << info.displayName << "\" 有多个下载链接，"
                     << "移除适配度较低的: " << result[idx].url << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                result[idx] = info;
            }
            else {
                SetConsoleTextAttribute(hConsole, L_warning);
                cout << "[同名去重] 已移除同名文件链接: " << info.url
                     << "  (文件名: " << info.displayName << ")" << endl;
                SetConsoleTextAttribute(hConsole, L_default);
            }
        }
    }

    // 按系统适配度降序排序（稳定排序，同分保持原有顺序）
    stable_sort(result.begin(), result.end(),
        [](const FileLinkInfo& a, const FileLinkInfo& b) { return a.systemScore > b.systemScore; });

    if (filteredBySystem > 0 || removedDuplicates > 0) {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[信息] 已按系统类型(" << GetNativeSystemArch() << ")过滤 " << filteredBySystem
             << " 个不适配链接，去除 " << removedDuplicates << " 个同名文件链接，剩余 "
             << result.size() << " 个可用链接" << endl;
    }
    return result;
}

class BingSearch {
private:
    HINTERNET hSession;
    HINTERNET hConnect;
    map<wstring, wstring> cookies;
    vector<string> foundLinks;
    string selectedUrl;
    string selectedName; // 所选链接的展示名（锚点文字，用于无扩展名链接的保存命名）

public:
    BingSearch() : hSession(NULL), hConnect(NULL) {}
    ~BingSearch() { if (hConnect) WinHttpCloseHandle(hConnect); if (hSession) WinHttpCloseHandle(hSession); }

    // 提取 HTML 中所有 href 属性值（同时支持双引号和单引号）
    static void ExtractAllHrefs(const string& html, vector<string>& out) {
        size_t pos = 0;
        while ((pos = html.find("href=", pos)) != string::npos) {
            size_t valStart = pos + 5;
            if (valStart >= html.size()) break;
            char quote = html[valStart];
            if (quote != '"' && quote != '\'') { pos = valStart; continue; }
            size_t valEnd = html.find(quote, valStart + 1);
            if (valEnd == string::npos) break;
            string link = html.substr(valStart + 1, valEnd - valStart - 1);
            if (!link.empty()) out.push_back(link);
            pos = valEnd + 1;
        }
    }

    // 提取有机搜索结果块（class="b_algo"）片段，过滤掉广告 / 导航 / 相关搜索等噪音
    vector<string> ExtractOrganicResultSegments(const string& html) {
        vector<string> segments;
        size_t pos = 0;
        while ((pos = html.find("b_algo", pos)) != string::npos) {
            size_t liStart = html.rfind('<', pos);
            if (liStart == string::npos) liStart = pos;
            size_t segEnd = html.find("</li>", pos);
            if (segEnd == string::npos) segEnd = html.size();
            segments.push_back(html.substr(liStart, segEnd - liStart));
            pos = segEnd;
        }
        return segments;
    }

    vector<string> ExtractDownloadPageLinks(const string& html) {
        vector<string> pageLinks;
        if (html.empty()) return pageLinks;
        vector<string> rawLinks;
        vector<string> organic = ExtractOrganicResultSegments(html);
        if (!organic.empty()) {
            // 优先从有机结果块中提取，结果更精准
            for (const auto& seg : organic) ExtractAllHrefs(seg, rawLinks);
        }
        else {
            ExtractAllHrefs(html, rawLinks);
        }
        for (const auto& raw : rawLinks) {
            // 还原 Bing ck/a 跳转链接中的真实 URL（关键修复：否则几乎全部结果都会被丢弃）
            string link = DecodeBingRedirectUrl(raw);
            if (link.find("http://") != 0 && link.find("https://") != 0) continue;
            if (IsBlacklistedHost(GetUrlHost(link))) continue;
            pageLinks.push_back(link);
        }
        // 先去重（相同 URL 只保留一个）
        sort(pageLinks.begin(), pageLinks.end());
        pageLinks.erase(unique(pageLinks.begin(), pageLinks.end()), pageLinks.end());
        // 按下载相关度排序：直链文件 > 下载关键词 > 可信软件托管域名 > 普通有机结果
        stable_sort(pageLinks.begin(), pageLinks.end(),
            [](const string& a, const string& b) { return ScoreDownloadPageLink(a) > ScoreDownloadPageLink(b); });
        // 限制页面解析数量，避免逐个抓取过多页面
        const size_t MAX_PAGE_LINKS = 12;
        if (pageLinks.size() > MAX_PAGE_LINKS) {
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[信息] 链接较多，仅解析相关度最高的前 " << MAX_PAGE_LINKS << " 个下载页面" << endl;
            pageLinks.resize(MAX_PAGE_LINKS);
        }
        return pageLinks;
    }

    bool IsValidDownloadPage(const string& link) {
        string lowerLink = link;
        transform(lowerLink.begin(), lowerLink.end(), lowerLink.begin(), ::tolower);
        vector<string> keywords = { "download", "release", "releases", "setup", "install", "get", "latest", "stable", "version", "archive", "下载", "发布", "安装" };
        for (const auto& keyword : keywords) { if (lowerLink.find(keyword) != string::npos) return true; }
        if (lowerLink.find("github.com") != string::npos) {
            if (lowerLink.find("/blob/") == string::npos && lowerLink.find("/issues") == string::npos && lowerLink.find("/pull/") == string::npos && lowerLink.find("/wiki") == string::npos) return true;
        }
        vector<string> trustedDomains = { "ghidra.net", "sourceforge.net", "gitlab.com", "bitbucket.org", "code.google.com", "archive.org", "launchpad.net" };
        for (const auto& domain : trustedDomains) { if (lowerLink.find(domain) != string::npos) return true; }
        vector<string> extensions = { ".exe", ".msi", ".zip", ".jar", ".dmg", ".pkg", ".deb", ".rpm", ".apk", ".tar.gz", ".tgz", ".7z", ".rar", ".iso", ".bin", ".xayx", ".xbyx", ".xcyx", ".xzyx" };
        for (const auto& ext : extensions) { if (lowerLink.find(ext) != string::npos) return true; }
        return false;
    }

    bool ParseAllDownloadPagesForFiles(const vector<string>& pageUrls) {
        if (pageUrls.empty()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 没有下载页面 URL" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[信息] 正在解析 " << pageUrls.size() << " 个下载页面..." << endl;
        vector<AnchorLink> allAnchors;
        int processedCount = 0;
        for (const auto& pageUrl : pageUrls) {
            processedCount++;
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[信息] 正在解析页面 " << processedCount << "/" << pageUrls.size() << ": " << pageUrl << endl;
            string urlToFetch = pageUrl;
            bool isGitHub = pageUrl.find("github.com") != string::npos;
            bool isGitHubReleases = isGitHub && pageUrl.find("/releases") != string::npos;
            if (isGitHub && !isGitHubReleases) {
                urlToFetch = (pageUrl.back() == '/') ? pageUrl + "releases" : pageUrl + "/releases";
                isGitHubReleases = true;
                SetConsoleTextAttribute(hConsole, L_default);
                cout << "[信息] 尝试 GitHub Releases: " << urlToFetch << endl;
            }
            string host, path;
            if (urlToFetch.find("https://") == 0) host = urlToFetch.substr(8);
            else if (urlToFetch.find("http://") == 0) host = urlToFetch.substr(7);
            else continue;
            size_t slashPos = host.find('/');
            if (slashPos != string::npos) { path = host.substr(slashPos); host = host.substr(0, slashPos); }
            else path = "/";
            HINTERNET hConnectPage = WinHttpConnect(hSession, wstring(host.begin(), host.end()).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (!hConnectPage) {
                SetConsoleTextAttribute(hConsole, L_warning);
                cout << "[警告] 无法连接到: " << urlToFetch << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                continue;
            }
            HINTERNET hRequest = WinHttpOpenRequest(hConnectPage, L"GET", wstring(path.begin(), path.end()).c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
            if (!hRequest) { WinHttpCloseHandle(hConnectPage); continue; }
            wstring headers = L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9\r\nAccept-Language: zh-CN,zh;q=0.9,en;q=0.8,ja;q=0.7,ko;q=0.6\r\nConnection: keep-alive\r\n";
            if (!WinHttpSendRequest(hRequest, headers.c_str(), headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnectPage); continue; }
            if (!WinHttpReceiveResponse(hRequest, NULL)) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnectPage); continue; }
            string response = ReadResponse(hRequest);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnectPage);
            if (response.empty()) continue;
            if (isGitHubReleases) {
                size_t pos = 0;
                while ((pos = response.find("/releases/expanded_assets/", pos)) != string::npos) {
                    size_t end = response.find("\"", pos);
                    if (end != string::npos) {
                        string assetsPath = response.substr(pos, end - pos);
                        string assetsUrl = "https://github.com" + assetsPath;
                        SetConsoleTextAttribute(hConsole, L_default);
                        cout << "[信息] 发现 Assets 页面: " << assetsUrl << endl;
                        string assetsResponse = FetchPage(assetsUrl, host);
                        if (!assetsResponse.empty()) ExtractFileLinksFromAssets(assetsResponse, host, allAnchors);
                        pos = end + 1;
                    }
                    else break;
                }
                if (allAnchors.empty()) {
                    for (const auto& a : ExtractAnchors(response)) {
                        string link = DecodeBingRedirectUrl(a.url);
                        if (link.empty()) continue;
                        if (IsFileExtensionUrl(link) || IsDownloadEntryUrl(link) || TextSuggestsDownload(a.text)) {
                            if (link.find("http") != 0) {
                                link = (link[0] == '/') ? "https://github.com" + link : "https://" + host + "/" + link;
                            }
                            allAnchors.push_back({ link, a.text });
                        }
                    }
                }
            }
            // 提取文件链接：扩展名直链 + 无扩展名下载入口 + 锚点文字含下载特征的链接
            for (const auto& a : ExtractAnchors(response)) {
                string link = DecodeBingRedirectUrl(a.url);
                if (link.empty()) continue;
                bool isFileLink = IsFileExtensionUrl(link) || IsDownloadEntryUrl(link) || TextSuggestsDownload(a.text);
                if (isFileLink) {
                    if (link.find("http") != 0) {
                        if (!link.empty() && link[0] == '/') link = "https://" + host + link;
                        else if (!link.empty()) {
                            size_t lastSlash = path.find_last_of('/');
                            if (lastSlash != string::npos) { string basePath = path.substr(0, lastSlash + 1); link = "https://" + host + basePath + link; }
                            else link = "https://" + host + "/" + link;
                        }
                    }
                    // 相对链接解析后再跳过页面自身链接（大小写不敏感）
                    if (ToLowerAscii(link) == ToLowerAscii(pageUrl) || ToLowerAscii(link) == ToLowerAscii(urlToFetch)) continue;
                    allAnchors.push_back({ link, a.text });
                }
            }
        }
        if (allAnchors.empty()) {
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[信息] 在所有下载页面中未找到文件链接" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        // 按系统类型过滤安装包 + 同名文件去重 + 按适配度排序
        vector<FileLinkInfo> fileInfos = FilterAndRankFileLinks(allAnchors);
        if (fileInfos.empty()) {
            SetConsoleTextAttribute(hConsole, L_warning);
            cout << "[信息] 没有适配当前系统(" << GetNativeSystemArch() << ")的文件链接" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "\n========================================" << endl;
        cout << "从 " << processedCount << " 个下载页面中找到 " << fileInfos.size() << " 个可用文件链接：" << endl;
        cout << "========================================" << endl;
        for (size_t i = 0; i < fileInfos.size(); ++i) {
            cout << "[" << (i + 1) << "] " << fileInfos[i].displayName;
            if (fileInfos[i].isInstaller) cout << " [安装包]";
            if (fileInfos[i].systemScore >= 100) cout << " [适配本机" << GetNativeSystemArch() << "]";
            cout << endl;
            cout << "     " << fileInfos[i].url << endl;
        }
        cout << "========================================" << endl;
        cout << "[0] 取消" << endl;
        cout << "请选择文件链接 (0-" << fileInfos.size() << "): ";
        int choice;
        cin >> choice;
        if (choice == 0) {
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[信息] 用户取消选择" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        if (choice < 1 || choice >(int)fileInfos.size()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 无效选择" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        selectedUrl = fileInfos[choice - 1].url;
        selectedName = fileInfos[choice - 1].displayName;
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[信息] 已选择实际文件: " << selectedUrl << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return true;
    }

    void ExtractFileLinksFromAssets(const string& assetsHtml, const string& host, vector<AnchorLink>& allFileLinks) {
        for (const auto& a : ExtractAnchors(assetsHtml)) {
            string link = DecodeBingRedirectUrl(a.url);
            if (link.empty()) continue;
            if (IsFileExtensionUrl(link) || IsDownloadEntryUrl(link) || TextSuggestsDownload(a.text)) {
                if (link.find("http") != 0) { link = (link[0] == '/') ? "https://github.com" + link : "https://" + host + "/" + link; }
                allFileLinks.push_back({ link, a.text });
            }
        }
    }

    string FetchPage(const string& url, const string& defaultHost) {
        string host, path;
        if (url.find("https://") == 0) host = url.substr(8);
        else if (url.find("http://") == 0) host = url.substr(7);
        else return "";
        size_t slashPos = host.find('/');
        if (slashPos != string::npos) { path = host.substr(slashPos); host = host.substr(0, slashPos); }
        else path = "/";
        HINTERNET hConnectPage = WinHttpConnect(hSession, wstring(host.begin(), host.end()).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnectPage) return "";
        HINTERNET hRequest = WinHttpOpenRequest(hConnectPage, L"GET", wstring(path.begin(), path.end()).c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
        if (!hRequest) { WinHttpCloseHandle(hConnectPage); return ""; }
        wstring headers = L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9\r\nAccept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\nConnection: keep-alive\r\n";
        if (!WinHttpSendRequest(hRequest, headers.c_str(), headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnectPage); return ""; }
        if (!WinHttpReceiveResponse(hRequest, NULL)) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnectPage); return ""; }
        string response = ReadResponse(hRequest);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnectPage);
        return response;
    }

    bool Initialize() {
        hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] WinHttpOpen 失败: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        DWORD timeout = 30000;
        WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        return true;
    }

    bool GetInitialCookies() {
        if (!hSession) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 会话未初始化" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        hConnect = WinHttpConnect(hSession, L"cn.bing.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] WinHttpConnect 失败: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/", NULL, WINHTTP_NO_REFERER, NULL, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] WinHttpOpenRequest 失败: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        wstring headers = L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\nAccept-Language: zh-CN,zh;q=0.9,en;q=0.8,ja;q=0.7,ko;q=0.6\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\nConnection: keep-alive\r\nUpgrade-Insecure-Requests: 1\r\nCache-Control: no-cache\r\n";
        if (!WinHttpSendRequest(hRequest, headers.c_str(), headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] WinHttpSendRequest 失败: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            WinHttpCloseHandle(hRequest);
            return false;
        }
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] WinHttpReceiveResponse 失败: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            WinHttpCloseHandle(hRequest);
            return false;
        }
        ExtractCookiesFromResponse(hRequest);
        string response = ReadResponse(hRequest);
        WinHttpCloseHandle(hRequest);
        if (cookies.empty()) {
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[信息] 未获取到 Cookie，手动构造..." << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            ConstructDefaultCookies();
        }
        return true;
    }

    string Search(const string& query) {
        if (!hConnect) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 未连接到 Bing" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return "";
        }
        foundLinks.clear();
        selectedUrl.clear();
        // 查询词已包含下载相关词汇时不再重复追加，避免污染搜索意图
        string lowerQuery = ToLowerAscii(query);
        bool hasDownloadWord =
            lowerQuery.find("download") != string::npos ||
            lowerQuery.find("下载") != string::npos ||
            lowerQuery.find("安装") != string::npos;
        string finalQuery = hasDownloadWord ? query : (query + " download");
        string encodedQuery = UrlEncode(finalQuery);
        wstring path = L"/search?q=" + wstring(encodedQuery.begin(), encodedQuery.end()) + L"&form=QBLH&count=30";
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, L"https://cn.bing.com/", NULL, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 创建搜索请求失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return "";
        }
        wstring headers = L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\nAccept-Language: zh-CN,zh;q=0.9,en;q=0.8,ja;q=0.7,ko;q=0.6\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\nConnection: keep-alive\r\nUpgrade-Insecure-Requests: 1\r\nCache-Control: no-cache\r\nReferer: https://cn.bing.com/\r\nCookie: " + BuildCookieString() + L"\r\n";
        if (!WinHttpSendRequest(hRequest, headers.c_str(), headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 发送搜索请求失败: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            WinHttpCloseHandle(hRequest);
            return "";
        }
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 接收搜索响应失败: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            WinHttpCloseHandle(hRequest);
            return "";
        }
        string response = ReadResponse(hRequest);
        WinHttpCloseHandle(hRequest);
        return response;
    }

    bool ExtractAndDisplayLinks(const string& html, const string& query) {
        if (html.empty()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] HTML 为空" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        foundLinks.clear();
        size_t searchPos = 0;
        while ((searchPos = html.find("href=\"", searchPos)) != string::npos) {
            size_t linkStart = searchPos + 6;
            size_t linkEnd = html.find("\"", linkStart);
            if (linkEnd == string::npos) break;
            string link = html.substr(linkStart, linkEnd - linkStart);
            if ((link.find("http://") == 0 || link.find("https://") == 0)) {
                if (link.find("bing.com") == string::npos && link.find("microsoft.com") == string::npos && link.find("go.microsoft.com") == string::npos) {
                    if (IsValidDownloadLink(link)) foundLinks.push_back(link);
                }
            }
            searchPos = linkEnd + 1;
        }
        sort(foundLinks.begin(), foundLinks.end());
        foundLinks.erase(unique(foundLinks.begin(), foundLinks.end()), foundLinks.end());
        if (foundLinks.empty()) {
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[信息] 未找到任何下载链接" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "\n========================================" << endl;
        cout << "从搜索结果中找到 " << foundLinks.size() << " 个下载页面链接：" << endl;
        cout << "========================================" << endl;
        for (size_t i = 0; i < foundLinks.size(); ++i) cout << "[" << (i + 1) << "] " << foundLinks[i] << endl;
        cout << "========================================" << endl;
        cout << "[0] 退出" << endl;
        cout << "请选择链接编号 (0-" << foundLinks.size() << "): ";
        int choice;
        cin >> choice;
        if (choice == 0) {
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[信息] 用户取消选择" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            selectedUrl = "";
            return false;
        }
        if (choice < 1 || choice >(int)foundLinks.size()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 无效选择" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            selectedUrl = "";
            return false;
        }
        selectedUrl = foundLinks[choice - 1];
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[信息] 已选择下载页面: " << selectedUrl << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return true;
    }

    bool ParseDownloadPageForFiles(const string& pageUrl) {
        if (pageUrl.empty()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 下载页面 URL 为空" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[信息] 正在解析下载页面: " << pageUrl << endl;
        string host, path;
        if (pageUrl.find("https://") == 0) host = pageUrl.substr(8);
        else if (pageUrl.find("http://") == 0) host = pageUrl.substr(7);
        else {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 无效的 URL 格式" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        size_t slashPos = host.find('/');
        if (slashPos != string::npos) { path = host.substr(slashPos); host = host.substr(0, slashPos); }
        else path = "/";
        HINTERNET hConnectPage = WinHttpConnect(hSession, wstring(host.begin(), host.end()).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnectPage) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 无法连接到下载页面" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        HINTERNET hRequest = WinHttpOpenRequest(hConnectPage, L"GET", wstring(path.begin(), path.end()).c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            WinHttpCloseHandle(hConnectPage);
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 创建请求失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        wstring headers = L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9\r\nAccept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\nConnection: keep-alive\r\n";
        if (!WinHttpSendRequest(hRequest, headers.c_str(), headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnectPage);
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 发送请求失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnectPage);
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 接收响应失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        string response = ReadResponse(hRequest);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnectPage);
        if (response.empty()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 下载页面内容为空" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        vector<AnchorLink> pageAnchors;
        for (const auto& a : ExtractAnchors(response)) {
            string link = DecodeBingRedirectUrl(a.url);
            if (link.empty()) continue;
            if (IsFileExtensionUrl(link) || IsDownloadEntryUrl(link) || TextSuggestsDownload(a.text)) {
                if (link.find("http") != 0) {
                    if (!link.empty() && link[0] == '/') link = "https://" + host + link;
                    else if (!link.empty()) {
                        size_t lastSlash = path.find_last_of('/');
                        if (lastSlash != string::npos) { string basePath = path.substr(0, lastSlash + 1); link = "https://" + host + basePath + link; }
                        else link = "https://" + host + "/" + link;
                    }
                }
                // 相对链接解析后再跳过页面自身链接（大小写不敏感）
                if (ToLowerAscii(link) == ToLowerAscii(pageUrl)) continue;
                pageAnchors.push_back({ link, a.text });
            }
        }
        if (pageAnchors.empty()) {
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[信息] 在下载页面中未找到文件链接" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        // 按系统类型过滤安装包 + 同名文件去重 + 按适配度排序
        vector<FileLinkInfo> fileInfos = FilterAndRankFileLinks(pageAnchors);
        if (fileInfos.empty()) {
            SetConsoleTextAttribute(hConsole, L_warning);
            cout << "[信息] 没有适配当前系统(" << GetNativeSystemArch() << ")的文件链接" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "\n========================================" << endl;
        cout << "在下载页面中找到 " << fileInfos.size() << " 个可用文件链接：" << endl;
        cout << "========================================" << endl;
        for (size_t i = 0; i < fileInfos.size(); ++i) {
            cout << "[" << (i + 1) << "] " << fileInfos[i].displayName;
            if (fileInfos[i].isInstaller) cout << " [安装包]";
            if (fileInfos[i].systemScore >= 100) cout << " [适配本机" << GetNativeSystemArch() << "]";
            cout << endl;
            cout << "     " << fileInfos[i].url << endl;
        }
        cout << "========================================" << endl;
        cout << "[0] 取消" << endl;
        cout << "请选择文件链接 (0-" << fileInfos.size() << "): ";
        int choice;
        cin >> choice;
        if (choice == 0) {
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[信息] 用户取消选择" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        if (choice < 1 || choice >(int)fileInfos.size()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 无效选择" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        selectedUrl = fileInfos[choice - 1].url;
        selectedName = fileInfos[choice - 1].displayName;
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[信息] 已选择实际文件: " << selectedUrl << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return true;
    }

    void SaveLog(const string& query, const string& htmlContent) {
        if (selectedUrl.empty()) {
            SetConsoleTextAttribute(hConsole, L_warning);
            cout << "[警告] 没有选择的 URL，0x00030" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            selectedUrl = "";
        }
        string cacheDir = cd + "\\Cache\\D-\\";
        if (_mkdir(cacheDir.c_str()) != 0 && errno != EEXIST) {
            SetConsoleTextAttribute(hConsole, L_warning);
            cout << "[警告] 无法创建 Cache 目录" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        string dateStr = GetCurrentDateString();
        string timeStr = GetCurrentTimeString();
        replace(timeStr.begin(), timeStr.end(), ':', '-');
        replace(timeStr.begin(), timeStr.end(), ' ', '_');
        string logFileName = cacheDir + "download_" + dateStr + "_" + timeStr + ".log";
        ofstream logFile(logFileName, ios::out | ios::trunc);
        if (!logFile.is_open()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 无法打开日志文件: " << logFileName << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return;
        }
        logFile << "========================================" << endl;
        logFile << "Bing 下载链接搜索日志" << endl;
        logFile << "========================================" << endl;
        logFile << "时间: " << GetCurrentTimeString() << endl;
        logFile << "搜索关键词: " << query << endl;
        logFile << "选择的 URL: " << selectedUrl << endl;
        logFile << "========================================" << endl;
        logFile << endl;
        logFile << "================ 网页源代码开始 ================" << endl;
        logFile << htmlContent << endl;
        logFile << "================ 网页源代码结束 ================" << endl;
        logFile.close();
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[信息] 日志已保存到: " << logFileName << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }

    string GetSelectedUrl() const { return selectedUrl; }
    string GetSelectedName() const { return selectedName; }

private:
    void ExtractCookiesFromResponse(HINTERNET hRequest) {
        DWORD dwSize = 0;
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, NULL, NULL, &dwSize, NULL) && dwSize > 0) {
            vector<wchar_t> buffer(dwSize / sizeof(wchar_t) + 1);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, NULL, buffer.data(), &dwSize, NULL)) {
                wstring headers(buffer.data());
                size_t pos = 0;
                while ((pos = headers.find(L"Set-Cookie:", pos)) != wstring::npos) {
                    size_t end = headers.find(L"\r\n", pos);
                    if (end != string::npos) {
                        wstring cookieLine = headers.substr(pos + 11, end - pos - 11);
                        size_t eqPos = cookieLine.find(L'=');
                        if (eqPos != wstring::npos) {
                            wstring key = cookieLine.substr(0, eqPos);
                            size_t semicolon = cookieLine.find(L';');
                            wstring value;
                            if (semicolon != wstring::npos) value = cookieLine.substr(eqPos + 1, semicolon - eqPos - 1);
                            else value = cookieLine.substr(eqPos + 1);
                            key.erase(0, key.find_first_not_of(L" \t"));
                            key.erase(key.find_last_not_of(L" \t") + 1);
                            value.erase(0, value.find_first_not_of(L" \t"));
                            value.erase(value.find_last_not_of(L" \t") + 1);
                            cookies[key] = value;
                        }
                        pos = end + 2;
                    }
                    else break;
                }
            }
        }
    }

    string ReadResponse(HINTERNET hRequest) {
        string response;
        DWORD bytesRead = 0;
        char buffer[8192];
        while (WinHttpReadData(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            response += buffer;
        }
        return response;
    }

    void ConstructDefaultCookies() {
        string randomValue = GenerateRandomString(16);
        string cvid = GenerateCVID();
        cookies[L"SRCHD"] = L"AF=NOFORM";
        cookies[L"SRCHUID"] = L"V=2&GUID=" + wstring(randomValue.begin(), randomValue.end()) + L"&dmnchg=1";
        cookies[L"SRCHUSR"] = L"DOB=20260823";
        cookies[L"_EDGE_S"] = L"M=1&S=1&";
        cookies[L"_EDGE_V"] = L"1";
        cookies[L"MUID"] = wstring(randomValue.begin(), randomValue.end());
        cookies[L"MUIDB"] = wstring(randomValue.begin(), randomValue.end());
        cookies[L"_SS"] = L"S=1";
        cookies[L"SRCHHPGUSR"] = L"SRCHLANG=zh-Hans";
        cookies[L"cvid"] = wstring(cvid.begin(), cvid.end());
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[信息] 构造了 " << cookies.size() << " 个默认 Cookie" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }

    wstring BuildCookieString() {
        wstring cookieStr;
        for (const auto& pair : cookies) {
            if (!cookieStr.empty()) cookieStr += L"; ";
            cookieStr += pair.first + L"=" + pair.second;
        }
        return cookieStr;
    }

    bool IsValidDownloadLink(const string& link) {
        vector<string> blacklist = { "bing.com", "microsoft.com", "go.microsoft.com", "google.com", "gstatic.com", "facebook.com", "twitter.com", "youtube.com", "googlesyndication.com", "doubleclick.net", "baidu.com", "zhihu.com" };
        string lowerLink = link;
        transform(lowerLink.begin(), lowerLink.end(), lowerLink.begin(), ::tolower);
        for (const auto& domain : blacklist) { if (lowerLink.find(domain) != string::npos) return false; }
        vector<string> extensions = { ".exe", ".msi", ".zip", ".dmg", ".pkg", ".deb", ".rpm", ".apk", ".tar.gz", ".tgz", ".7z", ".rar", ".jar", ".iso", ".bin", ".xayx", ".xbyx", ".xcyx", ".xzyx" };
        for (const auto& ext : extensions) { if (lowerLink.find(ext) != string::npos) return true; }
        vector<string> keywords = { "download", "setup", "install", "release", "latest", "version", "stable", "portable", "server", "下载", "安装" };
        for (const auto& keyword : keywords) { if (lowerLink.find(keyword) != string::npos) return true; }
        return false;
    }
};

// ==================== 下载函数 ====================

class DownloadCallback : public IBindStatusCallback {
public:
    DownloadCallback() : m_refCount(1), m_lastPercent(-1), m_downloadComplete(false) {}
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IBindStatusCallback) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    virtual ULONG STDMETHODCALLTYPE AddRef() { return InterlockedIncrement(&m_refCount); }
    virtual ULONG STDMETHODCALLTYPE Release() { LONG ref = InterlockedDecrement(&m_refCount); if (ref == 0) delete this; return ref; }
    virtual HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD dwReserved, IBinding* pib) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE GetPriority(LONG* pnPriority) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE OnLowResource(DWORD dwReserved) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE OnProgress(ULONG ulProgress, ULONG ulProgressMax, ULONG ulStatusCode, LPCWSTR szStatusText) {
        if (ulProgressMax > 0) {
            int percent = (int)((ulProgress * 100) / ulProgressMax);
            if (percent != m_lastPercent) { m_lastPercent = percent; cout << "\r下载进度: " << percent << "%" << flush; }
        }
        return S_OK;
    }
    virtual HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT hresult, LPCWSTR szError) { m_downloadComplete = true; cout << "\r下载完成!                  " << endl; return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD* grfBINDF, BINDINFO* pbindinfo) { *grfBINDF = BINDF_ASYNCHRONOUS | BINDF_ASYNCSTORAGE | BINDF_PULLDATA; return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD grfBSCF, DWORD dwSize, FORMATETC* pformatetc, STGMEDIUM* pstgmed) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID riid, IUnknown* punk) { return S_OK; }
    bool IsComplete() const { return m_downloadComplete; }

private:
    LONG m_refCount;
    int m_lastPercent;
    bool m_downloadComplete;
};

long long GetFileSizeFromURL(const string& url) {
    HINTERNET hSession = InternetOpenA("LightDownloader/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hSession) return -1;
    HINTERNET hConnect = InternetOpenUrlA(hSession, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hConnect) { InternetCloseHandle(hSession); return -1; }
    char buffer[64];
    DWORD len = 64;
    long long fileSize = -1;
    if (HttpQueryInfoA(hConnect, HTTP_QUERY_CONTENT_LENGTH, buffer, &len, NULL)) fileSize = _atoi64(buffer);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hSession);
    return fileSize;
}

bool DownloadChunk(const string& url, const string& savePath, long long start, long long end, atomic<long long>& totalDownloaded) {
    HINTERNET hSession = InternetOpenA("LightDownloader/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hSession) return false;
    char rangeHeader[128];
    sprintf_s(rangeHeader, "Range: bytes=%lld-%lld", start, end);
    HINTERNET hConnect = InternetOpenUrlA(hSession, url.c_str(), rangeHeader, (DWORD)strlen(rangeHeader), INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hConnect) { InternetCloseHandle(hSession); return false; }
    HANDLE hFile = CreateFileA(savePath.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { InternetCloseHandle(hConnect); InternetCloseHandle(hSession); return false; }
    LARGE_INTEGER li;
    li.QuadPart = start;
    SetFilePointerEx(hFile, li, NULL, FILE_BEGIN);
    DWORD bytesRead = 0;
    char buffer[8192];
    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        DWORD bytesWritten = 0;
        WriteFile(hFile, buffer, bytesRead, &bytesWritten, NULL);
        totalDownloaded += bytesRead;
    }
    CloseHandle(hFile);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hSession);
    return true;
}

bool DownloadFileWithProgressSingle(const string& url, const string& savePath) {
    DownloadCallback* callback = new DownloadCallback();
    HRESULT hr = URLDownloadToFileA(NULL, url.c_str(), savePath.c_str(), 0, callback);
    int waitTime = 0;
    while (!callback->IsComplete() && waitTime < 600) { Sleep(100); waitTime++; }
    callback->Release();
    return SUCCEEDED(hr) && callback->IsComplete();
}

bool DownloadFileWithProgressMulti(const string& url, const string& savePath) {
    long long fileSize = GetFileSizeFromURL(url);
    if (fileSize <= 0) return DownloadFileWithProgressSingle(url, savePath);
    HANDLE hFile = CreateFileA(savePath.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    CloseHandle(hFile);
    int threadCount = 4;
    if (fileSize < 10 * 1024 * 1024) threadCount = 2;
    else if (fileSize < 50 * 1024 * 1024) threadCount = 4;
    else threadCount = 8;
    long long chunkSize = fileSize / threadCount;
    vector<thread> threads;
    atomic<long long> totalDownloaded(0);
    for (int i = 0; i < threadCount; i++) {
        long long start = i * chunkSize;
        long long end = (i == threadCount - 1) ? fileSize - 1 : (i + 1) * chunkSize - 1;
        threads.push_back(thread([&, start, end]() { DownloadChunk(url, savePath, start, end, ref(totalDownloaded)); }));
    }
    int lastPercent = -1;
    while (true) {
        long long current = totalDownloaded.load();
        if (current > fileSize) current = fileSize;
        int currentPercent = (int)((current * 100) / fileSize);
        if (currentPercent > 100) currentPercent = 100;
        if (currentPercent != lastPercent) { lastPercent = currentPercent; cout << "\r下载进度: " << currentPercent << "%" << flush; }
        if (currentPercent >= 100) break;
        Sleep(500);
    }
    for (auto& t : threads) { if (t.joinable()) t.join(); }
    cout << "\r下载完成!                  " << endl;
    return true;
}

bool DownloadFileWithProgress(const string& url, const string& savePath) {
    return DownloadFileWithProgressMulti(url, savePath);
}

// ============================================================
// 用 WinHTTP 下载文件到内存，成功后一次性写盘
//   - 内存中完成下载，一次性写入，避免产生半成品文件
//   - 自动跟随 302 重定向
//   - 用 Edge 的 User-Agent
//
// 注意：不依赖 URL_COMPONENTS / WinHttpCrackUrl，
//       因为它们随 UNICODE 宏切换 A/W，和 WinHttpConnect 的 LPCWSTR 不兼容。
//       这里手动解析 URL，全程宽字符，最稳。
// ============================================================
static bool DownloadFileWinHTTP(const std::wstring& url,
    const std::wstring& outPath) {
    // ---- 1. 手动解析 URL ----
    std::wstring u = url;
    bool https = true;
    size_t pos = 0;
    if (u.rfind(L"https://", 0) == 0) { https = true;  pos = 8; }
    else if (u.rfind(L"http://", 0) == 0) { https = false; pos = 7; }
    else {
        OutputDebugStringW(L"[Light] URL 协议不支持\n");
        return false;
    }

    size_t slash = u.find(L'/', pos);
    std::wstring hostPort = (slash == std::wstring::npos)
        ? u.substr(pos) : u.substr(pos, slash - pos);
    std::wstring path = (slash == std::wstring::npos)
        ? L"/" : u.substr(slash);

    std::wstring host = hostPort;
    INTERNET_PORT port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    size_t colon = hostPort.find(L':');
    if (colon != std::wstring::npos) {
        host = hostPort.substr(0, colon);
        std::wstring portStr = hostPort.substr(colon + 1);
        port = (INTERNET_PORT)wcstol(portStr.c_str(), nullptr, 10);
    }

    {
        wchar_t dbg[1024];
        swprintf_s(dbg, L"[Light] WinHTTP host=%s port=%u path=%s\n",
            host.c_str(), (unsigned)port, path.c_str());
        OutputDebugStringW(dbg);
    }

    // ---- 2. 打开会话 ----
    HINTERNET hSession = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        L"AppleWebKit/537.36 (KHTML, like Gecko) "
        L"Chrome/120.0.0.0 Safari/537.36 Edg/120.0.0.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        OutputDebugStringW(L"[Light] WinHttpOpen 失败\n");
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        wchar_t dbg[256];
        swprintf_s(dbg, L"[Light] WinHttpConnect 失败: %u\n", GetLastError());
        OutputDebugStringW(dbg);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // ---- 3. 创建请求 ----
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        OutputDebugStringW(L"[Light] WinHttpOpenRequest 失败\n");
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD optRedirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY,
        &optRedirect, sizeof(optRedirect));

    DWORD timeout = 30000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

    // ---- 4. 发送 / 接收 ----
    if (!WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(hRequest, NULL)) {
        OutputDebugStringW(L"[Light] 请求发送/接收失败\n");
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
        WINHTTP_NO_HEADER_INDEX);

    if (statusCode < 200 || statusCode >= 300) {
        wchar_t dbg[128];
        swprintf_s(dbg, L"[Light] HTTP 状态码错误: %u\n", statusCode);
        OutputDebugStringW(dbg);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // ---- 5. 下载到内存 buffer ----
    std::vector<BYTE> buffer;
    buffer.reserve(1024 * 1024);

    DWORD bytesRead = 0;
    BYTE chunk[64 * 1024];
    while (WinHttpReadData(hRequest, chunk, sizeof(chunk), &bytesRead)
        && bytesRead > 0) {
        buffer.insert(buffer.end(), chunk, chunk + bytesRead);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (buffer.empty()) {
        OutputDebugStringW(L"[Light] 下载内容为空\n");
        return false;
    }

    // ---- 6. 一次性写入文件 ----
    std::wstring dir = outPath;
    size_t s = dir.find_last_of(L"\\/");
    if (s != std::wstring::npos) {
        dir = dir.substr(0, s);
        SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    }

    HANDLE hFile = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        wchar_t dbg[512];
        swprintf_s(dbg, L"[Light] 无法创建文件: %s (err=%u)\n",
            outPath.c_str(), GetLastError());
        OutputDebugStringW(dbg);
        return false;
    }

    DWORD totalWritten = 0;
    BOOL ok = WriteFile(hFile, buffer.data(),
        (DWORD)buffer.size(), &totalWritten, NULL);
    CloseHandle(hFile);

    if (!ok || totalWritten != (DWORD)buffer.size()) {
        DeleteFileW(outPath.c_str());
        OutputDebugStringW(L"[Light] 写入不完整\n");
        return false;
    }

    {
        wchar_t dbg[512];
        swprintf_s(dbg, L"[Light] 下载完成，写入 %u 字节 -> %s\n",
            totalWritten, outPath.c_str());
        OutputDebugStringW(dbg);
    }
    return true;
}

// ============================================================
// dll 桥接下载：在后台线程下载，完成后回调 IGuiProvider
//   url     : 源 URL（UTF-8）
//   outPath : 目标路径（UTF-8）
// ============================================================
static void DownloadForPlugin(const std::string& url,
    const std::string& outPath) {
    std::string outPathCopy = outPath;   // 捕获副本，供回调使用

    std::thread([url, outPath, outPathCopy]() {
        auto toW = [](const std::string& s) -> std::wstring {
            if (s.empty()) return L"";
            int len = MultiByteToWideChar(CP_UTF8, 0,
                s.c_str(), (int)s.size(), NULL, 0);
            std::wstring w(len, 0);
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                &w[0], len);
            return w;
            };

        std::wstring wUrl = toW(url);
        std::wstring wOut = toW(outPath);

        OutputDebugStringW(L"[Light] DownloadForPlugin 开始\n");
        OutputDebugStringW(wUrl.c_str());
        OutputDebugStringW(L"\n");

        bool ok = DownloadFileWinHTTP(wUrl, wOut);

        OutputDebugStringW(ok
            ? L"[Light] DownloadForPlugin 下载成功\n"
            : L"[Light] DownloadForPlugin 下载失败\n");

        // 回调 dll
        if (g_guiProvider) {
            g_guiProvider->OnDownloadCompleted(outPathCopy, ok);
        }
        else {
            OutputDebugStringW(L"[Light] g_guiProvider 为空，无法回调\n");
        }
        }).detach();
}

// ==================== 安全核心类 ====================

class SecurityCore {
private:
    vector<string> riskDomains;
    mutex mtx;
    atomic<bool> isUpdating{ false };
    string securityFile;
    string cachePath;
    string debugPath;

public:
    SecurityCore() {
        string cd = GetCurrentProgramPath();
        cachePath = cd + "\\Cache\\security\\";
        debugPath = cachePath + "Debug\\";
        securityFile = cachePath + "security.xayx";
        CreateDirectoryA(cachePath.c_str(), NULL);
        CreateDirectoryA(debugPath.c_str(), NULL);
        LoadSecurityDatabase();
    }

    void LoadSecurityDatabase() {
        lock_guard<mutex> lock(mtx);
        if (FileExists(securityFile)) {
            string data = ReadRegistryFile(securityFile);
            if (!data.empty()) {
                try {
                    string jsonData = XorEncryptDecrypt(data, PASSWORD);
                    stringstream ss(jsonData);
                    string line;
                    riskDomains.clear();
                    while (getline(ss, line)) {
                        if (!line.empty() && line[0] != '#') riskDomains.push_back(line);
                    }
                }
                catch (const exception& e) {
                    SetConsoleTextAttribute(hConsole, L_error);
                    cout << "[安全核心] 解析安全文件失败: " << e.what() << endl;
                    SetConsoleTextAttribute(hConsole, L_default);
                }
            }
        }
    }

    bool IsUrlSafe(const string& url) {
        lock_guard<mutex> lock(mtx);
        string domain = url;
        if (domain.find("http://") == 0) domain = domain.substr(7);
        else if (domain.find("https://") == 0) domain = domain.substr(8);
        size_t slashPos = domain.find('/');
        if (slashPos != string::npos) domain = domain.substr(0, slashPos);
        if (domain.find("www.") == 0) domain = domain.substr(4);

        for (const auto& blockedDomain : riskDomains) {
            if (blockedDomain.empty()) continue;
            if (domain == blockedDomain) return false;
            if (domain.find("." + blockedDomain) != string::npos) return false;
            if (blockedDomain[0] == '.' && domain.find(blockedDomain) != string::npos) return false;
        }
        return true;
    }

    bool CheckUrlSafety(const string& url, bool isUserSpecified = false) {
        if (isUserSpecified) {
            bool safe = IsUrlSafe(url);
            if (!safe) {
                SetConsoleTextAttribute(hConsole, L_warning);
                cout << "[安全警告] 您正在访问的URL可能不安全: " << url << endl;
                cout << "[安全警告] 请确认是否继续 (y/n): ";
                SetConsoleTextAttribute(hConsole, L_default);
                string choice;
                getline(cin, choice);
                if (choice != "y" && choice != "Y") {
                    LogBlockedUrl(url + " (用户拒绝)");
                    return false;
                }
                LogUserBypass(url);
                return true;
            }
            return true;
        }
        bool safe = IsUrlSafe(url);
        if (!safe) {
            cout << endl;
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[安全核心] 拦截风险网址: " << url << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            LogBlockedUrl(url);
            if (g_NetworkInterceptor) {
                string domain = url;
                if (domain.find("http://") == 0) domain = domain.substr(7);
                else if (domain.find("https://") == 0) domain = domain.substr(8);
                size_t pos = domain.find('/');
                if (pos != string::npos) domain = domain.substr(0, pos);
                g_NetworkInterceptor->BlockDomain(domain, 60000);
            }
            if (g_NotifyWindow) {
                g_NotifyWindow->ShowBlockNotification("Light拦截", url);
            }
        }
        return safe;
    }

    void LogBlockedUrl(const string& url) {
        string logPath = cachePath + "blocked.log";
        ofstream log(logPath, ios::app);
        if (log.is_open()) {
            log << GetCurrentTimeString() << " - 拦截: " << url << endl;
            log.close();
        }
    }

    void LogUserBypass(const string& url) {
        string logPath = cachePath + "user_bypass.log";
        ofstream log(logPath, ios::app);
        if (log.is_open()) {
            log << GetCurrentTimeString() << " - 用户允许: " << url << endl;
            log.close();
        }
    }

    bool ExtractZipWithMinizip(const string& zipPath, const string& destDir) {
        CreateDirectoryA(destDir.c_str(), NULL);
        unzFile zipFile = unzOpen(zipPath.c_str());
        if (!zipFile) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[安全更新] 无法打开 ZIP 文件: " << zipPath << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        unz_global_info globalInfo;
        if (unzGetGlobalInfo(zipFile, &globalInfo) != UNZ_OK) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[安全更新] 获取 ZIP 信息失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            unzClose(zipFile);
            return false;
        }
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[安全更新] ZIP 包含 " << globalInfo.number_entry << " 个文件" << endl;
        char filename[256];
        unz_file_info fileInfo;
        if (unzGoToFirstFile(zipFile) != UNZ_OK) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[安全更新] 定位到第一个文件失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            unzClose(zipFile);
            return false;
        }
        bool success = true;
        int fileCount = 0;
        do {
            if (unzGetCurrentFileInfo(zipFile, &fileInfo, filename, sizeof(filename), NULL, 0, NULL, 0) != UNZ_OK) {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "[安全更新] 获取文件信息失败" << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                success = false;
                break;
            }
            bool isDirectory = (filename[strlen(filename) - 1] == '/');
            if (isDirectory) {
                string dirPath = destDir + "\\" + string(filename);
                CreateDirectoryA(dirPath.c_str(), NULL);
            }
            else {
                string filePath = destDir + "\\" + string(filename);
                size_t lastSlash = filePath.find_last_of("\\/");
                if (lastSlash != string::npos) { string parentDir = filePath.substr(0, lastSlash); CreateDirectoryA(parentDir.c_str(), NULL); }
                if (unzOpenCurrentFile(zipFile) != UNZ_OK) {
                    SetConsoleTextAttribute(hConsole, L_error);
                    cout << "[安全更新] 打开文件失败: " << filename << endl;
                    SetConsoleTextAttribute(hConsole, L_default);
                    success = false;
                    break;
                }
                ofstream outFile(filePath, ios::binary);
                if (!outFile.is_open()) {
                    SetConsoleTextAttribute(hConsole, L_error);
                    cout << "[安全更新] 创建文件失败: " << filePath << endl;
                    SetConsoleTextAttribute(hConsole, L_default);
                    unzCloseCurrentFile(zipFile);
                    success = false;
                    break;
                }
                char buffer[8192];
                int bytesRead;
                do {
                    bytesRead = unzReadCurrentFile(zipFile, buffer, sizeof(buffer));
                    if (bytesRead > 0) outFile.write(buffer, bytesRead);
                } while (bytesRead > 0);
                outFile.close();
                unzCloseCurrentFile(zipFile);
                fileCount++;
                if (fileCount % 10 == 0) {
                    SetConsoleTextAttribute(hConsole, L_default);
                    cout << "[安全更新] 已解压 " << fileCount << " 个文件" << endl;
                }
            }
        } while (unzGoToNextFile(zipFile) == UNZ_OK);
        unzClose(zipFile);
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[安全更新] 解压完成，共 " << fileCount << " 个文件" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return success;
    }

    bool UpdateFromGitHub() {
        if (isUpdating) return false;
        isUpdating = true;
        bool result = false;
        try {
            SetConsoleTextAttribute(hConsole, L_default);
            cout << endl;
            cout << "[安全更新] 更新..." << endl;
            string securityNe = GetSecurityNeValue();
            string rawUrl = "https://raw.githubusercontent.com/yuxuan-gzs/Forge-NeoForge-and-Fabric/security/PM%20%E6%81%B6%E6%84%8F%E7%BD%91%E5%9D%80%E6%8B%A6%E6%88%AA%E5%BA%93Pro_V" + securityNe + ".zip";
            string zipPath = cachePath + "temp.zip";
            if (!DownloadFromGitHub(rawUrl, zipPath)) {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "[安全更新] 下载失败" << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                isUpdating = false;
                return false;
            }
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[安全更新] 下载完成，开始解压..." << endl;
            string extractDir = cachePath + "extracted\\";
            CreateDirectoryA(extractDir.c_str(), NULL);
            if (!ExtractZipWithMinizip(zipPath, extractDir)) {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "[安全更新] Minizip 解压失败" << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                isUpdating = false;
                return false;
            }
            string jsonContent;
            WIN32_FIND_DATAA findData;
            string searchPath = extractDir + "\\*";
            HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;
                    string filePath = extractDir + "\\" + findData.cFileName;
                    string fileName = findData.cFileName;
                    size_t dotPos = fileName.find_last_of(".");
                    if (dotPos != string::npos) {
                        string ext = fileName.substr(dotPos + 1);
                        if (ext == "xayx" || ext == "txt" || ext == "json") {
                            ifstream file(filePath, ios::binary);
                            if (file.is_open()) {
                                jsonContent = string((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
                                file.close();
                                SetConsoleTextAttribute(hConsole, L_default);
                                cout << "[安全更新] 读取文件: " << findData.cFileName << endl;
                                break;
                            }
                        }
                    }
                } while (FindNextFileA(hFind, &findData));
                FindClose(hFind);
            }
            if (jsonContent.empty()) {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "[安全更新] 解压后未找到有效文件" << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                isUpdating = false;
                return false;
            }
            string xayxPath = cachePath + "security.xayx";
            string encrypted = XorEncryptDecrypt(jsonContent, PASSWORD);
            ofstream xayxFile(xayxPath, ios::binary);
            if (!xayxFile.is_open()) {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "[安全更新] 无法创建 XAYX 文件" << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                isUpdating = false;
                return false;
            }
            xayxFile.write(encrypted.c_str(), encrypted.size());
            xayxFile.close();
            if (FileExists(zipPath)) remove(zipPath.c_str());
            Lock_RestoreFolderRecursively(extractDir);
            DeleteFilesInDirectory(extractDir);
            RemoveDirectoryA(extractDir.c_str());
            UpdateSecurityNeValue();
            LoadSecurityDatabase();
            SetConsoleTextAttribute(hConsole, L_success);
            cout << "[安全更新] 安全库更新成功！" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            result = true;
        }
        catch (const exception& e) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[安全更新] 异常: " << e.what() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        isUpdating = false;
        return result;
    }

    bool DownloadFromGitHub(const string& rawUrl, const string& savePath) {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[调试] 开始下载: " << rawUrl << endl;
        cout << "[调试] 保存路径: " << savePath << endl;
        string saveDir = savePath;
        size_t lastSlash = saveDir.find_last_of("\\/");
        if (lastSlash != string::npos) {
            saveDir = saveDir.substr(0, lastSlash);
            string tempPath;
            size_t pos = 0;
            while ((pos = saveDir.find_first_of("\\/", pos + 1)) != string::npos) {
                tempPath = saveDir.substr(0, pos);
                CreateDirectoryA(tempPath.c_str(), NULL);
            }
            CreateDirectoryA(saveDir.c_str(), NULL);
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[调试] 确保目录存在: " << saveDir << endl;
        }
        HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
        if (!hSession) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[调试] WinHttpOpen 失败，错误码: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        string host, path;
        if (rawUrl.find("https://") == 0) host = rawUrl.substr(8);
        else if (rawUrl.find("http://") == 0) host = rawUrl.substr(7);
        else {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[调试] URL 格式错误" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            WinHttpCloseHandle(hSession);
            return false;
        }
        size_t slashPos = host.find('/');
        if (slashPos != string::npos) { path = host.substr(slashPos); host = host.substr(0, slashPos); }
        else path = "/";
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[调试] 主机: " << host << endl;
        cout << "[调试] 路径: " << path << endl;
        HINTERNET hConnect = WinHttpConnect(hSession, wstring(host.begin(), host.end()).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[调试] WinHttpConnect 失败，错误码: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            WinHttpCloseHandle(hSession);
            return false;
        }
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wstring(path.begin(), path.end()).c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[调试] WinHttpOpenRequest 失败，错误码: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        wstring headers = L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\nAccept: application/octet-stream\r\nAccept-Encoding: identity\r\n";
        if (!WinHttpSendRequest(hRequest, headers.c_str(), headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[调试] WinHttpSendRequest 失败，错误码: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[调试] WinHttpReceiveResponse 失败，错误码: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &statusCode, &statusCodeSize, NULL);
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[调试] HTTP 状态码: " << statusCode << endl;
        if (statusCode != 200) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[调试] HTTP 错误，状态码: " << statusCode << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        DWORD dwAttrib = GetFileAttributesA(saveDir.c_str());
        if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
            CreateDirectoryA(saveDir.c_str(), NULL);
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[调试] 创建目录: " << saveDir << endl;
        }
        ofstream file(savePath, ios::binary);
        if (!file.is_open()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[调试] 无法创建文件: " << savePath << "，错误码: " << GetLastError() << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        DWORD bytesRead = 0;
        char buffer[8192];
        long long totalBytes = 0;
        while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            file.write(buffer, bytesRead);
            totalBytes += bytesRead;
        }
        file.close();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[调试] 下载完成，总字节数: " << totalBytes << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return totalBytes > 0;
    }

    void StartBackgroundUpdate() {
        thread([this]() { Sleep(1000); UpdateFromGitHub(); }).detach();
    }

    vector<string> GetRiskDomains() {
        lock_guard<mutex> lock(mtx);
        return riskDomains;
    }

    void ManualUpdate() {
        thread([this]() { UpdateFromGitHub(); }).detach();
    }

    void BlockUrlTemporarily(const string& url, uint64_t timeoutMs = 60000) {
        if (g_NetworkInterceptor) {
            g_NetworkInterceptor->BlockDomain(url, timeoutMs);
        }
        LogBlockedUrl(url + " (临时拦截)");
    }

    void UnblockUrl(const string& url) {
        if (g_NetworkInterceptor) {
            g_NetworkInterceptor->UnblockDomain(url);
        }
    }
};

std::vector<std::string> GetRiskDomains() {
    if (!g_SecurityCore) {
        return std::vector<std::string>();
    }
    return g_SecurityCore->GetRiskDomains();
}

// ==================== 安全加密工具类 ====================

class SecurityCrypto {
public:
    static void XorEncrypt(uint8_t* data, size_t len, const string& key, uint32_t salt) {
        for (size_t i = 0; i < len; i++) {
            data[i] ^= key[i % key.size()];
            data[i] ^= (uint8_t)(salt >> (i % 4) * 8);
            data[i] = ~data[i];
        }
    }

    static uint32_t SimpleHash(const uint8_t* data, size_t len) {
        uint32_t hash = 0x811C9DC5;
        for (size_t i = 0; i < len; i++) {
            hash ^= data[i];
            hash *= 0x01000193;
            hash ^= (hash >> 16);
        }
        return hash;
    }

    static uint32_t GenerateSalt() {
        srand((unsigned)time(NULL) ^ (unsigned)GetCurrentProcessId());
        return (uint32_t)rand() ^ (uint32_t)GetTickCount();
    }

    static void ObfuscateData(vector<uint8_t>& data, uint32_t salt) {
        if (data.size() < 10) return;
        size_t step = 10 + (salt % 20);
        vector<uint8_t> obfuscated;
        obfuscated.reserve(data.size() + data.size() / step + 10);
        for (size_t i = 0; i < data.size(); i++) {
            obfuscated.push_back(data[i]);
            if (i % step == 0 && i > 0) {
                obfuscated.push_back((uint8_t)(rand() % 256));
            }
        }
        data.swap(obfuscated);
    }

    static void DeobfuscateData(vector<uint8_t>& data, uint32_t salt) {
        if (data.size() < 10) return;
        size_t step = 10 + (salt % 20);
        vector<uint8_t> clean;
        clean.reserve(data.size());
        for (size_t i = 0; i < data.size(); i++) {
            if (i % (step + 1) == step && i > 0) continue;
            clean.push_back(data[i]);
        }
        data.swap(clean);
    }
};

// ==================== UltraCompressor 压缩类 ====================

class UltraCompressor {
private:
    static vector<uint8_t> DeltaEncode(const vector<uint8_t>& input) {
        if (input.size() < 2) return input;
        vector<uint8_t> output;
        output.reserve(input.size());
        output.push_back(input[0]);
        for (size_t i = 1; i < input.size(); i++) {
            int diff = (int)input[i] - (int)input[i - 1];
            output.push_back((uint8_t)(diff + 128));
        }
        return output;
    }

    static vector<uint8_t> DeltaDecode(const vector<uint8_t>& input) {
        if (input.empty()) return input;
        vector<uint8_t> output;
        output.reserve(input.size());
        output.push_back(input[0]);
        for (size_t i = 1; i < input.size(); i++) {
            int diff = (int)input[i] - 128;
            output.push_back((uint8_t)(output.back() + diff));
        }
        return output;
    }

    static vector<uint8_t> LZ77Tiny(const vector<uint8_t>& input) {
        if (input.size() < 8) return input;
        vector<uint8_t> output;
        output.reserve(input.size());
        size_t windowSize = 256;
        size_t minMatch = 3;
        size_t maxMatch = 63;
        size_t i = 0;
        while (i < input.size()) {
            int bestLen = 0;
            int bestDist = 0;
            int maxLook = min((size_t)maxMatch, input.size() - i);
            int start = max(0, (int)i - (int)windowSize);
            for (int j = start; j < (int)i; j++) {
                int len = 0;
                while (len < maxLook && j + len < (int)i && input[j + len] == input[i + len]) {
                    len++;
                }
                if (len > bestLen && len >= (int)minMatch) {
                    bestLen = len;
                    bestDist = i - j;
                }
            }
            if (bestLen >= (int)minMatch) {
                uint16_t code = 0x8000 | (bestLen << 8) | bestDist;
                output.push_back((uint8_t)(code >> 8));
                output.push_back((uint8_t)(code & 0xFF));
                i += bestLen;
            }
            else {
                output.push_back(input[i]);
                i++;
            }
        }
        return output;
    }

    static vector<uint8_t> LZ77TinyDecode(const vector<uint8_t>& input) {
        if (input.empty()) return input;
        vector<uint8_t> output;
        output.reserve(input.size() * 2);
        size_t i = 0;
        while (i < input.size()) {
            if (i + 1 < input.size() && (input[i] & 0x80)) {
                int len = (input[i] >> 2) & 0x3F;
                int dist = ((input[i] & 0x03) << 8) | input[i + 1];
                if (len >= 3 && dist > 0) {
                    int start = output.size() - dist;
                    for (int j = 0; j < len; j++) {
                        output.push_back(output[start + j]);
                    }
                    i += 2;
                    continue;
                }
            }
            output.push_back(input[i]);
            i++;
        }
        return output;
    }

    static vector<uint8_t> RLEEncode(const vector<uint8_t>& input) {
        if (input.empty()) return input;
        vector<uint8_t> output;
        output.reserve(input.size());
        size_t i = 0;
        while (i < input.size()) {
            uint8_t c = input[i];
            size_t count = 1;
            while (i + count < input.size() && input[i + count] == c && count < 255) count++;
            if (count > 2) {
                output.push_back(0xFE);
                output.push_back(c);
                output.push_back((uint8_t)count);
            }
            else {
                for (size_t j = 0; j < count; j++) output.push_back(c);
            }
            i += count;
        }
        return output;
    }

    static vector<uint8_t> RLEDecode(const vector<uint8_t>& input) {
        if (input.empty()) return input;
        vector<uint8_t> output;
        output.reserve(input.size());
        size_t i = 0;
        while (i < input.size()) {
            if (input[i] == 0xFE && i + 2 < input.size()) {
                uint8_t val = input[i + 1];
                uint8_t count = input[i + 2];
                output.insert(output.end(), count, val);
                i += 3;
            }
            else {
                output.push_back(input[i]);
                i++;
            }
        }
        return output;
    }

    struct HuffmanNode {
        int freq;
        uint8_t symbol;
        HuffmanNode* left;
        HuffmanNode* right;
        HuffmanNode(int f, uint8_t s = 0) : freq(f), symbol(s), left(nullptr), right(nullptr) {}
        ~HuffmanNode() { delete left; delete right; }
    };

    struct CompareNode {
        bool operator()(HuffmanNode* a, HuffmanNode* b) {
            return a->freq > b->freq;
        }
    };

    static HuffmanNode* BuildHuffmanTree(const vector<uint8_t>& data) {
        vector<int> freq(256, 0);
        for (uint8_t c : data) freq[c]++;
        priority_queue<HuffmanNode*, vector<HuffmanNode*>, CompareNode> pq;
        for (int i = 0; i < 256; i++) {
            if (freq[i] > 0) pq.push(new HuffmanNode(freq[i], i));
        }
        if (pq.empty()) return nullptr;
        if (pq.size() == 1) return pq.top();
        while (pq.size() > 1) {
            HuffmanNode* left = pq.top(); pq.pop();
            HuffmanNode* right = pq.top(); pq.pop();
            HuffmanNode* parent = new HuffmanNode(left->freq + right->freq);
            parent->left = left;
            parent->right = right;
            pq.push(parent);
        }
        return pq.top();
    }

    static void GenerateCodes(HuffmanNode* node, string code, vector<string>& codes) {
        if (!node) return;
        if (!node->left && !node->right) {
            codes[node->symbol] = code;
            return;
        }
        GenerateCodes(node->left, code + "0", codes);
        GenerateCodes(node->right, code + "1", codes);
    }

    static vector<uint8_t> HuffmanEncode(const vector<uint8_t>& input) {
        if (input.empty()) return input;
        HuffmanNode* root = BuildHuffmanTree(input);
        if (!root) return input;
        vector<string> codes(256);
        if (root->left || root->right) {
            GenerateCodes(root, "", codes);
        }
        else {
            codes[root->symbol] = "0";
        }
        vector<uint8_t> output;
        output.reserve(input.size() + 512);
        for (int i = 0; i < 256; i++) {
            uint8_t has = (codes[i].empty() ? 0 : 1);
            output.push_back(has);
            if (has) {
                uint8_t codeLen = (uint8_t)codes[i].size();
                output.push_back(codeLen);
                uint8_t byte = 0;
                for (size_t j = 0; j < codes[i].size(); j++) {
                    if (j > 0 && j % 8 == 0) {
                        output.push_back(byte);
                        byte = 0;
                    }
                    if (codes[i][j] == '1') byte |= (1 << (7 - (j % 8)));
                }
                if (codes[i].size() % 8 != 0) output.push_back(byte);
            }
        }
        bitset<8> bits;
        int bitPos = 0;
        for (uint8_t c : input) {
            for (char bit : codes[c]) {
                if (bit == '1') bits.set(7 - bitPos);
                bitPos++;
                if (bitPos == 8) {
                    output.push_back((uint8_t)bits.to_ulong());
                    bits.reset();
                    bitPos = 0;
                }
            }
        }
        if (bitPos > 0) output.push_back((uint8_t)bits.to_ulong());
        delete root;
        return output;
    }

    static vector<uint8_t> HuffmanDecode(const vector<uint8_t>& input) {
        if (input.size() < 256) return input;
        vector<string> codes(256);
        size_t pos = 0;
        for (int i = 0; i < 256 && pos < input.size(); i++) {
            uint8_t has = input[pos++];
            if (has) {
                if (pos >= input.size()) break;
                uint8_t codeLen = input[pos++];
                string code;
                while (code.size() < codeLen) {
                    if (pos >= input.size()) break;
                    uint8_t byte = input[pos++];
                    for (int j = 7; j >= 0 && code.size() < codeLen; j--) {
                        code += (byte & (1 << j)) ? '1' : '0';
                    }
                }
                codes[i] = code;
            }
        }
        map<string, uint8_t> reverseMap;
        for (int i = 0; i < 256; i++) {
            if (!codes[i].empty()) reverseMap[codes[i]] = (uint8_t)i;
        }
        vector<uint8_t> output;
        string currentCode;
        while (pos < input.size()) {
            uint8_t byte = input[pos++];
            for (int j = 7; j >= 0; j--) {
                currentCode += (byte & (1 << j)) ? '1' : '0';
                auto it = reverseMap.find(currentCode);
                if (it != reverseMap.end()) {
                    output.push_back(it->second);
                    currentCode.clear();
                }
            }
        }
        return output;
    }

    static vector<uint8_t> BitPack(const vector<uint8_t>& input) {
        if (input.empty()) return input;
        int maxVal = 0;
        for (uint8_t c : input) if (c > maxVal) maxVal = c;
        int bitsNeeded = 0;
        while ((1 << bitsNeeded) <= maxVal) bitsNeeded++;
        if (bitsNeeded == 0) bitsNeeded = 1;
        if (bitsNeeded > 8) bitsNeeded = 8;
        vector<uint8_t> output;
        output.push_back((uint8_t)bitsNeeded);
        uint8_t byte = 0;
        int bitPos = 0;
        for (uint8_t c : input) {
            if (c >= (1 << bitsNeeded)) c = (1 << bitsNeeded) - 1;
            byte |= (c << bitPos);
            bitPos += bitsNeeded;
            if (bitPos >= 8) {
                output.push_back(byte);
                byte = 0;
                bitPos = 0;
            }
        }
        if (bitPos > 0) output.push_back(byte);
        return output;
    }

    static vector<uint8_t> BitUnpack(const vector<uint8_t>& input) {
        if (input.empty()) return input;
        int bitsNeeded = input[0];
        if (bitsNeeded == 0 || bitsNeeded > 8) bitsNeeded = 8;
        vector<uint8_t> output;
        size_t pos = 1;
        int bitPos = 0;
        uint8_t currentByte = 0;
        while (pos < input.size()) {
            if (bitPos == 0) currentByte = input[pos++];
            uint8_t val = currentByte & ((1 << bitsNeeded) - 1);
            output.push_back(val);
            currentByte >>= bitsNeeded;
            bitPos = (bitPos + bitsNeeded) % 8;
            if (bitPos == 0 && pos < input.size()) currentByte = input[pos++];
        }
        return output;
    }

public:
    static bool CompressUltra(const vector<uint8_t>& input, vector<uint8_t>& output) {
        if (input.empty()) { output.clear(); return true; }

        size_t origSize = input.size();
        vector<vector<uint8_t>> results;
        vector<size_t> sizes;

        vector<uint8_t> d1 = DeltaEncode(input);
        vector<uint8_t> d2 = LZ77Tiny(d1);
        vector<uint8_t> d3 = RLEEncode(d2);
        vector<uint8_t> d4 = HuffmanEncode(d3);
        vector<uint8_t> d5 = BitPack(d4);
        results.push_back(d5);
        sizes.push_back(d5.size());

        vector<uint8_t> e1 = LZ77Tiny(input);
        vector<uint8_t> e2 = RLEEncode(e1);
        vector<uint8_t> e3 = HuffmanEncode(e2);
        vector<uint8_t> e4 = BitPack(e3);
        results.push_back(e4);
        sizes.push_back(e4.size());

        vector<uint8_t> f1 = HuffmanEncode(input);
        vector<uint8_t> f2 = BitPack(f1);
        results.push_back(f2);
        sizes.push_back(f2.size());

        vector<uint8_t> g1 = RLEEncode(input);
        vector<uint8_t> g2 = HuffmanEncode(g1);
        vector<uint8_t> g3 = BitPack(g2);
        results.push_back(g3);
        sizes.push_back(g3.size());

        size_t bestIdx = 0;
        for (size_t i = 1; i < sizes.size(); i++) {
            if (sizes[i] < sizes[bestIdx]) bestIdx = i;
        }

        if (sizes[bestIdx] >= origSize) {
            output.clear();
            output.push_back(0xFF);
            output.insert(output.end(), input.begin(), input.end());
            return true;
        }

        output.clear();
        output.reserve(sizes[bestIdx] + 16);
        output.insert(output.end(), (uint8_t*)&origSize, (uint8_t*)&origSize + 8);
        output.push_back((uint8_t)bestIdx);
        output.insert(output.end(), results[bestIdx].begin(), results[bestIdx].end());

        return true;
    }

    static bool DecompressUltra(const vector<uint8_t>& input, vector<uint8_t>& output) {
        if (input.size() < 10) return false;

        size_t origSize = 0;
        memcpy(&origSize, input.data(), 8);
        uint8_t method = input[8];

        if (method == 0xFF) {
            output.assign(input.begin() + 9, input.end());
            output.resize(origSize);
            return true;
        }

        vector<uint8_t> data(input.begin() + 9, input.end());

        data = BitUnpack(data);
        data = HuffmanDecode(data);
        data = RLEDecode(data);

        switch (method) {
        case 0:
            data = LZ77TinyDecode(data);
            data = DeltaDecode(data);
            break;
        case 1:
            data = LZ77TinyDecode(data);
            break;
        case 2:
        case 3:
            break;
        default:
            return false;
        }

        data.resize(origSize);
        output = data;
        return true;
    }
};

// ==================== LZMA压缩类 ====================

class LZMACompressor {
private:
    static const size_t LZMA_DICT_SIZE = 1 << 24;

public:
    static bool CompressLZMA(const vector<uint8_t>& input, vector<uint8_t>& output) {
        if (input.empty()) { output.clear(); return true; }

        size_t srcLen = input.size();
        size_t propsSize = LZMA_PROPS_SIZE;
        vector<unsigned char> props(propsSize);
        size_t destLen = srcLen + srcLen / 3 + 1024 * 1024;
        vector<unsigned char> compressed(destLen);

        int lzmaResult = LzmaCompress(
            compressed.data(), &destLen,
            input.data(), srcLen,
            props.data(), &propsSize,
            9,
            LZMA_DICT_SIZE,
            3,
            0,
            2,
            273,
            1
        );

        if (lzmaResult != 0) {
            return false;
        }

        compressed.resize(destLen);
        output.clear();
        output.reserve(propsSize + destLen);
        output.insert(output.end(), props.begin(), props.end());
        output.insert(output.end(), compressed.begin(), compressed.end());

        return true;
    }

    static bool DecompressLZMA(const vector<uint8_t>& input, vector<uint8_t>& output, size_t origSize) {
        if (input.empty()) { output.clear(); return true; }
        if (origSize == 0) { output.clear(); return true; }
        if (input.size() < LZMA_PROPS_SIZE) return false;

        vector<unsigned char> props(input.begin(), input.begin() + LZMA_PROPS_SIZE);
        vector<unsigned char> compressed(input.begin() + LZMA_PROPS_SIZE, input.end());

        output.resize(origSize);
        size_t destLen = origSize;
        size_t srcLen = compressed.size();

        int lzmaResult = LzmaUncompress(
            output.data(), &destLen,
            compressed.data(), &srcLen,
            props.data(), LZMA_PROPS_SIZE
        );

        if (lzmaResult != 0) {
            return false;
        }

        output.resize(destLen);
        return true;
    }
};

// ==================== SecureCompressor类 ====================

class SecureCompressor {
public:
    static bool CompressSecure(const vector<uint8_t>& input, vector<uint8_t>& output) {
        if (input.empty()) { output.clear(); return true; }

        vector<uint8_t> compressed;
        if (!LZMACompressor::CompressLZMA(input, compressed)) {
            compressed = input;
        }

        if (compressed.size() >= input.size()) {
            output = input;
        }
        else {
            output = compressed;
        }
        return true;
    }

    static bool DecompressSecure(const vector<uint8_t>& input, vector<uint8_t>& output, size_t origSize) {
        if (input.empty()) { output.clear(); return true; }

        if (LZMACompressor::DecompressLZMA(input, output, origSize)) {
            return true;
        }

        output = input;
        output.resize(origSize);
        return true;
    }
};

// ==================== 压缩函数 ====================

bool CompressFolderToXZYXSecure(const string& inputPath, const string& outputPath, bool includeFolder) {
    DWORD dwAttrib = GetFileAttributesA(inputPath.c_str());
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[压缩] 路径不存在: " << inputPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }

    bool isFile = !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
    vector<pair<string, bool>> items;
    vector<uint8_t> rawData;
    uint64_t totalOrigSize = 0;
    uint32_t fileCount = 0;
    uint32_t dirCount = 0;

    if (isFile) {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[压缩] 压缩单个文件: " << inputPath << endl;
        string fileName = GetFileNameFromPath(inputPath);

        ifstream file(inputPath, ios::binary);
        if (!file.is_open()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[压缩] 无法读取文件: " << inputPath << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return false;
        }
        vector<uint8_t> content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        file.close();

        uint32_t totalItems = 1;
        rawData.insert(rawData.end(), (uint8_t*)&totalItems, (uint8_t*)&totalItems + 4);
        uint32_t nameLen = (uint32_t)fileName.size();
        rawData.insert(rawData.end(), (uint8_t*)&nameLen, (uint8_t*)&nameLen + 4);
        rawData.insert(rawData.end(), fileName.begin(), fileName.end());
        uint8_t isDir = 0;
        rawData.push_back(isDir);
        uint64_t size = content.size();
        rawData.insert(rawData.end(), (uint8_t*)&size, (uint8_t*)&size + 8);

        vector<uint8_t> compressedData;
        if (!SecureCompressor::CompressSecure(content, compressedData)) {
            compressedData = content;
        }
        uint64_t compSize = compressedData.size();
        rawData.insert(rawData.end(), (uint8_t*)&compSize, (uint8_t*)&compSize + 8);
        rawData.insert(rawData.end(), compressedData.begin(), compressedData.end());

        totalOrigSize = content.size();
        fileCount = 1;
        dirCount = 0;
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[压缩] 文件: " << fileName << " (" << totalOrigSize << " 字节)" << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[压缩] 压缩文件夹: " << inputPath << endl;

        string folderName = GetFileNameFromPath(inputPath);

        function<void(const string&, const string&)> collectItems = [&](const string& basePath, const string& relativePath) {
            string searchPath = basePath + "\\*";
            WIN32_FIND_DATAA findData;
            HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
            if (hFind == INVALID_HANDLE_VALUE) return;
            do {
                if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;
                string fullPath = basePath + "\\" + findData.cFileName;
                string relPath = relativePath.empty() ? findData.cFileName : relativePath + "\\" + findData.cFileName;
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    items.push_back({ relPath, true });
                    collectItems(fullPath, relPath);
                }
                else {
                    items.push_back({ relPath, false });
                }
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
            };
        collectItems(inputPath, "");

        if (includeFolder) {
            items.insert(items.begin(), { folderName, true });
            dirCount++;
        }

        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[压缩] 找到 " << items.size() << " 个项目" << endl;

        uint32_t totalItems = (uint32_t)items.size();
        rawData.insert(rawData.end(), (uint8_t*)&totalItems, (uint8_t*)&totalItems + 4);

        for (const auto& item : items) {
            uint32_t nameLen = (uint32_t)item.first.size();
            rawData.insert(rawData.end(), (uint8_t*)&nameLen, (uint8_t*)&nameLen + 4);
            rawData.insert(rawData.end(), item.first.begin(), item.first.end());
            uint8_t isDir = item.second ? 1 : 0;
            rawData.push_back(isDir);

            if (!item.second) {
                string fullPath = inputPath + "\\" + item.first;
                ifstream file(fullPath, ios::binary);
                if (!file.is_open()) {
                    SetConsoleTextAttribute(hConsole, L_error);
                    cout << "[压缩] 无法读取: " << item.first << endl;
                    SetConsoleTextAttribute(hConsole, L_default);
                    continue;
                }
                vector<uint8_t> content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
                file.close();
                uint64_t size = content.size();
                rawData.insert(rawData.end(), (uint8_t*)&size, (uint8_t*)&size + 8);

                vector<uint8_t> compressedData;
                if (!SecureCompressor::CompressSecure(content, compressedData)) {
                    compressedData = content;
                }
                uint64_t compSize = compressedData.size();
                rawData.insert(rawData.end(), (uint8_t*)&compSize, (uint8_t*)&compSize + 8);
                rawData.insert(rawData.end(), compressedData.begin(), compressedData.end());
                totalOrigSize += content.size();
                fileCount++;
            }
            else {
                if (item.first != folderName || !includeFolder) {
                    dirCount++;
                }
                uint64_t zero = 0;
                rawData.insert(rawData.end(), (uint8_t*)&zero, (uint8_t*)&zero + 8);
                rawData.insert(rawData.end(), (uint8_t*)&zero, (uint8_t*)&zero + 8);
            }
        }
    }

    ofstream outFile(outputPath, ios::binary);
    if (!outFile.is_open()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[压缩] 无法创建输出文件: " << outputPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    outFile.write((char*)rawData.data(), rawData.size());
    outFile.close();

    SetConsoleTextAttribute(hConsole, L_success);
    cout << "[压缩] 完成!" << endl;
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "[压缩] 原始大小: " << totalOrigSize << " 字节" << endl;
    cout << "[压缩] 压缩后大小: " << rawData.size() << " 字节" << endl;
    if (totalOrigSize > 0) {
        cout << "[压缩] 压缩率: " << (rawData.size() * 100 / totalOrigSize) << "%" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
    return true;
}

// ==================== 解压函数 ====================

bool ExtractXZYXFileSecure(const string& filePath, const string& outputDir) {
    ifstream inFile(filePath, ios::binary);
    if (!inFile.is_open()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[解压] 无法打开文件: " << filePath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }

    vector<uint8_t> fileData((istreambuf_iterator<char>(inFile)), istreambuf_iterator<char>());
    inFile.close();

    size_t pos = 0;
    uint32_t totalItems = 0;
    if (fileData.size() < 4) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[解压] 无效的文件格式" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    memcpy(&totalItems, fileData.data() + pos, 4);
    pos += 4;

    CreateDirectoryA(outputDir.c_str(), NULL);

    for (uint32_t i = 0; i < totalItems && pos < fileData.size(); i++) {
        uint32_t nameLen = 0;
        memcpy(&nameLen, fileData.data() + pos, 4);
        pos += 4;
        string name((char*)fileData.data() + pos, nameLen);
        pos += nameLen;
        uint8_t isDir = fileData[pos++];
        uint64_t origSize = 0, compSize = 0;
        memcpy(&origSize, fileData.data() + pos, 8);
        pos += 8;
        memcpy(&compSize, fileData.data() + pos, 8);
        pos += 8;

        string fullPath = outputDir + "\\" + name;

        if (isDir) {
            CreateDirectoryA(fullPath.c_str(), NULL);
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[解压] 创建目录: " << name << endl;
        }
        else {
            size_t lastSlash = fullPath.find_last_of("\\/");
            if (lastSlash != string::npos) {
                string parentDir = fullPath.substr(0, lastSlash);
                CreateDirectoryA(parentDir.c_str(), NULL);
            }

            vector<uint8_t> compData(fileData.begin() + pos, fileData.begin() + pos + compSize);
            pos += compSize;

            vector<uint8_t> decompData;
            if (!SecureCompressor::DecompressSecure(compData, decompData, origSize)) {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "[解压] 解压失败: " << name << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                continue;
            }

            ofstream outFile(fullPath, ios::binary);
            outFile.write((char*)decompData.data(), decompData.size());
            outFile.close();
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[解压] " << name << " (" << decompData.size() << " 字节)" << endl;
        }
    }

    SetConsoleTextAttribute(hConsole, L_success);
    cout << "[解压] 完成!" << endl;
    SetConsoleTextAttribute(hConsole, L_default);
    return true;
}

// ==================== 树形图查看 ====================

bool ListXZYXContent(const string& filePath) {
    string resolvedPath = ResolvePath(filePath);

    ifstream inFile(resolvedPath, ios::binary);
    if (!inFile.is_open()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[错误] 无法打开文件: " << resolvedPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }

    vector<uint8_t> fileData((istreambuf_iterator<char>(inFile)), istreambuf_iterator<char>());
    inFile.close();

    SetConsoleTextAttribute(hConsole, L_default);
    cout << endl;
    cout << "╔══════════════════════════════════════════════════════════════╗" << endl;
    cout << "║              .xzyx 文件结构树形图                          ║" << endl;
    cout << "╚══════════════════════════════════════════════════════════════╝" << endl;
    cout << endl;
    cout << "📦 " << resolvedPath << endl;
    cout << "│" << endl;

    size_t pos = 0;
    uint32_t totalItems = 0;
    if (fileData.size() < 4) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[错误] 无效的文件格式" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return false;
    }
    memcpy(&totalItems, fileData.data() + pos, 4);
    pos += 4;

    struct ItemInfo { string name; bool isDir; uint64_t size; int depth; };
    vector<ItemInfo> items;
    int dirCount = 0, fileCount = 0;

    for (uint32_t i = 0; i < totalItems && pos < fileData.size(); i++) {
        uint32_t nameLen = 0;
        memcpy(&nameLen, fileData.data() + pos, 4);
        pos += 4;
        string name((char*)fileData.data() + pos, nameLen);
        pos += nameLen;
        uint8_t isDir = fileData[pos++];
        uint64_t origSize = 0, compSize = 0;
        memcpy(&origSize, fileData.data() + pos, 8);
        pos += 8;
        memcpy(&compSize, fileData.data() + pos, 8);
        pos += 8;
        pos += compSize;

        int depth = 0;
        for (char c : name) if (c == '\\') depth++;
        items.push_back({ name, (bool)isDir, origSize, depth });
        if (isDir) dirCount++; else fileCount++;
    }

    for (size_t i = 0; i < items.size(); i++) {
        const auto& item = items[i];
        string prefix;
        if (item.depth > 0) {
            for (int d = 0; d < item.depth; d++) {
                bool hasMore = false;
                for (size_t j = i + 1; j < items.size(); j++) {
                    if (items[j].depth == d) { hasMore = true; break; }
                }
                prefix += hasMore ? "│   " : "    ";
            }
        }
        string icon = item.isDir ? "📁" : "📄";
        string sizeStr = item.isDir ? "" : " (" + to_string(item.size) + " 字节)";
        bool isLastInParent = true;
        for (size_t j = i + 1; j < items.size(); j++) {
            if (items[j].depth == item.depth) { isLastInParent = false; break; }
            if (items[j].depth < item.depth) break;
        }
        if (item.depth == 0) {
            cout << (i == items.size() - 1 ? "└── " : "├── ") << icon << " " << item.name << sizeStr << endl;
        }
        else {
            cout << prefix << (isLastInParent ? "└── " : "├── ") << icon << " " << item.name << sizeStr << endl;
        }
    }

    SetConsoleTextAttribute(hConsole, L_default);
    cout << endl;
    cout << "═══════════════════════════════════════════════════════════════" << endl;
    cout << "  总计: " << items.size() << " 个项目 (文件: " << fileCount << ", 目录: " << dirCount << ")" << endl;
    cout << "═══════════════════════════════════════════════════════════════" << endl;
    SetConsoleTextAttribute(hConsole, L_default);

    return true;
}

// Implement wrapper functions for SecurityCore declared in SecurityCore.h
bool IsDomainInFixedBlacklist(const std::string& domain) {
    if (!g_SecurityCore) return false;
    return !g_SecurityCore->IsUrlSafe(domain);
}

bool IsUrlSafe(const std::string& url) {
    if (!g_SecurityCore) return true;
    return g_SecurityCore->IsUrlSafe(url);
}

void LogBlockedUrl(const std::string& url) {
    if (!g_SecurityCore) return;
    g_SecurityCore->LogBlockedUrl(url);
}

///////////////////////////////////////////////////////////
// ======================================================
// ==================== 辅助函数声明 ====================
// ======================================================
// ////////////////////////////////////////////////////////
// ///////////////////////////////////////////////////////////
// 
// ==================== 辅助函数实现 ====================

void HandleCdCommand(const string& command, string& cd) {
    string targetDir = command.substr(3);
    targetDir.erase(0, targetDir.find_first_not_of(" \t\r\n"));
    targetDir.erase(targetDir.find_last_not_of(" \t\r\n") + 1);

    if (targetDir.empty()) {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "当前目录: " << cd << endl;
        return;
    }

    string resolvedPath;
    if (targetDir == "..") {
        size_t lastSlash = cd.find_last_of("\\/");
        if (lastSlash != string::npos) {
            resolvedPath = cd.substr(0, lastSlash);
        }
        else {
            resolvedPath = cd;
        }
    }
    else if (targetDir == ".") {
        resolvedPath = cd;
    }
    else if (targetDir == "~") {
        char* homeDir = nullptr;
        size_t homeLen = 0;
        errno_t err = _dupenv_s(&homeDir, &homeLen, "USERPROFILE");
        if (err == 0 && homeDir != nullptr) {
            resolvedPath = string(homeDir);
            free(homeDir);
        }
        else {
            resolvedPath = cd;
        }
    }
    else {
        resolvedPath = ResolvePath(targetDir);
    }

    DWORD dwAttrib = GetFileAttributesA(resolvedPath.c_str());
    if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        cd = resolvedPath;
        SetConsoleTextAttribute(hConsole, L_success);
        cout << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "目录不存在或无法访问: " << resolvedPath << endl;
        printWithTilde(resolvedPath);
        cout << "                      " << cX << "^" << endl;
        cout << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
}

void HandleBlockCommand(const string& command) {
    string domain = command.substr(6);
    if (domain.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "用法: block <域名> [时间(秒)]" << endl;
        cout << "示例: block bad-site.com 60" << endl;
        cout << "提示: 临时黑名单重启后失效" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    uint64_t timeoutMs = 0;
    vector<string> parts = ParseCommandLine(domain);
    if (parts.size() >= 2) {
        domain = parts[0];
        int seconds = atoi(parts[1].c_str());
        if (seconds > 0) {
            timeoutMs = seconds * 1000;
        }
    }

    if (g_NetworkInterceptor) {
        if (g_NetworkInterceptor->IsWhitelisted(domain)) {
            SetConsoleTextAttribute(hConsole, L_warning);
            cout << "警告: " << domain << " 在白名单中，不会被拦截" << endl;
            cout << "如需拦截，请先从白名单移除" << endl;
        }
        else {
            g_NetworkInterceptor->BlockDomain(domain, timeoutMs);
            SetConsoleTextAttribute(hConsole, L_success);
            if (timeoutMs > 0) {
                cout << "已临时拦截: " << domain << " (" << timeoutMs / 1000 << "秒)" << endl;
            }
            else {
                cout << "已拦截: " << domain << " (直到程序退出)" << endl;
            }
            if (g_NotifyWindow) {
                g_NotifyWindow->ShowBlockNotification("System", domain);
                OutputDebugStringA("[Main] Test notification sent\n");
            }
        }
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "拦截器未初始化" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandleUnblockCommand(const string& command) {
    string domain = command.substr(8);
    if (domain.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "用法: unblock <域名>" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    if (g_NetworkInterceptor) {
        g_NetworkInterceptor->UnblockDomain(domain);
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "已取消拦截: " << domain << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "拦截器未初始化" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandleShowBlocksCommand() {
    if (g_NetworkInterceptor) {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "=== 临时黑名单 (命令行设置) ===" << endl;
        auto stats = g_NetworkInterceptor->GetStats();
        if (stats.blockedDomains.empty()) {
            cout << "(空)" << endl;
        }
        else {
            for (const auto& entry : stats.blockedDomains) {
                cout << "  ✗ " << entry.first << " (拦截 " << entry.second << " 次)" << endl;
            }
        }
        cout << "总计: " << stats.blockedDomains.size() << " 个域名" << endl;
        cout << "提示: 重启程序后黑名单自动清空" << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "拦截器未初始化" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandleClearBlocksCommand() {
    if (g_NetworkInterceptor) {
        g_NetworkInterceptor->ClearAllBlocks();
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "已清除所有临时黑名单规则" << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "拦截器未初始化" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandleWhitelistCommand(const string& command) {
    string domain = command.substr(10);
    if (domain.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "用法: whitelist <域名>" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    if (g_NetworkInterceptor) {
        g_NetworkInterceptor->AddWhitelistDomain(domain);
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "已添加到白名单: " << domain << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "拦截器未初始化" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandleUnwhitelistCommand(const string& command) {
    string domain = command.substr(12);
    if (domain.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "用法: unwhitelist <域名>" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    if (g_NetworkInterceptor) {
        g_NetworkInterceptor->RemoveWhitelistDomain(domain);
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "已从白名单移除: " << domain << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "拦截器未初始化" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandleCELCommand() {
    string cd = GetCurrentProgramPath();
    string exePath = cd + "\\rppcst\\CEL.exe";
    system(exePath.c_str());
}

void HandleUninstallCommand(const string& folderPath) {
    RestoreFolderPermission(folderPath);
    uninstall();
}

void HandleFPNPCTCommand() {
    string cd = GetCurrentProgramPath();
    string fullPortTool = cd + "\\rppcst\\Full_Port_Network_Packet_Capture_Tool.exe";
    system(fullPortTool.c_str());
}

void HandleAboutCommand() {
    about();
}

void HandleFillCommand(const string& cd, const string& registryPath) {
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "fill+" << endl;
    cout << "<" << cd << ":fill/> ";
    string registryData = ReadRegistryFile(registryPath);
    if (registryData.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "无法读取注册表文件" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    vector<string> path = { "HKEY_LOCAL_MACHINE", "xbyx" };
    string fillValue = FindValueInRegistry(registryData, path, "fill");
    if (fillValue == "air") {
        string deletePath;
        cin.clear();
        getline(cin, deletePath);
        DeleteFilesInDirectory(deletePath);
    }
    else if (fillValue.empty()) {
        SetConsoleTextAttribute(hConsole, L_warning);
        cout << "未找到 fill 值" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
    else {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "fill 当前值: " << fillValue << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
}

void HandleDumpCommand(const string& cd, const string& registryPath) {
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "=== 调试信息 ===" << endl;
    cout << "注册表文件路径: " << registryPath << endl;
    ifstream checkFile(registryPath, ios::binary);
    if (!checkFile.is_open()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误：文件不存在或无法访问！" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    checkFile.seekg(0, ios::end);
    size_t fileSize = checkFile.tellg();
    checkFile.close();
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "文件大小: " << fileSize << " 字节" << endl;
    if (fileSize == 0) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误：文件为空！" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    string data = ReadRegistryFile(registryPath);
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "解密后数据大小: " << data.size() << " 字节" << endl;
    if (!data.empty()) {
        if (data.find("NODE:") != string::npos) {
            SetConsoleTextAttribute(hConsole, L_success);
            cout << "✓ 数据格式正确，包含注册表节点信息" << endl;
        }
        else {
            SetConsoleTextAttribute(hConsole, L_warning);
            cout << "⚠ 警告：数据中未找到 'NODE:' 关键字" << endl;
        }
        SetConsoleTextAttribute(hConsole, L_default);
        string cacheDir = cd + "\\Cache";
        CreateDirectoryA(cacheDir.c_str(), NULL);
        string hexPath = cacheDir + "\\decrypted_hex.txt";
        ofstream hexOut(hexPath, ios::out | ios::trunc);
        if (hexOut.is_open()) {
            hexOut << "========================================" << endl;
            hexOut << "解密数据 - 十六进制 + ASCII 混合显示" << endl;
            hexOut << "========================================" << endl;
            hexOut << endl;
            stringstream ss(data);
            string line;
            int lineNum = 0;
            while (getline(ss, line)) {
                lineNum++;
                hexOut << "[" << setw(4) << lineNum << "] ";
                string displayLine = line;
                for (char& c : displayLine) {
                    if (c < 32 && c != '\t' && c != '\r' && c != '\n') c = ' ';
                }
                hexOut << displayLine << endl;
            }
            hexOut.close();
            SetConsoleTextAttribute(hConsole, L_success);
            cout << "✓ 已导出为可读格式: " << hexPath << endl;
        }
        string rawPath = cacheDir + "\\decrypted_raw.txt";
        ofstream rawOut(rawPath, ios::binary);
        if (rawOut.is_open()) {
            rawOut.write(data.c_str(), data.size());
            rawOut.close();
            SetConsoleTextAttribute(hConsole, L_success);
            cout << "✓ 已导出原始数据: " << rawPath << endl;
        }
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "请用记事本打开 decrypted_hex.txt 查看" << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "✗ 解密后数据为空！" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "=== 调试结束 ===" << endl;
}

void HandleLockCommand(const string& command) {
    string folderToLock = command.substr(5);
    if (folderToLock.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误：请指定要锁定的文件夹路径！" << endl;
        cout << "用法: Lock <文件夹路径>" << endl;
        cout << "示例: Lock C:\\Program Files\\.light" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    if (!IsRunningAsAdmin()) ElevateCurrentProgram();
    string resolvedPath = ResolvePath(folderToLock);
    DWORD dwAttrib = GetFileAttributesA(resolvedPath.c_str());
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误：文件夹不存在或无法访问！" << endl;
        cout << "路径: " << resolvedPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    if (!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误：指定路径不是文件夹！" << endl;
        cout << "路径: " << resolvedPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "准备锁定文件夹: " << resolvedPath << endl;
    cout << "确定继续(y/n): ";
    string confirm;
    getline(cin, confirm);
    if (confirm == "y" || confirm == "Y" || confirm == "yes" || confirm == "Yes") {
        StartSpinner();
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "正在设置权限..." << endl;
        EnableSecurityPrivileges();
        if (Lock_ProtectFolderRecursively(resolvedPath)) {
            StopSpinner();
            SetConsoleTextAttribute(hConsole, L_success);
            cout << "文件夹锁定成功" << endl;
            cout << "               : " << resolvedPath << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        else {
            StopSpinner();
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "文件夹锁定失败" << endl;
            cout << "               检查是否有其他程序正在使用该文件夹" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
    }
    else {
        SetConsoleTextAttribute(hConsole, L_warning);
        cout << "操作已取消" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
}

void HandleUnlockCommand(const string& command) {
    string folderToUnlock = command.substr(7);
    if (folderToUnlock.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误：请指定要解锁的文件夹路径！" << endl;
        cout << "用法: Unlock <文件夹路径>" << endl;
        cout << "示例: Unlock C:\\Program Files\\.light" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    if (!IsRunningAsAdmin()) ElevateCurrentProgram();
    string resolvedPath = ResolvePath(folderToUnlock);
    DWORD dwAttrib = GetFileAttributesA(resolvedPath.c_str());
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误：文件夹不存在或无法访问！" << endl;
        cout << "路径: " << resolvedPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "准备解锁文件夹: " << resolvedPath << endl;
    cout << "确定要继续吗？(y/n): ";
    string confirm;
    getline(cin, confirm);
    if (confirm == "y" || confirm == "Y" || confirm == "yes" || confirm == "Yes") {
        StartSpinner();
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "正在恢复权限..." << endl;
        EnableSecurityPrivileges();
        if (Lock_RestoreFolderRecursively(resolvedPath)) {
            StopSpinner();
            SetConsoleTextAttribute(hConsole, L_success);
            cout << "  文件夹解锁成功！" << endl;
            cout << "  路径: " << resolvedPath << endl;
            cout << "  已恢复访问权限" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        else {
            StopSpinner();
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "  .文件夹解锁失败." << endl;
            cout << "  请检查文件夹权限状态" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
    }
    else {
        SetConsoleTextAttribute(hConsole, L_warning);
        cout << "操作已取消." << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
}

void HandleRegistryCommand() {
    string cd = GetCurrentProgramPath();
    string Registry = cd + "\\Registry.exe";
    string userChoice;
    bool waitForCompletion = false;
    bool validInput = false;
    while (!validInput) {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "是否等待 Registry 关闭后再继续？(y/n): ";
        getline(cin, userChoice);
        userChoice.erase(0, userChoice.find_first_not_of(" \t\r\n"));
        userChoice.erase(userChoice.find_last_not_of(" \t\r\n") + 1);
        if (userChoice == "y" || userChoice == "Y" || userChoice == "yes" || userChoice == "YES" || userChoice == "是") {
            waitForCompletion = true;
            validInput = true;
        }
        else if (userChoice == "n" || userChoice == "N" || userChoice == "no" || userChoice == "NO" || userChoice == "否") {
            waitForCompletion = false;
            validInput = true;
        }
        else {
            SetConsoleTextAttribute(hConsole, L_warning);
            cout << "无效输入，请输入 y 或 n" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
    }
    if (LaunchProgram(Registry, waitForCompletion)) {
        SetConsoleTextAttribute(hConsole, L_success);
        if (waitForCompletion) cout << "Registry 已启动，等待关闭..." << endl;
        else cout << "Registry 已启动" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "启动 Registry 失败" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
}

void HandleBinaryHexEditorCommand() {
    string cd = GetCurrentProgramPath();
    string BinaryHexEditor = cd + "\\BinaryHexEditor.exe";
    string userChoice;
    bool waitForCompletion = false;
    bool validInput = false;
    while (!validInput) {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "是否等待 BinaryHexEditor 关闭后再继续？(y/n): ";
        getline(cin, userChoice);
        userChoice.erase(0, userChoice.find_first_not_of(" \t\r\n"));
        userChoice.erase(userChoice.find_last_not_of(" \t\r\n") + 1);
        if (userChoice == "y" || userChoice == "Y" || userChoice == "yes" || userChoice == "YES" || userChoice == "是") {
            waitForCompletion = true;
            validInput = true;
        }
        else if (userChoice == "n" || userChoice == "N" || userChoice == "no" || userChoice == "NO" || userChoice == "否") {
            waitForCompletion = false;
            validInput = true;
        }
        else {
            SetConsoleTextAttribute(hConsole, L_warning);
            cout << "无效输入，请输入 y 或 n" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
    }
    if (LaunchProgram(BinaryHexEditor, waitForCompletion)) {
        SetConsoleTextAttribute(hConsole, L_success);
        if (waitForCompletion) cout << "BinaryHexEditor 已启动，等待关闭..." << endl;
        else cout << "BinaryHexEditor 已启动" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "启动 BinaryHexEditor 失败" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
}

void HandleRestoreFolderPermissionCommand(const string& folderPath) {
    RestoreFolderPermission(folderPath);
}

void HandleCompressCommand(const string& command) {
    string params = command.substr(command.find(' ') + 1);
    vector<string> parts = ParseCommandLine(params);

    if (parts.empty() || parts[0] == "-h" || parts[0] == "--help") {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "用法: compress \"<文件或文件夹路径>\" [选项]" << endl;
        cout << "选项:" << endl;
        cout << "  -d <目录>    指定输出目录 (默认: Cache\\ysXzyxs)" << endl;
        cout << "  -c <名称>    指定输出文件名 (不含扩展名)" << endl;
        cout << "  -h, --help   显示帮助信息" << endl;
        cout << "示例:" << endl;
        cout << "  compress \"D:/MyFolder\"" << endl;
        cout << "  compress \"D:/MyFolder\" -d \"D:/Output\"" << endl;
        cout << "  compress \"D:/MyFolder\" -c \"MyArchive\"" << endl;
        cout << "  compress \"D:/MyFolder\" -d \"D:/Output\" -c \"MyArchive\"" << endl;
        return;
    }

    string inputPath = parts[0];
    string outputDir = "";
    string customName = "";
    bool hasD = false, hasC = false;

    for (size_t i = 1; i < parts.size(); i++) {
        if (parts[i] == "-d" && i + 1 < parts.size()) {
            outputDir = parts[++i];
            hasD = true;
        }
        else if (parts[i] == "-c" && i + 1 < parts.size()) {
            customName = parts[++i];
            hasC = true;
        }
    }

    string resolvedInput = ResolvePath(inputPath);
    DWORD dwAttrib = GetFileAttributesA(resolvedInput.c_str());
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[错误] 路径不存在: " << resolvedInput << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    bool isFile = !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
    string baseName = GetFileNameFromPath(resolvedInput);

    if (!hasC) {
        if (isFile) {
            customName = "新建压缩文件";
        }
        else {
            customName = baseName;
        }
    }

    if (!IsValidFileName(customName)) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[错误] 文件名包含非法字符: " << customName << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    string finalOutputDir;
    if (hasD) {
        finalOutputDir = ResolvePath(outputDir);
    }
    else {
        finalOutputDir = GetCacheXzyxPath();
    }

    CreateDirectoryA(finalOutputDir.c_str(), NULL);
    string outputFile = finalOutputDir + "\\" + customName + ".xzyx";

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "开始压缩: " << resolvedInput << " -> " << outputFile << endl;
    StartSpinner();
    if (CompressFolderToXZYXSecure(resolvedInput, outputFile, true)) {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "压缩成功!" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
    else {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "压缩失败!" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
}

void HandleExtractCommand(const string& command) {
    string params = command.substr(command.find(' ') + 1);
    vector<string> parts = ParseCommandLine(params);

    if (parts.empty() || parts[0] == "-h" || parts[0] == "--help") {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "用法: extract \"<文件.xzyx>\" [<输出文件夹>]" << endl;
        cout << "  <输出文件夹> 可选，默认为文件所在目录" << endl;
        cout << "示例:" << endl;
        cout << "  extract \"output.xzyx\"" << endl;
        cout << "  extract \"D:/Archive.xzyx\" \"D:/Extracted\"" << endl;
        return;
    }

    string xzyxFile = parts[0];
    string outDir;

    if (parts.size() >= 2) {
        outDir = parts[1];
    }
    else {
        string resolvedPath = ResolvePath(xzyxFile);
        string parentDir = GetParentDirPath(resolvedPath);
        string baseName = GetFileNameFromPath(resolvedPath);
        size_t dotPos = baseName.find_last_of('.');
        if (dotPos != string::npos) baseName = baseName.substr(0, dotPos);
        outDir = parentDir + "\\" + baseName + "_extracted";
    }

    string resolvedXzyx = ResolvePath(xzyxFile);
    string resolvedOutDir = ResolvePath(outDir);

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "开始解压: " << resolvedXzyx << " -> " << resolvedOutDir << endl;
    StartSpinner();
    if (ExtractXZYXFileSecure(resolvedXzyx, resolvedOutDir)) {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "解压成功!" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
    else {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "解压失败!" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
}

void HandleListCommand(const string& command) {
    string params = command.substr(command.find(' ') + 1);
    vector<string> parts = ParseCommandLine(params);
    if (parts.empty() || parts[0] == "-h" || parts[0] == "--help") {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "用法: list \"<文件.xzyx>\"" << endl;
        cout << "示例: list \"output.xzyx\"" << endl;
        return;
    }
    ListXZYXContent(parts[0]);
}

void HandleDownloadCommand(const string& command, SecurityCore* securityCore) {
    string params = command.substr(command.find(' ') + 1);
    params.erase(0, params.find_first_not_of(" \t\r\n"));
    params.erase(params.find_last_not_of(" \t\r\n") + 1);

    if (params.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误的命令参数：未指定 URL" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "用法: download <URL> [-s] [-d] [-b <目录>]" << endl;
        cout << "  (无参数)  直接下载" << endl;
        cout << "  -s        使用搜索下载" << endl;
        cout << "  -d        下载插件" << endl;
        cout << "  -b <目录> 指定下载保存目录" << endl;
        return;
    }

    string url = "";
    string saveDir = "";
    bool useSearch = false;
    bool usePlugin = false;

    vector<string> parts = ParseCommandLine(params);

    for (size_t i = 0; i < parts.size(); i++) {
        if (parts[i] == "-s") {
            useSearch = true;
        }
        else if (parts[i] == "-d") {
            usePlugin = true;
        }
        else if (parts[i] == "-b" && i + 1 < parts.size()) {
            saveDir = parts[i + 1];
            i++;
        }
    }

    if (useSearch && usePlugin) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误：-s 和 -d 参数不能同时使用" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    for (size_t i = 0; i < parts.size(); i++) {
        if (parts[i] != "-s" && parts[i] != "-d" && parts[i] != "-b" &&
            !(i > 0 && parts[i - 1] == "-b")) {
            url = parts[i];
            break;
        }
    }

    if (url.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误的命令参数：未指定 URL" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    string downloadFolder;
    if (!saveDir.empty()) {
        downloadFolder = ResolvePath(saveDir);
        DWORD dwAttrib = GetFileAttributesA(downloadFolder.c_str());
        if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
            if (!CreateDirectoryA(downloadFolder.c_str(), NULL)) {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "错误：无法创建目录 " << downloadFolder << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                return;
            }
        }
    }
    else {
        downloadFolder = GetDownloadFolderPath();
    }

    // 直接下载模式
    if (!useSearch && !usePlugin) {
        if (url.find("http://") != 0 && url.find("https://") != 0) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "错误：无效的 URL，请以 http:// 或 https:// 开头" << endl;
            cout << "提示：如需搜索下载，请使用 -s 参数" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return;
        }

        if (!securityCore->CheckUrlSafety(url, true)) {
            return;
        }

        string fileName = DeriveDownloadFileName(url);
        string savePath = downloadFolder + "\\" + fileName;

        SetConsoleTextAttribute(hConsole, L_default);
        cout << "开始下载: " << url << endl;
        cout << "保存到: " << savePath << endl;

        if (DownloadFileWithProgress(url, savePath)) {
            SetConsoleTextAttribute(hConsole, L_success);
            cout << "下载成功: " << savePath << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        else {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "下载失败: " << url << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        return;
    }
    // Bing 搜索模式
    else if (useSearch) {
        BingSearch bing;
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "[1/4] 初始化 WinHTTP..." << endl;
        if (!bing.Initialize()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 初始化失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return;
        }
        cout << "[2/4] 获取 Cookie..." << endl;
        if (!bing.GetInitialCookies()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 获取 Cookie 失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return;
        }
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[成功] Cookie 获取成功" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        cout << endl;
        cout << "[3/4] 搜索关键词: " << url << endl;
        cout << endl;
        cout << "[4/4] 正在搜索: " << url << endl;
        string html = bing.Search(url);
        if (html.empty()) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[错误] 搜索返回空结果" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return;
        }
        cout << endl;
        cout << "正在提取下载链接..." << endl;
        vector<string> pageUrls = bing.ExtractDownloadPageLinks(html);

        if (pageUrls.empty()) {
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "[信息] 未找到相关下载页面" << endl;
            return;
        }

        vector<string> safePageUrls;
        cout << "[信息] 找到 " << pageUrls.size() << " 个下载页面链接" << endl;
        for (size_t i = 0; i < pageUrls.size(); ++i) {
            if (!securityCore->CheckUrlSafety(pageUrls[i], false)) {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "  [" << (i + 1) << "] " << pageUrls[i] << " [已拦截并阻止]" << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                securityCore->LogBlockedUrl(pageUrls[i]);
            }
            else {
                SetConsoleTextAttribute(hConsole, L_default);
                cout << "  [" << (i + 1) << "] " << pageUrls[i] << endl;
                safePageUrls.push_back(pageUrls[i]);
            }
        }

        if (safePageUrls.empty()) {
            SetConsoleTextAttribute(hConsole, L_warning);
            cout << "[安全] 所有链接已被拦截，无安全链接可用" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            return;
        }
        cout << endl;

        if (bing.ParseAllDownloadPagesForFiles(safePageUrls)) {
            string finalUrl = bing.GetSelectedUrl();
            if (finalUrl.empty()) {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "[错误] 未选择有效的下载链接" << endl;
                SetConsoleTextAttribute(hConsole, L_default);
            }
            else if (!securityCore->CheckUrlSafety(finalUrl, false)) {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "[安全核心] 已阻止下载风险文件: " << finalUrl << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                securityCore->BlockUrlTemporarily(finalUrl, 60000);
            }
            else {
                cout << endl;
                SetConsoleTextAttribute(hConsole, L_default);
                cout << "[下载] : " << finalUrl << endl;
                // 无扩展名的下载入口页（如 /thank-you?dv=...）下载到的可能是网页，需提示
                if (!IsFileExtensionUrl(finalUrl)) {
                    SetConsoleTextAttribute(hConsole, L_warning);
                    cout << "[提示] 该链接为下载入口页（无文件扩展名），部分网站需在浏览器中打开才会开始下载" << endl;
                    SetConsoleTextAttribute(hConsole, L_default);
                }
                string finalFileName = DeriveDownloadFileName(finalUrl, bing.GetSelectedName());
                string finalSavePath = downloadFolder + "\\" + finalFileName;
                if (DownloadFileWithProgress(finalUrl, finalSavePath)) {
                    SetConsoleTextAttribute(hConsole, L_success);
                    cout << "下载成功: " << finalSavePath << endl;
                    SetConsoleTextAttribute(hConsole, L_default);
                }
                else {
                    SetConsoleTextAttribute(hConsole, L_error);
                    cout << "下载失败: " << finalUrl << endl;
                    SetConsoleTextAttribute(hConsole, L_default);
                }
            }
        }
        else {
            SetConsoleTextAttribute(hConsole, L_warning);
            cout << "[信息] 用户取消或解析失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        bing.SaveLog(url, html);
        return;
    }
    // 插件模式
    else if (usePlugin) {
        string cd = GetCurrentProgramPath();
        string savecj = cd + "\\Cache\\ModMigrator\\";
        string d_savecj = cd + "\\Cache\\";
        if (url == "ModMigrator") {
            cout << "开始下载: " << url << "插件" << endl;
            CreateFolder123(d_savecj, "ModMigrator");
            string zipFilePath = savecj + "ModMigrator.zip";
            url = "https://github.com/yuxuan-gzs/Forge-NeoForge-and-Fabric/releases/download/ModMigrator/ModMigrator.zip";
            if (DownloadFileWithProgress(url, zipFilePath)) {
                SetConsoleTextAttribute(hConsole, L_success);
                ExtractAndDeleteZip(zipFilePath, savecj);
                cout << "下载成功: " << "ModMigrator" << endl;
                SetConsoleTextAttribute(hConsole, L_default);
            }
            else {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "下载失败: " << "ModMigrator" << endl;
                SetConsoleTextAttribute(hConsole, L_default);
            }
        }
        return;
    }
}

void HandleReloadPluginsCommand(const string& pluginsDir) {
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "\n[插件] 正在热重载所有插件..." << endl;

    if (g_PluginManager) {
        g_PluginManager->UnloadAllPlugins(true);
        int loadedCount = g_PluginManager->LoadPluginsFromDirectoryWithUI(pluginsDir);
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "\n[插件] 热重载完成，共加载 " << loadedCount << " 个插件" << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[插件] 插件管理器未初始化" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandleReloadPluginCommand(const string& command) {
    string pluginId = command.substr(command.find(' ') + 1);
    pluginId.erase(0, pluginId.find_first_not_of(" \t\r\n"));
    pluginId.erase(pluginId.find_last_not_of(" \t\r\n") + 1);

    if (pluginId.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "用法: reloadplugin <插件ID>" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "\n[插件] 正在热重载插件: " << pluginId << endl;

    if (g_PluginManager) {
        g_PluginManager->ReloadPlugin(pluginId, false);
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "\n[插件] 热重载完成" << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[插件] 插件管理器未初始化" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandleLoadPluginCommand(const string& command) {
    string pluginPath = command.substr(command.find(' ') + 1);
    pluginPath.erase(0, pluginPath.find_first_not_of(" \t\r\n"));
    pluginPath.erase(pluginPath.find_last_not_of(" \t\r\n") + 1);

    if (pluginPath.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "用法: loadplugin <路径>" << endl;
        cout << "示例: loadplugin C:\\plugins\\MyPlugin.zip" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    string resolvedPath = ResolvePath(pluginPath);
    SetConsoleTextAttribute(hConsole, L_default);
    cout << "\n[插件] 加载: " << resolvedPath << endl;

    if (g_PluginManager) {
        if (g_PluginManager->LoadPlugin(resolvedPath, false)) {
            SetConsoleTextAttribute(hConsole, L_success);
            cout << "[插件] 加载成功" << endl;
        }
        else {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[插件] 加载失败" << endl;
        }
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[插件] 插件管理器未初始化" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandleUnloadPluginCommand(const string& command) {
    string pluginId = command.substr(command.find(' ') + 1);
    pluginId.erase(0, pluginId.find_first_not_of(" \t\r\n"));
    pluginId.erase(pluginId.find_last_not_of(" \t\r\n") + 1);

    if (pluginId.empty()) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "用法: unloadplugin <插件ID>" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "\n[插件] 卸载: " << pluginId << endl;

    if (g_PluginManager) {
        g_PluginManager->UnloadPlugin(pluginId, false);
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[插件] 已卸载" << endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[插件] 插件管理器未初始化" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandlePluginsCommand() {
    if (!g_PluginManager) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[插件] 插件管理器未初始化" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    auto plugins = g_PluginManager->GetLoadedPlugins();
    auto allCmds = g_PluginManager->GetAllPluginCommands();

    SetConsoleTextAttribute(hConsole, L_default);
    cout << endl;
    cout << "╔══════════════════════════════════════════════════════════════╗" << endl;
    cout << "║                    已加载的插件                             ║" << endl;
    cout << "╚══════════════════════════════════════════════════════════════╝" << endl;

    if (plugins.empty()) {
        SetConsoleTextAttribute(hConsole, L_warning);
        cout << "  没有已加载的插件" << endl;
    }
    else {
        for (const auto& info : plugins) {
            SetConsoleTextAttribute(hConsole, L_6);
            cout << "  📦 " << info.name << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "    版本: " << info.version << endl;
            cout << "    作者: " << info.author << endl;
            cout << "    描述: " << info.description << endl;
            cout << "    ID: " << info.id << endl;

            cout << "    命令: ";
            bool hasCmd = false;
            for (const auto& cmd : allCmds) {
                if (cmd.pluginName == info.name) {
                    if (hasCmd) cout << ", ";
                    cout << cmd.commandName;
                    hasCmd = true;
                }
            }
            if (!hasCmd) cout << "(无)";
            cout << endl;
            cout << endl;
        }
    }

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "╔══════════════════════════════════════════════════════════════╗" << endl;
    cout << "║                    所有插件命令                                ║" << endl;
    cout << "╚══════════════════════════════════════════════════════════════╝" << endl;

    if (allCmds.empty()) {
        SetConsoleTextAttribute(hConsole, L_warning);
        cout << "  没有可用的插件命令" << endl;
    }
    else {
        for (const auto& cmd : allCmds) {
            SetConsoleTextAttribute(hConsole, L_6);
            cout << "  🔧 " << cmd.commandName;
            SetConsoleTextAttribute(hConsole, L_default);
            cout << " (" << cmd.pluginName << ")" << endl;
            cout << "    描述: " << cmd.description << endl;
            cout << "    用法: " << cmd.usage << endl;
            cout << endl;
        }
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandleCreatePluginCommand(const string& command) {
    string params = command.substr(command.find(' ') + 1);
    params.erase(0, params.find_first_not_of(" \t\r\n"));
    params.erase(params.find_last_not_of(" \t\r\n") + 1);

    vector<string> parts = ParseCommandLine(params);

    if (parts.empty() || parts[0] == "-h" || parts[0] == "--help") {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "\n用法: createplugin <插件名称> [选项]" << endl;
        cout << "选项:" << endl;
        cout << "  -i <ID>     插件ID (默认: com.example.<名称>)" << endl;
        cout << "  -v <版本>   版本号 (默认: 1.0.0)" << endl;
        cout << "  -a <作者>   作者名 (默认: 当前用户)" << endl;
        cout << "  -d <描述>   插件描述" << endl;
        cout << "  -o <目录>   输出目录 (默认: ./api_templates/)" << endl;
        cout << "  -api        生成 api_*.dll 新增指令集模板（非管理员、放入程序目录）" << endl;
        cout << "示例:" << endl;
        cout << "  createplugin MyPlugin" << endl;
        cout << "  createplugin MyApi -api" << endl;
        cout << "  createplugin MyPlugin -i com.example.myplugin -v 2.0.0 -a \"Your Name\"" << endl;
        cout << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    string pluginName = parts[0];
    string pluginId = "com.example." + pluginName;
    string version = "1.0.0";
    string author = "Unknown";
    string description = pluginName + " plugin for Light";
    // 默认输出到程序根目录下的新目录（Cache/PluginTemplates 等历史目录
    // 是旧程序以管理员身份创建的，普通权限用户只有读权限，会导致生成失败）。
    string outputDir = "api_templates\\" + pluginName + "\\";
    bool apiTemplate = false;
    for (size_t i = 1; i < parts.size(); i++) {
        if (parts[i] == "-i" && i + 1 < parts.size()) {
            pluginId = parts[++i];
        }
        else if (parts[i] == "-v" && i + 1 < parts.size()) {
            version = parts[++i];
        }
        else if (parts[i] == "-a" && i + 1 < parts.size()) {
            author = parts[++i];
        }
        else if (parts[i] == "-d" && i + 1 < parts.size()) {
            description = parts[++i];
        }
        else if (parts[i] == "-o" && i + 1 < parts.size()) {
            outputDir = parts[++i];
            if (!outputDir.empty() && outputDir.back() != '\\') outputDir += '\\';
            outputDir += pluginName + "\\";
        }
        else if (parts[i] == "-api" || parts[i] == "--api" ||
            parts[i] == "-t" && i + 1 < parts.size() &&
            (parts[i + 1] == "api" || parts[i + 1] == "dll")) {
            apiTemplate = true;
            if (parts[i] == "-t") ++i;
        }
    }

    if (author == "Unknown") {
        char* username = nullptr;
        size_t len = 0;
        if (_dupenv_s(&username, &len, "USERNAME") == 0 && username != nullptr) {
            author = username;
            free(username);
        }
    }

    string resolvedOutputDir = ResolvePath(outputDir);

    PluginTemplateGenerator::TemplateConfig config;
    config.pluginName = pluginName;
    config.pluginId = pluginId;
    config.version = version;
    config.author = author;
    config.description = description;
    config.outputDir = resolvedOutputDir;

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "\n[模板生成] 开始生成插件模板..." << endl;
    cout << "[模板生成] 插件名称: " << pluginName << endl;
    cout << "[模板生成] 插件ID: " << pluginId << endl;
    cout << "[模板生成] 版本: " << version << endl;
    cout << "[模板生成] 作者: " << author << endl;
    cout << "[模板生成] 输出目录: " << resolvedOutputDir << endl;
    cout << "[模板生成] 类型: " << (apiTemplate ? "api_*.dll 指令集" : "通用插件") << endl;
    cout << endl;

    StartSpinner();

    bool ok = apiTemplate ? PluginTemplateGenerator::GenerateApiTemplate(config)
                          : PluginTemplateGenerator::GenerateTemplate(config);
    if (ok) {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "\n[模板生成] 成功!" << endl;
        if (apiTemplate) {
            cout << "[模板生成] 工程目录: " << resolvedOutputDir << endl;
            cout << "[模板生成] 构建后把 api_" << pluginName
                << ".dll 放到 light.exe 同目录即可静默加载。" << endl;
        }
        else {
            fs::path out = fs::absolute(fs::path(resolvedOutputDir));
            if (out.filename().empty()) out = out.parent_path();
            std::string zipPath = (out.parent_path() /
                (pluginName + ".zip")).string();
            cout << "[模板生成] ZIP文件: " << zipPath << endl;
        }
    }
    else {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "\n[模板生成] 失败!" << endl;
    }
    SetConsoleTextAttribute(hConsole, L_default);
}

void HandleGuiProviderCommand(const string& command) {
    string params = command.find(' ') == string::npos
        ? "" : command.substr(command.find(' ') + 1);
    params = TrimCopy(params);

    if (params.empty()) {
        string current = GetGuiProviderPreference();
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "\nGUI 提供者偏好: " << current << endl;
        cout << "  auto     外部 GUI 插件优先，找不到时使用内置 GUI（默认）" << endl;
        cout << "  external 仅优先尝试外部 GUI 插件，失败回退内置" << endl;
        cout << "  builtin  始终使用内置 GUI" << endl;
        cout << "用法: guiprovider <auto|external|builtin>" << endl;
        cout << "修改后重新启动 light.exe 生效。" << endl;
        cout << endl;
        return;
    }

    std::string value = params;
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    if (value != "auto" && value != "external" && value != "builtin") {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "无效的 GUI 提供者: " << params << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    if (SetGuiProviderPreference(value)) {
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "GUI 提供者偏好已设置为: " << value
            << "，重新启动 light.exe 后生效。" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
    else {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "写入 GUI 提供者偏好失败。" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
}

bool ExecutePluginCommand(const string& command) {
    if (!g_PluginManager) return false;
    vector<string> cmdParts = ParseCommandLine(command);
    if (cmdParts.empty()) return false;
    string cmdName = cmdParts[0];    vector<string> cmdArgs;
    for (size_t i = 1; i < cmdParts.size(); i++) {
        cmdArgs.push_back(cmdParts[i]);
    }
    return g_PluginManager->ExecuteCommand(cmdName, cmdArgs);
}

// ==================== 通用解 ZIP 命令 ====================
// 用法: unzip "<zip文件路径>" ["<目标目录>"]
//   目标目录省略时，默认解到 zip 所在目录的同名文件夹
void HandleUnzipCommand(const string& command) {
    // 跳过 "unzip "
    size_t sp = command.find(' ');
    string params = (sp == string::npos) ? "" : command.substr(sp + 1);

    // 去掉首尾空白
    params.erase(0, params.find_first_not_of(" \t\r\n"));
    if (!params.empty())
        params.erase(params.find_last_not_of(" \t\r\n") + 1);

    vector<string> parts = ParseCommandLine(params);

    if (parts.empty() || parts[0] == "-h" || parts[0] == "--help") {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "用法: unzip \"<zip文件路径>\" [\"<目标目录>\"]" << endl;
        cout << "  <目标目录> 省略时解到 zip 同目录的同名文件夹" << endl;
        cout << "示例:" << endl;
        cout << "  unzip \"Cache\\crx\\abc.zip\"" << endl;
        cout << "  unzip \"Cache\\crx\\abc.zip\" \"Cache\\Extensions\\abc\"" << endl;
        return;
    }

    string zipFile = parts[0];
    string outDir;

    if (parts.size() >= 2) {
        outDir = parts[1];
    }
    else {
        // 默认解到同目录的同名文件夹
        string resolvedZip = ResolvePath(zipFile);
        string parentDir = GetParentDirPath(resolvedZip);
        string baseName = GetFileNameFromPath(resolvedZip);
        size_t dotPos = baseName.find_last_of('.');
        if (dotPos != string::npos) baseName = baseName.substr(0, dotPos);
        outDir = parentDir + "\\" + baseName + "_extracted";
    }

    string resolvedZip = ResolvePath(zipFile);
    string resolvedOutDir = ResolvePath(outDir);

    DWORD dwAttrib = GetFileAttributesA(resolvedZip.c_str());
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[unzip] 文件不存在: " << resolvedZip << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return;
    }

    // 如果目标目录已存在，先清空（保证解压结果干净）
    DWORD outAttrib = GetFileAttributesA(resolvedOutDir.c_str());
    if (outAttrib != INVALID_FILE_ATTRIBUTES && (outAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        DeleteFilesInDirectory(resolvedOutDir);
        RemoveDirectoryA(resolvedOutDir.c_str());
    }
    CreateDirectoryA(resolvedOutDir.c_str(), NULL);

    SetConsoleTextAttribute(hConsole, L_default);
    cout << "[unzip] " << resolvedZip << " -> " << resolvedOutDir << endl;
    StartSpinner();

    if (ExtractZipWithMinizip(resolvedZip, resolvedOutDir)) {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[unzip] 解压成功: " << resolvedOutDir << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
    else {
        StopSpinner();
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[unzip] 解压失败: " << resolvedZip << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
}

// ============================================================
// ========== 新增：命令执行器（供 GUI 模式复用） ==========
// ============================================================
// 这个函数把 main() 里的 if-else if 逻辑抽出来，
// 但不需要改动 main() 本身。GUI 模式调用它即可。
// 返回 false 表示退出程序
// ============================================================
// ============================================================
// 命令行直入 / cmd 桥接 / 未知指令文件回退
// ============================================================
static std::wstring LightUtf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

static std::string LightWideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
        nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
        &out[0], len, nullptr, nullptr);
    return out;
}

static std::string TrimCopy(const std::string& value) {
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

static bool StartsWithNoCase(const std::string& text, const std::string& prefix) {
    if (text.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower((unsigned char)text[i]) !=
            std::tolower((unsigned char)prefix[i])) {
            return false;
        }
    }
    return true;
}

static std::vector<std::string> GetCommandLineArgsUtf8() {
    std::vector<std::string> args;
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv) return args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(LightWideToUtf8(wargv[i] ? wargv[i] : L""));
    }
    LocalFree(wargv);
    return args;
}

static std::string JoinCommandLineArgs(const std::vector<std::string>& args) {
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out += " ";
        const std::string& arg = args[i];
        bool needQuote = arg.find_first_of(" \t\"") != std::string::npos;
        if (!needQuote) {
            out += arg;
        }
        else {
            out += '"';
            for (char c : arg) {
                if (c == '"') out += '\\';
                out += c;
            }
            out += '"';
        }
    }
    return out;
}

static std::string GetGuiProviderPreference() {
    std::string path = cd + "\\Cache\\gui_provider.txt";
    std::ifstream in(path);
    if (!in.is_open()) return "auto";
    std::string value;
    std::getline(in, value);
    value = TrimCopy(value);
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    if (value == "builtin" || value == "external" || value == "auto") {
        return value;
    }
    return "auto";
}

static bool SetGuiProviderPreference(const std::string& value) {
    std::string dir = cd + "\\Cache";
    CreateDirectoryA(dir.c_str(), nullptr);
    std::ofstream out(dir + "\\gui_provider.txt", std::ios::trunc);
    if (!out.is_open()) return false;
    out << value << "\n";
    return true;
}

static std::string ConvertCommandOutput(const std::string& bytes, UINT codePage) {
    if (bytes.empty()) return "";
    int wlen = MultiByteToWideChar(codePage, 0, bytes.data(), (int)bytes.size(),
        nullptr, 0);
    if (wlen <= 0) return bytes;
    std::wstring wide((size_t)wlen, L'\0');
    MultiByteToWideChar(codePage, 0, bytes.data(), (int)bytes.size(),
        &wide[0], wlen);

    UINT targetCp = g_guiProvider ? CP_UTF8 : CP_ACP;
    int len = WideCharToMultiByte(targetCp, 0, wide.c_str(), (int)wide.size(),
        nullptr, 0, nullptr, nullptr);
    if (len <= 0) return bytes;
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(targetCp, 0, wide.c_str(), (int)wide.size(),
        &out[0], len, nullptr, nullptr);
    return out;
}

// 在 light.exe 自己的窗口里执行 cmd 指令。
// 控制台模式：子 cmd 附着当前控制台，交互式批处理可直接工作。
// GUI 模式：捕获 stdout/stderr，转码后写入 GUI 控制台。
static int RunCommandViaCmd(const std::string& rawCommand) {
    std::string command = TrimCopy(rawCommand);
    if (command.empty()) {
        SetConsoleTextAttribute(hConsole, L_warning);
        cout << "用法: cmd <命令>" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return 1;
    }

    const bool capture = (g_guiProvider != nullptr);
    std::wstring wcmd = LightUtf8ToWide(command);
    std::wstring cmdline = L"cmd.exe /d /s /c \"" + wcmd + L"\"";
    std::vector<wchar_t> cmdBuf(cmdline.begin(), cmdline.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    HANDLE nulInput = nullptr;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    if (capture) {
        if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
            cout << "[cmd] 创建管道失败" << endl;
            return 1;
        }
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
        nulInput = CreateFileW(L"NUL", GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;
        si.hStdInput = nulInput;
    }

    DWORD flags = capture ? CREATE_NO_WINDOW : 0;
    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
        capture ? TRUE : FALSE, flags, nullptr, nullptr, &si, &pi);
    if (!ok) {
        if (readPipe) CloseHandle(readPipe);
        if (writePipe) CloseHandle(writePipe);
        if (nulInput) CloseHandle(nulInput);
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "[cmd] 启动 cmd.exe 失败 (错误码 " << GetLastError() << ")" << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return 1;
    }

    if (capture) {
        CloseHandle(writePipe);
        writePipe = nullptr;
        std::string output;
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) &&
            read > 0) {
            output.append(buffer, read);
            if (output.size() > 8 * 1024 * 1024) break;   // 防御性上限
        }
        CloseHandle(readPipe);
        readPipe = nullptr;
        WaitForSingleObject(pi.hProcess, INFINITE);
        std::string text = ConvertCommandOutput(output, GetOEMCP());
        if (!text.empty()) {
            cout << text;
            if (text.back() != '\n' && text.back() != '\r') cout << endl;
            cout.flush();
        }
    }
    else {
        WaitForSingleObject(pi.hProcess, INFINITE);
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (nulInput) CloseHandle(nulInput);
    if (readPipe) CloseHandle(readPipe);
    if (writePipe) CloseHandle(writePipe);

    if (capture && exitCode != 0) {
        cout << "[cmd] 退出码: " << exitCode << endl;
    }
    return (int)exitCode;
}

static void LaunchInteractiveCmd() {
    if (g_guiProvider) {
        cout << "GUI 模式下请输入: cmd <命令>" << endl;
        return;
    }
    std::wstring cmdline = L"cmd.exe /d";
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0,
        nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static bool ResolveCommandFile(const std::string& command,
    const std::string& currentDir,
    std::string& outPath, std::string& outArgs) {
    std::string text = TrimCopy(command);
    if (text.empty()) return false;

    std::string token;
    size_t pos = 0;
    if (text[0] == '"') {
        size_t end = text.find('"', 1);
        if (end == std::string::npos) return false;
        token = text.substr(1, end - 1);
        pos = end + 1;
    }
    else {
        size_t end = text.find_first_of(" \t");
        token = (end == std::string::npos) ? text : text.substr(0, end);
        pos = (end == std::string::npos) ? text.size() : end;
    }
    outArgs = TrimCopy(text.substr(pos));
    if (token.empty()) return false;

    std::string baseDir = currentDir;
    if (baseDir.empty()) baseDir = ".";

    std::vector<std::string> candidates;
    auto addCandidate = [&](const std::string& name) {
        if (name.empty()) return;
        bool absolute = (name.size() >= 2 && name[1] == ':') ||
            (name.size() >= 2 && name[0] == '\\' && name[1] == '\\') ||
            (!name.empty() && name[0] == '\\');
        std::string full = absolute ? name : (baseDir + "\\" + name);
        DWORD attr = GetFileAttributesA(full.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES &&
            !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            candidates.push_back(full);
        }
        };

    addCandidate(token);
    if (token.find('.') == std::string::npos) {
        const char* pathext = getenv("PATHEXT");
        std::string exts = pathext ? pathext : ".COM;.EXE;.BAT;.CMD";
        std::stringstream ss(exts);
        std::string ext;
        while (std::getline(ss, ext, ';')) {
            if (!ext.empty()) addCandidate(token + ext);
        }
        addCandidate(token + ".bat");
        addCandidate(token + ".cmd");
        addCandidate(token + ".exe");
    }

    if (candidates.empty()) return false;
    outPath = candidates.front();
    return true;
}

static bool TryRunUnknownCommandFile(const std::string& command,
    const std::string& currentDir) {
    std::string path;
    std::string args;
    if (!ResolveCommandFile(command, currentDir, path, args)) return false;

    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    std::string ext;
    size_t dot = lower.find_last_of('.');
    if (dot != std::string::npos) ext = lower.substr(dot);

    if (ext == ".bat" || ext == ".cmd") {
        // 批处理/命令脚本：在 light.exe 窗口内执行，并传入未知指令参数。
        std::string cmd = "\"" + path + "\"";
        if (!args.empty()) cmd += " " + args;
        RunCommandViaCmd(cmd);
        return true;
    }

    std::wstring wpath = LightUtf8ToWide(path);
    std::wstring wargs = LightUtf8ToWide(args);
    HINSTANCE result = ShellExecuteW(nullptr, L"open", wpath.c_str(),
        wargs.empty() ? nullptr : wargs.c_str(), nullptr, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

static int SilentLoadApiPlugins(const std::string& directory) {
    if (!g_PluginManager) return 0;
    return g_PluginManager->LoadApiPluginsFromDirectory(directory, true);
}

// ============================================================
// 统一命令执行入口（控制台 / GUI 工作线程共用）
// ============================================================

static bool ExecuteOneCommand(const std::string& command,
    std::string& cd,
    SecurityCore* securityCore,
    PluginManager* pluginManager,
    const std::string& pluginsDir,
    const std::string& registryPath,
    const std::string& folderPath)
{
    IGuiProvider* activeGui = g_guiProvider;
    if (activeGui) activeGui->UpdatePrompt(cd);

    const std::string trimmed = TrimCopy(command);

    // ====== cmd 前缀：交给 cmd 指令集，在 light 窗口内执行 ======
    if (StartsWithNoCase(trimmed, "cmd ")) {
        RunCommandViaCmd(trimmed.substr(4));
        return true;
    }
    if (trimmed.size() == 3 && StartsWithNoCase(trimmed, "cmd")) {
        LaunchInteractiveCmd();
        return true;
    }

    // ====== help ======
    if (command == "help") {
        help();
        ExecutePluginCommand(command);
        return true;
    }
    // ====== cd ======
    else if (command.rfind("cd ", 0) == 0 || command.rfind("CD ", 0) == 0) {
        HandleCdCommand(command, cd);
        IGuiProvider* activeGui = g_guiProvider;
        if (activeGui) activeGui->UpdatePrompt(cd);
        return true;
    }
    // ====== block ======
    else if (command.rfind("block ", 0) == 0 || command.rfind("Block ", 0) == 0) {
        HandleBlockCommand(command);
        return true;
    }
    // ====== unblock ======
    else if (command.rfind("unblock ", 0) == 0 || command.rfind("Unblock ", 0) == 0) {
        HandleUnblockCommand(command);
        return true;
    }
    // ====== showblocks ======
    else if (command == "showblocks" || command == "ShowBlocks") {
        HandleShowBlocksCommand();
        return true;
    }
    // ====== clearblocks ======
    else if (command == "clearblocks" || command == "ClearBlocks") {
        HandleClearBlocksCommand();
        return true;
    }
    // ====== whitelist ======
    else if (command.rfind("whitelist ", 0) == 0 || command.rfind("Whitelist ", 0) == 0) {
        HandleWhitelistCommand(command);
        return true;
    }
    // ====== unwhitelist ======
    else if (command.rfind("unwhitelist ", 0) == 0 || command.rfind("Unwhitelist ", 0) == 0) {
        HandleUnwhitelistCommand(command);
        return true;
    }
    // ====== exit ======
    else if (command == "exit") {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "exit+" << endl;
        return false;
    }
    // ====== CEL ======
    else if (command == "CEL") {
        HandleCELCommand();
        return true;
    }
    // ====== uninstall ======
    else if (command == "uninstall") {
        HandleUninstallCommand(folderPath);
        return true;
    }
    // ====== FPNPCT ======
    else if (command == "FPNPCT" || command == "Full Port Network Packet Capture Tool") {
        HandleFPNPCTCommand();
        return true;
    }
    // ====== about ======
    else if (command == "about") {
        HandleAboutCommand();
        return true;
    }
    // ====== fill ======
    else if (command == "fill") {
        HandleFillCommand(cd, registryPath);
        return true;
    }
    // ====== dump ======
    else if (command == "dump") {
        HandleDumpCommand(cd, registryPath);
        return true;
    }
    // ====== lock ======
    else if (command.rfind("Lock ", 0) == 0 || command.rfind("lock ", 0) == 0) {
        HandleLockCommand(command);
        return true;
    }
    // ====== unlock ======
    else if (command.rfind("Unlock ", 0) == 0 || command.rfind("unlock ", 0) == 0) {
        HandleUnlockCommand(command);
        return true;
    }
    // ====== Lock/Unlock 单独使用 ======
    else if (command == "Lock" || command == "lock" || command == "LOCK" ||
        command == "Unlock" || command == "unlock" || command == "UNLOCK") {
        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误的参数指令：" << command << endl;
        cout << "                ~~~~^" << endl;
        cout << "参数后应跟相应的路径。" << endl;
        cout << " " << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return true;
    }
    // ====== Registry ======
    else if (command == "Registry") {
        HandleRegistryCommand();
        return true;
    }
    // ====== BinaryHexEditor ======
    else if (command == "BinaryHexEditor") {
        HandleBinaryHexEditorCommand();
        return true;
    }
    // ====== RestoreFolderPermission ======
    else if (command == "RestoreFolderPermission") {
        HandleRestoreFolderPermissionCommand(folderPath);
        return true;
    }
    // ====== compress ======
    else if (command.rfind("compress ", 0) == 0 || command.rfind("Compress ", 0) == 0) {
        HandleCompressCommand(command);
        return true;
    }
    // ====== extract ======
    else if (command.rfind("extract ", 0) == 0 || command.rfind("Extract ", 0) == 0) {
        HandleExtractCommand(command);
        return true;
    }
    // ====== list ======
    else if (command.rfind("list ", 0) == 0 || command.rfind("List ", 0) == 0) {
        HandleListCommand(command);
        return true;
    }
    // ====== unzip ======
    else if (command.rfind("unzip ", 0) == 0 || command.rfind("Unzip ", 0) == 0 || command.rfind("UNZIP ", 0) == 0) {
        HandleUnzipCommand(command);
        return true;
    }
    // ====== dll 桥接：download "<url>" "<outPath>" ======
    // 这个分支必须在 HandleDownloadCommand 之前判断，
    // ====== dll 桥接：download "<url>" "<outPath>" ======
    else if (command.length() >= 10 &&
        command.compare(0, 9, "download ") == 0 &&
        command[9] == '"') {
        // 从 `download ` 之后的第一个 `"` 开始扫描两个引号参数
        std::vector<std::string> parts;
        size_t i = 9;   // 指向第一个 `"`
        while (i < command.size() && parts.size() < 2) {
            // 跳过空白
            while (i < command.size() &&
                (command[i] == ' ' || command[i] == '\t')) i++;
            if (i >= command.size()) break;
            if (command[i] != '"') {
                wchar_t dbg[512];
                swprintf_s(dbg,
                    L"[Light] 参数 %zu 不是引号开头: char=0x%02X\n",
                    parts.size(), (unsigned char)command[i]);
                OutputDebugStringW(dbg);
                break;
            }
            i++;   // 跳过开引号
            size_t end = command.find('"', i);
            if (end == std::string::npos) {
                OutputDebugStringW(L"[Light] 找不到闭引号\n");
                break;
            }
            parts.push_back(command.substr(i, end - i));
            i = end + 1;
        }

        {
            wchar_t dbg[2048];
            swprintf_s(dbg,
                L"[Light] download 桥接解析: parts.size()=%zu\n",
                parts.size());
            OutputDebugStringW(dbg);
        }

        if (parts.size() >= 2) {
            {
                wchar_t dbg[2048];
                swprintf_s(dbg,
                    L"[Light]   url = %hs\n"
                    L"[Light]   out = %hs\n",
                    parts[0].c_str(), parts[1].c_str());
                OutputDebugStringW(dbg);
            }
            DownloadForPlugin(parts[0], parts[1]);
        }
        else {
            wchar_t dbg[2048];
            cout << "[Light] download 桥接格式错误 原始命令 = " << command << " parts.size() = " << parts.size() << endl;

            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[Light] download 桥接格式错误" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        return true;
    }
    // ====== download ======
    else if (command.rfind("download ", 0) == 0 || command.rfind("Download ", 0) == 0 || command.rfind("D ", 0) == 0) {
        HandleDownloadCommand(command, securityCore);
        return true;
    }
    // ====== 插件系统命令 ======
    else if (command == "reloadplugins" || command == "ReloadPlugins") {
        HandleReloadPluginsCommand(pluginsDir);
        return true;
    }
    else if (command.rfind("reloadplugin ", 0) == 0 || command.rfind("ReloadPlugin ", 0) == 0) {
        HandleReloadPluginCommand(command);
        return true;
    }
    else if (command.rfind("loadplugin ", 0) == 0 || command.rfind("LoadPlugin ", 0) == 0) {
        HandleLoadPluginCommand(command);
        return true;
    }
    else if (command.rfind("unloadplugin ", 0) == 0 || command.rfind("UnloadPlugin ", 0) == 0) {
        HandleUnloadPluginCommand(command);
        return true;
    }
    else if (command == "plugins" || command == "Plugins" || command == "listplugins") {
        HandlePluginsCommand();
        return true;
    }
    else if (command.rfind("createplugin ", 0) == 0 || command.rfind("CreatePlugin ", 0) == 0) {
        HandleCreatePluginCommand(command);
        return true;
    }
    else if (command == "guiprovider" || command == "GuiProvider" ||
        command.rfind("guiprovider ", 0) == 0 ||
        command.rfind("GuiProvider ", 0) == 0) {
        HandleGuiProviderCommand(command);
        return true;
    }
    // ====== 插件命令执行 ======
    else {
        if (ExecutePluginCommand(command)) {
            return true;
        }

        if (command.empty()) {
            return true;
        }

        // 未知指令：检查当前 cd 下是否存在同名文件，有则打开；
        // .bat/.cmd 在 light.exe 窗口内执行并传入剩余参数。
        if (TryRunUnknownCommandFile(command, cd)) {
            return true;
        }

        SetConsoleTextAttribute(hConsole, L_error);
        cout << "错误的参数指令：" << command << endl;
        SetConsoleTextAttribute(hConsole, L_default);
        return true;
    }
}

// ============================================================
// ========== 新增：GUI 模式下的命令处理回调 ==========
// ============================================================
// 当 GUI 收到命令时，在独立线程调用此函数
// 它执行完命令后，把结果投递回 GUI 输出面板
// ============================================================
static void GuiCommandHandler(const std::string& command,
    SecurityCore* securityCore,
    PluginManager* pluginManager,
    const std::string& pluginsDir,
    const std::string& registryPath,
    const std::string& folderPath)
{
    bool keepRunning = ExecuteOneCommand(
        command, cd, securityCore, pluginManager,
        pluginsDir, registryPath, folderPath);
    if (!keepRunning) {
        g_guiShouldExit = true;
        // 通知插件主窗口关闭（WM_CLOSE -> 插件 DestroyWindow -> PostQuitMessage）
        if (g_guiMainWnd && IsWindow(g_guiMainWnd)) {
            PostMessageW(g_guiMainWnd, WM_CLOSE, 0, 0);
        }
    }
}

// ============================================================
// ========== 新增：GUI 模式下，等待外部命令的线程 ==========
// ============================================================
// 这个线程从 g_guiCmdQueue 取命令，执行它
// GUI 的输入框把命令 push 到这个队列
// ============================================================
static void GuiCommandWorker(SecurityCore* securityCore,
    PluginManager* pluginManager,
    const std::string& pluginsDir,
    const std::string& registryPath,
    const std::string& folderPath)
{
    while (g_guiRunning && !g_guiShouldExit) {
        std::unique_lock<std::mutex> lock(g_guiCmdMutex);
        g_guiCmdCV.wait_for(lock, std::chrono::milliseconds(100), [] {
            return !g_guiCmdQueue.empty() || !g_guiRunning || g_guiShouldExit.load();
            });
        while (!g_guiCmdQueue.empty()) {
            std::string cmd = g_guiCmdQueue.front();
            g_guiCmdQueue.pop();
            lock.unlock();
            GuiCommandHandler(cmd, securityCore, pluginManager,
                pluginsDir, registryPath, folderPath);
            lock.lock();
        }
    }
}

// ============================================================
// ========== 新增：GUI 模式的入口函数 ==========
// ============================================================
// 返回值：
//   true  = 成功进入并退出了 GUI 模式
//   false = 没有找到 GUI 插件，应该走控制台模式
// ============================================================
// ============================================================
// ========== 新增：cout 重定向流缓冲（GUI 模式用） ==========
// ============================================================
// 把 std::cout / std::cerr 的内容转发到 GUI 插件的控制台。
// 构造时传入原来的 streambuf，内容同时保留一份到原控制台（黑窗口），
// 方便调试时对照。
// 转发到 GUI 时附带 ANSI 颜色标记（由 SetConsoleTextAttribute 宏注入），
// 原始控制台则去掉这些标记，避免黑窗口出现乱码。
// ============================================================
// 当前线程已注入到 GUI 流的颜色（-1 表示尚未注入）
static thread_local int g_guiStreamColor = -1;

// 去掉 ANSI SGR 转义序列（\x1b[...m），用于原始控制台回显。
static std::string StripAnsi(const std::string& s) {
    if (s.find('\x1b') == std::string::npos) return s;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < s.size() &&
                   (isdigit((unsigned char)s[j]) || s[j] == ';')) ++j;
            if (j < s.size() && s[j] == 'm') { i = j; continue; }
        }
        out += s[i];
    }
    return out;
}

class GuiStreamBuf : public std::streambuf {
public:
    GuiStreamBuf(IGuiProvider* provider, std::streambuf* fallback)
        : m_provider(provider), m_fallback(fallback) {
    }

    // 刷新缓冲区中残留的内容（退出 GUI 模式前调用）
    void Flush() {
        std::lock_guard<std::mutex> lock(m_mutex);
        FlushLocked();
    }

protected:
    int overflow(int ch) override {
        if (ch == traits_type::eof()) {
            Flush();
            return traits_type::not_eof(ch);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            EnsureColorMarkerLocked();
            m_buffer += (char)ch;
            if (ch == '\n') FlushLocked();
        }
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        EnsureColorMarkerLocked();
        m_buffer.append(s, (size_t)n);
        // 按行刷出，避免每个字符都发一次消息
        size_t pos;
        while ((pos = m_buffer.find('\n')) != std::string::npos) {
            std::string line = m_buffer.substr(0, pos + 1);
            m_buffer.erase(0, pos + 1);
            SendToGui(line);
        }
        // 防止极端情况下缓冲区无限增长
        if (m_buffer.size() > 8192) FlushLocked();
        return n;
    }

    int sync() override {
        Flush();
        return 0;
    }

private:
    // 若当前线程前景色自上次注入后发生变化，则先注入一个 ANSI 颜色标记。
    void EnsureColorMarkerLocked() {
        if (g_guiStreamColor == g_consoleFg) return;
        g_guiStreamColor = g_consoleFg;
        char buf[16];
        int n = sprintf_s(buf, "\x1b[%dm", ConsoleAttrToAnsi(g_consoleFg));
        if (n > 0) m_buffer.append(buf, (size_t)n);
    }

    void FlushLocked() {
        if (m_buffer.empty()) return;
        std::string text;
        text.swap(m_buffer);
        SendToGui(text);
    }

    void SendToGui(const std::string& text) {
        // 转发到插件控制台（插件内部会 PostMessage 到 UI 线程，线程安全）
        // 文本中携带 ANSI 颜色标记，供 GUI 富文本框按段着色。
        if (m_provider) m_provider->AppendOutput(text);
        // 同时保留原始控制台输出（调试时黑窗口仍能看到），去掉颜色标记。
        if (m_fallback) {
            std::string clean = StripAnsi(text);
            m_fallback->sputn(clean.c_str(), (std::streamsize)clean.size());
        }
    }

    IGuiProvider* m_provider;
    std::streambuf* m_fallback;
    std::string m_buffer;
    std::mutex m_mutex;
};

// ============================================================
// GUI 启动开关：Cache\g.xayx 与 sys.xayx 使用相同的 XOR 读写格式。
// 文件不存在时写入默认值，默认值不是 GUI 启用标记，因此首次启动进入控制台。
// ============================================================
static const char* kGuiModeDefaultValue = "dkg33h4x2f24hdg5x2f2g4:FT12sf";
static const char* kGuiModeEnabledValue = "dkg33h4x2f24hdg5x2f2g4:Tdsg2";

static bool IsGuiModeConfigured()
{
    const std::string cacheDir = cd + "\\Cache";
    if (GetFileAttributesA(cacheDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        CreateDirectoryA(cacheDir.c_str(), nullptr);
    }

    const std::string configPath = cacheDir + "\\g.xayx";
    if (GetFileAttributesA(configPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const std::string encrypted = XorEncryptDecrypt(kGuiModeDefaultValue, PASSWORD);
        std::ofstream out(configPath, std::ios::binary | std::ios::trunc);
        if (out.is_open()) {
            out.write(encrypted.data(), static_cast<std::streamsize>(encrypted.size()));
        }
        return false;
    }

    // 与 sys.xayx 完全一致：二进制读取后用 PASSWORD 做 XOR 解密。
    return ReadRegistryFile(configPath) == kGuiModeEnabledValue;
}

// ============================================================
// 系统托盘：左键双击显示；右键菜单提供显示/隐藏/退出。
// 主窗口的 X 按钮默认隐藏到托盘，只有明确选择“退出”才关闭进程。
// ============================================================
static constexpr UINT kTrayCallbackMessage = WM_APP + 0x371;
static constexpr UINT_PTR kTraySubclassId = 0x4C495447;
static constexpr UINT kTrayMenuShow = 41001;
static constexpr UINT kTrayMenuHide = 41002;
static constexpr UINT kTrayMenuExit = 41003;
static NOTIFYICONDATAW g_trayIconData{};
static bool g_trayIconInstalled = false;

static void TrayShowMainWindow(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    ShowWindow(hwnd, SW_RESTORE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

static bool AddLightTrayIcon(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return false;

    ZeroMemory(&g_trayIconData, sizeof(g_trayIconData));
    g_trayIconData.cbSize = sizeof(g_trayIconData);
    g_trayIconData.hWnd = hwnd;
    g_trayIconData.uID = 1;
    g_trayIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIconData.uCallbackMessage = kTrayCallbackMessage;
    g_trayIconData.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wcsncpy_s(g_trayIconData.szTip, _countof(g_trayIconData.szTip), L"Light", _TRUNCATE);

    g_trayIconInstalled = Shell_NotifyIconW(NIM_ADD, &g_trayIconData) != FALSE;
    if (!g_trayIconInstalled) {
        OutputDebugStringA("[Tray] Shell_NotifyIcon(NIM_ADD) failed\n");
    }
    return g_trayIconInstalled;
}

static void RemoveLightTrayIcon(HWND hwnd)
{
    if (!g_trayIconInstalled) return;
    if (hwnd && g_trayIconData.hWnd != hwnd) return;
    Shell_NotifyIconW(NIM_DELETE, &g_trayIconData);
    g_trayIconInstalled = false;
    ZeroMemory(&g_trayIconData, sizeof(g_trayIconData));
}

static void ShowLightTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, kTrayMenuShow, L"显示主窗口");
    AppendMenuW(menu, MF_STRING, kTrayMenuHide, L"隐藏主窗口");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayMenuExit, L"退出");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    const UINT selected = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (selected) {
    case kTrayMenuShow:
        TrayShowMainWindow(hwnd);
        break;
    case kTrayMenuHide:
        ShowWindow(hwnd, SW_HIDE);
        break;
    case kTrayMenuExit:
        g_guiShouldExit = true;
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
}

static LRESULT CALLBACK LightTraySubclassProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR refData)
{
    (void)wParam;
    (void)refData;

    switch (message) {
    case kTrayCallbackMessage:
        switch (static_cast<UINT>(lParam)) {
        case WM_LBUTTONDBLCLK:
            TrayShowMainWindow(hwnd);
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowLightTrayMenu(hwnd);
            return 0;
        default:
            break;
        }
        break;

    case WM_CLOSE:
        // X 按钮只隐藏到托盘；托盘“退出”或控制台 exit 命令才真正结束。
        if (!g_guiShouldExit.load()) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_DESTROY:
        RemoveLightTrayIcon(hwnd);
        RemoveWindowSubclass(hwnd, LightTraySubclassProc, subclassId);
        break;
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

static bool RunGuiMode(HINSTANCE hInstance,
    IGuiProvider* guiProvider,
    SecurityCore* securityCore,
    PluginManager* pluginManager,
    const std::string& pluginsDir,
    const std::string& registryPath,
    const std::string& folderPath)
{
    if (!guiProvider) return false;

    // 初始化 common controls（Tab 需要）
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // 设置命令回调：插件控制台收到命令 -> 投递到命令队列，工作线程执行
    guiProvider->SetCommandHandler([](const std::string& cmd) {
        {
            std::lock_guard<std::mutex> lock(g_guiCmdMutex);
            g_guiCmdQueue.push(cmd);
        }
        g_guiCmdCV.notify_one();
        });

    // 设置输出回调：插件如需主动输出，直接转发到它自己的控制台
    guiProvider->SetOutputHandler([guiProvider](const std::string& text) {
        guiProvider->AppendOutput(text);
        });

    // 窗口初始化期间也可能触发桥接回调，因此创建前先发布当前 provider。
    g_guiProvider = guiProvider;
    g_guiShouldExit = false;

    // 直接让 provider 创建主窗口（内置 YXGuiPlugin 优先，不套外层窗口）
    HWND hMainWnd = guiProvider->CreateCustomWindow(hInstance);
    if (!hMainWnd || !IsWindow(hMainWnd)) {
        g_guiProvider = nullptr;
        return false;
    }
    g_guiMainWnd = hMainWnd;

    if (!SetWindowSubclass(hMainWnd, LightTraySubclassProc, kTraySubclassId, 0)) {
        OutputDebugStringA("[Tray] SetWindowSubclass failed; tray close behavior may be limited\n");
    }
    AddLightTrayIcon(hMainWnd);

    // 重定向 std::cout / std::cerr 到插件控制台
    std::streambuf* oldCoutBuf = std::cout.rdbuf();
    std::streambuf* oldCerrBuf = std::cerr.rdbuf();
    GuiStreamBuf guiOutBuf(guiProvider, oldCoutBuf);
    GuiStreamBuf guiErrBuf(guiProvider, oldCerrBuf);
    std::cout.rdbuf(&guiOutBuf);
    std::cerr.rdbuf(&guiErrBuf);

    // 保存全局状态
    g_guiMode = true;
    g_guiRunning = true;
    g_guiShouldExit = false;

    // 启动命令队列工作线程
    std::thread worker(GuiCommandWorker, securityCore, pluginManager,
        pluginsDir, registryPath, folderPath);

    // 跑消息循环（插件窗口即主窗口，插件 WM_DESTROY 里会 PostQuitMessage）
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理：停止工作线程
    g_guiRunning = false;
    g_guiCmdCV.notify_all();
    if (worker.joinable()) worker.join();

    // 退出前把残留输出刷到插件，然后恢复 cout / cerr
    guiOutBuf.Flush();
    guiErrBuf.Flush();
    std::cout.rdbuf(oldCoutBuf);
    std::cerr.rdbuf(oldCerrBuf);

    RemoveLightTrayIcon(hMainWnd);
    if (IsWindow(hMainWnd)) {
        RemoveWindowSubclass(hMainWnd, LightTraySubclassProc, kTraySubclassId);
    }
    g_guiMainWnd = nullptr;
    g_guiShouldExit = true;
    g_guiMode = false;

    // 清理全局 provider
    g_guiProvider = nullptr;

    return true;
}

// ============================================================
// ========== 新增：检测是否有 GUI 插件 ==========
// ============================================================
// 遍历 plugins 目录，加载所有插件，然后检查是否有插件实现了 IGuiProvider
// 返回值：找到的第一个 IGuiProvider（可能为 nullptr）
// ============================================================
static IGuiProvider* DetectGuiProvider(PluginManager* pluginManager,
    const std::string& pluginsDir)
{
    if (!pluginManager) return nullptr;
    // 加载插件（静默模式，不输出）
    pluginManager->LoadPluginsFromDirectory(pluginsDir, true);

    // 查找 GUI 插件
    auto entry = pluginManager->FindGuiPlugin();
    return entry.provider;
}

// ==================== 新增：只隐藏，不锁定权限 ====================
// 替代 LockFolderWithTrustedInstaller + Lock_ProtectFolderRecursively
// 效果：文件夹设为隐藏 + 系统属性，不改所有者、不改 ACL
bool HideFolderOnly(const string& folderPath, bool silent = false)
{
    // 1. 确保文件夹存在
    DWORD dwAttrib = GetFileAttributesA(folderPath.c_str());
    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryA(folderPath.c_str(), NULL)) {
            if (!silent) {
                SetConsoleTextAttribute(hConsole, L_error);
                cout << "[隐藏] 创建文件夹失败: " << folderPath
                    << " (错误码: " << GetLastError() << ")" << endl;
                SetConsoleTextAttribute(hConsole, L_default);
            }
            return false;
        }
        dwAttrib = GetFileAttributesA(folderPath.c_str());
    }

    if (!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!silent) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[隐藏] 路径不是文件夹: " << folderPath << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        return false;
    }

    // 2. 只设置隐藏属性（可选加 SYSTEM，如果你想要"系统+隐藏"效果就保留 SYSTEM）
    //    只隐藏：FILE_ATTRIBUTE_HIDDEN
    //    系统+隐藏：FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM
    DWORD newAttrib = dwAttrib
        | FILE_ATTRIBUTE_HIDDEN
        | FILE_ATTRIBUTE_SYSTEM;   // 不想要"系统"属性就删掉这一行

    // 3. 清除可能残留的只读属性（避免后续写入失败）
    newAttrib &= ~FILE_ATTRIBUTE_READONLY;

    if (!SetFileAttributesA(folderPath.c_str(), newAttrib)) {
        if (!silent) {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[隐藏] 设置属性失败: " << folderPath
                << " (错误码: " << GetLastError() << ")" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        return false;
    }

    if (!silent) {
        SetConsoleTextAttribute(hConsole, L_success);
        cout << "[隐藏] 已设置隐藏属性: " << folderPath << endl;
        SetConsoleTextAttribute(hConsole, L_default);
    }
    return true;
}

// ==================== main 函数 ====================
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    //SetConsoleCtrlHandler(CtrlHandler, TRUE);

    cd = GetCurrentProgramPath();

    // ============================================================
    // 命令行直入：light.exe <指令> —— 非管理员模式，不初始化 GUI/服务。
    // 例：light.exe plugins
    //     light.exe cmd dir
    //     light.exe download https://example.com/a.zip
    // ============================================================
    std::vector<std::string> cliArgs = GetCommandLineArgsUtf8();
    const bool directMode = !cliArgs.empty();

    if (directMode) {
        LoadColorConfig();
        SetConsoleTextAttribute(hConsole, L_default);
        LightApi_Init();

        SecurityCore directSecurity;
        g_SecurityCore = &directSecurity;
        g_PluginManager = PluginManager::GetInstance();

        std::string directPluginsDir = cd + "\\plugins\\";
        CreateDirectoryA(directPluginsDir.c_str(), nullptr);
        g_PluginManager->LoadPluginsFromDirectory(directPluginsDir, true);
        SilentLoadApiPlugins(cd);

        std::string directRegistryPath = cd + "\\rppcst\\sys.xayx";
        std::string directFolderPath = "C:\\Program Files\\.light";
        std::string directCommand = JoinCommandLineArgs(cliArgs);

        ExecuteOneCommand(directCommand, cd,
            &directSecurity, g_PluginManager,
            directPluginsDir, directRegistryPath, directFolderPath);

        if (g_PluginManager) g_PluginManager->UnloadAllPlugins(true);
        LightApi_Shutdown();
        SetConsoleTextAttribute(hConsole, L_default);
        return 0;
    }

    const bool guiModeRequested = IsGuiModeConfigured();
    HWND hConsoleWindow = GetConsoleWindow();
    if (guiModeRequested && hConsoleWindow) {
        ShowWindow(hConsoleWindow, SW_HIDE);
    }

    LoadColorConfig();
    SetConsoleTextAttribute(hConsole, L_default);

    // 初始化 LightApi 附属网络接口模块（供同进程插件 DLL 调用）
    LightApi_Init();

    // 插件管理器尽早建立，用于静默加载程序目录下的 api_*.dll 指令集。
    g_PluginManager = PluginManager::GetInstance();

    string lightPath = cd + "light.exe";
    string exePath = cd + "\\rppcst\\CEL.exe";
    string vbsPath = cd + "\\rppcst\\a1.vbs";
    string fullPortTool = cd + "\\rppcst\\Full_Port_Network_Packet_Capture_Tool.exe";
    string registryPath = cd + "\\rppcst\\sys.xayx";
    string filePath = "C:\\Program Files\\.light\\light.xbyx";
    string betPath = cd + "\\rppcst\\light.exe";
    string Registry = cd + "\\Registry.exe";
    string BinaryHexEditor = cd + "\\BinaryHexEditor.exe";
    string folderPath = "C:\\Program Files\\.light";
    string savecj = cd + "\\Cache\\ModMigrator\\";
    string d_savecj = cd + "\\Cache\\";

    cout << "_________________________________________________________________________________________________" << endl;
    cout << "===========================================Hello itc!============================================" << endl;
    cout << "_________________________________________________________________________________________________" << endl;

    // 静默扫描并加载程序目录下的 api_*.dll 新增指令集
    // （PE/导入表/附属插件安全校验在 PluginManager 内部完成，失败仅记录调试日志）。
    SilentLoadApiPlugins(cd);

    bool fileExists = FileExists(filePath);

    if (!fileExists) {
        if (!IsRunningAsAdmin()) ElevateCurrentProgram();
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "正在初始化配置状态..." << endl;
        // 配置初始化阶段同时补扫 api_*.dll（幂等，已加载的不会重复）。
        SilentLoadApiPlugins(cd);
        StartSpinner();
        EnableSecurityPrivileges();
        UnlockFolderForAdmin(folderPath, true);   // 如果之前被锁过，先解锁，避免访问不了
        fileExists = FileExists(filePath);
        if (fileExists) {
            HideFolderOnly(folderPath, true);      // ← 改成只隐藏
            StopSpinner();
        }
        else StopSpinner();
    }
    if (!fileExists) {
        if (!IsRunningAsAdmin()) {
            ElevateCurrentProgram();
        }
        int cs = 0;
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "正在准备中..." << endl;
        StartSpinner();
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> dis(8000, 20000);
        CheckAndInstallService();
        Sleep(dis(gen));
        StopSpinner();
        Sleep(dis(gen));
        Sleep(3000);
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "即将为你配置环境..." << endl;
        StartSpinner();
        Sleep(dis(gen));
        UpdateSecurityNeValue_it();
        Sleep(dis(gen));
        StopSpinner();
        Sleep(3000);
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "开始为你配置环境，请耐心等待..." << endl;
        StartSpinner();
        Sleep(dis(gen));
        if (!peizhi()) {
            StopSpinner();
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "环境配置失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            cout << "程序即将关闭..." << endl;
            Sleep(5000);
            ExitProcess(0);
        }
        else {
            Sleep(dis(gen));
            StopSpinner();
            Sleep(3000);
        }
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "正在做最后的部署，请耐心等待..." << endl;
        StartSpinner();
        Sleep(dis(gen));
        CreateDirectoryA(folderPath.c_str(), NULL);
        EnableSecurityPrivileges();
        if (LockFolderWithTrustedInstaller(folderPath)) { Sleep(3000); }
        else { SetConsoleTextAttribute(hConsole, L_error); cs = cs + 1; cout << "出现1个失败" << endl; SetConsoleTextAttribute(hConsole, L_default); }
        if (Lock_ProtectFolderRecursively(folderPath)) {
            SetConsoleTextAttribute(hConsole, L_success);
            cout << "环境配置完成" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
        else {
            SetConsoleTextAttribute(hConsole, L_error);
            cs = cs + 1;
            cout << "出现" << cs << "个失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
    }

    if (!IsRunningAsAdmin()) {
        ElevateCurrentProgram();
        EnableSecurityPrivileges();
    }

    // ==================== 初始化安全核心 ====================
    SecurityCore securityCore;
    g_SecurityCore = &securityCore;

    // ==================== 初始化通知窗口 ====================
    g_NotifyWindow = NotifyWindow::GetInstance();
    if (g_NotifyWindow) {
        g_NotifyWindow->SetDisplayDuration(3000);
        g_NotifyWindow->SetMaxQueueSize(10);
        OutputDebugStringA("[Main] NotifyWindow initialized\n");
    }
    else {
        OutputDebugStringA("[Main] Failed to initialize NotifyWindow\n");
    }

    // ==================== 初始化网络拦截器 ====================
    g_NetworkInterceptor = NetworkInterceptor::GetInstance();
    if (g_NetworkInterceptor) {
        string whitelistPath = cd + "\\whitelist.json";
        ifstream wlFile(whitelistPath);
        if (wlFile.is_open()) {
            string line;
            while (getline(wlFile, line)) {
                if (!line.empty() && line[0] != '#') {
                    g_NetworkInterceptor->AddWhitelistDomain(line);
                }
            }
            wlFile.close();
        }

        g_NetworkInterceptor->ClearAllBlocks();

        g_NetworkInterceptor->SetBlockCallback([](const std::string& domain, const std::string& processName, DWORD pid) {
            OutputDebugStringA(("[Callback] Blocked: " + processName + " -> " + domain + "\n").c_str());
            if (g_NotifyWindow) {
                g_NotifyWindow->ShowBlockNotification(processName, domain);
            }
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[拦截] " << processName << " (PID:" << pid << ") -> " << domain << endl;
            SetConsoleTextAttribute(hConsole, L_default);
            });

        if (g_NetworkInterceptor->Start()) {
        }
        else {
            SetConsoleTextAttribute(hConsole, L_error);
            cout << "[安全核心] 网络防护启动失败" << endl;
            SetConsoleTextAttribute(hConsole, L_default);
        }
    }

    GetSecurityNeValue();
    string securityNe = GetSecurityNeValue();
    string url_bin = "https://raw.githubusercontent.com/yuxuan-gzs/Forge-NeoForge-and-Fabric/security/PM%20%E6%81%B6%E6%84%8F%E7%BD%91%E5%9D%80%E6%8B%A6%E6%88%AA%E5%BA%93Pro_V" + securityNe + ".zip";
    if (CheckRemoteFileExists(url_bin)) {
        SetConsoleTextAttribute(hConsole, L_default);
        cout << "安全库更新中..." << endl;
        StartSpinner();
        securityCore.UpdateFromGitHub();
        StopSpinner();
    }

    // ==================== 初始化插件管理器 ====================
    g_PluginManager = PluginManager::GetInstance();

    PluginLoadCallback callback;
    callback.onLoad = [](const std::string& pluginName, bool success, const std::string& errorMsg) {
        if (success) {
            SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), L_success);
            cout << "[插件] 加载成功: " << pluginName << endl;
        }
        else {
            SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), L_error);
            cout << "[插件] 加载失败: " << pluginName << " - " << errorMsg << endl;
        }
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), L_default);
        };

    callback.onUnload = [](const std::string& pluginName) {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), L_warning);
        cout << "[插件] 已卸载: " << pluginName << endl;
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), L_default);
        };

    callback.onBatchProgress = [](int total, int loaded, int failed) {};

    g_PluginManager->SetLoadCallback(callback);

    string pluginsDir = cd + "\\plugins\\";
    CreateDirectoryA(pluginsDir.c_str(), NULL);
    //g_PluginManager->LoadPluginsFromDirectory(pluginsDir, true);
    g_PluginManager->LoadPluginsFromDirectory(pluginsDir, false);

    

    // ============================================================
    // GUI 模式仅由 Cache\g.xayx 的解密内容决定。
    // GUI 提供者偏好（Cache\gui_provider.txt）：
    //   auto     外部 GUI 插件优先，找不到时使用内置 GUI（默认）
    //   external 优先外部 GUI 插件，失败回退内置
    //   builtin  始终使用编入 light.exe 的内置 GUI
    // 修复：旧机制总是先启动内置 GUI，导致外置 GUI 插件永远无法加载。
    // ============================================================
    if (guiModeRequested) {
        HINSTANCE hInstance = GetModuleHandleW(nullptr);
        PluginUIManager uiManager;
        g_PluginManager->SetUIManager(&uiManager);

        auto cleanupAfterGui = [&]() {
            if (g_NetworkInterceptor) {
                g_NetworkInterceptor->Stop();
                g_NetworkInterceptor = nullptr;
            }
            if (g_PluginManager) {
                g_PluginManager->UnloadAllPlugins(false);
            }
            SetConsoleTextAttribute(hConsole, L_default);
        };

        // 内置 GUI：进程内对象，生命周期覆盖整个 GUI 消息循环。
        auto tryEmbeddedGui = [&]() -> bool {
            YXGuiPlugin embeddedGui;
            embeddedGui.SetUIManager(&uiManager);
            const bool initialized = embeddedGui.Initialize();
            if (!initialized) return false;
            embeddedGui.OnLoad();
            const bool started = RunGuiMode(
                hInstance, static_cast<IGuiProvider*>(&embeddedGui),
                &securityCore, g_PluginManager,
                pluginsDir, registryPath, folderPath);
            embeddedGui.OnUnload();
            embeddedGui.Shutdown();
            return started;
        };

        // 外置 GUI 插件：plugins\*.dll 中实现 IGuiProvider 的模块。
        auto tryExternalGui = [&]() -> bool {
            auto externalGui = g_PluginManager->FindGuiPlugin();
            if (externalGui.provider == nullptr) return false;

            // 与内置 GUI 同 ID（com.yx.gui）的旧版外置 DLL 属于重复界面：
            // 直接回退到内置新界面，避免旧 UI 掩盖本次的侧边栏/设置面板修复。
            if (externalGui.pluginId == "com.yx.gui") {
                OutputDebugStringA(
                    "[GUI] 跳过与内置 GUI 重复的外置插件 (com.yx.gui)\n");
                return false;
            }

            return RunGuiMode(
                hInstance, externalGui.provider,
                &securityCore, g_PluginManager,
                pluginsDir, registryPath, folderPath);
        };

        const std::string guiPref = GetGuiProviderPreference();
        bool guiStarted = false;
        if (guiPref == "builtin") {
            guiStarted = tryEmbeddedGui();
        }
        else {
            guiStarted = tryExternalGui();
            if (!guiStarted) guiStarted = tryEmbeddedGui();
        }

        if (guiStarted) {
            cleanupAfterGui();
            return 0;
        }

        // GUI 配置已打开但内置及外部 GUI 均失败，恢复控制台以便诊断。
        g_guiProvider = nullptr;
        g_guiMode = false;
        if (hConsoleWindow && IsWindow(hConsoleWindow)) {
            ShowWindow(hConsoleWindow, SW_SHOW);
        }
        OutputDebugStringA("[GUI] Embedded and external GUI providers failed; using console mode.\n");
    }


    // ==================== 主循环（控制台模式） ====================
    while (true) {
        SetConsoleCtrlHandler(CtrlHandler, TRUE);

        PrintPrompt(cd);
        string command;
        getline(cin, command);

        if (cin.fail()) {
            cout << "\r" << string(80, ' ') << "\r";
            cin.clear();
            continue;
        }

        // 使用统一的命令执行器
        bool keepRunning = ExecuteOneCommand(
            command, cd, &securityCore, g_PluginManager,
            pluginsDir, registryPath, folderPath);

        if (!keepRunning) {
            // exit 命令
            break;
        }
    }

    // ==================== 清理资源 ====================
    if (g_NetworkInterceptor) {
        g_NetworkInterceptor->Stop();
        g_NetworkInterceptor = nullptr;
    }

    if (g_PluginManager) {
        SetConsoleTextAttribute(hConsole, L_default);
        g_PluginManager->UnloadAllPlugins(false);
    }

    SetConsoleTextAttribute(hConsole, L_default);

    return 0;
}