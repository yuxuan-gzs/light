// src/JSPlugin.cpp
// ============================================================================
// winsock2.h 保护：必须在 windows.h 之前包含 winsock2.h，
// 否则 winsock.h 会被 windows.h 先包含，导致 WSA* / sockaddr 等重定义。
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "JSPlugin.h"
#include "PermissionManager.h"
#include "JSWebViewBridge.h"
#include "GuiDllStore.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <wchar.h>
#include <cmath>
#include <set>
#include <map>
#include <mutex>
#include <list>
#include <shellapi.h>
#include <commctrl.h>
#include <objbase.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <winhttp.h>
#include <commdlg.h>
#include <richedit.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comdlg32.lib")

extern "C" {
#include "quickjs.h"
}

namespace fs = std::filesystem;
using namespace Gdiplus;

// ============================================================
// 工具
// ============================================================
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, NULL, NULL);
    return s;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

// 从 JSON 里提取字符串字段（不做完整 JSON 解析）
static std::wstring JsonStr(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return L"";
    p += pat.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':' || json[p] == '\t')) p++;
    if (p >= json.size() || json[p] != '"') return L"";
    p++;
    size_t e = p;
    while (e < json.size() && json[e] != '"') {
        if (json[e] == '\\') e++;
        e++;
    }
    return Utf8ToWide(json.substr(p, e - p));
}

// ============================================================
// v9.2: 流式请求状态 & base64 工具
// ============================================================
constexpr UINT WM_YXGUI_HTTP_STREAM_LOCAL = WM_APP + 900;

struct HttpStreamChunkMsg {
    uint32_t seq = 0;
    std::string chunk;
    bool isDone = false;
    int status = -1;
    std::string error;
};

struct StreamState {
    JSPlugin*   plugin      = nullptr;
    JSContext*  ctx         = nullptr;
    uint64_t    onChunkBits[2] = { 0, 0 };
    uint64_t    resolveBits[2] = { 0, 0 };
};

static std::map<uint32_t, StreamState> g_streamStates;
static std::mutex g_streamStatesMutex;

static void ClearStreamStatesForPlugin(JSPlugin* plugin) {
    std::vector<uint32_t> victims;
    {
        std::lock_guard<std::mutex> lock(g_streamStatesMutex);
        for (auto& kv : g_streamStates) {
            if (kv.second.plugin == plugin) victims.push_back(kv.first);
        }
    }
    for (uint32_t seq : victims) {
        StreamState st;
        {
            std::lock_guard<std::mutex> lock(g_streamStatesMutex);
            auto it = g_streamStates.find(seq);
            if (it == g_streamStates.end()) continue;
            st = it->second;
            g_streamStates.erase(it);
        }
        if (st.ctx) {
            JSValue onChunk, resolve;
            memcpy(&onChunk, st.onChunkBits, sizeof(JSValue));
            memcpy(&resolve, st.resolveBits, sizeof(JSValue));
            JS_FreeValue(st.ctx, onChunk);
            JS_FreeValue(st.ctx, resolve);
        }
    }
}

static std::string Base64Encode(const char* data, size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < len) {
        uint32_t v = ((uint8_t)data[i] << 16) |
                     ((uint8_t)data[i + 1] << 8) |
                     (uint8_t)data[i + 2];
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        out.push_back(tbl[(v >> 6) & 0x3F]);
        out.push_back(tbl[v & 0x3F]);
        i += 3;
    }
    if (i < len) {
        uint32_t v = (uint8_t)data[i] << 16;
        if (i + 1 < len) v |= (uint8_t)data[i + 1] << 8;
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? tbl[(v >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

// ============================================================
// 生命周期
// ============================================================
JSPlugin::JSPlugin() {}
JSPlugin::~JSPlugin() { Unload(); }

// 从内存 JSON 提取 manifest 字段（失败返回 false）
bool JSPlugin::ParseManifestJson(const std::string& json) {
    m_id = JsonStr(json, "id");
    m_name = JsonStr(json, "name");
    m_version = JsonStr(json, "version");
    m_author = JsonStr(json, "author");
    m_description = JsonStr(json, "description");
    m_iconEmoji = JsonStr(json, "icon");
    m_entryFile = JsonStr(json, "entry");
    if (m_id.empty()) return false;
    if (m_name.empty()) m_name = m_id;
    if (m_iconEmoji.empty()) m_iconEmoji = L"🔌";
    if (m_entryFile.empty()) m_entryFile = L"main.js";
    return true;
}

// 读取 plugin.json
bool JSPlugin::ReadManifest(const std::wstring& folderPath) {
    std::wstring manifestPath = folderPath + L"\\plugin.json";
    std::ifstream f(manifestPath, std::ios::binary);
    if (!f.is_open()) return false;
    std::string json((std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    f.close();

    if (!ParseManifestJson(json)) {
        // id 为空时从目录名派生
        std::wstring s = folderPath;
        size_t p = s.find_last_of(L"\\/");
        m_id = (p != std::wstring::npos) ? s.substr(p + 1) : s;
        if (m_name.empty()) m_name = m_id;
        if (m_iconEmoji.empty()) m_iconEmoji = L"🔌";
        if (m_entryFile.empty()) m_entryFile = L"main.js";
        if (!m_id.empty()) return true;
        return false;
    }
    return true;
}

// 创建 QuickJS runtime/context、注入 host/win32/light 并注册 API（Load/LoadBuiltin 共用）
bool JSPlugin::InitRuntimeAndApis() {
    // 创建 JSRuntime + JSContext
    m_rt = JS_NewRuntime();
    if (!m_rt) return false;

    // 内存限制：64MB
    JS_SetMemoryLimit(m_rt, 64 * 1024 * 1024);
    // 栈限制：1MB
    JS_SetMaxStackSize(m_rt, 1024 * 1024);

    m_ctx = JS_NewContext(m_rt);
    if (!m_ctx) {
        JS_FreeRuntime(m_rt);
        m_rt = nullptr;
        return false;
    }

    // 注入全局对象：host / win32 / light
    // 这三个绑定分别在 JSPlugin 的 HostApi / Win32Api / LightApi 部分注册，
    // 这里先创建空对象
    {
        JSValue global = JS_GetGlobalObject(m_ctx);
        JSValue host = JS_NewObject(m_ctx);
        JSValue win32 = JS_NewObject(m_ctx);
        JSValue light = JS_NewObject(m_ctx);
        JS_SetPropertyStr(m_ctx, global, "host", host);
        JS_SetPropertyStr(m_ctx, global, "win32", win32);
        JS_SetPropertyStr(m_ctx, global, "light", light);
        JS_FreeValue(m_ctx, global);
        // 注意：SetPropertyStr 会接管 host/win32/light 的所有权，不需要手动 Free
    }

    // 注册 host.* / win32.* / light.* 实际函数
    // 注意：先加载扩展 DLL（扩展可注册新 API / 覆盖内置 API），再绑定 JS 对象
    ScanAndLoadExtensions();

    // 为扩展注册的自定义命名空间（如 mc）创建全局对象
    {
        JSValue global = JS_GetGlobalObject(m_ctx);
        for (const auto& ns : GetExtNamespaces()) {
            JSValue obj = JS_NewObject(m_ctx);
            JS_SetPropertyStr(m_ctx, global, ns.c_str(), obj);
            // SetPropertyStr 接管 obj 所有权，无需手动 Free
        }
        JS_FreeValue(m_ctx, global);
    }

    extern void RegisterHostApi(JSPlugin*, JSContext*);
    extern void RegisterWin32Api(JSPlugin*, JSContext*);
    extern void RegisterLightApi(JSPlugin*, JSContext*);
    extern void RegisterExtNamespaceApis(JSPlugin*, JSContext*);
    RegisterHostApi(this, m_ctx);
    RegisterWin32Api(this, m_ctx);
    RegisterLightApi(this, m_ctx);
    RegisterExtNamespaceApis(this, m_ctx);
    return true;
}

bool JSPlugin::Load(const std::wstring& folderPath,
    const std::wstring& zipPath) {
    if (m_loaded) Unload();

    m_folderPath = folderPath;
    m_zipSource = zipPath;

    if (!ReadManifest(folderPath)) {
        OutputDebugStringW(L"[JSPlugin] 读取 plugin.json 失败\n");
        return false;
    }

    if (!InitRuntimeAndApis()) return false;

    // 读取并执行入口脚本
    std::wstring entryPath = folderPath + L"\\" + m_entryFile;
    if (!EvalFile(entryPath)) {
        OutputDebugStringW(L"[JSPlugin] 执行入口脚本失败\n");
        JS_FreeContext(m_ctx);
        JS_FreeRuntime(m_rt);
        m_ctx = nullptr;
        m_rt = nullptr;
        return false;
    }

    m_loaded = true;
    return true;
}

// 原生插件元信息注入（ApiAiNative 等 C++ 实现，不经 manifest/JS）
void JSPlugin::SetMetaInfo(const std::wstring& id, const std::wstring& name,
    const std::wstring& version, const std::wstring& author,
    const std::wstring& description, const std::wstring& iconEmoji) {
    m_id = id;
    m_name = name.empty() ? id : name;
    m_version = version;
    m_author = author;
    m_description = description;
    m_iconEmoji = iconEmoji.empty() ? L"🔌" : iconEmoji;
    if (m_entryFile.empty()) m_entryFile = L"";
}



// 为子控件分配进程内唯一的控件 ID。控件 ID 需保持在 WM_COMMAND 常用的
// 16 位范围内；使用原子计数器可避免多个插件在不同线程创建控件时发生竞争。
int JSPlugin::AllocControlId() {
    static std::atomic<unsigned int> s_nextControlId{ 10000 };
    constexpr unsigned int kFirst = 10000;
    constexpr unsigned int kRange = 50000; // 最后一个 ID 为 59999
    const unsigned int n = s_nextControlId.fetch_add(1, std::memory_order_relaxed);
    return static_cast<int>(kFirst + (n % kRange));
}

// 返回 Cache\\gui 下可发现的插件显示名称。对内置插件（没有 folderPath）也从
// light.exe 所在目录定位 Cache\\gui；始终确保当前插件出现在列表中。
std::vector<std::wstring> JSPlugin::GetPluginList() const {
    std::vector<std::wstring> result;
    fs::path pluginRoot;

    if (!m_folderPath.empty()) {
        pluginRoot = fs::path(m_folderPath).parent_path();
    } else {
        wchar_t exePath[MAX_PATH] = {};
        const DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            pluginRoot = fs::path(exePath).parent_path() / L"Cache" / L"gui";
        }
    }

    std::error_code ec;
    if (!pluginRoot.empty() && fs::is_directory(pluginRoot, ec) && !ec) {
        fs::directory_iterator it(pluginRoot, ec), end;
        while (!ec && it != end) {
            const fs::directory_entry entry = *it;
            it.increment(ec);
            if (ec || !entry.is_directory(ec) || ec) {
                ec.clear();
                continue;
            }

            const fs::path manifestPath = entry.path() / L"plugin.json";
            if (!fs::is_regular_file(manifestPath, ec) || ec) {
                ec.clear();
                continue;
            }

            std::ifstream f(manifestPath, std::ios::binary);
            if (!f.is_open()) continue;
            const std::string json((std::istreambuf_iterator<char>(f)),
                std::istreambuf_iterator<char>());

            std::wstring displayName = JsonStr(json, "name");
            if (displayName.empty()) displayName = JsonStr(json, "id");
            if (displayName.empty()) displayName = entry.path().filename().wstring();
            if (!displayName.empty() &&
                std::find(result.begin(), result.end(), displayName) == result.end()) {
                result.push_back(std::move(displayName));
            }
        }
    }

    if (!m_name.empty() &&
        std::find(result.begin(), result.end(), m_name) == result.end()) {
        result.push_back(m_name);
    }
    return result;
}

// 内置插件：从内存加载 manifest + 入口脚本（不依赖外部目录）
bool JSPlugin::LoadBuiltin(const std::string& manifestJson,
    const std::string& entryJs) {
    if (m_loaded) Unload();

    // 内置插件没有外部目录/zip
    m_folderPath.clear();
    m_zipSource.clear();

    if (!ParseManifestJson(manifestJson)) {
        OutputDebugStringW(L"[JSPlugin] 内置插件 manifest 无效\n");
        return false;
    }

    if (!InitRuntimeAndApis()) return false;

    // 执行内存入口脚本（文件名提示 <builtin:id>，便于异常定位）
    std::wstring hint = L"<builtin:" + m_id + L">";
    if (!EvalString(entryJs, hint)) {
        OutputDebugStringW(L"[JSPlugin] 执行内置入口脚本失败\n");
        JS_FreeContext(m_ctx);
        JS_FreeRuntime(m_rt);
        m_ctx = nullptr;
        m_rt = nullptr;
        return false;
    }

    m_loaded = true;
    return true;
}

void JSPlugin::Unload() {
    if (!m_loaded && !m_ctx && !m_rt) return;

    // 调用 onUnload
    if (m_ctx) {
        CallGlobalFunction("onUnload");
    }

    // 清理脚本创建的所有子控件
    for (auto& kv : m_controls) {
        if (kv.second && IsWindow(kv.second)) {
            DestroyWindow(kv.second);
        }
    }
    m_controls.clear();

    // 释放自绘样式与壁纸资源
    ClearControlStyles();
    ClearWallpaperCache();

    // v9.2: 清理本插件挂起的流式请求
    ClearStreamStatesForPlugin(this);

    if (m_ctx) { JS_FreeContext(m_ctx); m_ctx = nullptr; }
    if (m_rt) { JS_FreeRuntime(m_rt); m_rt = nullptr; }
    m_loaded = false;

    // 清理扩展绑定槽并卸载扩展 DLL（卸载必须在释放 QJS 之后，
    // 避免扩展回调持有已释放的上下文）
    m_apiSlots.clear();
    UnloadExtensions();
}

// ============================================================
// JSPlugin 委托方法（转发到 ExtensionManager）
// ============================================================
void JSPlugin::ScanAndLoadExtensions() {
    if (!m_extMgr) m_extMgr = new ExtensionManager(this);
    wchar_t selfPath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, selfPath, MAX_PATH)) {
        std::wstring p = selfPath;
        size_t pos = p.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            std::wstring dir = p.substr(0, pos);
            m_extMgr->ScanAndLoad(dir);
        }
    }
}

void JSPlugin::UnloadExtensions() {
    if (m_extMgr) {
        m_extMgr->UnloadAll();
    }
}

YXG_ExtensionApi* JSPlugin::GetExtensionApi() const {
    if (!m_extMgr) return nullptr;
    static YXG_ExtensionApi api{};
    const_cast<JSPlugin*>(this)->m_extMgr->BuildApiTable(&api);
    return &api;
}

int JSPlugin::RunHooks(YXG_HookType hookType, void* dataIn, void* dataOut) {
    if (m_extMgr) {
        int r = m_extMgr->RunHooks(hookType, m_id.c_str(), dataIn, dataOut);
        if (r) return r;
    }
    if (g_guiDllHost) {
        return g_guiDllHost->RunHooks(hookType, m_id.c_str(), dataIn, dataOut);
    }
    return 0;
}

bool JSPlugin::IsJSApiOverridden(const std::string& ns, const std::string& name) const {
    if (m_extMgr && m_extMgr->HasJSApi(ns, name)) return true;
    if (g_guiDllHost && g_guiDllHost->HasJSApi(ns, name)) return true;
    return false;
}

bool JSPlugin::CallExtensionJSApi(const std::string& ns, const std::string& name,
    int argc, YXG_JSValue* argv, YXG_JSValue* ret) {
    if (m_extMgr && m_extMgr->CallJSApi(ns, name, argc, argv, ret)) return true;
    if (g_guiDllHost && g_guiDllHost->CallJSApi(ns, name, argc, argv, ret)) return true;
    return false;
}

// ============================================================
// v3 控件样式 / 壁纸实现
// ============================================================
// v3 视觉工具：颜色 / 圆角 / 自绘控件
// ============================================================

// "#RRGGBB" / "#AARRGGBB" -> COLORREF；失败返回 false
static bool ParseColorHex(const std::string& s, COLORREF& out) {
    if (s.size() < 7 || s[0] != '#') return false;
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int r = hexVal(s[1]), g = hexVal(s[2]), b = hexVal(s[3]);
    if (r < 0 || g < 0 || b < 0) return false;
    int nr = r * 16 + hexVal(s[4]);
    int ng = g * 16 + hexVal(s[5]);
    int nb = b * 16 + hexVal(s[6]);
    // 7 位长度时第 4、5、6 个字符分别对应 r/g/b 的低位十六进制
    if (s.size() >= 8) { nr = r * 16 + g; ng = b * 16 + nr; /* 未使用 */ }
    // 上面的解析按 6 位 RGB 处理：#RRGGBB
    if (s.size() >= 8) {
        // 8 位：#RRGGBB（忽略 alpha 前 2 位的情况，统一按 #RRGGBB 处理）
    }
    nr = hexVal(s[1]) * 16 + hexVal(s[2]);
    ng = hexVal(s[3]) * 16 + hexVal(s[4]);
    nb = hexVal(s[5]) * 16 + hexVal(s[6]);
    if (nr < 0 || ng < 0 || nb < 0) return false;
    out = RGB(nr, ng, nb);
    return true;
}

static COLORREF LightenColor(COLORREF c, int delta) {
    int r = GetRValue(c) + delta, g = GetGValue(c) + delta, b = GetBValue(c) + delta;
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    return RGB(r, g, b);
}

static void AddRoundRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    r = d / 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

static bool IsImageFile(const std::wstring& name) {
    std::wstring n = name;
    for (auto& c : n) c = towlower(c);
    if (n.find(L".jpg") != std::wstring::npos || n.find(L".jpeg") != std::wstring::npos ||
        n.find(L".png") != std::wstring::npos || n.find(L".bmp") != std::wstring::npos ||
        n.find(L".gif") != std::wstring::npos) return true;
    return false;
}

static bool GetPngEncoderClsid(CLSID& outClsid) {
    UINT count = 0, size = 0;
    if (GetImageEncodersSize(&count, &size) != Ok || size == 0) return false;

    std::vector<BYTE> buffer(size);
    auto* encoders = reinterpret_cast<ImageCodecInfo*>(buffer.data());
    if (GetImageEncoders(count, size, encoders) != Ok) return false;

    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
            outClsid = encoders[i].Clsid;
            return true;
        }
    }
    return false;
}

static bool ExtractGifFrames(const std::wstring& gifPath,
    const std::wstring& frameDir, std::vector<std::wstring>& outFrames) {
    Bitmap gif(gifPath.c_str(), FALSE);
    if (gif.GetLastStatus() != Ok) return false;

    UINT dimCount = gif.GetFrameDimensionsCount();
    if (dimCount == 0) return false;

    std::vector<GUID> dims(dimCount);
    if (gif.GetFrameDimensionsList(dims.data(), dimCount) != Ok) return false;

    UINT frameCount = gif.GetFrameCount(&dims[0]);
    if (frameCount <= 1) return false;

    std::error_code ec;
    fs::create_directories(frameDir, ec);

    CLSID pngClsid;
    if (!GetPngEncoderClsid(pngClsid)) return false;

    std::wstring stem = fs::path(gifPath).stem().wstring();
    for (UINT i = 0; i < frameCount; ++i) {
        if (gif.SelectActiveFrame(&dims[0], (int)i) != Ok) continue;

        wchar_t number[32];
        swprintf_s(number, L"%06u", i);
        std::wstring framePath = frameDir + L"\\" + stem + L"_" + number + L".png";

        Bitmap frame(gif.GetWidth(), gif.GetHeight(), PixelFormat32bppARGB);
        Graphics g(&frame);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(&gif, 0, 0, (INT)gif.GetWidth(), (INT)gif.GetHeight());

        if (frame.Save(framePath.c_str(), &pngClsid, nullptr) == Ok) {
            outFrames.push_back(framePath);
        }
    }

    return !outFrames.empty();
}

// ============================================================
// v12: 实心玻璃按钮（现代化）
//  - 玻璃板：PathGradient 中心薄 / 边缘厚 + 加粗描边 + 顶部高光 + 底部内阴影
//  - 文字直印在玻璃板上（GDI+ DrawString，与玻璃板共用同一变换）
//  - hover：玻璃板和文字一起放大 1.05 并上移 3px（缓动动画）
//  - 按下：整体收缩到 0.95 + 涟漪扩散；松开 easeOutBack 回弹
//  - swatchColor != -1：内陷色板（外框内凹 + 中央实色色片，选中描蓝环 + ✓）
//  - tint != -1 / active：玻璃板叠加色调（primary 浅蓝 / danger 浅红）
// 背景经 AcquireParentBgSnapshot 缓存，重绘不再反复通知宿主全窗重绘（性能）。
// ============================================================
namespace {
    const UINT_PTR kGlassAnimTimerId = 0x4A41;

