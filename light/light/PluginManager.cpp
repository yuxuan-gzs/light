// PluginManager.cpp
#define _CRT_SECURE_NO_WARNINGS
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#include "PluginManager.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <set>
#include <vector>
#include <shlwapi.h>

#include <wintrust.h>
#include <softpub.h>

// 包含 minizip 头文件
#include "../../../zlib-1.3.2/contrib/minizip/unzip.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "wintrust.lib")

namespace fs = std::filesystem;

#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif

PluginManager* PluginManager::s_instance = nullptr;

namespace {

std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string ExeDirectory() {
    wchar_t buf[MAX_PATH * 4] = { 0 };
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)_countof(buf));
    if (n == 0) return "";
    fs::path p(buf);
    return p.parent_path().string();
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
        nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
        &out[0], len, nullptr, nullptr);
    return out;
}

bool ReadWholeFile(const fs::path& path, std::vector<unsigned char>& data) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size <= 0 || size > (std::streamoff)(64 * 1024 * 1024)) return false;
    in.seekg(0, std::ios::beg);
    data.resize((size_t)size);
    in.read(reinterpret_cast<char*>(data.data()), (std::streamsize)size);
    return in.good() || in.eof();
}

bool RvaToOffset(const std::vector<unsigned char>& data, DWORD rva, DWORD& offset) {
    if (data.size() < sizeof(IMAGE_DOS_HEADER)) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
    if ((size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > data.size()) return false;

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        data.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    if (rva < nt->OptionalHeader.SizeOfHeaders) {
        offset = rva;
        return true;
    }

    const auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sec[i];
        DWORD va = s.VirtualAddress;
        DWORD span = std::max<DWORD>(s.Misc.VirtualSize, s.SizeOfRawData);
        if (rva >= va && rva < va + span) {
            DWORD delta = rva - va;
            if (delta >= s.SizeOfRawData) return false;
            offset = s.PointerToRawData + delta;
            return offset < data.size();
        }
    }
    return false;
}

struct PeImportInfo {
    bool valid = false;
    bool is64 = false;
    std::string reason;
    std::vector<std::string> imports;
};

bool ReadPeImports(const std::string& path, PeImportInfo& out) {
    std::vector<unsigned char> data;
    if (!ReadWholeFile(fs::u8path(path), data)) {
        out.reason = "无法读取文件";
        return false;
    }
    if (data.size() < sizeof(IMAGE_DOS_HEADER)) {
        out.reason = "不是有效的 PE 文件";
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > data.size()) {
        out.reason = "PE DOS 头无效";
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        data.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        out.reason = "PE NT 头无效";
        return false;
    }

    WORD machine = nt->FileHeader.Machine;
    out.is64 = (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
#ifdef _WIN64
    const WORD expected = IMAGE_FILE_MACHINE_AMD64;
#else
    const WORD expected = IMAGE_FILE_MACHINE_I386;
#endif
    if (machine != expected) {
        out.reason = "插件位数与 light.exe 不一致";
        return false;
    }

    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0 || dir.Size == 0) {
        out.valid = true;
        return true;   // 没有导入表在技术上仍可加载
    }

    DWORD importOff = 0;
    if (!RvaToOffset(data, dir.VirtualAddress, importOff)) {
        out.reason = "PE 导入表地址无效";
        return false;
    }

    const auto* imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        data.data() + importOff);
    const size_t maxDesc = dir.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR) + 1;
    for (size_t i = 0; i < maxDesc; ++i, ++imp) {
        if (imp->Name == 0) break;
        DWORD nameOff = 0;
        if (!RvaToOffset(data, imp->Name, nameOff)) continue;
        const char* name = reinterpret_cast<const char*>(data.data() + nameOff);
        size_t maxLen = data.size() - nameOff;
        size_t len = 0;
        while (len < maxLen && name[len] != '\0') ++len;
        if (len == 0 || len > 260) continue;
        out.imports.push_back(std::string(name, len));
    }

    out.valid = true;
    return true;
}

bool IsSignedTrusted(const std::string& path) {
    std::wstring wpath = Utf8ToWide(path);
    if (wpath.empty()) return false;

    WINTRUST_FILE_INFO fileInfo = {};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = wpath.c_str();

    WINTRUST_DATA data = {};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &fileInfo;
    data.dwStateAction = WTD_STATEACTION_VERIFY;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust(nullptr, &action, &data);

    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &data);

    return status == ERROR_SUCCESS;
}

