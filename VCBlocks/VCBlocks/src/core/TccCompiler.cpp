// TccCompiler.cpp
// 内置编译系统：TinyCC 已打包进 DLL 资源（libtcc.dll + include + libtcc1-64.a），
// 运行时提取到临时目录后加载，不依赖任何外部文件。
#include "TccCompiler.h"
#include "util.h"
#include "../../resource.h"
#include <windows.h>
#include <vector>

namespace vcb {

// libtcc 函数指针（与 libtcc.h 一致）
struct TCCState;
typedef TCCState* (*PFN_tcc_new)(void);
typedef void (*PFN_tcc_delete)(TCCState*);
typedef int (*PFN_tcc_set_output_type)(TCCState*, int);
typedef int (*PFN_tcc_add_include_path)(TCCState*, const char*);
typedef int (*PFN_tcc_add_library_path)(TCCState*, const char*);
typedef int (*PFN_tcc_compile_string)(TCCState*, const char*);
typedef int (*PFN_tcc_output_file)(TCCState*, const char*);
typedef void (*PFN_tcc_set_error_func)(TCCState*, void*, void (*)(void*, const char*));

#define TCC_OUTPUT_EXE 2
#define TCC_OUTPUT_DLL 3

namespace {

struct Api {
    HMODULE mod = nullptr;
    PFN_tcc_new tcc_new = nullptr;
    PFN_tcc_delete tcc_delete = nullptr;
    PFN_tcc_set_output_type tcc_set_output_type = nullptr;
    PFN_tcc_add_include_path tcc_add_include_path = nullptr;
    PFN_tcc_add_library_path tcc_add_library_path = nullptr;
    PFN_tcc_compile_string tcc_compile_string = nullptr;
    PFN_tcc_output_file tcc_output_file = nullptr;
    PFN_tcc_set_error_func tcc_set_error_func = nullptr;
};

Api g_api;
std::wstring g_tccRoot;

HMODULE DllModule() {
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&g_api, &h);
    return h;
}

std::wstring PluginDllDir() {
    wchar_t buf[MAX_PATH];
    HMODULE h = DllModule();
    if (h) GetModuleFileNameW(h, buf, MAX_PATH);
    else GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L".\\" : p.substr(0, pos + 1);
}

// 从 DLL 资源提取 TCC bundle 到临时目录，返回临时目录（含结尾 '\'）
std::wstring ExtractBundle(std::string& err) {
    HMODULE h = DllModule();
    if (!h) h = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(h, MAKEINTRESOURCEW(IDR_TCC_BUNDLE), MAKEINTRESOURCEW(10));
    if (!res) { err = "找不到 TCC 资源"; return L""; }
    HGLOBAL glob = LoadResource(h, res);
    const unsigned char* data = (const unsigned char*)LockResource(glob);
    DWORD size = SizeofResource(h, res);
    if (!data || size < 4) { err = "TCC 资源无效"; return L""; }

    // 提取到主程序 Cache\vC\tcc\（工作区可写目录，避免 %TEMP% 权限问题）
    std::wstring root = ExeDir() + L"Cache\\vC\\tcc\\";
    EnsureDir(root);

    size_t off = 0;
    auto rd32 = [&](size_t& p) -> uint32_t {
        if (p + 4 > size) return 0;
        uint32_t v = *(const uint32_t*)(data + p);
        p += 4;
        return v;
    };

    uint32_t count = rd32(off);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t pathLen = rd32(off);
        if (off + pathLen > size) break;
        std::string path((const char*)(data + off), pathLen);
        off += pathLen;
        uint32_t fsize = rd32(off);
        if (off + fsize > size) break;
        const unsigned char* fdata = data + off;
        off += fsize;

        // 统一路径分隔符为 '\'（bundle 内用 '/' 存储）
        std::wstring wpath = Utf8ToWide(path);
        for (auto& c : wpath) if (c == L'/') c = L'\\';
        std::wstring full = root + wpath;
        size_t slash = full.find_last_of(L'\\');
        if (slash != std::wstring::npos) EnsureDir(full.substr(0, slash + 1));
        WriteFileBytes(full, std::vector<unsigned char>(fdata, fdata + fsize));
    }
    return root;
}

std::string WideToAnsi(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

} // namespace

