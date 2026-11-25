// src/ExtensionManager.cpp
// ============================================================================
// winsock2.h 保护：必须在 windows.h 之前包含 winsock2.h，
// 否则 winsock.h 会被 windows.h 先包含，导致 WSA* / sockaddr 等重定义。
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

// YXGuiPlugin v3.1 扩展管理器：扫描并加载同级目录下 YXG_*.dll

#include "ExtensionManager.h"
#include "JSPlugin.h"
#include <algorithm>
#include <string>

// ============================================================
// 全局状态（供静态 C 回调访问）
// ============================================================
static ExtensionManager* g_apiTableOwner = nullptr;

// ============================================================
// ExtensionManager 方法实现
// ============================================================
ExtensionManager::ExtensionManager(JSPlugin* owner) : m_owner(owner) {}
ExtensionManager::~ExtensionManager() { UnloadAll(); }

void ExtensionManager::ScanAndLoad(const std::wstring& pluginsDir) {
    m_pluginsDir = pluginsDir;
    if (pluginsDir.empty()) return;
    std::wstring searchPath = pluginsDir;
    if (searchPath.back() != L'\\' && searchPath.back() != L'/') searchPath += L'\\';
    searchPath += L"YXG_*.dll";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring fullPath = pluginsDir;
            if (fullPath.back() != L'\\' && fullPath.back() != L'/') fullPath += L'\\';
            fullPath += fd.cFileName;
            LoadOne(fullPath);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void ExtensionManager::ScanAndLoadGuiDll(const std::wstring& pluginsDir) {
    m_pluginsDir = pluginsDir;
    if (pluginsDir.empty()) return;
    std::wstring searchPath = pluginsDir;
    if (searchPath.back() != L'\\' && searchPath.back() != L'/') searchPath += L'\\';
    searchPath += L"*.dll";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring fn = fd.cFileName;
        if (fn.size() < 4) continue;
        // 严格校验扩展名为 .dll，避免 *.dll 匹配到 name.dll.N（禁用后缀）
        if (_wcsicmp(fn.substr(fn.size() - 4).c_str(), L".dll") != 0) continue;
        std::wstring fullPath = pluginsDir;
        if (fullPath.back() != L'\\' && fullPath.back() != L'/') fullPath += L'\\';
        fullPath += fd.cFileName;
        LoadOne(fullPath);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void ExtensionManager::UnloadAll() {
    for (auto& rec : m_dlls) {
        if (rec.unregFn && rec.context) {
            rec.unregFn(rec.context);
        }
        if (rec.hMod) {
            FreeLibrary(rec.hMod);
        }
    }
    m_dlls.clear();
    for (auto& chain : m_hooks) chain.clear();
    m_jsApis.clear();
    m_nativePanels.clear();
}

int ExtensionManager::RegisterHook(YXG_PluginContext* ctx, YXG_HookType type, YXG_HookCallback cb, int priority) {
    if (type < 0 || type >= YXG_HOOK_COUNT) return 0;
    HookRecord rec; rec.callback = cb; rec.ctx = ctx; rec.priority = priority;
    auto& chain = m_hooks[type];
    chain.push_back(rec);
    std::sort(chain.begin(), chain.end(), [](const HookRecord& a, const HookRecord& b) {
        return a.priority > b.priority;
    });
    return 1;
}

int ExtensionManager::UnregisterHook(YXG_PluginContext* ctx, YXG_HookType type, YXG_HookCallback cb) {
    if (type < 0 || type >= YXG_HOOK_COUNT) return 0;
    auto& chain = m_hooks[type];
    chain.erase(std::remove_if(chain.begin(), chain.end(),
        [ctx, cb](const HookRecord& r) { return r.ctx == ctx && r.callback == cb; }), chain.end());
    return 1;
}

int ExtensionManager::RunHooks(YXG_HookType type, const wchar_t* pluginId, void* dataIn, void* dataOut) {
    if (type < 0 || type >= YXG_HOOK_COUNT) return 0;
    for (const auto& rec : m_hooks[type]) {
        if (rec.callback && rec.callback(rec.ctx, pluginId, type, dataIn, dataOut)) {
            return 1;
        }
    }
    return 0;
}

int ExtensionManager::RegisterJSApi(YXG_PluginContext* ctx, const std::string& ns, const std::string& name, YXG_JSApiCallback cb) {
    JSApiRecord rec; rec.ns = ns; rec.name = name; rec.callback = cb; rec.ctx = ctx;
    auto it = std::find_if(m_jsApis.begin(), m_jsApis.end(),
        [&ns, &name](const JSApiRecord& r) { return r.ns == ns && r.name == name; });
    if (it != m_jsApis.end()) {
        *it = rec;
    } else {
        m_jsApis.push_back(rec);
    }
    return 1;
}

int ExtensionManager::UnregisterJSApi(YXG_PluginContext* ctx, const std::string& ns, const std::string& name) {
    m_jsApis.erase(std::remove_if(m_jsApis.begin(), m_jsApis.end(),
        [ctx, &ns, &name](const JSApiRecord& r) { return r.ctx == ctx && r.ns == ns && r.name == name; }), m_jsApis.end());
    return 1;
}

bool ExtensionManager::HasJSApi(const std::string& ns, const std::string& name) const {
    for (const auto& r : m_jsApis) {
        if (r.ns == ns && r.name == name) return true;
    }
    return false;
}

bool ExtensionManager::CallJSApi(const std::string& ns, const std::string& name,
    int argc, YXG_JSValue* argv, YXG_JSValue* ret) const {
    for (const auto& r : m_jsApis) {
        if (r.ns == ns && r.name == name) {
            if (r.callback) r.callback(r.ctx, argc, argv, ret);
            return true;
        }
    }
    return false;
}

std::vector<std::string> ExtensionManager::GetJSApiNames(const std::string& ns) const {
    std::vector<std::string> out;
    for (const auto& r : m_jsApis) {
        if (r.ns == ns) {
            // 去重（同 ns 下同名只出现一次）
            if (std::find(out.begin(), out.end(), r.name) == out.end()) {
                out.push_back(r.name);
            }
        }
    }
    return out;
}

std::vector<std::string> ExtensionManager::GetNamespaces() const {
    std::vector<std::string> out;
    for (const auto& r : m_jsApis) {
        if (r.ns.empty()) continue;
        if (std::find(out.begin(), out.end(), r.ns) == out.end()) {
            out.push_back(r.ns);
        }
    }
    return out;
}

int ExtensionManager::RegisterNativePanel(YXG_PluginContext* ctx,
    const YXG_PanelInfo* info, YXG_CreatePanelFn create, YXG_DestroyPanelFn destroy) {
    if (!info || !info->id || !info->name || !create) return 0;
    NativePanelRecord rec;
    rec.id = info->id;
    rec.name = info->name;
    rec.icon = info->icon ? info->icon : "";
    rec.ctx = ctx;
    rec.createFn = create;
    rec.destroyFn = destroy;
    // 同 id 覆盖（后注册优先，与 JS API 一致）
    auto it = std::find_if(m_nativePanels.begin(), m_nativePanels.end(),
        [&rec](const NativePanelRecord& r) { return r.id == rec.id; });
    if (it != m_nativePanels.end()) *it = rec;
    else m_nativePanels.push_back(rec);
    return 1;
}

std::vector<NativePanelRecord> ExtensionManager::GetNativePanels() const {
    return m_nativePanels;
}

void ExtensionManager::LoadOne(const std::wstring& path) {
    HMODULE h = LoadLibraryW(path.c_str());
    if (!h) return;
    auto reg = (YXG_RegisterPluginFn)GetProcAddress(h, "YXG_RegisterPlugin");
    auto unreg = (YXG_UnregisterPluginFn)GetProcAddress(h, "YXG_UnregisterPlugin");
    auto info = (YXG_GetExtensionInfoFn)GetProcAddress(h, "YXG_GetExtensionInfo");
    if (!reg) {
        FreeLibrary(h);
        return;
    }
    YXG_ExtensionApi api{};
    api.apiVersion = YXG_API_VERSION;
    BuildApiTable(&api);
    void* ctx = nullptr;
    if (!reg(&api, YXG_API_VERSION, &ctx)) {
        FreeLibrary(h);
        return;
    }
    ExtDllRecord rec;
    rec.hMod = h; rec.context = ctx; rec.regFn = reg; rec.unregFn = unreg; rec.infoFn = info; rec.path = path;
    m_dlls.push_back(rec);
    if (info) {
        auto inf = info();
        if (inf && inf->name) {
            OutputDebugStringA(("[YXExtension] Loaded: " + std::string(inf->name) +
                " v" + (inf->version ? inf->version : "?") + "\n").c_str());
        }
    }
}

// ============================================================
// JS 值 helper（静态实现）
// 统一布局（宿主与扩展 DLL 共用约定）：
//   bits[0] 低 8 位 = tag（YXG_JS_TAG_*）
//   bits[0] 其余 56 位 + bits[1] 低 8 位 = 64 位值
//     （int64 原值 / bool 0|1 / 字符串指针）
//   bits[1] 其余位恒 0（保留）
// ============================================================
static uint64_t ExtValueOf(const YXG_JSValue* v) {
    return (v->bits[0] >> 8) | (v->bits[1] << 56);
}
static void ExtSetValue(YXG_JSValue* v, int tag, uint64_t val) {
    v->bits[0] = ((uint64_t)(tag & 0xFF)) | (val << 8);
    v->bits[1] = val >> 56;
}

static int JSValueGetTagImpl(const YXG_JSValue* v) {
    if (!v) return YXG_JS_TAG_UNDEFINED;
    int tag = (int)(v->bits[0] & 0xFF);
    if (tag < YXG_JS_TAG_INT || tag > YXG_JS_TAG_EXCEPTION) return YXG_JS_TAG_UNDEFINED;
    return tag;
}

static int64_t JSValueGetIntImpl(const YXG_JSValue* v) {
    if (!v) return 0;
    return (int64_t)ExtValueOf(v);
}

static int JSValueGetBoolImpl(const YXG_JSValue* v) {
    if (!v) return 0;
    return (int)ExtValueOf(v);
}

static const char* JSValueGetStringImpl(const YXG_JSValue* v) {
    if (!v) return "";
    return (const char*)(uintptr_t)ExtValueOf(v);
}

static void JSValueSetIntImpl(YXG_JSValue* v, int64_t n) {
    if (!v) return;
    ExtSetValue(v, YXG_JS_TAG_INT, (uint64_t)n);
}

static void JSValueSetBoolImpl(YXG_JSValue* v, int b) {
    if (!v) return;
    ExtSetValue(v, YXG_JS_TAG_BOOL, b ? 1 : 0);
}

static void JSValueSetStringImpl(YXG_JSValue* v, const char* s) {
    if (!v) return;
    ExtSetValue(v, YXG_JS_TAG_STRING, (uint64_t)(uintptr_t)s);
}

static void JSValueSetUndefinedImpl(YXG_JSValue* v) {
    if (!v) return;
    ExtSetValue(v, YXG_JS_TAG_UNDEFINED, 0);
}

static void JSValueSetNullImpl(YXG_JSValue* v) {
    if (!v) return;
    ExtSetValue(v, YXG_JS_TAG_NULL, 0);
}

// ============================================================
// 控件操作 helper（静态实现）
// ============================================================
static HWND GetControlHwndImpl(const wchar_t* pluginId, const char* controlId) {
    (void)pluginId; (void)controlId;
    return nullptr;
}

static int SetControlTextImpl(const wchar_t* pluginId, const char* controlId, const wchar_t* text) {
    (void)pluginId; (void)controlId; (void)text;
    return 0;
}

static int GetControlTextImpl(const wchar_t* pluginId, const char* controlId, wchar_t* buf, int bufLen) {
    (void)pluginId; (void)controlId; (void)buf; (void)bufLen;
    return 0;
}

// ============================================================
// 构建 API 表
// ============================================================
static int ExtRegisterHook(YXG_PluginContext* ctx, YXG_HookType type, YXG_HookCallback cb, int priority) {
    return g_apiTableOwner ? g_apiTableOwner->RegisterHook(ctx, type, cb, priority) : 0;
}
static int ExtUnregisterHook(YXG_PluginContext* ctx, YXG_HookType type, YXG_HookCallback cb) {
    return g_apiTableOwner ? g_apiTableOwner->UnregisterHook(ctx, type, cb) : 0;
}
static int ExtRegisterJSApi(YXG_PluginContext* ctx, const char* ns, const char* name, YXG_JSApiCallback cb) {
    return g_apiTableOwner ? g_apiTableOwner->RegisterJSApi(ctx, ns, name, cb) : 0;
}
static int ExtUnregisterJSApi(YXG_PluginContext* ctx, const char* ns, const char* name) {
    return g_apiTableOwner ? g_apiTableOwner->UnregisterJSApi(ctx, ns, name) : 0;
}
static int ExtRegisterNativePanel(YXG_PluginContext* ctx, const YXG_PanelInfo* info,
    YXG_CreatePanelFn create, YXG_DestroyPanelFn destroy) {
    return g_apiTableOwner ? g_apiTableOwner->RegisterNativePanel(ctx, info, create, destroy) : 0;
}
static void ExtLogInfo(const wchar_t* msg) {
    OutputDebugStringW(msg); OutputDebugStringW(L"\n");
}
static void ExtLogError(const wchar_t* msg) {
    OutputDebugStringW(L"[YXExtension ERR] "); OutputDebugStringW(msg); OutputDebugStringW(L"\n");
}
static const wchar_t* ExtGetPluginsDir(void) {
    static std::wstring s_dir;
    if (g_apiTableOwner) s_dir = g_apiTableOwner->m_pluginsDir;
    return s_dir.c_str();
}
static const char* ExtGetHostVersion(void) { return "3.1.0"; }
static int ExtCallOriginal(YXG_HookType hookType, void* dataIn, void* dataOut) {
    (void)hookType; (void)dataIn; (void)dataOut;
    return 0;
}

void ExtensionManager::BuildApiTable(YXG_ExtensionApi* api) {
    g_apiTableOwner = this;

    api->RegisterHook = ExtRegisterHook;
    api->UnregisterHook = ExtUnregisterHook;
    api->RegisterJSApi = ExtRegisterJSApi;
    api->UnregisterJSApi = ExtUnregisterJSApi;
    api->LogInfo = ExtLogInfo;
    api->LogError = ExtLogError;
    api->GetPluginsDir = ExtGetPluginsDir;
    api->GetHostVersion = ExtGetHostVersion;

    api->JSValueGetTag = JSValueGetTagImpl;
    api->JSValueGetInt = JSValueGetIntImpl;
    api->JSValueGetBool = JSValueGetBoolImpl;
    api->JSValueGetString = JSValueGetStringImpl;
    api->JSValueSetInt = JSValueSetIntImpl;
    api->JSValueSetBool = JSValueSetBoolImpl;
    api->JSValueSetString = JSValueSetStringImpl;
    api->JSValueSetUndefined = JSValueSetUndefinedImpl;
    api->JSValueSetNull = JSValueSetNullImpl;

    api->GetControlHwnd = GetControlHwndImpl;
    api->SetControlText = SetControlTextImpl;
    api->GetControlText = GetControlTextImpl;
    api->CallOriginal = ExtCallOriginal;

    api->RegisterNativePanel = ExtRegisterNativePanel;
}