bool IsDangerousImport(const std::string& nameLower) {
    static const char* kBlocked[] = {
        "createremotethread",
        "createremotethreadex",
        "ntcreatethreadex",
        "writeprocessmemory",
        "ntwritevirtualmemory",
        "virtualallocex",
        "queueuserapc",
        "rtlcreateuserthread",
        "setwindowshookexa",
        "setwindowshookexw",
        "ntmapviewofsection",
        "zwmapviewofsection",
        "setthreadcontext",
        "ntsetcontextthread",
        "process32firstw",
        "process32nextw"
    };
    for (const char* item : kBlocked) {
        if (nameLower == item) return true;
    }
    return false;
}

bool ImportTableDangerous(const std::string& path, const std::string& basename,
    std::string& reason) {
    std::vector<unsigned char> data;
    if (!ReadWholeFile(fs::u8path(path), data)) return false;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
    if (data.size() < sizeof(IMAGE_DOS_HEADER) ||
        dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > data.size()) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        data.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0 || dir.Size == 0) return false;

    DWORD importOff = 0;
    if (!RvaToOffset(data, dir.VirtualAddress, importOff)) return false;

    const auto* imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        data.data() + importOff);
    const size_t maxDesc = dir.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR) + 1;

    for (size_t i = 0; i < maxDesc; ++i, ++imp) {
        if (imp->Name == 0) break;

        DWORD dllNameOff = 0;
        if (!RvaToOffset(data, imp->Name, dllNameOff)) continue;

        // 导入名称表：OriginalFirstThunk 或 FirstThunk
        DWORD thunkRva = imp->OriginalFirstThunk ? imp->OriginalFirstThunk
                                                 : imp->FirstThunk;
        DWORD thunkOff = 0;
        if (thunkRva == 0 || !RvaToOffset(data, thunkRva, thunkOff)) continue;

        for (int k = 0; k < 4096; ++k) {
            size_t entrySize = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
                ? sizeof(ULONGLONG) : sizeof(DWORD);
            size_t pos = thunkOff + (size_t)k * entrySize;
            if (pos + entrySize > data.size()) break;

            ULONGLONG value = 0;
            if (entrySize == sizeof(ULONGLONG)) {
                value = *reinterpret_cast<const ULONGLONG*>(data.data() + pos);
            }
            else {
                value = *reinterpret_cast<const DWORD*>(data.data() + pos);
            }
            if (value == 0) break;

            ULONGLONG ordinalFlag =
                (entrySize == sizeof(ULONGLONG)) ? 0x8000000000000000ULL : 0x80000000ULL;
            if ((value & ordinalFlag) != 0) continue;   // 按序号导入，无名称

            DWORD hintOff = 0;
            if (!RvaToOffset(data, (DWORD)(value & 0x7FFFFFFF), hintOff)) continue;
            if (hintOff + 2 >= data.size()) continue;
            const char* fn = reinterpret_cast<const char*>(data.data() + hintOff + 2);
            size_t maxLen = data.size() - (hintOff + 2);
            size_t len = 0;
            while (len < maxLen && fn[len] != '\0' && len < 256) ++len;
            if (len == 0) continue;

            std::string fnLower = ToLowerAscii(std::string(fn, len));
            if (IsDangerousImport(fnLower)) {
                reason = "检测到高风险导入: " + std::string(fn, len) +
                    " (" + basename + ")";
                return true;
            }
        }
    }
    return false;
}

// 在程序目录 / plugins / api_deps 中查找附属 DLL（不搜索当前工作目录）
std::string FindLocalDependency(const std::string& dllName,
    const std::string& pluginDir) {
    std::string base = ExeDirectory();
    std::vector<fs::path> candidates = {
        fs::u8path(pluginDir) / fs::u8path(dllName),
        fs::u8path(base) / fs::u8path(dllName),
        fs::u8path(base) / "plugins" / fs::u8path(dllName),
        fs::u8path(base) / "api_deps" / fs::u8path(dllName),
        fs::u8path(base) / "lib" / fs::u8path(dllName),
    };
    for (const auto& p : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) return p.string();
    }
    return "";
}

