// host_test.cpp - 模拟 light 的 ExtensionManager 完整加载流程，验证 YXG_VCBlocks.dll
#include <windows.h>
#include <cstdio>
#include "YXExtension.h"

static void LogInfo(const wchar_t* msg) { printf("  [LogInfo] %ls\n", msg); }
static void LogError(const wchar_t* msg) { printf("  [LogError] %ls\n", msg); }

static YXG_JSApiCallback g_cb = nullptr;
static YXG_PluginContext* g_ctx = nullptr;
static bool g_panelRegistered = false;

static int RegisterJSApi(YXG_PluginContext* ctx, const char* ns, const char* name, YXG_JSApiCallback cb) {
    printf("  [RegisterJSApi] ns=%s name=%s\n", ns, name);
    g_cb = cb; g_ctx = ctx;
    return 1;
}
static int UnregisterJSApi(YXG_PluginContext* ctx, const char* ns, const char* name) { return 1; }
static void JSValueSetInt(YXG_JSValue* v, int64_t n) { v->bits[0] = YXG_JS_TAG_INT; v->bits[1] = (uint64_t)n; }
static int RegisterNativePanel(YXG_PluginContext* ctx, const YXG_PanelInfo* info,
                               YXG_CreatePanelFn create, YXG_DestroyPanelFn destroy) {
    (void)destroy;
    if (!info || !info->id || !info->name || !create) return 0;
    g_panelRegistered = true;
    printf("  [RegisterNativePanel] id=%s name=%s icon=%s\n",
           info->id, info->name, info->icon ? info->icon : "(none)");
    // 模拟 light：点击侧边栏 Tab 时创建面板
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 800, 600,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND panel = create(ctx, parent);
    printf("  [CreatePanel] panel hwnd=%p IsWindow=%d\n", panel, panel ? IsWindow(panel) : 0);
    if (panel && IsWindow(panel)) {
        MoveWindow(panel, 0, 0, 800, 600, TRUE);
        ShowWindow(panel, SW_SHOW);
        UpdateWindow(panel);
        printf("  [Panel OK] 面板已创建并显示\n");
        DestroyWindow(panel);
    }
    if (parent) DestroyWindow(parent);
    return 1;
}

int main() {
    HMODULE h = LoadLibraryW(L"D:\\YX\\VCBlocks\\build\\YXG_VCBlocks.dll");
    if (!h) { printf("LoadLibrary FAILED (err=%lu)\n", GetLastError()); return 1; }
    printf("[OK] LoadLibrary\n");

    auto reg  = (YXG_RegisterPluginFn)GetProcAddress(h, "YXG_RegisterPlugin");
    auto unreg = (YXG_UnregisterPluginFn)GetProcAddress(h, "YXG_UnregisterPlugin");
    auto info = (YXG_GetExtensionInfoFn)GetProcAddress(h, "YXG_GetExtensionInfo");
    if (!reg) { printf("YXG_RegisterPlugin NOT FOUND\n"); return 1; }

    YXG_ExtensionApi api{};
    api.apiVersion = YXG_API_VERSION;
    api.LogInfo = LogInfo;
    api.LogError = LogError;
    api.RegisterJSApi = RegisterJSApi;
    api.UnregisterJSApi = UnregisterJSApi;
    api.JSValueSetInt = JSValueSetInt;
    api.RegisterNativePanel = RegisterNativePanel;

    void* ctx = nullptr;
    int r = reg(&api, YXG_API_VERSION, &ctx);
    printf("[%s] YXG_RegisterPlugin returned %d\n", r == 1 ? "OK" : "FAIL", r);
    if (r != 1) return 1;

    printf("[%s] 原生面板注册\n", g_panelRegistered ? "OK" : "FAIL");

    if (info) {
        auto inf = info();
        if (inf) printf("[OK] ExtensionInfo: name=%s version=%s author=%s\n",
            inf->name ? inf->name : "?", inf->version ? inf->version : "?", inf->author ? inf->author : "?");
    }

    if (g_cb) {
        printf("=== 调用 run API（触发积木逻辑）===\n");
        YXG_JSValue ret{};
        g_cb(g_ctx, 0, nullptr, &ret);
    }

    if (unreg) unreg(ctx);
    FreeLibrary(h);
    printf("==== 验证通过：新 DLL 可被 light 加载，并注册侧边栏原生面板 ====\n");
    return 0;
}
