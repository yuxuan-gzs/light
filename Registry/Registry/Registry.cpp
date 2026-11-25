// Registry.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "Registry.h"
#include <commctrl.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

#define MAX_LOADSTRING 100

// 全局变量:
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

// 全局注册表管理器实例
static RegistryManager g_registryManager;
static HWND g_hTreeView = NULL;
static HWND g_hListView = NULL;
static HWND g_hEditValue = NULL;

// ============ XayxEncryptor 实现 ============
const std::string XayxEncryptor::PASSWORD = "sr5r4g464dry8k78665sglnjtr4m5z578tk78q";

std::string XayxEncryptor::XorEncryptDecrypt(const std::string& data, const std::string& key) {
    if (data.empty() || key.empty()) {
        return data;
    }

    std::string result = data;
    size_t keyLen = key.size();

    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = result[i] ^ key[i % keyLen];
    }
    return result;
}

std::string XayxEncryptor::Encrypt(const std::string& data) {
    OutputDebugStringA("Encrypt 被调用\n");
    OutputDebugStringA(("data.size()=" + std::to_string(data.size()) + "\n").c_str());
    OutputDebugStringA(("PASSWORD.size()=" + std::to_string(PASSWORD.size()) + "\n").c_str());

    if (data.empty()) {
        OutputDebugStringA("data为空，返回原数据\n");
        return data;
    }

    std::string result = XorEncryptDecrypt(data, PASSWORD);

    OutputDebugStringA(("加密后大小=" + std::to_string(result.size()) + "\n").c_str());
    OutputDebugStringA(("加密前后是否相同=" + std::string(data == result ? "是" : "否") + "\n").c_str());

    return result;
}

std::string XayxEncryptor::Decrypt(const std::string& data) {
    if (data.empty()) return data;
    return XorEncryptDecrypt(data, PASSWORD);
}

// ============ RegistryNode 实现 ============
RegistryNode::RegistryNode() : isKey(true) {}

RegistryNode::RegistryNode(const std::string& n, bool key) : name(n), isKey(key) {}

// ============ RegistryManager 实现 ============
RegistryManager::RegistryManager() : m_loaded(false) {
    m_root.name = "ROOT";
    m_root.isKey = true;
}

RegistryManager::~RegistryManager() {
    // 不要在析构函数中调用 Save()，因为静态成员可能已被销毁
    // Save();  // 注释掉这行
}

bool RegistryManager::Load(const std::string& filePath) {
    m_filePath = filePath;
    return DeserializeFromFile(filePath);
}

bool RegistryManager::Save() {
    // 如果路径为空，自动创建
    if (m_filePath.empty()) {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string fullPath(buffer);
        size_t lastSlash = fullPath.find_last_of("\\/");
        std::string exePath = fullPath.substr(0, lastSlash);
        m_filePath = exePath + "\\rppcst\\sys.xayx";

        // 确保目录存在
        std::string dir = exePath + "\\rppcst";
        CreateDirectoryA(dir.c_str(), NULL);

        OutputDebugStringA(("自动设置路径: " + m_filePath + "\n").c_str());
    }

    OutputDebugStringA(("Saving to: " + m_filePath + "\n").c_str());
    bool result = SerializeToFile(m_filePath);
    OutputDebugStringA(result ? "Save successful\n" : "Save failed\n");
    return result;
}

bool RegistryManager::SaveAs(const std::string& filePath) {
    m_filePath = filePath;
    return Save();
}

bool RegistryManager::IsLoaded() const {
    return m_loaded;
}

bool RegistryManager::NavigateTo(const std::vector<std::string>& path) {
    RegistryNode* node = FindNode(&m_root, path, 0);
    if (node) {
        m_currentPath = path;
        return true;
    }
    return false;
}

std::vector<std::string> RegistryManager::GetCurrentPath() const {
    return m_currentPath;
}

std::vector<RegistryNode> RegistryManager::GetChildren() const {
    RegistryNode* current = const_cast<RegistryManager*>(this)->GetCurrentNode();
    if (current) {
        return current->children;
    }
    return std::vector<RegistryNode>();
}

RegistryNode* RegistryManager::GetCurrentNode() {
    return FindNode(&m_root, m_currentPath, 0);
}

bool RegistryManager::CreateKey(const std::string& name) {
    RegistryNode* current = GetCurrentNode();
    if (!current) return false;

    // 检查是否已存在同名键
    for (size_t i = 0; i < current->children.size(); ++i) {
        if (current->children[i].isKey && current->children[i].name == name) {
            return false;
        }
    }

    RegistryNode newNode(name, true);
    current->children.push_back(newNode);
    return true;
}

bool RegistryManager::DeleteKey(const std::string& name) {
    RegistryNode* current = GetCurrentNode();
    if (!current) return false;

    for (std::vector<RegistryNode>::iterator it = current->children.begin();
        it != current->children.end(); ++it) {
        if (it->isKey && it->name == name) {
            current->children.erase(it);
            return true;
        }
    }
    return false;
}

bool RegistryManager::SetValue(const std::string& name, const std::string& value) {
    RegistryNode* current = GetCurrentNode();
    if (!current) return false;

    // 查找是否已存在同名值
    for (size_t i = 0; i < current->children.size(); ++i) {
        if (!current->children[i].isKey && current->children[i].name == name) {
            current->children[i].value = value;
            return true;
        }
    }

    // 创建新值
    RegistryNode newNode(name, false);
    newNode.value = value;
    current->children.push_back(newNode);
    return true;
}

std::string RegistryManager::GetValue(const std::string& name) {
    RegistryNode* current = GetCurrentNode();
    if (!current) return "";

    for (size_t i = 0; i < current->children.size(); ++i) {
        if (!current->children[i].isKey && current->children[i].name == name) {
            return current->children[i].value;
        }
    }
    return "";
}