bool ScanRecursive(const std::string& dllPath, int depth,
    std::set<std::string>& visited, ApiPluginScanResult& result) {
    if (depth > 5) {
        result.reason = "附属插件引用层级过深";
        return false;
    }

    std::string normalized = ToLowerAscii(fs::absolute(fs::u8path(dllPath)).string());
    if (visited.count(normalized)) return true;
    visited.insert(normalized);

    PeImportInfo pe;
    if (!ReadPeImports(dllPath, pe)) {
        result.reason = pe.reason.empty() ? "不是有效的 PE 插件" : pe.reason;
        return false;
    }

    for (const auto& imp : pe.imports) {
        std::string base = ToLowerAscii(fs::u8path(imp).filename().string());
        if (base.empty()) continue;

        // 只把 .dll 当作附属插件；系统 DLL 交给 OS。
        if (base.size() < 4 || base.substr(base.size() - 4) != ".dll") continue;

        std::string pluginDir = fs::u8path(dllPath).parent_path().string();
        std::string local = FindLocalDependency(imp, pluginDir);
        if (local.empty()) continue;

        std::string localLower = ToLowerAscii(local);
        bool already = false;
        for (const auto& dep : result.localDependencies) {
            if (ToLowerAscii(dep) == localLower) { already = true; break; }
        }
        if (!already) result.localDependencies.push_back(local);

        if (!ScanRecursive(local, depth + 1, visited, result)) return false;
    }

    if (ImportTableDangerous(dllPath,
        ToLowerAscii(fs::u8path(dllPath).filename().string()), result.reason)) {
        return false;
    }
    return true;
}

bool PreloadLocalDependencies(const std::vector<std::string>& deps) {
    bool allOk = true;
    for (const auto& dep : deps) {
        HMODULE h = LoadLibraryExW(fs::u8path(dep).wstring().c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!h) {
            std::string msg = "[PluginManager] 附属插件加载失败: " + dep + "\n";
            OutputDebugStringA(msg.c_str());
            allOk = false;
        }
    }
    return allOk;
}

HMODULE LoadPluginModule(const std::string& dllPath) {
    std::wstring wpath = fs::u8path(dllPath).wstring();
    HMODULE h = LoadLibraryExW(wpath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h) {
        // 某些精简系统不支持 LOAD_LIBRARY_SEARCH_*，回退到默认搜索。
        h = LoadLibraryW(wpath.c_str());
    }
    return h;
}

} // namespace

PluginManager* PluginManager::GetInstance() {
    if (!s_instance) {
        s_instance = new PluginManager();
    }
    return s_instance;
}

PluginManager::~PluginManager() {
    UnloadAllPlugins(true);
}

// ============================================================
// ========== GUI 相关方法实现 ==========
// ============================================================

void PluginManager::SetUIManager(PluginUIManager* ui) {
    // 锁外回调插件，避免插件在 SetUIManager 里反向调用 PluginManager 造成死锁。
    std::vector<IPlugin*> instances;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_uiManager = ui;
        for (auto& pair : m_plugins) {
            if (pair.second.instance) {
                instances.push_back(pair.second.instance);
            }
        }
    }
    for (auto* instance : instances) {
        instance->SetUIManager(ui);
    }
}

PluginManager::GuiPluginEntry PluginManager::FindGuiPlugin() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& pair : m_plugins) {
        if (pair.second.guiProvider != nullptr) {
            GuiPluginEntry entry;
            entry.plugin = pair.second.instance;
            entry.provider = pair.second.guiProvider;
            entry.pluginId = pair.first;
            return entry;
        }
    }
    GuiPluginEntry empty;
    empty.plugin = nullptr;
    empty.provider = nullptr;
    empty.pluginId = "";
    return empty;
}

void PluginManager::RegisterGuiProvider(const std::string& pluginId,
    IGuiProvider* provider) {
    // 先在锁外取标题，再只对 map 加锁，避免持有锁调用插件代码。
    std::string title;
    if (provider) title = provider->GetWindowTitle();

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_plugins.find(pluginId);
    if (it != m_plugins.end()) {
        it->second.guiProvider = provider;
    }
    else if (provider) {
        LoadedPlugin loaded;
        loaded.instance = nullptr;
        loaded.guiProvider = provider;
        loaded.info.id = pluginId;
        loaded.info.name = title;
        if (loaded.info.name.empty()) loaded.info.name = pluginId;
        m_plugins[pluginId] = loaded;
    }
}

void PluginManager::SetLoadCallback(const PluginLoadCallback& callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = callback;
}

void PluginManager::ClearCallbacks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = PluginLoadCallback{};
}

std::string PluginManager::GetTempPluginDir(const std::string& pluginId) {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    return std::string(tempPath) + "LightPlugins\\" + pluginId + "\\";
}

void PluginManager::CleanupTempDirs() {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string baseDir = std::string(tempPath) + "LightPlugins\\";
    std::error_code ec;
    if (fs::exists(baseDir, ec)) {
        fs::remove_all(fs::u8path(baseDir), ec);
    }
}

