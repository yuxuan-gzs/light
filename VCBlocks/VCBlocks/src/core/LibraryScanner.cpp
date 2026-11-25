// LibraryScanner.cpp
#include "LibraryScanner.h"
#include "util.h"
#include <cctype>
#include <set>

namespace vcb {

namespace {

struct STok {
    int kind = 0;      // 0=ident,1=number,2=string,3=punct,4=comment
    std::string text;
    size_t start = 0, end = 0;
};

// 轻量词法：记录标识符/数字/字符串/标点/注释及其位置
std::vector<STok> TokenizeLib(const std::string& s) {
    std::vector<STok> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (isspace((unsigned char)c)) { ++i; continue; }
        if (isalpha((unsigned char)c) || c == '_') {
            size_t st = i;
            while (i < n && (isalnum((unsigned char)s[i]) || s[i] == '_')) ++i;
            STok t; t.kind = 0; t.text = s.substr(st, i - st); t.start = st; t.end = i;
            out.push_back(t);
            continue;
        }
        if (isdigit((unsigned char)c)) {
            size_t st = i;
            while (i < n && (isalnum((unsigned char)s[i]) || s[i] == '.')) ++i;
            STok t; t.kind = 1; t.text = s.substr(st, i - st); t.start = st; t.end = i;
            out.push_back(t);
            continue;
        }
        if (c == '"') {
            size_t st = i; ++i;
            while (i < n && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < n) i += 2; else ++i;
            }
            if (i < n) ++i;
            STok t; t.kind = 2; t.text = s.substr(st, i - st); t.start = st; t.end = i;
            out.push_back(t);
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            size_t st = i;
            while (i < n && s[i] != '\n') ++i;
            STok t; t.kind = 4; t.text = s.substr(st + 2, i - st - 2); t.start = st; t.end = i;
            out.push_back(t);
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            size_t st = i; i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) ++i;
            i += 2;
            std::string body = s.substr(st + 2, (i > st + 4 ? i - st - 4 : 0));
            STok t; t.kind = 4; t.text = body; t.start = st; t.end = i;
            out.push_back(t);
            continue;
        }
        // 标点
        STok t; t.kind = 3; t.text = std::string(1, c); t.start = i; t.end = i + 1;
        out.push_back(t);
        ++i;
    }
    return out;
}

bool IsControlKeyword(const std::string& w) {
    static const std::set<std::string> k = {
        "if","for","while","switch","return","else","do","case","default","goto",
        "struct","typedef","enum","union","namespace","class","public","private","protected",
        "template","typename","using","new","delete","sizeof","break","continue","const","static","extern","inline",
        "operator","friend","virtual","override","final"
    };
    return k.count(w) > 0;
}

std::string CleanComment(std::string c) {
    // 去除每行开头的 * 与空白
    std::vector<std::string> lines = SplitLines(c);
    std::vector<std::string> cleaned;
    for (auto& ln : lines) {
        std::string s = Trim(ln);
        while (!s.empty() && (s[0] == '*' || s[0] == '/')) s = Trim(s.substr(1));
        if (!s.empty()) cleaned.push_back(s);
    }
    return Join(cleaned, " ");
}

} // namespace

std::string LibraryScanner::TranslateSentence(const std::string& locale, const std::string& text) {
    if (locale == "en") return text;
    std::string out;
    std::string word;
    auto flush = [&]() {
        if (!word.empty()) {
            const char* t = Translator::BuiltinWord(locale, ToLowerAscii(word));
            out += (t && t[0]) ? t : word;
            word.clear();
        }
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (isalnum((unsigned char)c) || c == '_') { word += c; continue; }
        flush();
        out += c;
    }
    flush();
    return out;
}

