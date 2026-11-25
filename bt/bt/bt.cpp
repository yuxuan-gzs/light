#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <shlobj.h>
#include <direct.h>
#include <shellapi.h>
#include <shlwapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

// 颜色宏定义
#define COLOR_WHITE  FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
#define COLOR_GREEN  FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define COLOR_RED    FOREGROUND_RED | FOREGROUND_INTENSITY
#define COLOR_YELLOW FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define COLOR_CYAN   FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY

HANDLE hConsole;

void SetColor(WORD color) {
    SetConsoleTextAttribute(hConsole, color);
}

std::string GetProgramDir() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string fullPath(buffer);
    size_t lastSlash = fullPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return fullPath.substr(0, lastSlash);
    }
    return fullPath;
}

std::string GetParentDir(const std::string& dir) {
    size_t lastSlash = dir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return dir.substr(0, lastSlash);
    }
    return dir;
}

void RequireAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    if (!isAdmin) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        ShellExecuteA(NULL, "runas", exePath, NULL, NULL, SW_SHOWNORMAL);
        ExitProcess(0);
    }
}

std::string CopyIconToSystem(const std::string& sourceIcon) {
    std::string destDir = "C:\\Program Files\\.light";
    std::string destIcon = destDir + "\\light.ico";
    CreateDirectoryA(destDir.c_str(), NULL);
    if (CopyFileA(sourceIcon.c_str(), destIcon.c_str(), FALSE)) {
        SetFileAttributesA(destIcon.c_str(), FILE_ATTRIBUTE_HIDDEN);
        return destIcon;
    }
    return "";
}

bool RegisterFileType(const std::string& extension, const std::string& description,
    const std::string& iconPath, const std::string& openExePath = "") {
    HKEY hKey;
    LONG result;
    std::string extKey = "." + extension;
    std::string progID = "light." + extension;

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
        std::string iconKey = progID + "\\DefaultIcon";
        result = RegCreateKeyA(HKEY_CLASSES_ROOT, iconKey.c_str(), &hKey);
        if (result == ERROR_SUCCESS) {
            RegSetValueA(hKey, NULL, REG_SZ, iconPath.c_str(), (DWORD)iconPath.size());
            RegCloseKey(hKey);
        }
    }

    // 设置打开方式
    if (!openExePath.empty()) {
        std::string openCmd = "\"" + openExePath + "\" \"%1\"";
        std::string shellKey = progID + "\\shell\\open\\command";
        result = RegCreateKeyA(HKEY_CLASSES_ROOT, shellKey.c_str(), &hKey);
        if (result == ERROR_SUCCESS) {
            RegSetValueA(hKey, NULL, REG_SZ, openCmd.c_str(), (DWORD)openCmd.size());
            RegCloseKey(hKey);
        }

        // 设置 UserChoice
        std::string userChoiceKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\."
            + extension + "\\UserChoice";
        HKEY hUserKey;
        if (RegCreateKeyA(HKEY_CURRENT_USER, userChoiceKey.c_str(), &hUserKey) == ERROR_SUCCESS) {
            RegSetValueExA(hUserKey, "Progid", 0, REG_SZ,
                (BYTE*)progID.c_str(), (DWORD)progID.size() + 1);
            RegCloseKey(hUserKey);
        }
    }

    return true;
}

bool RegisterApplication(const std::string& appName, const std::string& appPath,
    const std::string& uninstallPath, const std::string& iconPath,
    const std::string& version) {
    HKEY hKey;
    LONG result;
    std::string keyPath = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + appName;

    result = RegCreateKeyA(HKEY_LOCAL_MACHINE, keyPath.c_str(), &hKey);
    if (result != ERROR_SUCCESS) return false;

    RegSetValueExA(hKey, "DisplayName", 0, REG_SZ, (BYTE*)appName.c_str(), (DWORD)appName.size() + 1);
    RegSetValueExA(hKey, "DisplayVersion", 0, REG_SZ, (BYTE*)version.c_str(), (DWORD)version.size() + 1);
    RegSetValueExA(hKey, "Publisher", 0, REG_SZ, (BYTE*)"light", 5);
    RegSetValueExA(hKey, "InstallLocation", 0, REG_SZ, (BYTE*)appPath.c_str(), (DWORD)appPath.size() + 1);
    RegSetValueExA(hKey, "UninstallString", 0, REG_SZ, (BYTE*)uninstallPath.c_str(), (DWORD)uninstallPath.size() + 1);
    if (!iconPath.empty()) {
        RegSetValueExA(hKey, "DisplayIcon", 0, REG_SZ, (BYTE*)iconPath.c_str(), (DWORD)iconPath.size() + 1);
    }
    DWORD noModify = 1;
    RegSetValueExA(hKey, "NoModify", 0, REG_DWORD, (BYTE*)&noModify, sizeof(DWORD));
    RegSetValueExA(hKey, "NoRepair", 0, REG_DWORD, (BYTE*)&noModify, sizeof(DWORD));
    RegCloseKey(hKey);
    return true;
}

