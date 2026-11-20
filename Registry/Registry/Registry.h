// Registry.h
#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <fstream>

// 加密器类
class XayxEncryptor {
public:
    static std::string Encrypt(const std::string& data);
    static std::string Decrypt(const std::string& data);

private:
    static const std::string PASSWORD;
    static std::string XorEncryptDecrypt(const std::string& data, const std::string& key);
};

// 注册表节点类
class RegistryNode {
public:
    std::string name;
    std::string value;
    bool isKey;
    std::vector<RegistryNode> children;

    RegistryNode();
    RegistryNode(const std::string& n, bool key);
};

// 注册表管理器类
class RegistryManager {
public:
    RegistryNode m_root;

    RegistryManager();
    ~RegistryManager();

    bool Load(const std::string& filePath);
    bool Save();
    bool SaveAs(const std::string& filePath);
    bool IsLoaded() const;

    bool NavigateTo(const std::vector<std::string>& path);
    std::vector<std::string> GetCurrentPath() const;
    std::vector<RegistryNode> GetChildren() const;
    RegistryNode* GetCurrentNode();

    bool CreateKey(const std::string& name);
    bool DeleteKey(const std::string& name);
    bool SetValue(const std::string& name, const std::string& value);
    std::string GetValue(const std::string& name);
    bool DeleteValue(const std::string& name);
    bool KeyExists(const std::string& name);
    bool ValueExists(const std::string& name);

    void CreateDefaultStructure();

private:
    std::string m_filePath;
    std::vector<std::string> m_currentPath;
    bool m_loaded;

    RegistryNode* FindNode(RegistryNode* node, const std::vector<std::string>& path, int index);
    std::string SerializeNode(const RegistryNode& node, int depth);
    bool SerializeToFile(const std::string& filePath);
    bool DeserializeNodeContent(const std::string& content, size_t& pos, RegistryNode& node);
    bool DeserializeFromFile(const std::string& filePath);
};

// 控件ID定义
// Resource.h
#pragma once

// 主图标
#define IDI_REGISTRY                    101
#define IDI_SMALL                       102

// 字符串资源
#define IDS_APP_TITLE                   103
#define IDC_REGISTRY                    104

// 菜单命令 - 基础命令
#define IDM_ABOUT                       105
#define IDM_EXIT                        106

// 菜单命令 - 文件菜单
#define IDM_IMPORT                      107
#define IDM_EXPORT                      108

// 菜单命令 - 编辑菜单
#define IDM_NEW_KEY                     109
#define IDM_DELETE_KEY                  110
#define IDM_NEW_VALUE                   111
#define IDM_DELETE_VALUE                112
#define IDM_EDIT_VALUE                  113
#define IDM_REFRESH                     114

// 主窗口控件ID
#define IDC_TREE_REGISTRY               1001
#define IDC_LIST_VALUES                 1002
#define IDC_EDIT_VALUE                  1003
#define IDC_BUTTON_ADD_KEY              1004
#define IDC_BUTTON_DELETE_KEY           1005
#define IDC_BUTTON_ADD_VALUE            1006
#define IDC_BUTTON_DELETE_VALUE         1007
#define IDC_BUTTON_EDIT_VALUE           1008
#define IDC_BUTTON_REFRESH              1009

// 对话框ID
#define IDD_ABOUTBOX                    101
#define IDD_NEWKEY_DIALOG               102
#define IDD_NEWVALUE_DIALOG             103

// 对话框控件ID
#define IDC_EDIT_VALUE_NAME             10001
#define IDC_EDIT_VALUE_DATA             10002
#define IDC_STATIC_NAME                 10003
#define IDC_STATIC_DATA                 10004
#define IDC_EDIT_KEY_NAME               10005
#define IDC_STATIC_KEY_NAME             10006