bool PluginManager::ExtractZipToTemp(const std::string& zipPath,
    std::string& tempDir) {
    std::string pluginId = "plugin_" + std::to_string(GetTickCount64());
    tempDir = GetTempPluginDir(pluginId);

    std::error_code ec;
    fs::remove_all(fs::u8path(tempDir), ec);
    fs::create_directories(fs::u8path(tempDir), ec);

    unzFile zipFile = unzOpen(zipPath.c_str());
    if (!zipFile) return false;

    unz_global_info globalInfo;
    if (unzGetGlobalInfo(zipFile, &globalInfo) != UNZ_OK) {
        unzClose(zipFile);
        return false;
    }

    char filename[512];
    unz_file_info fileInfo;

    if (unzGoToFirstFile(zipFile) != UNZ_OK) {
        unzClose(zipFile);
        return false;
    }

    do {
        if (unzGetCurrentFileInfo(zipFile, &fileInfo, filename,
            sizeof(filename), NULL, 0, NULL, 0) != UNZ_OK) {
            break;
        }

        std::string entryName = filename;
        // 阻断 Zip Slip：拒绝绝对路径 / 上级目录跳转
        if (entryName.find("..") != std::string::npos ||
            entryName.find(':') != std::string::npos ||
            (!entryName.empty() && (entryName[0] == '\\' || entryName[0] == '/'))) {
            continue;
        }

        std::string outputPath = tempDir + entryName;
        std::replace(outputPath.begin(), outputPath.end(), '/', '\\');

        bool isDir = (!entryName.empty() &&
            (entryName.back() == '/' || entryName.back() == '\\'));
        if (isDir) {
            fs::create_directories(fs::u8path(outputPath), ec);
        }
        else {
            fs::create_directories(fs::u8path(outputPath).parent_path(), ec);

            if (unzOpenCurrentFile(zipFile) == UNZ_OK) {
                FILE* outFile = nullptr;
                fopen_s(&outFile, outputPath.c_str(), "wb");
                if (outFile) {
                    char buffer[8192];
                    int bytesRead;
                    while ((bytesRead = unzReadCurrentFile(zipFile, buffer,
                        sizeof(buffer))) > 0) {
                        fwrite(buffer, 1, bytesRead, outFile);
                    }
                    fclose(outFile);
                }
                unzCloseCurrentFile(zipFile);
            }
        }
    } while (unzGoToNextFile(zipFile) == UNZ_OK);

    unzClose(zipFile);

    bool foundDll = false;
    for (const auto& entry : fs::recursive_directory_iterator(fs::u8path(tempDir), ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".dll") {
            foundDll = true;
            break;
        }
    }

    if (!foundDll) {
        fs::remove_all(fs::u8path(tempDir), ec);
        return false;
    }

    m_tempDirs[pluginId] = tempDir;
    return true;
}

bool PluginManager::LoadPluginFromZip(const std::string& zipPath, bool silent) {
    if (!fs::exists(fs::u8path(zipPath))) {
        if (!silent && m_callback.onLoad) {
            m_callback.onLoad(fs::u8path(zipPath).filename().string(),
                false, "文件不存在");
        }
        return false;
    }

    std::string tempDir;
    if (!ExtractZipToTemp(zipPath, tempDir)) {
        if (!silent && m_callback.onLoad) {
            m_callback.onLoad(fs::u8path(zipPath).filename().string(),
                false, "解压失败");
        }
        return false;
    }

    std::string dllPath;
    std::vector<std::string> searchPaths = {
        tempDir,
        tempDir + "bin\\",
        tempDir + "plugins\\",
        tempDir + "lib\\",
    };

    std::error_code ec;
    for (const auto& searchPath : searchPaths) {
        if (!fs::exists(fs::u8path(searchPath), ec)) continue;
        for (const auto& entry : fs::directory_iterator(fs::u8path(searchPath), ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".dll") {
                dllPath = entry.path().string();
                break;
            }
        }
        if (!dllPath.empty()) break;
    }

    if (dllPath.empty()) {
        fs::remove_all(fs::u8path(tempDir), ec);
        if (!silent && m_callback.onLoad) {
            m_callback.onLoad(fs::u8path(zipPath).filename().string(),
                false, "未找到 DLL 文件");
        }
        return false;
    }

    std::string pluginId = "plugin_" + std::to_string(GetTickCount64());
    bool ok = LoadPluginDll(dllPath, pluginId, silent, false);
    if (!ok) {
        fs::remove_all(fs::u8path(tempDir), ec);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tempDirs.erase(pluginId);
    }
    return ok;
}

bool PluginManager::IsDllAlreadyLoaded(const std::string& dllPath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string normalized = ToLowerAscii(fs::absolute(fs::u8path(dllPath)).string());
    for (const auto& pair : m_plugins) {
        if (!pair.second.dllPath.empty() &&
            ToLowerAscii(fs::absolute(fs::u8path(pair.second.dllPath)).string()) ==
            normalized) {
            return true;
        }
    }
    return false;
}