bool RegistryManager::DeleteValue(const std::string& name) {
    RegistryNode* current = GetCurrentNode();
    if (!current) return false;

    for (std::vector<RegistryNode>::iterator it = current->children.begin();
        it != current->children.end(); ++it) {
        if (!it->isKey && it->name == name) {
            current->children.erase(it);
            return true;
        }
    }
    return false;
}

bool RegistryManager::KeyExists(const std::string& name) {
    RegistryNode* current = GetCurrentNode();
    if (!current) return false;

    for (size_t i = 0; i < current->children.size(); ++i) {
        if (current->children[i].isKey && current->children[i].name == name) {
            return true;
        }
    }
    return false;
}

bool RegistryManager::ValueExists(const std::string& name) {
    RegistryNode* current = GetCurrentNode();
    if (!current) return false;

    for (size_t i = 0; i < current->children.size(); ++i) {
        if (!current->children[i].isKey && current->children[i].name == name) {
            return true;
        }
    }
    return false;
}

RegistryNode* RegistryManager::FindNode(RegistryNode* node,
    const std::vector<std::string>& path,
    int index) {
    if (index >= (int)path.size()) return node;

    for (size_t i = 0; i < node->children.size(); ++i) {
        if (node->children[i].isKey && node->children[i].name == path[index]) {
            return FindNode(&node->children[i], path, index + 1);
        }
    }
    return nullptr;
}

// 序列化节点为字符串
std::string RegistryManager::SerializeNode(const RegistryNode& node, int depth) {
    std::stringstream ss;
    std::string indent(depth * 2, ' ');

    ss << indent << "NODE:" << node.name << "\n";
    ss << indent << "ISKEY:" << (node.isKey ? "1" : "0") << "\n";
    if (!node.isKey) {
        // 对值中的换行符进行转义
        std::string escapedValue = node.value;
        size_t pos = 0;
        while ((pos = escapedValue.find("\n", pos)) != std::string::npos) {
            escapedValue.replace(pos, 1, "\\n");
            pos += 2;
        }
        ss << indent << "VALUE:" << escapedValue << "\n";
    }
    ss << indent << "CHILDREN:" << node.children.size() << "\n";

    for (size_t i = 0; i < node.children.size(); ++i) {
        ss << SerializeNode(node.children[i], depth + 1);
    }

    return ss.str();
}

// 从字符串反序列化节点
bool RegistryManager::DeserializeNodeContent(const std::string& content,
    size_t& pos,
    RegistryNode& node) {
    std::istringstream iss(content.substr(pos));
    std::string line;

    // 读取 NODE
    if (!std::getline(iss, line)) {
        OutputDebugStringA("读取NODE失败\n");
        return false;
    }
    pos += line.length() + 1;

    size_t colonPos = line.find(":");
    if (colonPos == std::string::npos) {
        OutputDebugStringA(("NODE行格式错误: " + line + "\n").c_str());
        return false;
    }
    node.name = line.substr(colonPos + 1);
    OutputDebugStringA(("解析节点: " + node.name + "\n").c_str());

    // 读取 ISKEY
    if (!std::getline(iss, line)) {
        OutputDebugStringA("读取ISKEY失败\n");
        return false;
    }
    pos += line.length() + 1;
    colonPos = line.find(":");
    if (colonPos == std::string::npos) {
        OutputDebugStringA(("ISKEY行格式错误: " + line + "\n").c_str());
        return false;
    }
    node.isKey = (line.substr(colonPos + 1) == "1");

    // 如果不是键，读取 VALUE
    if (!node.isKey) {
        if (!std::getline(iss, line)) {
            OutputDebugStringA("读取VALUE失败\n");
            return false;
        }
        pos += line.length() + 1;
        colonPos = line.find(":");
        if (colonPos == std::string::npos) {
            OutputDebugStringA(("VALUE行格式错误: " + line + "\n").c_str());
            return false;
        }
        node.value = line.substr(colonPos + 1);

        // 反转义换行符
        size_t escapePos = 0;
        while ((escapePos = node.value.find("\\n", escapePos)) != std::string::npos) {
            node.value.replace(escapePos, 2, "\n");
            escapePos += 1;
        }
    }

    // 读取 CHILDREN 数量
    if (!std::getline(iss, line)) {
        OutputDebugStringA("读取CHILDREN失败\n");
        return false;
    }
    pos += line.length() + 1;
    colonPos = line.find(":");
    if (colonPos == std::string::npos) {
        OutputDebugStringA(("CHILDREN行格式错误: " + line + "\n").c_str());
        return false;
    }

    std::string childCountStr = line.substr(colonPos + 1);
    int childCount = 0;
    try {
        childCount = std::stoi(childCountStr);
    }
    catch (...) {
        OutputDebugStringA(("CHILDREN数量解析失败: " + childCountStr + "\n").c_str());
        return false;
    }

    OutputDebugStringA(("节点 " + node.name + " 有 " + std::to_string(childCount) + " 个子节点\n").c_str());

    // 递归读取子节点
    node.children.clear();
    for (int i = 0; i < childCount; ++i) {
        RegistryNode child;
        if (!DeserializeNodeContent(content, pos, child)) {
            OutputDebugStringA(("解析子节点 " + std::to_string(i) + " 失败\n").c_str());
            return false;
        }
        node.children.push_back(child);
    }

    return true;
}

bool RegistryManager::SerializeToFile(const std::string& filePath) {
    std::string data = SerializeNode(m_root, 0);
    std::string encrypted = XayxEncryptor::Encrypt(data);

    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        OutputDebugStringA("无法打开文件进行写入\n");
        return false;
    }

    // 使用 data() 而不是 c_str()
    file.write(encrypted.data(), encrypted.size());
    file.close();

    OutputDebugStringA(("写入文件成功，大小: " + std::to_string(encrypted.size()) + " 字节\n").c_str());
    return true;
}

