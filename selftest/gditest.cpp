// gditest.cpp - 测量 GdiplusStartup 耗时
#include <windows.h>
#include <gdiplus.h>
#include <cstdio>
int main() {
    ULONGLONG t0 = GetTickCount64();
    Gdiplus::GdiplusStartupInput in;
    ULONG_PTR tok = 0;
    Gdiplus::Status st = Gdiplus::GdiplusStartup(&tok, &in, nullptr);
    printf("GdiplusStartup: %llu ms, status=%d\n", (unsigned long long)(GetTickCount64() - t0), (int)st);

    // 再测字体创建
    ULONGLONG t1 = GetTickCount64();
    for (int i = 0; i < 4; ++i) {
        Gdiplus::Font* f = new Gdiplus::Font(L"Microsoft YaHei UI", 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        delete f;
    }
    printf("4x Font create: %llu ms\n", (unsigned long long)(GetTickCount64() - t1));

    Gdiplus::GdiplusShutdown(tok);
    return 0;
}