    struct GlassBtnAnim {
        bool  pressed = false;   // 鼠标按住中
        ULONGLONG t0 = 0;        // 动画起始时刻（GetTickCount64）
        float rippleT = 1.f;     // 涟漪进度 0..1（1=结束）
        float scale = 1.f;       // 按下收缩（1 → 0.95）
        float hoverT = 0.f;      // hover 动画进度 0..1（放大 + 上移）
        bool  hovered = false;   // 鼠标是否悬停
    };
    std::map<HWND, GlassBtnAnim> g_glassAnims;
    // 控件矩形比可见玻璃板大的内边距（避免 hover 放大被控件矩形裁剪）
    std::map<HWND, int> g_glassPad;

    static float EaseOutCubic(float t) {
        float u = 1.f - t;
        return 1.f - u * u * u;
    }
    static float EaseOutBack(float t) {
        float c1 = 1.70158f, c3 = c1 + 1.f;
        float u = t - 1.f;
        return 1.f + c3 * u * u * u + c1 * u * u;
    }
    static float ApproachF(float cur, float target, float step) {
        if (cur < target) { cur += step; return cur > target ? target : cur; }
        cur -= step; return cur < target ? target : cur;
    }
    static int GlassPadOf(HWND h) {
        auto it = g_glassPad.find(h);
        return it == g_glassPad.end() ? 0 : it->second;
    }
    // GDI+ 字体缓存（按钮文字按板高度取号，避免每次绘制新建字体）
    static Gdiplus::Font* GlassFont(int px) {
        static std::map<int, Gdiplus::Font*> s_fonts;
        auto it = s_fonts.find(px);
        if (it != s_fonts.end()) return it->second;
        Gdiplus::Font* f = new Gdiplus::Font(L"Microsoft YaHei", (Gdiplus::REAL)px,
            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        s_fonts[px] = f;
        return f;
    }
    // 玻璃按钮统一的"悬停放大 + 按下收缩"变换（玻璃板与文字共用）
    static void GlassTransform(Gdiplus::Graphics& g, float cx, float cy,
                               float hs, float pressK) {
        float s = 1.f + 0.05f * hs - 0.05f * pressK;
        float dy = -3.f * hs + 2.f * pressK;
        g.TranslateTransform(cx, cy);
        g.ScaleTransform(s, s);
        g.TranslateTransform(-cx, -cy);
        g.TranslateTransform(0.f, dy);
    }
    // 动画是否还在进行（决定计时器去留）
    static bool GlassAnimBusy(const GlassBtnAnim& a) {
        if (a.pressed) return true;
        if (a.rippleT < 1.f) return true;
        if (a.scale < 0.999f) return true;
        if (a.hovered ? (a.hoverT < 0.999f) : (a.hoverT > 0.001f)) return true;
        return false;
    }
    // 计时器心跳：推进按下回弹 / 涟漪 / hover 缓动
    static void GlassAnimTick(HWND hWnd) {
        auto it = g_glassAnims.find(hWnd);
        if (it == g_glassAnims.end()) return;
        GlassBtnAnim& a = it->second;
        ULONGLONG now = GetTickCount64();
        ULONGLONG dt = now - a.t0;
        if (a.pressed) {
            // 按下：0~90ms 收缩到 0.95；涟漪 0~300ms 扩散
            float k = dt < 90 ? (float)dt / 90.f : 1.f;
            a.scale = 1.f - 0.05f * k;
            float rt = (float)dt / 300.0f;
            a.rippleT = rt > 1.f ? 1.f : rt;
        } else if (a.scale < 0.999f) {
            // 松开：0~220ms easeOutBack 回弹到 1.0
            float tt = (float)dt / 220.0f;
            if (tt >= 1.f) {
                a.scale = 1.f;
            } else {
                a.scale = 0.95f + 0.05f * EaseOutBack(tt);
            }
            float rt = (float)dt / 260.0f;
            a.rippleT = rt > 1.f ? 1.f : rt;
        }
        // hover 缓动（帧步进，约 130ms 到位）
        a.hoverT = ApproachF(a.hoverT, a.hovered ? 1.f : 0.f, 0.12f);
        if (GlassAnimBusy(a)) {
            InvalidateRect(hWnd, NULL, TRUE);
        } else {
            KillTimer(hWnd, kGlassAnimTimerId);
        }
    }
    // 从父容器背景快照取底图（玻璃透出效果）；失败返回 nullptr
    static HBITMAP GlassBgSnapshot(JSPlugin* self, HWND hWnd, HDC hdc,
                                    int w, int h, int& outPw, int& outPh) {
        outPw = 0; outPh = 0;
        HWND hParent = GetParent(hWnd);
        RECT prc = { 0, 0, 0, 0 };
        if (hParent && IsWindow(hParent)) GetClientRect(hParent, &prc);
        int pw = prc.right - prc.left, ph = prc.bottom - prc.top;
        if (!self || !hParent || pw <= 4 || ph <= 4) return nullptr;
        HBITMAP bgBmp = self->AcquireParentBgSnapshot(hParent, pw, ph, self->BgVersion());
        if (!bgBmp) return nullptr;
        HDC memDC = CreateCompatibleDC(hdc);
        HGDIOBJ oldBmp = SelectObject(memDC, bgBmp);
        POINT pt = { 0, 0 };
        ClientToScreen(hWnd, &pt);
        ScreenToClient(hParent, &pt);
        BitBlt(hdc, 0, 0, w, h, memDC, pt.x, pt.y, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
        outPw = pw; outPh = ph;
        return bgBmp;
    }
    // 内陷色板（调色板）：外框内凹 + 中央实色色片；active 时描蓝环 + ✓
    static void DrawSwatchWell(Gdiplus::Graphics& g, const Gdiplus::RectF& rf,
                               COLORREF swCol, bool active, bool hover, bool pressed) {
        float rad = rf.Height * 0.5f - 1.5f;
        if (rad > 10.f) rad = 10.f;
        if (rad < 4.f) rad = 4.f;
        Gdiplus::GraphicsPath well;
        AddRoundRect(well, rf.X, rf.Y, rf.Width, rf.Height, rad);
        // 插座底色（比页面背景略深，形成凹陷）
        Gdiplus::SolidBrush socket(Gdiplus::Color(255,
            hover ? 234 : 220, hover ? 239 : 228, hover ? 247 : 237));
        g.FillPath(&socket, &well);
        // 顶部内阴影 + 底部受光（内陷的关键）
        g.SetClip(&well);
        {
            Gdiplus::LinearGradientBrush ts(
                Gdiplus::PointF(0.f, rf.Y), Gdiplus::PointF(0.f, rf.Y + rf.Height * 0.5f),
                Gdiplus::Color(pressed ? 96 : 64, 15, 23, 42),
                Gdiplus::Color(0, 15, 23, 42));
            g.FillRectangle(&ts, rf.X, rf.Y, rf.Width, rf.Height * 0.5f);
            Gdiplus::LinearGradientBrush bl(
                Gdiplus::PointF(0.f, rf.Y + rf.Height),
                Gdiplus::PointF(0.f, rf.Y + rf.Height * 0.55f),
                Gdiplus::Color(150, 255, 255, 255), Gdiplus::Color(0, 255, 255, 255));
            g.FillRectangle(&bl, rf.X, rf.Y + rf.Height * 0.55f,
                rf.Width, rf.Height * 0.45f);
        }
        g.ResetClip();
        Gdiplus::Pen sp(Gdiplus::Color(255, 203, 213, 225), 1.4f);
        g.DrawPath(&sp, &well);
        // 色片（内缩 4px，实色）
        float inset = 4.f;
        float crad = rad > 4.f ? rad - 2.5f : 2.5f;
        Gdiplus::GraphicsPath chip;
        AddRoundRect(chip, rf.X + inset, rf.Y + inset,
            rf.Width - inset * 2, rf.Height - inset * 2, crad);
        Gdiplus::SolidBrush chipBr(Gdiplus::Color(255,
            GetRValue(swCol), GetGValue(swCol), GetBValue(swCol)));
        g.FillPath(&chipBr, &chip);
        g.SetClip(&chip);
        {
            Gdiplus::LinearGradientBrush ch(
                Gdiplus::PointF(0.f, rf.Y + inset),
                Gdiplus::PointF(0.f, rf.Y + inset + (rf.Height - inset * 2) * 0.55f),
                Gdiplus::Color(70, 255, 255, 255), Gdiplus::Color(0, 255, 255, 255));
            g.FillRectangle(&ch, rf.X + inset, rf.Y + inset,
                rf.Width - inset * 2, (rf.Height - inset * 2) * 0.55f);
        }
        g.ResetClip();
        // 选中：蓝色圆环 + ✓（按色片亮度取对比色）
        if (active) {
            Gdiplus::GraphicsPath ring;
            AddRoundRect(ring, rf.X - 2.6f, rf.Y - 2.6f,
                rf.Width + 5.2f, rf.Height + 5.2f, rad + 2.6f);
            Gdiplus::Pen rp(Gdiplus::Color(255, 37, 99, 235), 2.6f);
            g.DrawPath(&rp, &ring);
            int lum = (GetRValue(swCol) * 30 + GetGValue(swCol) * 59 +
                       GetBValue(swCol) * 11) / 100;
            Gdiplus::SolidBrush chk(lum > 150
                ? Gdiplus::Color(255, 15, 23, 42)
                : Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::StringFormat cfmt;
            cfmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            cfmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(L"\u2713", -1, GlassFont(14), rf, &cfmt, &chk);
        }
    }
}

void JSPlugin::SetGlassPad(HWND h, int pad) {
    if (!h) return;
    if (pad <= 0) g_glassPad.erase(h);
    else g_glassPad[h] = pad;
}

LRESULT CALLBACK FlatButtonSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    JSPlugin* self = (JSPlugin*)dwRefData;
    switch (msg) {
    case WM_ERASEBKGND:
        // 阻止系统用默认灰色/黑色擦除背景；WM_PAINT 会自己用父容器快照填充
        return 1;
    case WM_NCPAINT:
        // 阻止系统画直角黑边框；WM_PAINT 会自己画圆角边框
        return 0;
    case WM_PAINT: {
        // 扩展 RENDER_PRE Hook：dataIn = HWND, dataOut = 控件类型串；拦截则扩展自行绘制
        if (self && self->RunHooks(YXG_HOOK_RENDER_PRE, (void*)hWnd, (void*)"button")) {
            ValidateRect(hWnd, NULL);   // 清除无效区域，避免重绘风暴
            return 0;
        }
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w < 1 || h < 1) { EndPaint(hWnd, &ps); return 0; }

        const CtrlColor* cc = self ? self->GetCtrlColor(hWnd) : nullptr;
        bool enabled = IsWindowEnabled(hWnd) != FALSE;
        COLORREF bd = (cc && cc->border != -1) ? (COLORREF)cc->border : RGB(37, 99, 235);
        COLORREF fg = (cc && cc->fg != -1) ? (COLORREF)cc->fg : RGB(30, 58, 138);
        bool active = cc && cc->active == 1;
        bool hasTint = cc && cc->tint != -1;
        COLORREF tintCol = hasTint ? (COLORREF)cc->tint : RGB(219, 234, 254);
        bool isSwatch = cc && cc->swatchColor != -1;
        COLORREF swCol = isSwatch ? (COLORREF)cc->swatchColor : RGB(255, 255, 255);
        bool hover = enabled && self && self->IsCtrlHover(hWnd);
        auto ait = g_glassAnims.find(hWnd);
        bool pressed = (ait != g_glassAnims.end()) && ait->second.pressed;
        float pressScale = (ait != g_glassAnims.end()) ? ait->second.scale : 1.f;
        float rippleT = (ait != g_glassAnims.end()) ? ait->second.rippleT : 1.f;
        float hs = (ait != g_glassAnims.end()) ? ait->second.hoverT : (hover ? 1.f : 0.f);
        float pressK = 1.f - pressScale;          // 0..0.05
        if (!enabled) fg = RGB(148, 163, 184);

        // ---------- 1) 底图：父容器最新背景（快照缓存） ----------
        int snapW = 0, snapH = 0;
        HBITMAP bgBmp = GlassBgSnapshot(self, hWnd, hdc, w, h, snapW, snapH);
        // 兜底：万一取背景失败，用默认浅色
        if (!bgBmp) {
            Gdiplus::SolidBrush fb(Gdiplus::Color(255, 246, 249, 253));
            Gdiplus::Graphics gh(hdc);
            gh.FillRectangle(&fb, 0.f, 0.f, (Gdiplus::REAL)w, (Gdiplus::REAL)h);
        }

        // ---------- 2) 可见玻璃板几何（控件矩形内缩 pad） ----------
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        int pad = GlassPadOf(hWnd);
        float bx = (float)pad, by = (float)pad;
        float bw = (float)(w - pad * 2), bh = (float)(h - pad * 2);
        if (bw < 6.f) { bx = 0.f; bw = (float)w; }
        if (bh < 6.f) { by = 0.f; bh = (float)h; }
        float cx = bx + bw * 0.5f, cy = by + bh * 0.5f;
        float radius = (bw < bh ? bw : bh) * 0.28f;
        if (radius > 12.f) radius = 12.f;
        if (radius < 4.f) radius = 4.f;
        Gdiplus::RectF rf(bx + 1.2f, by + 1.2f, bw - 2.4f, bh - 2.4f);

        Gdiplus::GraphicsState gstate = g.Save();
        GlassTransform(g, cx, cy, hs, pressK);

        if (isSwatch) {
            // ===== 内陷色板 =====
            DrawSwatchWell(g, rf, swCol, active, hover, pressed);
        } else {
            // ===== 玻璃板：中心薄 / 边缘厚 =====
            Gdiplus::GraphicsPath path;
            AddRoundRect(path, rf.X, rf.Y, rf.Width, rf.Height, radius);

            int cA = 68, sA = 214;                       // 中心薄、边缘厚
            if (!enabled)      { cA = 40; sA = 150; }
            else if (pressed)  { cA = 110; sA = 238; }
            else if (hover)    { cA = 96;  sA = 226; }
            Gdiplus::PathGradientBrush pg(&path);
            pg.SetCenterPoint(Gdiplus::PointF(cx, cy));
            pg.SetCenterColor(Gdiplus::Color((BYTE)cA, 255, 255, 255));
            Gdiplus::Color surround((BYTE)sA, 255, 255, 255);
            INT scount = 1;
            pg.SetSurroundColors(&surround, &scount);
            g.FillPath(&pg, &path);

            // 色调叠加（primary / danger / active）
            if (hasTint || active) {
                int tA = active ? 155 : (hover ? 118 : 92);
                Gdiplus::SolidBrush tb(Gdiplus::Color((BYTE)tA,
                    GetRValue(tintCol), GetGValue(tintCol), GetBValue(tintCol)));
                g.FillPath(&tb, &path);
            }

            // 顶部高光 + 底部内阴影（裁剪在板内）
            g.SetClip(&path);
            {
                Gdiplus::LinearGradientBrush hl(
                    Gdiplus::PointF(0.f, rf.Y),
                    Gdiplus::PointF(0.f, rf.Y + rf.Height * 0.52f),
                    Gdiplus::Color(hover ? 110 : 84, 255, 255, 255),
                    Gdiplus::Color(0, 255, 255, 255));
                g.FillRectangle(&hl, rf.X, rf.Y, rf.Width, rf.Height * 0.52f);
                Gdiplus::LinearGradientBrush sh(
                    Gdiplus::PointF(0.f, rf.Y + rf.Height),
                    Gdiplus::PointF(0.f, rf.Y + rf.Height * 0.5f),
                    Gdiplus::Color(pressed ? 42 : 26, 0, 0, 0),
                    Gdiplus::Color(0, 0, 0, 0));
                g.FillRectangle(&sh, rf.X, rf.Y + rf.Height * 0.5f,
                    rf.Width, rf.Height * 0.5f);
            }
            g.ResetClip();

            // 边缘加粗描边
            int eA = active ? 255 : (pressed ? 255 : (hover ? 246 : 230));
            if (!enabled) eA = 140;
            float eW = 2.0f + 0.5f * hs + 0.5f * pressK;
            Gdiplus::Pen pen(Gdiplus::Color((BYTE)eA,
                GetRValue(bd), GetGValue(bd), GetBValue(bd)), eW);
            g.DrawPath(&pen, &path);
        }
        g.Restore(gstate);

        // ---------- 3) 涟漪（点击反馈，独立于缩放变换） ----------
        if (rippleT < 1.f && !isSwatch) {
            float e = EaseOutCubic(rippleT);
            float maxR = (bw < bh ? bw : bh) * 0.72f;
            float rCur = maxR * e;
            int ringA = (int)(120.f * (1.f - e));
            if (ringA > 0) {
                Gdiplus::Pen ring(Gdiplus::Color((BYTE)ringA, 255, 255, 255), 2.0f);
                g.DrawEllipse(&ring, cx - rCur, cy - rCur, rCur * 2, rCur * 2);
            }
        }

        // ---------- 4) 悬浮投影（常态即有，hover 更明显） ----------
        {
            int shadowA = hover ? 46 : 28;
            if (!enabled) shadowA = 14;
            Gdiplus::SolidBrush shadowBr(Gdiplus::Color((BYTE)shadowA, 0, 0, 0));
            g.FillRectangle(&shadowBr, bx + 6.f, by + bh + 1.f, bw - 12.f, 3.f);
        }

        // ---------- 5) 文字：直印在玻璃板上，与板一起放大上移 ----------
        wchar_t txt[512];
        GetWindowTextW(hWnd, txt, 512);
        int fs = (int)(bh * 0.40f);
        if (fs < 11) fs = 11;
        if (fs > 16) fs = 16;
        Gdiplus::SolidBrush textBr(Gdiplus::Color(255,
            GetRValue(fg), GetGValue(fg), GetBValue(fg)));
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        Gdiplus::RectF tr(bx + 4.f, by, bw - 8.f, bh);
        Gdiplus::GraphicsState tstate = g.Save();
        GlassTransform(g, cx, cy, hs, pressK);
        g.DrawString(txt, -1, GlassFont(fs), tr, &fmt, &textBr);
        g.Restore(tstate);

        EndPaint(hWnd, &ps);
        if (self) self->RunHooks(YXG_HOOK_RENDER_POST, (void*)hWnd, (void*)"button");
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!IsWindowEnabled(hWnd)) return 1;
        if (self && !self->IsCtrlHover(hWnd)) {
            self->SetCtrlHover(hWnd, true);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
        }
        {
            GlassBtnAnim& a = g_glassAnims[hWnd];
            if (!a.hovered || a.hoverT < 0.999f) {
                a.hovered = true;
                SetTimer(hWnd, kGlassAnimTimerId, 16, nullptr);
                InvalidateRect(hWnd, NULL, TRUE);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        if (self && self->IsCtrlHover(hWnd)) self->SetCtrlHover(hWnd, false);
        {
            auto it = g_glassAnims.find(hWnd);
            if (it != g_glassAnims.end()) {
                it->second.hovered = false;
                SetTimer(hWnd, kGlassAnimTimerId, 16, nullptr);
                InvalidateRect(hWnd, NULL, TRUE);
            }
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!IsWindowEnabled(hWnd)) break;
        GlassBtnAnim& a = g_glassAnims[hWnd];
        a.pressed = true;
        a.t0 = GetTickCount64();
        a.scale = 1.f;
        a.rippleT = 0.f;
        SetTimer(hWnd, kGlassAnimTimerId, 16, nullptr);
        InvalidateRect(hWnd, NULL, TRUE);
        break;   // 交默认按钮处理（触发 BN_CLICKED 等）
    }
    case WM_LBUTTONUP: {
        auto it = g_glassAnims.find(hWnd);
        if (it != g_glassAnims.end() && it->second.pressed) {
            it->second.pressed = false;
            it->second.t0 = GetTickCount64();
            SetTimer(hWnd, kGlassAnimTimerId, 16, nullptr);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;   // 交默认按钮处理（BN_CLICKED）
    }
    case WM_CAPTURECHANGED: {
        // 鼠标捕获被转移（移出窗口释放等）：按松开处理
        auto it = g_glassAnims.find(hWnd);
        if (it != g_glassAnims.end() && it->second.pressed) {
            it->second.pressed = false;
            it->second.t0 = GetTickCount64();
            SetTimer(hWnd, kGlassAnimTimerId, 16, nullptr);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }
    case WM_TIMER: {
        if (wParam == kGlassAnimTimerId) {
            GlassAnimTick(hWnd);
            return 0;
        }
        break;
    }
    case WM_NCDESTROY:
        g_glassAnims.erase(hWnd);
        g_glassPad.erase(hWnd);
        break;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// 面板圆角卡片自绘过程（panel 应用颜色后子类化）
LRESULT CALLBACK PanelCardSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    JSPlugin* self = (JSPlugin*)dwRefData;
    switch (msg) {
    case WM_PAINT: {
        // 扩展 RENDER_PRE Hook：dataIn = HWND, dataOut = 控件类型串；拦截则扩展自行绘制
        if (self && self->RunHooks(YXG_HOOK_RENDER_PRE, (void*)hWnd, (void*)"panel")) {
            ValidateRect(hWnd, NULL);   // 清除无效区域，避免重绘风暴
            return 0;
        }
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        const CtrlColor* cc = self ? self->GetCtrlColor(hWnd) : nullptr;
        COLORREF bg = (cc && cc->bg != -1) ? (COLORREF)cc->bg : RGB(255, 255, 255);
        COLORREF bd = (cc && cc->border != -1) ? (COLORREF)cc->border : RGB(229, 234, 243);
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        float w = (float)(rc.right - rc.left), h = (float)(rc.bottom - rc.top);
        float radius = 10.0f;
        if (w < 60) radius = w / 6;
        if (h < 60) radius = h / 6;
        GraphicsPath path;
        AddRoundRect(path, 1, 1, w - 2, h - 2, radius);
        SolidBrush bb(bg);
        g.FillPath(&bb, &path);
        Pen pen(bd, 1.0f);
        g.DrawPath(&pen, &path);
        EndPaint(hWnd, &ps);
        if (self) self->RunHooks(YXG_HOOK_RENDER_POST, (void*)hWnd, (void*)"panel");
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ============================================================
// v9: 新控件子类（textarea / roundbutton / imagebox）
// ============================================================

// 图片控件路径表（供 imagebox 子类读取）
static std::map<HWND, std::wstring> g_imagePaths;
static std::mutex g_imagePathsMutex;

// v11: 给 imagebox 设置图片路径（全局 map，跨插件共享）
void JSPlugin::AddImagePath(HWND h, const std::wstring& path) {
    if (!h) return;
    std::lock_guard<std::mutex> lock(g_imagePathsMutex);
    if (path.empty()) g_imagePaths.erase(h);
    else g_imagePaths[h] = path;
}

// v9.1: 图片 LRU 缓存（避免每次 WM_PAINT 都从磁盘读）
namespace {
    struct ImageCacheEntry {
        Gdiplus::Bitmap* bmp = nullptr;
        std::list<std::wstring>::iterator lruIt;
    };
    const size_t IMAGE_CACHE_MAX = 48;
    std::map<std::wstring, ImageCacheEntry> g_imgCache;
    std::list<std::wstring> g_imgLRU;       // 尾部 = 最近使用
    std::mutex g_imgCacheMutex;

    Gdiplus::Bitmap* AcquireCachedImage(const std::wstring& path) {
        if (path.empty()) return nullptr;
        std::lock_guard<std::mutex> lock(g_imgCacheMutex);

        auto it = g_imgCache.find(path);
        if (it != g_imgCache.end()) {
            g_imgLRU.erase(it->second.lruIt);
            g_imgLRU.push_back(path);
            it->second.lruIt = std::prev(g_imgLRU.end());
            return it->second.bmp;
        }

        Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(path.c_str());
        if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
            delete bmp;
            return nullptr;
        }

        while (g_imgCache.size() >= IMAGE_CACHE_MAX && !g_imgLRU.empty()) {
            const std::wstring victim = g_imgLRU.front();
            g_imgLRU.pop_front();
            auto vit = g_imgCache.find(victim);
            if (vit != g_imgCache.end()) {
                delete vit->second.bmp;
                g_imgCache.erase(vit);
            }
        }

        ImageCacheEntry entry;
        entry.bmp = bmp;
        g_imgLRU.push_back(path);
        entry.lruIt = std::prev(g_imgLRU.end());
        g_imgCache[path] = entry;
        return bmp;
    }

    void ClearImageCache() {
        std::lock_guard<std::mutex> lock(g_imgCacheMutex);
        for (auto& kv : g_imgCache) delete kv.second.bmp;
        g_imgCache.clear();
        g_imgLRU.clear();
    }
}

// v9: textarea 子类 —— Enter 发送（回发 WM_COMMAND, code=0x1000），
// Shift+Enter 保留换行默认行为。
LRESULT CALLBACK TextareaSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
    if (msg == WM_CHAR && wParam == 0x0D) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!shift) {
            HWND parent = GetParent(hWnd);
            int ctrlId = GetDlgCtrlID(hWnd);
            SendMessageW(parent, WM_COMMAND,
                MAKEWPARAM(ctrlId, 0x1000), (LPARAM)hWnd);
            return 0;
        }
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// v12: roundbutton 子类 —— 圆形玻璃板（中心薄 / 边缘厚）+ 齿轮图标
// 与 FlatButtonSubclassProc 同一套玻璃规范：背景快照透出 + PathGradient 中心薄边缘厚
// + 顶部高光弧 + 加粗圆环描边 + hover 玻璃板与内容一起放大上移 + 按下/松开动画。
// 文本传 L"gear" 时绘制齿轮图标（设置入口），否则绘制普通文字。
LRESULT CALLBACK RoundButtonSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    JSPlugin* self = (JSPlugin*)dwRefData;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_NCPAINT:
        return 0;
    case WM_PAINT: {
        if (self && self->RunHooks(YXG_HOOK_RENDER_PRE, (void*)hWnd, (void*)"roundbutton")) {
            ValidateRect(hWnd, NULL);
            return 0;
        }
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w < 1 || h < 1) { EndPaint(hWnd, &ps); return 0; }

        const CtrlColor* cc = self ? self->GetCtrlColor(hWnd) : nullptr;
        bool enabled = IsWindowEnabled(hWnd) != FALSE;
        COLORREF bd = (cc && cc->border != -1) ? (COLORREF)cc->border : RGB(37, 99, 235);
        COLORREF fg = (cc && cc->fg != -1) ? (COLORREF)cc->fg : RGB(30, 58, 138);
        bool active = cc && cc->active == 1;
        bool hasTint = cc && cc->tint != -1;
        COLORREF tintCol = hasTint ? (COLORREF)cc->tint : RGB(219, 234, 254);
        bool hover = enabled && self && self->IsCtrlHover(hWnd);
        auto ait = g_glassAnims.find(hWnd);
        bool pressed = (ait != g_glassAnims.end()) && ait->second.pressed;
        float pressScale = (ait != g_glassAnims.end()) ? ait->second.scale : 1.f;
        float rippleT = (ait != g_glassAnims.end()) ? ait->second.rippleT : 1.f;
        float hs = (ait != g_glassAnims.end()) ? ait->second.hoverT : (hover ? 1.f : 0.f);
        float pressK = 1.f - pressScale;
        if (!enabled) fg = RGB(148, 163, 184);

        // ---------- 1) 底图：父容器背景快照 ----------
        int snapW = 0, snapH = 0;
        HBITMAP bgBmp = GlassBgSnapshot(self, hWnd, hdc, w, h, snapW, snapH);
        if (!bgBmp) {
            Gdiplus::SolidBrush fb(Gdiplus::Color(255, 246, 249, 253));
            Gdiplus::Graphics gh(hdc);
            gh.FillRectangle(&fb, 0.f, 0.f, (Gdiplus::REAL)w, (Gdiplus::REAL)h);
        }

        // ---------- 2) 圆形玻璃板 ----------
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        int pad = GlassPadOf(hWnd);
        float bx = (float)pad, by = (float)pad;
        float bw = (float)(w - pad * 2), bh = (float)(h - pad * 2);
        if (bw < 6.f) { bx = 0.f; bw = (float)w; }
        if (bh < 6.f) { by = 0.f; bh = (float)h; }
        float d = bw < bh ? bw : bh;
        float cx = bx + bw * 0.5f, cy = by + bh * 0.5f;
        float R = d * 0.5f - 1.8f;

        Gdiplus::GraphicsState gstate = g.Save();
        GlassTransform(g, cx, cy, hs, pressK);

        Gdiplus::GraphicsPath path;
        path.AddEllipse(cx - R, cy - R, R * 2, R * 2);

        int cA = 66, sA = 216;
        if (!enabled)     { cA = 40; sA = 150; }
        else if (pressed) { cA = 110; sA = 240; }
        else if (hover)   { cA = 96;  sA = 228; }
        Gdiplus::PathGradientBrush pg(&path);
        pg.SetCenterPoint(Gdiplus::PointF(cx, cy));
        pg.SetCenterColor(Gdiplus::Color((BYTE)cA, 255, 255, 255));
        Gdiplus::Color surround((BYTE)sA, 255, 255, 255);
        INT scount = 1;
        pg.SetSurroundColors(&surround, &scount);
        g.FillPath(&pg, &path);

        if (hasTint || active) {
            int tA = active ? 155 : (hover ? 118 : 92);
            Gdiplus::SolidBrush tb(Gdiplus::Color((BYTE)tA,
                GetRValue(tintCol), GetGValue(tintCol), GetBValue(tintCol)));
            g.FillPath(&tb, &path);
        }

        // 顶部高光弧（玻璃反光）
        Gdiplus::Pen hlPen(Gdiplus::Color(hover ? 120 : 95, 255, 255, 255),
            std::max(1.6f, R * 0.09f));
        Gdiplus::RectF hr(cx - R + 1.5f, cy - R + 1.5f, R * 2 - 3, R * 2 - 3);
        g.DrawArc(&hlPen, hr, 205.0f, 130.0f);

        // 圆环：边缘稍厚，active/按下增亮加粗
        int eA = active ? 255 : (pressed ? 255 : (hover ? 246 : 230));
        if (!enabled) eA = 140;
        float eW = 2.0f + 0.5f * hs + 0.5f * pressK;
        Gdiplus::Pen pen(Gdiplus::Color((BYTE)eA,
            GetRValue(bd), GetGValue(bd), GetBValue(bd)), eW);
        g.DrawEllipse(&pen, cx - R, cy - R, R * 2, R * 2);

        g.Restore(gstate);

        // ---------- 3) 涟漪 ----------
        if (rippleT < 1.f) {
            float e = EaseOutCubic(rippleT);
            float maxR = R * 1.6f;
            float rCur = maxR * e;
            int ringA = (int)(120.f * (1.f - e));
            if (ringA > 0) {
                Gdiplus::Pen ring(Gdiplus::Color((BYTE)ringA, 255, 255, 255), 2.0f);
                g.DrawEllipse(&ring, cx - rCur, cy - rCur, rCur * 2, rCur * 2);
            }
        }

        // ---------- 4) 悬浮投影 ----------
        {
            int shadowA = hover ? 48 : 30;
            if (!enabled) shadowA = 14;
            Gdiplus::SolidBrush shadowBr(Gdiplus::Color((BYTE)shadowA, 0, 0, 0));
            g.FillEllipse(&shadowBr, cx - R + 4, cy + R - 3, R * 2 - 8, 4.0f);
        }

        // ---------- 5) 内容：齿轮图标 / 文字（与玻璃板同一变换） ----------
        wchar_t txt[128] = { 0 };
        GetWindowTextW(hWnd, txt, 128);
        bool isGear = (_wcsicmp(txt, L"gear") == 0);
        Gdiplus::Color iconCol(
            pressed ? 255 : (hover ? 245 : 228),
            GetRValue(fg), GetGValue(fg), GetBValue(fg));
        Gdiplus::GraphicsState tstate = g.Save();
        GlassTransform(g, cx, cy, hs, pressK);
        if (isGear) {
            // 齿轮：外圈细环 + 8 齿 + 中心孔
            Gdiplus::Pen ringPen(iconCol, std::max(1.6f, R * 0.13f));
            float ringR = R * 0.46f;
            g.DrawEllipse(&ringPen, cx - ringR, cy - ringR, ringR * 2, ringR * 2);
            Gdiplus::SolidBrush tooth(iconCol);
            float tr = R * 0.13f, tR = R * 0.58f;
            for (int i = 0; i < 8; i++) {
                float a = (float)i * 3.14159265f / 4.0f;
                float tx = cx + tR * cosf(a), ty = cy + tR * sinf(a);
                g.FillEllipse(&tooth, tx - tr, ty - tr, tr * 2, tr * 2);
            }
            Gdiplus::Pen holePen(iconCol, std::max(1.3f, R * 0.10f));
            float hr_ = R * 0.20f;
            g.DrawEllipse(&holePen, cx - hr_, cy - hr_, hr_ * 2, hr_ * 2);
        } else {
            int fs = (int)(bh * 0.40f);
            if (fs < 11) fs = 11;
            if (fs > 16) fs = 16;
            Gdiplus::SolidBrush textBr(Gdiplus::Color(255,
                GetRValue(fg), GetGValue(fg), GetBValue(fg)));
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            g.DrawString(txt, -1, GlassFont(fs),
                Gdiplus::RectF(bx + 2.f, by, bw - 4.f, bh), &fmt, &textBr);
        }
        g.Restore(tstate);

        EndPaint(hWnd, &ps);
        if (self) self->RunHooks(YXG_HOOK_RENDER_POST, (void*)hWnd, (void*)"roundbutton");
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!IsWindowEnabled(hWnd)) return 1;
        if (self && !self->IsCtrlHover(hWnd)) {
            self->SetCtrlHover(hWnd, true);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
        }
        {
            GlassBtnAnim& a = g_glassAnims[hWnd];
            if (!a.hovered || a.hoverT < 0.999f) {
                a.hovered = true;
                SetTimer(hWnd, kGlassAnimTimerId, 16, nullptr);
                InvalidateRect(hWnd, NULL, TRUE);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        if (self && self->IsCtrlHover(hWnd)) self->SetCtrlHover(hWnd, false);
        {
            auto it = g_glassAnims.find(hWnd);
            if (it != g_glassAnims.end()) {
                it->second.hovered = false;
                SetTimer(hWnd, kGlassAnimTimerId, 16, nullptr);
                InvalidateRect(hWnd, NULL, TRUE);
            }
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!IsWindowEnabled(hWnd)) break;
        GlassBtnAnim& a = g_glassAnims[hWnd];
        a.pressed = true;
        a.t0 = GetTickCount64();
        a.scale = 1.f;
        a.rippleT = 0.f;
        SetTimer(hWnd, kGlassAnimTimerId, 16, nullptr);
        InvalidateRect(hWnd, NULL, TRUE);
        break;
    }
    case WM_LBUTTONUP: {
        auto it = g_glassAnims.find(hWnd);
        if (it != g_glassAnims.end() && it->second.pressed) {
            it->second.pressed = false;
            it->second.t0 = GetTickCount64();
            SetTimer(hWnd, kGlassAnimTimerId, 16, nullptr);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }
    case WM_CAPTURECHANGED: {
        auto it = g_glassAnims.find(hWnd);
        if (it != g_glassAnims.end() && it->second.pressed) {
            it->second.pressed = false;
            it->second.t0 = GetTickCount64();
            SetTimer(hWnd, kGlassAnimTimerId, 16, nullptr);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }
    case WM_TIMER: {
        if (wParam == kGlassAnimTimerId) {
            GlassAnimTick(hWnd);
            return 0;
        }
        break;
    }
    case WM_NCDESTROY:
        g_glassAnims.erase(hWnd);
        g_glassPad.erase(hWnd);
        break;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// v9: imagebox 子类 —— 按路径绘制图片（等比缩放到控件大小内）
LRESULT CALLBACK ImageBoxSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    JSPlugin* self = (JSPlugin*)dwRefData;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        const CtrlColor* cc = self ? self->GetCtrlColor(hWnd) : nullptr;
        COLORREF bg = (cc && cc->bg != -1) ? (COLORREF)cc->bg : RGB(244, 247, 251);
        COLORREF bd = (cc && cc->border != -1) ? (COLORREF)cc->border : RGB(203, 213, 225);
        bool hasBorder = (cc && cc->border != -1);

        HBRUSH brush = CreateSolidBrush(bg);
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);

        std::wstring path;
        {
            std::lock_guard<std::mutex> lock(g_imagePathsMutex);
            auto it = g_imagePaths.find(hWnd);
            if (it != g_imagePaths.end()) path = it->second;
        }

        if (!path.empty() && w > 4 && h > 4) {
            // v9.1: 走 LRU 缓存，避免每次都 FromFile
            Gdiplus::Bitmap* bmp = AcquireCachedImage(path);
            if (bmp) {
                Graphics g(hdc);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);

                int iw = bmp->GetWidth(), ih = bmp->GetHeight();
                if (iw > 0 && ih > 0) {
                    double scaleW = (double)(w - 4) / iw;
                    double scaleH = (double)(h - 4) / ih;
                    double scale = (scaleW < scaleH) ? scaleW : scaleH;
                    int dw = (int)(iw * scale), dh = (int)(ih * scale);
                    int dx = (w - dw) / 2, dy = (h - dh) / 2;
                    g.DrawImage(bmp, (Gdiplus::REAL)dx, (Gdiplus::REAL)dy,
                                (Gdiplus::REAL)dw, (Gdiplus::REAL)dh);
                }
                // Bitmap 由缓存持有，此处不 delete
            }
        }

        if (hasBorder) {
            HPEN pen = CreatePen(PS_SOLID, 1, bd);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY: {
        std::lock_guard<std::mutex> lock(g_imagePathsMutex);
        g_imagePaths.erase(hWnd);
        break;
    }
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

void JSPlugin::RememberControlType(HWND h, const std::string& type) {
    if (h) m_ctrlType[h] = type;
}
std::string JSPlugin::CtrlTypeOf(HWND h) const {
    auto it = m_ctrlType.find(h);
    return (it != m_ctrlType.end()) ? it->second : std::string();
}
const CtrlColor* JSPlugin::GetCtrlColor(HWND h) const {
    auto it = m_ctrlColors.find(h);
    return (it != m_ctrlColors.end()) ? &it->second : nullptr;
}
void JSPlugin::SetCtrlColor(HWND h, const CtrlColor& cc) {
    if (!h) return;
    m_ctrlColors[h] = cc;
}
bool JSPlugin::IsCtrlHover(HWND h) const {
    auto it = m_ctrlHover.find(h);
    return (it != m_ctrlHover.end()) && it->second;
}
void JSPlugin::SetCtrlHover(HWND h, bool on) {
    if (!h) return;
    m_ctrlHover[h] = on;
}
HBRUSH JSPlugin::BrushFor(COLORREF color) {
    auto it = m_brushCache.find(color);
    if (it != m_brushCache.end()) return it->second;
    HBRUSH b = CreateSolidBrush(color);
    if (b) m_brushCache[color] = b;
    return b;
}
HFONT JSPlugin::FlatFont() {
    if (m_hFlatFont) return (HFONT)m_hFlatFont;
    LOGFONTW lf = {0};
    lf.lfHeight = -13;
    lf.lfWeight = FW_BOLD;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    m_hFlatFont = CreateFontIndirectW(&lf);
    if (!m_hFlatFont) {
        m_hFlatFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    return (HFONT)m_hFlatFont;
}
void JSPlugin::ClearControlStyles() {
    for (auto& kv : m_brushCache) {
        if (kv.second) DeleteObject(kv.second);
    }
    m_brushCache.clear();
    if (m_hFlatFont && m_hFlatFont != GetStockObject(DEFAULT_GUI_FONT)) {
        DeleteObject((HFONT)m_hFlatFont);
    }
    m_hFlatFont = nullptr;
    m_ctrlType.clear();
    m_ctrlColors.clear();
    m_ctrlHover.clear();
}

bool JSPlugin::ApplyWallpaper(const std::string& mode, const std::string& colorHex,
    const std::wstring& imagePath, const std::wstring& cycleDir, int cycleMs) {
if (mode == "none") {
        m_wallpaper.mode = 0;
        BumpBgVersion();
        return true;
    }
    if (mode == "solid") {
        COLORREF c;
        if (!ParseColorHex(colorHex, c)) return false;
        m_wallpaper.mode = 1;
        m_wallpaper.color = c;
        BumpBgVersion();
        return true;
    }
    if (mode == "image") {
        if (imagePath.empty() || GetFileAttributesW(imagePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return false;
        }
        m_wallpaper.mode = 2;
        m_wallpaper.image = imagePath;
        BumpBgVersion();
        return true;
    }
    if (mode == "cycle") {
        if (cycleDir.empty() ||
            GetFileAttributesW(cycleDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return false;
        }
        m_wallpaper.mode = 3;
        m_wallpaper.cycleDir = cycleDir;
        m_wallpaper.cycleMs = (cycleMs > 500) ? cycleMs : 3000;
        m_wallpaper.lastTick = GetTickCount64();
        RefreshCycleFiles();
        BumpBgVersion();
        return !m_wallpaper.cycleFiles.empty();
    }
    if (mode == "video") {
        if (imagePath.empty() ||
            GetFileAttributesW(imagePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return false;
        }
        m_wallpaper.mode = 4;
        m_wallpaper.videoPath = imagePath;
        BumpBgVersion();
        return true;
    }
    if (mode == "lively") {
        if (cycleDir.empty() ||
            GetFileAttributesW(cycleDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return false;
        }
        m_wallpaper.mode = 5;
        m_wallpaper.livelyDir = cycleDir;
        BumpBgVersion();
        return true;
    }
return false;
}
void JSPlugin::RefreshCycleFiles() {
    m_wallpaper.cycleFiles.clear();

    if (m_wallpaper.cycleDir.empty()) {
        m_wallpaper.cycleIndex = 0;
        return;
    }

    // 动态 GIF：转换为 PNG 帧缓存，让现有 cycle 心跳可以逐帧播放。
    const std::wstring frameDir = m_wallpaper.cycleDir + L"\\.gif_frames";
    std::error_code ec;
    fs::remove_all(frameDir, ec);
    fs::create_directories(frameDir, ec);

    std::wstring pattern = m_wallpaper.cycleDir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::wstring name = fd.cFileName;
        if (!IsImageFile(name)) continue;

        std::wstring fullPath = m_wallpaper.cycleDir + L"\\" + name;
        std::wstring lower = name;
        for (auto& c : lower) c = towlower(c);

        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".gif") {
            std::vector<std::wstring> frames;
            if (ExtractGifFrames(fullPath, frameDir, frames)) {
                for (const auto& frame : frames) {
                    m_wallpaper.cycleFiles.push_back(frame);
                }
                continue;
            }
        }

        m_wallpaper.cycleFiles.push_back(fullPath);
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    if (!m_wallpaper.cycleFiles.empty()) {
        m_wallpaper.cycleIndex %= m_wallpaper.cycleFiles.size();
    } else {
        m_wallpaper.cycleIndex = 0;
    }
}

bool JSPlugin::CycleNext() {
    if (m_wallpaper.cycleFiles.empty()) return false;
    m_wallpaper.cycleIndex = (m_wallpaper.cycleIndex + 1) % m_wallpaper.cycleFiles.size();
    m_wallpaper.lastTick = GetTickCount64();
    return true;
}
void JSPlugin::ClearWallpaperCache() {
    if (m_wallpaper.imgCache) {
        delete (Bitmap*)m_wallpaper.imgCache;
        m_wallpaper.imgCache = nullptr;
    }
    m_wallpaper.cycleFiles.clear();
}

// ===== 玻璃按钮背景快照（v10：性能） =====
// 返回父窗口背景位图；仅当尺寸或背景版本变化时才重建（宿主整窗重绘代价高）。
HBITMAP JSPlugin::AcquireParentBgSnapshot(HWND hParent, int pw, int ph, ULONGLONG ver) {
    if (m_parentBgBmp && m_parentBgW == pw && m_parentBgH == ph && m_bgVerStored == ver) {
        return m_parentBgBmp;
    }
    if (m_parentBgBmp) {
        DeleteObject(m_parentBgBmp);
        m_parentBgBmp = nullptr;
    }
    HDC memDC = CreateCompatibleDC(nullptr);
    if (!memDC) return nullptr;
    m_parentBgBmp = CreateCompatibleBitmap(memDC, pw, ph);
    if (!m_parentBgBmp) {
        DeleteDC(memDC);
        return nullptr;
    }
    HGDIOBJ oldBmp = SelectObject(memDC, m_parentBgBmp);
    // 触发宿主（父窗口 proc）重绘最新背景到快照位图
    SendMessageW(hParent, WM_ERASEBKGND, (WPARAM)memDC, 0);
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    m_parentBgW = pw;
    m_parentBgH = ph;
    m_bgVerStored = ver;
    return m_parentBgBmp;
}

void JSPlugin::ClearBgCache() {
    if (m_parentBgBmp) {
        DeleteObject(m_parentBgBmp);
        m_parentBgBmp = nullptr;
    }
    m_parentBgW = m_parentBgH = 0;
    m_bgVerStored = 0;
}

bool JSPlugin::EvalFile(const std::wstring& absPath) {
    if (!m_ctx) return false;
    std::ifstream f(absPath, std::ios::binary);
    if (!f.is_open()) return false;
    std::string code((std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    f.close();
    return EvalString(code, absPath);
}

bool JSPlugin::EvalString(const std::string& code, const std::wstring& filenameHint) {
    if (!m_ctx) return false;
    std::string fname = WideToUtf8(filenameHint);
    JSValue val = JS_Eval(m_ctx, code.c_str(), code.size(),
        fname.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(m_ctx);
        const char* s = JS_ToCString(m_ctx, exc);
        if (s) {
            OutputDebugStringA("[JSPlugin] 脚本异常: ");
            OutputDebugStringA(s);
            OutputDebugStringA("\n");
            JS_FreeCString(m_ctx, s);
        }
        JS_FreeValue(m_ctx, exc);
        JS_FreeValue(m_ctx, val);
        return false;
    }
JS_FreeValue(m_ctx, val);
    return true;
}

// 驱动 QuickJS 微任务队列（Promise .then / await 的后续代码）
// 在任何 JS 回调返回后调用，确保异步链能继续推进。
static void DrainJobs(JSContext* ctx) {
    if (!ctx) return;
    JSRuntime* rt = JS_GetRuntime(ctx);
    for (;;) {
        JSContext* jobCtx = nullptr;
        int r = JS_ExecutePendingJob(rt, &jobCtx);
        if (r <= 0) {
            if (r < 0 && jobCtx) {
                JSValue exc = JS_GetException(jobCtx);
                const char* s = JS_ToCString(jobCtx, exc);
                if (s) {
                    OutputDebugStringA("[JSPlugin] promise 任务异常: ");
                    OutputDebugStringA(s);
                    OutputDebugStringA("\n");
                    JS_FreeCString(jobCtx, s);
                }
                JS_FreeValue(jobCtx, exc);
            }
            break;
        }
    }
}

bool JSPlugin::CallGlobalFunction(const char* fnName, int argc, void** argv) {
    if (!m_ctx) return false;
    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue fn = JS_GetPropertyStr(m_ctx, global, fnName);
    JS_FreeValue(m_ctx, global);

    if (!JS_IsFunction(m_ctx, fn)) {
        JS_FreeValue(m_ctx, fn);
        return false;
    }

    JSValue ret = JS_Call(m_ctx, fn, JS_UNDEFINED, argc, (JSValue*)argv);
    JS_FreeValue(m_ctx, fn);

    if (JS_IsException(ret)) {
        JSValue exc = JS_GetException(m_ctx);
        const char* s = JS_ToCString(m_ctx, exc);
        if (s) {
            OutputDebugStringA("[JSPlugin] 回调异常: ");
            OutputDebugStringA(s);
            OutputDebugStringA("\n");
            JS_FreeCString(m_ctx, s);
        }
        JS_FreeValue(m_ctx, exc);
        JS_FreeValue(m_ctx, ret);
        return false;
    }
JS_FreeValue(m_ctx, ret);
    DrainJobs(m_ctx);
    return true;
}

void JSPlugin::CallOnLoad() {
    // 扩展生命周期 Hook：拦截则跳过 onLoad
    if (RunHooks(YXG_HOOK_LIFECYCLE_LOAD, nullptr, nullptr)) return;
    CallGlobalFunction("onLoad");
}

void JSPlugin::CallOnViewShow(HWND hostContainer, int w, int h) {
    m_hostContainer = hostContainer;
    if (RunHooks(YXG_HOOK_LIFECYCLE_SHOW, nullptr, nullptr)) return;
    if (!m_ctx) return;

    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue fn = JS_GetPropertyStr(m_ctx, global, "onViewShow");
    JS_FreeValue(m_ctx, global);
    if (!JS_IsFunction(m_ctx, fn)) {
        JS_FreeValue(m_ctx, fn);
        return;
    }

    JSValue args[2];
    args[0] = JS_NewInt32(m_ctx, w);
    args[1] = JS_NewInt32(m_ctx, h);
JSValue ret = JS_Call(m_ctx, fn, JS_UNDEFINED, 2, args);
    JS_FreeValue(m_ctx, args[0]);
    JS_FreeValue(m_ctx, args[1]);
    JS_FreeValue(m_ctx, fn);
    if (JS_IsException(ret)) {
        JSValue exc = JS_GetException(m_ctx);
        JS_FreeValue(m_ctx, exc);
    }
    JS_FreeValue(m_ctx, ret);
    DrainJobs(m_ctx);
}

void JSPlugin::CallOnViewHide() {
    if (RunHooks(YXG_HOOK_LIFECYCLE_HIDE, nullptr, nullptr)) return;
    CallGlobalFunction("onViewHide");
}

void JSPlugin::CallOnResize(int w, int h) {
    if (RunHooks(YXG_HOOK_LIFECYCLE_RESIZE, nullptr, nullptr)) return;
    if (!m_ctx) return;
    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue fn = JS_GetPropertyStr(m_ctx, global, "onResize");
    JS_FreeValue(m_ctx, global);
    if (!JS_IsFunction(m_ctx, fn)) {
        JS_FreeValue(m_ctx, fn);
        return;
    }
    JSValue args[2] = { JS_NewInt32(m_ctx, w), JS_NewInt32(m_ctx, h) };
    JSValue ret = JS_Call(m_ctx, fn, JS_UNDEFINED, 2, args);
    JS_FreeValue(m_ctx, args[0]);
    JS_FreeValue(m_ctx, args[1]);
    JS_FreeValue(m_ctx, fn);
    JS_FreeValue(m_ctx, ret);
}

void JSPlugin::CallOnUnload() {
    if (RunHooks(YXG_HOOK_LIFECYCLE_UNLOAD, nullptr, nullptr)) return;
    CallGlobalFunction("onUnload");
}

bool JSPlugin::RequestPermission(const std::wstring& actionDesc) {
    if (m_permanentAuth) return true;

    // 先查 cf.m
    if (PermissionManager::Instance().IsPermanentlyGranted(m_id)) {
        m_permanentAuth = true;
        return true;
    }

    if (!m_permPrompt) return false;
    return m_permPrompt(m_id, m_name, actionDesc);
}

// src/JSPlugin.cpp 续
#include "JSPlugin.h"
#include "PermissionManager.h"

extern "C" {
#include "quickjs.h"
}

// ============================================================
// 工具：把 JSValue 转 std::wstring
// ============================================================
static std::wstring JSValueToWString(JSContext* ctx, JSValueConst v) {
    const char* s = JS_ToCString(ctx, v);
    if (!s) return L"";
    std::wstring w(s, s + strlen(s));
    // 从 UTF-8 转宽字符
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    std::wstring out(len > 0 ? len - 1 : 0, 0);
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], len);
    JS_FreeCString(ctx, s);
    return out;
}

static JSValue WStringToJSValue(JSContext* ctx, const std::wstring& w) {
    if (w.empty()) return JS_NewString(ctx, "");
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, NULL, NULL);
    return JS_NewStringLen(ctx, s.c_str(), s.size());
}

// ============================================================

// 异步 HTTP 完成（主窗口线程收到 WM_YXGUI_HTTP_DONE 后调用）
// resolve({ ok, status, body }) 或 resolve({ ok:false, status, error })
void JSPlugin::CallHttpDone(uint32_t seq, const HttpResult& result) {
    if (!m_ctx) return;
    auto it = m_pendingHttp.find(seq);
    if (it == m_pendingHttp.end()) return;   // 非本插件 / 已清理
    uint64_t bits[2] = { it->second.a, it->second.b };
    m_pendingHttp.erase(it);

    JSValue resolve;
    static_assert(sizeof(JSValue) <= sizeof(bits), "JSValue 过大");
    memcpy(&resolve, bits, sizeof(JSValue));

    JSValue obj = JS_NewObject(m_ctx);
    if (result.status >= 0) {
        JS_SetPropertyStr(m_ctx, obj, "ok", JS_TRUE);
        JS_SetPropertyStr(m_ctx, obj, "status", JS_NewInt32(m_ctx, result.status));
        JS_SetPropertyStr(m_ctx, obj, "body",
            JS_NewStringLen(m_ctx, result.body.c_str(), result.body.size()));
    }
    else {
        JS_SetPropertyStr(m_ctx, obj, "ok", JS_FALSE);
        JS_SetPropertyStr(m_ctx, obj, "status", JS_NewInt32(m_ctx, result.status));
        JS_SetPropertyStr(m_ctx, obj, "error",
            JS_NewStringLen(m_ctx, result.error.c_str(), result.error.size()));
        JS_SetPropertyStr(m_ctx, obj, "body", JS_NewStringLen(m_ctx, "", 0));
    }

    JSValue ret = JS_Call(m_ctx, resolve, JS_UNDEFINED, 1, &obj);
    if (JS_IsException(ret)) {
        JSValue exc = JS_GetException(m_ctx);
        const char* s = JS_ToCString(m_ctx, exc);
        if (s) {
            OutputDebugStringA("[JSPlugin] http resolve 异常: ");
            OutputDebugStringA(s);
            OutputDebugStringA("\n");
            JS_FreeCString(m_ctx, s);
        }
        JS_FreeValue(m_ctx, exc);
    }
JS_FreeValue(m_ctx, ret);
    JS_FreeValue(m_ctx, obj);
    JS_FreeValue(m_ctx, resolve);
    DrainJobs(m_ctx);
}

// 子控件事件 -> JS 全局 onControlEvent(controlId, code)
// 由宿主容器 WM_COMMAND 转发，必须在 UI 线程调用
void JSPlugin::CallOnControlEvent(const std::wstring& controlId, int code) {
    // 扩展控件事件 Hook：dataIn = controlId（UTF-8），拦截则跳过 JS
    std::string idUtf8 = WideToUtf8(controlId);
    if (RunHooks(YXG_HOOK_CONTROL_EVENT, (void*)idUtf8.c_str(), nullptr)) return;
    if (!m_ctx) return;

    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue fn = JS_GetPropertyStr(m_ctx, global, "onControlEvent");
    JS_FreeValue(m_ctx, global);
    if (!JS_IsFunction(m_ctx, fn)) {
        JS_FreeValue(m_ctx, fn);
        return;
    }

    JSValue args[2];
    args[0] = WStringToJSValue(m_ctx, controlId);
    args[1] = JS_NewInt32(m_ctx, code);
    JSValue ret = JS_Call(m_ctx, fn, JS_UNDEFINED, 2, args);
    JS_FreeValue(m_ctx, args[0]);
    JS_FreeValue(m_ctx, args[1]);
    JS_FreeValue(m_ctx, fn);

    if (JS_IsException(ret)) {
        JSValue exc = JS_GetException(m_ctx);
        const char* s = JS_ToCString(m_ctx, exc);
        if (s) {
            OutputDebugStringA("[JSPlugin] onControlEvent 异常: ");
            OutputDebugStringA(s);
            OutputDebugStringA("\n");
            JS_FreeCString(m_ctx, s);
        }
        JS_FreeValue(m_ctx, exc);
}
    JS_FreeValue(m_ctx, ret);
    DrainJobs(m_ctx);
}

// ============================================================
// host.* API
// ============================================================

// host.log(msg)
static JSValue js_host_log(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    const char* s = JS_ToCString(ctx, argv[0]);
    if (s) {
        OutputDebugStringA("[JSPlugin] ");
        OutputDebugStringA(s);
        OutputDebugStringA("\n");
        JS_FreeCString(ctx, s);
    }
    return JS_UNDEFINED;
}

// host.getId() / host.getName() 等由属性提供，这里不重复

// host.requestPermission(actionDesc) → bool
static JSValue js_host_requestPermission(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self) return JS_FALSE;
    std::wstring desc = L"未知操作";
    if (argc >= 1) desc = JSValueToWString(ctx, argv[0]);
    bool ok = self->RequestPermission(desc);
    return ok ? JS_TRUE : JS_FALSE;
}

// host.isAuthorized() → bool
static JSValue js_host_isAuthorized(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self) return JS_FALSE;
    return self->IsPermanentlyAuthorized() ? JS_TRUE : JS_FALSE;
}

// host.listPlugins() → string[]
static JSValue js_host_listPlugins(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self) return JS_NewArray(ctx);
    auto list = self->GetPluginList();
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < list.size(); ++i) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i, WStringToJSValue(ctx, list[i]));
    }
    return arr;
}

// ============================================================
// 原生控件创建（JS 绑定层与 ApiAiNative 原生插件共用）
// ============================================================
HWND JSPlugin::CreateNativeControl(const std::string& typeArg,
    const std::wstring& textArg, int x, int y, int w, int h,
    const std::wstring& idArg) {
    HWND host = GetHostContainer();
    if (!host || !IsWindow(host)) return nullptr;

    // 扩展 CREATE_CONTROL_PRE Hook：可拦截（返回 1）或修改参数（回写 desc 字段）
    std::string sType = typeArg;
    std::string sId = WideToUtf8(idArg);
    std::string sText = WideToUtf8(textArg);
    YXG_ControlDesc desc{ sType.c_str(), sId.c_str(), sText.c_str(),
        x, y, w, h, nullptr, nullptr, nullptr };
    if (RunHooks(YXG_HOOK_CREATE_CONTROL_PRE, &desc, nullptr)) return nullptr;
    // 读回扩展可能修改的参数
    if (desc.type) sType = desc.type;
    if (desc.id) sId = desc.id;
    if (desc.text) sText = desc.text;
    x = desc.x; y = desc.y; w = desc.w; h = desc.h;

    std::wstring type = Utf8ToWide(sType);
    std::wstring text = Utf8ToWide(sText);
    std::wstring id = Utf8ToWide(sId);

    // 映射到 Win32 类名 + 默认样式
    const wchar_t* cls = L"STATIC";
    DWORD style = WS_CHILD | WS_VISIBLE | SS_LEFT;
    if (type == L"button") {
        cls = L"BUTTON";
        style = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
    }
    else if (type == L"edit") {
        cls = L"EDIT";
        style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    }
    else if (type == L"multiline") {
        cls = L"EDIT";
        style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
            ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN;
    }
    else if (type == L"output") {
        // AI 输出专用：无 WS_BORDER，视觉上与宿主背景融为一体。
        cls = L"EDIT";
        style = WS_CHILD | WS_VISIBLE | ES_MULTILINE |
            ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN | ES_READONLY;
    }
    else if (type == L"textarea") {
        // v9: 无边框多行输入，嵌在 panel 卡片里；卡片提供灰边与圆角。
        cls = L"EDIT";
        style = WS_CHILD | WS_VISIBLE | ES_MULTILINE |
            ES_AUTOVSCROLL | ES_WANTRETURN | ES_LEFT;
    }
    else if (type == L"roundbutton") {
        // v9: 自绘圆形/药丸按钮（发送、关闭等图标按钮）。
        // v10: 改为 BS_OWNERDRAW，避免系统按钮与玻璃自绘冲突。
        cls = L"BUTTON";
        style = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
    }
    else if (type == L"imagebox") {
        // v9: 图片显示控件（壁纸缩略图等）。
        cls = L"STATIC";
        style = WS_CHILD | WS_VISIBLE | SS_NOTIFY;
    }
    else if (type == L"combobox") {
        // v9: 下拉框（模型列表等）。
        cls = L"COMBOBOX";
        style = WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            CBS_DROPDOWN | WS_TABSTOP | WS_VSCROLL;
    }
    else if (type == L"richoutput") {
        // v9.3: 富文本输出（Markdown 渲染）
        static bool s_richInit = false;
        if (!s_richInit) {
            LoadLibraryW(L"Msftedit.dll");
            s_richInit = true;
        }
        cls = MSFTEDIT_CLASS;
        style = WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
            WS_VSCROLL | ES_AUTOVSCROLL | ES_NOHIDESEL;
    }
    else if (type == L"listbox") {
        cls = L"LISTBOX";
        style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | LBS_NOTIFY;
    }
    else if (type == L"checkbox") {
        cls = L"BUTTON";
        style = WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX;
    }
    else if (type == L"label") {
        cls = L"STATIC";
        style = WS_CHILD | WS_VISIBLE | SS_LEFT;
    }
    else if (type == L"panel") {
        cls = L"STATIC";
        style = WS_CHILD | WS_VISIBLE | SS_LEFT;
    }
    // 更多类型可选扩展

    int ctrlId = AllocControlId();
    HWND hCtrl = CreateWindowExW(0, cls, text.c_str(),
        style, x, y, w, h,
        host, (HMENU)(INT_PTR)ctrlId,
        GetModuleHandle(NULL), NULL);

    if (!hCtrl) return nullptr;

    // 统一设置宋体字体
    HFONT hFont = FlatFont();
    if (hFont) SendMessageW(hCtrl, WM_SETFONT, (WPARAM)hFont, TRUE);

    // v3：记录控件类型；button 应用扁平自绘、panel 应用圆角卡片自绘
    std::string typeA = WideToUtf8(type);
    RememberControlType(hCtrl, typeA);
    if (type == L"button") {
        SetWindowSubclass(hCtrl, FlatButtonSubclassProc, 0, (DWORD_PTR)this);
    }
    else if (type == L"panel") {
        SetWindowSubclass(hCtrl, PanelCardSubclassProc, 0, (DWORD_PTR)this);
    }
    else if (type == L"textarea") {
        SetWindowSubclass(hCtrl, TextareaSubclassProc, 0, (DWORD_PTR)this);
    }
    else if (type == L"roundbutton") {
        SetWindowSubclass(hCtrl, RoundButtonSubclassProc, 0, (DWORD_PTR)this);
    }
    else if (type == L"imagebox") {
        SetWindowSubclass(hCtrl, ImageBoxSubclassProc, 0, (DWORD_PTR)this);
    }
    else if (type == L"combobox") {
        // v9.1: 用 Explorer 主题替换默认主题，
        //       让下拉列表在视觉上与卡片边缘对齐。
        SetWindowTheme(hCtrl, L"Explorer", NULL);
    }

    // 使用 "pluginId::controlId" 作为 key，避免不同插件 id 冲突
    std::wstring key = id.empty()
        ? (GetId() + L"::" + std::to_wstring(ctrlId))
        : (GetId() + L"::" + id);
    MutableControls()[key] = hCtrl;

    // 扩展 CREATE_CONTROL_POST Hook：控件已创建，desc 仍可读
    RunHooks(YXG_HOOK_CREATE_CONTROL_POST, &desc, nullptr);

    return hCtrl;
}

// ============================================================
// win32.* API
// ============================================================

// win32.createControl({type, text, x, y, w, h, id})
static JSValue js_win32_createControl(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || !self->GetHostContainer()) return JS_NULL;
    if (argc < 1 || !JS_IsObject(argv[0])) return JS_NULL;

    // 读取参数
    auto getProp = [&](const char* key) -> JSValue {
        return JS_GetPropertyStr(ctx, argv[0], key);
    };

    std::wstring type = L"static";
    std::wstring text = L"";
    std::wstring id = L"";
    int x = 0, y = 0, w = 100, h = 30;

    JSValue vType = getProp("type");
    if (JS_IsString(vType)) type = JSValueToWString(ctx, vType);
    JS_FreeValue(ctx, vType);

    JSValue vText = getProp("text");
    if (JS_IsString(vText)) text = JSValueToWString(ctx, vText);
    JS_FreeValue(ctx, vText);

    JSValue vId = getProp("id");
    if (JS_IsString(vId)) id = JSValueToWString(ctx, vId);
    JS_FreeValue(ctx, vId);

    auto getInt = [&](const char* key, int def) -> int {
        JSValue v = getProp(key);
        int r = def;
        if (JS_IsNumber(v)) JS_ToInt32(ctx, &r, v);
        JS_FreeValue(ctx, v);
        return r;
        };
    x = getInt("x", 0);
    y = getInt("y", 0);
    w = getInt("w", 100);
    h = getInt("h", 30);

// 复用公共原生控件创建（与 ApiAiNative 同路径），
    // Hook / 子类化 / 登记语义完全一致。
    std::string typeUtf8 = WideToUtf8(type);
    HWND hCtrl = self->CreateNativeControl(typeUtf8, text, x, y, w, h, id);
    if (!hCtrl) return JS_NULL;

    wchar_t buf[32];
    swprintf_s(buf, L"%p", hCtrl);
    return WStringToJSValue(ctx, buf);
}

// win32.destroyControl(id)
static JSValue js_win32_destroyControl(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_UNDEFINED;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    std::wstring fullKey = self->GetId() + L"::" + key;
    auto& map = self->MutableControls();
    auto it = map.find(fullKey);
    if (it != map.end()) {
        if (IsWindow(it->second)) DestroyWindow(it->second);
        map.erase(it);
    }
    return JS_UNDEFINED;
}

// win32.setWindowText(id, text)
static JSValue js_win32_setWindowText(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 2) return JS_UNDEFINED;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    std::wstring text = JSValueToWString(ctx, argv[1]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it != map.end()) {
        SetWindowTextW(it->second, text.c_str());
    }
    return JS_UNDEFINED;
}

// win32.getWindowText(id) → string
static JSValue js_win32_getWindowText(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_NewString(ctx, "");
    std::wstring key = JSValueToWString(ctx, argv[0]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it != map.end() && IsWindow(it->second)) {
        int len = GetWindowTextLengthW(it->second);
        std::wstring buf(len, 0);
        GetWindowTextW(it->second, &buf[0], len + 1);
        return WStringToJSValue(ctx, buf);
    }
    return JS_NewString(ctx, "");
}

// win32.moveWindow(id, x, y, w, h)
static JSValue js_win32_moveWindow(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 5) return JS_UNDEFINED;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    int x, y, w, h;
    JS_ToInt32(ctx, &x, argv[1]);
    JS_ToInt32(ctx, &y, argv[2]);
    JS_ToInt32(ctx, &w, argv[3]);
    JS_ToInt32(ctx, &h, argv[4]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it != map.end() && IsWindow(it->second)) {
        MoveWindow(it->second, x, y, w, h, TRUE);
    }
    return JS_UNDEFINED;
}

// win32.setVisible(id, visible)
static JSValue js_win32_setVisible(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 2) return JS_UNDEFINED;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    int visible = JS_ToBool(ctx, argv[1]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it != map.end() && IsWindow(it->second)) {
        ShowWindow(it->second, visible ? SW_SHOW : SW_HIDE);
    }
    return JS_UNDEFINED;
}

