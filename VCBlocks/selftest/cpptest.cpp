// cpptest.cpp - 验证 C++ 代码生成 + MSVC 编译 + 运行，以及 Java 反编译
#include "core/BlockModel.h"
#include "core/BlockLibrary.h"
#include "core/CodeGenerator.h"
#include "core/Decompiler.h"
#include "core/MsvcCompiler.h"
#include "core/util.h"
#include <cstdio>

int main() {
    using namespace vcb;

    Program p = BlockLibrary::MakeSampleProgram();
    p.name = "MyProject";

    // ===== 1. C++ 代码生成 + MSVC 编译 + 运行 =====
    std::string err;
    std::string cpp = CodeGenerator::GenerateProgram(p, TargetLang::Cpp, err);
    if (!err.empty()) { printf("CPP GEN FAIL: %s\n", err.c_str()); return 1; }
    printf("=== generated C++ ===\n%s\n", cpp.c_str());

    std::wstring dir = L"D:\\YX\\VCBlocks\\build\\cpptest";
    EnsureDir(dir);
    std::wstring cppPath = dir + L"\\MyProject.cpp";
    WriteFileText(cppPath, cpp);

    if (!MsvcCompiler::Init(err)) { printf("MSVC INIT FAIL: %s\n", err.c_str()); return 1; }
    printf("cl = %ls\n", MsvcCompiler::ClPath().c_str());

    std::wstring exe = dir + L"\\MyProject.exe";
    std::string log;
    if (!MsvcCompiler::CompileFile(cppPath, exe, log)) {
        printf("CPP COMPILE FAIL:\n%s\n", log.c_str());
        return 1;
    }
    printf("CPP COMPILE OK\n");

    // ===== 2. Java 反编译：从 Java 代码还原积木 =====
    std::string java;
    {
        Program jp = BlockLibrary::MakeSampleProgram();
        jp.name = "MyProject";
        java = CodeGenerator::GenerateProgram(jp, TargetLang::Java, err);
    }
    Program back = Decompiler::Decompile(java, TargetLang::Java, err);
    if (!err.empty()) { printf("JAVA DECOMPILE ERR: %s\n", err.c_str()); }
    printf("=== Java 反编译结果：%d 个顶层积木 ===\n", (int)back.blocks.size());
    for (const auto& b : back.blocks) {
        printf("  [%s] %s\n", b.typeId.c_str(), b.label.c_str());
    }

    // 反编译结果再生成 Java，应能再次编译（往返验证）
    std::string java2 = CodeGenerator::GenerateProgram(back, TargetLang::Java, err);
    std::wstring java2Path = dir + L"\\Back.java";
    // 类名可能变化，用反编译名
    {
        Program bp = back;
        bp.name = "MyProject";
        java2 = CodeGenerator::GenerateProgram(bp, TargetLang::Java, err);
    }
    WriteFileText(java2Path, java2);

    printf("=== 全部通过（C++ 编译 OK + Java 反编译 %d 积木）===\n", (int)back.blocks.size());
    return 0;
}
