// BlockLibrary.cpp
#include "BlockLibrary.h"
#include <map>

namespace vcb {

// 头文件/库函数动态积木（运行时由 MainWindow 扫描 libs\<lang>\ 注入）
static std::vector<BlockDef> g_headerBlocks;
static std::map<std::string, std::string> g_headerFuncs; // typeId "call_<name>" -> 函数名
static std::vector<std::string> g_headerIncludes;         // 已安装头文件名（#include 用）

static std::vector<CategoryDef> MakeCategories() {
    return {
        { "output",    "Output",    0xFF4C97FF },
        { "variables", "Variables", 0xFFFF8C1A },
        { "control",   "Control",   0xFFFFAB19 },
        { "math",      "Math",      0xFF59C059 },
        { "logic",     "Logic",     0xFF00B8A9 },
    };
}

static std::vector<BlockDef> MakeBlocks() {
    std::vector<BlockDef> b;
    // ---- 输出 ----
    b.push_back({ "print",    "output", "print {0}",             { ParamType::Text },     false, false, false });
    b.push_back({ "printNum", "output", "print number {0}",      { ParamType::Reporter }, false, false, false });
    // ---- 变量 ----
    b.push_back({ "set",    "variables", "set {0} to {1}",       { ParamType::Text, ParamType::Reporter }, false, false, false });
    b.push_back({ "change", "variables", "change {0} by {1}",    { ParamType::Text, ParamType::Reporter }, false, false, false });
    b.push_back({ "var",    "variables", "{0}",                  { ParamType::Text },     false, false, true });
    // ---- 控制 ----
    b.push_back({ "if",      "control", "if {0} then",           { ParamType::Reporter }, true,  false, false });
    b.push_back({ "ifElse",  "control", "if {0} then / else",    { ParamType::Reporter }, true,  true,  false });
    b.push_back({ "repeat",  "control", "repeat {0} times",      { ParamType::Number },   true,  false, false });
    b.push_back({ "while",   "control", "while {0}",             { ParamType::Reporter }, true,  false, false });
    // ---- 数学 ----
    b.push_back({ "num",    "math", "number",                    { ParamType::Number },   false, false, true });
    b.push_back({ "add",    "math", "{0} + {1}",                 { ParamType::Reporter, ParamType::Reporter }, false, false, true });
    b.push_back({ "sub",    "math", "{0} - {1}",                 { ParamType::Reporter, ParamType::Reporter }, false, false, true });
    b.push_back({ "mul",    "math", "{0} \xC3\x97 {1}",          { ParamType::Reporter, ParamType::Reporter }, false, false, true });
    b.push_back({ "div",    "math", "{0} \xC3\xB7 {1}",          { ParamType::Reporter, ParamType::Reporter }, false, false, true });
    b.push_back({ "random", "math", "pick random {0} to {1}",    { ParamType::Number, ParamType::Number },   false, false, true });
    // ---- 逻辑 ----
    b.push_back({ "eq",  "logic", "{0} = {1}",    { ParamType::Reporter, ParamType::Reporter }, false, false, true });
    b.push_back({ "lt",  "logic", "{0} < {1}",    { ParamType::Reporter, ParamType::Reporter }, false, false, true });
    b.push_back({ "gt",  "logic", "{0} > {1}",    { ParamType::Reporter, ParamType::Reporter }, false, false, true });
    b.push_back({ "and", "logic", "{0} and {1}",  { ParamType::Reporter, ParamType::Reporter }, false, false, true });
    b.push_back({ "or",  "logic", "{0} or {1}",   { ParamType::Reporter, ParamType::Reporter }, false, false, true });
    b.push_back({ "not", "logic", "not {0}",      { ParamType::Reporter }, false, false, true });
    return b;
}

const std::vector<CategoryDef>& BlockLibrary::Categories() {
    static std::vector<CategoryDef> base = MakeCategories();
    static std::vector<CategoryDef> merged;
    merged = base;
    if (!g_headerBlocks.empty()) {
        merged.push_back({ "header", "Header", 0xFF7C4DFF });
    }
    return merged;
}

const std::vector<BlockDef>& BlockLibrary::Blocks() {
    static std::vector<BlockDef> blocks = MakeBlocks();
    return blocks;
}

const BlockDef* BlockLibrary::Find(const std::string& typeId) {
    for (const auto& d : Blocks()) if (d.typeId == typeId) return &d;
    for (const auto& d : g_headerBlocks) if (d.typeId == typeId) return &d;
    return nullptr;
}

const CategoryDef* BlockLibrary::FindCategory(const std::string& id) {
    for (const auto& c : Categories()) if (c.id == id) return &c;
    return nullptr;
}

std::vector<BlockDef> BlockLibrary::BlocksInCategory(const std::string& cat) {
    if (cat == "header") return g_headerBlocks;
    std::vector<BlockDef> out;
    for (const auto& d : Blocks()) if (d.category == cat) out.push_back(d);
    return out;
}

bool BlockLibrary::IsReporter(const std::string& typeId) {
    const BlockDef* d = Find(typeId);
    return d && d->isReporter;
}

// ---- 头文件动态积木 ----
void BlockLibrary::SetHeaderFunctions(const std::vector<std::pair<std::string, std::string>>& funcs,
                                      const std::vector<std::string>& headers) {
    g_headerBlocks.clear();
    g_headerFuncs.clear();
    g_headerIncludes = headers;

    // 通用「调用函数」积木：{0}=函数名，{1}=实参（单个表达式）
    BlockDef call;
    call.typeId = "callfunc";
    call.category = "header";
    call.label = "call function {0} ( {1} )";
    call.paramTypes = { ParamType::Text, ParamType::Reporter };
    call.hasChildren = false;
    call.hasElse = false;
    call.isReporter = false;
    g_headerBlocks.push_back(call);

    // 每个扫描到的函数一个快捷积木（无参调用）
    for (const auto& f : funcs) {
        BlockDef d;
        d.typeId = "call_" + f.first;
        d.category = "header";
        d.label = f.second.empty() ? f.first : f.second;
        d.paramTypes = {};
        d.hasChildren = false;
        d.hasElse = false;
        d.isReporter = false;
        g_headerBlocks.push_back(d);
        g_headerFuncs[d.typeId] = f.first;
    }
}

const std::vector<BlockDef>& BlockLibrary::HeaderBlocks() {
    return g_headerBlocks;
}

const std::vector<std::string>& BlockLibrary::HeaderIncludes() {
    return g_headerIncludes;
}

std::string BlockLibrary::HeaderFuncName(const std::string& typeId) {
    auto it = g_headerFuncs.find(typeId);
    return it != g_headerFuncs.end() ? it->second : std::string();
}

bool BlockLibrary::IsCallFunc(const std::string& typeId) {
    return typeId == "callfunc";
}

static Block MakeDefaultReporter() {
    Block n;
    n.typeId = "num";
    n.category = "math";
    n.label = "number";
    n.AddNumber("0");
    return n;
}

Block BlockLibrary::MakeBlock(const std::string& typeId) {
    const BlockDef* d = Find(typeId);
    Block b;
    if (!d) return b;
    b.typeId = d->typeId;
    b.category = d->category;
    b.label = d->label;
    for (auto pt : d->paramTypes) {
        switch (pt) {
            case ParamType::Text:    b.AddText(""); break;
            case ParamType::Number:  b.AddNumber("0"); break;
            case ParamType::Boolean: b.AddBool("true"); break;
            case ParamType::Reporter: b.AddReporter(MakeDefaultReporter()); break;
        }
    }
    b.RelinkReporters();
    return b;
}

Program BlockLibrary::MakeSampleProgram() {
    Program p;
    p.name = "MyProject";

    Block print1 = MakeBlock("print");
    print1.params[0].text = "Hello, VCBlocks!";

    Block setx = MakeBlock("set");
    setx.params[0].text = "x";
    // 值 = 数字 42
    Block num42 = MakeBlock("num");
    num42.params[0].text = "42";
    setx.reporters.clear();
    setx.params[1].type = ParamType::Reporter;
    setx.params[1].hasReporter = true;
    setx.reporters.push_back(num42);
    setx.RelinkReporters();

    Block repeat = MakeBlock("repeat");
    repeat.params[0].text = "10";
    Block printx = MakeBlock("printNum");
    Block varx = MakeBlock("var");
    varx.params[0].text = "x";
    printx.reporters.clear();
    printx.params[0].type = ParamType::Reporter;
    printx.params[0].hasReporter = true;
    printx.reporters.push_back(varx);
    printx.RelinkReporters();
    repeat.children.push_back(printx);

    p.blocks.push_back(print1);
    p.blocks.push_back(setx);
    p.blocks.push_back(repeat);
    return p;
}

} // namespace vcb