// win32.getHostSize() → {w, h}
static JSValue js_win32_getHostSize(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    HWND host = self ? self->GetHostContainer() : nullptr;
    RECT rc = { 0, 0, 0, 0 };
    if (host && IsWindow(host)) GetClientRect(host, &rc);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "w", JS_NewInt32(ctx, rc.right - rc.left));
    JS_SetPropertyStr(ctx, obj, "h", JS_NewInt32(ctx, rc.bottom - rc.top));
    return obj;
}

// win32.clear() —— 清空本插件所有控件
static JSValue js_win32_clear(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self) return JS_UNDEFINED;
    auto& map = self->MutableControls();
    for (auto& kv : map) {
        if (kv.second && IsWindow(kv.second)) DestroyWindow(kv.second);
    }
    map.clear();
    return JS_UNDEFINED;
}

// win32.sendMessage(id, msg, wParam, lParam)
static JSValue js_win32_sendMessage(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 4) return JS_UNDEFINED;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    uint32_t msg = 0, wp = 0, lp = 0;
    JS_ToUint32(ctx, &msg, argv[1]);
    JS_ToUint32(ctx, &wp, argv[2]);
    JS_ToUint32(ctx, &lp, argv[3]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it != map.end() && IsWindow(it->second)) {
        SendMessageW(it->second, msg, (WPARAM)wp, (LPARAM)lp);
    }
    return JS_UNDEFINED;
}