bool PluginManager::LoadPluginDll(const std::string& dllPath,
    const std::string& pluginId, bool silent, bool apiPlugin) {
    if (IsDllAlreadyLoaded(dllPath)) {
        return false;
    }

    HMODULE hModule = LoadPluginModule(dllPath);
    if (!hModule) {
        if (!silent && m_callback.onLoad) {
            m_callback.onLoad(fs::u8path(dllPath).filename().string(),
                false, "加载 DLL 失败 (错误码 " + std::to_string(GetLastError()) + ")");
        }
        return false;
    }

    auto createFunc = (IPlugin * (*)())GetProcAddress(hModule, "CreatePlugin");
    auto createGuiFunc = (IGuiProvider * (*)())GetProcAddress(hModule,
        "CreateGuiProvider");
    auto destroyGuiFunc = (void (*)(IGuiProvider*))GetProcAddress(hModule,
        "DestroyGuiProvider");

    // ============================================================
    // 外置 GUI-only 插件：没有 CreatePlugin，只有 CreateGuiProvider。
    // 旧实现要求 CreatePlugin 必须存在，导致这类 GUI 插件永远加载失败。
    // ============================================================
    if (!createFunc) {
        if (!createGuiFunc) {
            FreeLibrary(hModule);
            if (!silent && m_callback.onLoad) {
                m_callback.onLoad(fs::u8path(dllPath).filename().string(),
                    false, "未找到 CreatePlugin / CreateGuiProvider 导出函数");
            }
            return false;
        }

        IGuiProvider* provider = createGuiFunc();
        if (!provider) {
            FreeLibrary(hModule);
            if (!silent && m_callback.onLoad) {
                m_callback.onLoad(fs::u8path(dllPath).filename().string(),
                    false, "创建 GUI 提供者失败");
            }
            return false;
        }

        LoadedPlugin loaded;
        loaded.handle = hModule;
        loaded.instance = nullptr;
        loaded.dllPath = dllPath;
        loaded.guiProvider = provider;
        loaded.isGuiOnly = true;
        loaded.destroyGuiProvider = destroyGuiFunc;
        loaded.info.id = "gui." + fs::u8path(dllPath).stem().string();
        loaded.info.name = provider->GetWindowTitle();
        if (loaded.info.name.empty()) {
            loaded.info.name = fs::u8path(dllPath).stem().string();
        }
        loaded.info.version = "1.0.0";
        loaded.info.author = "External GUI";
        loaded.info.description = "外置 GUI 插件";

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_plugins.count(loaded.info.id)) {
                if (destroyGuiFunc) destroyGuiFunc(provider);
                FreeLibrary(hModule);
                if (!silent && m_callback.onLoad) {
                    m_callback.onLoad(loaded.info.name, false, "插件已加载");
                }
                return false;
            }
            m_plugins[loaded.info.id] = loaded;
        }

        if (!silent && m_callback.onLoad) {
            m_callback.onLoad(loaded.info.name, true, "");
        }
        return true;
    }

    IPlugin* plugin = createFunc();
    if (!plugin) {
        FreeLibrary(hModule);
        if (!silent && m_callback.onLoad) {
            m_callback.onLoad(fs::u8path(dllPath).filename().string(),
                false, "创建插件实例失败");
        }
        return false;
    }

    PluginInfo info = plugin->GetInfo();
    if (info.id.empty()) {
        info.id = "plugin." + fs::u8path(dllPath).stem().string();
    }
    if (info.name.empty()) {
        info.name = fs::u8path(dllPath).stem().string();
    }

    // 重复检查在 Initialize 之前完成，避免初始化副作用。
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_plugins.count(info.id)) {
            plugin->Shutdown();
            delete plugin;
            FreeLibrary(hModule);
            if (!silent && m_callback.onLoad) {
                m_callback.onLoad(info.name, false,
                    "插件已加载 (ID: " + info.id + ")");
            }
            return false;
        }
    }

    if (!plugin->Initialize()) {
        plugin->Shutdown();
        delete plugin;
        FreeLibrary(hModule);
        if (!silent && m_callback.onLoad) {
            m_callback.onLoad(info.name, false, "插件初始化失败");
        }
        return false;
    }

    plugin->OnLoad();

    LoadedPlugin loaded;
    loaded.handle = hModule;
    loaded.instance = plugin;
    loaded.info = info;
    loaded.dllPath = dllPath;
    loaded.isApiPlugin = apiPlugin;
    loaded.isFromZip = false;

    if (createGuiFunc) {
        loaded.guiProvider = createGuiFunc();
        loaded.destroyGuiProvider = destroyGuiFunc;
    }

    if (m_uiManager) {
        plugin->SetUIManager(m_uiManager);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_tempDirs.find(pluginId);
        if (it != m_tempDirs.end()) {
            loaded.extractPath = it->second;
            loaded.isFromZip = true;
        }
        m_plugins[info.id] = loaded;
    }

    if (!silent && m_callback.onLoad) {
        m_callback.onLoad(info.name, true, "");
    }
    return true;
}

