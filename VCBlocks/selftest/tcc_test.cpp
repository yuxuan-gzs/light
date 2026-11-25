// tcc_test.cpp - 验证内置 TCC 编译系统
#include "core/TccCompiler.h"
#include <cstdio>
using namespace vcb;
int main() {
    std::string err;
    if (!TccCompiler::Init(err)) { printf("[FAIL] Init: %s\n", err.c_str()); return 1; }
    printf("[OK] TCC Init, root=%ls\n", TccCompiler::TccRootDir().c_str());

    std::string code =
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "    int x = 42;\n"
        "    for (int i = 0; i < 3; i++) { printf(\"x = %d\\n\", x + i); }\n"
        "    return 0;\n"
        "}\n";
    std::string log;
    bool ok = TccCompiler::CompileToExe(code, L"D:\\YX\\VCBlocks\\build\\tcc_prog.exe", log);
    printf("[%s] CompileToExe\n", ok ? "OK" : "FAIL");
    if (!log.empty()) printf("log:\n%s\n", log.c_str());
    if (!ok) return 1;

    // 运行生成的 exe
    printf("=== 运行生成的 exe ===\n");
    system("D:\\YX\\VCBlocks\\build\\tcc_prog.exe");
    return 0;
}