// ============================================================
// light.* API
// ============================================================

// light.exec(cmd) → Promise<bool>
// 未授权：弹窗询问。授权后通过 m_lightCmd 转发命令。
static JSValue js_light_exec(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_NULL;

    std::string cmd;
    {
        const char* s = JS_ToCString(ctx, argv[0]);
        if (s) { cmd = s; JS_FreeCString(ctx, s); }
    }

    // 请求权限（弹窗/查表）
    std::wstring desc = L"执行命令：" + std::wstring(cmd.begin(), cmd.end());
    if (!self->RequestPermission(desc)) {
        // 用户拒绝
        return JS_NewBool(ctx, false);
    }

// 转发
    self->ForwardLightCommand(cmd);
    return JS_NewBool(ctx, true);
}

// ============================================================
// 异步 HTTP：win32.httpRequest(opts) / light.request(opts) → Promise
// opts = { url, method?, headers?{name:value}, body?, timeoutMs? }
// 后台线程调用 light.exe 导出的 LightApi_HttpRequest，结果经
// PostMessage 回主窗口线程，由 JSPlugin::CallHttpDone resolve Promise。
// ============================================================

namespace {

struct HttpWorkerArgs {
    std::string method;   // 默认 GET
    std::string url;
    std::string headers;  // "Name: value\r\n..." 或空
    std::string body;
    int timeoutMs = 0;
    uint32_t seq = 0;
    HWND notifyWnd = nullptr;
    UINT notifyMsg = 0;
};

// 后台线程入口（args 所有权在本函数内释放；done 由主线程 delete）
void HttpWorkerThreadProc(HttpWorkerArgs* args) {
    auto* done = new JSPlugin::HttpTaskDoneMsg;
    done->seq = args->seq;

    int rc = -1;
    HMODULE hExe = GetModuleHandleW(NULL);
    if (hExe) {
        typedef int (WINAPI* FnHttpRequest)(const char*, const char*, const char*,
            const char*, int, int*, char**);
        typedef void (WINAPI* FnFree)(void*);
        auto fnHttp = (FnHttpRequest)GetProcAddress(hExe, "LightApi_HttpRequest");
        auto fnFree = (FnFree)GetProcAddress(hExe, "LightApi_Free");
        if (fnHttp) {
            char* outBody = nullptr;
            int statusCode = 0;
            rc = fnHttp(args->method.c_str(), args->url.c_str(),
                args->headers.empty() ? nullptr : args->headers.c_str(),
                args->body.c_str(), args->timeoutMs, &statusCode, &outBody);
            done->result.status = statusCode;
            if (rc == 0 && outBody) {
                done->result.body.assign(outBody);
            }
            if (outBody && fnFree) fnFree(outBody);
        }
    }

    if (rc != 0) {
        done->result.status = rc;
        done->result.error = "HTTP 请求失败（错误码 " + std::to_string(rc) + "）";
    }

    if (args->notifyWnd && IsWindow(args->notifyWnd)) {
        PostMessageW(args->notifyWnd, args->notifyMsg, 0, (LPARAM)done);
    }
    else {
        delete done;  // 窗口已销毁，无人接收
    }
    delete args;
}

} // namespace

