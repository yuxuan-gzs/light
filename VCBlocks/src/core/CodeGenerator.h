// CodeGenerator.h
// 编译：把积木程序生成为 C 或 Java 代码
#pragma once
#include <string>
#include <vector>
#include "BlockModel.h"

namespace vcb {

// 目标语言
enum class TargetLang { C, Cpp, Java };

class CodeGenerator {
public:
    // 生成完整独立程序（含 main）。lang 指定目标语言。
    // outBlockStartLines（可选）：若提供，填每个顶层积木在生成代码中的起始行号（1-based，与 p.blocks 一一对应），
    // 用于编译错误跳转到对应积木。
    static std::string GenerateProgram(const Program& p, TargetLang lang, std::string& err,
                                       std::vector<int>* outBlockStartLines = nullptr);
    // 兼容旧接口：默认生成 C
    static std::string GenerateProgram(const Program& p, std::string& err);

    // 生成语句体（供 DLL 嵌入，使用 VBC_PRINT_TEXT/VBC_PRINT_NUM 宏输出；仅 C 语言）
    // outVars：收集到的变量名（调用方据此声明）
    static std::string GenerateBody(const Program& p, std::vector<std::string>& outVars, std::string& err);

    // 生成表达式（值积木）
    static std::string GenerateExpr(const Block& b, TargetLang lang, std::string& err);
    static std::string GenerateExpr(const Block& b, std::string& err); // 默认 C

    // 把用户输入的变量名规范化为合法标识符
    static std::string SanitizeVarName(const std::string& name);
    // 把项目名规范化为合法 Java 类名（与文件名一致）
    static std::string SanitizeJavaClassName(const std::string& name);
    // 规范化数字字面量（非法则返回 "0"）
    static std::string NormalizeNumber(const std::string& text);
};

} // namespace vcb
