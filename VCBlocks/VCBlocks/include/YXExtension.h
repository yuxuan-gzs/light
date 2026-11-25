// YXExtension.h
// light 项目 GUI 附属插件 ABI（自包含副本，供生成的 DLL 直接编译使用）
// ----------------------------------------------------------------------------
// 本文件是 light 宿主 (D:\YX\light\light\Gui\YXExtension.h) 的兼容副本，
// 仅去掉了对 winwrap.h 的依赖（把其内容内联），其余结构、常量、函数签名
// 与宿主完全一致，确保 VCBlocks 编译出来的 DLL 能被 light 的
// GuiDllPluginHost / ExtensionManager 正确加载。
//
// 外部 DLL 必须导出以下符号（至少一个）：
//   extern "C" __declspec(dllexport) int  YXG_RegisterPlugin(YXG_ExtensionApi* api, int apiVersion, void** context);
//   extern "C" __declspec(dllexport) void YXG_UnregisterPlugin(void* context);
//   （可选）extern "C" __declspec(dllexport) const YXG_ExtensionInfo* YXG_GetExtensionInfo(void);
//
// 命名约定：DLL 文件名前缀建议为 YXG_，放到 light 的 Cache\gui\dll\ 目录下。
// 禁用态：name.dll 重命名为 name.dll.N。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 导出宏（生成 DLL 时使用）
#ifndef YXG_EXPORT
#ifdef YXG_BUILD_DLL
#define YXG_EXPORT __declspec(dllexport)
#else
#define YXG_EXPORT
#endif
#endif

// ============================================================
// 版本
// ============================================================
#define YXG_API_VERSION 1

// ============================================================
// 不透明上下文
// ============================================================
typedef struct YXG_PluginContext YXG_PluginContext;

// ============================================================
// JS 值封装（QuickJS 不透明暴露）
// ============================================================
typedef struct YXG_JSValue {
    uint64_t bits[2];
} YXG_JSValue;

// JS 值类型 tag
#define YXG_JS_TAG_INT        0
#define YXG_JS_TAG_BOOL       1
#define YXG_JS_TAG_STRING     2
#define YXG_JS_TAG_OBJECT     3
#define YXG_JS_TAG_UNDEFINED  4
#define YXG_JS_TAG_NULL       5
#define YXG_JS_TAG_EXCEPTION  6

// ============================================================
// 控件描述（供 createControl Hook 使用）
// ============================================================
typedef struct YXG_ControlDesc {
    const char* type;
    const char* id;
    const char* text;
    int x, y, w, h;
    const char* bgColor;
    const char* fgColor;
    const char* borderColor;
} YXG_ControlDesc;

// ============================================================
// HTTP 请求/响应描述
// ============================================================
typedef struct YXG_HttpRequest {
    const char* url;
    const char* method;
    const char* headersJson;
    const char* body;
    int timeoutMs;
} YXG_HttpRequest;

typedef struct YXG_HttpResponse {
    int status;
    const char* body;
    const char* error;
} YXG_HttpResponse;

// ============================================================
// JS API 调用信息
// ============================================================
typedef struct YXG_JsApiCallInfo {
    const char* ns;
    const char* name;
    int argc;
    YXG_JSValue* argv;
    YXG_JSValue* ret;
    int stage;
} YXG_JsApiCallInfo;

// ============================================================
// Hook 类型枚举
// ============================================================
typedef enum {
    YXG_HOOK_CREATE_CONTROL_PRE,
    YXG_HOOK_CREATE_CONTROL_POST,
    YXG_HOOK_RENDER_PRE,
    YXG_HOOK_RENDER_POST,
    YXG_HOOK_HTTP_PRE,
    YXG_HOOK_HTTP_POST,
    YXG_HOOK_JSAPI_CALL,
    YXG_HOOK_LIFECYCLE_LOAD,
    YXG_HOOK_LIFECYCLE_SHOW,
    YXG_HOOK_LIFECYCLE_HIDE,
    YXG_HOOK_LIFECYCLE_RESIZE,
    YXG_HOOK_LIFECYCLE_UNLOAD,
    YXG_HOOK_CONTROL_EVENT,
    YXG_HOOK_COUNT
} YXG_HookType;