// win32.httpRequest(opts) / light.request(opts) → Promise<void>（值对象）
static JSValue js_http_request(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1 || !JS_IsObject(argv[0])) return JS_NULL;

    // 读取 opts
    JSValue vUrl = JS_GetPropertyStr(ctx, argv[0], "url");
    JSValue vMethod = JS_GetPropertyStr(ctx, argv[0], "method");
    JSValue vBody = JS_GetPropertyStr(ctx, argv[0], "body");
    JSValue vTimeout = JS_GetPropertyStr(ctx, argv[0], "timeoutMs");
    JSValue vHeaders = JS_GetPropertyStr(ctx, argv[0], "headers");

    std::string url, method = "GET", body;
    if (JS_IsString(vUrl)) {
        const char* s = JS_ToCString(ctx, vUrl);
        if (s) { url = s; JS_FreeCString(ctx, s); }
    }
    if (JS_IsString(vMethod)) {
        const char* s = JS_ToCString(ctx, vMethod);
        if (s) { method = s; JS_FreeCString(ctx, s); }
    }
    if (JS_IsString(vBody)) {
        const char* s = JS_ToCString(ctx, vBody);
        if (s) { body = s; JS_FreeCString(ctx, s); }
    }
    int timeoutMs = 0;
    if (JS_IsNumber(vTimeout)) JS_ToInt32(ctx, &timeoutMs, vTimeout);

    std::string headers;
    if (JS_IsObject(vHeaders)) {
        JSPropertyEnum* tab = nullptr;
        uint32_t len = 0;
        if (JS_GetOwnPropertyNames(ctx, &tab, &len, vHeaders,
            JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < len; i++) {
                const char* k = JS_AtomToCString(ctx, tab[i].atom);
                JSValue v = JS_GetProperty(ctx, vHeaders, tab[i].atom);
                const char* vs = JS_ToCString(ctx, v);
                if (k && vs) {
                    headers += k; headers += ": "; headers += vs; headers += "\r\n";
                }
                if (vs) JS_FreeCString(ctx, vs);
                JS_FreeValue(ctx, v);
                if (k) JS_FreeCString(ctx, k);
            }
            JS_FreePropertyEnum(ctx, tab, len);
        }
    }
    JS_FreeValue(ctx, vUrl);
    JS_FreeValue(ctx, vMethod);
    JS_FreeValue(ctx, vBody);
    JS_FreeValue(ctx, vTimeout);
    JS_FreeValue(ctx, vHeaders);

    if (url.empty()) return JS_NULL;

    // 扩展 HTTP_PRE Hook：可拦截（返回 1）或修改请求参数字段
    YXG_HttpRequest req{ url.c_str(), method.c_str(), headers.c_str(), body.c_str(), timeoutMs };
    if (self->RunHooks(YXG_HOOK_HTTP_PRE, &req, nullptr)) return JS_NULL;
    // 读回扩展可能修改的字段
    if (req.url) url = req.url;
    if (req.method) method = req.method;
    if (req.body) body = req.body;
    if (req.headersJson) headers = req.headersJson;
    timeoutMs = req.timeoutMs;

    // 创建 Promise，保存 resolve 函数（位拷贝；跨线程只传 seq）
    JSValue resolving[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        return JS_EXCEPTION;
    }

    static std::atomic<uint32_t> g_httpSeq{ 1 };  // 跨插件全局唯一
    uint32_t seq = g_httpSeq.fetch_add(1);
    uint64_t bits[2] = { 0, 0 };
    static_assert(sizeof(JSValue) <= sizeof(bits), "JSValue 过大");
    memcpy(bits, &resolving[0], sizeof(JSValue));
    self->StorePendingHttp(seq, bits[0], bits[1]);
    JS_FreeValue(ctx, resolving[1]);  // 本实现统一 resolve 结果对象，不使用 reject

    auto* args = new HttpWorkerArgs;
    args->method = method;
    args->url = url;
    args->headers = headers;
    args->body = body;
    args->timeoutMs = timeoutMs;
    args->seq = seq;
    args->notifyWnd = self->GetOwnerWnd();
    args->notifyMsg = WM_YXGUI_HTTP_DONE;
    std::thread(HttpWorkerThreadProc, args).detach();

    // 扩展 HTTP_POST Hook：请求已派发至后台线程
    self->RunHooks(YXG_HOOK_HTTP_POST, &req, nullptr);
    return promise;
}

// ============================================================
// v9.2: 流式 HTTP（WinHTTP）
// ============================================================
namespace {

struct HttpStreamArgs {
    std::string url;
    std::string method;
    std::string headers;
    std::string body;
    int timeoutMs = 0;
    uint32_t seq = 0;
    HWND notifyWnd = nullptr;
    UINT notifyMsg = 0;
};

void PostStreamChunk(HWND wnd, UINT msg, uint32_t seq,
    std::string chunk, bool isDone, int status, std::string error) {
    if (!wnd || !IsWindow(wnd)) return;
    auto* m = new HttpStreamChunkMsg;
    m->seq = seq;
    m->chunk = std::move(chunk);
    m->isDone = isDone;
    m->status = status;
    m->error = std::move(error);
    if (!PostMessageW(wnd, msg, 0, (LPARAM)m)) delete m;
}

void HttpStreamWorker(HttpStreamArgs* args) {
    HINTERNET hSession = WinHttpOpen(L"YXGUI-AI-API/9.2",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        PostStreamChunk(args->notifyWnd, args->notifyMsg, args->seq,
            "", true, -1, "WinHttpOpen 失败");
        delete args;
        return;
    }
    int to = args->timeoutMs > 0 ? args->timeoutMs : 120000;
    WinHttpSetTimeouts(hSession, 15000, 15000, to, to);

    std::wstring wurl = Utf8ToWide(args->url);
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[512] = { 0 };
    wchar_t path[4096] = { 0 };
    uc.lpszHostName = host;
    uc.dwHostNameLength = 512;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        WinHttpCloseHandle(hSession);
        PostStreamChunk(args->notifyWnd, args->notifyMsg, args->seq,
            "", true, -1, "URL 解析失败");
        delete args;
        return;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        PostStreamChunk(args->notifyWnd, args->notifyMsg, args->seq,
            "", true, -1, "WinHttpConnect 失败");
        delete args;
        return;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    std::wstring wmethod = Utf8ToWide(args->method.empty() ? "POST" : args->method);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod.c_str(), path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        PostStreamChunk(args->notifyWnd, args->notifyMsg, args->seq,
            "", true, -1, "WinHttpOpenRequest 失败");
        delete args;
        return;
    }

    // 追加请求头
    if (!args->headers.empty()) {
        std::wstring wheaders = Utf8ToWide(args->headers);
        // headers 用 CRLF 分隔；逐行添加更稳
        size_t start = 0;
        while (start < wheaders.size()) {
            size_t end = wheaders.find(L"\r\n", start);
            std::wstring line = (end == std::wstring::npos)
                ? wheaders.substr(start)
                : wheaders.substr(start, end - start);
            if (!line.empty()) {
                WinHttpAddRequestHeaders(hRequest, line.c_str(), (DWORD)-1L,
                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            }
            if (end == std::wstring::npos) break;
            start = end + 2;
        }
    }

    BOOL sent = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        args->body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)args->body.data(),
        (DWORD)args->body.size(), (DWORD)args->body.size(), 0);

    if (!sent || !WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        char buf[64];
        sprintf_s(buf, "WinHTTP 错误 %lu", err);
        PostStreamChunk(args->notifyWnd, args->notifyMsg, args->seq,
            "", true, -1, buf);
        delete args;
        return;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
        WINHTTP_NO_HEADER_INDEX);

    // 循环读
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
        if (avail == 0) break;

        std::string buf(avail, 0);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, &buf[0], avail, &read)) break;
        if (read == 0) break;
        buf.resize(read);

        PostStreamChunk(args->notifyWnd, args->notifyMsg, args->seq,
            std::move(buf), false, (int)statusCode, "");
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    PostStreamChunk(args->notifyWnd, args->notifyMsg, args->seq,
        "", true, (int)statusCode, "");

    delete args;
}

} // namespace

