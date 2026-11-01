// dirtest.cpp - 验证 EnsureDir
#include "core/util.h"
#include <cstdio>

int main() {
    using namespace vcb;
    std::wstring p = L"C:\\Users\\29406\\AppData\\Local\\Temp\\vcb_tcc_probe\\sub\\";
    EnsureDir(p);
    DWORD a = GetFileAttributesW(p.c_str());
    printf("EnsureDir -> exists=%d err=%lu\n", a != INVALID_FILE_ATTRIBUTES ? 1 : 0, (unsigned long)GetLastError());

    std::wstring p2 = L"C:\\Users\\29406\\AppData\\Local\\Temp\\vcb_tcc_probe2\\";
    BOOL r = CreateDirectoryW(p2.c_str(), nullptr);
    printf("CreateDirectoryW(单层) -> %d err=%lu\n", r, (unsigned long)GetLastError());
    return 0;
}
