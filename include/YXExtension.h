// include/YXExtension.h
// YXGuiPlugin 扩展接口：外部 DLL 可通过实现导出函数注册到宿主，
// 从而添加新 JS API、拦截现有 API、Hook 控件创建/渲染/HTTP/生命周期。
// 所有接口均为纯 C（extern "C"），方便不同编译器混用。
//
// 外部 DLL 必须导出以下符号（至少一个）：
//   extern "C" __declspec(dllexport) int YXG_RegisterPlugin(YXG_ExtensionApi* api, int apiVersion);
//   extern "C" __declspec(dllexport) void YXG_UnregisterPlugin(void* context);
//
// 命名约定：外部 DLL 文件名前缀建议为 YXG_，放在 plugins\ 目录下与 YXGuiPlugin.dll 同级。

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "winwrap.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
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
// JS 值封装（QuickJS 不透明暴露，扩展 DLL 通过宿主 helper 操作）
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
    const char* type;   // "label"/"edit"/"multiline"/"button"/"panel"
    const char* id;
    const char* text;
    int x, y, w, h;
    const char* bgColor;    // 可为 nullptr；格式 "#RRGGBB"
    const char* fgColor;    // 可为 nullptr
    const char* borderColor;// 可为 nullptr
} YXG_ControlDesc;

// ============================================================
// HTTP 请求描述（供 httpRequest Hook 使用）
// ============================================================
typedef struct YXG_HttpRequest {
    const char* url;
    const char* method;
    const char* headersJson; // JSON object string，如 {"Content-Type":"application/json"}
    const char* body;
    int timeoutMs;
} YXG_HttpRequest;

typedef struct YXG_HttpResponse {
    int status;         // HTTP 状态码；<0 表示传输错误
    const char* body;   // UTF-8 响应体（宿主管理内存，扩展只读）
    const char* error;  // 错误描述（status < 0 时有效）
} YXG_HttpResponse;

// ============================================================
// JS API 调用信息（供 YXG_HOOK_JSAPI_CALL 使用）
// ============================================================
// argv 由宿主从 JS 参数转换而来（扩展用 JSValueGet* 读取，hook 期间有效）；
// ret 是输出区：
//   - pre 阶段：扩展可调用 JSValueSet* 写入 ret 并返回 1 以拦截调用；
//   - post 阶段：ret 包含原函数结果的转换值，扩展可改写。
typedef struct YXG_JsApiCallInfo {
    const char* ns;         // "win32" / "host" / "light"
    const char* name;       // 函数名
    int argc;               // 参数个数
    YXG_JSValue* argv;      // 参数数组（可空）
    YXG_JSValue* ret;       // 返回值输出区（可空）
    int stage;              // 0=pre(调用前), 1=post(调用后)
} YXG_JsApiCallInfo;

// ============================================================
// Hook 类型枚举
// ============================================================
typedef enum {
    YXG_HOOK_CREATE_CONTROL_PRE,   // 控件创建前
    YXG_HOOK_CREATE_CONTROL_POST,  // 控件创建后
    YXG_HOOK_RENDER_PRE,           // WM_PAINT 自绘前（button/panel）
    YXG_HOOK_RENDER_POST,          // WM_PAINT 自绘后
    YXG_HOOK_HTTP_PRE,             // HTTP 请求发出前
    YXG_HOOK_HTTP_POST,            // HTTP 响应返回后
    YXG_HOOK_JSAPI_CALL,           // JS API 调用时（win32/host/light 任一函数）
    YXG_HOOK_LIFECYCLE_LOAD,       // onLoad 前
    YXG_HOOK_LIFECYCLE_SHOW,       // onViewShow 前
    YXG_HOOK_LIFECYCLE_HIDE,       // onViewHide 前
    YXG_HOOK_LIFECYCLE_RESIZE,     // onResize 前
    YXG_HOOK_LIFECYCLE_UNLOAD,     // onUnload 前
    YXG_HOOK_CONTROL_EVENT,        // onControlEvent 前
    YXG_HOOK_COUNT
} YXG_HookType;

// ============================================================
// Hook 回调签名
// ============================================================
// 返回 1 = 拦截（不再执行后续链和原函数）；0 = 继续
// resultOut / resultInOut 视 Hook 类型而定，可为 nullptr
//
// ctx: 扩展 DLL 在 Register 时得到的 context（可用于存储自己的状态）
// pluginId: 当前正在执行的 JS 插件 ID
//
typedef int (*YXG_HookCallback)(
    YXG_PluginContext* ctx,
    const wchar_t* pluginId,
    YXG_HookType hookType,
    void* dataIn,      // 输入数据（如 YXG_ControlDesc* / YXG_HttpRequest* / JS 参数）
    void* dataOut      // 输出/修改数据（如 YXG_HttpResponse* / 返回值）
);

// ============================================================
// 注册新 JS API 的回调签名
// ============================================================
// name: API 名称（如 "myFunc"）
// argc: 参数个数
// argv: 参数数组（YXG_JSValue*）
// ret: 返回值（扩展写入）
// ctx: 扩展 DLL 的上下文
//
typedef void (*YXG_JSApiCallback)(
    YXG_PluginContext* ctx,
    int argc,
    YXG_JSValue* argv,
    YXG_JSValue* ret
);

