// GuiDllStore.cpp
// 插件商店 + GUI 附属插件宿主实现
#include "GuiDllStore.h"

#include <windowsx.h>
#include <ole2.h>
#include <gdiplus.h>
#include <winhttp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <atomic>
#include <set>
#include <cwchar>

#include "ExtensionManager.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winhttp.lib")

namespace fs = std::filesystem;
using namespace Gdiplus;

// 详情弹窗状态（完整定义见「插件详情窗口」一节）；前向声明供析构 / 销毁时关闭。
struct DetailState;
static DetailState* g_detailState = nullptr;
static void CloseDetailWindow();

// ============================================================================
// 基础辅助
// ============================================================================
static std::wstring AnsiToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(),
        (int)s.size(), NULL, 0);
    UINT cp = CP_UTF8;
    if (len <= 0) {
        cp = CP_ACP;
        len = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), NULL, 0);
        if (len <= 0) return L"";
    }
    std::wstring out(len, 0);
    MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
        s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
        NULL, 0, NULL, NULL);
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], len,
        NULL, NULL);
    return out;
}

static std::string GetExeDir() {
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir(exePath);
    size_t p = exeDir.find_last_of("\\/");
    if (p != std::string::npos) exeDir = exeDir.substr(0, p);
    return exeDir;
}

static std::wstring StoreCacheDir() {
    std::wstring dir = AnsiToWide(GetExeDir() + "\\Cache\\gui\\store");
    SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    return dir;
}

static std::wstring LowerExt(const std::wstring& s) {
    size_t dot = s.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring e = s.substr(dot);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

static std::wstring StemOf(const std::wstring& filename) {
    size_t dot = filename.find_last_of(L'.');
    if (dot == std::wstring::npos) return filename;
    return filename.substr(0, dot);
}

static bool IsHttp(const std::wstring& u) {
    return u.find(L"http://") == 0 || u.find(L"https://") == 0;
}

static bool FileExistsW(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::string ReadLocalText(const std::wstring& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return "";
    std::string s((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    return s;
}

static bool CopyLocal(const std::wstring& src, const std::wstring& dst) {
    if (!FileExistsW(src)) return false;
    try {
        fs::path d(dst);
        if (d.has_parent_path()) fs::create_directories(d.parent_path());
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        return true;
    }
    catch (...) { return false; }
}

static std::wstring UrlEncodePath(const std::wstring& p) {
    std::wstring out;
    for (wchar_t ch : p) {
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_' ||
            ch == L'.' || ch == L'/' || ch == L'~') {
            out += ch;
        }
        else {
            std::string u = WideToUtf8(std::wstring(1, ch));
            for (unsigned char b : u) {
                wchar_t hex[4];
                swprintf_s(hex, L"%%%02X", (unsigned)b);
                out += hex;
            }
        }
    }
    return out;
}

// ============================================================================
// HTTP（WinHTTP）
// ============================================================================
static bool SplitUrl(const std::wstring& url, std::wstring& host, int& port,
    std::wstring& path, bool& https) {
    std::wstring u = url;
    https = true; port = 443;
    size_t p = u.find(L"://");
    if (p == std::wstring::npos) return false;
    std::wstring scheme = u.substr(0, p);
    if (_wcsicmp(scheme.c_str(), L"https") == 0) { https = true; port = 443; }
    else if (_wcsicmp(scheme.c_str(), L"http") == 0) { https = false; port = 80; }
    else return false;
    u = u.substr(p + 3);
    size_t slash = u.find(L'/');
    std::wstring hostport = (slash == std::wstring::npos) ? u : u.substr(0, slash);
    path = (slash == std::wstring::npos) ? L"/" : u.substr(slash);
    size_t colon = hostport.find(L':');
    if (colon != std::wstring::npos) {
        host = hostport.substr(0, colon);
        port = _wtoi(hostport.substr(colon + 1).c_str());
    }
    else host = hostport;
    if (host.empty()) return false;
    return true;
}

static bool WinHttpGetToFile(const std::wstring& url, const std::wstring& dest,
    const std::wstring& extraHeaders = L"") {
    std::wstring host, path; int port; bool https;
    if (!SplitUrl(url, host, port, path, https)) return false;

    HINTERNET hSession = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    DWORD optRedirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &optRedirect,
        sizeof(optRedirect));
    DWORD timeout = 15000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    DWORD connectTimeout = 8000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
    DWORD resolveTimeout = 8000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RESOLVE_TIMEOUT, &resolveTimeout, sizeof(resolveTimeout));

    LPCWSTR hdrs = WINHTTP_NO_ADDITIONAL_HEADERS;
    DWORD hdrLen = 0;
    if (!extraHeaders.empty()) { hdrs = extraHeaders.c_str(); hdrLen = (DWORD)extraHeaders.size(); }

    if (!WinHttpSendRequest(hRequest, hdrs, hdrLen, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }
    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode < 200 || statusCode >= 300) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring dir = dest;
    size_t s = dir.find_last_of(L"\\/");
    if (s != std::wstring::npos) {
        dir = dir.substr(0, s);
        SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    }
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }
    DWORD bytesRead = 0;
    BYTE chunk[64 * 1024];
    while (WinHttpReadData(hRequest, chunk, sizeof(chunk), &bytesRead) && bytesRead > 0) {
        out.write((const char*)chunk, bytesRead);
    }
    out.close();
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return true;
}

static bool WinHttpGetText(const std::wstring& url, std::string& out,
    const std::wstring& extraHeaders = L"") {
    std::wstring host, path; int port; bool https;
    if (!SplitUrl(url, host, port, path, https)) return false;

    HINTERNET hSession = WinHttpOpen(L"Light-Plugin-Store/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    DWORD optRedirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &optRedirect, sizeof(optRedirect));
    DWORD timeout = 15000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    DWORD connectTimeout = 8000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
    DWORD resolveTimeout = 8000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RESOLVE_TIMEOUT, &resolveTimeout, sizeof(resolveTimeout));

    LPCWSTR hdrs = WINHTTP_NO_ADDITIONAL_HEADERS;
    DWORD hdrLen = 0;
    if (!extraHeaders.empty()) { hdrs = extraHeaders.c_str(); hdrLen = (DWORD)extraHeaders.size(); }

    if (!WinHttpSendRequest(hRequest, hdrs, hdrLen, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }
    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode < 200 || statusCode >= 300) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }
    std::string body;
    DWORD bytesRead = 0;
    char chunk[8192];
    while (WinHttpReadData(hRequest, chunk, sizeof(chunk) - 1, &bytesRead) && bytesRead > 0) {
        chunk[bytesRead] = 0;
        body.append(chunk, bytesRead);
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    out = std::move(body);
    return true;
}

// ============================================================================
// 仓库解析与列表获取
// ============================================================================
static GuiDllRepo ParseRepoLink(const std::wstring& link) {
    GuiDllRepo r;
    std::wstring t = link;
    // 去除首尾空白
    while (!t.empty() && (t.front() == L' ' || t.front() == L'\t')) t.erase(t.begin());
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\t')) t.pop_back();

    if (t.find(L"github.com/") != std::wstring::npos) {
        // https://github.com/<owner>/<repo>(/tree/<branch>)?
        std::wstring u = t;
        size_t p = u.find(L"github.com/");
        u = u.substr(p + 11);
        // 去掉可能的前导 /
        while (!u.empty() && u.front() == L'/') u.erase(u.begin());
        std::vector<std::wstring> segs;
        size_t pos = 0;
        while (pos <= u.size()) {
            size_t slash = u.find(L'/', pos);
            std::wstring seg = (slash == std::wstring::npos) ? u.substr(pos) : u.substr(pos, slash - pos);
            if (!seg.empty()) segs.push_back(seg);
            if (slash == std::wstring::npos) break;
            pos = slash + 1;
        }
        if (segs.size() >= 2) {
            r.type = 0;
            r.owner = segs[0];
            r.repo = segs[1];
            r.branch = L"";
            if (segs.size() >= 4 && segs[2] == L"tree") {
                r.branch = segs[3];
                for (size_t i = 4; i < segs.size(); ++i) r.branch += L"/" + segs[i];
            }
            else if (segs.size() >= 4 && segs[2] == L"blob") {
                r.branch = segs[3];
            }
            r.display = L"github.com/" + r.owner + L"/" + r.repo +
                (r.branch.empty() ? L"" : (L"@" + r.branch));
            return r;
        }
    }

    if (t.find(L"http://") == 0 || t.find(L"https://") == 0) {
        r.type = 2;
        r.path = t;
        while (!r.path.empty() && r.path.back() == L'/') r.path.pop_back();
        r.display = r.path;
        return r;
    }

    // 本地目录
    r.type = 1;
    r.path = t;
    r.display = t;
    return r;
}

