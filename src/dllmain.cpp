// dllmain.cpp — YXG ABI 原生面板插件入口
// 作为 light 的 GUI 附属插件（Cache\gui\dll\YXG_MinecraftLauncher.dll）：
//   导出 YXG_RegisterPlugin / YXG_UnregisterPlugin / YXG_GetExtensionInfo，
//   通过 YXG_ExtensionApi::RegisterNativePanel 注册一个原生面板，
//   在 light 原 GUI 的侧边栏显示为「我的世界启动器」Tab，点击后由本 DLL
//   用 GDI+ 自绘子窗口填满内容区（HMCL 风格 + PCL2 背景/Mod 前置）。
#include "YXExtension.h"
#include "Ui.h"

#include <windows.h>

// ============================================================
// 全局状态
// ============================================================
static HINSTANCE g_hInst = nullptr;
static ULONG_PTR g_gdiToken = 0;
static bool g_gdiInit = false;
static ui::Ui* g_launcherUi = nullptr;

static void EnsureGdiPlus() {
    if (g_gdiInit) return;
    Gdiplus::GdiplusStartupInput si;
    si.GdiplusVersion = 1;
    if (Gdiplus::GdiplusStartup(&g_gdiToken, &si, nullptr) == Gdiplus::Ok) {
        g_gdiInit = true;
    }
}

// ============================================================
// 面板生命周期
// ============================================================
static HWND CreateLauncherPanel(YXG_PluginContext* ctx, HWND parentHost) {
    (void)ctx;
    EnsureGdiPlus();

    // 复用已有窗口（宿主切换离开后可能再次进入）
    if (g_launcherUi && g_launcherUi->hwnd() && IsWindow(g_launcherUi->hwnd())) {
        SetParent(g_launcherUi->hwnd(), parentHost);
        return g_launcherUi->hwnd();
    }

    if (g_launcherUi) { delete g_launcherUi; g_launcherUi = nullptr; }
    g_launcherUi = new ui::Ui();
    if (!g_launcherUi->Create(g_hInst, parentHost)) {
        delete g_launcherUi;
        g_launcherUi = nullptr;
        return nullptr;
    }
    return g_launcherUi->hwnd();
}

static void DestroyLauncherPanel(HWND panel) {
    (void)panel;
    if (g_launcherUi) {
        delete g_launcherUi;   // Ui 析构会销毁窗口并置空内部指针
        g_launcherUi = nullptr;
    }
}

// ============================================================
// YXG ABI 导出
// ============================================================
extern "C" __declspec(dllexport)
int YXG_RegisterPlugin(YXG_ExtensionApi* api, int apiVersion, void** context) {
    if (!api || apiVersion < YXG_API_VERSION) return 0;
    EnsureGdiPlus();
    (void)context;

    if (api->RegisterNativePanel) {
        YXG_PanelInfo info;
        info.id = "com.yx.minecraft.launcher";
        info.name = "我的世界启动器";
        info.icon = "\xE2\x9B\x8F";   // ⛏ (U+26CF) UTF-8
        api->RegisterNativePanel(nullptr, &info,
            CreateLauncherPanel, DestroyLauncherPanel);
    }
    return 1;
}

extern "C" __declspec(dllexport)
void YXG_UnregisterPlugin(void* context) {
    (void)context;
    if (g_launcherUi) {
        delete g_launcherUi;
        g_launcherUi = nullptr;
    }
    if (g_gdiInit) {
        Gdiplus::GdiplusShutdown(g_gdiToken);
        g_gdiInit = false;
        g_gdiToken = 0;
    }
}

extern "C" __declspec(dllexport)
const YXG_ExtensionInfo* YXG_GetExtensionInfo() {
    static const YXG_ExtensionInfo info = {
        "YXG Minecraft Launcher",
        "1.0.0",
        "yuxuan",
        "HMCL 风格我的世界第三方启动器（版本管理/下载/离线+微软登录/启动/背景/Mod 前置）"
    };
    return &info;
}

BOOL WINAPI DllMain(HINSTANCE hInstDll, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hInstDll;
        DisableThreadLibraryCalls(hInstDll);
    }
    return TRUE;
}
