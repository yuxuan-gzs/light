// Decompiler.cpp
#include "Decompiler.h"
#include "BlockLibrary.h"
#include "util.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace vcb {

namespace {

// ============================================================
// 去除注释与预处理指令（保留字符串字面量）
// ============================================================
std::string StripCommentsAndPreproc(const std::string& code) {
    std::string out;
    size_t i = 0;
    size_t n = code.size();
    bool atLineStart = true;

    while (i < n) {
        char c = code[i];
        // 预处理指令
        if (atLineStart && c == '#') {
            while (i < n && code[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && code[i + 1] == '/') {
            while (i < n && code[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && code[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(code[i] == '*' && code[i + 1] == '/')) ++i;
            i += 2;
            continue;
        }
        if (c == '"') {
            out += c; ++i;
            while (i < n) {
                out += code[i];
                if (code[i] == '\\' && i + 1 < n) { out += code[i + 1]; i += 2; continue; }
                if (code[i] == '"') { ++i; break; }
                ++i;
            }
            atLineStart = false;
            continue;
        }
        out += c;
        atLineStart = (c == '\n');
        ++i;
    }
    return out;
}

// ============================================================
// 词法分析
// ============================================================
struct Tok {
    enum Kind { Ident, Number, String, Punct, End } kind = End;
    std::string text;
};

static std::vector<Tok> Tokenize(const std::string& s) {
    std::vector<Tok> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (isspace((unsigned char)c)) { ++i; continue; }
        if (isalpha((unsigned char)c) || c == '_') {
            size_t st = i;
            while (i < n && (isalnum((unsigned char)s[i]) || s[i] == '_')) ++i;
            Tok t; t.kind = Tok::Ident; t.text = s.substr(st, i - st);
            out.push_back(t);
            continue;
        }
        if (isdigit((unsigned char)c)) {
            size_t st = i;
            while (i < n && (isdigit((unsigned char)s[i]) || s[i] == '.')) ++i;
            if (i < n && (s[i] == 'e' || s[i] == 'E')) {
                ++i;
                if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
                while (i < n && isdigit((unsigned char)s[i])) ++i;
            }
            Tok t; t.kind = Tok::Number; t.text = s.substr(st, i - st);
            out.push_back(t);
            continue;
        }
        if (c == '"') {
            ++i; std::string str;
            while (i < n && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < n) {
                    char e = s[i + 1];
                    switch (e) {
                        case 'n': str += '\n'; break;
                        case 't': str += '\t'; break;
                        case 'r': str += '\r'; break;
                        case '"': str += '"'; break;
                        case '\\': str += '\\'; break;
                        default: str += e; break;
                    }
                    i += 2; continue;
                }
                str += s[i]; ++i;
            }
            if (i < n) ++i; // 跳过结束引号
            Tok t; t.kind = Tok::String; t.text = str;
            out.push_back(t);
            continue;
        }
        // 多字符操作符
        static const char* multi[] = { "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=", "++", "--" };
        bool matched = false;
        for (const char* m : multi) {
            if (s.compare(i, 2, m) == 0) {
                Tok t; t.kind = Tok::Punct; t.text = m;
                out.push_back(t); i += 2; matched = true; break;
            }
        }
        if (matched) continue;
        // 单字符
        Tok t; t.kind = Tok::Punct; t.text = std::string(1, c);
        out.push_back(t); ++i;
    }
    Tok t; t.kind = Tok::End;
    out.push_back(t);
    return out;
}

// ============================================================
// 解析器
// ============================================================
struct Parser {
    std::vector<Tok> toks;
    size_t pos = 0;
    std::string err;

    explicit Parser(std::vector<Tok> t) : toks(std::move(t)) {}

    const Tok& peek(int off = 0) const {
        size_t p = pos + off;
        if (p >= toks.size()) return toks.back();
        return toks[p];
    }
    bool atEnd() const { return peek().kind == Tok::End; }
    const Tok& next() { const Tok& t = peek(); if (pos < toks.size() - 1) ++pos; return t; }
    bool accept(const std::string& punct) {
        if (peek().kind == Tok::Punct && peek().text == punct) { ++pos; return true; }
        return false;
    }
    bool acceptIdent(const std::string& name) {
        if (peek().kind == Tok::Ident && peek().text == name) { ++pos; return true; }
        return false;
    }
    void expect(const std::string& punct) {
        if (!accept(punct)) { if (err.empty()) err = "expected '" + punct + "'"; }
    }
    // 跳过直到某个标点（不消费该标点）
    void skipTo(const std::string& punct) {
        while (!atEnd() && !(peek().kind == Tok::Punct && peek().text == punct)) next();
    }
    // 跳过到匹配的右括号（当前应在 '(' 之后）
    void skipBalancedParens() {
        int depth = 1;
        while (!atEnd() && depth > 0) {
            const Tok& t = next();
            if (t.kind == Tok::Punct && t.text == "(") ++depth;
            else if (t.kind == Tok::Punct && t.text == ")") --depth;
        }
    }
};

static Block MakeNum(const std::string& n) {
    Block b = BlockLibrary::MakeBlock("num");
    b.params[0].text = n;
    return b;
}
static Block MakeVar(const std::string& name) {
    Block b = BlockLibrary::MakeBlock("var");
    b.params[0].text = name;
    return b;
}
static Block MakeBin(const std::string& typeId, const Block& a, const Block& b) {
    Block r = BlockLibrary::MakeBlock(typeId);
    if (r.reporters.size() >= 2) {
        r.reporters[0] = a;
        r.reporters[1] = b;
        r.RelinkReporters();
    }
    return r;
}
static Block MakeUn(const std::string& typeId, const Block& a) {
    Block r = BlockLibrary::MakeBlock(typeId);
    if (!r.reporters.empty()) {
        r.reporters[0] = a;
        r.RelinkReporters();
    }
    return r;
}
static void SetReporter(Block& b, size_t paramIndex, const Block& value) {
    // 找到 paramIndex 对应的 reporter 下标
    int repIdx = -1;
    for (size_t i = 0; i <= paramIndex && i < b.params.size(); ++i) {
        if (b.params[i].type == ParamType::Reporter) ++repIdx;
    }
    if (repIdx >= 0 && (size_t)repIdx < b.reporters.size()) {
        b.reporters[repIdx] = value;
        b.RelinkReporters();
    }
}

static bool IsTypeKeyword(const std::string& s) {
    static const char* types[] = { "int","long","short","char","float","double","unsigned","signed","void","bool","size_t","uint8_t","uint16_t","uint32_t","uint64_t","int8_t","int16_t","int32_t","int64_t","static","const","extern","register","volatile",
        "boolean","byte","String","Random","Scanner","Object","final","public","private","protected","class","interface","package","import" };
    for (const char* t : types) if (s == t) return true;
    return false;
}

static bool IsNum0(const Block& b) {
    return b.typeId == "num" && !b.params.empty() && b.params[0].text == "0";
}

struct DecompilerImpl {
    Parser p;
    bool java = false;

    explicit DecompilerImpl(std::vector<Tok> t, bool isJava) : p(std::move(t)), java(isJava) {}

    Block ParseExpr() {
        Block cond = ParseOr();
        // Java 三元：cond ? 1 : 0 -> cond；cond ? 0 : 1 -> not(cond)
        if (java && p.accept("?")) {
            Block thenV = ParseExpr();
            p.expect(":");
            Block elseV = ParseExpr();
            bool thenIsOne = thenV.typeId == "num" && !thenV.params.empty() && thenV.params[0].text == "1";
            bool elseIsZero = elseV.typeId == "num" && !elseV.params.empty() && elseV.params[0].text == "0";
            bool thenIsZero = thenV.typeId == "num" && !thenV.params.empty() && thenV.params[0].text == "0";
            bool elseIsOne = elseV.typeId == "num" && !elseV.params.empty() && elseV.params[0].text == "1";
            if (thenIsOne && elseIsZero) return cond;
            if (thenIsZero && elseIsOne) return MakeUn("not", cond);
            return MakeNum("0");
        }
        return cond;
    }

    Block ParseOr() {
        Block l = ParseAnd();
        while (p.accept("||")) { Block r = ParseAnd(); l = MakeBin("or", l, r); }
        return l;
    }
    Block ParseAnd() {
        Block l = ParseCompare();
        while (p.accept("&&")) { Block r = ParseCompare(); l = MakeBin("and", l, r); }
        return l;
    }
    Block ParseCompare() {
        Block l = ParseAddSub();
        while (p.peek().kind == Tok::Punct) {
            std::string op = p.peek().text;
            if (op != "==" && op != "!=" && op != "<" && op != ">" && op != "<=" && op != ">=") break;
            p.next();
            Block r = ParseAddSub();
            if (op == "==") {
                // Java: a == 0 -> not(a)（对应 not 积木 / 布尔转换）
                if (java && IsNum0(r)) l = MakeUn("not", l);
                else l = MakeBin("eq", l, r);
            }
            else if (op == "!=") {
                // Java: a != 0 -> a（剥壳）；否则 not(eq)
                if (java && IsNum0(r)) { /* 保持 l */ }
                else l = MakeUn("not", MakeBin("eq", l, r));
            }
            else if (op == "<") l = MakeBin("lt", l, r);
            else if (op == ">") l = MakeBin("gt", l, r);
            else if (op == "<=") l = MakeUn("not", MakeBin("gt", l, r));
            else if (op == ">=") l = MakeUn("not", MakeBin("lt", l, r));
        }
        return l;
    }
    Block ParseAddSub() {
        Block l = ParseMulDiv();
        while (p.peek().kind == Tok::Punct && (p.peek().text == "+" || p.peek().text == "-")) {
            std::string op = p.next().text;
            Block r = ParseMulDiv();
            l = MakeBin(op == "+" ? "add" : "sub", l, r);
        }
        return l;
    }
    Block ParseMulDiv() {
        Block l = ParseUnary();
        while (p.peek().kind == Tok::Punct && (p.peek().text == "*" || p.peek().text == "/")) {
            std::string op = p.next().text;
            Block r = ParseUnary();
            l = MakeBin(op == "*" ? "mul" : "div", l, r);
        }
        return l;
    }
    Block ParseUnary() {
        if (p.accept("!")) { Block e = ParseUnary(); return MakeUn("not", e); }
        if (p.accept("-")) { Block e = ParseUnary(); return MakeBin("sub", MakeNum("0"), e); }
        return ParsePrimary();
    }
    Block ParsePrimary() {
        if (p.accept("(")) { Block e = ParseExpr(); p.expect(")"); return e; }
        const Tok& t = p.peek();
        if (t.kind == Tok::Number) { p.next(); return MakeNum(t.text); }
        if (t.kind == Tok::String) { p.next(); return MakeNum("0"); } // 字符串值不支持，按 0
        if (t.kind == Tok::Ident) {
            std::string name = p.next().text;
            if (p.accept("(")) { p.skipBalancedParens(); return MakeNum("0"); } // 函数调用：暂不支持
            return MakeVar(name);
        }
        return MakeNum("0");
    }

    // 解析语句列表，直到遇到 endPunct（如 "}"），消费该 endPunct 后返回
    std::vector<Block> ParseStmtList(const std::string& endPunct) {
        std::vector<Block> out;
        while (!p.atEnd()) {
            if (p.peek().kind == Tok::Punct && p.peek().text == endPunct) { p.next(); break; }
            Block b = ParseStatement();
            if (!b.typeId.empty()) out.push_back(b);
        }
        return out;
    }

    Block ParseStatement() {
        const Tok& t = p.peek();
        if (t.kind == Tok::Punct) {
            if (t.text == "}") { p.next(); return {}; }
            if (t.text == "{") { p.next(); ParseStmtList("}"); return {}; }
            p.next(); // 其它：跳过
            return {};
        }
        if (t.kind != Tok::Ident) { p.next(); return {}; }
        std::string kw = t.text;
        if (kw == "if") return ParseIf();
        if (kw == "for") return ParseFor();
        if (kw == "while") return ParseWhile();
        if (kw == "return" || kw == "break" || kw == "continue" || kw == "else" || kw == "goto" || kw == "case") {
            SkipSimpleStatement();
            return {};
        }
        if (IsTypeKeyword(kw)) { SkipDeclaration(); return {}; }
        return ParseAssignOrCall();
    }

    void SkipSimpleStatement() {
        // 跳到 ';' 或 '{'（消费到分号为止）
        while (!p.atEnd()) {
            const Tok& t = p.next();
            if (t.kind == Tok::Punct && t.text == ";") return;
            if (t.kind == Tok::Punct && t.text == "}") { --p.pos; return; }
        }
    }
    void SkipDeclaration() {
        while (!p.atEnd()) {
            const Tok& t = p.next();
            if (t.kind == Tok::Punct && t.text == ";") return;
            if (t.kind == Tok::Punct && t.text == "{") { --p.pos; return; }
        }
    }

    Block ParseIf() {
        p.next(); // 'if'
        p.expect("(");
        Block cond = ParseExpr();
        p.expect(")");
        Block b = BlockLibrary::MakeBlock("if");
        SetReporter(b, 0, cond);
        if (p.accept("{")) {
            b.children = ParseStmtList("}");
        }
        if (p.acceptIdent("else")) {
            b.typeId = "ifElse";
            b.label = "if {0} then / else";
            if (p.peek().kind == Tok::Ident && p.peek().text == "if") {
                Block nested = ParseStatement();
                if (!nested.typeId.empty()) b.elseChildren.push_back(nested);
            } else if (p.accept("{")) {
                b.elseChildren = ParseStmtList("}");
            }
        }
        return b;
    }

    Block ParseFor() {
        p.next(); // 'for'
        p.expect("(");
        // init：跳过到第一个 ';'
        p.skipTo(";"); p.expect(";");
        // condition：期望形如 _i < N 或 _i <= N
        std::string count = "10";
        if (p.peek().kind == Tok::Ident) {
            p.next(); // 循环变量
            std::string op;
            if (p.peek().kind == Tok::Punct && (p.peek().text == "<" || p.peek().text == "<=")) {
                op = p.next().text;
                Block cnt = ParseExpr();
                if (cnt.typeId == "num") {
                    count = cnt.params.empty() ? "10" : cnt.params[0].text;
                    if (op == "<=") {
                        // N+1
                        double v = atof(count.c_str());
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%d", (int)(v + 1));
                        count = buf;
                    }
                }
            } else {
                p.skipTo(";");
            }
        } else {
            p.skipTo(";");
        }
        p.expect(";");
        // increment：跳到 ')'
        p.skipTo(")"); p.expect(")");

        Block b = BlockLibrary::MakeBlock("repeat");
        b.params[0].text = count;
        if (p.accept("{")) {
            b.children = ParseStmtList("}");
        }
        return b;
    }

    Block ParseWhile() {
        p.next(); // 'while'
        p.expect("(");
        Block cond = ParseExpr();
        p.expect(")");
        Block b = BlockLibrary::MakeBlock("while");
        SetReporter(b, 0, cond);
        if (p.accept("{")) {
            b.children = ParseStmtList("}");
        }
        return b;
    }

    Block ParseAssignOrCall() {
        std::string name = p.next().text;
        // Java: System.out.println(...)
        if (java && name == "System" && p.accept(".") && p.acceptIdent("out") && p.accept(".")) {
            if (p.acceptIdent("println") || p.acceptIdent("print")) {
                p.expect("(");
                if (p.peek().kind == Tok::String) {
                    std::string arg = p.next().text;
                    p.skipTo(")"); p.expect(")");
                    if (p.accept(";")) {}
                    Block b = BlockLibrary::MakeBlock("print");
                    b.params[0].text = arg;
                    return b;
                } else {
                    Block e = ParseExpr();
                    p.skipTo(")"); p.expect(")");
                    if (p.accept(";")) {}
                    Block b = BlockLibrary::MakeBlock("printNum");
                    SetReporter(b, 0, e);
                    return b;
                }
            }
            // 其它 System.xxx 调用：跳过
            SkipSimpleStatement();
            return {};
        }
        // 打印相关调用
        if (name == "VBC_PRINT_TEXT") {
            p.expect("(");
            std::string arg;
            if (p.peek().kind == Tok::String) arg = p.next().text;
            p.skipTo(")"); p.expect(")");
            if (p.accept(";")) {}
            Block b = BlockLibrary::MakeBlock("print");
            b.params[0].text = arg;
            return b;
        }
        if (name == "VBC_PRINT_NUM") {
            p.expect("(");
            Block e = ParseExpr();
            p.skipTo(")"); p.expect(")");
            if (p.accept(";")) {}
            Block b = BlockLibrary::MakeBlock("printNum");
            SetReporter(b, 0, e);
            return b;
        }
        if (name == "printf") {
            return ParsePrintf();
        }
        if (name == "puts") {
            p.expect("(");
            std::string arg;
            if (p.peek().kind == Tok::String) arg = p.next().text;
            p.skipTo(")"); p.expect(")");
            if (p.accept(";")) {}
            Block b = BlockLibrary::MakeBlock("print");
            b.params[0].text = arg;
            return b;
        }
        // 函数调用：跳过
        if (p.accept("(")) { p.skipBalancedParens(); if (p.accept(";")) {} return {}; }

        // 赋值
        if (p.accept("=")) {
            Block e = ParseExpr();
            if (p.accept(";")) {}
            Block b = BlockLibrary::MakeBlock("set");
            b.params[0].text = name;
            SetReporter(b, 1, e);
            return b;
        }
        if (p.accept("+=")) {
            Block e = ParseExpr();
            if (p.accept(";")) {}
            Block b = BlockLibrary::MakeBlock("change");
            b.params[0].text = name;
            SetReporter(b, 1, e);
            return b;
        }
        if (p.accept("-=")) {
            Block e = ParseExpr();
            if (p.accept(";")) {}
            Block b = BlockLibrary::MakeBlock("change");
            b.params[0].text = name;
            SetReporter(b, 1, MakeBin("sub", MakeNum("0"), e));
            return b;
        }
        if (p.accept("++")) { if (p.accept(";")) {} Block b = BlockLibrary::MakeBlock("change"); b.params[0].text = name; SetReporter(b, 1, MakeNum("1")); return b; }
        if (p.accept("--")) { if (p.accept(";")) {} Block b = BlockLibrary::MakeBlock("change"); b.params[0].text = name; SetReporter(b, 1, MakeNum("-1")); return b; }

        // 其它：跳过到分号
        SkipSimpleStatement();
        return {};
    }

    Block ParsePrintf() {
        p.expect("(");
        std::string fmt;
        if (p.peek().kind == Tok::String) fmt = p.next().text;
        bool hasPctS = fmt.find("%s") != std::string::npos;
        bool hasPctD = fmt.find("%d") != std::string::npos;
        std::string strArg;
        Block exprArg;
        bool hasExpr = false;
        bool hasStr = false;
        while (p.accept(",")) {
            if (p.peek().kind == Tok::String) { strArg = p.next().text; hasStr = true; }
            else { exprArg = ParseExpr(); hasExpr = true; }
        }
        p.skipTo(")"); p.expect(")");
        if (p.accept(";")) {}

        if (hasPctS && hasStr) {
            Block b = BlockLibrary::MakeBlock("print");
            b.params[0].text = strArg;
            return b;
        }
        if (hasPctD && hasExpr) {
            Block b = BlockLibrary::MakeBlock("printNum");
            SetReporter(b, 0, exprArg);
            return b;
        }
        if (hasStr) {
            Block b = BlockLibrary::MakeBlock("print");
            b.params[0].text = strArg;
            return b;
        }
        if (hasExpr) {
            Block b = BlockLibrary::MakeBlock("printNum");
            SetReporter(b, 0, exprArg);
            return b;
        }
        Block b = BlockLibrary::MakeBlock("print");
        b.params[0].text = fmt;
        return b;
    }
};

} // namespace

Program Decompiler::Decompile(const std::string& code, std::string& err) {
    return Decompile(code, TargetLang::C, err);
}

Program Decompiler::Decompile(const std::string& code, TargetLang lang, std::string& err) {
    Program prog;
    prog.name = "Decompiled";
    std::string cleaned = StripCommentsAndPreproc(code);
    auto toks = Tokenize(cleaned);
    DecompilerImpl d(std::move(toks), lang == TargetLang::Java);

    // 尝试定位 main 函数体；若无则解析整段语句
    size_t mainPos = std::string::npos;
    {
        // 在 token 流中找 "main"
        for (size_t i = 0; i < d.p.toks.size(); ++i) {
            if (d.p.toks[i].kind == Tok::Ident && d.p.toks[i].text == "main") { mainPos = i; break; }
        }
    }
    if (mainPos != std::string::npos) {
        // 跳过 main ( ... ) {
        d.p.pos = mainPos + 1;
        if (d.p.peek().kind == Tok::Punct && d.p.peek().text == "(") {
            d.p.next();                 // 消费 '('
            d.p.skipBalancedParens();   // 跳到匹配的 ')'
            if (d.p.accept("{")) {
                prog.blocks = d.ParseStmtList("}");
            }
        }
    } else {
        // 无 main：若以 { 开头则剥掉，否则直接解析
        if (d.p.accept("{")) {
            prog.blocks = d.ParseStmtList("}");
        } else {
            prog.blocks = d.ParseStmtList("");
        }
    }
    err = d.p.err;
    return prog;
}

} // namespace vcb
