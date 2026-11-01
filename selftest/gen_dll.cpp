// gen_dll.cpp - 生成正式命名的 YXG 插件 DLL 源码（部署到 light）
#include <cstdio>
#include "core/BlockLibrary.h"
#include "core/DllBuilder.h"
#include "core/util.h"

using namespace vcb;

// 把一个 Block 的第 paramIndex 个 Reporter 参数替换为给定值积木
static void SetReporterParam(Block& b, size_t paramIndex, Block value) {
    b.reporters.clear();
    b.params[paramIndex].type = ParamType::Reporter;
    b.params[paramIndex].hasReporter = true;
    b.reporters.push_back(value);
    b.RelinkReporters();
}

int main() {
    // 示例程序：set counter=1; repeat 5 { print "计数:"; printNum counter; change counter by 1 }
    Program p;
    p.name = "VCBlocksDemo";

    Block setc = BlockLibrary::MakeBlock("set");
    setc.params[0].text = "counter";
    Block n1 = BlockLibrary::MakeBlock("num"); n1.params[0].text = "1";
    SetReporterParam(setc, 1, n1);

    Block repeat = BlockLibrary::MakeBlock("repeat");
    repeat.params[0].text = "5";

    Block printLabel = BlockLibrary::MakeBlock("print");
    printLabel.params[0].text = "计数: ";

    Block printNum = BlockLibrary::MakeBlock("printNum");
    Block varc = BlockLibrary::MakeBlock("var"); varc.params[0].text = "counter";
    SetReporterParam(printNum, 0, varc);

    Block changec = BlockLibrary::MakeBlock("change");
    changec.params[0].text = "counter";
    Block n1b = BlockLibrary::MakeBlock("num"); n1b.params[0].text = "1";
    SetReporterParam(changec, 1, n1b);

    repeat.children.push_back(printLabel);
    repeat.children.push_back(printNum);
    repeat.children.push_back(changec);

    p.blocks.push_back(setc);
    p.blocks.push_back(repeat);

    DllBuilder::Options opt;
    opt.dllName = "YXG_VCBlocks";
    opt.jsNamespace = "blocks";
    opt.version = "1.0.0";
    opt.author = "VCBlocks";
    opt.description = "VCBlocks demo plugin: counter loop 1..5 (logs via LogInfo)";
    opt.panelTitle = "VCBlocks 演示";
    opt.panelIcon = "\xF0\x9F\xA7\xA9"; // 🧩

    std::string err;
    std::string src = DllBuilder::GenerateSource(p, opt, err);
    if (!err.empty()) { printf("GEN ERROR: %s\n", err.c_str()); return 1; }

    if (!WriteFileText(L"D:\\YX\\VCBlocks\\build\\YXG_VCBlocks.c", src)) {
        printf("WRITE ERROR\n"); return 1;
    }
    printf("OK: generated YXG_VCBlocks.c (%zu bytes)\n", src.size());
    return 0;
}
