// PluginEntry.cpp
// VCBlocks 插件入口：作为 light 的原生面板插件（YXG ABI），在侧边栏提供图形化编程界面。
#include <windows.h>
#include <gdiplus.h>
#include "YXExtension.h"
#include "ui/MainWindow.h"
#include "core/TccCompiler.h"

using namespace vcb;

static MainWindow* g_panel = nullptr;
static ULONG_PTR g_gdiplusToken = 0;

static HINSTANCE DllInstance() {
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&DllInstance, &h);
    return (HINSTANCE)h;
}

// ---- 原生面板：创建/销毁 ----
static HWND PanelCreate(YXG_PluginContext* ctx, HWND parentHost) {
    (void)ctx;
    try {
        if (g_panel) return g_panel->Hwnd();
        g_panel = new MainWindow();
        if (!g_panel->CreatePanel(DllInstance(), parentHost)) {
            delete g_panel;
            g_panel = nullptr;
            return nullptr;
        }
        return g_panel->Hwnd();
    } catch (...) {
        // 防止 C++ 异常跨 DLL 边界导致宿主崩溃
        if (g_panel) { delete g_panel; g_panel = nullptr; }
        return nullptr;
    }
}

static void PanelDestroy(HWND h) {
    (void)h;
    if (g_panel) { delete g_panel; g_panel = nullptr; }
}

static YXG_PanelInfo g_panelInfo = {
    "com.yx.vcblocks",        // id
    "VCBlocks",              // name（侧边栏标签）
    "\xF0\x9F\xA7\xA9",      // icon 🧩
};

extern "C" {

__declspec(dllexport) int YXG_RegisterPlugin(YXG_ExtensionApi* api, int apiVersion, void** context) {
    if (apiVersion < YXG_API_VERSION) return 0;
    *context = (void*)1;

    try {
        // 初始化 GDI+
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
            return 0;
        }

        // 注册图形化编程原生面板（light 侧边栏新 Tab）
        if (api && api->RegisterNativePanel) {
            api->RegisterNativePanel((YXG_PluginContext*)*context, &g_panelInfo, PanelCreate, PanelDestroy);
        }
        if (api && api->LogInfo) api->LogInfo(L"VCBlocks loaded");
        return 1;
    } catch (...) {
        if (g_gdiplusToken) { Gdiplus::GdiplusShutdown(g_gdiplusToken); g_gdiplusToken = 0; }
        return 0;
    }
}

__declspec(dllexport) void YXG_UnregisterPlugin(void* context) {
    (void)context;
    if (g_panel) { delete g_panel; g_panel = nullptr; }
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    g_gdiplusToken = 0;
}

__declspec(dllexport) const YXG_ExtensionInfo* YXG_GetExtensionInfo(void) {
    static YXG_ExtensionInfo inf = {
        "VCBlocks", "2.0.0", "VCBlocks",
        "C++ graphical programming tool with built-in TCC compiler"
    };
    return &inf;
}

} // extern "C"