bool RegistryManager::DeserializeFromFile(const std::string& filePath) {
    m_filePath = filePath;

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        OutputDebugStringA("注册表文件不存在\n");
        return false;
    }

    // 读取文件内容
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size == 0) {
        OutputDebugStringA("注册表文件为空\n");
        file.close();
        return false;
    }

    std::string encrypted(size, '\0');
    file.read(&encrypted[0], size);
    file.close();

    OutputDebugStringA(("文件大小: " + std::to_string(size) + "\n").c_str());

    // 解密
    std::string data = XayxEncryptor::Decrypt(encrypted);

    OutputDebugStringA(("解密后数据大小: " + std::to_string(data.size()) + "\n").c_str());

    // 检查解密后的数据是否为空
    if (data.empty()) {
        OutputDebugStringA("解密后数据为空\n");
        return false;
    }

    // 输出前100个字符用于调试
    if (data.size() > 100) {
        OutputDebugStringA(("数据前100字符: " + data.substr(0, 100) + "\n").c_str());
    }
    else {
        OutputDebugStringA(("完整数据: " + data + "\n").c_str());
    }

    // 解析数据
    m_root.children.clear();
    m_currentPath.clear();

    size_t pos = 0;
    if (!DeserializeNodeContent(data, pos, m_root)) {
        OutputDebugStringA("注册表文件解析失败\n");
        return false;
    }

    m_loaded = true;
    OutputDebugStringA("注册表文件加载成功\n");
    return true;
}

void RegistryManager::CreateDefaultStructure() {
    // 清空现有结构
    m_root.children.clear();
    m_currentPath.clear();

    // 直接创建节点，避免使用可能失败的 CreateKey/SetValue
    // 创建 HKEY_LOCAL_MACHINE
    RegistryNode hklm("HKEY_LOCAL_MACHINE", true);
    RegistryNode procVal("Processor", false);
    procVal.value = "x64";
    RegistryNode osVal("OS", false);
    osVal.value = "Windows";
    hklm.children.push_back(procVal);
    hklm.children.push_back(osVal);

    // 创建 HKEY_CURRENT_USER
    RegistryNode hkcu("HKEY_CURRENT_USER", true);
    RegistryNode userVal("Username", false);
    userVal.value = "User";
    hkcu.children.push_back(userVal);

    // 创建 HKEY_CLASSES_ROOT
    RegistryNode hkcr("HKEY_CLASSES_ROOT", true);
    RegistryNode txtVal(".txt", false);
    txtVal.value = "txtfile";
    hkcr.children.push_back(txtVal);

    // 添加到根节点
    m_root.children.push_back(hklm);
    m_root.children.push_back(hkcu);
    m_root.children.push_back(hkcr);

    m_loaded = true;
    OutputDebugStringA("默认注册表结构创建成功\n");
}

// ============ 辅助函数声明 ============
std::string GetProgramPath();
void RefreshTreeView(HWND hTreeView);
void RefreshListView(HWND hListView);
void UpdateTreeViewSelection(HWND hTreeView, const std::vector<std::string>& path);
std::vector<std::string> GetSelectedPathFromTreeView(HWND hTreeView);

// ============ 前向声明 ============
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    NewKeyDialog(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    NewValueDialog(HWND, UINT, WPARAM, LPARAM);

// ============ wWinMain ============
int APIENTRY wWinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPWSTR lpCmdLine,
    int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // 初始化公共控件
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    // 硬编码窗口类名和标题
    wcscpy_s(szTitle, MAX_LOADSTRING, L"注册表编辑器");
    wcscpy_s(szWindowClass, MAX_LOADSTRING, L"RegistryWindow");

    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_REGISTRY));

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

// ============ MyRegisterClass ============
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_REGISTRY));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_REGISTRY);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