// 从 GitHub tree API 拉取 .dll 及伴生文件
static std::vector<GuiDllStoreItem> FetchGithubListing(const GuiDllRepo& repo) {
    std::vector<GuiDllStoreItem> out;
    std::wstring api = L"https://api.github.com/repos/" + repo.owner + L"/" +
        repo.repo + L"/git/trees/" + repo.branch + L"?recursive=1";
    std::string json;
    std::wstring hdr = L"User-Agent: Light-Plugin-Store\r\n"
        L"Accept: application/vnd.github+json\r\n";
    if (!WinHttpGetText(api, json, hdr)) return out;

    std::string rawBase = "https://raw.githubusercontent.com/" +
        WideToUtf8(repo.owner) + "/" + WideToUtf8(repo.repo) + "/" +
        WideToUtf8(repo.branch) + "/";

    // name -> item
    std::map<std::wstring, GuiDllStoreItem> map;
    size_t pos = 0;
    const std::string key = "\"path\":\"";
    while ((pos = json.find(key, pos)) != std::string::npos) {
        pos += key.size();
        size_t e = pos;
        while (e < json.size() && json[e] != '"') {
            if (json[e] == '\\') e++;
            e++;
        }
        std::string pth = json.substr(pos, e - pos);
        pos = e + 1;
        std::wstring wp = AnsiToWide(pth);
        size_t slash = wp.find_last_of(L'/');
        std::wstring filename = (slash == std::wstring::npos) ? wp : wp.substr(slash + 1);
        std::wstring ext = LowerExt(filename);
        std::wstring raw = AnsiToWide(rawBase) + UrlEncodePath(wp);

        if (ext == L".dll") {
            std::wstring name = StemOf(filename);
            GuiDllStoreItem& it = map[name];
            if (it.name.empty()) it.name = name;
            it.dllUrl = raw;
        }
        else if (ext == L".txt") {
            std::wstring name = StemOf(filename);
            map[name].txtUrl = raw;
        }
        else if (ext == L".png" || ext == L".jpg" || ext == L".jpeg") {
            std::wstring name = StemOf(filename);
            GuiDllStoreItem& it = map[name];
            // 优先 png
            if (it.logoUrl.empty() || ext == L".png") it.logoUrl = raw;
        }
        else if (ext == L".mp4") {
            std::wstring name = StemOf(filename);
            map[name].videoUrl = raw;
        }
    }

    for (auto& kv : map) {
        if (!kv.second.dllUrl.empty()) out.push_back(kv.second);
    }
    return out;
}

static std::vector<GuiDllStoreItem> FetchLocalListing(const GuiDllRepo& repo) {
    std::vector<GuiDllStoreItem> out;
    if (repo.path.empty()) return out;
    std::wstring dir = repo.path;
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    std::wstring pat = dir + L"\\*.dll";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring fn = fd.cFileName;
        if (fn.size() < 4) continue;
        std::wstring ext = LowerExt(fn);
        if (ext != L".dll") continue;
        std::wstring name = StemOf(fn);
        GuiDllStoreItem it;
        it.name = name;
        it.dllUrl = dir + L"\\" + fn;
        std::wstring txt = dir + L"\\" + name + L".txt";
        if (FileExistsW(txt)) it.txtUrl = txt;
        std::wstring png = dir + L"\\" + name + L".png";
        std::wstring jpg = dir + L"\\" + name + L".jpg";
        if (FileExistsW(png)) it.logoUrl = png;
        else if (FileExistsW(jpg)) it.logoUrl = jpg;
        std::wstring mp4 = dir + L"\\" + name + L".mp4";
        if (FileExistsW(mp4)) it.videoUrl = mp4;
        out.push_back(it);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

static std::vector<GuiDllStoreItem> FetchHttpListing(const GuiDllRepo& repo) {
    std::vector<GuiDllStoreItem> out;
    std::wstring base = repo.path;
    if (base.empty()) return out;
    std::string list;
    if (!WinHttpGetText(base + L"/list.txt", list)) return out;
    std::istringstream iss(list);
    std::string line;
    while (std::getline(iss, line)) {
        // 去除 \r
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        std::wstring name = AnsiToWide(line);
        if (name.empty()) continue;
        GuiDllStoreItem it;
        it.name = name;
        it.dllUrl = base + L"/" + name + L".dll";
        it.txtUrl = base + L"/" + name + L".txt";
        it.logoUrl = base + L"/" + name + L".png";   // 若 404，元数据拉取时回退 jpg
        it.videoUrl = base + L"/" + name + L".mp4";
        out.push_back(it);
    }
    return out;
}

// 拉取描述与 logo（在后台线程中执行）
static void FetchMetadata(GuiDllStoreItem& it) {
    if (!it.txtUrl.empty()) {
        if (IsHttp(it.txtUrl)) {
            std::string s;
            if (WinHttpGetText(it.txtUrl, s)) {
                // 去首尾空白
                while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
                it.description = AnsiToWide(s);
            }
        }
        else {
            it.description = AnsiToWide(ReadLocalText(it.txtUrl));
        }
    }
    if (!it.logoUrl.empty()) {
        std::wstring ext = LowerExt(it.logoUrl);
        if (ext != L".png" && ext != L".jpg" && ext != L".jpeg") ext = L".png";
        std::wstring dest = StoreCacheDir() + L"\\" + it.name + ext;
        bool ok = false;
        if (IsHttp(it.logoUrl)) {
            ok = WinHttpGetToFile(it.logoUrl, dest);
            if (!ok) {
                // 尝试 jpg 回退（http 列表可能用 jpg）
                std::wstring jpgUrl = it.logoUrl;
                size_t d = jpgUrl.find_last_of(L'.');
                if (d != std::wstring::npos) jpgUrl = jpgUrl.substr(0, d) + L".jpg";
                ok = WinHttpGetToFile(jpgUrl, dest);
                if (ok) it.logoUrl = jpgUrl;
            }
        }
        else {
            ok = CopyLocal(it.logoUrl, dest);
        }
        if (ok) it.logoPath = dest;
    }
}

// ============================================================================
// GuiDllPluginHost
// ============================================================================
GuiDllPluginHost* g_guiDllHost = nullptr;

struct GuiDllPluginHost::Impl {
    std::unique_ptr<ExtensionManager> ext;
};

GuiDllPluginHost::GuiDllPluginHost() : m_impl(new Impl()) {
    m_impl->ext = std::make_unique<ExtensionManager>(nullptr);
}

GuiDllPluginHost::~GuiDllPluginHost() = default;

GuiDllPluginHost* GuiDllPluginHost::Instance() {
    static GuiDllPluginHost inst;
    if (!g_guiDllHost) g_guiDllHost = &inst;
    return &inst;
}

std::wstring GuiDllPluginHost::DllDir() {
    std::wstring dir = AnsiToWide(GetExeDir() + "\\Cache\\gui\\dll");
    SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    return dir;
}

void GuiDllPluginHost::LoadAll() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_impl->ext->UnloadAll();
    m_impl->ext->ScanAndLoadGuiDll(DllDir());
}

void GuiDllPluginHost::UnloadAll() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_impl->ext->UnloadAll();
}

void GuiDllPluginHost::Reload() { LoadAll(); }

bool GuiDllPluginHost::IsInstalled(const std::wstring& name) const {
    std::wstring base = const_cast<GuiDllPluginHost*>(this)->DllDir() + L"\\" + name;
    if (FileExistsW(base + L".dll")) return true;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L".dll.*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { FindClose(h); return true; }
    return false;
}

bool GuiDllPluginHost::IsEnabled(const std::wstring& name) const {
    std::wstring base = const_cast<GuiDllPluginHost*>(this)->DllDir() + L"\\" + name;
    return FileExistsW(base + L".dll");
}

std::wstring GuiDllPluginHost::DllPath(const std::wstring& name) const {
    std::wstring dir = const_cast<GuiDllPluginHost*>(this)->DllDir();
    std::wstring base = dir + L"\\" + name;
    if (FileExistsW(base + L".dll")) return base + L".dll";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L".dll.*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        std::wstring p = dir + L"\\" + fd.cFileName;
        FindClose(h);
        return p;
    }
    return L"";
}

static std::wstring FindDisabledDll(const std::wstring& dir, const std::wstring& name) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\" + name + L".dll.*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring p = dir + L"\\" + fd.cFileName;
    FindClose(h);
    return p;
}

