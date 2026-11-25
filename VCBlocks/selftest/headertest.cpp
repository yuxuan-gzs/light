// headertest.cpp
// 验证：头文件积木动态分类 + 调用函数代码生成 + 背景/编译配置
#include <cstdio>
#include <string>
#include <vector>
#include "../VCBlocks/src/core/BlockLibrary.h"
#include "../VCBlocks/src/core/CodeGenerator.h"
#include "../VCBlocks/src/core/CompileConfig.h"

using namespace vcb;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++g_fail; } else { printf("OK:   %s\n", msg); } } while (0)

int main() {
    // 1. 注册头文件函数
    std::vector<std::pair<std::string, std::string>> funcs = {
        { "printf", "打印输出" },
        { "malloc", "分配内存" },
    };
    std::vector<std::string> headers = { "stdio.h", "stdlib.h" };
    BlockLibrary::SetHeaderFunctions(funcs, headers);

    // 2. 分类包含 header
    bool hasHeader = false;
    for (const auto& c : BlockLibrary::Categories()) if (c.id == "header") hasHeader = true;
    CHECK(hasHeader, "Categories 包含 header 分类");

    // 3. header 分类积木（通用 callfunc + 2 个函数积木）
    auto hb = BlockLibrary::BlocksInCategory("header");
    CHECK(hb.size() == 3, "header 分类有 3 个积木（callfunc + 2 函数）");

    // 4. HeaderFuncName
    CHECK(BlockLibrary::HeaderFuncName("call_printf") == "printf", "HeaderFuncName(call_printf)");
    CHECK(BlockLibrary::IsCallFunc("callfunc"), "IsCallFunc(callfunc)");

    // 5. 代码生成：call_printf 语句 + callfunc
    Program p;
    p.name = "HeaderTest";
    Block callPrintf;
    callPrintf.typeId = "call_printf";
    callPrintf.category = "header";
    callPrintf.label = "printf";
    p.blocks.push_back(callPrintf);

    Block callfunc = BlockLibrary::MakeBlock("callfunc");
    callfunc.params[0].text = "printf";
    callfunc.reporters.clear();
    callfunc.params[1].type = ParamType::Reporter;
    callfunc.params[1].hasReporter = true;
    Block strNum = BlockLibrary::MakeBlock("num");
    strNum.params[0].text = "42";
    callfunc.reporters.push_back(strNum);
    callfunc.RelinkReporters();
    p.blocks.push_back(callfunc);

    std::string err;
    std::string code = CodeGenerator::GenerateProgram(p, TargetLang::C, err);
    CHECK(err.empty(), "C 代码生成无错误");
    CHECK(code.find("#include \"stdio.h\"") != std::string::npos, "生成 #include \"stdio.h\"");
    CHECK(code.find("printf();") != std::string::npos, "生成 printf(); 调用");
    CHECK(code.find("printf((42));") != std::string::npos, "生成 printf((42)); 调用（callfunc）");

    // 6. C++ 代码生成含头文件
    std::string cpp = CodeGenerator::GenerateProgram(p, TargetLang::Cpp, err);
    CHECK(cpp.find("#include \"stdlib.h\"") != std::string::npos, "C++ 生成 #include \"stdlib.h\"");

    // 7. 编译配置 flags
    CompileConfig cfg;
    cfg.config = BuildConfig::Debug;
    cfg.runtime = RuntimeLib::StaticDebug;
    std::string flags = cfg.MsBuildFlags();
    CHECK(flags.find("/MTd") != std::string::npos && flags.find("/Zi") != std::string::npos,
          "Debug+MTd flags 正确");

    printf("\n%s (fail=%d)\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