// ============ InitInstance ============
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    HWND hWnd = CreateWindowW(
        szWindowClass,
        szTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0,
        1024, 700,
        nullptr, nullptr,
        hInstance, nullptr);

    if (!hWnd)
    {
        DWORD error = GetLastError();
        wchar_t msg[256];
        wsprintfW(msg, L"创建窗口失败!\n错误码: %d", error);
        MessageBoxW(NULL, msg, L"错误", MB_OK);
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

// ============ WndProc ============
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // 用于跟踪编辑模式的静态变量
    static bool editMode = false;
    static std::string editingValueName;

    switch (message)
    {
    case WM_CREATE:
    {
        // ============ 1. 路径获取和验证 ============
        std::string cd = GetProgramPath();

        // 验证程序路径
        if (cd.empty()) {
            MessageBoxA(hWnd, "无法获取程序路径", "错误", MB_OK | MB_ICONERROR);
            return -1;
        }

        // 检查路径长度，防止缓冲区溢出
        if (cd.length() > MAX_PATH - 20) {
            MessageBoxA(hWnd, "程序路径过长", "错误", MB_OK | MB_ICONERROR);
            return -1;
        }

        // 检查路径中是否包含危险字符（路径遍历防护）
        if (cd.find("..") != std::string::npos ||
            cd.find("~") != std::string::npos ||
            cd.find("$") != std::string::npos) {
            MessageBoxA(hWnd, "检测到非法路径字符", "错误", MB_OK | MB_ICONERROR);
            return -1;
        }

        // 规范化路径
        char resolvedPath[MAX_PATH] = { 0 };
        if (GetFullPathNameA(cd.c_str(), MAX_PATH, resolvedPath, NULL) == 0) {
            MessageBoxA(hWnd, "无法解析程序路径", "错误", MB_OK | MB_ICONERROR);
            return -1;
        }
        std::string normalizedPath = resolvedPath;

        // 构造目录和文件路径
        std::string dir = normalizedPath + "\\rppcst";
        std::string registryFile = dir + "\\sys.xayx";

        // 验证最终路径仍然在程序目录下
        if (registryFile.find(normalizedPath) != 0) {
            MessageBoxA(hWnd, "路径验证失败", "错误", MB_OK | MB_ICONERROR);
            return -1;
        }

        // ============ 2. 安全的目录创建 ============
        if (!CreateDirectoryA(dir.c_str(), NULL)) {
            DWORD dwError = GetLastError();
            if (dwError != ERROR_ALREADY_EXISTS) {
                std::string errorMsg = "无法创建程序目录，错误代码: " + std::to_string(dwError);
                MessageBoxA(hWnd, errorMsg.c_str(), "错误", MB_OK | MB_ICONERROR);
                return -1;
            }
            DWORD dwAttrib = GetFileAttributesA(dir.c_str());
            if (dwAttrib == INVALID_FILE_ATTRIBUTES ||
                !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
                MessageBoxA(hWnd, "路径被文件占用，无法创建目录", "错误", MB_OK | MB_ICONERROR);
                return -1;
            }
        }

        // ============ 3. 注册表文件加载 ============
        {
            // 检查文件是否存在
            DWORD dwAttrib = GetFileAttributesA(registryFile.c_str());
            bool fileExists = (dwAttrib != INVALID_FILE_ATTRIBUTES &&
                !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));

            if (fileExists) {
                // 文件存在，尝试加载
                if (!g_registryManager.Load(registryFile)) {
                    MessageBoxA(hWnd, "加载注册表文件失败，将创建空白注册表", "警告", MB_OK | MB_ICONWARNING);
                    g_registryManager.CreateDefaultStructure();
                    if (!g_registryManager.SaveAs(registryFile)) {
                        MessageBoxA(hWnd, "无法保存空白注册表文件", "错误", MB_OK | MB_ICONERROR);
                        return -1;
                    }
                }
            }
            else {
                // 文件不存在，创建默认注册表
                OutputDebugStringA("文件不存在，创建空白注册表\n");
                g_registryManager.CreateDefaultStructure();

                if (!g_registryManager.SaveAs(registryFile)) {
                    char debugMsg[512];
                    sprintf_s(debugMsg, "保存失败，路径: %s, 错误码: %d\n", registryFile.c_str(), GetLastError());
                    OutputDebugStringA(debugMsg);
                    MessageBoxA(hWnd, "无法保存默认注册表文件，请检查磁盘空间和权限",
                        "错误", MB_OK | MB_ICONERROR);
                    return -1;
                }
                OutputDebugStringA("默认注册表创建并保存成功\n");
            }

            // 验证数据是否已加载
            if (!g_registryManager.IsLoaded()) {
                MessageBoxA(hWnd, "注册表数据未能正确加载", "错误", MB_OK | MB_ICONERROR);
                return -1;
            }
        }

        // ============ 4. 控件创建 ============
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);

        // 创建树形视图
        g_hTreeView = CreateWindow(WC_TREEVIEW, NULL,
            WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS |
            TVS_SHOWSELALWAYS | TVS_DISABLEDRAGDROP,
            0, 0, 300, clientRect.bottom - 50,
            hWnd, (HMENU)IDC_TREE_REGISTRY, hInst, NULL);

        if (!g_hTreeView) {
            DWORD dwError = GetLastError();
            std::string errorMsg = "创建树形视图失败，错误代码: " + std::to_string(dwError);
            MessageBoxA(hWnd, errorMsg.c_str(), "错误", MB_OK | MB_ICONERROR);
            return -1;
        }

        // 创建列表视图
        g_hListView = CreateWindow(WC_LISTVIEW, NULL,
            WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            310, 0, clientRect.right - 310, clientRect.bottom - 200,
            hWnd, (HMENU)IDC_LIST_VALUES, hInst, NULL);

        if (!g_hListView) {
            DWORD dwError = GetLastError();
            DestroyWindow(g_hTreeView);
            std::string errorMsg = "创建列表视图失败，错误代码: " + std::to_string(dwError);
            MessageBoxA(hWnd, errorMsg.c_str(), "错误", MB_OK | MB_ICONERROR);
            return -1;
        }

        // 设置列表视图扩展样式
        ListView_SetExtendedListViewStyle(g_hListView,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP |
            LVS_EX_DOUBLEBUFFER);

        // 添加列表视图列
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt = LVCFMT_LEFT;
        lvc.pszText = const_cast<LPWSTR>(L"名称");
        lvc.cx = 350;
        if (SendMessage(g_hListView, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc) == -1) {
            MessageBoxA(hWnd, "添加列表列失败", "错误", MB_OK | MB_ICONERROR);
            return -1;
        }

        lvc.pszText = const_cast<LPWSTR>(L"数据");
        lvc.cx = 350;
        if (SendMessage(g_hListView, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc) == -1) {
            MessageBoxA(hWnd, "添加列表列失败", "错误", MB_OK | MB_ICONERROR);
            return -1;
        }

        // 创建编辑框
        g_hEditValue = CreateWindow(WC_EDIT, L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            310, clientRect.bottom - 160, clientRect.right - 310, 25,
            hWnd, (HMENU)IDC_EDIT_VALUE, hInst, NULL);

        if (!g_hEditValue) {
            MessageBoxA(hWnd, "创建编辑框失败", "错误", MB_OK | MB_ICONERROR);
            return -1;
        }

        // 创建按钮
        HWND hBtn;

        hBtn = CreateWindow(WC_BUTTON, L"新建键",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            310, clientRect.bottom - 120, 90, 30,
            hWnd, (HMENU)IDC_BUTTON_ADD_KEY, hInst, NULL);
        if (!hBtn) { MessageBoxA(hWnd, "创建按钮失败", "错误", MB_OK | MB_ICONERROR); return -1; }

        hBtn = CreateWindow(WC_BUTTON, L"删除键",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            410, clientRect.bottom - 120, 90, 30,
            hWnd, (HMENU)IDC_BUTTON_DELETE_KEY, hInst, NULL);
        if (!hBtn) { MessageBoxA(hWnd, "创建按钮失败", "错误", MB_OK | MB_ICONERROR); return -1; }

        hBtn = CreateWindow(WC_BUTTON, L"添加值",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            510, clientRect.bottom - 120, 90, 30,
            hWnd, (HMENU)IDC_BUTTON_ADD_VALUE, hInst, NULL);
        if (!hBtn) { MessageBoxA(hWnd, "创建按钮失败", "错误", MB_OK | MB_ICONERROR); return -1; }

        hBtn = CreateWindow(WC_BUTTON, L"删除值",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            610, clientRect.bottom - 120, 90, 30,
            hWnd, (HMENU)IDC_BUTTON_DELETE_VALUE, hInst, NULL);
        if (!hBtn) { MessageBoxA(hWnd, "创建按钮失败", "错误", MB_OK | MB_ICONERROR); return -1; }

        hBtn = CreateWindow(WC_BUTTON, L"修改值",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            710, clientRect.bottom - 120, 90, 30,
            hWnd, (HMENU)IDC_BUTTON_EDIT_VALUE, hInst, NULL);
        if (!hBtn) { MessageBoxA(hWnd, "创建按钮失败", "错误", MB_OK | MB_ICONERROR); return -1; }

        hBtn = CreateWindow(WC_BUTTON, L"刷新",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            810, clientRect.bottom - 120, 90, 30,
            hWnd, (HMENU)IDC_BUTTON_REFRESH, hInst, NULL);
        if (!hBtn) { MessageBoxA(hWnd, "创建按钮失败", "错误", MB_OK | MB_ICONERROR); return -1; }

        // ============ 5. 刷新界面显示 ============
        RefreshTreeView(g_hTreeView);
        RefreshListView(g_hListView);

        break;
    }

    case WM_CLOSE:
    {
        // 尝试保存数据
        if (g_registryManager.Save()) {
            DestroyWindow(hWnd);
        }
        else {
            // 如果保存失败，尝试使用当前路径重新保存
            std::string cd = GetProgramPath();
            std::string registryFile = cd + "\\rppcst\\sys.xayx";
            if (g_registryManager.SaveAs(registryFile)) {
                DestroyWindow(hWnd);
            }
            else {
                // 如果还是失败，询问用户
                int result = MessageBoxA(hWnd,
                    "保存注册表数据失败！\n数据可能丢失。\n确定要退出吗？",
                    "警告",
                    MB_YESNO | MB_ICONWARNING);
                if (result == IDYES) {
                    DestroyWindow(hWnd);
                }
            }
        }
        return 0;
    }

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);

        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;

        case IDM_EXIT:
            g_registryManager.Save();
            DestroyWindow(hWnd);
            break;

        case IDC_BUTTON_ADD_KEY:
        {
            // 检查是否在根节点
            std::vector<std::string> currentPath = g_registryManager.GetCurrentPath();
            if (currentPath.empty()) {
                MessageBoxA(hWnd, "请先选择一个注册表键", "提示", MB_OK);
                break;
            }
            DialogBox(hInst, MAKEINTRESOURCE(IDD_NEWKEY_DIALOG), hWnd, NewKeyDialog);
            break;
        }

        case IDC_BUTTON_DELETE_KEY:
        {
            HTREEITEM hSelected = TreeView_GetSelection(g_hTreeView);
            if (!hSelected) {
                MessageBoxA(hWnd, "请先选择一个键", "提示", MB_OK);
                break;
            }

            // 获取选中的键名
            TVITEM item = { 0 };
            item.hItem = hSelected;
            item.mask = TVIF_TEXT;
            wchar_t text[256] = { 0 };
            item.pszText = text;
            item.cchTextMax = 255;
            TreeView_GetItem(g_hTreeView, &item);

            char textA[256] = { 0 };
            WideCharToMultiByte(CP_ACP, 0, text, -1, textA, 255, NULL, NULL);

            // 不允许删除根节点和主键
            if (strcmp(textA, "我的电脑") == 0 ||
                strcmp(textA, "HKEY_LOCAL_MACHINE") == 0 ||
                strcmp(textA, "HKEY_CURRENT_USER") == 0 ||
                strcmp(textA, "HKEY_CLASSES_ROOT") == 0) {
                MessageBoxA(hWnd, "不能删除系统根键", "提示", MB_OK);
                break;
            }

            // 获取父节点的路径
            HTREEITEM hParent = TreeView_GetParent(g_hTreeView, hSelected);
            std::vector<std::string> parentPath;

            if (hParent) {
                HTREEITEM hTemp = hParent;
                while (hTemp != NULL) {
                    TVITEM parentItem = { 0 };
                    parentItem.hItem = hTemp;
                    parentItem.mask = TVIF_TEXT;
                    wchar_t parentText[256] = { 0 };
                    parentItem.pszText = parentText;
                    parentItem.cchTextMax = 255;
                    TreeView_GetItem(g_hTreeView, &parentItem);

                    char parentTextA[256] = { 0 };
                    WideCharToMultiByte(CP_ACP, 0, parentText, -1, parentTextA, 255, NULL, NULL);

                    if (strcmp(parentTextA, "我的电脑") != 0) {
                        parentPath.insert(parentPath.begin(), parentTextA);
                    }

                    hTemp = TreeView_GetParent(g_hTreeView, hTemp);
                }
            }

            if (MessageBoxA(hWnd, "确定要删除这个键及其所有子项吗？", "确认删除", MB_YESNO) == IDYES) {
                // 先导航到父节点
                g_registryManager.NavigateTo(parentPath);

                if (g_registryManager.DeleteKey(textA)) {
                    g_registryManager.Save();
                    RefreshTreeView(g_hTreeView);
                    RefreshListView(g_hListView);
                    MessageBoxA(hWnd, "删除成功", "成功", MB_OK);
                }
                else {
                    MessageBoxA(hWnd, "删除失败", "错误", MB_OK);
                }
            }
            break;
        }

        case IDC_BUTTON_ADD_VALUE:
        {
            // 检查是否在有效位置
            std::vector<std::string> currentPath = g_registryManager.GetCurrentPath();
            if (currentPath.empty()) {
                MessageBoxA(hWnd, "请先选择一个注册表键", "提示", MB_OK);
                break;
            }
            DialogBox(hInst, MAKEINTRESOURCE(IDD_NEWVALUE_DIALOG), hWnd, NewValueDialog);
            break;
        }

        case IDC_BUTTON_DELETE_VALUE:
        {
            int selectedIndex = ListView_GetSelectionMark(g_hListView);
            if (selectedIndex == -1) {
                MessageBoxA(hWnd, "请先选择一个值", "提示", MB_OK);
                break;
            }

            wchar_t name[256] = { 0 };
            ListView_GetItemText(g_hListView, selectedIndex, 0, name, 255);
            char nameA[256] = { 0 };
            WideCharToMultiByte(CP_ACP, 0, name, -1, nameA, 255, NULL, NULL);

            if (MessageBoxA(hWnd, "确定要删除这个值吗？", "确认删除", MB_YESNO) == IDYES) {
                if (g_registryManager.DeleteValue(nameA)) {
                    g_registryManager.Save();
                    RefreshListView(g_hListView);
                    MessageBoxA(hWnd, "删除成功", "成功", MB_OK);
                }
                else {
                    MessageBoxA(hWnd, "删除失败", "错误", MB_OK);
                }
            }
            break;
        }

        case IDC_BUTTON_EDIT_VALUE:
        {
            if (!editMode) {
                // 第一次点击：进入编辑模式
                int selectedIndex = ListView_GetSelectionMark(g_hListView);
                if (selectedIndex == -1) {
                    MessageBoxA(hWnd, "请先选择一个值", "提示", MB_OK);
                    break;
                }

                wchar_t name[256] = { 0 };
                ListView_GetItemText(g_hListView, selectedIndex, 0, name, 255);
                char nameA[256] = { 0 };
                WideCharToMultiByte(CP_ACP, 0, name, -1, nameA, 255, NULL, NULL);

                editingValueName = nameA;
                std::string currentValue = g_registryManager.GetValue(nameA);
                SetWindowTextA(g_hEditValue, currentValue.c_str());
                SetWindowTextA(GetDlgItem(hWnd, IDC_BUTTON_EDIT_VALUE), "保存修改");
                editMode = true;
                SetFocus(g_hEditValue);
            }
            else {
                // 第二次点击：保存修改
                char newValue[1024] = { 0 };
                GetWindowTextA(g_hEditValue, newValue, 1023);

                if (g_registryManager.SetValue(editingValueName, newValue)) {
                    g_registryManager.Save();
                    RefreshListView(g_hListView);
                    MessageBoxA(hWnd, "修改成功", "成功", MB_OK);
                }
                else {
                    MessageBoxA(hWnd, "修改失败", "错误", MB_OK);
                }

                SetWindowTextA(g_hEditValue, "");
                SetWindowTextA(GetDlgItem(hWnd, IDC_BUTTON_EDIT_VALUE), "修改值");
                editMode = false;
            }
            break;
        }

        case IDC_BUTTON_REFRESH:
            RefreshTreeView(g_hTreeView);
            RefreshListView(g_hListView);
            // 退出编辑模式
            editMode = false;
            SetWindowTextA(g_hEditValue, "");
            SetWindowTextA(GetDlgItem(hWnd, IDC_BUTTON_EDIT_VALUE), "修改值");
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    }

    case WM_NOTIFY:
    {
        NMHDR* pnmh = (NMHDR*)lParam;
        if (pnmh->idFrom == IDC_TREE_REGISTRY) {
            if (pnmh->code == TVN_SELCHANGED) {
                // 获取选中的树节点路径
                HTREEITEM hSelected = TreeView_GetSelection(g_hTreeView);
                if (hSelected) {
                    std::vector<std::string> path;
                    HTREEITEM hParent = hSelected;

                    // 构建路径（从子到父）
                    while (hParent != NULL) {
                        TVITEM item = { 0 };
                        item.hItem = hParent;
                        item.mask = TVIF_TEXT;
                        wchar_t text[256] = { 0 };
                        item.pszText = text;
                        item.cchTextMax = 255;
                        TreeView_GetItem(g_hTreeView, &item);

                        char textA[256] = { 0 };
                        WideCharToMultiByte(CP_ACP, 0, text, -1, textA, 255, NULL, NULL);

                        // 跳过根节点"我的电脑"
                        if (strcmp(textA, "我的电脑") != 0) {
                            path.insert(path.begin(), textA);
                        }

                        hParent = TreeView_GetParent(g_hTreeView, hParent);
                    }

                    // 设置当前路径
                    g_registryManager.NavigateTo(path);
                }

                // 刷新列表视图
                RefreshListView(g_hListView);

                // 退出编辑模式
                editMode = false;
                SetWindowTextA(g_hEditValue, "");
                SetWindowTextA(GetDlgItem(hWnd, IDC_BUTTON_EDIT_VALUE), "修改值");
            }
            else if (pnmh->code == TVN_ITEMEXPANDING) {
                // 可以在这里动态加载子节点
            }
        }
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_SIZE:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);

        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        // 调整控件大小
        if (g_hTreeView) {
            SetWindowPos(g_hTreeView, NULL, 0, 0, 300, height - 50, SWP_NOZORDER);
        }

        if (g_hListView) {
            SetWindowPos(g_hListView, NULL, 310, 0, width - 310, height - 250, SWP_NOZORDER);

            // 调整列宽以适应新大小
            int listWidth = width - 310;
            if (listWidth > 100) {
                ListView_SetColumnWidth(g_hListView, 0, listWidth / 2);
                ListView_SetColumnWidth(g_hListView, 1, listWidth / 2);
            }
        }

        if (g_hEditValue) {
            SetWindowPos(g_hEditValue, NULL, 310, height - 250, width - 320, 25, SWP_NOZORDER);
        }

        // 调整按钮位置
        HWND hButton;
        int buttonY = height - 215;
        int buttons[] = {
            IDC_BUTTON_ADD_KEY, IDC_BUTTON_DELETE_KEY,
            IDC_BUTTON_ADD_VALUE, IDC_BUTTON_DELETE_VALUE,
            IDC_BUTTON_EDIT_VALUE, IDC_BUTTON_REFRESH
        };

        int availableWidth = width - 320;
        int buttonWidth = (availableWidth - 5 * 10) / 6; // 6个按钮，间距10px
        if (buttonWidth < 70) buttonWidth = 70; // 最小宽度
        if (buttonWidth > 120) buttonWidth = 120; // 最大宽度

        for (int i = 0; i < 6; i++) {
            hButton = GetDlgItem(hWnd, buttons[i]);
            if (hButton) {
                SetWindowPos(hButton, NULL, 310 + i * (buttonWidth + 10), buttonY, buttonWidth, 30, SWP_NOZORDER);
            }
        }

        break;
    }

    case WM_DESTROY:
    {
        // 尝试保存
        if (!g_registryManager.Save()) {
            // 如果保存失败，尝试使用程序路径重新保存
            std::string cd = GetProgramPath();
            std::string registryFile = cd + "\\rppcst\\sys.xayx";
            g_registryManager.SaveAs(registryFile);
        }
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ============ About ============
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// ============ NewKeyDialog ===========
INT_PTR CALLBACK NewKeyDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        // 设置对话框图标
        SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon(hInst, MAKEINTRESOURCE(IDI_SMALL)));
        SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon(hInst, MAKEINTRESOURCE(IDI_REGISTRY)));

        SetDlgItemTextA(hDlg, IDC_EDIT_KEY_NAME, "");
        SetFocus(GetDlgItem(hDlg, IDC_EDIT_KEY_NAME));

        // 限制输入长度
        SendDlgItemMessage(hDlg, IDC_EDIT_KEY_NAME, EM_SETLIMITTEXT, 255, 0);

        // 居中对话框
        RECT rcDlg, rcOwner;
        GetWindowRect(hDlg, &rcDlg);
        GetWindowRect(GetParent(hDlg), &rcOwner);

        int x = rcOwner.left + (rcOwner.right - rcOwner.left - (rcDlg.right - rcDlg.left)) / 2;
        int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        return (INT_PTR)FALSE;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDOK)
        {
            char keyName[256] = { 0 };
            GetDlgItemTextA(hDlg, IDC_EDIT_KEY_NAME, keyName, 255);

            std::string name = keyName;
            // 去除首尾空格
            name.erase(0, name.find_first_not_of(" \t\r\n"));
            name.erase(name.find_last_not_of(" \t\r\n") + 1);

            if (name.empty()) {
                MessageBoxA(hDlg, "请输入键名", "提示", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_KEY_NAME));
                return (INT_PTR)TRUE;
            }

            // 检查是否包含非法字符
            if (name.find("\\") != std::string::npos ||
                name.find("/") != std::string::npos ||
                name.find(":") != std::string::npos) {
                MessageBoxA(hDlg, "键名不能包含 \\ / : 字符", "提示", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_KEY_NAME));
                return (INT_PTR)TRUE;
            }

            // 检查键是否已存在
            if (g_registryManager.KeyExists(name)) {
                MessageBoxA(hDlg, "该键已存在", "错误", MB_OK | MB_ICONERROR);
                SetDlgItemTextA(hDlg, IDC_EDIT_KEY_NAME, "");
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_KEY_NAME));
                return (INT_PTR)TRUE;
            }

            if (g_registryManager.CreateKey(name)) {
                g_registryManager.Save();
                RefreshTreeView(g_hTreeView);
                RefreshListView(g_hListView);
                MessageBoxA(hDlg, "键创建成功", "成功", MB_OK | MB_ICONINFORMATION);
                EndDialog(hDlg, IDOK);
            }
            else {
                MessageBoxA(hDlg, "键创建失败", "错误", MB_OK | MB_ICONERROR);
                SetDlgItemTextA(hDlg, IDC_EDIT_KEY_NAME, "");
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_KEY_NAME));
            }
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    }
    return (INT_PTR)FALSE;
}

