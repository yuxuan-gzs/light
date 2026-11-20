// include/ExtensionManager.h
// YXGuiPlugin v3.1 扩展管理器：扫描并加载同级目录下 YXG_*.dll

#pragma once

#include "YXExtension.h"
#include "winwrap.h"
#include <string>
#include <vector>
#include <map>
#include <functional>

// 前向声明
class JSPlugin;

// ============================================================
// 扩展 DLL 实例记录
// ============================================================
struct ExtDllRecord {
    HMODULE hMod = nullptr;
    void* context = nullptr;
    YXG_RegisterPluginFn regFn = nullptr;
    YXG_UnregisterPluginFn unregFn = nullptr;
    YXG_GetExtensionInfoFn infoFn = nullptr;
    std::wstring path;
};

// ============================================================
// Hook 记录（单条）
// ============================================================
struct HookRecord {
    YXG_HookCallback callback = nullptr;
    YXG_PluginContext* ctx = nullptr;
    int priority = 0;
};

// ============================================================
// JS API 覆盖记录
// ============================================================
struct JSApiRecord {
    std::string ns;
    std::string name;
    YXG_JSApiCallback callback = nullptr;
    YXG_PluginContext* ctx = nullptr;
};

// ============================================================
// 原生面板记录（外部 DLL 注册的原生界面）
// ============================================================
struct NativePanelRecord {
    std::string id;
    std::string name;
    std::string icon;
    YXG_PluginContext* ctx = nullptr;
    YXG_CreatePanelFn createFn = nullptr;
    YXG_DestroyPanelFn destroyFn = nullptr;
};

// ============================================================
// ExtensionManager
// ============================================================
class ExtensionManager {
public:
    ExtensionManager(JSPlugin* owner);
    ~ExtensionManager();

    void ScanAndLoad(const std::wstring& pluginsDir);
    // 加载目录下所有 *.dll（跳过 .dll.N 禁用后缀），供 GUI 附属插件宿主使用
    void ScanAndLoadGuiDll(const std::wstring& pluginsDir);
    void UnloadAll();

    // ---- Hook 链 ----
    int RegisterHook(YXG_PluginContext* ctx, YXG_HookType type, YXG_HookCallback cb, int priority);
    int UnregisterHook(YXG_PluginContext* ctx, YXG_HookType type, YXG_HookCallback cb);
    int RunHooks(YXG_HookType type, const wchar_t* pluginId, void* dataIn, void* dataOut);
    // 该类型是否有注册的 Hook（宿主侧快速判断，避免无谓开销）
    bool HasHooks(YXG_HookType type) const {
        return type >= 0 && type < YXG_HOOK_COUNT && !m_hooks[type].empty();
    }

    // ---- JS API 覆盖 ----
    int RegisterJSApi(YXG_PluginContext* ctx, const std::string& ns, const std::string& name, YXG_JSApiCallback cb);
    int UnregisterJSApi(YXG_PluginContext* ctx, const std::string& ns, const std::string& name);
    bool HasJSApi(const std::string& ns, const std::string& name) const;
    bool CallJSApi(const std::string& ns, const std::string& name, int argc, YXG_JSValue* argv, YXG_JSValue* ret) const;
    // 枚举某个命名空间下扩展注册的全部 JS API 名（供绑定层追加到 JS 对象）
    std::vector<std::string> GetJSApiNames(const std::string& ns) const;
    // 枚举扩展注册的全部命名空间（去重，含 host/win32/light 及自定义，供绑定层创建全局对象）
    std::vector<std::string> GetNamespaces() const;

    // ---- 原生面板 ----
    int RegisterNativePanel(YXG_PluginContext* ctx, const YXG_PanelInfo* info,
        YXG_CreatePanelFn create, YXG_DestroyPanelFn destroy);
    std::vector<NativePanelRecord> GetNativePanels() const;

    // ---- 构建 API 表 ----
    void BuildApiTable(YXG_ExtensionApi* api);

    std::wstring m_pluginsDir;

private:
    JSPlugin* m_owner = nullptr;
    std::vector<ExtDllRecord> m_dlls;
    std::vector<HookRecord> m_hooks[YXG_HOOK_COUNT];
    std::vector<JSApiRecord> m_jsApis;
    std::vector<NativePanelRecord> m_nativePanels;

    void LoadOne(const std::wstring& path);
};
