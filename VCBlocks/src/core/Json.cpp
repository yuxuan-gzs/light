// Json.cpp
#include "Json.h"
#include "util.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace vcb {

const JsonValue& JsonValue::Get(const std::string& k) const {
    static JsonValue nullv;
    if (!IsObject()) return nullv;
    auto it = obj.find(k);
    return it == obj.end() ? nullv : it->second;
}

JsonValue& JsonValue::GetRef(const std::string& k) {
    if (type != Type::Object) { type = Type::Object; }
    return obj[k];
}

void JsonValue::Set(const std::string& k, const JsonValue& v) {
    if (type != Type::Object) type = Type::Object;
    obj[k] = v;
}

std::string JsonValue::AsString(const std::string& def) const {
    if (type == Type::String) return str;
    if (type == Type::Number) {
        char buf[64];
        if (num == (long long)num) snprintf(buf, sizeof(buf), "%lld", (long long)num);
        else snprintf(buf, sizeof(buf), "%g", num);
        return buf;
    }
    if (type == Type::Bool) return b ? "true" : "false";
    return def;
}
double JsonValue::AsNumber(double def) const { return type == Type::Number ? num : def; }
int JsonValue::AsInt(int def) const { return type == Type::Number ? (int)num : def; }
bool JsonValue::AsBool(bool def) const { return type == Type::Bool ? b : def; }

// ============================================================
// 解析器
// ============================================================
namespace {

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;

    explicit Parser(const std::string& text) : s(text) {}

    void SkipWs() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    }
    bool Fail(const std::string& m) { if (err.empty()) err = m; return false; }

    bool ParseValue(JsonValue& out) {
        SkipWs();
        if (i >= s.size()) return Fail("unexpected end of input");
        char c = s[i];
        if (c == '{') return ParseObject(out);
        if (c == '[') return ParseArray(out);
        if (c == '"') { std::string str; if (!ParseString(str)) return false; out.type = JsonValue::Type::String; out.str = str; return true; }
        if (c == 't' || c == 'f') return ParseBool(out);
        if (c == 'n') return ParseNull(out);
        return ParseNumber(out);
    }

    bool ParseObject(JsonValue& out) {
        out.type = JsonValue::Type::Object;
        out.obj.clear();
        ++i; // '{'
        SkipWs();
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        while (true) {
            SkipWs();
            if (i >= s.size() || s[i] != '"') return Fail("expected string key");
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (i >= s.size() || s[i] != ':') return Fail("expected ':'");
            ++i;
            JsonValue v;
            if (!ParseValue(v)) return false;
            out.obj[key] = v;
            SkipWs();
            if (i >= s.size()) return Fail("unterminated object");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; return true; }
            return Fail("expected ',' or '}'");
        }
    }

    bool ParseArray(JsonValue& out) {
        out.type = JsonValue::Type::Array;
        out.arr.clear();
        ++i; // '['
        SkipWs();
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        while (true) {
            JsonValue v;
            if (!ParseValue(v)) return false;
            out.arr.push_back(v);
            SkipWs();
            if (i >= s.size()) return Fail("unterminated array");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == ']') { ++i; return true; }
            return Fail("expected ',' or ']'");
        }
    }

    static bool IsHex(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }
    static unsigned HexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return c - 'A' + 10;
    }
    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    bool ParseString(std::string& out) {
        out.clear();
        if (i >= s.size() || s[i] != '"') return Fail("expected '\"'");
        ++i;
        while (true) {
            if (i >= s.size()) return Fail("unterminated string");
            char c = s[i];
            if (c == '"') { ++i; return true; }
            if (c == '\\') {
                ++i;
                if (i >= s.size()) return Fail("bad escape");
                char e = s[i];
                if (e == '"') out += '"';
                else if (e == '\\') out += '\\';
                else if (e == '/') out += '/';
                else if (e == 'b') out += '\b';
                else if (e == 'f') out += '\f';
                else if (e == 'n') out += '\n';
                else if (e == 'r') out += '\r';
                else if (e == 't') out += '\t';
                else if (e == 'u') {
                    if (i + 4 >= s.size()) return Fail("bad unicode escape");
                    unsigned cp = 0;
                    for (int k = 1; k <= 4; ++k) {
                        char h = s[i + k];
                        if (!IsHex(h)) return Fail("bad unicode escape");
                        cp = (cp << 4) | HexVal(h);
                    }
                    i += 4;
                    // 处理代理对
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
                        unsigned lo = 0;
                        bool ok = true;
                        for (int k = 3; k <= 6; ++k) {
                            char h = s[i + k];
                            if (!IsHex(h)) { ok = false; break; }
                            lo = (lo << 4) | HexVal(h);
                        }
                        if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            i += 6;
                        }
                    }
                    AppendUtf8(out, cp);
                }
                else return Fail("bad escape");
                ++i;
            }
            else { out += c; ++i; }
        }
    }

    bool ParseBool(JsonValue& out) {
        if (s.compare(i, 4, "true") == 0) { out.type = JsonValue::Type::Bool; out.b = true; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { out.type = JsonValue::Type::Bool; out.b = false; i += 5; return true; }
        return Fail("bad literal");
    }
    bool ParseNull(JsonValue& out) {
        if (s.compare(i, 4, "null") == 0) { out.type = JsonValue::Type::Null; i += 4; return true; }
        return Fail("bad literal");
    }
    bool ParseNumber(JsonValue& out) {
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
        std::string num = s.substr(start, i - start);
        if (num.empty()) return Fail("bad number");
        out.type = JsonValue::Type::Number;
        out.num = atof(num.c_str());
        return true;
    }
};

} // namespace