bool PluginManager::LoadPlugin(const std::string& pluginPath, bool silent) {
    if (pluginPath.empty()) return false;

    std::error_code ec;
    if (!fs::exists(fs::u8path(pluginPath), ec)) {
        if (!silent && m_callback.onLoad) {
            m_callback.onLoad(fs::u8path(pluginPath).filename().string(),
                false, "文件不存在");
        }
        return false;
    }

    std::string ext = ToLowerAscii(fs::u8path(pluginPath).extension().string());

    if (ext == ".zip") {
        return LoadPluginFromZip(pluginPath, silent);
    }
    if (ext == ".dll") {
        std::string stem = ToLowerAscii(fs::u8path(pluginPath).stem().string());
        bool api = stem.rfind("api_", 0) == 0;
        std::string pluginId = "plugin_" + std::to_string(GetTickCount64());
        return LoadPluginDll(pluginPath, pluginId, silent, api);
    }

    if (!silent && m_callback.onLoad) {
        m_callback.onLoad(fs::u8path(pluginPath).filename().string(),
            false, "不支持的文件格式 (仅支持 .zip 和 .dll)");
    }
    return false;
}

int PluginManager::LoadPluginsFromDirectory(const std::string& dirPath,
    bool silent) {
    std::error_code ec;
    if (!fs::exists(fs::u8path(dirPath), ec)) return 0;

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(fs::u8path(dirPath), ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = ToLowerAscii(entry.path().extension().string());
        if (ext == ".zip" || ext == ".dll") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    int loaded = 0;
    for (const auto& file : files) {
        if (LoadPlugin(file.string(), silent)) ++loaded;
    }
    return loaded;
}

int PluginManager::LoadPluginsFromDirectoryWithUI(const std::string& dirPath) {
    std::error_code ec;
    if (!fs::exists(fs::u8path(dirPath), ec)) {
        if (m_callback.onBatchProgress) m_callback.onBatchProgress(0, 0, 0);
        return 0;
    }

    std::vector<fs::path> pluginFiles;
    for (const auto& entry : fs::directory_iterator(fs::u8path(dirPath), ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = ToLowerAscii(entry.path().extension().string());
        if (ext == ".zip" || ext == ".dll") pluginFiles.push_back(entry.path());
    }
    std::sort(pluginFiles.begin(), pluginFiles.end());

    if (pluginFiles.empty()) {
        if (m_callback.onBatchProgress) m_callback.onBatchProgress(0, 0, 0);
        return 0;
    }

    int loaded = 0;
    int failed = 0;
    int total = (int)pluginFiles.size();
    if (m_callback.onBatchProgress) m_callback.onBatchProgress(total, 0, 0);

    for (size_t i = 0; i < pluginFiles.size(); i++) {
        std::cout << "\n[插件] (" << (i + 1) << "/" << total << ") 加载: "
            << pluginFiles[i].filename().string() << std::endl;
        if (LoadPlugin(pluginFiles[i].string(), false)) ++loaded;
        else ++failed;
        if (m_callback.onBatchProgress) {
            m_callback.onBatchProgress(total, loaded, failed);
        }
    }

    std::cout << std::endl;
    return loaded;
}

// ============================================================
// api_*.dll 静默加载 + 安全扫描
// ============================================================
ApiPluginScanResult PluginManager::ScanApiPlugin(
    const std::string& dllPath) const {
    ApiPluginScanResult result;
    std::error_code ec;

    if (!fs::is_regular_file(fs::u8path(dllPath), ec)) {
        result.reason = "文件不存在";
        return result;
    }

    std::string ext = ToLowerAscii(fs::u8path(dllPath).extension().string());
    if (ext != ".dll") {
        result.reason = "扩展名不是 .dll";
        return result;
    }

    std::string exeDir = ToLowerAscii(ExeDirectory());
    std::string full = ToLowerAscii(fs::absolute(fs::u8path(dllPath)).string());
    if (!exeDir.empty() && full.rfind(exeDir, 0) != 0) {
        result.reason = "插件不在 light.exe 目录内";
        return result;
    }

    std::set<std::string> visited;
    if (!ScanRecursive(dllPath, 0, visited, result)) {
        if (result.reason.empty()) result.reason = "安全扫描未通过";
        return result;
    }

    result.signedPlugin = IsSignedTrusted(dllPath);
    result.safe = true;
    return result;
}

int PluginManager::LoadApiPluginsFromDirectory(const std::string& dirPath,
    bool silent) {
    std::error_code ec;
    if (!fs::exists(fs::u8path(dirPath), ec)) return 0;

    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(fs::u8path(dirPath), ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = ToLowerAscii(entry.path().filename().string());
        std::string ext = ToLowerAscii(entry.path().extension().string());
        if (ext == ".dll" && name.rfind("api_", 0) == 0) {
            candidates.push_back(entry.path());
        }
    }
    std::sort(candidates.begin(), candidates.end());

    int loaded = 0;
    for (const auto& path : candidates) {
        if (LoadApiPlugin(path.string(), true)) ++loaded;
    }

    if (!silent) {
        std::cout << "[插件] api_ 指令集静默加载完成: " << loaded << "/"
            << candidates.size() << std::endl;
    }
    else if (!candidates.empty()) {
        std::string msg = "[PluginManager] api_ 指令集: " +
            std::to_string(loaded) + "/" + std::to_string(candidates.size()) + "\n";
        OutputDebugStringA(msg.c_str());
    }
    return loaded;
}

bool PluginManager::LoadApiPlugin(const std::string& dllPath, bool silent) {
    ApiPluginScanResult scan = ScanApiPlugin(dllPath);
    if (!scan.safe) {
        if (!silent) {
            std::cout << "[插件] 安全扫描拒绝: "
                << fs::u8path(dllPath).filename().string()
                << " - " << scan.reason << std::endl;
        }
        else {
            std::string msg = "[PluginManager] 拒绝加载不安全的 api_ 插件: " +
                dllPath + " - " + scan.reason + "\n";
            OutputDebugStringA(msg.c_str());
        }
        return false;
    }

    PreloadLocalDependencies(scan.localDependencies);

    std::string pluginId = "api_" + std::to_string(GetTickCount64());
    return LoadPluginDll(dllPath, pluginId, silent, true);
}

void PluginManager::UnloadPlugin(const std::string& pluginId, bool silent) {
    LoadedPlugin removed;
    std::string pluginName;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_plugins.find(pluginId);
        if (it == m_plugins.end()) return;
        removed = it->second;
        pluginName = removed.info.name.empty() ? pluginId : removed.info.name;
        m_plugins.erase(it);
        found = true;
    }

    if (!found) return;

    if (removed.instance) {
        removed.instance->OnUnload();
        removed.instance->Shutdown();
        delete removed.instance;
    }
    if (removed.guiProvider && removed.destroyGuiProvider) {
        removed.destroyGuiProvider(removed.guiProvider);
    }
    if (removed.handle) {
        FreeLibrary(removed.handle);
    }

    std::error_code ec;
    if (!removed.extractPath.empty() && fs::exists(fs::u8path(removed.extractPath), ec)) {
        fs::remove_all(fs::u8path(removed.extractPath), ec);
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it2 = m_tempDirs.begin(); it2 != m_tempDirs.end(); ++it2) {
            if (it2->second == removed.extractPath) {
                m_tempDirs.erase(it2);
                break;
            }
        }
    }

    if (!silent && m_callback.onUnload) {
        m_callback.onUnload(pluginName);
    }
}