bool LibraryScanner::Scan(const std::string& code, const std::string& libraryName,
                          const std::string& language, const std::string& locale,
                          Result& out, std::string& err) {
    out = Result();
    out.library = libraryName.empty() ? "library" : libraryName;
    out.language = language;
    out.locale = locale;

    auto toks = TokenizeLib(code);
    size_t n = toks.size();

    for (size_t i = 0; i + 2 < n; ++i) {
        const STok& name = toks[i];
        if (name.kind != 0) continue;
        if (IsControlKeyword(name.text)) continue;
        // 后面必须是 '('
        if (!(toks[i + 1].kind == 3 && toks[i + 1].text == "(")) continue;
        // 前面必须有返回类型（标识符或 *），且不是控制关键字
        if (i == 0) continue;
        const STok& prev = toks[i - 1];
        if (prev.kind == 3 && prev.text == "*") {
            // char* foo(...)   ok
        } else if (prev.kind == 0) {
            if (IsControlKeyword(prev.text)) continue; // return foo(...) 等
        } else {
            continue; // 前面是 '(' '=' ';' '{' '}' 等 -> 调用
        }

        // 回退收集返回类型
        size_t typeStart = i - 1;
        while (typeStart > 0) {
            const STok& t = toks[typeStart - 1];
            if (t.kind == 0 || (t.kind == 3 && (t.text == "*" || t.text == "&"))) {
                --typeStart;
            } else break;
        }
        std::string retType;
        for (size_t k = typeStart; k < i; ++k) retType += toks[k].text + " ";
        retType = Trim(retType);

        // 找匹配的 ')'（考虑嵌套）
        size_t j = i + 2;
        int depth = 1;
        while (j < n && depth > 0) {
            if (toks[j].kind == 3 && toks[j].text == "(") ++depth;
            else if (toks[j].kind == 3 && toks[j].text == ")") --depth;
            if (depth == 0) break;
            ++j;
        }
        if (j >= n) continue; // 未找到
        size_t closePos = j;
        // 后面必须是 '{' 或 ';'
        size_t after = closePos + 1;
        while (after < n && toks[after].kind == 4) ++after; // 跳过注释
        bool isDef = false, isDecl = false;
        if (after < n && toks[after].kind == 3 && toks[after].text == "{") isDef = true;
        else if (after < n && toks[after].kind == 3 && toks[after].text == ";") isDecl = true;
        if (!isDef && !isDecl) continue;

        std::string params = code.substr(toks[i + 1].end, toks[closePos].start - toks[i + 1].end);
        params = Trim(params);

        // 前置注释（最近的注释 token）
        std::string comment;
        for (size_t k = typeStart; k > 0; --k) {
            if (toks[k - 1].kind == 4) { comment = CleanComment(toks[k - 1].text); break; }
            // 允许跳过少量空白，但遇到其它 token 则停
            if (toks[k - 1].kind != 0 && toks[k - 1].kind != 1) break;
        }

        Function f;
        f.name = name.text;
        f.returnType = retType;
        f.params = params;
        f.signature = (retType.empty() ? "" : retType + " ") + name.text + "(" + params + ")";
        f.comment = comment;
        out.functions.push_back(f);
        i = closePos; // 跳过已处理区域
    }

    // 生成翻译
    for (const auto& f : out.functions) {
        std::string tr = Translator::TranslateIdentifier(locale, f.name);
        out.translations[f.name] = tr;
    }
    return true;
}

std::string LibraryScanner::BuildTranslationJson(const Result& r) {
    JsonValue root = JsonValue::MakeObject();
    root.Set("name", JsonValue::MakeString(r.library));
    root.Set("language", JsonValue::MakeString(r.language));
    root.Set("locale", JsonValue::MakeString(r.locale));

    JsonValue strings = JsonValue::MakeObject();
    for (const auto& f : r.functions) {
        auto it = r.translations.find(f.name);
        strings.Set(f.name, JsonValue::MakeString(it != r.translations.end() ? it->second : f.name));
    }
    root.Set("strings", strings);

    JsonValue arr = JsonValue::MakeArray();
    for (const auto& f : r.functions) {
        JsonValue o = JsonValue::MakeObject();
        o.Set("name", JsonValue::MakeString(f.name));
        o.Set("signature", JsonValue::MakeString(f.signature));
        o.Set("returnType", JsonValue::MakeString(f.returnType));
        o.Set("params", JsonValue::MakeString(f.params));
        o.Set("description", JsonValue::MakeString(f.comment));
        auto it = r.translations.find(f.name);
        o.Set("translation", JsonValue::MakeString(it != r.translations.end() ? it->second : f.name));
        arr.Push(o);
    }
    root.Set("functions", arr);

    return DumpJson(root);
}

} // namespace vcb
