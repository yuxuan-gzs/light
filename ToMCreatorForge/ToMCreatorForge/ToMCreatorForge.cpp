// ToMCreatorForge.cpp - 完整版
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <atomic>
#include <sstream>
#include <vector>
#include <algorithm>
#include "resource.h"
#include "ConverterCore.h"
#include "ConfigManager.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// 全局句柄
HWND g_hDlg = nullptr;
HWND g_hEditSource = nullptr;
HWND g_hEditOutput = nullptr;
HWND g_hEditLog = nullptr;
HWND g_hProgress = nullptr;
HWND g_hBtnConvert = nullptr;

// 复选框句柄
HWND g_hCheckEvents = nullptr;
HWND g_hCheckCaps = nullptr;
HWND g_hCheckNetwork = nullptr;
HWND g_hCheckZip = nullptr;
HWND g_hCheckSource = nullptr;
HWND g_hCheckDocs = nullptr;
HWND g_hCheckRemember = nullptr;

// 按钮句柄
HWND g_hBtnSaveConfig = nullptr;
HWND g_hBtnLoadConfig = nullptr;
HWND g_hBtnClearLog = nullptr;
HWND g_hBtnOpenOutput = nullptr;
HWND g_hBtnAbout = nullptr;

std::atomic<bool> g_isConverting(false);
std::string g_currentOutputPath;
AppConfig g_appConfig;

// 向日志框追加文本（线程安全）
// 向日志框追加文本（同步，实时显示）
void AppendLog(const std::string& text) {
    if (!g_hEditLog) return;

    std::string logLine = text + "\r\n";

    // 获取当前文本长度
    int len = GetWindowTextLengthA(g_hEditLog);

    // 选中末尾
    SendMessageA(g_hEditLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);

    // 使用 EM_REPLACESEL 追加文本（同步）
    SendMessageA(g_hEditLog, EM_REPLACESEL, FALSE, (LPARAM)logLine.c_str());

    // 滚动到末尾
    SendMessageA(g_hEditLog, EM_SCROLLCARET, 0, 0);

    // 强制刷新
    UpdateWindow(g_hEditLog);
}

// 更新进度条（线程安全）
void UpdateProgress(int value) {
    if (g_hProgress) {
        SendMessage(g_hProgress, PBM_SETPOS, (WPARAM)value, 0);
    }
}

// 获取复选框状态
ConvertOptions GetConvertOptions() {
    ConvertOptions options;
    options.generateEvents = (SendMessage(g_hCheckEvents, BM_GETCHECK, 0, 0) == BST_CHECKED);
    options.generateCaps = (SendMessage(g_hCheckCaps, BM_GETCHECK, 0, 0) == BST_CHECKED);
    options.generateNetwork = (SendMessage(g_hCheckNetwork, BM_GETCHECK, 0, 0) == BST_CHECKED);
    options.exportZip = (SendMessage(g_hCheckZip, BM_GETCHECK, 0, 0) == BST_CHECKED);
    options.includeSource = (SendMessage(g_hCheckSource, BM_GETCHECK, 0, 0) == BST_CHECKED);
    options.generateDocs = (SendMessage(g_hCheckDocs, BM_GETCHECK, 0, 0) == BST_CHECKED);
    return options;
}

// 设置复选框状态
void SetConvertOptions(const ConvertOptions& options) {
    SendMessage(g_hCheckEvents, BM_SETCHECK, options.generateEvents ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hCheckCaps, BM_SETCHECK, options.generateCaps ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hCheckNetwork, BM_SETCHECK, options.generateNetwork ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hCheckZip, BM_SETCHECK, options.exportZip ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hCheckSource, BM_SETCHECK, options.includeSource ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hCheckDocs, BM_SETCHECK, options.generateDocs ? BST_CHECKED : BST_UNCHECKED, 0);
}