void PluginManager::UnloadAllPlugins(bool silent) {
    std::map<std::string, LoadedPlugin> removed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        removed.swap(m_plugins);
        m_tempDirs.clear();
    }

    for (auto& pair : removed) {
        auto& plugin = pair.second;
        if (plugin.instance) {
            plugin.instance->OnUnload();
            plugin.instance->Shutdown();
            delete plugin.instance;
        }
        if (plugin.guiProvider && plugin.destroyGuiProvider) {
            plugin.destroyGuiProvider(plugin.guiProvider);
        }
        if (plugin.handle) {
            FreeLibrary(plugin.handle);
        }
        std::error_code ec;
        if (!plugin.extractPath.empty() &&
            fs::exists(fs::u8path(plugin.extractPath), ec)) {
            fs::remove_all(fs::u8path(plugin.extractPath), ec);
        }
        if (!silent && m_callback.onUnload) {
            m_callback.onUnload(plugin.info.name);
        }
    }

    CleanupTempDirs();
}

void PluginManager::ReloadAllPlugins(bool silent) {
    std::vector<std::string> pluginPaths;
    std::vector<bool> wasApi;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& pair : m_plugins) {
            if (!pair.second.dllPath.empty()) {
                pluginPaths.push_back(pair.second.dllPath);
                wasApi.push_back(pair.second.isApiPlugin);
            }
        }
    }

    UnloadAllPlugins(silent);

    for (size_t i = 0; i < pluginPaths.size(); i++) {
        if (!silent) {
            std::cout << "\n[插件] 热重载: "
                << fs::u8path(pluginPaths[i]).filename().string() << std::endl;
        }
        if (wasApi[i]) LoadApiPlugin(pluginPaths[i], silent);
        else LoadPlugin(pluginPaths[i], silent);
    }
}