// ============================================================
// Hook 回调签名
// ============================================================
typedef int (*YXG_HookCallback)(
    YXG_PluginContext* ctx,
    const wchar_t* pluginId,
    YXG_HookType hookType,
    void* dataIn,
    void* dataOut
);

// ============================================================
// JS API 回调签名
// ============================================================
typedef void (*YXG_JSApiCallback)(
    YXG_PluginContext* ctx,
    int argc,
    YXG_JSValue* argv,
    YXG_JSValue* ret
);

// ============================================================
// 原生面板（外部 DLL 直接提供原生 Win32 界面，宿主挂载到侧边栏）
// ============================================================
typedef struct YXG_PanelInfo {
    const char* id;      // 唯一 id（如 "com.yx.minecraft"），不能为空
    const char* name;    // 显示名（侧边栏标签），不能为空
    const char* icon;    // emoji 图标（UTF-8），可空
} YXG_PanelInfo;

typedef HWND (*YXG_CreatePanelFn)(YXG_PluginContext* ctx, HWND parentHost);
typedef void (*YXG_DestroyPanelFn)(HWND panel);

// ============================================================
// 宿主提供的 API 表
// ============================================================
typedef struct YXG_ExtensionApi {
    int apiVersion;

    int (*RegisterHook)(
        YXG_PluginContext* ctx,
        YXG_HookType hookType,
        YXG_HookCallback callback,
        int priority
    );
    int (*UnregisterHook)(
        YXG_PluginContext* ctx,
        YXG_HookType hookType,
        YXG_HookCallback callback
    );

    int (*RegisterJSApi)(
        YXG_PluginContext* ctx,
        const char* namespace_,
        const char* name,
        YXG_JSApiCallback callback
    );
    int (*UnregisterJSApi)(
        YXG_PluginContext* ctx,
        const char* namespace_,
        const char* name
    );

    void (*LogInfo)(const wchar_t* msg);
    void (*LogError)(const wchar_t* msg);

    const wchar_t* (*GetPluginsDir)(void);
    const char* (*GetHostVersion)(void);

    int         (*JSValueGetTag)(const YXG_JSValue* v);
    int64_t     (*JSValueGetInt)(const YXG_JSValue* v);
    int         (*JSValueGetBool)(const YXG_JSValue* v);
    const char* (*JSValueGetString)(const YXG_JSValue* v);
    void        (*JSValueSetInt)(YXG_JSValue* v, int64_t n);
    void        (*JSValueSetBool)(YXG_JSValue* v, int b);
    void        (*JSValueSetString)(YXG_JSValue* v, const char* s);
    void        (*JSValueSetUndefined)(YXG_JSValue* v);
    void        (*JSValueSetNull)(YXG_JSValue* v);

    HWND (*GetControlHwnd)(const wchar_t* pluginId, const char* controlId);
    int  (*SetControlText)(const wchar_t* pluginId, const char* controlId, const wchar_t* text);
    int  (*GetControlText)(const wchar_t* pluginId, const char* controlId, wchar_t* buf, int bufLen);

    int (*CallOriginal)(YXG_HookType hookType, void* dataIn, void* dataOut);

    // 注册原生面板（DLL 提供原生界面，宿主挂载到侧边栏显示为新 Tab）
    int (*RegisterNativePanel)(
        YXG_PluginContext* ctx,
        const YXG_PanelInfo* info,
        YXG_CreatePanelFn createFn,
        YXG_DestroyPanelFn destroyFn
    );
} YXG_ExtensionApi;

// ============================================================
// 扩展 DLL 必须导出的入口函数
// ============================================================
typedef int (*YXG_RegisterPluginFn)(YXG_ExtensionApi* api, int apiVersion, void** context);
typedef void (*YXG_UnregisterPluginFn)(void* context);

// ============================================================
// 可选：扩展元信息
// ============================================================
typedef struct YXG_ExtensionInfo {
    const char* name;
    const char* version;
    const char* author;
    const char* description;
} YXG_ExtensionInfo;

typedef const YXG_ExtensionInfo* (*YXG_GetExtensionInfoFn)(void);

#ifdef __cplusplus
}
#endif
