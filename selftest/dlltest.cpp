// dlltest.cpp - 验证 DllBuilder 生成的 YXG 插件源码可被 TCC 编译为 DLL
#include "core/DllBuilder.h"
#include "core/BlockLibrary.h"
#include "core/TccCompiler.h"
#include "core/util.h"
#include <cstdio>
using namespace vcb;
int main() {
    Program p = BlockLibrary::MakeSampleProgram();
    DllBuilder::Options opt;
    opt.dllName = "YXG_TestPlugin";
    opt.description = "tcc compile test";
    std::string err;
    std::string src = DllBuilder::GenerateSource(p, opt, err);
    if (!err.empty()) { printf("[FAIL] gen: %s\n", err.c_str()); return 1; }
    WriteFileText(L"D:\\YX\\VCBlocks\\build\\tcc_plugin.c", src);
    printf("[OK] source generated (%zu bytes)\n", src.size());

    if (!TccCompiler::Init(err)) { printf("[FAIL] init: %s\n", err.c_str()); return 1; }
    std::string log;
    bool ok = TccCompiler::CompileToDll(src, L"D:\\YX\\VCBlocks\\build\\tcc_plugin.dll", log);
    printf("[%s] CompileToDll\n", ok ? "OK" : "FAIL");
    if (!log.empty()) printf("log:\n%s\n", log.c_str());
    return ok ? 0 : 1;
}