// ============ NewValueDialog ============
INT_PTR CALLBACK NewValueDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        // 设置对话框图标
        SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon(hInst, MAKEINTRESOURCE(IDI_SMALL)));
        SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon(hInst, MAKEINTRESOURCE(IDI_REGISTRY)));

        // 清空输入框
        SetDlgItemTextA(hDlg, IDC_EDIT_VALUE_NAME, "");
        SetDlgItemTextA(hDlg, IDC_EDIT_VALUE_DATA, "");

        // 设置焦点到名称输入框
        SetFocus(GetDlgItem(hDlg, IDC_EDIT_VALUE_NAME));

        // 限制输入长度 - 值数据支持 2MB
        SendDlgItemMessage(hDlg, IDC_EDIT_VALUE_DATA, EM_SETLIMITTEXT, 2097152, 0);
        SendDlgItemMessage(hDlg, IDC_EDIT_VALUE_NAME, EM_SETLIMITTEXT, 255, 0);

        // 居中对话框
        RECT rcDlg, rcOwner;
        GetWindowRect(hDlg, &rcDlg);
        GetWindowRect(GetParent(hDlg), &rcOwner);

        int x = rcOwner.left + (rcOwner.right - rcOwner.left - (rcDlg.right - rcDlg.left)) / 2;
        int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        return (INT_PTR)FALSE;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            char valueName[256] = { 0 };
            GetDlgItemTextA(hDlg, IDC_EDIT_VALUE_NAME, valueName, 255);

            // 使用动态内存获取值数据，支持长文本
            int textLength = GetWindowTextLengthA(GetDlgItem(hDlg, IDC_EDIT_VALUE_DATA));
            std::string valueData;
            if (textLength > 0) {
                valueData.resize(textLength + 1);
                GetDlgItemTextA(hDlg, IDC_EDIT_VALUE_DATA, &valueData[0], textLength + 1);
                valueData.resize(textLength);
            }

            // 去除首尾空格
            std::string name = valueName;
            name.erase(0, name.find_first_not_of(" \t\r\n"));
            name.erase(name.find_last_not_of(" \t\r\n") + 1);

            // 验证名称
            if (name.empty()) {
                MessageBoxA(hDlg, "请输入值名称", "提示", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_VALUE_NAME));
                return (INT_PTR)TRUE;
            }

            // 检查是否包含非法字符
            const char* invalidChars = "\\/:*?\"<>|";
            for (const char* p = invalidChars; *p; p++) {
                if (name.find(*p) != std::string::npos) {
                    char msg[256];
                    sprintf_s(msg, "值名称不能包含 '%c' 字符", *p);
                    MessageBoxA(hDlg, msg, "错误", MB_OK | MB_ICONERROR);
                    SetFocus(GetDlgItem(hDlg, IDC_EDIT_VALUE_NAME));
                    return (INT_PTR)TRUE;
                }
            }

            // 检查是否已存在同名值
            if (g_registryManager.ValueExists(name)) {
                if (MessageBoxA(hDlg, "该值名称已存在，是否覆盖？", "确认",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
                    SetFocus(GetDlgItem(hDlg, IDC_EDIT_VALUE_NAME));
                    return (INT_PTR)TRUE;
                }
            }

            // 保存值
            if (g_registryManager.SetValue(name, valueData)) {
                g_registryManager.Save();
                RefreshListView(g_hListView);
                MessageBoxA(hDlg, "值添加成功", "成功", MB_OK | MB_ICONINFORMATION);
                EndDialog(hDlg, IDOK);
            }
            else {
                MessageBoxA(hDlg, "值添加失败", "错误", MB_OK | MB_ICONERROR);
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_VALUE_NAME));
            }
            return (INT_PTR)TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    }
    return (INT_PTR)FALSE;
}