int main() {
    //RequireAdmin();

    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetColor(COLOR_WHITE);

    std::string programDir = GetProgramDir();
    std::string parentDir = GetParentDir(programDir);

    std::string lightExe = parentDir + "\\light.exe";
    std::string uninstallExe = parentDir + "\\rppcst\\light_Uninstall.exe";
    std::string sourceIcon = programDir + "\\rppcst\\favicon.ico";
    std::string hiddenDir = "C:\\Program Files\\.light";
    std::string binaryHexEditor = parentDir + "\\BinaryHexEditor.exe";

    SetColor(COLOR_WHITE);
    std::cout << "[*] 创建隐藏目录..." << std::endl;
    if (CreateDirectoryA(hiddenDir.c_str(), NULL)) {
        SetFileAttributesA(hiddenDir.c_str(), FILE_ATTRIBUTE_HIDDEN);
        SetColor(COLOR_GREEN);
        std::cout << "[OK] 创建目录: " << hiddenDir << std::endl;
    }
    else if (GetLastError() == ERROR_ALREADY_EXISTS) {
        SetFileAttributesA(hiddenDir.c_str(), FILE_ATTRIBUTE_HIDDEN);
        SetColor(COLOR_GREEN);
        std::cout << "[OK] 目录已存在: " << hiddenDir << std::endl;
    }
    else {
        SetColor(COLOR_RED);
        std::cout << "[X] 创建目录失败，错误码: " << GetLastError() << std::endl;
    }

    SetColor(COLOR_WHITE);
    std::cout << "[*] 复制图标..." << std::endl;
    std::string iconPath = CopyIconToSystem(sourceIcon);
    if (!iconPath.empty()) {
        SetColor(COLOR_GREEN);
        std::cout << "[OK] 图标已保存" << std::endl;
    }
    else {
        SetColor(COLOR_YELLOW);
        std::cout << "[!] 未找到 favicon.ico，跳过" << std::endl;
    }

    SetColor(COLOR_WHITE);
    std::cout << "[*] 创建加密文件..." << std::endl;
    std::string xbyxFile = hiddenDir + "\\light.xbyx";
    std::ofstream file(xbyxFile);
    if (file.is_open()) {
        file.close();
        SetFileAttributesA(xbyxFile.c_str(), FILE_ATTRIBUTE_HIDDEN);
        SetColor(COLOR_GREEN);
        std::cout << "[OK] 创建文件: light.xbyx" << std::endl;
    }
    else {
        SetColor(COLOR_RED);
        std::cout << "[X] 创建文件失败" << std::endl;
    }

    SetColor(COLOR_WHITE);
    std::cout << "[*] 注册文件类型..." << std::endl;

    // 检查 BinaryHexEditor.exe 是否存在
    bool hexEditorExists = (GetFileAttributesA(binaryHexEditor.c_str()) != INVALID_FILE_ATTRIBUTES);
    std::string openExePath = hexEditorExists ? binaryHexEditor : lightExe;

    bool xayxOk = RegisterFileType("xayx", "light 1级文件", iconPath, openExePath);
    bool xbyxOk = RegisterFileType("xbyx", "light 2级文件", iconPath, openExePath);
    bool xcyxOk = RegisterFileType("xcyx", "light 3级文件", iconPath, openExePath);

    if (xayxOk) { SetColor(COLOR_GREEN); std::cout << "[OK] .xayx - 1级文件 (打开程序: " << openExePath << ")" << std::endl; }
    else { SetColor(COLOR_RED);   std::cout << "[X] .xayx - 注册失败" << std::endl; }

    if (xbyxOk) { SetColor(COLOR_GREEN); std::cout << "[OK] .xbyx - 2级文件" << std::endl; }
    else { SetColor(COLOR_RED);   std::cout << "[X] .xbyx - 注册失败" << std::endl; }

    if (xcyxOk) { SetColor(COLOR_GREEN); std::cout << "[OK] .xcyx - 3级文件" << std::endl; }
    else { SetColor(COLOR_RED);   std::cout << "[X] .xcyx - 注册失败" << std::endl; }

    // 刷新图标缓存
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    SetColor(COLOR_WHITE);
    std::cout << "[*] 注册应用程序..." << std::endl;
    if (RegisterApplication("light", lightExe, uninstallExe, iconPath, "1.0.0")) {
        SetColor(COLOR_GREEN);
        std::cout << "[OK] 已添加到应用和功能" << std::endl;
    }
    else {
        SetColor(COLOR_RED);
        std::cout << "[X] 注册应用程序失败" << std::endl;
    }

    SetColor(COLOR_CYAN);
    std::cout << "\n[*] 完成！BinaryHexEditor.exe 已设置为 .xayx/.xbyx/.xcyx 的默认打开程序" << std::endl;

    SetColor(COLOR_WHITE);
    system("pause");
    return 0;
}