// Decompiler.h
// 反编译：把（本工具生成的）C / C++ / Java 代码解析回积木程序
#pragma once
#include <string>
#include "BlockModel.h"
#include "CodeGenerator.h"

namespace vcb {

class Decompiler {
public:
    // 从源码反编译为积木程序；lang 指定源语言，err 非空表示失败
    static Program Decompile(const std::string& code, TargetLang lang, std::string& err);
    // 默认按 C 反编译
    static Program Decompile(const std::string& code, std::string& err);
};

} // namespace vcb
