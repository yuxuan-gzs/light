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
#include "winwrap.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <atomic>
#include <mutex>
#include <utility>
#include "YXExtension.h"
#include "ExtensionManager.h"
struct JSRuntime;
struct JSContext;
using PermissionPromptFn = std::function<bool(
    const std::wstring& pluginId,
    const std::wstring& pluginName,
    const std::wstring& actionDesc)>;
using LightCommandFn = std::function<void(const std::string& cmd)>;

#define WM_YXGUI_HTTP_DONE (WM_USER + 731)

struct CtrlColor {
    int bg = -1;
    int fg = -1;
    int border = -1;
    int active = -1;
    // v12 玻璃现代化：
    //   tint        != -1 时，玻璃板叠加该色（primary 浅蓝 / danger 浅红）
    //   swatchColor != -1 时，按钮按"内陷色板"绘制（调色板，中间薄边缘厚反转：
    //                   外框内凹 + 中央实色色片，选中描蓝环 + ✓）
    int tint = -1;
    int swatchColor = -1;
};

struct PluginWallpaper {
    int mode = 0; // 0=none 1=solid 2=image 3=cycle 4=video 5=lively
    COLORREF color = RGB(244, 247, 251);
    std::wstring image;
    std::wstring cycleDir;
    int cycleMs = 3000;
    std::vector<std::wstring> cycleFiles;
    size_t cycleIndex = 0;
    ULONGLONG lastTick = 0;
    void* imgCache = nullptr;
    std::wstring imgCachePath;
    std::wstring videoPath;
    std::wstring livelyDir;
    bool videoLoop = true;
};

class JSPlugin {
public:
    struct HttpResult {
        int status = -1;
        std::string body;
        std::string error;
    };
    struct HttpTaskDoneMsg {
        uint32_t seq = 0;
        HttpResult result;
    };

    struct ApiSlot {
        std::string ns;
        std::string name;
        int kind = 0;
        void* origFn = nullptr;
    };

    struct PendingHttpPromise {
        uint64_t a = 0;
        uint64_t b = 0;
    };

    static void AddImagePath(HWND h, const std::wstring& path);
    static void SetImagePath(HWND h, const std::wstring& path) { AddImagePath(h, path); }

    JSPlugin();
    virtual ~JSPlugin();

    bool Load(const std::wstring& folderPath, const std::wstring& zipPath = L"");
    bool LoadBuiltin(const std::string& manifestJson, const std::string& entryJs);
    void Unload();
    bool IsLoaded() const { return m_loaded; }
    bool IsPermanentlyAuthorized() const { return m_permanentAuth; }

    void SetMetaInfo(const std::wstring& id, const std::wstring& name,
        const std::wstring& version, const std::wstring& author,
        const std::wstring& description, const std::wstring& iconEmoji);
    void MarkLoaded() { m_loaded = true; }
    void MarkPermanentlyAuthorized() { m_permanentAuth = true; }

    void SetOwnerWnd(HWND wnd) { m_ownerWnd = wnd; }
    HWND GetOwnerWnd() const { return m_ownerWnd ? m_ownerWnd : m_hostContainer; }
    void SetHostContainer(HWND host) { m_hostContainer = host; }
    HWND GetHostContainer() const { return m_hostContainer; }

    void SetLightCommandFn(LightCommandFn fn) { m_lightCmd = std::move(fn); }
    void ForwardLightCommand(const std::string& cmd) const {
        if (m_lightCmd) m_lightCmd(cmd);
    }
    void SetPermissionPrompt(PermissionPromptFn fn) { m_permPrompt = std::move(fn); }

    std::wstring GetId() const { return m_id; }
    const std::wstring& GetVersion() const { return m_version; }
    const std::wstring& GetFolderPath() const { return m_folderPath; }
    std::vector<std::wstring> GetPluginList() const;
    const std::wstring& GetName() const { return m_name; }
    const std::wstring& GetIconEmoji() const { return m_iconEmoji; }
    const std::map<std::wstring, HWND>& GetControls() const { return m_controls; }
    std::map<std::wstring, HWND>& MutableControls() { return m_controls; }
    const std::map<std::wstring, HWND>& Controls() const { return m_controls; }
    const PluginWallpaper* GetWallpaper() const { return &m_wallpaper; }

    HWND CreateNativeControl(const std::string& typeArg,
        const std::wstring& textArg, int x, int y, int w, int h,
        const std::wstring& idArg);