// JSPlugin 层入口：分发 chunk / 结束通知到对应插件的 JS 回调
extern "C" bool YXGui_ProcessStreamChunk(void* payload) {
    if (!payload) return false;
    auto* msg = (HttpStreamChunkMsg*)payload;

    StreamState st;
    {
        std::lock_guard<std::mutex> lock(g_streamStatesMutex);
        auto it = g_streamStates.find(msg->seq);
        if (it == g_streamStates.end()) return false;
        st = it->second;
    }

    JSContext* ctx = st.ctx;
    if (!ctx) return false;

    JSValue onChunk;
    memcpy(&onChunk, st.onChunkBits, sizeof(JSValue));

    // 每次（含最后），只要 chunk 非空或 isDone 都调用一次
    if (!msg->chunk.empty() || msg->isDone) {
        JSValue arg = JS_NewStringLen(ctx, msg->chunk.c_str(), msg->chunk.size());
        JSValue ret = JS_Call(ctx, onChunk, JS_UNDEFINED, 1, &arg);
        JS_FreeValue(ctx, arg);
        if (JS_IsException(ret)) {
            JSValue exc = JS_GetException(ctx);
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, ret);
    }

    if (msg->isDone) {
        JSValue resolve;
        memcpy(&resolve, st.resolveBits, sizeof(JSValue));

        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "ok",
            msg->status >= 0 && msg->error.empty() ? JS_TRUE : JS_FALSE);
        JS_SetPropertyStr(ctx, obj, "status", JS_NewInt32(ctx, msg->status));
        if (!msg->error.empty()) {
            JS_SetPropertyStr(ctx, obj, "error",
                JS_NewStringLen(ctx, msg->error.c_str(), msg->error.size()));
        }
        JSValue ret = JS_Call(ctx, resolve, JS_UNDEFINED, 1, &obj);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, obj);

        JS_FreeValue(ctx, resolve);
        JS_FreeValue(ctx, onChunk);

        {
            std::lock_guard<std::mutex> lock(g_streamStatesMutex);
            g_streamStates.erase(msg->seq);
        }
    }

    // 推动 Promise 链
    {
        JSRuntime* rt = JS_GetRuntime(ctx);
        for (;;) {
            JSContext* jobCtx = nullptr;
            int r = JS_ExecutePendingJob(rt, &jobCtx);
            if (r <= 0) break;
        }
    }

    delete msg;
    return true;
}

extern "C" void YXGui_FreeStreamChunk(void* payload) {
    if (!payload) return;
    delete (HttpStreamChunkMsg*)payload;
}

// win32.httpRequestStream(opts, onChunk) -> Promise<{ok,status,error?}>
static JSValue js_win32_httpRequestStream(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 2 || !JS_IsObject(argv[0]) || !JS_IsFunction(ctx, argv[1])) {
        return JS_NULL;
    }

    JSValue vUrl = JS_GetPropertyStr(ctx, argv[0], "url");
    JSValue vMethod = JS_GetPropertyStr(ctx, argv[0], "method");
    JSValue vBody = JS_GetPropertyStr(ctx, argv[0], "body");
    JSValue vTimeout = JS_GetPropertyStr(ctx, argv[0], "timeoutMs");
    JSValue vHeaders = JS_GetPropertyStr(ctx, argv[0], "headers");

    std::string url, method = "POST", body;
    if (JS_IsString(vUrl)) { const char* s = JS_ToCString(ctx, vUrl); if (s) { url = s; JS_FreeCString(ctx, s); } }
    if (JS_IsString(vMethod)) { const char* s = JS_ToCString(ctx, vMethod); if (s) { method = s; JS_FreeCString(ctx, s); } }
    if (JS_IsString(vBody)) { const char* s = JS_ToCString(ctx, vBody); if (s) { body = s; JS_FreeCString(ctx, s); } }
    int timeoutMs = 0;
    if (JS_IsNumber(vTimeout)) JS_ToInt32(ctx, &timeoutMs, vTimeout);

    std::string headers;
    if (JS_IsObject(vHeaders)) {
        JSPropertyEnum* tab = nullptr;
        uint32_t len = 0;
        if (JS_GetOwnPropertyNames(ctx, &tab, &len, vHeaders,
            JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < len; i++) {
                const char* k = JS_AtomToCString(ctx, tab[i].atom);
                JSValue v = JS_GetProperty(ctx, vHeaders, tab[i].atom);
                const char* vs = JS_ToCString(ctx, v);
                if (k && vs) { headers += k; headers += ": "; headers += vs; headers += "\r\n"; }
                if (vs) JS_FreeCString(ctx, vs);
                JS_FreeValue(ctx, v);
                if (k) JS_FreeCString(ctx, k);
            }
            JS_FreePropertyEnum(ctx, tab, len);
        }
    }
    JS_FreeValue(ctx, vUrl);
    JS_FreeValue(ctx, vMethod);
    JS_FreeValue(ctx, vBody);
    JS_FreeValue(ctx, vTimeout);
    JS_FreeValue(ctx, vHeaders);

    if (url.empty()) return JS_NULL;

    // Promise
    JSValue resolving[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        return JS_EXCEPTION;
    }

    static std::atomic<uint32_t> g_streamSeq{ 0x40000000u };
    uint32_t seq = g_streamSeq.fetch_add(1);

    // 保护 onChunk / resolve 不被 GC 回收
    JS_DupValue(ctx, argv[1]);
    JS_DupValue(ctx, resolving[0]);

    StreamState st;
    st.plugin = self;
    st.ctx = ctx;
    static_assert(sizeof(JSValue) <= sizeof(st.onChunkBits), "JSValue too large");
    memcpy(st.onChunkBits, &argv[1], sizeof(JSValue));
    memcpy(st.resolveBits, &resolving[0], sizeof(JSValue));

    {
        std::lock_guard<std::mutex> lock(g_streamStatesMutex);
        g_streamStates[seq] = st;
    }

    JS_FreeValue(ctx, resolving[1]);

    auto* args = new HttpStreamArgs;
    args->url = url;
    args->method = method;
    args->headers = headers;
    args->body = body;
    args->timeoutMs = timeoutMs;
    args->seq = seq;
    args->notifyWnd = self->GetOwnerWnd();
    args->notifyMsg = WM_YXGUI_HTTP_STREAM_LOCAL;

    std::thread(HttpStreamWorker, args).detach();
    return promise;
}

// win32.readFileBase64(path) -> string（读文件并 base64 编码）
static JSValue js_win32_readFileBase64(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NewString(ctx, "");
    std::wstring path = JSValueToWString(ctx, argv[0]);
    if (path.empty()) return JS_NewString(ctx, "");

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return JS_NewString(ctx, "");
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size <= 0) return JS_NewString(ctx, "");
    f.seekg(0, std::ios::beg);

    std::vector<char> data((size_t)size);
    f.read(data.data(), size);
    f.close();

    std::string b64 = Base64Encode(data.data(), data.size());
    return JS_NewStringLen(ctx, b64.c_str(), b64.size());
}

// ============================================================
// v3 扩展 API：文件系统 / 浏览器 / 缓存目录 / 壁纸 / 控件颜色
// ============================================================

// win32.readTextFile(path) → string（UTF-8 文本；读 JSON/配置用）
static JSValue js_win32_readTextFile(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NewString(ctx, "");
    std::wstring path = JSValueToWString(ctx, argv[0]);
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return JS_NewString(ctx, "");
    std::string data((std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    f.close();
    return JS_NewStringLen(ctx, data.c_str(), (size_t)data.size());
}

// win32.writeTextFile(path, text) → bool（自动创建父目录）
static JSValue js_win32_writeTextFile(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    if (argc < 2) return JS_FALSE;
    std::wstring path = JSValueToWString(ctx, argv[0]);
    const char* cs = JS_ToCString(ctx, argv[1]);
    if (!cs) return JS_FALSE;
    std::string text(cs);
    JS_FreeCString(ctx, cs);

    // 自动创建父目录
    fs::path p(path);
    fs::path parent = p.has_parent_path() ? p.parent_path() : fs::path();
    std::error_code ec;
    if (!parent.empty()) fs::create_directories(parent, ec);

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return JS_FALSE;
    f.write(text.data(), (std::streamsize)text.size());
    f.close();
    return JS_TRUE;
}

// win32.listDir(path) → string[]（条目名，含目录标记后缀 "/"）
static JSValue js_win32_listDir(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NewArray(ctx);
    std::wstring dir = JSValueToWString(ctx, argv[0]);
    JSValue arr = JS_NewArray(ctx);
    uint32_t idx = 0;
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return arr;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        bool isDir = !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        if (isDir) name += L"/";
        JS_SetPropertyUint32(ctx, arr, idx++,
            JS_NewString(ctx, WideToUtf8(name).c_str()));
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return arr;
}

// win32.moveFile(from, to) → bool
static JSValue js_win32_moveFile(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    if (argc < 2) return JS_FALSE;
    std::wstring from = JSValueToWString(ctx, argv[0]);
    std::wstring to = JSValueToWString(ctx, argv[1]);
    if (from.empty() || to.empty()) return JS_FALSE;
    if (MoveFileExW(from.c_str(), to.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return JS_TRUE;
    }
    return JS_FALSE;
}

// win32.deleteFile(path) → bool
static JSValue js_win32_deleteFile(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    std::wstring path = JSValueToWString(ctx, argv[0]);
    return (DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND)
        ? JS_TRUE : JS_FALSE;
}

// win32.openBrowser(url) → bool
static JSValue js_win32_openBrowser(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    std::wstring url = JSValueToWString(ctx, argv[0]);
    if (url.empty()) return JS_FALSE;
    HINSTANCE r = ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)r > 32) ? JS_TRUE : JS_FALSE;
}

// win32.getCacheDir() → string（宿主 exe 所在目录 + \Cache，兼容 \Cache\gui\*.m 等一切缓存）
static JSValue js_win32_getCacheDir(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    wchar_t exe[1024] = { 0 };
    GetModuleFileNameW(NULL, exe, 1024);
    std::wstring cacheDir = std::wstring(exe);
    size_t pos = cacheDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) cacheDir = cacheDir.substr(0, pos);
    cacheDir += L"\\Cache";
    return WStringToJSValue(ctx, cacheDir);
}


// ============================================================
// WebView2 桥接：导航 / Cookie / 页面脚本
// ============================================================
// 说明：
// JSPlugin 本身不直接持有 WebView2 COM 对象，因此通过 owner window
// 投递消息给 YXGuiPlugin；真正的 WebView2 操作发生在宿主 UI 线程。
// webViewGetCookies / webViewExecuteScript 复用现有 pending Promise
// 通道，由 YXGuiPlugin 完成后调用 JSPlugin::CallHttpDone(seq, result)。
// ============================================================
static std::atomic<uint32_t> g_jsWebViewSeq{ 0x70000000u };

static JSValue MakeWebViewPromise(JSPlugin* self, JSContext* ctx, uint32_t& seq) {
    JSValue resolving[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        return JS_EXCEPTION;
    }

    seq = g_jsWebViewSeq.fetch_add(1);
    uint64_t bits[2] = { 0, 0 };
    static_assert(sizeof(JSValue) <= sizeof(bits), "JSValue 过大");
    memcpy(bits, &resolving[0], sizeof(JSValue));
    self->StorePendingHttp(seq, bits[0], bits[1]);

    // WebView2 桥统一使用 resolve({ok,status,body/error})，与 httpRequest 一致。
    JS_FreeValue(ctx, resolving[1]);
    return promise;
}

static void FailWebViewPromise(JSPlugin* self, uint32_t seq, const std::string& error) {
    if (!self) return;
    // Use the already-public completion envelope so this free helper does not
    // depend on the nested HttpResult type being visible in this scope.
    JSPlugin::HttpTaskDoneMsg msg{};
    msg.seq = seq;
    msg.result.status = -1;
    msg.result.error = error;
    self->CallHttpDone(seq, msg.result);
}

// win32.webViewNavigate(url, newTab?) → bool
static JSValue js_win32_webViewNavigate(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_FALSE;

    std::wstring url = JSValueToWString(ctx, argv[0]);
    if (url.empty()) return JS_FALSE;

    bool newTab = false;
    if (argc >= 2) newTab = JS_ToBool(ctx, argv[1]) != 0;

    HWND owner = self->GetOwnerWnd();
    if (!owner || !IsWindow(owner)) return JS_FALSE;

    auto* req = new JSWebViewRequest();
    req->plugin = self;
    req->op = JSWebViewOp::Navigate;
    req->newTab = newTab;
    req->url = url;

    if (!PostMessageW(owner, WM_YXGUI_JS_WEBVIEW, 0, (LPARAM)req)) {
        delete req;
        return JS_FALSE;
    }

    return JS_TRUE;
}

// win32.webViewGetCookies(url) → Promise<string>
// body = "name=value; name2=value2"
static JSValue js_win32_webViewGetCookies(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_NULL;

    std::wstring url = JSValueToWString(ctx, argv[0]);
    if (url.empty()) return JS_NULL;

    if (!self->RequestPermission(L"读取当前 WebView2 的网站登录 Cookie")) {
        return JS_FALSE;
    }

    uint32_t seq = 0;
    JSValue promise = MakeWebViewPromise(self, ctx, seq);
    if (JS_IsException(promise)) return promise;

    HWND owner = self->GetOwnerWnd();
    if (!owner || !IsWindow(owner)) {
        FailWebViewPromise(self, seq, "宿主 WebView2 窗口不可用");
        return promise;
    }

    auto* req = new JSWebViewRequest();
    req->plugin = self;
    req->seq = seq;
    req->op = JSWebViewOp::GetCookies;
    req->url = url;

    if (!PostMessageW(owner, WM_YXGUI_JS_WEBVIEW, 0, (LPARAM)req)) {
        delete req;
        FailWebViewPromise(self, seq, "无法投递 WebView2 Cookie 请求");
    }

    return promise;
}

// win32.webViewExecuteScript(script) → Promise<string>
// body = WebView2 ExecuteScript 的 resultObjectAsJson（字符串形式）。
static JSValue js_win32_webViewExecuteScript(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_NULL;

    std::wstring script = JSValueToWString(ctx, argv[0]);
    if (script.empty()) return JS_NULL;

    if (!self->RequestPermission(L"在当前 WebView2 页面执行 JavaScript")) {
        return JS_FALSE;
    }

    uint32_t seq = 0;
    JSValue promise = MakeWebViewPromise(self, ctx, seq);
    if (JS_IsException(promise)) return promise;

    HWND owner = self->GetOwnerWnd();
    if (!owner || !IsWindow(owner)) {
        FailWebViewPromise(self, seq, "宿主 WebView2 窗口不可用");
        return promise;
    }

    auto* req = new JSWebViewRequest();
    req->plugin = self;
    req->seq = seq;
    req->op = JSWebViewOp::ExecuteScript;
    req->script = script;

    if (!PostMessageW(owner, WM_YXGUI_JS_WEBVIEW, 0, (LPARAM)req)) {
        delete req;
        FailWebViewPromise(self, seq, "无法投递 WebView2 Script 请求");
    }

    return promise;
}

// win32.setBackground({mode,color,image,cycleDir,cycleMs}) → bool
// mode: "none" | "solid" | "image" | "cycle"
static JSValue js_win32_setBackground(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1 || !JS_IsObject(argv[0])) return JS_FALSE;

    std::string mode = "none";
    std::string colorHex = "";
    std::wstring image, cycleDir;
    int cycleMs = 3000;

    auto getStr = [&](const char* key, std::string* out) {
        JSValue v = JS_GetPropertyStr(ctx, argv[0], key);
        if (JS_IsString(v)) {
            const char* cs = JS_ToCString(ctx, v);
            if (cs) { *out = cs; JS_FreeCString(ctx, cs); }
        }
        JS_FreeValue(ctx, v);
    };
    getStr("mode", &mode);
    getStr("color", &colorHex);

    JSValue vImg = JS_GetPropertyStr(ctx, argv[0], "image");
    if (JS_IsString(vImg)) image = JSValueToWString(ctx, vImg);
    JS_FreeValue(ctx, vImg);
    JSValue vDir = JS_GetPropertyStr(ctx, argv[0], "cycleDir");
    if (JS_IsString(vDir)) cycleDir = JSValueToWString(ctx, vDir);
    JS_FreeValue(ctx, vDir);
    JSValue vMs = JS_GetPropertyStr(ctx, argv[0], "cycleMs");
    if (JS_IsNumber(vMs)) JS_ToInt32(ctx, &cycleMs, vMs);
    JS_FreeValue(ctx, vMs);

    bool ok = self->ApplyWallpaper(mode, colorHex, image, cycleDir, cycleMs);
    if (ok && self->GetHostContainer()) {
        InvalidateRect(self->GetHostContainer(), NULL, TRUE);
    }
    return ok ? JS_TRUE : JS_FALSE;
}

// win32.setControlColor(id, {bg,fg,border}) → bool（#RRGGBB 或 null）
static JSValue js_win32_setControlColor(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 2 || !JS_IsObject(argv[1])) return JS_FALSE;

    std::wstring key = JSValueToWString(ctx, argv[0]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end()) return JS_FALSE;
    HWND h = it->second;
    if (!h || !IsWindow(h)) return JS_FALSE;

    CtrlColor cc;
    // 解析可选字符串属性（"#RRGGBB" 或 null/undefined -> 保持 -1）
    auto getColorProp = [&](const char* prop, int& out) {
        JSValue v = JS_GetPropertyStr(ctx, argv[1], prop);
        if (JS_IsString(v)) {
            const char* cs = JS_ToCString(ctx, v);
            if (cs) {
                COLORREF c;
                if (ParseColorHex(cs, c)) out = (int)c;
                JS_FreeCString(ctx, cs);
            }
        }
        JS_FreeValue(ctx, v);
    };
getColorProp("bg", cc.bg);
    getColorProp("fg", cc.fg);
    getColorProp("border", cc.border);

    // active：激活/选中标记（布尔或 0/1 数字）
    JSValue vActive = JS_GetPropertyStr(ctx, argv[1], "active");
    if (JS_IsBool(vActive)) {
        cc.active = JS_ToBool(ctx, vActive) ? 1 : 0;
    } else if (JS_IsNumber(vActive)) {
        int a = 0;
        JS_ToInt32(ctx, &a, vActive);
        cc.active = a ? 1 : 0;
    }
    JS_FreeValue(ctx, vActive);

    self->SetCtrlColor(h, cc);
    // 立即重绘：非自绘控件强制刷新（文字/背景色由宿主 CTLCOLOR 生效）
    RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    return JS_TRUE;
}

// ============================================================
// v9 新增 API：图片 / 下拉框
// ============================================================

// win32.setImage(id, path) —— 给 imagebox 控件设置图片路径
static JSValue js_win32_setImage(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 2) return JS_FALSE;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    std::wstring path = JSValueToWString(ctx, argv[1]);

    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end()) return JS_FALSE;
    HWND h = it->second;
    if (!h || !IsWindow(h)) return JS_FALSE;

    {
        std::lock_guard<std::mutex> lock(g_imagePathsMutex);
        if (path.empty()) g_imagePaths.erase(h);
        else g_imagePaths[h] = path;
    }
    InvalidateRect(h, NULL, TRUE);
    return JS_TRUE;
}

// win32.comboAddItem(id, text) -> bool
static JSValue js_win32_comboAddItem(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 2) return JS_FALSE;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    std::wstring text = JSValueToWString(ctx, argv[1]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end() || !IsWindow(it->second)) return JS_FALSE;
    LRESULT r = SendMessageW(it->second, CB_ADDSTRING, 0, (LPARAM)text.c_str());
    return (r == CB_ERR || r == CB_ERRSPACE) ? JS_FALSE : JS_TRUE;
}

// win32.comboClear(id) -> bool
static JSValue js_win32_comboClear(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_FALSE;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end() || !IsWindow(it->second)) return JS_FALSE;
    SendMessageW(it->second, CB_RESETCONTENT, 0, 0);
    return JS_TRUE;
}

// win32.comboGetSel(id) -> int（-1 表示无选择）
static JSValue js_win32_comboGetSel(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_NewInt32(ctx, -1);
    std::wstring key = JSValueToWString(ctx, argv[0]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end() || !IsWindow(it->second)) return JS_NewInt32(ctx, -1);
    LRESULT r = SendMessageW(it->second, CB_GETCURSEL, 0, 0);
    return JS_NewInt32(ctx, (int)r);
}

// win32.comboSetSel(id, idx) -> bool
static JSValue js_win32_comboSetSel(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 2) return JS_FALSE;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    int idx = 0;
    JS_ToInt32(ctx, &idx, argv[1]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end() || !IsWindow(it->second)) return JS_FALSE;
    SendMessageW(it->second, CB_SETCURSEL, (WPARAM)idx, 0);
    return JS_TRUE;
}