void PluginManager::ReloadPlugin(const std::string& pluginId, bool silent) {
    std::string dllPath;
    bool api = false;
    bool isFromZip = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_plugins.find(pluginId);
        if (it == m_plugins.end()) {
            if (!silent && m_callback.onLoad) {
                m_callback.onLoad(pluginId, false, "插件未找到");
            }
            return;
        }
        dllPath = it->second.dllPath;
        api = it->second.isApiPlugin;
        isFromZip = it->second.isFromZip;
    }

    UnloadPlugin(pluginId, silent);

    if (!silent) {
        std::cout << "\n[插件] 热重载: "
            << fs::u8path(dllPath).filename().string() << std::endl;
    }

    if (isFromZip) LoadPlugin(dllPath, silent);
    else if (api) LoadApiPlugin(dllPath, silent);
    else LoadPlugin(dllPath, silent);

    if (!silent) std::cout << std::endl;
}

bool PluginManager::ExecuteCommand(const std::string& cmd,
    const std::vector<std::string>& args) {
    std::vector<PluginCommand> matched;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& pair : m_plugins) {
            auto& plugin = pair.second;
            if (!plugin.instance || !plugin.enabled) continue;
            auto commands = plugin.instance->GetCommands();
            for (const auto& pluginCmd : commands) {
                if (pluginCmd.name == cmd) matched.push_back(pluginCmd);
            }
        }
    }
    for (auto& c : matched) {
        if (c.callback) c.callback(args);
    }
    return !matched.empty();
}

std::vector<std::string> PluginManager::GetPluginCommands() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> commands;
    for (const auto& pair : m_plugins) {
        auto& plugin = pair.second;
        if (plugin.instance) {
            auto cmds = plugin.instance->GetCommands();
            for (const auto& cmd : cmds) commands.push_back(cmd.name);
        }
    }
    return commands;
}

std::vector<PluginCommandInfo> PluginManager::GetAllPluginCommands() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PluginCommandInfo> result;
    for (const auto& pair : m_plugins) {
        auto& plugin = pair.second;
        if (plugin.instance) {
            auto commands = plugin.instance->GetCommands();
            for (const auto& cmd : commands) {
                PluginCommandInfo info;
                info.pluginName = plugin.info.name;
                info.commandName = cmd.name;
                info.description = cmd.description;
                info.usage = cmd.usage;
                result.push_back(info);
            }
        }
    }
    return result;
}

std::string PluginManager::GetCommandHelp(const std::string& cmd) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& pair : m_plugins) {
        auto& plugin = pair.second;
        if (plugin.instance) {
            auto commands = plugin.instance->GetCommands();
            for (const auto& pluginCmd : commands) {
                if (pluginCmd.name == cmd) {
                    return pluginCmd.usage + " - " + pluginCmd.description;
                }
            }
        }
    }
    return "";
}

std::vector<PluginInfo> PluginManager::GetLoadedPlugins() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PluginInfo> infos;
    for (const auto& pair : m_plugins) infos.push_back(pair.second.info);
    return infos;
}

std::vector<PluginRecord> PluginManager::GetPluginRecords() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PluginRecord> records;
    for (const auto& pair : m_plugins) {
        const auto& plugin = pair.second;
        PluginRecord r;
        r.id = plugin.info.id.empty() ? pair.first : plugin.info.id;
        r.name = plugin.info.name;
        r.version = plugin.info.version;
        r.author = plugin.info.author;
        r.description = plugin.info.description;
        r.path = plugin.dllPath;
        r.isApiPlugin = plugin.isApiPlugin;
        r.isGuiPlugin = (plugin.guiProvider != nullptr);
        r.isFromZip = plugin.isFromZip;
        r.enabled = plugin.enabled;
        records.push_back(r);
    }
    return records;
}

bool PluginManager::GetPluginPath(const std::string& pluginId,
    std::string& outPath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end()) return false;
    outPath = it->second.dllPath;
    return !outPath.empty();
}

bool PluginManager::IsPluginLoaded(const std::string& pluginId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_plugins.find(pluginId) != m_plugins.end();
}

bool PluginManager::SetPluginEnabled(const std::string& pluginId, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end()) return false;
    it->second.enabled = enabled;
    return true;
}

bool PluginManager::IsPluginEnabled(const std::string& pluginId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end()) return false;
    return it->second.enabled;
}
