// CodeGenerator.cpp
#include "CodeGenerator.h"
#include "BlockLibrary.h"
#include "util.h"
#include <set>
#include <cctype>

namespace vcb {

std::string CodeGenerator::SanitizeVarName(const std::string& name) {
    std::string out;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        out += ok ? c : '_';
    }
    if (out.empty()) out = "v";
    if (out[0] >= '0' && out[0] <= '9') out = "v" + out;
    return out;
}

std::string CodeGenerator::SanitizeJavaClassName(const std::string& name) {
    std::string s = SanitizeVarName(name);
    if (s.empty()) s = "Main";
    // 首字母大写，符合 Java 类名约定（可选，但更规范）
    if (s[0] >= 'a' && s[0] <= 'z') s[0] = (char)(s[0] - 'a' + 'A');
    return s;
}

std::string CodeGenerator::NormalizeNumber(const std::string& text) {
    std::string t = Trim(text);
    if (t.empty()) return "0";
    bool valid = true;
    bool hasDigit = false;
    size_t i = 0;
    if (t[0] == '+' || t[0] == '-') i = 1;
    for (; i < t.size(); ++i) {
        char c = t[i];
        if (c >= '0' && c <= '9') hasDigit = true;
        else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') { /* 允许，但需含数字 */ }
        else { valid = false; break; }
    }
    if (!valid || !hasDigit) return "0";
    // 转换为整数友好的形式：若含小数点，保留，否则去掉多余符号
    return t;
}

