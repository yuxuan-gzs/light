// ProjectStore.cpp
#include "ProjectStore.h"
#include "util.h"
#include <windows.h>

namespace vcb {

std::wstring ProjectStore::RootDir() {
    std::wstring d = ExeDir() + L"Cache\\vC\\";
    EnsureDir(d);
    return d;
}

std::wstring ProjectStore::ProjectDir(const std::string& project) {
    std::wstring d = RootDir() + Utf8ToWide(project) + L"\\";
    EnsureDir(d);
    return d;
}

std::wstring ProjectStore::LangDir(const std::string& project) {
    std::wstring d = ProjectDir(project) + L"lang\\";
    EnsureDir(d);
    return d;
}

std::wstring ProjectStore::SrcDir(const std::string& project) {
    std::wstring d = ProjectDir(project) + L"src\\";
    EnsureDir(d);
    return d;
}

std::wstring ProjectStore::OutDir(const std::string& project) {
    std::wstring d = ProjectDir(project) + L"out\\";
    EnsureDir(d);
    return d;
}

std::wstring ProjectStore::BlocksPath(const std::string& project) {
    return ProjectDir(project) + L"blocks.json";
}

std::wstring ProjectStore::SettingsPath(const std::string& project) {
    return ProjectDir(project) + L"settings.json";
}

std::wstring ProjectStore::LangPath(const std::string& project, const std::string& library) {
    return LangDir(project) + Utf8ToWide(library) + L".json";
}

// ---- 库（按语言分开放）----
std::string ProjectStore::LangId(TargetLang lang) {
    if (lang == TargetLang::Cpp) return "cpp";
    if (lang == TargetLang::Java) return "java";
    return "c";
}

std::wstring ProjectStore::LibsRoot() {
    std::wstring d = RootDir() + L"libs\\";
    EnsureDir(d);
    return d;
}

std::wstring ProjectStore::LibsDir(TargetLang lang) {
    return LibsDirById(LangId(lang));
}

std::wstring ProjectStore::LibsDirById(const std::string& langId) {
    std::wstring d = LibsRoot() + Utf8ToWide(langId.empty() ? "c" : langId) + L"\\";
    EnsureDir(d);
    return d;
}

std::vector<std::string> ProjectStore::ListProjects() {
    std::vector<std::string> out;
    std::wstring root = RootDir();
    std::wstring search = root + L"*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            out.push_back(WideToUtf8(n));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

bool ProjectStore::IsValidProjectName(const std::string& name) {
    if (name.empty()) return false;
    for (unsigned char c : name) {
        if (c < 0x80) {
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ' || c == '.';
            if (!ok) return false;
        }
        // 高位字节（CJK 等）放行
    }
    // 去掉非法 Windows 字符
    for (char c : name) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    }
    return true;
}

std::string ProjectStore::SanitizeLibraryName(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out += c;
        }
    }
    if (out.empty()) out = "library";
    return out;
}

} // namespace vcb
