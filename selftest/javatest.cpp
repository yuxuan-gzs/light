// javatest.cpp - 验证 Java 代码生成 + JDK 编译 + 运行
#include "core/BlockModel.h"
#include "core/BlockLibrary.h"
#include "core/CodeGenerator.h"
#include "core/JdkCompiler.h"
#include "core/util.h"
#include <cstdio>

int main() {
    using namespace vcb;

    Program p = BlockLibrary::MakeSampleProgram();
    p.name = "MyProject";

    // 1. 生成 Java 代码
    std::string err;
    std::string java = CodeGenerator::GenerateProgram(p, TargetLang::Java, err);
    if (!err.empty()) { printf("GEN FAIL: %s\n", err.c_str()); return 1; }
    printf("=== generated Java ===\n%s\n", java.c_str());

    // 2. 写 .java 文件
    std::wstring srcDir = L"D:\\YX\\VCBlocks\\build\\jtest";
    EnsureDir(srcDir);
    std::wstring javaPath = srcDir + L"\\MyProject.java";
    if (!WriteFileText(javaPath, java)) { printf("WRITE FAIL\n"); return 1; }

    // 3. 检测 JDK
    if (!JdkCompiler::Init(err)) { printf("JDK INIT FAIL: %s\n", err.c_str()); return 1; }
    printf("javac = %ls\n", JdkCompiler::JavacPath().c_str());
    printf("java  = %ls\n", JdkCompiler::JavaPath().c_str());

    // 4. 编译
    std::string log;
    std::wstring outDir = srcDir + L"\\out";
    EnsureDir(outDir);
    if (!JdkCompiler::Compile(javaPath, outDir, log)) {
        printf("COMPILE FAIL:\n%s\n", log.c_str());
        return 1;
    }
    printf("COMPILE OK\n");

    // 5. 运行
    std::string runLog;
    if (!JdkCompiler::Run(outDir, "MyProject", runLog)) {
        printf("RUN FAIL:\n%s\n", runLog.c_str());
        return 1;
    }
    printf("=== run output ===\n%s\n", runLog.c_str());
    printf("ALL OK\n");
    return 0;
}