// 转换线程函数
DWORD WINAPI ConvertThread(LPVOID lpParam) {
    g_isConverting = true;
    EnableWindow(g_hBtnConvert, FALSE);

    char sourcePath[MAX_PATH] = { 0 };
    char outputPath[MAX_PATH] = { 0 };
    GetWindowTextA(g_hEditSource, sourcePath, MAX_PATH);
    GetWindowTextA(g_hEditOutput, outputPath, MAX_PATH);
    g_currentOutputPath = outputPath;

    ConvertOptions options = GetConvertOptions();

    AppendLog("========================================");
    AppendLog("  开始转换 - " + std::string(__TIMESTAMP__));
    AppendLog("========================================");
    AppendLog("[配置] 生成事件: " + std::string(options.generateEvents ? "是" : "否"));
    AppendLog("[配置] 生成Capability: " + std::string(options.generateCaps ? "是" : "否"));
    AppendLog("[配置] 生成网络包: " + std::string(options.generateNetwork ? "是" : "否"));
    AppendLog("[配置] 导出ZIP: " + std::string(options.exportZip ? "是" : "否"));
    AppendLog("[配置] 包含源码: " + std::string(options.includeSource ? "是" : "否"));
    AppendLog("[配置] 生成文档: " + std::string(options.generateDocs ? "是" : "否"));

    ConverterCore core;
    bool result = core.Convert(
        sourcePath,
        outputPath,
        [](const std::string& msg) { AppendLog(msg); },
        [](int progress) { UpdateProgress(progress); },
        options
    );

    if (!result) {
        AppendLog("[错误] 转换失败，请检查路径和权限");
    }

    AppendLog("========================================");
    AppendLog("  转换完成！");
    AppendLog("========================================");

    // 保存配置（如果启用记住路径）
    if (SendMessage(g_hCheckRemember, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        g_appConfig.lastSourcePath = sourcePath;
        g_appConfig.lastOutputPath = outputPath;
        g_appConfig.options = options;
        ConfigManager::SaveConfig(g_appConfig);
    }

    g_isConverting = false;
    EnableWindow(g_hBtnConvert, TRUE);
    return 0;
}

// 浏览文件夹对话框
void BrowseFolder(HWND hEdit) {
    BROWSEINFOA bi = { 0 };
    bi.lpszTitle = "请选择文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            SetWindowTextA(hEdit, path);
        }
        CoTaskMemFree(pidl);
    }
}

// 检查路径是否有效
bool ValidatePaths() {
    char sourcePath[MAX_PATH] = { 0 };
    char outputPath[MAX_PATH] = { 0 };
    GetWindowTextA(g_hEditSource, sourcePath, MAX_PATH);
    GetWindowTextA(g_hEditOutput, outputPath, MAX_PATH);

    if (strlen(sourcePath) == 0) {
        MessageBoxA(g_hDlg, "请选择源模组文件夹", "输入错误", MB_OK | MB_ICONWARNING);
        return false;
    }

    if (strlen(outputPath) == 0) {
        MessageBoxA(g_hDlg, "请选择目标工作区文件夹", "输入错误", MB_OK | MB_ICONWARNING);
        return false;
    }

    DWORD attrs = GetFileAttributesA(sourcePath);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxA(g_hDlg, "源文件夹不存在或不是目录", "路径错误", MB_OK | MB_ICONERROR);
        return false;
    }

    return true;
}