namespace {

// ============================================================
// C 代码生成
// ============================================================

std::string ParamExpr(const Block& b, size_t paramIndex, std::string& err) {
    if (paramIndex >= b.params.size()) return "0";
    const Param& p = b.params[paramIndex];
    if (p.type == ParamType::Reporter) {
        if (p.reporter) return CodeGenerator::GenerateExpr(*p.reporter, err);
        return "0";
    }
    if (p.type == ParamType::Number) return "(" + CodeGenerator::NormalizeNumber(p.text) + ")";
    return "0";
}

std::string GenExpr(const Block& b, std::string& err);

std::string GenExpr(const Block& b, std::string& err) {
    const std::string& t = b.typeId;
    if (t == "num") {
        std::string v = b.params.empty() ? "0" : CodeGenerator::NormalizeNumber(b.params[0].text);
        return "(" + v + ")";
    }
    if (t == "var") {
        std::string name = CodeGenerator::SanitizeVarName(b.params.empty() ? "x" : b.params[0].text);
        return "(" + name + ")";
    }
    if (t == "add" || t == "sub" || t == "mul" || t == "div" ||
        t == "eq" || t == "lt" || t == "gt" || t == "and" || t == "or") {
        std::string a = ParamExpr(b, 0, err);
        std::string c = ParamExpr(b, 1, err);
        if (t == "add") return "(" + a + " + " + c + ")";
        if (t == "sub") return "(" + a + " - " + c + ")";
        if (t == "mul") return "(" + a + " * " + c + ")";
        if (t == "div") return "((" + c + ") != 0 ? (" + a + " / " + c + ") : 0)";
        if (t == "eq")  return "(" + a + " == " + c + ")";
        if (t == "lt")  return "(" + a + " < " + c + ")";
        if (t == "gt")  return "(" + a + " > " + c + ")";
        if (t == "and") return "(" + a + " && " + c + ")";
        if (t == "or")  return "(" + a + " || " + c + ")";
    }
    if (t == "not") return "(! " + ParamExpr(b, 0, err) + ")";
    if (t == "random") {
        std::string lo = b.params.size() > 0 ? CodeGenerator::NormalizeNumber(b.params[0].text) : "1";
        std::string hi = b.params.size() > 1 ? CodeGenerator::NormalizeNumber(b.params[1].text) : "10";
        return "((rand() % ((" + hi + ") - (" + lo + ") + 1)) + (" + lo + "))";
    }
    return "0";
}

void GenStatement(const Block& b, int& idc, std::string& out, int indent, std::string& err);

std::string Pad(int n) { return std::string(n * 4, ' '); }

static int CountLines(const std::string& s) {
    int n = 0;
    for (char c : s) if (c == '\n') ++n;
    return n;
}

void GenBlockList(const std::vector<Block>& list, int& idc, std::string& out, int indent, std::string& err,
                  std::vector<int>* lineStarts = nullptr) {
    for (const auto& b : list) {
        if (lineStarts) lineStarts->push_back(CountLines(out) + 1);
        GenStatement(b, idc, out, indent, err);
    }
}

void GenStatement(const Block& b, int& idc, std::string& out, int indent, std::string& err) {
    const std::string& t = b.typeId;
    std::string p = Pad(indent);
    if (t == "print") {
        std::string text = b.params.empty() ? "" : b.params[0].text;
        out += p + "VBC_PRINT_TEXT(\"" + EscapeCString(text) + "\");\n";
    }
    else if (t == "printNum") {
        out += p + "VBC_PRINT_NUM(" + ParamExpr(b, 0, err) + ");\n";
    }
    else if (t == "set") {
        std::string name = CodeGenerator::SanitizeVarName(b.params.empty() ? "x" : b.params[0].text);
        out += p + name + " = " + ParamExpr(b, 1, err) + ";\n";
    }
    else if (t == "change") {
        std::string name = CodeGenerator::SanitizeVarName(b.params.empty() ? "x" : b.params[0].text);
        out += p + name + " += " + ParamExpr(b, 1, err) + ";\n";
    }
    else if (t == "if") {
        out += p + "if (" + ParamExpr(b, 0, err) + ") {\n";
        GenBlockList(b.children, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "ifElse") {
        out += p + "if (" + ParamExpr(b, 0, err) + ") {\n";
        GenBlockList(b.children, idc, out, indent + 1, err);
        out += p + "} else {\n";
        GenBlockList(b.elseChildren, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "repeat") {
        std::string n = b.params.empty() ? "1" : CodeGenerator::NormalizeNumber(b.params[0].text);
        std::string ivar = "_i" + std::to_string(idc++);
        out += p + "for (int " + ivar + " = 0; " + ivar + " < (" + n + "); " + ivar + "++) {\n";
        GenBlockList(b.children, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "while") {
        out += p + "while (" + ParamExpr(b, 0, err) + ") {\n";
        GenBlockList(b.children, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "callfunc") {
        std::string fn = CodeGenerator::SanitizeVarName(b.params.empty() ? "" : b.params[0].text);
        if (fn.empty()) fn = "func";
        out += p + fn + "(" + ParamExpr(b, 1, err) + ");\n";
    }
    else if (t.rfind("call_", 0) == 0) {
        std::string fn = BlockLibrary::HeaderFuncName(t);
        if (fn.empty()) fn = CodeGenerator::SanitizeVarName(t.substr(5));
        out += p + fn + "();\n";
    }
    // 值积木出现在语句位置时忽略
}

// ============================================================
// Java 代码生成
// 说明：Java 的 boolean 与 int 分离，为避免类型推断，统一用 int 语义——
//       比较/逻辑运算返回 1/0（三元表达式），if/while 条件用 `!= 0` 包裹。
// ============================================================

void GenStatementJava(const Block& b, int& idc, std::string& out, int indent, std::string& err);
void GenBlockListJava(const std::vector<Block>& list, int& idc, std::string& out, int indent, std::string& err,
                      std::vector<int>* lineStarts = nullptr);

std::string ParamExprJava(const Block& b, size_t paramIndex, std::string& err) {
    if (paramIndex >= b.params.size()) return "0";
    const Param& p = b.params[paramIndex];
    if (p.type == ParamType::Reporter) {
        if (p.reporter) return CodeGenerator::GenerateExpr(*p.reporter, TargetLang::Java, err);
        return "0";
    }
    if (p.type == ParamType::Number) return "(" + CodeGenerator::NormalizeNumber(p.text) + ")";
    return "0";
}

std::string GenExprJava(const Block& b, std::string& err) {
    const std::string& t = b.typeId;
    if (t == "num") {
        std::string v = b.params.empty() ? "0" : CodeGenerator::NormalizeNumber(b.params[0].text);
        return "(" + v + ")";
    }
    if (t == "var") {
        std::string name = CodeGenerator::SanitizeVarName(b.params.empty() ? "x" : b.params[0].text);
        return "(" + name + ")";
    }
    if (t == "add" || t == "sub" || t == "mul" || t == "div" ||
        t == "eq" || t == "lt" || t == "gt" || t == "and" || t == "or") {
        std::string a = ParamExprJava(b, 0, err);
        std::string c = ParamExprJava(b, 1, err);
        if (t == "add") return "(" + a + " + " + c + ")";
        if (t == "sub") return "(" + a + " - " + c + ")";
        if (t == "mul") return "(" + a + " * " + c + ")";
        if (t == "div") return "((" + c + ") != 0 ? (" + a + " / " + c + ") : 0)";
        if (t == "eq")  return "((" + a + ") == (" + c + ") ? 1 : 0)";
        if (t == "lt")  return "((" + a + ") < (" + c + ") ? 1 : 0)";
        if (t == "gt")  return "((" + a + ") > (" + c + ") ? 1 : 0)";
        if (t == "and") return "(((" + a + ") != 0 && (" + c + ") != 0) ? 1 : 0)";
        if (t == "or")  return "(((" + a + ") != 0 || (" + c + ") != 0) ? 1 : 0)";
    }
    if (t == "not") return "((" + ParamExprJava(b, 0, err) + ") == 0 ? 1 : 0)";
    if (t == "random") {
        std::string lo = b.params.size() > 0 ? CodeGenerator::NormalizeNumber(b.params[0].text) : "1";
        std::string hi = b.params.size() > 1 ? CodeGenerator::NormalizeNumber(b.params[1].text) : "10";
        return "(_rand.nextInt((" + hi + ") - (" + lo + ") + 1) + (" + lo + "))";
    }
    return "0";
}

void GenStatementJava(const Block& b, int& idc, std::string& out, int indent, std::string& err) {
    const std::string& t = b.typeId;
    std::string p = Pad(indent);
    if (t == "print") {
        std::string text = b.params.empty() ? "" : b.params[0].text;
        out += p + "System.out.println(\"" + EscapeCString(text) + "\");\n";
    }
    else if (t == "printNum") {
        out += p + "System.out.println(" + ParamExprJava(b, 0, err) + ");\n";
    }
    else if (t == "set") {
        std::string name = CodeGenerator::SanitizeVarName(b.params.empty() ? "x" : b.params[0].text);
        out += p + name + " = " + ParamExprJava(b, 1, err) + ";\n";
    }
    else if (t == "change") {
        std::string name = CodeGenerator::SanitizeVarName(b.params.empty() ? "x" : b.params[0].text);
        out += p + name + " += " + ParamExprJava(b, 1, err) + ";\n";
    }
    else if (t == "if") {
        out += p + "if ((" + ParamExprJava(b, 0, err) + ") != 0) {\n";
        GenBlockListJava(b.children, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "ifElse") {
        out += p + "if ((" + ParamExprJava(b, 0, err) + ") != 0) {\n";
        GenBlockListJava(b.children, idc, out, indent + 1, err);
        out += p + "} else {\n";
        GenBlockListJava(b.elseChildren, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "repeat") {
        std::string n = b.params.empty() ? "1" : CodeGenerator::NormalizeNumber(b.params[0].text);
        std::string ivar = "_i" + std::to_string(idc++);
        out += p + "for (int " + ivar + " = 0; " + ivar + " < (" + n + "); " + ivar + "++) {\n";
        GenBlockListJava(b.children, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "while") {
        out += p + "while ((" + ParamExprJava(b, 0, err) + ") != 0) {\n";
        GenBlockListJava(b.children, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "callfunc") {
        std::string fn = CodeGenerator::SanitizeVarName(b.params.empty() ? "" : b.params[0].text);
        if (fn.empty()) fn = "func";
        out += p + fn + "(" + ParamExprJava(b, 1, err) + ");\n";
    }
    else if (t.rfind("call_", 0) == 0) {
        std::string fn = BlockLibrary::HeaderFuncName(t);
        if (fn.empty()) fn = CodeGenerator::SanitizeVarName(t.substr(5));
        out += p + fn + "();\n";
    }
}

void GenBlockListJava(const std::vector<Block>& list, int& idc, std::string& out, int indent, std::string& err,
                      std::vector<int>* lineStarts) {
    for (const auto& b : list) {
        if (lineStarts) lineStarts->push_back(CountLines(out) + 1);
        GenStatementJava(b, idc, out, indent, err);
    }
}

// ============================================================
// C++ 代码生成
// 说明：C++ 的 int 可直接参与逻辑判断（与 C 相同），表达式复用 C 的 GenExpr；
//       仅 print/printNum 改用 std::cout，头部改用 <iostream>。
// ============================================================

void GenStatementCpp(const Block& b, int& idc, std::string& out, int indent, std::string& err);
void GenBlockListCpp(const std::vector<Block>& list, int& idc, std::string& out, int indent, std::string& err,
                     std::vector<int>* lineStarts = nullptr);

void GenStatementCpp(const Block& b, int& idc, std::string& out, int indent, std::string& err) {
    const std::string& t = b.typeId;
    std::string p = Pad(indent);
    if (t == "print") {
        std::string text = b.params.empty() ? "" : b.params[0].text;
        out += p + "std::cout << \"" + EscapeCString(text) + "\" << std::endl;\n";
    }
    else if (t == "printNum") {
        out += p + "std::cout << " + ParamExpr(b, 0, err) + " << std::endl;\n";
    }
    else if (t == "set") {
        std::string name = CodeGenerator::SanitizeVarName(b.params.empty() ? "x" : b.params[0].text);
        out += p + name + " = " + ParamExpr(b, 1, err) + ";\n";
    }
    else if (t == "change") {
        std::string name = CodeGenerator::SanitizeVarName(b.params.empty() ? "x" : b.params[0].text);
        out += p + name + " += " + ParamExpr(b, 1, err) + ";\n";
    }
    else if (t == "if") {
        out += p + "if (" + ParamExpr(b, 0, err) + ") {\n";
        GenBlockListCpp(b.children, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "ifElse") {
        out += p + "if (" + ParamExpr(b, 0, err) + ") {\n";
        GenBlockListCpp(b.children, idc, out, indent + 1, err);
        out += p + "} else {\n";
        GenBlockListCpp(b.elseChildren, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "repeat") {
        std::string n = b.params.empty() ? "1" : CodeGenerator::NormalizeNumber(b.params[0].text);
        std::string ivar = "_i" + std::to_string(idc++);
        out += p + "for (int " + ivar + " = 0; " + ivar + " < (" + n + "); " + ivar + "++) {\n";
        GenBlockListCpp(b.children, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "while") {
        out += p + "while (" + ParamExpr(b, 0, err) + ") {\n";
        GenBlockListCpp(b.children, idc, out, indent + 1, err);
        out += p + "}\n";
    }
    else if (t == "callfunc") {
        std::string fn = CodeGenerator::SanitizeVarName(b.params.empty() ? "" : b.params[0].text);
        if (fn.empty()) fn = "func";
        out += p + fn + "(" + ParamExpr(b, 1, err) + ");\n";
    }
    else if (t.rfind("call_", 0) == 0) {
        std::string fn = BlockLibrary::HeaderFuncName(t);
        if (fn.empty()) fn = CodeGenerator::SanitizeVarName(t.substr(5));
        out += p + fn + "();\n";
    }
}

void GenBlockListCpp(const std::vector<Block>& list, int& idc, std::string& out, int indent, std::string& err,
                     std::vector<int>* lineStarts) {
    for (const auto& b : list) {
        if (lineStarts) lineStarts->push_back(CountLines(out) + 1);
        GenStatementCpp(b, idc, out, indent, err);
    }
}

} // namespace

std::string CodeGenerator::GenerateExpr(const Block& b, std::string& err) {
    return GenExpr(b, err);
}

std::string CodeGenerator::GenerateExpr(const Block& b, TargetLang lang, std::string& err) {
    if (lang == TargetLang::Java) return GenExprJava(b, err);
    return GenExpr(b, err);
}

std::string CodeGenerator::GenerateBody(const Program& p, std::vector<std::string>& outVars, std::string& err) {
    outVars = p.CollectVariables();
    int idc = 0;
    std::string out;
    GenBlockList(p.blocks, idc, out, 0, err);
    return out;
}

std::string CodeGenerator::GenerateProgram(const Program& p, std::string& err) {
    return GenerateProgram(p, TargetLang::C, err);
}

std::string CodeGenerator::GenerateProgram(const Program& p, TargetLang lang, std::string& err,
                                           std::vector<int>* outBlockStartLines) {
    if (outBlockStartLines) outBlockStartLines->clear();

    if (lang == TargetLang::Java) {
        std::vector<std::string> vars = p.CollectVariables();
        int idc = 0;
        std::vector<int> bodyLines;
        std::string body;
        GenBlockListJava(p.blocks, idc, body, 2, err, outBlockStartLines ? &bodyLines : nullptr);

        std::string cls = SanitizeJavaClassName(p.name);
        std::string out;
        out += "/* Auto-generated by VCBlocks */\n";
        out += "import java.util.Random;\n\n";
        out += "public class " + cls + " {\n";
        out += "    public static void main(String[] args) {\n";
        out += "        Random _rand = new Random();\n";
        for (const auto& v : vars) {
            out += "        int " + v + " = 0;\n";
        }
        if (!vars.empty()) out += "\n";
        int headerLines = CountLines(out);
        out += body;
        out += "    }\n";
        out += "}\n";
        if (outBlockStartLines) {
            for (int ln : bodyLines) outBlockStartLines->push_back(ln + headerLines);
        }
        return out;
    }

    // C++（iostream 风格）
    if (lang == TargetLang::Cpp) {
        std::vector<std::string> vars = p.CollectVariables();
        int idc = 0;
        std::vector<int> bodyLines;
        std::string body;
        GenBlockListCpp(p.blocks, idc, body, 1, err, outBlockStartLines ? &bodyLines : nullptr);

        std::string out;
        out += "/* Auto-generated by VCBlocks */\n";
        out += "#include <iostream>\n#include <cstdlib>\n#include <ctime>\n";
        for (const auto& h : BlockLibrary::HeaderIncludes()) {
            out += "#include \"" + h + "\"\n";
        }
        out += "\n";
        out += "int main() {\n";
        out += "    std::srand((unsigned)std::time(nullptr));\n";
        for (const auto& v : vars) {
            out += "    int " + v + " = 0;\n";
        }
        if (!vars.empty()) out += "\n";
        int headerLines = CountLines(out);
        out += body;
        out += "\n    return 0;\n";
        out += "}\n";
        if (outBlockStartLines) {
            for (int ln : bodyLines) outBlockStartLines->push_back(ln + headerLines);
        }
        return out;
    }

    // C（原有逻辑）
    std::vector<std::string> vars = p.CollectVariables();
    int idc = 0;
    std::vector<int> bodyLines;
    std::string body;
    GenBlockList(p.blocks, idc, body, 0, err, outBlockStartLines ? &bodyLines : nullptr);

    std::string out;
    out += "/* Auto-generated by VCBlocks */\n";
    out += "#include <stdio.h>\n#include <stdlib.h>\n#include <time.h>\n";
    for (const auto& h : BlockLibrary::HeaderIncludes()) {
        out += "#include \"" + h + "\"\n";
    }
    out += "\n";
    out += "#define VBC_PRINT_TEXT(s) printf(\"%s\\n\", s)\n";
    out += "#define VBC_PRINT_NUM(n) printf(\"%d\\n\", (int)(n))\n\n";
    out += "int main(void) {\n";
    out += "    srand((unsigned)time(NULL));\n";
    for (const auto& v : vars) {
        out += "    int " + v + " = 0;\n";
    }
    out += "\n";
    int headerLines = CountLines(out);
    out += body;
    out += "\n    return 0;\n";
    out += "}\n";
    if (outBlockStartLines) {
        for (int ln : bodyLines) outBlockStartLines->push_back(ln + headerLines);
    }
    return out;
}

} // namespace vcb