bool TccCompiler::Init(std::string& err) {
    if (g_api.mod) return true;

    // 1. 从 DLL 资源提取 TCC 到临时目录
    std::wstring root = ExtractBundle(err);
    HMODULE mod = nullptr;
    if (!root.empty()) {
        mod = LoadLibraryW((root + L"libtcc.dll").c_str());
        if (mod) g_tccRoot = root;
    }
    // 2. 回退：DLL 旁 tcc 目录（开发环境）
    if (!mod) {
        std::wstring dllDir = PluginDllDir();
        mod = LoadLibraryW((dllDir + L"tcc\\libtcc.dll").c_str());
        if (mod) g_tccRoot = dllDir + L"tcc\\";
    }
    if (!mod) {
        if (err.empty()) err = "加载 libtcc.dll 失败";
        return false;
    }

    auto get = [&](const char* name) { return (FARPROC)GetProcAddress(mod, name); };
    g_api.mod = mod;
    g_api.tcc_new = (PFN_tcc_new)get("tcc_new");
    g_api.tcc_delete = (PFN_tcc_delete)get("tcc_delete");
    g_api.tcc_set_output_type = (PFN_tcc_set_output_type)get("tcc_set_output_type");
    g_api.tcc_add_include_path = (PFN_tcc_add_include_path)get("tcc_add_include_path");
    g_api.tcc_add_library_path = (PFN_tcc_add_library_path)get("tcc_add_library_path");
    g_api.tcc_compile_string = (PFN_tcc_compile_string)get("tcc_compile_string");
    g_api.tcc_output_file = (PFN_tcc_output_file)get("tcc_output_file");
    g_api.tcc_set_error_func = (PFN_tcc_set_error_func)get("tcc_set_error_func");

    if (!g_api.tcc_new || !g_api.tcc_compile_string || !g_api.tcc_output_file) {
        err = "libtcc.dll 缺少必要导出";
        FreeLibrary(mod);
        g_api = Api();
        return false;
    }
    return true;
}

bool TccCompiler::IsAvailable() { return g_api.mod != nullptr; }
std::wstring TccCompiler::TccRootDir() { return g_tccRoot; }

static bool DoCompile(const std::string& code, const std::wstring& outPath,
                      int outType, std::string& log, const std::wstring& extraInclude) {
    if (!g_api.mod) return false;
    log.clear();

    std::string outAnsi = WideToAnsi(outPath);
    std::string incRoot = WideToAnsi(g_tccRoot + L"include");
    std::string libRoot = WideToAnsi(g_tccRoot + L"lib");
    std::string extraInc = WideToAnsi(extraInclude);

    TCCState* s = g_api.tcc_new();
    if (!s) { log = "tcc_new failed"; return false; }

    std::string* errBuf = new std::string();
    g_api.tcc_set_error_func(s, errBuf, [](void* opaque, const char* msg) {
        std::string* b = (std::string*)opaque;
        *b += msg;
        *b += "\n";
    });

    bool ok = true;
    g_api.tcc_set_output_type(s, outType);
    g_api.tcc_add_include_path(s, incRoot.c_str());
    if (!extraInc.empty()) g_api.tcc_add_include_path(s, extraInc.c_str());
    g_api.tcc_add_library_path(s, libRoot.c_str());

    if (g_api.tcc_compile_string(s, code.c_str()) == -1) {
        log = "compile error:\n" + *errBuf;
        ok = false;
    }
    if (ok && g_api.tcc_output_file(s, outAnsi.c_str()) == -1) {
        log = "output error:\n" + *errBuf;
        ok = false;
    }

    if (!errBuf->empty() && ok) log = *errBuf;
    delete errBuf;
    g_api.tcc_delete(s);
    return ok;
}

bool TccCompiler::CompileToExe(const std::string& code, const std::wstring& outPath, std::string& log,
                               const std::wstring& extraInclude) {
    return DoCompile(code, outPath, TCC_OUTPUT_EXE, log, extraInclude);
}
bool TccCompiler::CompileToDll(const std::string& code, const std::wstring& outPath, std::string& log,
                               const std::wstring& extraInclude) {
    return DoCompile(code, outPath, TCC_OUTPUT_DLL, log, extraInclude);
}

} // namespace vcb