// win32.comboGetText(id) -> string（当前编辑区文本 / 选中项文本）
static JSValue js_win32_comboGetText(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_NewString(ctx, "");
    std::wstring key = JSValueToWString(ctx, argv[0]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end() || !IsWindow(it->second)) return JS_NewString(ctx, "");

    HWND h = it->second;

    // CBS_DROPDOWN: 编辑区文本优先
    COMBOBOXINFO cbi = { sizeof(cbi) };
    if (GetComboBoxInfo(h, &cbi) && cbi.hwndItem) {
        int len = GetWindowTextLengthW(cbi.hwndItem);
        if (len > 0) {
            std::wstring buf(len, 0);
            GetWindowTextW(cbi.hwndItem, &buf[0], len + 1);
            return WStringToJSValue(ctx, buf);
        }
    }
    // 回退到选中项
    LRESULT sel = SendMessageW(h, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR) {
        int len = (int)SendMessageW(h, CB_GETLBTEXTLEN, (WPARAM)sel, 0);
        if (len > 0) {
            std::wstring buf(len, 0);
            SendMessageW(h, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)&buf[0]);
            return WStringToJSValue(ctx, buf);
        }
    }
    return JS_NewString(ctx, "");
}

// ============================================================
// v9.1: 文件拖放支持
// ============================================================
static std::vector<std::wstring> g_droppedFiles;
static std::mutex g_droppedMutex;

// C 风格入口：由 YXGuiPlugin.cpp 的 WM_DROPFILES 处理调用
void YXGui_PushDroppedFiles(JSPlugin* plugin,
    const wchar_t* const* paths, int count) {
    if (!plugin) return;
    {
        std::lock_guard<std::mutex> lock(g_droppedMutex);
        g_droppedFiles.clear();
        if (paths && count > 0) {
            g_droppedFiles.reserve((size_t)count);
            for (int i = 0; i < count; ++i) {
                if (paths[i]) g_droppedFiles.push_back(paths[i]);
            }
        }
    }
    // 通过特殊 controlId 触发 JS 侧 onControlEvent
    plugin->CallOnControlEvent(L"__file_drop__", 0);
}

// win32.getDroppedFiles() -> string[]
static JSValue js_win32_getDroppedFiles(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    std::lock_guard<std::mutex> lock(g_droppedMutex);
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < g_droppedFiles.size(); ++i) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i,
            WStringToJSValue(ctx, g_droppedFiles[i]));
    }
    return arr;
}

// ============================================================
// v9.5: 简单模态对话框（alert / confirm / inputBox）
// ============================================================
namespace {

struct SimpleDialogData {
    std::wstring title;
    std::wstring label;
    std::wstring text;
    bool multiline = false;
    int  type = 0;          // 0=alert, 1=confirm, 2=input
    bool ok = false;
    std::wstring result;
};

LRESULT CALLBACK SimpleDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SimpleDialogData* p = (SimpleDialogData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        p = (SimpleDialogData*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)p);
        if (!p) return -1;

        HFONT hFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        int y = 16;
        if (!p->label.empty()) {
            HWND hLbl = CreateWindowExW(0, L"STATIC", p->label.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL,
                20, y, w - 40, 24, hWnd, NULL, NULL, NULL);
            SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += 28;
        }

        int bottomH = 52;
        int editH = h - y - bottomH - 8;
        if (editH < 30) editH = 30;

        if (p->type == 0 || p->type == 1) {
            HWND hTxt = CreateWindowExW(0, L"STATIC", p->text.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL,
                20, y, w - 40, editH, hWnd, NULL, NULL, NULL);
            SendMessageW(hTxt, WM_SETFONT, (WPARAM)hFont, TRUE);
        } else {
            DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
            if (p->multiline)
                style |= ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN;
            HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", p->text.c_str(),
                style, 20, y, w - 40,
                p->multiline ? editH : 32,
                hWnd, (HMENU)100, NULL, NULL);
            SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageW(hEdit, EM_SETSEL, 0, -1);
        }

        int by = h - 44;
        if (p->type == 0) {
            HWND hOk = CreateWindowExW(0, L"BUTTON", L"确定",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                w - 110, by, 90, 32, hWnd, (HMENU)IDOK, NULL, NULL);
            SendMessageW(hOk, WM_SETFONT, (WPARAM)hFont, TRUE);
            SetFocus(hOk);
        } else {
            HWND hOk = CreateWindowExW(0, L"BUTTON", L"确定",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                w - 210, by, 90, 32, hWnd, (HMENU)IDOK, NULL, NULL);
            HWND hCa = CreateWindowExW(0, L"BUTTON", L"取消",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                w - 110, by, 90, 32, hWnd, (HMENU)IDCANCEL, NULL, NULL);
            SendMessageW(hOk, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageW(hCa, WM_SETFONT, (WPARAM)hFont, TRUE);
            SetFocus(hOk);
        }
        return 0;
    }
    case WM_COMMAND: {
        if (!p) break;
        int id = LOWORD(wParam);
        if (id == IDOK) {
            if (p->type == 2) {
                int len = GetWindowTextLengthW(GetDlgItem(hWnd, 100));
                if (len < 0) len = 0;
                std::wstring buf(len + 1, 0);
                GetDlgItemTextW(hWnd, 100, &buf[0], len + 1);
                buf.resize(len);
                p->result = buf;
            }
            p->ok = true;
            DestroyWindow(hWnd);
        } else if (id == IDCANCEL) {
            DestroyWindow(hWnd);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// kind: 0=alert, 1=confirm, 2=input
// 返回 1 表示用户点确定（input 时 out 有值），0 表示取消
int ShowSimpleDialog(HWND owner, int kind, const std::wstring& title,
    const std::wstring& label, const std::wstring& initial,
    bool multiline, std::wstring& out) {

    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = SimpleDialogProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(248, 250, 253));
        wc.lpszClassName = L"YXGuiSimpleDialog";
        RegisterClassExW(&wc);
        s_reg = true;
    }

    int w = 480, h = 220;
    if (kind == 0)      { w = 500; h = 220; }
    else if (kind == 1) { w = 500; h = 240; }
    else {
        w = multiline ? 660 : 500;
        h = multiline ? 440 : (label.empty() ? 180 : 210);
    }

    int x = 0, y = 0;
    if (owner && IsWindow(owner)) {
        RECT rc;
        GetWindowRect(owner, &rc);
        x = rc.left + ((rc.right - rc.left) - w) / 2;
        y = rc.top + ((rc.bottom - rc.top) - h) / 2;
    } else {
        x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    }

    SimpleDialogData data;
    data.title = title;
    data.label = label;
    data.text = initial;
    data.multiline = multiline;
    data.type = kind;
    data.ok = false;

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"YXGuiSimpleDialog", title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, owner, NULL, GetModuleHandle(NULL), &data);
    if (!hDlg) return 0;

    ShowWindow(hDlg, SW_SHOW);
    SetForegroundWindow(hDlg);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (data.ok) { out = data.result; return 1; }
    return 0;
}

} // namespace

// ============================================================
// v9.4: 富文本链接范围表（供 EN_LINK 反查 tag）
// ============================================================
struct RichLinkRange {
    LONG start = 0;
    LONG end = 0;
    std::wstring tag;
};

static std::map<HWND, std::vector<RichLinkRange>> g_richLinks;
static std::mutex g_richLinksMutex;

static std::wstring g_pendingLinkTag;
static std::mutex g_pendingLinkMtx;

extern "C" void YXGui_ClearRichLinks(HWND h) {
    if (!h) return;
    std::lock_guard<std::mutex> lock(g_richLinksMutex);
    g_richLinks.erase(h);
}

extern "C" std::wstring YXGui_QueryRichLinkTag(HWND h, LONG cpMin, LONG cpMax) {
    if (!h) return L"";
    std::lock_guard<std::mutex> lock(g_richLinksMutex);
    auto it = g_richLinks.find(h);
    if (it == g_richLinks.end()) return L"";
    for (auto& r : it->second) {
        if (cpMin >= r.start && cpMax <= r.end) return r.tag;
    }
    return L"";
}

extern "C" void YXGui_SetPendingLinkTag(const std::wstring& t) {
    std::lock_guard<std::mutex> lock(g_pendingLinkMtx);
    g_pendingLinkTag = t;
}

// ============================================================
// 原生插件（ApiAiNative）桥接：富文本链接登记 / 待处理 tag 读取
// ============================================================
void JSPlugin::RegisterRichLink(HWND hEdit, LONG startPos, LONG endPos,
    const std::wstring& tag) {
    if (!hEdit || !IsWindow(hEdit)) return;
    {
        std::lock_guard<std::mutex> lock(g_richLinksMutex);
        RichLinkRange r;
        r.start = startPos;
        r.end = endPos;
        r.tag = tag;
        g_richLinks[hEdit].push_back(r);
    }
    // 启用 ENM_LINK 事件（与 JS richAppendLink 一致）
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_LINK);
}

std::wstring JSPlugin::ConsumePendingLinkTag() {
    std::wstring t;
    {
        std::lock_guard<std::mutex> lock(g_pendingLinkMtx);
        t = g_pendingLinkTag;
        g_pendingLinkTag.clear();
    }
    return t;
}

// ============================================================
// v9.3: richoutput 富文本 API
// ============================================================
extern "C" void YXGui_ShowOverlayWebView(void* self, void* plugin, const wchar_t* url);
extern "C" void YXGui_HideOverlayWebView(void* self, void* plugin);

static COLORREF ParseHexToColorref(const std::wstring& s, COLORREF def) {
    if (s.length() < 7 || s[0] != L'#') return def;
    auto hv = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    int r = hv(s[1]) * 16 + hv(s[2]);
    int g = hv(s[3]) * 16 + hv(s[4]);
    int b = hv(s[5]) * 16 + hv(s[6]);
    if (r < 0 || g < 0 || b < 0) return def;
    return RGB(r, g, b);
}

static JSValue js_win32_richClear(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_FALSE;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end() || !IsWindow(it->second)) return JS_FALSE;
    SetWindowTextW(it->second, L"");
    // v9.4: 清空对应链接表
    extern void YXGui_ClearRichLinks(HWND);
    YXGui_ClearRichLinks(it->second);
    return JS_TRUE;
}

static JSValue js_win32_richAppend(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 3) return JS_FALSE;
    std::wstring key  = JSValueToWString(ctx, argv[0]);
    std::wstring kind = JSValueToWString(ctx, argv[1]);
    std::wstring text = JSValueToWString(ctx, argv[2]);

    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end() || !IsWindow(it->second)) return JS_FALSE;
    HWND h = it->second;

    SendMessageW(h, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);

    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC |
                CFM_COLOR | CFM_CHARSET;
    cf.bCharSet = DEFAULT_CHARSET;
    cf.bPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy_s(cf.szFaceName, L"Microsoft YaHei");
    cf.yHeight = 210;
    cf.crTextColor = RGB(15, 23, 42);

    PARAFORMAT2 pf;
    memset(&pf, 0, sizeof(pf));
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_STARTINDENT | PFM_SPACEAFTER | PFM_LINESPACING;
    pf.dySpaceAfter = 60;
    pf.bLineSpacingRule = 0;

    bool isCodeBlock = false;
    bool isHr = false;

    if (kind == L"h1") {
        cf.yHeight = 400;
        cf.dwEffects = CFE_BOLD;
        pf.dySpaceBefore = 160;
        pf.dySpaceAfter = 100;
        pf.dwMask |= PFM_SPACEBEFORE;
    } else if (kind == L"h2") {
        cf.yHeight = 320;
        cf.dwEffects = CFE_BOLD;
        pf.dySpaceBefore = 140;
        pf.dySpaceAfter = 80;
        pf.dwMask |= PFM_SPACEBEFORE;
    } else if (kind == L"h3") {
        cf.yHeight = 260;
        cf.dwEffects = CFE_BOLD;
        pf.dySpaceBefore = 120;
        pf.dySpaceAfter = 60;
        pf.dwMask |= PFM_SPACEBEFORE;
    } else if (kind == L"code") {
        wcscpy_s(cf.szFaceName, L"Consolas");
        cf.yHeight = 200;
        cf.crTextColor = RGB(30, 41, 59);
        cf.dwMask |= CFM_BACKCOLOR;
        cf.crBackColor = RGB(241, 245, 249);
        pf.dxStartIndent = 240;
        pf.dxOffset = -240;
        pf.dwMask |= PFM_OFFSET;
        pf.wNumbering = 0;
        isCodeBlock = true;
    } else if (kind == L"bullet") {
        cf.yHeight = 210;
        pf.dxStartIndent = 360;
        pf.dxOffset = -280;
        pf.dwMask |= PFM_OFFSET;
        pf.wNumbering = 0;
    } else if (kind == L"quote") {
        cf.yHeight = 210;
        cf.dwEffects = CFE_ITALIC;
        cf.crTextColor = RGB(71, 85, 105);
        pf.dxStartIndent = 300;
    } else if (kind == L"bold") {
        cf.dwEffects = CFE_BOLD;
    } else if (kind == L"italic") {
        cf.dwEffects = CFE_ITALIC;
    } else if (kind == L"hr") {
        isHr = true;
        cf.crTextColor = RGB(203, 213, 225);
        pf.dySpaceBefore = 80;
        pf.dySpaceAfter = 80;
        pf.dwMask |= PFM_SPACEBEFORE;
    } else {
        cf.dwEffects = 0;
    }

    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(h, EM_SETPARAFORMAT, 0, (LPARAM)&pf);

    std::wstring out = text;
    if (isHr) {
        out = L"────────────────────────────────────";
        out += L"\r\n";
    } else {
        out += L"\r\n";
    }

    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)out.c_str());
    return JS_TRUE;
}

static JSValue js_win32_richScrollToEnd(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_FALSE;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end() || !IsWindow(it->second)) return JS_FALSE;
    HWND h = it->second;
    int len = GetWindowTextLengthW(h);
    SendMessageW(h, EM_SETSEL, len, len);
    SendMessageW(h, EM_SCROLLCARET, 0, 0);
    return JS_TRUE;
}

// v9.4: 单行内联段（粗体/斜体/行内代码）——用当前字符格式追加文本，不换行
static JSValue js_win32_richAppendSpan(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 3) return JS_FALSE;
    std::wstring key  = JSValueToWString(ctx, argv[0]);
    std::wstring text = JSValueToWString(ctx, argv[1]);

    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end() || !IsWindow(it->second)) return JS_FALSE;
    HWND h = it->second;

    // 读取 flags 对象
    bool bold = false, italic = false, code = false;
    std::wstring color;
    if (argc >= 3 && JS_IsObject(argv[2])) {
        JSValue vb = JS_GetPropertyStr(ctx, argv[2], "bold");
        JSValue vi = JS_GetPropertyStr(ctx, argv[2], "italic");
        JSValue vc = JS_GetPropertyStr(ctx, argv[2], "code");
        JSValue vcol = JS_GetPropertyStr(ctx, argv[2], "color");
        bold = JS_ToBool(ctx, vb) != 0;
        italic = JS_ToBool(ctx, vi) != 0;
        code = JS_ToBool(ctx, vc) != 0;
        if (JS_IsString(vcol)) color = JSValueToWString(ctx, vcol);
        JS_FreeValue(ctx, vb);
        JS_FreeValue(ctx, vi);
        JS_FreeValue(ctx, vc);
        JS_FreeValue(ctx, vcol);
    }

    SendMessageW(h, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);

    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC |
                CFM_COLOR | CFM_CHARSET;
    cf.bCharSet = DEFAULT_CHARSET;
    wcscpy_s(cf.szFaceName, L"Microsoft YaHei");
    cf.yHeight = 210;
    cf.crTextColor = RGB(15, 23, 42);
    cf.dwEffects = 0;

    if (code) {
        wcscpy_s(cf.szFaceName, L"Consolas");
        cf.yHeight = 200;
        cf.crTextColor = RGB(30, 41, 59);
        cf.dwMask |= CFM_BACKCOLOR;
        cf.crBackColor = RGB(241, 245, 249);
    }
    if (bold)   cf.dwEffects |= CFE_BOLD;
    if (italic) cf.dwEffects |= CFE_ITALIC;
    if (!color.empty()) cf.crTextColor = ParseHexToColorref(color, cf.crTextColor);

    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());

    // 恢复背景：行内代码只在文本本身上加灰底，不影响后续
    if (code) {
        CHARFORMAT2W cfReset;
        memset(&cfReset, 0, sizeof(cfReset));
        cfReset.cbSize = sizeof(cfReset);
        cfReset.dwMask = CFM_BACKCOLOR;
        cfReset.crBackColor = RGB(255, 255, 255);
        SendMessageW(h, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfReset);
    }

    return JS_TRUE;
}

// v9.4: 带 tag 的链接段（EN_LINK 会触发）
static JSValue js_win32_richAppendLink(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 3) return JS_FALSE;
    std::wstring key  = JSValueToWString(ctx, argv[0]);
    std::wstring text = JSValueToWString(ctx, argv[1]);
    std::wstring tag  = JSValueToWString(ctx, argv[2]);

    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end() || !IsWindow(it->second)) return JS_FALSE;
    HWND h = it->second;

    SendMessageW(h, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);

    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_LINK | CFM_COLOR | CFM_CHARSET;
    cf.bCharSet = DEFAULT_CHARSET;
    wcscpy_s(cf.szFaceName, L"Microsoft YaHei");
    cf.yHeight = 210;
    cf.dwEffects = CFE_LINK | CFE_UNDERLINE;
    cf.crTextColor = RGB(29, 78, 216);

    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    LONG startPos = (LONG)GetWindowTextLengthW(h);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    LONG endPos = (LONG)GetWindowTextLengthW(h);

    // 恢复默认字符格式，避免后续文本也是链接
    CHARFORMAT2W cfReset;
    memset(&cfReset, 0, sizeof(cfReset));
    cfReset.cbSize = sizeof(cfReset);
    cfReset.dwMask = CFM_LINK | CFM_UNDERLINE;
    cfReset.dwEffects = 0;
    SendMessageW(h, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfReset);

    {
        std::lock_guard<std::mutex> lock(g_richLinksMutex);
        RichLinkRange r;
        r.start = startPos;
        r.end = endPos;
        r.tag = tag;
        g_richLinks[h].push_back(r);
    }

    // 启用 ENM_LINK 事件
    SendMessageW(h, EM_SETEVENTMASK, 0, ENM_LINK);

    return JS_TRUE;
}

// v9.4: 读取滚动条位置 {pos, min, max, page}
static JSValue js_win32_getScrollPos(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "pos",   JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "min",   JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "max",   JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "page",  JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "track", JS_NewInt32(ctx, 0));

    if (!self || argc < 1) return obj;
    std::wstring key = JSValueToWString(ctx, argv[0]);
    auto& map = self->MutableControls();
    auto it = map.find(self->GetId() + L"::" + key);
    if (it == map.end() || !IsWindow(it->second)) return obj;

    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (GetScrollInfo(it->second, SB_VERT, &si)) {
        JS_SetPropertyStr(ctx, obj, "pos",   JS_NewInt32(ctx, si.nPos));
        JS_SetPropertyStr(ctx, obj, "min",   JS_NewInt32(ctx, si.nMin));
        JS_SetPropertyStr(ctx, obj, "max",   JS_NewInt32(ctx, si.nMax));
        JS_SetPropertyStr(ctx, obj, "page",  JS_NewInt32(ctx, (int)si.nPage));
        JS_SetPropertyStr(ctx, obj, "track", JS_NewInt32(ctx, si.nTrackPos));
    }
    return obj;
}

// v9.4: 待处理链接 tag（EN_LINK 触发后由 YXGui 侧写入，JS 立即读走）
static JSValue js_win32_getPendingLinkTag(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    std::wstring t;
    {
        std::lock_guard<std::mutex> lock(g_pendingLinkMtx);
        t = g_pendingLinkTag;
        g_pendingLinkTag.clear();
    }
    return WStringToJSValue(ctx, t);
}

// v9.4: 系统剪贴板
static JSValue js_win32_setClipboard(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    std::wstring text = JSValueToWString(ctx, argv[0]);
    if (!OpenClipboard(GetActiveWindow())) return JS_FALSE;
    EmptyClipboard();
    size_t sz = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (hGlob) {
        void* p = GlobalLock(hGlob);
        if (p) {
            memcpy(p, text.c_str(), sz);
            GlobalUnlock(hGlob);
            SetClipboardData(CF_UNICODETEXT, hGlob);
        } else {
            GlobalFree(hGlob);
        }
    }
    CloseClipboard();
    return JS_TRUE;
}