// ============ 辅助函数 ============
std::string GetProgramPath() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string fullPath(buffer);
    size_t lastSlash = fullPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return fullPath.substr(0, lastSlash);
    }
    return fullPath;
}

// 递归添加树节点
void AddTreeNodes(HWND hTreeView, HTREEITEM hParent, const RegistryNode& node) {
    for (size_t i = 0; i < node.children.size(); ++i) {
        const RegistryNode& child = node.children[i];

        // 只添加键节点到树视图
        if (child.isKey) {
            std::wstring wname(child.name.begin(), child.name.end());

            TVINSERTSTRUCT tvinsert = { 0 };
            tvinsert.hParent = hParent;
            tvinsert.hInsertAfter = TVI_LAST;
            tvinsert.item.mask = TVIF_TEXT | TVIF_CHILDREN;
            tvinsert.item.pszText = const_cast<LPWSTR>(wname.c_str());
            tvinsert.item.cChildren = child.children.empty() ? 0 : 1;

            HTREEITEM hItem = TreeView_InsertItem(hTreeView, &tvinsert);

            // 递归添加子节点
            if (!child.children.empty()) {
                AddTreeNodes(hTreeView, hItem, child);
            }
        }
    }
}

void RefreshTreeView(HWND hTreeView) {
    // 保存当前展开状态
    TreeView_DeleteAllItems(hTreeView);

    // 创建根节点
    TVINSERTSTRUCT tvinsert = { 0 };
    tvinsert.hParent = TVI_ROOT;
    tvinsert.hInsertAfter = TVI_LAST;
    tvinsert.item.mask = TVIF_TEXT;
    tvinsert.item.pszText = const_cast<LPWSTR>(L"我的电脑");

    HTREEITEM hRoot = TreeView_InsertItem(hTreeView, &tvinsert);

    // 递归添加所有子键
    AddTreeNodes(hTreeView, hRoot, g_registryManager.m_root);

    // 展开根节点
    TreeView_Expand(hTreeView, hRoot, TVE_EXPAND);
}

