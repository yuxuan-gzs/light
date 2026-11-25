// util.cpp
#include "util.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace vcb {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::wstring AnsiToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &u[0], n, nullptr, nullptr);
    return u;
}

std::wstring ToWide(const std::string& s) { return Utf8ToWide(s); }

bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD hi = 0, lo = GetFileSize(h, &hi);
    if (lo == INVALID_FILE_SIZE) { CloseHandle(h); return false; }
    size_t size = ((size_t)hi << 32) | lo;
    out.resize(size);
    if (size) {
        DWORD read = 0;
        if (!ReadFile(h, out.data(), (DWORD)size, &read, nullptr)) { CloseHandle(h); return false; }
    }
    CloseHandle(h);
    return true;
}

bool WriteFileBytes(const std::wstring& path, const std::vector<unsigned char>& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    if (!data.empty()) {
        DWORD written = 0;
        ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) != 0;
    }
    CloseHandle(h);
    return ok;
}

// 读取文本并统一为 UTF-8：识别 BOM，否则尝试 UTF-8，失败按本地 ANSI。
bool ReadFileText(const std::wstring& path, std::string& out) {
    std::vector<unsigned char> raw;
    if (!ReadFileBytes(path, raw)) return false;
    out.clear();
    if (raw.empty()) return true;

    // UTF-8 BOM
    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        out.assign((const char*)raw.data() + 3, raw.size() - 3);
        return true;
    }
    // UTF-16 LE BOM
    if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        const wchar_t* p = (const wchar_t*)(raw.data() + 2);
        size_t n = (raw.size() - 2) / 2;
        out = WideToUtf8(std::wstring(p, n));
        return true;
    }
    // UTF-16 BE BOM
    if (raw.size() >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        std::wstring w;
        for (size_t i = 2; i + 1 < raw.size(); i += 2) {
            w.push_back((wchar_t)((raw[i] << 8) | raw[i + 1]));
        }
        out = WideToUtf8(w);
        return true;
    }

    std::string s((const char*)raw.data(), raw.size());
    // 校验是否为合法 UTF-8
    bool validUtf8 = true;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { i += 1; continue; }
        else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= s.size() || ((unsigned char)s[i + 1] & 0xC0) != 0x80) { validUtf8 = false; break; }
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= s.size() || ((unsigned char)s[i + 1] & 0xC0) != 0x80 || ((unsigned char)s[i + 2] & 0xC0) != 0x80) { validUtf8 = false; break; }
            i += 3;
        }
        else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= s.size() || ((unsigned char)s[i + 1] & 0xC0) != 0x80 || ((unsigned char)s[i + 2] & 0xC0) != 0x80 || ((unsigned char)s[i + 3] & 0xC0) != 0x80) { validUtf8 = false; break; }
            i += 4;
        }
        else { validUtf8 = false; break; }
    }
    if (validUtf8) { out = s; return true; }

    // 按本地 ANSI (GBK) 转 UTF-8
    int n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n > 0) {
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), &w[0], n);
        out = WideToUtf8(w);
    } else {
        out = s;
    }
    return true;
}

bool WriteFileText(const std::wstring& path, const std::string& text) {
    std::vector<unsigned char> data(text.begin(), text.end());
    return WriteFileBytes(path, data);
}

std::wstring ExePath() {
    wchar_t buf[MAX_PATH];
    // 定位主程序（宿主进程，如 light.exe）路径——项目数据 Cache\vC 应在主程序目录下
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

std::wstring ExeDir() {
    std::wstring p = ExePath();
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".\\";
    return p.substr(0, pos + 1);
}

void EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return;
    std::wstring d = dir;
    for (auto& c : d) if (c == L'/') c = L'\\';
    // 递归创建
    std::wstring cur;
    size_t i = 0;
    // 处理盘符
    if (d.size() >= 2 && d[1] == L':') { cur = d.substr(0, 2); i = 2; }
    for (; i < d.size(); ++i) {
        if (d[i] == L'\\') {
            if (!cur.empty()) {
                CreateDirectoryW(cur.c_str(), nullptr);
            }
        }
        cur += d[i];
    }
    if (!cur.empty() && cur.back() != L'\\') CreateDirectoryW(cur.c_str(), nullptr);
}

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring BaseName(const std::wstring& path) {
    std::wstring p = path;
    size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) p = p.substr(slash + 1);
    size_t dot = p.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) p = p.substr(0, dot);
    return p;
}

std::wstring DirOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"";
    return path.substr(0, slash + 1);
}

std::wstring ReplaceExt(const std::wstring& path, const std::wstring& ext) {
    std::wstring p = path;
    size_t slash = p.find_last_of(L"\\/");
    size_t dot = p.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
        p = p.substr(0, dot);
    }
    return p + ext;
}

std::string ToLowerAscii(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}
std::string ToUpperAscii(std::string s) {
    for (auto& c : s) if (c >= 'a' && c <= 'z') c -= 32;
    return s;
}

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}
bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> SplitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else if (c == '\r') { /* skip */ }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string Join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

std::string ReplaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string EscapeCString(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

} // namespace vcb