// 打开输出文件夹
void OpenOutputFolder() {
    char outputPath[MAX_PATH] = { 0 };
    GetWindowTextA(g_hEditOutput, outputPath, MAX_PATH);

    if (strlen(outputPath) == 0) {
        MessageBoxA(g_hDlg, "请先选择输出文件夹", "提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 检查文件夹是否存在
    DWORD attrs = GetFileAttributesA(outputPath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        // 创建文件夹
        if (!CreateDirectoryA(outputPath, NULL)) {
            MessageBoxA(g_hDlg, "无法创建输出文件夹", "错误", MB_OK | MB_ICONERROR);
            return;
        }
    }

    // 打开文件夹
    ShellExecuteA(NULL, "open", outputPath, NULL, NULL, SW_SHOW);
}

// 显示关于对话框
void ShowAbout() {
    MessageBoxA(g_hDlg,
        "Forge/NeoForge → MCreator 转换器 v0.4\n\n"
        "功能特性:\n"
        "• 解析 Forge 1.16+ 和 NeoForge 模组\n"
        "• 识别 @Mod, @SubscribeEvent, @ObjectHolder\n"
        "• 解析 DeferredRegister 注册\n"
        "• 生成 MCreator 兼容的元素文件\n"
        "• 支持事件、Capability、网络包识别\n\n"
        "使用方法:\n"
        "1. 选择 Forge/NeoForge 模组源码文件夹\n"
        "2. 选择 MCreator 工作区文件夹\n"
        "3. 配置转换选项\n"
        "4. 点击\"开始转换\"\n\n"
        "作者: yuxuangzs\n"
        "版本: 0.4\n"
        "许可证: MIT",
        "关于", MB_OK | MB_ICONINFORMATION);
}

// 清空日志
void ClearLog() {
    SetWindowTextA(g_hEditLog, "");
    SendMessage(g_hProgress, PBM_SETPOS, 0, 0);
}

// 保存配置
void SaveConfig() {
    char sourcePath[MAX_PATH] = { 0 };
    char outputPath[MAX_PATH] = { 0 };
    GetWindowTextA(g_hEditSource, sourcePath, MAX_PATH);
    GetWindowTextA(g_hEditOutput, outputPath, MAX_PATH);

    g_appConfig.lastSourcePath = sourcePath;
    g_appConfig.lastOutputPath = outputPath;
    g_appConfig.options = GetConvertOptions();
    g_appConfig.options.exportZip = (SendMessage(g_hCheckZip, BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (ConfigManager::SaveConfig(g_appConfig)) {
        AppendLog("[配置] 配置已保存");
        MessageBoxA(g_hDlg, "配置已保存", "成功", MB_OK | MB_ICONINFORMATION);
    }
    else {
        AppendLog("[错误] 保存配置失败");
        MessageBoxA(g_hDlg, "保存配置失败", "错误", MB_OK | MB_ICONERROR);
    }
}

// 加载配置
void LoadConfig() {
    AppConfig config;
    if (ConfigManager::LoadConfig(config)) {
        g_appConfig = config;

        if (!config.lastSourcePath.empty()) {
            SetWindowTextA(g_hEditSource, config.lastSourcePath.c_str());
        }
        if (!config.lastOutputPath.empty()) {
            SetWindowTextA(g_hEditOutput, config.lastOutputPath.c_str());
        }

        SetConvertOptions(config.options);
        SendMessage(g_hCheckZip, BM_SETCHECK, config.options.exportZip ? BST_CHECKED : BST_UNCHECKED, 0);

        AppendLog("[配置] 配置已加载");
        MessageBoxA(g_hDlg, "配置已加载", "成功", MB_OK | MB_ICONINFORMATION);
    }
    else {
        AppendLog("[配置] 未找到配置文件，使用默认设置");
        MessageBoxA(g_hDlg, "未找到配置文件，使用默认设置", "提示", MB_OK | MB_ICONINFORMATION);
    }
}

// 对话框过程
INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        g_hDlg = hDlg;
        g_hEditSource = GetDlgItem(hDlg, IDC_EDIT_SOURCE);
        g_hEditOutput = GetDlgItem(hDlg, IDC_EDIT_OUTPUT);
        g_hEditLog = GetDlgItem(hDlg, IDC_EDIT_LOG);
        g_hProgress = GetDlgItem(hDlg, IDC_PROGRESS);
        g_hBtnConvert = GetDlgItem(hDlg, IDC_BTN_CONVERT);

        g_hCheckEvents = GetDlgItem(hDlg, IDC_CHECK_EVENTS);
        g_hCheckCaps = GetDlgItem(hDlg, IDC_CHECK_CAPS);
        g_hCheckNetwork = GetDlgItem(hDlg, IDC_CHECK_NETWORK);
        g_hCheckZip = GetDlgItem(hDlg, IDC_CHECK_ZIP);
        g_hCheckSource = GetDlgItem(hDlg, IDC_CHECK_SOURCE);
        g_hCheckDocs = GetDlgItem(hDlg, IDC_CHECK_DOCS);
        g_hCheckRemember = GetDlgItem(hDlg, IDC_CHECK_REMEMBER);

        g_hBtnSaveConfig = GetDlgItem(hDlg, IDC_BTN_SAVE_CONFIG);
        g_hBtnLoadConfig = GetDlgItem(hDlg, IDC_BTN_LOAD_CONFIG);
        g_hBtnClearLog = GetDlgItem(hDlg, IDC_BTN_CLEAR_LOG);
        g_hBtnOpenOutput = GetDlgItem(hDlg, IDC_BTN_OPEN_OUTPUT);
        g_hBtnAbout = GetDlgItem(hDlg, IDC_BTN_ABOUT);

        // 设置默认复选框状态
        SendMessage(g_hCheckEvents, BM_SETCHECK, BST_CHECKED, 0);
        SendMessage(g_hCheckCaps, BM_SETCHECK, BST_CHECKED, 0);
        SendMessage(g_hCheckNetwork, BM_SETCHECK, BST_CHECKED, 0);
        SendMessage(g_hCheckZip, BM_SETCHECK, BST_CHECKED, 0);
        SendMessage(g_hCheckSource, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessage(g_hCheckDocs, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessage(g_hCheckRemember, BM_SETCHECK, BST_CHECKED, 0);

        // 初始化进度条
        SendMessage(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(g_hProgress, PBM_SETSTEP, (WPARAM)1, 0);

        // 设置图标
        HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
        if (hIcon) {
            SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        }

        // 加载配置
        LoadConfig();

        AppendLog("[信息] 程序已启动 v0.4");
        AppendLog("[信息] 支持 Forge 1.16+ 和 NeoForge 模组");
        AppendLog("[信息] 请选择源模组和目标工作区");
        return TRUE;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDC_BTN_BROWSE_SOURCE:
            BrowseFolder(g_hEditSource);
            break;

        case IDC_BTN_BROWSE_OUTPUT:
            BrowseFolder(g_hEditOutput);
            break;

        case IDC_BTN_CONVERT: {
            if (g_isConverting) {
                AppendLog("[警告] 转换正在进行中...");
                break;
            }

            if (!ValidatePaths()) {
                break;
            }

            ClearLog();
            HANDLE hThread = CreateThread(NULL, 0, ConvertThread, NULL, 0, NULL);
            if (hThread) {
                CloseHandle(hThread);
                AppendLog("[信息] 转换线程已启动");
            }
            else {
                AppendLog("[错误] 无法创建转换线程");
                MessageBoxA(hDlg, "无法创建转换线程", "错误", MB_OK | MB_ICONERROR);
            }
            break;
        }

        case IDC_BTN_SAVE_CONFIG:
            SaveConfig();
            break;

        case IDC_BTN_LOAD_CONFIG:
            LoadConfig();
            break;

        case IDC_BTN_CLEAR_LOG:
            ClearLog();
            AppendLog("[信息] 日志已清空");
            break;

        case IDC_BTN_OPEN_OUTPUT:
            OpenOutputFolder();
            break;

        case IDC_BTN_ABOUT:
            ShowAbout();
            break;

        case IDC_CHECK_EVENTS:
        case IDC_CHECK_CAPS:
        case IDC_CHECK_NETWORK:
        case IDC_CHECK_ZIP:
        case IDC_CHECK_SOURCE:
        case IDC_CHECK_DOCS:
        case IDC_CHECK_REMEMBER:
            break;

        case IDCANCEL:
            if (g_isConverting) {
                int result = MessageBoxA(hDlg, "转换正在进行中，确定要退出吗？", "确认退出", MB_YESNO | MB_ICONQUESTION);
                if (result != IDYES) break;
            }
            EndDialog(hDlg, 0);
            break;
        }
        break;
    }

    case WM_DROPFILES: {
        // 支持拖放文件
        HDROP hDrop = (HDROP)wParam;
        char fileName[MAX_PATH];
        DragQueryFileA(hDrop, 0, fileName, MAX_PATH);
        DragFinish(hDrop);

        // 检查是否为文件夹
        DWORD attrs = GetFileAttributesA(fileName);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            SetWindowTextA(g_hEditSource, fileName);
            AppendLog("[信息] 拖放文件夹: " + std::string(fileName));
        }
        else {
            MessageBoxA(g_hDlg, "请拖放文件夹，不是文件", "提示", MB_OK | MB_ICONINFORMATION);
        }
        break;
    }

    case WM_CLOSE:
        if (g_isConverting) {
            int result = MessageBoxA(hDlg, "转换正在进行中，确定要退出吗？", "确认退出", MB_YESNO | MB_ICONQUESTION);
            if (result != IDYES) return TRUE;
        }
        // 保存配置
        if (SendMessage(g_hCheckRemember, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            char sourcePath[MAX_PATH] = { 0 };
            char outputPath[MAX_PATH] = { 0 };
            GetWindowTextA(g_hEditSource, sourcePath, MAX_PATH);
            GetWindowTextA(g_hEditOutput, outputPath, MAX_PATH);
            g_appConfig.lastSourcePath = sourcePath;
            g_appConfig.lastOutputPath = outputPath;
            g_appConfig.options = GetConvertOptions();
            ConfigManager::SaveConfig(g_appConfig);
        }
        EndDialog(hDlg, 0);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return FALSE;
}

// WinMain 入口
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    INT_PTR result = DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN_DIALOG), NULL, DialogProc);

    if (result == -1) {
        MessageBoxA(NULL, "无法创建对话框，请检查资源文件是否正确", "错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    return 0;
}