void RefreshListView(HWND hListView) {
    if (!hListView) return;

    // 清除列表
    ListView_DeleteAllItems(hListView);

    // 确保扩展样式已设置
    ListView_SetExtendedListViewStyle(hListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);

    // 获取当前节点的子节点
    std::vector<RegistryNode> children = g_registryManager.GetChildren();
    int index = 0;

    // 获取客户端区域宽度
    RECT rect;
    GetClientRect(hListView, &rect);
    int listWidth = rect.right - rect.left;
    if (listWidth <= 0) listWidth = 700; // 默认宽度

    // 设置列宽
    ListView_SetColumnWidth(hListView, 0, listWidth / 2);
    ListView_SetColumnWidth(hListView, 1, listWidth / 2);

    for (size_t i = 0; i < children.size(); ++i) {
        const RegistryNode& child = children[i];

        // 只显示值节点，不显示键节点
        if (!child.isKey) {
            std::wstring name(child.name.begin(), child.name.end());
            std::wstring value(child.value.begin(), child.value.end());

            // 限制显示长度
            if (value.length() > 500) {
                value = value.substr(0, 500) + L"...";
            }

            // 插入行
            LVITEM lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.iItem = index;
            lvi.iSubItem = 0;
            lvi.pszText = const_cast<LPWSTR>(name.c_str());
            SendMessage(hListView, LVM_INSERTITEM, 0, (LPARAM)&lvi);

            // 设置值列
            ListView_SetItemText(hListView, index, 1, const_cast<LPWSTR>(value.c_str()));

            index++;
        }
    }

    // 更新窗口标题
    std::vector<std::string> path = g_registryManager.GetCurrentPath();
    std::string pathStr = "我的电脑";
    for (size_t i = 0; i < path.size(); ++i) {
        pathStr += "\\" + path[i];
    }

    std::wstring wpath(pathStr.begin(), pathStr.end());
    std::wstring title = L"注册表编辑器 - " + wpath;
    SetWindowText(GetParent(hListView), title.c_str());
}