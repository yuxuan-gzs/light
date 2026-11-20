#pragma once

#include <cstdint>
#include <string>
#include "winwrap.h"

class JSPlugin;

// JSPlugin -> YXGuiPlugin WebView2 bridge message.
// All requests are consumed on the YXGui UI thread.
#ifndef WM_YXGUI_JS_WEBVIEW
#define WM_YXGUI_JS_WEBVIEW (WM_APP + 0x4C0)
#endif

enum class JSWebViewOp : int {
    Navigate = 1,
    GetCookies = 2,
    ExecuteScript = 3
};

struct JSWebViewRequest {
    JSPlugin* plugin = nullptr;
    uint32_t seq = 0;
    JSWebViewOp op = JSWebViewOp::Navigate;
    bool newTab = false;
    std::wstring url;
    std::wstring script;
};
