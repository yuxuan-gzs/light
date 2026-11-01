// tccres_test.cpp - 验证 TCC 资源嵌入 + 提取 + 编译 C 代码
#include "core/TccCompiler.h"
#include "core/util.h"
#include <cstdio>
#include <string>

int main() {
    using namespace vcb;

    // 诊断：直接检查资源
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC r = FindResourceW(self, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));
    printf("FindResource(101,10): %s\n", r ? "OK" : "FAIL");
    if (r) {
        HGLOBAL g = LoadResource(self, r);
        void* d = LockResource(g);
        DWORD s = SizeofResource(self, r);
        printf("resource size=%u, data=%p\n", s, d);
    }

    // 1. Init：从资源提取 TCC 到临时目录并加载 libtcc.dll
    std::string err;
    if (!TccCompiler::Init(err)) {
        printf("TCC Init FAIL: %s\n", err.c_str());
        return 1;
    }
    printf("TCC Init OK, tccRoot=%ls\n", TccCompiler::TccRootDir().c_str());

    // 诊断：检查临时目录提取结果
    {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring root = std::wstring(tmp) + L"vcb_tcc\\";
        std::wstring dll = root + L"libtcc.dll";
        DWORD a = GetFileAttributesW(dll.c_str());
        printf("临时目录 libtcc.dll: %s\n", (a != INVALID_FILE_ATTRIBUTES) ? "存在" : "不存在");
        std::wstring inc = root + L"include\\stdio.h";
        DWORD b = GetFileAttributesW(inc.c_str());
        printf("临时目录 include\\stdio.h: %s\n", (b != INVALID_FILE_ATTRIBUTES) ? "存在" : "不存在");
        printf("临时目录路径: %ls\n", root.c_str());
    }

    // 2. 编译一段 C 代码为 exe
    const char* code =
        "#include <stdio.h>\n"
        "int main() { printf(\"Hello from embedded TCC!\\n\"); return 0; }\n";
    std::wstring out = L"D:\\YX\\VCBlocks\\build\\tccres_out.exe";
    std::string log;
    if (!TccCompiler::CompileToExe(code, out, log)) {
        printf("COMPILE FAIL:\n%s\n", log.c_str());
        return 1;
    }
    printf("COMPILE OK -> %ls\n", out.c_str());

    // 3. 运行生成的 exe
    printf("=== 运行生成的 exe ===\n");
    // 用 system 运行（简单验证）
    std::wstring cmd = L"\"" + out + L"\"";
    int rc = _wsystem(cmd.c_str());
    printf("运行返回: %d\n", rc);

    printf("=== tccres_test 完成 ===\n");
    return 0;
}