bool GuiDllPluginHost::SetEnabled(const std::wstring& name, bool enabled) {
    std::lock_guard<std::mutex> lk(m_mu);
    std::wstring dir = DllDir();
    std::wstring dll = dir + L"\\" + name + L".dll";
    std::wstring disabled = FindDisabledDll(dir, name);
    if (enabled) {
        if (!disabled.empty()) {
            return MoveFileExW(disabled.c_str(), dll.c_str(),
                MOVEFILE_REPLACE_EXISTING) != 0;
        }
        return FileExistsW(dll);   // 已启用
    }
    else {
        if (FileExistsW(dll)) {
            std::wstring target = dir + L"\\" + name + L".dll.1";
            return MoveFileExW(dll.c_str(), target.c_str(),
                MOVEFILE_REPLACE_EXISTING) != 0;
        }
        return !disabled.empty();  // 已停用
    }
}

std::vector<std::wstring> GuiDllPluginHost::InstalledNames() const {
    std::vector<std::wstring> names;
    std::wstring dir = const_cast<GuiDllPluginHost*>(this)->DllDir();
    std::set<std::wstring> seen;
    WIN32_FIND_DATAW fd;
    // 同时收集启用（name.dll）与停用（name.dll.N）的插件，供插件中心统一管理。
    HANDLE h = FindFirstFileW((dir + L"\\*.dll*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring fn = fd.cFileName;
            std::wstring stem;
            if (fn.size() > 4 && fn.compare(fn.size() - 4, 4, L".dll") == 0) {
                stem = fn.substr(0, fn.size() - 4);        // name.dll
            }
            else {
                size_t p = fn.find(L".dll.");
                if (p == std::wstring::npos) continue;      // 伴生文件，跳过
                stem = fn.substr(0, p);                      // name.dll.N
            }
            if (stem.empty()) continue;
            if (seen.insert(stem).second) names.push_back(stem);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return names;
}

std::wstring GuiDllPluginHost::Description(const std::wstring& name) const {
    std::wstring txt = const_cast<GuiDllPluginHost*>(this)->DllDir() +
        L"\\" + name + L".txt";
    std::string s = ReadLocalText(txt);
    if (s.empty()) return L"";
    // 去首尾空白
    size_t b = s.find_first_not_of("\r\n\t ");
    size_t e = s.find_last_not_of("\r\n\t ");
    if (b == std::string::npos) return L"";
    return AnsiToWide(s.substr(b, e - b + 1));
}

int GuiDllPluginHost::RunHooks(YXG_HookType type, const wchar_t* pluginId,
    void* in, void* out) {
    return m_impl->ext->RunHooks(type, pluginId, in, out);
}

bool GuiDllPluginHost::HasJSApi(const std::string& ns, const std::string& name) const {
    return m_impl->ext->HasJSApi(ns, name);
}

bool GuiDllPluginHost::CallJSApi(const std::string& ns, const std::string& name,
    int argc, YXG_JSValue* argv, YXG_JSValue* ret) {
    return m_impl->ext->CallJSApi(ns, name, argc, argv, ret);
}

std::vector<std::string> GuiDllPluginHost::GetJSApiNames(const std::string& ns) const {
    return m_impl->ext->GetJSApiNames(ns);
}

std::vector<std::string> GuiDllPluginHost::GetNamespaces() const {
    return m_impl->ext->GetNamespaces();
}

std::vector<NativePanelRecord> GuiDllPluginHost::GetNativePanels() const {
    return m_impl->ext->GetNativePanels();
}

// ============================================================================
// GuiDllStorePanel
// ============================================================================
namespace {

constexpr int kTopBarH = 44;
constexpr int kCardH = 214;
constexpr int kGap = 12;
constexpr int kPadX = 14;

constexpr ARGB kBg = 0xFFF6F8FC;
constexpr ARGB kCard = 0xFFFFFFFF;
constexpr ARGB kCardHover = 0xFFF8FBFF;
constexpr ARGB kBorder = 0xFFE2E8F2;
constexpr ARGB kBorderHover = 0xFFBCD2F5;
constexpr ARGB kText = 0xFF1E293B;
constexpr ARGB kMuted = 0xFF6B7A90;
constexpr ARGB kAccent = 0xFF3B82F6;
constexpr ARGB kAccentSoft = 0xFFE8F1FF;
constexpr ARGB kGreen = 0xFF22C55E;
constexpr ARGB kGreenSoft = 0xFFE1F9EA;
constexpr ARGB kOff = 0xFFCBD5E1;

constexpr wchar_t kClassName[] = L"LightGuiDllStoreWnd";

#define WM_YXGUI_STORE_LOADED      (WM_USER + 740)
#define WM_YXGUI_STORE_INSTALL_DONE (WM_USER + 741)
#define IDT_STORE_SPIN             1

void MakeRoundedRect(const RectF& r, REAL radius, GraphicsPath& path) {
    path.Reset();
    const REAL d = radius * 2.0f;
    if (radius <= 0.1f) { path.AddRectangle(r); return; }
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillRounded(Graphics& g, const RectF& r, REAL radius, ARGB color) {
    GraphicsPath path;
    MakeRoundedRect(r, radius, path);
    SolidBrush brush{ Color(color) };
    g.FillPath(&brush, &path);
}

void StrokeRounded(Graphics& g, const RectF& r, REAL radius, ARGB color,
    REAL width = 1.0f) {
    GraphicsPath path;
    MakeRoundedRect(r, radius, path);
    Pen pen(Color(color), width);
    g.DrawPath(&pen, &path);
}

std::wstring Ellipsis(const std::wstring& text, size_t maxChars) {
    if (text.size() <= maxChars) return text;
    if (maxChars <= 1) return text.substr(0, maxChars);
    return text.substr(0, maxChars - 1) + L"…";
}

void DrawStringSimple(Graphics& g, const std::wstring& text, Font& font,
    const RectF& rect, ARGB color, StringAlignment align = StringAlignmentNear,
    StringAlignment lineAlign = StringAlignmentCenter, bool ellipsis = true) {
    SolidBrush brush{ Color(color) };
    StringFormat fmt;
    fmt.SetAlignment(align);
    fmt.SetLineAlignment(lineAlign);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    fmt.SetTrimming(ellipsis ? StringTrimmingEllipsisCharacter : StringTrimmingNone);
    g.DrawString(text.c_str(), -1, &font, rect, &fmt, &brush);
}

} // namespace

GuiDllStorePanel::GuiDllStorePanel() {
    GuiDllRepo def;
    def.type = 0;
    def.display = L"官方仓库 github.com/yuxuan-gzs/light @ gui.dll";
    def.owner = L"yuxuan-gzs";
    def.repo = L"light";
    def.branch = L"gui.dll";
    m_repos.push_back(def);
}

GuiDllStorePanel::~GuiDllStorePanel() {
    CloseDetailWindow();
    for (auto& kv : m_logoCache) {
        if (kv.second) delete reinterpret_cast<Bitmap*>(kv.second);
    }
    m_logoCache.clear();
    m_hWnd = nullptr;
}

void GuiDllStorePanel::EnsureClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = GuiDllStorePanel::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    registered = true;
}

bool GuiDllStorePanel::Create(HWND parent, HINSTANCE instance, int controlId) {
    if (m_hWnd && IsWindow(m_hWnd)) return true;
    EnsureClass(instance);
    m_hParent = parent;
    m_hInstance = instance;
    m_controlId = controlId;

    m_hWnd = CreateWindowExW(0, kClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_TABSTOP,
        0, 0, 100, 100, parent, (HMENU)(INT_PTR)controlId, instance, this);
    if (!m_hWnd) return false;

    // 搜索框
    m_hSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        0, 0, 200, 30, m_hWnd, (HMENU)(INT_PTR)1, instance, NULL);
    SendMessageW(m_hSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索插件…");

    // 添加仓库
    m_hAddRepo = CreateWindowExW(0, L"BUTTON", L"＋ 仓库",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 88, 30, m_hWnd, (HMENU)(INT_PTR)2, instance, NULL);

    // 刷新
    m_hRefresh = CreateWindowExW(0, L"BUTTON", L"⟳",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 36, 30, m_hWnd, (HMENU)(INT_PTR)3, instance, NULL);

    HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(m_hSearch, WM_SETFONT, (WPARAM)hf, TRUE);
    SendMessageW(m_hAddRepo, WM_SETFONT, (WPARAM)hf, TRUE);
    SendMessageW(m_hRefresh, WM_SETFONT, (WPARAM)hf, TRUE);

    StartLoad();
    return true;
}

void GuiDllStorePanel::Destroy() {
    CloseDetailWindow();
    if (m_hWnd && IsWindow(m_hWnd)) DestroyWindow(m_hWnd);
    m_hWnd = nullptr;
}

void GuiDllStorePanel::Move(int x, int y, int w, int h) {
    if (m_hWnd && IsWindow(m_hWnd)) MoveWindow(m_hWnd, x, y, w, h, TRUE);
}

void GuiDllStorePanel::Show(bool show) {
    if (!m_hWnd || !IsWindow(m_hWnd)) return;
    ShowWindow(m_hWnd, show ? SW_SHOW : SW_HIDE);
    // 显示时若仍在加载，重新挂载旋转动画定时器，确保 spinner 持续转动
    // （防止窗口隐藏期间定时器失效或切标签页后动画停止）。
    if (show && m_loading) {
        KillTimer(m_hWnd, IDT_STORE_SPIN);
        SetTimer(m_hWnd, IDT_STORE_SPIN, 30, NULL);
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void GuiDllStorePanel::Refresh() {
    if (!m_hWnd || !IsWindow(m_hWnd)) return;
    UpdateInstalledState();
    RebuildFilter();
    InvalidateRect(m_hWnd, nullptr, FALSE);
    // 仅在尚无数据时自动加载（⟳ 按钮可手动强制重新拉取）
    if (m_items.empty() && !m_loadRunning) StartLoad();
}

void GuiDllStorePanel::RebuildFilter() {
    m_filtered.clear();
    std::wstring q = m_search;
    for (auto& c : q) c = (wchar_t)towlower(c);
    for (int i = 0; i < (int)m_items.size(); ++i) {
        if (q.empty()) { m_filtered.push_back(i); continue; }
        std::wstring hay = m_items[i].name + L" " + m_items[i].description;
        for (auto& c : hay) c = (wchar_t)towlower(c);
        if (hay.find(q) != std::wstring::npos) m_filtered.push_back(i);
    }
    m_scrollY = 0;
}

void GuiDllStorePanel::StartLoad() {
    if (m_repos.empty()) return;
    if (m_loadRunning) return;
    GuiDllRepo repo = m_repos[m_curRepo];
    m_loading = true;
    m_spinActive = true;
    m_spin = 0.0f;
    m_status = L"正在加载商店…";
    m_loadRunning = true;
    if (m_hWnd && IsWindow(m_hWnd)) {
        SetTimer(m_hWnd, IDT_STORE_SPIN, 30, NULL);
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
    std::thread t([this, repo]() { LoadRepoInBackground(repo); });
    t.detach();
}

void GuiDllStorePanel::LoadRepoInBackground(const GuiDllRepo& repo) {
    std::vector<GuiDllStoreItem> items;
    std::wstring status;
    try {
        if (repo.type == 0) items = FetchGithubListing(repo);
        else if (repo.type == 1) items = FetchLocalListing(repo);
        else items = FetchHttpListing(repo);

        for (auto& it : items) {
            FetchMetadata(it);
        }
        if (items.empty()) status = L"仓库中暂无可安装插件";
    }
    catch (...) {
        status = L"加载失败（网络或仓库不可用）";
    }

    if (m_hWnd && IsWindow(m_hWnd)) {
        auto* pItems = new std::vector<GuiDllStoreItem>(std::move(items));
        auto* pStatus = new std::wstring(status);
        PostMessageW(m_hWnd, WM_YXGUI_STORE_LOADED, (WPARAM)pItems, (LPARAM)pStatus);
    }
}

void GuiDllStorePanel::OnLoadFinished(std::vector<GuiDllStoreItem>* items,
    std::wstring* status) {
    m_loadRunning = false;
    m_loading = false;
    m_spinActive = false;
    if (m_hWnd && IsWindow(m_hWnd)) KillTimer(m_hWnd, IDT_STORE_SPIN);
    if (items) {
        m_items = std::move(*items);
        delete items;
        UpdateInstalledState();
    }
    if (status) {
        if (!status->empty()) m_status = *status;
        delete status;
    }
    RebuildFilter();
    if (m_hWnd && IsWindow(m_hWnd)) InvalidateRect(m_hWnd, nullptr, FALSE);
}

void GuiDllStorePanel::UpdateInstalledState() {
    auto* host = GuiDllPluginHost::Instance();
    std::wstring dir = host->DllDir();
    for (auto& it : m_items) {
        it.installed = host->IsInstalled(it.name);
        it.enabled = host->IsEnabled(it.name);
        // 本地伴生文件
        if (it.logoPath.empty()) {
            std::wstring png = dir + L"\\" + it.name + L".png";
            std::wstring jpg = dir + L"\\" + it.name + L".jpg";
            if (FileExistsW(png)) it.logoPath = png;
            else if (FileExistsW(jpg)) it.logoPath = jpg;
        }
        if (it.videoPath.empty()) {
            std::wstring mp4 = dir + L"\\" + it.name + L".mp4";
            if (FileExistsW(mp4)) it.videoPath = mp4;
        }
    }
}

void GuiDllStorePanel::InstallItem(const GuiDllStoreItem& item) {
    for (auto& it : m_items) {
        if (it.name == item.name) { it.installing = true; it.installed = false; }
    }
    if (m_hWnd && IsWindow(m_hWnd)) InvalidateRect(m_hWnd, nullptr, FALSE);
    std::thread t([this, item]() { DoInstallInBackground(item); });
    t.detach();
}

void GuiDllStorePanel::DoInstallInBackground(GuiDllStoreItem item) {
    std::wstring dir = GuiDllPluginHost::Instance()->DllDir();
    std::wstring dest = dir + L"\\" + item.name + L".dll";

    // 若存在旧 .dll.N，先移除，避免加载器漏扫
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\" + item.name + L".dll.*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    bool ok = IsHttp(item.dllUrl) ? WinHttpGetToFile(item.dllUrl, dest)
                                  : CopyLocal(item.dllUrl, dest);
    std::wstring err = L"";
    if (!ok) err = L"下载 DLL 失败";

    if (ok && !item.txtUrl.empty()) {
        std::wstring t = dir + L"\\" + item.name + L".txt";
        if (IsHttp(item.txtUrl)) WinHttpGetToFile(item.txtUrl, t);
        else CopyLocal(item.txtUrl, t);
    }
    if (ok && !item.logoUrl.empty()) {
        std::wstring ext = LowerExt(item.logoUrl);
        if (ext != L".png" && ext != L".jpg" && ext != L".jpeg") ext = L".png";
        std::wstring l = dir + L"\\" + item.name + ext;
        if (IsHttp(item.logoUrl)) WinHttpGetToFile(item.logoUrl, l);
        else CopyLocal(item.logoUrl, l);
    }
    if (ok && !item.videoUrl.empty()) {
        std::wstring v = dir + L"\\" + item.name + L".mp4";
        if (IsHttp(item.videoUrl)) WinHttpGetToFile(item.videoUrl, v);
        else CopyLocal(item.videoUrl, v);
    }

    if (ok) {
        // 重新加载放到 UI 线程（避免与 JS 执行路径竞争 g_apiTableOwner）
    }

    if (m_hWnd && IsWindow(m_hWnd)) {
        auto* pStatus = new std::wstring(err);
        PostMessageW(m_hWnd, WM_YXGUI_STORE_INSTALL_DONE, 0, (LPARAM)pStatus);
    }
}

void GuiDllStorePanel::DoToggleEnabled(const GuiDllStoreItem& item) {
    bool target = !item.enabled;
    bool ok = GuiDllPluginHost::Instance()->SetEnabled(item.name, target);
    if (ok) {
        GuiDllPluginHost::Instance()->Reload();
        UpdateInstalledState();
        RebuildFilter();
        if (m_hWnd && IsWindow(m_hWnd)) InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void GuiDllStorePanel::PlayVideo(const GuiDllStoreItem& item) {
    std::wstring target;
    if (!item.videoPath.empty() && FileExistsW(item.videoPath)) target = item.videoPath;
    else if (IsHttp(item.videoUrl)) target = item.videoUrl;
    else if (!item.videoUrl.empty() && FileExistsW(item.videoUrl)) target = item.videoUrl;
    if (target.empty()) return;
    ShellExecuteW(m_hWnd, L"open", target.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

// ============================================================
// 插件详情窗口（全自绘、无子控件、走主消息循环）
// ------------------------------------------------------------
// 之前用系统 "STATIC" 类当弹窗 + 局部 GetMessageW 循环：
// 子按钮的 BN_CLICKED 经 WM_COMMAND 同步 SendMessage 给父窗口，
// 被 STATIC 默认过程吞掉，局部循环收不到 → 所有按钮无反应。
// 现在改成独立窗口类 + 命中测试，所有交互在自己的 WndProc 内完成。
// ============================================================
static const int kDetailW = 580;
static const int kDetailH = 560;
static const wchar_t kDetailClass[] = L"LightGuiDllDetailWnd";

struct DetailState {
    GuiDllStoreItem item;
    HWND  hwnd = nullptr;
    HWND  owner = nullptr;
    Gdiplus::Bitmap* logo = nullptr;
    std::wstring desc;
    std::wstring badge;
    ARGB  badgeBg = 0xFFE8F1FF;
    ARGB  badgeFg = 0xFF2563EB;
    bool  installed = false;
    bool  enabled = false;
    bool  installing = false;
    bool  hasVideo = false;
    bool  confirmUninstall = false;
    int   scrollY = 0;
    int   maxScroll = 0;
    int   hover = -1;      // 0=右上关闭 1=介绍视频 2=主按钮 3=底部关闭
    int   pressed = -1;
    RECT  rcTopClose{};
    RECT  rcVideo{};
    RECT  rcMain{};
    RECT  rcDismiss{};
    std::function<void(int)> onAction;  // 1=安装 2=卸载 3=播放视频
};

static LRESULT CALLBACK DetailWndProc(HWND, UINT, WPARAM, LPARAM);

static void CloseDetailWindow() {
    if (g_detailState && IsWindow(g_detailState->hwnd))
        DestroyWindow(g_detailState->hwnd);
    g_detailState = nullptr;
}

static void EnsureDetailClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc = DetailWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kDetailClass;
    RegisterClassExW(&wc);
    registered = true;
}

static Gdiplus::Bitmap* LoadDetailLogo(const std::wstring& path) {
    if (path.empty() || !FileExistsW(path)) return nullptr;
    Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(path.c_str());
    if (bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return nullptr; }
    return bmp;
}

// 卸载：先卸载全部 GUI 附属 DLL 释放文件占用，再删除 dll 与伴生文件，最后重载。
static void DoUninstallGuiDll(const std::wstring& name) {
    GuiDllPluginHost* host = GuiDllPluginHost::Instance();
    host->UnloadAll();

    std::wstring dir = host->DllDir();
    DeleteFileW((dir + L"\\" + name + L".dll").c_str());
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\" + name + L".dll.*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    const wchar_t* exts[] = { L".txt", L".png", L".jpg", L".jpeg", L".mp4" };
    for (const wchar_t* e : exts)
        DeleteFileW((dir + L"\\" + name + e).c_str());

    host->Reload();
}

static int DetailHit(const DetailState* st, int x, int y) {
    const RECT* rects[] = { &st->rcTopClose, &st->rcVideo,
                            &st->rcMain, &st->rcDismiss };
    for (int i = 0; i < 4; ++i) {
        const RECT& r = *rects[i];
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
    }
    return -1;
}

static void DrawDetailButton(Gdiplus::Graphics& g, const RECT& r,
    const wchar_t* text, int kind, bool hot, bool pressed, bool disabled) {
    ARGB bg, fg, border;
    switch (kind) {
    case 0:  // 主色（安装）
        bg = disabled ? 0xFFBFCEE6 : (pressed ? 0xFF2A6BE0 : (hot ? 0xFF2F72E8 : 0xFF3B82F6));
        fg = 0xFFFFFFFF; border = bg; break;
    case 1:  // 危险（卸载）
        bg = disabled ? 0xFFF6F7F9 : (pressed ? 0xFFFFD9DE : (hot ? 0xFFFFE7EA : 0xFFFFF1F3));
        fg = disabled ? 0xFFB6BFCC : 0xFFDC2645; border = hot ? 0xFFF7C2CC : 0xFFF6D2D9; break;
    case 2:  // 次要（关闭）
        bg = pressed ? 0xFFDDE4EE : (hot ? 0xFFE7ECF4 : 0xFFF1F4F9);
        fg = 0xFF475569; border = 0xFFE4E9F1; break;
    default: // 幽灵（视频）
        bg = disabled ? 0xFFF6F8FB : (hot ? 0xFFDEEAFF : 0xFFEDF4FF);
        fg = disabled ? 0xFFB6BFCC : 0xFF2563EB; border = 0xFFDCE8FB; break;
    }
    RectF rf((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left),
        (REAL)(r.bottom - r.top));
    FillRounded(g, rf, 12.0f, bg);
    StrokeRounded(g, rf, 12.0f, border, 1.0f);
    Font f(L"Microsoft YaHei", 13, FontStyleBold, UnitPixel);
    DrawStringSimple(g, text, f, rf, fg, StringAlignmentCenter,
        StringAlignmentCenter, false);
}

static void DrawDetail(Gdiplus::Graphics& g, DetailState* st) {
    const REAL W = (REAL)kDetailW, H = (REAL)kDetailH;
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // 底色 + 顶部渐变 hero
    SolidBrush white(Color(0xFFFFFFFF));
    g.FillRectangle(&white, 0.0f, 0.0f, W, H);
    LinearGradientBrush hero(RectF(0, 0, W, 168.0f),
        Color(0xFFEAF2FF), Color(0xFFF8FBFF), LinearGradientModeVertical);
    g.FillRectangle(&hero, 0.0f, 0.0f, W, 168.0f);
    Pen div(Color(0xFFE3EAF6), 1.0f);
    g.DrawLine(&div, 32.0f, 168.0f, W - 32.0f, 168.0f);

    // 外框
    StrokeRounded(g, RectF(0.5f, 0.5f, W - 1, H - 1), 20.0f, 0xFFD8E3F4, 1.0f);

    // Logo（圆角裁剪）
    RectF logoR(32, 30, 96, 96);
    {
        GraphicsPath clip;
        MakeRoundedRect(logoR, 24.0f, clip);
        GraphicsContainer c = g.BeginContainer();
        g.SetClip(&clip);
        if (st->logo) {
            g.DrawImage(st->logo, logoR);
        }
        else {
            SolidBrush ph(Color(0xFFE4EEFF));
            g.FillRectangle(&ph, logoR);
            Font emoji(L"Segoe UI Emoji", 38, FontStyleRegular, UnitPixel);
            DrawStringSimple(g, L"\U0001F9E9", emoji,
                RectF(logoR.X, logoR.Y + 4, logoR.Width, logoR.Height - 4),
                0xFF3B82F6, StringAlignmentCenter);
        }
        g.EndContainer(c);
    }
    StrokeRounded(g, logoR, 24.0f, 0xFFD3E2FA, 1.0f);

    // 名称
    Font nameFont(L"Microsoft YaHei", 21, FontStyleBold, UnitPixel);
    DrawStringSimple(g, Ellipsis(st->item.name, 22), nameFont,
        RectF(148, 32, W - 210, 34), 0xFF16233A);

    // 状态徽章
    Font badgeFont(L"Microsoft YaHei", 11, FontStyleBold, UnitPixel);
    REAL badgeW = (REAL)(std::max)(78, 26 + (int)st->badge.size() * 13);
    RectF badgeR(148, 76, badgeW, 26);
    FillRounded(g, badgeR, 13.0f, st->badgeBg);
    DrawStringSimple(g, st->badge, badgeFont, badgeR, st->badgeFg,
        StringAlignmentCenter, StringAlignmentCenter, false);

    // 元信息
    Font metaFont(L"Microsoft YaHei", 11, FontStyleRegular, UnitPixel);
    DrawStringSimple(g, L"GUI 附属插件  ·  来源：插件商店", metaFont,
        RectF(148, 110, W - 210, 22), 0xFF7C8AA1);

    // 右上角关闭 X
    {
        RectF xr((REAL)st->rcTopClose.left, (REAL)st->rcTopClose.top,
            (REAL)(st->rcTopClose.right - st->rcTopClose.left),
            (REAL)(st->rcTopClose.bottom - st->rcTopClose.top));
        if (st->hover == 0) FillRounded(g, xr, 15.0f, 0xFFFFE3E7);
        Pen xp(Color(st->hover == 0 ? 0xFFE11D48 : 0xFF8493A8), 1.8f);
        g.DrawLine(&xp, xr.X + 10, xr.Y + 10, xr.X + 20, xr.Y + 20);
        g.DrawLine(&xp, xr.X + 20, xr.Y + 10, xr.X + 10, xr.Y + 20);
    }

    // 分区标题
    Font secFont(L"Microsoft YaHei", 14, FontStyleBold, UnitPixel);
    DrawStringSimple(g, L"插件介绍", secFont, RectF(32, 184, 240, 26), 0xFF1E293B);
    Font hintFont(L"Microsoft YaHei", 11, FontStyleRegular, UnitPixel);
    DrawStringSimple(g, L"滚动查看全部内容", hintFont,
        RectF(W - 172, 186, 140, 22), 0xFF9AA7B8, StringAlignmentFar);

    // 介绍卡片
    RectF cardR(32, 216, W - 64, 238);
    FillRounded(g, cardR, 14.0f, 0xFFFBFCFE);
    StrokeRounded(g, cardR, 14.0f, 0xFFE6ECF5, 1.0f);

    RectF textR(cardR.X + 18, cardR.Y + 16, cardR.Width - 40, cardR.Height - 32);
    Font bodyFont(L"Microsoft YaHei", 12.5f, FontStyleRegular, UnitPixel);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentNear);
    fmt.SetTrimming(StringTrimmingWord);
    RectF bound;
    g.MeasureString(st->desc.c_str(), -1, &bodyFont,
        RectF(0, 0, textR.Width, 100000.0f), &fmt, &bound);
    st->maxScroll = (std::max)(0, (int)(bound.Height - textR.Height) + 6);
    if (st->scrollY > st->maxScroll) st->scrollY = st->maxScroll;

    {
        GraphicsContainer c = g.BeginContainer();
        g.SetClip(RectF(cardR.X + 2, cardR.Y + 2,
            cardR.Width - 4, cardR.Height - 4));
        SolidBrush tb(Color(0xFF41536B));
        g.DrawString(st->desc.c_str(), -1, &bodyFont,
            RectF(textR.X, textR.Y - (REAL)st->scrollY,
                textR.Width, bound.Height + 40), &fmt, &tb);
        g.EndContainer(c);
    }
    // 细滚动条
    if (st->maxScroll > 0) {
        REAL trackX = cardR.X + cardR.Width - 9;
        REAL trackY = cardR.Y + 12, trackH = cardR.Height - 24;
        FillRounded(g, RectF(trackX, trackY, 3, trackH), 1.5f, 0x1A000000);
        REAL thumbH = (std::max)(30.0f, trackH * trackH / (trackH + st->maxScroll));
        REAL thumbY = trackY + (trackH - thumbH) * st->scrollY / st->maxScroll;
        FillRounded(g, RectF(trackX, thumbY, 3, thumbH), 1.5f, 0xFF9FB6DE);
    }

    // 提示行
    DrawStringSimple(g,
        L"提示：安装后可在「插件中心」启用 / 停用此插件", hintFont,
        RectF(32, 462, W - 64, 22), 0xFF8C9AAE);

    // 底部按钮
    bool disMain = st->installing;
    bool disVideo = !st->hasVideo;
    std::wstring mainLabel;
    int mainKind = 0;
    if (st->installing) { mainLabel = L"安装中…"; }
    else if (!st->installed) { mainLabel = L"一键安装"; mainKind = 0; }
    else if (st->confirmUninstall) { mainLabel = L"确认卸载？"; mainKind = 1; }
    else { mainLabel = L"卸载插件"; mainKind = 1; }

    DrawDetailButton(g, st->rcVideo, L"▶  介绍视频", 3,
        st->hover == 1, st->pressed == 1, disVideo);
    DrawDetailButton(g, st->rcMain, mainLabel.c_str(), mainKind,
        st->hover == 2, st->pressed == 2, disMain);
    DrawDetailButton(g, st->rcDismiss, L"关闭", 2,
        st->hover == 3, st->pressed == 3, false);
}

LRESULT CALLBACK DetailWndProc(HWND hWnd, UINT msg, WPARAM wParam,
    LPARAM lParam) {
    DetailState* st = (DetailState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        st = (DetailState*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)st);
        if (st) st->hwnd = hWnd;
        return TRUE;
    }
    if (!st) return DefWindowProcW(hWnd, msg, wParam, lParam);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP old = (HBITMAP)SelectObject(memDC, bmp);
        {
            Graphics g(memDC);
            DrawDetail(g, st);
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, old);
        DeleteObject(bmp);
        DeleteDC(memDC);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        if (DetailHit(st, pt.x, pt.y) == 0) return HTCLIENT;
        if (pt.y < 168) return HTCAPTION;   // hero 区可拖动
        return HTCLIENT;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int h = DetailHit(st, x, y);
        if (h != st->hover) {
            st->hover = h;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        SetCursor(LoadCursor(nullptr, h >= 0 ? IDC_HAND : IDC_ARROW));
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (st->hover != -1) {
            st->hover = -1;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        st->pressed = DetailHit(st, x, y);
        if (st->pressed >= 0) {
            SetCapture(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        ReleaseCapture();
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int hit = DetailHit(st, x, y);
        int act = st->pressed;
        st->pressed = -1;
        InvalidateRect(hWnd, nullptr, FALSE);
        if (hit != act || act < 0) return 0;

        // 注意：回调内部可能销毁本窗口（进而 delete st），
        // 因此必须先拷贝 std::function，避免在执行中销毁自身。
        auto invoke = [st](int action) {
            if (!st->onAction) return;
            auto cb = st->onAction;
            cb(action);
        };
        if (act == 0) { DestroyWindow(hWnd); return 0; }       // 右上 X
        if (act == 3) {                                        // 底部关闭
            DestroyWindow(hWnd); return 0;
        }
        if (act == 1) {                                        // 介绍视频
            if (st->hasVideo) invoke(3);
            return 0;
        }
        if (act == 2) {                                        // 主按钮
            if (st->installing) return 0;
            if (!st->installed) { invoke(1); return 0; }
            if (!st->confirmUninstall) {                       // 二次确认
                st->confirmUninstall = true;
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            invoke(2);
            return 0;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        st->scrollY -= delta / WHEEL_DELTA * 48;
        if (st->scrollY < 0) st->scrollY = 0;
        if (st->scrollY > st->maxScroll) st->scrollY = st->maxScroll;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(hWnd); return 0; }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        if (IsWindow(st->owner)) {
            EnableWindow(st->owner, TRUE);
            SetForegroundWindow(st->owner);
        }
        delete st->logo;
        if (g_detailState == st) g_detailState = nullptr;
        delete st;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void GuiDllStorePanel::ShowDetails(const GuiDllStoreItem& item) {
    // 单例：已打开则直接激活
    if (g_detailState && IsWindow(g_detailState->hwnd)) {
        SetForegroundWindow(g_detailState->hwnd);
        return;
    }
    EnsureDetailClass();

    DetailState* st = new DetailState();
    st->item = item;
    st->owner = m_hParent;
    st->installed = item.installed;
    st->enabled = item.enabled;
    st->installing = item.installing;
    st->hasVideo = (!item.videoUrl.empty() || !item.videoPath.empty());

    st->desc = item.description;
    if (st->desc.empty()) st->desc = GuiDllPluginHost::Instance()->Description(item.name);
    if (st->desc.empty()) st->desc = L"暂无介绍。该插件未提供说明文件。";

    if (item.installing) {
        st->badge = L"安装中…"; st->badgeBg = 0xFFE8F1FF; st->badgeFg = 0xFF2563EB;
    }
    else if (!item.installed) {
        st->badge = L"未安装"; st->badgeBg = 0xFFEDF1F7; st->badgeFg = 0xFF64748B;
    }
    else if (item.enabled) {
        st->badge = L"已启用"; st->badgeBg = 0xFFE1F9EA; st->badgeFg = 0xFF15803D;
    }
    else {
        st->badge = L"已停用"; st->badgeBg = 0xFFFFF3E0; st->badgeFg = 0xFFB45309;
    }

    std::wstring logoPath = item.logoPath;
    if (logoPath.empty()) {
        std::wstring dir = GuiDllPluginHost::Instance()->DllDir();
        if (FileExistsW(dir + L"\\" + item.name + L".png")) logoPath = dir + L"\\" + item.name + L".png";
        else if (FileExistsW(dir + L"\\" + item.name + L".jpg")) logoPath = dir + L"\\" + item.name + L".jpg";
    }
    st->logo = LoadDetailLogo(logoPath);

    st->rcTopClose = { kDetailW - 50, 18, kDetailW - 18, 50 };
    const int by = kDetailH - 64, bh = 42;
    st->rcVideo = { 32, by, 32 + 160, by + bh };
    st->rcMain = { kDetailW - 32 - 96 - 12 - 140, by,
                   kDetailW - 32 - 96 - 12, by + bh };
    st->rcDismiss = { kDetailW - 32 - 96, by, kDetailW - 32, by + bh };

    st->onAction = [this, st](int act) {
        HWND self = st->hwnd;
        if (act == 1) {                       // 一键安装
            InstallItem(st->item);
            if (IsWindow(self)) DestroyWindow(self);
        }
        else if (act == 2) {                  // 卸载
            std::wstring name = st->item.name;
            DoUninstallGuiDll(name);
            auto itc = m_logoCache.find(name);
            if (itc != m_logoCache.end()) {
                delete reinterpret_cast<Bitmap*>(itc->second);
                m_logoCache.erase(itc);
            }
            if (m_hWnd && IsWindow(m_hWnd)) {
                PostMessageW(m_hWnd, WM_YXGUI_STORE_INSTALL_DONE, 0,
                    (LPARAM)new std::wstring(L"已卸载「" + name + L"」"));
            }
            if (IsWindow(self)) DestroyWindow(self);
        }
        else if (act == 3) {                  // 播放介绍视频
            PlayVideo(st->item);
        }
    };

    RECT prc;
    GetWindowRect(m_hParent, &prc);
    int sx = prc.left + ((prc.right - prc.left) - kDetailW) / 2;
    int sy = prc.top + ((prc.bottom - prc.top) - kDetailH) / 2;
    RECT screen;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &screen, 0);
    if (sx < screen.left) sx = screen.left;
    if (sy < screen.top) sy = screen.top;
    if (sx + kDetailW > screen.right) sx = screen.right - kDetailW;
    if (sy + kDetailH > screen.bottom) sy = screen.bottom - kDetailH;

    HWND h = CreateWindowExW(0, kDetailClass, L"插件详情",
        WS_POPUP,
        sx, sy, kDetailW, kDetailH,
        m_hParent, NULL, GetModuleHandleW(NULL), st);
    if (!h) { delete st->logo; delete st; return; }

    HRGN rgn = CreateRoundRectRgn(0, 0, kDetailW + 1, kDetailH + 1, 22, 22);
    SetWindowRgn(h, rgn, TRUE);   // 系统接管 rgn，不需要 DeleteObject
    g_detailState = st;
    EnableWindow(m_hParent, FALSE);   // 模态（弹窗为顶层窗口，不受影响）
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    InvalidateRect(h, nullptr, FALSE);
}

// ---- 布局计算（Paint 与命中测试共用） ----
static void ComputeGrid(int clientW, int clientH, int& cols, int& cardW) {
    cols = clientW >= 960 ? 4 : (clientW >= 680 ? 3 : (clientW >= 430 ? 2 : 1));
    cardW = (clientW - kPadX * 2 - (cols - 1) * kGap) / cols;
    if (cardW < 120) cardW = 120;
}

void GuiDllStorePanel::Paint(HDC hdc) {
    if (!m_hWnd || !IsWindow(m_hWnd)) return;
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) return;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, cw, ch);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    {
        Graphics g(memDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        SolidBrush bg{ Color(kBg) };
        g.FillRectangle(&bg, 0, 0, cw, ch);

        // 顶部信息行
        Font infoFont(L"Microsoft YaHei", 11, FontStyleRegular, UnitPixel);
        std::wstring info = m_loading ? m_status :
            (L"共 " + std::to_wstring(m_filtered.size()) + L" 个插件");
        if (!m_repos.empty() && !m_repos[m_curRepo].display.empty())
            info += L"  ·  " + m_repos[m_curRepo].display;
        DrawStringSimple(g, Ellipsis(info, 90), infoFont,
            RectF((REAL)kPadX, (REAL)(ch - 20), (REAL)(cw - kPadX * 2), 18), kMuted);

        // 加载循环条
        if (m_loading) {
            DrawSpinner(&g, cw / 2, (kTopBarH + ch) / 2 - 30, 26);
            Font loadFont(L"Microsoft YaHei", 13, FontStyleRegular, UnitPixel);
            DrawStringSimple(g, m_status, loadFont,
                RectF((REAL)0, (REAL)((kTopBarH + ch) / 2 + 8), (REAL)cw, 24),
                kMuted, StringAlignmentCenter);
            // 不再绘制卡片
        }
        else if (m_filtered.empty()) {
            Font emptyFont(L"Microsoft YaHei", 13, FontStyleRegular, UnitPixel);
            DrawStringSimple(g, m_status.empty() ? L"仓库中暂无可安装插件" : m_status,
                emptyFont, RectF((REAL)0, (REAL)((kTopBarH + ch) / 2 - 20), (REAL)cw, 28),
                kMuted, StringAlignmentCenter);
        }
        else {
            int cols = 0, cardW = 0;
            ComputeGrid(cw, ch, cols, cardW);
            int gridH = ch - kTopBarH - 22;
            int rows = ((int)m_filtered.size() + cols - 1) / cols;
            int contentH = rows * (kCardH + kGap);
            int maxScroll = std::max(0, contentH - gridH);
            if (m_scrollY > maxScroll) m_scrollY = maxScroll;

            for (int i = 0; i < (int)m_filtered.size(); ++i) {
                int col = i % cols;
                int row = i / cols;
                int x = kPadX + col * (cardW + kGap);
                int y = kTopBarH + row * (kCardH + kGap) - m_scrollY;
                if (y + kCardH < kTopBarH) continue;
                if (y > ch) break;
                DrawCard(&g, m_items[m_filtered[i]], x, y, cardW, kCardH);
            }
        }
    }

    BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

void GuiDllStorePanel::DrawSpinner(void* gp, int x, int y, int r) {
    Graphics& g = *reinterpret_cast<Graphics*>(gp);
    float a0 = m_spin;
    Pen pen(Color(0xFF3B82F6), 4.0f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    for (int k = 0; k < 3; ++k) {
        float start = a0 + k * 100.0f;
        g.DrawArc(&pen, x - r, y - r, r * 2, r * 2, start, 70.0f);
    }
}

void GuiDllStorePanel::DrawPill(void* gp, const wchar_t* text, int x, int y,
    int w, int h, int kind) {
    Graphics& g = *reinterpret_cast<Graphics*>(gp);
    ARGB bgC, fgC;
    if (kind == 0) { bgC = kAccent; fgC = 0xFFFFFFFF; }                 // 安装
    else if (kind == 1) { bgC = kGreenSoft; fgC = 0xFF16A34A; }          // 已启用
    else if (kind == 2) { bgC = 0xFFF1F5F9; fgC = kMuted; }              // 已停用
    else { bgC = kAccentSoft; fgC = kAccent; }                           // 加载中
    RectF r((REAL)x, (REAL)y, (REAL)w, (REAL)h);
    FillRounded(g, r, (REAL)(h / 2.0f), bgC);
    Font f(L"Microsoft YaHei", 12, FontStyleBold, UnitPixel);
    DrawStringSimple(g, text, f, r, fgC, StringAlignmentCenter);
}

void GuiDllStorePanel::DrawCard(void* gp, const GuiDllStoreItem& item,
    int x, int y, int w, int h) {
    Graphics& g = *reinterpret_cast<Graphics*>(gp);
    bool hot = false;
    if (m_hoverCard >= 0 && m_hoverCard < (int)m_items.size())
        hot = (m_items[m_hoverCard].name == item.name);

    const RectF card((REAL)x, (REAL)y, (REAL)w, (REAL)h);
    FillRounded(g, RectF(card.X, card.Y + 1.5f, card.Width, card.Height), 14.0f, 0x0F1E3A5F);
    FillRounded(g, card, 14.0f, hot ? kCardHover : kCard);
    StrokeRounded(g, card, 14.0f, hot ? kBorderHover : kBorder);

    // Logo
    RectF logoR((REAL)(x + w / 2 - 32), (REAL)(y + 16), 64, 64);
    Bitmap* bmp = nullptr;
    auto itc = m_logoCache.find(item.name);
    if (itc != m_logoCache.end()) bmp = reinterpret_cast<Bitmap*>(itc->second);
    else if (!item.logoPath.empty()) {
        Bitmap* nb = new Bitmap(item.logoPath.c_str());
        if (nb && nb->GetLastStatus() == Gdiplus::Ok) {
            m_logoCache[item.name] = nb;
            bmp = nb;
        }
        else { delete nb; }
    }

    if (bmp) {
        // 圆角裁剪绘制
        GraphicsPath clip;
        MakeRoundedRect(logoR, 14.0f, clip);
        GraphicsContainer cont = g.BeginContainer();
        g.SetClip(&clip);
        g.DrawImage(bmp, logoR);
        g.EndContainer(cont);
    }
    else {
        FillRounded(g, logoR, 14.0f, kAccentSoft);
        Font iconFont(L"Segoe UI Emoji", 26, FontStyleRegular, UnitPixel);
        DrawStringSimple(g, L"🧩", iconFont,
            RectF(logoR.X, logoR.Y + 4, logoR.Width, logoR.Height - 4),
            kAccent, StringAlignmentCenter);
    }

    // 名称
    Font nameFont(L"Microsoft YaHei", 14, FontStyleBold, UnitPixel);
    DrawStringSimple(g, Ellipsis(item.name, 16), nameFont,
        RectF((REAL)(x + 10), (REAL)(y + 88), (REAL)(w - 20), 20), kText,
        StringAlignmentCenter);

    // 描述（两行）
    Font descFont(L"Microsoft YaHei", 10, FontStyleRegular, UnitPixel);
    std::wstring desc = item.description.empty() ? L"暂无介绍" : item.description;
    RectF descR((REAL)(x + 10), (REAL)(y + 112), (REAL)(w - 20), 30);
    SolidBrush db{ Color(kMuted) };
    StringFormat df;
    df.SetAlignment(StringAlignmentCenter);
    df.SetLineAlignment(StringAlignmentNear);
    df.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(desc.c_str(), -1, &descFont, descR, &df, &db);

    // 介绍视频链接
    bool hasVideo = (!item.videoUrl.empty() || !item.videoPath.empty());
    if (hasVideo) {
        Font vf(L"Microsoft YaHei", 10, FontStyleRegular, UnitPixel);
        DrawStringSimple(g, L"▶ 介绍视频", vf,
            RectF((REAL)(x + 10), (REAL)(y + 142), (REAL)(w - 20), 16),
            kAccent, StringAlignmentCenter);
    }

    // 主按钮
    int bw = w - 32;
    int bx = x + 16;
    int by = y + h - 44;
    int kind;
    const wchar_t* label;
    if (item.installing) { kind = 3; label = L"安装中…"; }
    else if (!item.installed) { kind = 0; label = L"一键安装"; }
    else { kind = 1; label = L"已安装"; }   // 商店只负责下载，启停交给插件中心
    DrawPill(&g, label, bx, by, bw, 30, kind);
}

int GuiDllStorePanel::CardAt(int x, int y, int& button) const {
    button = -1;
    if (!m_hWnd) return -1;
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    int cols = 0, cardW = 0;
    ComputeGrid(cw, ch, cols, cardW);

    for (int i = 0; i < (int)m_filtered.size(); ++i) {
        int col = i % cols;
        int row = i / cols;
        int cx = kPadX + col * (cardW + kGap);
        int cy = kTopBarH + row * (kCardH + kGap) - m_scrollY;
        if (x < cx || x >= cx + cardW) continue;
        if (y < cy || y >= cy + kCardH) continue;
        // 视频链接
        if (y >= cy + 138 && y <= cy + 158) button = 1;
        else if (y >= cy + kCardH - 48 && y <= cy + kCardH - 14) button = 0;
        else button = -1;
        return m_filtered[i];
    }
    return -1;
}

void GuiDllStorePanel::HandleClick(int x, int y) {
    int btn = -1;
    int idx = CardAt(x, y, btn);
    if (idx < 0 || idx >= (int)m_items.size()) return;
    const GuiDllStoreItem& item = m_items[idx];
    if (btn == 1) { PlayVideo(item); return; }   // 介绍视频
    if (btn == 0) {                              // “一键安装”按钮：只负责下载安装
        if (item.installing) return;
        if (!item.installed) InstallItem(item);
        return;
    }
    // 点击卡片其它区域 → 弹出详情窗口
    ShowDetails(item);
}

void GuiDllStorePanel::UpdateHover(int x, int y) {
    int btn = -1;
    int idx = CardAt(x, y, btn);
    if (idx != m_hoverCard || btn != m_hoverButton) {
        m_hoverCard = idx;
        m_hoverButton = btn;
        if (m_hWnd && IsWindow(m_hWnd)) InvalidateRect(m_hWnd, nullptr, FALSE);
    }
    if (m_hWnd && IsWindow(m_hWnd))
        SetCursor(LoadCursor(nullptr, (btn >= 0 || idx >= 0) ? IDC_HAND : IDC_ARROW));
}

LRESULT CALLBACK GuiDllStorePanel::WndProc(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    GuiDllStorePanel* self = (GuiDllStorePanel*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        self = (GuiDllStorePanel*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->m_hWnd = hWnd;
        return TRUE;
    }
    if (!self) return DefWindowProcW(hWnd, msg, wParam, lParam);
    return self->HandleMessage(hWnd, msg, wParam, lParam);
}

// 简单输入对话框（添加仓库链接/本地路径）
static bool PromptForRepoText(HWND parent, const std::wstring& title,
    std::wstring& out) {
    // 使用固定控件 ID
    const int ID_EDIT = 100, ID_OK = 101, ID_CANCEL = 102;
    int w = 460, h = 150;
    RECT prc;
    GetWindowRect(parent, &prc);
    int sx = prc.left + ((prc.right - prc.left) - w) / 2;
    int sy = prc.top + ((prc.bottom - prc.top) - h) / 2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"STATIC", title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        sx, sy, w, h, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (!dlg) return false;

    HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND lbl = CreateWindowExW(0, L"STATIC", L"输入 GitHub 仓库链接 / HTTP 基址 / 本地目录路径：",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 14, w - 32, 20, dlg, NULL, NULL, NULL);
    HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", out.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        16, 40, w - 32, 26, dlg, (HMENU)(INT_PTR)ID_EDIT, NULL, NULL);
    HWND bok = CreateWindowExW(0, L"BUTTON", L"确定",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        w - 190, h - 62, 80, 30, dlg, (HMENU)(INT_PTR)ID_OK, NULL, NULL);
    HWND bcancel = CreateWindowExW(0, L"BUTTON", L"取消",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        w - 100, h - 62, 80, 30, dlg, (HMENU)(INT_PTR)ID_CANCEL, NULL, NULL);
    SendMessageW(lbl, WM_SETFONT, (WPARAM)hf, TRUE);
    SendMessageW(ed, WM_SETFONT, (WPARAM)hf, TRUE);
    SendMessageW(bok, WM_SETFONT, (WPARAM)hf, TRUE);
    SendMessageW(bcancel, WM_SETFONT, (WPARAM)hf, TRUE);
    SetFocus(ed);
    SendMessageW(ed, EM_SETSEL, 0, -1);

    bool ok = false;
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
            int len = GetWindowTextLengthW(ed);
            std::wstring buf(len + 1, 0);
            GetWindowTextW(ed, &buf[0], len + 1);
            out = buf;
            ok = true;
            break;
        }
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) { ok = false; break; }
        if (m.message == WM_COMMAND && LOWORD(m.wParam) == ID_OK) {
            int len = GetWindowTextLengthW(ed);
            std::wstring buf(len + 1, 0);
            GetWindowTextW(ed, &buf[0], len + 1);
            out = buf;
            ok = true;
            break;
        }
        if (m.message == WM_COMMAND && LOWORD(m.wParam) == ID_CANCEL) { ok = false; break; }
        if (m.message == WM_CLOSE) { ok = false; break; }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    DestroyWindow(dlg);
    return ok && !out.empty();
}

void GuiDllStorePanel::AddRepoDialog() {
    std::wstring text;
    if (!PromptForRepoText(m_hParent, L"添加仓库 / 本地目录", text)) return;
    GuiDllRepo r = ParseRepoLink(text);
    if (r.type == 0 && (r.owner.empty() || r.repo.empty())) return;
    if (r.type == 1 && r.path.empty()) return;
    if (r.type == 2 && r.path.empty()) return;
    // 替换同名仓库
    for (auto& e : m_repos) if (e.display == r.display) { e = r; m_curRepo = (int)(&e - &m_repos[0]); StartLoad(); return; }
    m_repos.push_back(r);
    m_curRepo = (int)m_repos.size() - 1;
    StartLoad();
}

LRESULT GuiDllStorePanel::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam,
    LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        int cw = rc.right - rc.left;
        int searchW = std::max(120, cw - 160 - 36 - 24);
        MoveWindow(m_hSearch, 8, 8, searchW, 30, TRUE);
        MoveWindow(m_hAddRepo, 8 + searchW + 8, 8, 88, 30, TRUE);
        MoveWindow(m_hRefresh, 8 + searchW + 8 + 88 + 8, 8, 36, 30, TRUE);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        Paint(hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wParam == IDT_STORE_SPIN && m_spinActive) {
            m_spin += 9.0f;
            if (m_spin >= 360.0f) m_spin -= 360.0f;
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        UpdateHover(x, y);
        return 0;
    }

    case WM_MOUSELEAVE:
        m_hoverCard = -1;
        m_hoverButton = -1;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        HandleClick(x, y);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        m_scrollY -= delta / WHEEL_DELTA * 60;
        if (m_scrollY < 0) m_scrollY = 0;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == 1 && code == EN_CHANGE) {
            wchar_t buf[512] = { 0 };
            GetWindowTextW(m_hSearch, buf, 512);
            m_search = buf;
            RebuildFilter();
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        if (id == 2 && code == BN_CLICKED) { AddRepoDialog(); return 0; }
        if (id == 3 && code == BN_CLICKED) { StartLoad(); return 0; }
        return 0;
    }

    case WM_YXGUI_STORE_LOADED: {
        auto* items = reinterpret_cast<std::vector<GuiDllStoreItem>*>(wParam);
        auto* status = reinterpret_cast<std::wstring*>(lParam);
        OnLoadFinished(items, status);
        return 0;
    }

    case WM_YXGUI_STORE_INSTALL_DONE: {
        std::wstring* pStatus = reinterpret_cast<std::wstring*>(lParam);
        if (pStatus) {
            if (!pStatus->empty()) m_status = *pStatus;
            delete pStatus;
        }
        GuiDllPluginHost::Instance()->Reload();
        UpdateInstalledState();
        RebuildFilter();
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_DESTROY:
        m_hWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
