// tctest.cpp - 验证 MSVC 工具链检测
#include <cstdio>
#include "core/Toolchain.h"
int main() {
    vcb::Toolchain tc;
    bool ok = vcb::ToolchainDetect::Detect(tc);
    printf("detect=%d valid=%d\n", ok ? 1 : 0, tc.valid ? 1 : 0);
    if (tc.valid) {
        printf("cl      = %ls\n", tc.clPath.c_str());
        printf("toolset = %ls\n", tc.toolset.c_str());
        printf("binDir  = %ls\n", tc.binDir.c_str());
        printf("sdkBin  = %ls\n", tc.sdkBin.c_str());
    }
    return tc.valid ? 0 : 1;
}
