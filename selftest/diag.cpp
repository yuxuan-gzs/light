// diag.cpp - 诊断工具链检测各步骤
#include <cstdio>
#include <string>
#include <windows.h>
int main() {
    auto de = [](const wchar_t* p) {
        DWORD a = GetFileAttributesW(p);
        bool dir = (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
        bool file = (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
        printf("%-70ls attr=0x%08lX dir=%d file=%d\n", p, a, dir ? 1 : 0, file ? 1 : 0);
    };
    de(L"I:\\");
    de(L"I:\\Microsoft Visual Studio");
    de(L"I:\\Microsoft Visual Studio\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.51.36231");
    de(L"I:\\Microsoft Visual Studio\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.51.36231\\bin\\Hostx64\\x64\\cl.exe");
    de(L"I:\\Windows Kits\\10");
    de(L"C:\\Program Files (x86)\\Microsoft Visual Studio");
    return 0;
}