bool ParseJson(const std::string& text, JsonValue& out, std::string& err) {
    Parser p(text);
    if (!p.ParseValue(out)) { err = p.err; return false; }
    p.SkipWs();
    if (p.i != text.size()) { err = "trailing characters"; return false; }
    return true;
}

// ============================================================
// 序列化
// ============================================================
static std::string JsonEscape(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += (char)c;
                break;
        }
    }
    out += "\"";
    return out;
}

static void DumpValue(const JsonValue& v, std::string& out, int indent, int depth) {
    std::string pad(depth * 4, ' ');
    std::string pad2((depth + 1) * 4, ' ');
    switch (v.type) {
        case JsonValue::Type::Null: out += "null"; break;
        case JsonValue::Type::Bool: out += v.b ? "true" : "false"; break;
        case JsonValue::Type::Number: {
            char buf[64];
            if (v.num == (long long)v.num && fabs(v.num) < 1e15) snprintf(buf, sizeof(buf), "%lld", (long long)v.num);
            else snprintf(buf, sizeof(buf), "%g", v.num);
            out += buf;
            break;
        }
        case JsonValue::Type::String: out += JsonEscape(v.str); break;
        case JsonValue::Type::Array: {
            if (v.arr.empty()) { out += "[]"; break; }
            out += "[\n";
            for (size_t i = 0; i < v.arr.size(); ++i) {
                out += pad2;
                DumpValue(v.arr[i], out, indent, depth + 1);
                if (i + 1 < v.arr.size()) out += ",";
                out += "\n";
            }
            out += pad + "]";
            break;
        }
        case JsonValue::Type::Object: {
            if (v.obj.empty()) { out += "{}"; break; }
            out += "{\n";
            size_t idx = 0;
            for (const auto& kv : v.obj) {
                out += pad2 + JsonEscape(kv.first) + ": ";
                DumpValue(kv.second, out, indent, depth + 1);
                if (++idx < v.obj.size()) out += ",";
                out += "\n";
            }
            out += pad + "}";
            break;
        }
    }
}

std::string DumpJson(const JsonValue& v, int indent) {
    (void)indent;
    std::string out;
    DumpValue(v, out, indent, 0);
    return out;
}

} // namespace vcb