// v9.4: 读剪贴板（供测试）
static JSValue js_win32_getClipboard(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    if (!OpenClipboard(GetActiveWindow())) return JS_NewString(ctx, "");
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    std::wstring out;
    if (h) {
        wchar_t* p = (wchar_t*)GlobalLock(h);
        if (p) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return WStringToJSValue(ctx, out);
}

// ============================================================
// v9.5: 简单模态对话框 JS 绑定
// ============================================================
static JSValue js_win32_alert(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    HWND owner = self ? self->GetOwnerWnd() : nullptr;
    std::wstring title = L"提示", msg;
    if (argc >= 1) msg = JSValueToWString(ctx, argv[0]);
    if (argc >= 2) title = JSValueToWString(ctx, argv[1]);
    std::wstring dummy;
    ShowSimpleDialog(owner, 0, title, L"", msg, false, dummy);
    return JS_UNDEFINED;
}

static JSValue js_win32_confirm(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    HWND owner = self ? self->GetOwnerWnd() : nullptr;
    std::wstring title = L"确认", msg;
    if (argc >= 1) msg = JSValueToWString(ctx, argv[0]);
    if (argc >= 2) title = JSValueToWString(ctx, argv[1]);
    std::wstring dummy;
    int r = ShowSimpleDialog(owner, 1, title, L"", msg, false, dummy);
    return JS_NewBool(ctx, r == 1);
}

static JSValue js_win32_inputBox(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    HWND owner = self ? self->GetOwnerWnd() : nullptr;
    std::wstring title = L"输入", label, initial;
    bool multiline = false;
    if (argc >= 1) title = JSValueToWString(ctx, argv[0]);
    if (argc >= 2) label = JSValueToWString(ctx, argv[1]);
    if (argc >= 3) initial = JSValueToWString(ctx, argv[2]);
    if (argc >= 4) multiline = JS_ToBool(ctx, argv[3]) != 0;
    std::wstring out;
    int r = ShowSimpleDialog(owner, 2, title, label, initial, multiline, out);
    if (r) return WStringToJSValue(ctx, out);
    return JS_NULL;
}

// ============================================================
// v9.3: 系统调色盘
// ============================================================
static JSValue js_win32_chooseColor(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    std::wstring init = L"#FFFFFF";
    if (argc >= 1) {
        std::wstring s = JSValueToWString(ctx, argv[0]);
        if (!s.empty()) init = s;
    }
    COLORREF c = ParseHexToColorref(init, RGB(255, 255, 255));

    static COLORREF custom[16] = { 0 };
    CHOOSECOLORW cc;
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = GetActiveWindow();
    cc.lpCustColors = custom;
    cc.rgbResult = c;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;

    if (ChooseColorW(&cc)) {
        wchar_t buf[16];
        swprintf_s(buf, L"#%02X%02X%02X",
            GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
        return WStringToJSValue(ctx, buf);
    }
    return JS_NewString(ctx, "");
}

// ============================================================
// v9.3: WebView2 覆盖层（登录）
// ============================================================
static JSValue js_win32_showWebViewOverlay(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self || argc < 1) return JS_FALSE;
    std::wstring url = JSValueToWString(ctx, argv[0]);
    if (url.empty()) return JS_FALSE;
    HWND owner = self->GetOwnerWnd();
    if (!owner) return JS_FALSE;
    void* gui = (void*)GetWindowLongPtrW(owner, GWLP_USERDATA);
    if (!gui) return JS_FALSE;
    YXGui_ShowOverlayWebView(gui, self, url.c_str());
    return JS_TRUE;
}

static JSValue js_win32_hideWebViewOverlay(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv) {
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self) return JS_FALSE;
    HWND owner = self->GetOwnerWnd();
    if (!owner) return JS_FALSE;
    void* gui = (void*)GetWindowLongPtrW(owner, GWLP_USERDATA);
    if (!gui) return JS_FALSE;
    YXGui_HideOverlayWebView(gui, self);
    return JS_TRUE;
}

// ============================================================
// 注册函数（被 JSPlugin::Load 调用）
// ============================================================

// ---- JSPlugin 扩展 API 绑定槽实现 ----
int JSPlugin::AllocApiSlot(const std::string& ns, const std::string& name,
    int kind, void* origFn) {
    ApiSlot s;
    s.ns = ns;
    s.name = name;
    s.kind = kind;
    s.origFn = origFn;
    m_apiSlots.push_back(s);
    return (int)m_apiSlots.size() - 1;
}

const JSPlugin::ApiSlot* JSPlugin::GetApiSlot(int magic) const {
    if (magic < 0 || magic >= (int)m_apiSlots.size()) return nullptr;
    return &m_apiSlots[magic];
}

std::vector<std::string> JSPlugin::GetExtJSApiNames(const std::string& ns) const {
    std::vector<std::string> out;
    if (m_extMgr) out = m_extMgr->GetJSApiNames(ns);
    if (g_guiDllHost) {
        auto g = g_guiDllHost->GetJSApiNames(ns);
        for (const auto& n : g) {
            if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        }
    }
    return out;
}

std::vector<std::string> JSPlugin::GetExtNamespaces() const {
    std::vector<std::string> out;
    if (m_extMgr) out = m_extMgr->GetNamespaces();
    if (g_guiDllHost) {
        auto g = g_guiDllHost->GetNamespaces();
        for (const auto& n : g) {
            if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        }
    }
    // 过滤内置命名空间，仅保留自定义（如 mc）
    out.erase(std::remove_if(out.begin(), out.end(), [](const std::string& n) {
        return n.empty() || n == "host" || n == "win32" || n == "light";
    }), out.end());
    return out;
}

// ---- JS 值 <-> YXG_JSValue 转换（布局约定见 ExtensionManager ExtValueOf/ExtSetValue） ----
static void SetExtValue(YXG_JSValue* v, int tag, uint64_t val) {
    v->bits[0] = ((uint64_t)(tag & 0xFF)) | (val << 8);
    v->bits[1] = val >> 56;
}

static uint64_t ExtValueOf(const YXG_JSValue* v) {
    return (v->bits[0] >> 8) | (v->bits[1] << 56);
}

// 把 JS 值转成扩展可见的 YXG_JSValue；字符串指针压入 owned（调用结束统一 JS_FreeCString）
static YXG_JSValue ExtFromJs(JSContext* ctx, JSValueConst v,
    std::vector<const char*>& owned) {
    YXG_JSValue out;
    out.bits[0] = YXG_JS_TAG_UNDEFINED;
    out.bits[1] = 0;
    if (JS_IsUndefined(v)) return out;
    if (JS_IsNull(v)) { SetExtValue(&out, YXG_JS_TAG_NULL, 0); return out; }
    if (JS_IsBool(v)) { SetExtValue(&out, YXG_JS_TAG_BOOL, JS_ToBool(ctx, v) ? 1 : 0); return out; }
    if (JS_IsNumber(v)) {
        int64_t n = 0;
        JS_ToInt64(ctx, &n, v);
        SetExtValue(&out, YXG_JS_TAG_INT, (uint64_t)n);
        return out;
    }
    if (JS_IsString(v)) {
        const char* s = JS_ToCString(ctx, v);
        if (!s) s = "";
        owned.push_back(s);
        SetExtValue(&out, YXG_JS_TAG_STRING, (uint64_t)(uintptr_t)s);
        return out;
    }
    if (JS_IsException(v)) { SetExtValue(&out, YXG_JS_TAG_EXCEPTION, 0); return out; }
    SetExtValue(&out, YXG_JS_TAG_OBJECT, 0);
    return out;
}

static JSValue ExtToJs(JSContext* ctx, const YXG_JSValue& v) {
    int tag = (int)(v.bits[0] & 0xFF);
    uint64_t val = ExtValueOf(&v);
    switch (tag) {
        case YXG_JS_TAG_INT:
            return JS_NewInt64(ctx, (int64_t)val);
        case YXG_JS_TAG_BOOL:
            return JS_NewBool(ctx, (int)val);
        case YXG_JS_TAG_STRING: {
            const char* s = (const char*)(uintptr_t)val;
            if (!s) return JS_UNDEFINED;
            return JS_NewStringLen(ctx, s, (int)strlen(s));
        }
        case YXG_JS_TAG_NULL:
            return JS_NULL;
        case YXG_JS_TAG_EXCEPTION:
            return JS_ThrowInternalError(ctx, "extension error");
        default:
            return JS_UNDEFINED;
    }
}

// ---- 统一的 JS API 入口（扩展可覆盖/追加/Hook） ----
static JSValue js_ext_api_dispatch(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv, int magic, JSValueConst* func_data) {
    (void)func_data;
    JSPlugin* self = (JSPlugin*)JS_GetContextOpaque(ctx);
    if (!self) return JS_UNDEFINED;

    const JSPlugin::ApiSlot* slot = self->GetApiSlot(magic);

    // 参数转换（字符串指针在调用期间有效）
    std::vector<YXG_JSValue> extArgs(argc);
    std::vector<const char*> owned;
    owned.reserve(argc);
    for (int i = 0; i < argc; i++) extArgs[i] = ExtFromJs(ctx, argv[i], owned);

    YXG_JSValue ret;
    SetExtValue(&ret, YXG_JS_TAG_UNDEFINED, 0);

    YXG_JsApiCallInfo info;
    memset(&info, 0, sizeof(info));
    if (slot) {
        info.ns = slot->ns.c_str();
        info.name = slot->name.c_str();
    }
    info.argc = argc;
    info.argv = argc > 0 ? extArgs.data() : nullptr;
    info.ret = &ret;
    info.stage = 0;  // pre

    // 调用前 Hook：扩展可改写 ret 并返回 1 以拦截（原函数不再执行）
    if (self->RunHooks(YXG_HOOK_JSAPI_CALL, &info, &ret)) {
        JSValue out = ExtToJs(ctx, ret);
        for (auto s : owned) JS_FreeCString(ctx, s);
        return out;
    }

    bool haveResult = false;
    JSValue result = JS_UNDEFINED;
    if (slot && slot->kind == 0 && slot->origFn) {
        // 内置 API：走原函数
        result = ((JSCFunction*)slot->origFn)(ctx, this_val, argc, argv);
        haveResult = true;
    } else if (slot) {
        // 扩展新增/覆盖的 API：走扩展实现
        haveResult = self->CallExtensionJSApi(slot->ns, slot->name,
            argc, extArgs.data(), &ret);
        if (!haveResult) {
            for (auto s : owned) JS_FreeCString(ctx, s);
            return JS_UNDEFINED;
        }
    }

    info.stage = 1;  // post
    if (haveResult && slot && slot->kind == 0) {
        // 把原函数结果转成扩展可见值，供 post Hook 观察/改写
        YXG_JSValue base = ExtFromJs(ctx, result, owned);
        YXG_JSValue orig = base;
        self->RunHooks(YXG_HOOK_JSAPI_CALL, &info, &base);
        bool changed = (base.bits[0] != orig.bits[0]) || (base.bits[1] != orig.bits[1]);
        if (changed) {
            JSValue out = ExtToJs(ctx, base);
            JS_FreeValue(ctx, result);
            for (auto s : owned) JS_FreeCString(ctx, s);
            return out;
        }
        for (auto s : owned) JS_FreeCString(ctx, s);
        return result;
    }

    // 扩展 API 结果（扩展实现已把返回值写入 ret）
    JSValue out = ExtToJs(ctx, ret);
    for (auto s : owned) JS_FreeCString(ctx, s);
    return out;
}

// 绑定一个内置 API：扩展若覆盖则转派给扩展实现
static void BindApi(JSPlugin* self, JSContext* ctx, JSValue nsObj,
    const std::string& ns, const char* name, JSCFunction* fn, int len) {
    int kind = self->IsJSApiOverridden(ns, name) ? 1 : 0;
    int magic = self->AllocApiSlot(ns, name, kind, kind == 0 ? (void*)fn : nullptr);
    JS_SetPropertyStr(ctx, nsObj, name,
        JS_NewCFunctionData(ctx, js_ext_api_dispatch, len, magic, 0, nullptr));
}

// 追加绑定扩展新注册的 API（不与内置名重叠的）
static void BindExtApis(JSPlugin* self, JSContext* ctx, JSValue nsObj,
    const std::string& ns, const std::set<std::string>& builtin) {
    for (const auto& name : self->GetExtJSApiNames(ns)) {
        if (builtin.count(name)) continue;
        int magic = self->AllocApiSlot(ns, name, 1, nullptr);
        JS_SetPropertyStr(ctx, nsObj, name.c_str(),
            JS_NewCFunctionData(ctx, js_ext_api_dispatch, 0, magic, 0, nullptr));
    }
}

void RegisterHostApi(JSPlugin* plugin, JSContext* ctx) {
    JS_SetContextOpaque(ctx, plugin);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue host = JS_GetPropertyStr(ctx, global, "host");

    // 属性
    JS_SetPropertyStr(ctx, host, "id", WStringToJSValue(ctx, plugin->GetId()));
    JS_SetPropertyStr(ctx, host, "name", WStringToJSValue(ctx, plugin->GetName()));
    JS_SetPropertyStr(ctx, host, "version", WStringToJSValue(ctx, plugin->GetVersion()));
    JS_SetPropertyStr(ctx, host, "pluginDir", WStringToJSValue(ctx, plugin->GetFolderPath()));

    // 方法（扩展可覆盖/Hook/追加）
    static const char* const ns = "host";
    static const std::set<std::string> builtin = {
        "log", "requestPermission", "isAuthorized", "listPlugins"
    };
    BindApi(plugin, ctx, host, ns, "log", js_host_log, 1);
    BindApi(plugin, ctx, host, ns, "requestPermission", js_host_requestPermission, 1);
    BindApi(plugin, ctx, host, ns, "isAuthorized", js_host_isAuthorized, 0);
    BindApi(plugin, ctx, host, ns, "listPlugins", js_host_listPlugins, 0);
    BindExtApis(plugin, ctx, host, ns, builtin);

    JS_FreeValue(ctx, host);
    JS_FreeValue(ctx, global);
}

void RegisterWin32Api(JSPlugin* plugin, JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue win32 = JS_GetPropertyStr(ctx, global, "win32");

    static const char* const ns = "win32";
    static const std::set<std::string> builtin = {
        "createControl", "destroyControl", "setWindowText", "getWindowText",
        "moveWindow", "setVisible", "getHostSize", "clear", "sendMessage",
        "httpRequest", "readTextFile", "writeTextFile", "listDir", "moveFile",
        "deleteFile", "openBrowser", "getCacheDir", "setBackground",
        "webViewNavigate", "webViewGetCookies", "webViewExecuteScript",
        "setControlColor"
    };
    BindApi(plugin, ctx, win32, ns, "createControl", js_win32_createControl, 1);
    BindApi(plugin, ctx, win32, ns, "destroyControl", js_win32_destroyControl, 1);
    BindApi(plugin, ctx, win32, ns, "setWindowText", js_win32_setWindowText, 2);
    BindApi(plugin, ctx, win32, ns, "getWindowText", js_win32_getWindowText, 1);
    BindApi(plugin, ctx, win32, ns, "moveWindow", js_win32_moveWindow, 5);
    BindApi(plugin, ctx, win32, ns, "setVisible", js_win32_setVisible, 2);
    BindApi(plugin, ctx, win32, ns, "getHostSize", js_win32_getHostSize, 0);
    BindApi(plugin, ctx, win32, ns, "clear", js_win32_clear, 0);
    BindApi(plugin, ctx, win32, ns, "sendMessage", js_win32_sendMessage, 4);
    BindApi(plugin, ctx, win32, ns, "httpRequest", js_http_request, 1);

    // v3 扩展
    BindApi(plugin, ctx, win32, ns, "readTextFile", js_win32_readTextFile, 1);
    BindApi(plugin, ctx, win32, ns, "writeTextFile", js_win32_writeTextFile, 2);
    BindApi(plugin, ctx, win32, ns, "listDir", js_win32_listDir, 1);
    BindApi(plugin, ctx, win32, ns, "moveFile", js_win32_moveFile, 2);
    BindApi(plugin, ctx, win32, ns, "deleteFile", js_win32_deleteFile, 1);
    BindApi(plugin, ctx, win32, ns, "openBrowser", js_win32_openBrowser, 1);
    BindApi(plugin, ctx, win32, ns, "getCacheDir", js_win32_getCacheDir, 0);
    BindApi(plugin, ctx, win32, ns, "setBackground", js_win32_setBackground, 1);
    BindApi(plugin, ctx, win32, ns, "webViewNavigate", js_win32_webViewNavigate, 2);
    BindApi(plugin, ctx, win32, ns, "webViewGetCookies", js_win32_webViewGetCookies, 1);
    BindApi(plugin, ctx, win32, ns, "webViewExecuteScript", js_win32_webViewExecuteScript, 1);
    BindApi(plugin, ctx, win32, ns, "setControlColor", js_win32_setControlColor, 2);
    // v9 新增
    BindApi(plugin, ctx, win32, ns, "setImage",     js_win32_setImage,     2);
    BindApi(plugin, ctx, win32, ns, "comboAddItem", js_win32_comboAddItem, 2);
    BindApi(plugin, ctx, win32, ns, "comboClear",   js_win32_comboClear,   1);
    BindApi(plugin, ctx, win32, ns, "comboGetSel",  js_win32_comboGetSel,  1);
    BindApi(plugin, ctx, win32, ns, "comboSetSel",  js_win32_comboSetSel,  2);
    BindApi(plugin, ctx, win32, ns, "comboGetText", js_win32_comboGetText, 1);
    BindApi(plugin, ctx, win32, ns, "getDroppedFiles", js_win32_getDroppedFiles, 0);
    BindApi(plugin, ctx, win32, ns, "readFileBase64",    js_win32_readFileBase64,    1);
    BindApi(plugin, ctx, win32, ns, "httpRequestStream", js_win32_httpRequestStream, 2);
    BindApi(plugin, ctx, win32, ns, "richClear",          js_win32_richClear,         1);
    BindApi(plugin, ctx, win32, ns, "richAppend",         js_win32_richAppend,        3);
    BindApi(plugin, ctx, win32, ns, "richScrollToEnd",    js_win32_richScrollToEnd,   1);
    BindApi(plugin, ctx, win32, ns, "chooseColor",        js_win32_chooseColor,       1);
    BindApi(plugin, ctx, win32, ns, "showWebViewOverlay", js_win32_showWebViewOverlay,1);
    BindApi(plugin, ctx, win32, ns, "hideWebViewOverlay", js_win32_hideWebViewOverlay,0);
    // v9.4
    BindApi(plugin, ctx, win32, ns, "richAppendSpan",     js_win32_richAppendSpan,    3);
    BindApi(plugin, ctx, win32, ns, "richAppendLink",     js_win32_richAppendLink,    3);
    BindApi(plugin, ctx, win32, ns, "getScrollPos",       js_win32_getScrollPos,      1);
    BindApi(plugin, ctx, win32, ns, "getPendingLinkTag",  js_win32_getPendingLinkTag, 0);
    BindApi(plugin, ctx, win32, ns, "setClipboard",       js_win32_setClipboard,      1);
    BindApi(plugin, ctx, win32, ns, "getClipboard",       js_win32_getClipboard,      0);
    // v9.5
    BindApi(plugin, ctx, win32, ns, "alert",              js_win32_alert,             2);
    BindApi(plugin, ctx, win32, ns, "confirm",            js_win32_confirm,           2);
    BindApi(plugin, ctx, win32, ns, "inputBox",           js_win32_inputBox,          4);
    BindExtApis(plugin, ctx, win32, ns, builtin);

    JS_FreeValue(ctx, win32);
    JS_FreeValue(ctx, global);
}

void RegisterLightApi(JSPlugin* plugin, JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue light = JS_GetPropertyStr(ctx, global, "light");

    static const char* const ns = "light";
    static const std::set<std::string> builtin = { "exec", "request" };
    BindApi(plugin, ctx, light, ns, "exec", js_light_exec, 1);
    BindApi(plugin, ctx, light, ns, "request", js_http_request, 1);
    BindExtApis(plugin, ctx, light, ns, builtin);

    JS_FreeValue(ctx, light);
    JS_FreeValue(ctx, global);
}

// 绑定扩展注册的自定义命名空间（如 mc.*）。全局对象在 InitRuntimeAndApis 中已创建。
void RegisterExtNamespaceApis(JSPlugin* plugin, JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    for (const auto& ns : plugin->GetExtNamespaces()) {
        JSValue obj = JS_GetPropertyStr(ctx, global, ns.c_str());
        if (JS_IsObject(obj)) {
            static const std::set<std::string> kNoBuiltin;
            BindExtApis(plugin, ctx, obj, ns.c_str(), kNoBuiltin);
        }
        JS_FreeValue(ctx, obj);
    }
    JS_FreeValue(ctx, global);
}