    void RememberControlType(HWND h, const std::string& type);
    std::string CtrlTypeOf(HWND h) const;
    const CtrlColor* GetCtrlColor(HWND h) const;
    void SetCtrlColor(HWND h, const CtrlColor& cc);
    // v12: 玻璃按钮视觉内边距（控件矩形比可见玻璃板大 pad 像素，
    //      避免 hover 放大时被控件矩形裁剪）。默认 0。
    void SetGlassPad(HWND h, int pad);
    bool IsCtrlHover(HWND h) const;
    void SetCtrlHover(HWND h, bool on);
    HFONT FlatFont();
    HBRUSH BrushFor(COLORREF color);
    void ClearControlStyles();

    bool ApplyWallpaper(const std::string& mode, const std::string& colorHex,
        const std::wstring& image, const std::wstring& extra, int cycleMs);
    void RefreshCycleFiles();
    bool CycleNext();
    void ClearWallpaperCache();
    HBITMAP AcquireParentBgSnapshot(HWND hParent, int pw, int ph, ULONGLONG ver);
    void ClearBgCache();
    ULONGLONG BgVersion() const { return m_bgVersion; }
    void BumpBgVersion() { ++m_bgVersion; if (m_bgVersion == 0) ++m_bgVersion; }

    bool RequestPermission(const std::wstring& actionDesc);
    int RunHooks(YXG_HookType hookType, void* dataIn, void* dataOut);
    void ScanAndLoadExtensions();
    void UnloadExtensions();
    YXG_ExtensionApi* GetExtensionApi() const;
    bool IsJSApiOverridden(const std::string& ns, const std::string& name) const;
    bool CallExtensionJSApi(const std::string& ns, const std::string& name,
        int argc, YXG_JSValue* argv, YXG_JSValue* ret);
    std::vector<std::string> GetExtJSApiNames(const std::string& ns) const;
    // 枚举扩展注册的全部自定义命名空间（排除 host/win32/light）
    std::vector<std::string> GetExtNamespaces() const;

    bool EvalFile(const std::wstring& absPath);
    bool EvalString(const std::string& code, const std::wstring& filenameHint);
    bool CallGlobalFunction(const char* fnName, int argc = 0, void** argv = nullptr);
    int AllocApiSlot(const std::string& ns, const std::string& name,
        int kind, void* origFn);
    const ApiSlot* GetApiSlot(int magic) const;

    void StorePendingHttp(uint32_t seq, uint64_t a, uint64_t b) {
        m_pendingHttp[seq] = PendingHttpPromise{ a, b };
    }

    void RegisterRichLink(HWND hEdit, LONG startPos, LONG endPos,
        const std::wstring& tag);
    std::wstring ConsumePendingLinkTag();

    virtual void CallOnLoad();
    virtual void CallOnViewShow(HWND hostContainer, int w, int h);
    virtual void CallOnViewHide();
    virtual void CallOnResize(int w, int h);
    virtual void CallOnUnload();
    virtual void CallOnControlEvent(const std::wstring& controlId, int code);
    virtual void CallHttpDone(uint32_t seq, const HttpResult& result);

protected:
    std::map<std::wstring, HWND> m_controls;
    HWND m_hostContainer = nullptr;
    HWND m_ownerWnd = nullptr;
    std::wstring m_id;
    std::wstring m_name;
    std::wstring m_version;
    std::wstring m_author;
    std::wstring m_description;
    std::wstring m_iconEmoji;
    std::wstring m_entryFile;
    std::wstring m_folderPath;
    std::wstring m_zipSource;

    bool m_loaded = false;
    bool m_permanentAuth = false;
    PermissionPromptFn m_permPrompt;
    LightCommandFn m_lightCmd;

    std::map<HWND, std::string> m_ctrlType;
    std::map<HWND, CtrlColor> m_ctrlColors;
    std::map<HWND, bool> m_ctrlHover;
    std::map<COLORREF, HBRUSH> m_brushCache;
    HFONT m_hFlatFont = nullptr;
    PluginWallpaper m_wallpaper;
    ULONGLONG m_bgVersion = 1;
    HBITMAP m_parentBgBmp = nullptr;
    int m_parentBgW = 0;
    int m_parentBgH = 0;
    ULONGLONG m_bgVerStored = 0;

    ExtensionManager* m_extMgr = nullptr;
    std::vector<ApiSlot> m_apiSlots;
    std::map<uint32_t, PendingHttpPromise> m_pendingHttp;

private:
    int AllocControlId();
    bool ParseManifestJson(const std::string& json);
    bool ReadManifest(const std::wstring& folderPath);
    bool InitRuntimeAndApis();
    JSRuntime* m_rt = nullptr;
    JSContext* m_ctx = nullptr;
};