// ============================================================
// 原生面板（外部 DLL 直接提供原生 Win32 界面，宿主挂载到侧边栏）
// ============================================================
// 面板元信息（DLL 提供静态字符串，宿主会拷贝保存）
typedef struct YXG_PanelInfo {
    const char* id;      // 唯一 id（如 "com.yx.minecraft"），不能为空
    const char* name;    // 显示名（侧边栏标签），不能为空
    const char* icon;    // emoji 图标（UTF-8，如 "\xE2\x9B\x8F"），可空
} YXG_PanelInfo;

// 创建面板：返回一个 WS_CHILD 子窗口 HWND（父窗口 = 宿主内容区 parentHost）。
// 面板应自行渲染全部界面；宿主随后会 MoveWindow 将面板填满内容区。
// 返回 nullptr 表示创建失败。
typedef HWND (*YXG_CreatePanelFn)(YXG_PluginContext* ctx, HWND parentHost);

// 销毁面板（宿主在切换离开/卸载插件时调用）
typedef void (*YXG_DestroyPanelFn)(HWND panel);

// ============================================================
// 宿主提供的 API 表（扩展 DLL 通过此表与宿主交互）
// ============================================================
typedef struct YXG_ExtensionApi {
    // ---- 版本校验 ----
    int apiVersion; // 宿主写入 YXG_API_VERSION

    // ---- Hook 注册/注销 ----
    // 注册一个 Hook；priority 越大越先执行（默认 0）
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

    // ---- JS API 注册 ----
    // namespace: "win32" / "host" / "light" / 自定义
    // 若 name 已存在，扩展版本会覆盖原版本（冲突处理：后注册优先，支持 Unregister 恢复）
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

    // ---- 日志 ----
    void (*LogInfo)(const wchar_t* msg);
    void (*LogError)(const wchar_t* msg);

    // ---- 宿主路径查询 ----
    // 返回 plugins\ 目录绝对路径（wchar_t*，宿主管理内存，只读）
    const wchar_t* (*GetPluginsDir)(void);
    // 返回当前宿主版本号字符串
    const char* (*GetHostVersion)(void);

    // ---- JS 值 helper（扩展 DLL 通过这些函数操作 JS 参数/返回值） ----
    int         (*JSValueGetTag)(const YXG_JSValue* v);
    int64_t     (*JSValueGetInt)(const YXG_JSValue* v);
    int         (*JSValueGetBool)(const YXG_JSValue* v);
    // 返回 UTF-8 字符串（宿主管理内存，只读）
    const char* (*JSValueGetString)(const YXG_JSValue* v);
    // 构造返回值
    void        (*JSValueSetInt)(YXG_JSValue* v, int64_t n);
    void        (*JSValueSetBool)(YXG_JSValue* v, int b);
    void        (*JSValueSetString)(YXG_JSValue* v, const char* s);
    void        (*JSValueSetUndefined)(YXG_JSValue* v);
    void        (*JSValueSetNull)(YXG_JSValue* v);

    // ---- 控件操作 helper ----
    HWND (*GetControlHwnd)(const wchar_t* pluginId, const char* controlId);
    int  (*SetControlText)(const wchar_t* pluginId, const char* controlId, const wchar_t* text);
    int  (*GetControlText)(const wchar_t* pluginId, const char* controlId, wchar_t* buf, int bufLen);

    // ---- 调用原功能（扩展 Hook 中想放行时调用） ----
    // 例如 Hook 了 YXG_HOOK_HTTP_PRE 后仍想发请求，调用此函数执行原始 httpRequest
    int (*CallOriginal)(YXG_HookType hookType, void* dataIn, void* dataOut);

    // ---- 原生面板注册（DLL 提供原生界面，宿主挂载到侧边栏显示为新 Tab） ----
    // 返回 1 = 注册成功；0 = 失败（id/name 为空或 createFn 为空）
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
// 宿主加载扩展 DLL 后，会调用 YXG_RegisterPlugin。
// api: 宿主提供的 API 表（扩展 DLL 保存此指针供后续使用）
// apiVersion: 宿主传入的当前 API 版本（扩展 DLL 应校验 >= 所需版本）
// 返回值：1 = 注册成功；0 = 失败（宿主会忽略此 DLL）
// 输出 context: 扩展 DLL 可写入自己的状态指针，后续 Hook 回调会回传
//
// 示例：
//   extern "C" __declspec(dllexport)
//   int YXG_RegisterPlugin(YXG_ExtensionApi* api, int apiVersion, void** context) {
//       if (apiVersion < 1) return 0;
//       MyState* st = new MyState();
//       *context = st;
//       api->RegisterHook((YXG_PluginContext*)st, YXG_HOOK_CREATE_CONTROL_PRE, MyHook, 0);
//       api->RegisterJSApi((YXG_PluginContext*)st, "win32", "myCustomApi", MyApi);
//       return 1;
//   }
//
//   extern "C" __declspec(dllexport)
//   void YXG_UnregisterPlugin(void* context) {
//       delete (MyState*)context;
//   }
//
typedef int (*YXG_RegisterPluginFn)(YXG_ExtensionApi* api, int apiVersion, void** context);
typedef void (*YXG_UnregisterPluginFn)(void* context);

// ============================================================
// 可选：导出扩展元信息（宿主会在日志中显示）
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
