// Json.cpp — JSON 解析与序列化实现
#include "Json.h"

#include <cstdio>
#include <cstring>
#include <cmath>

namespace json {

// ---------------------------------------------------------------
// UTF-8 工具
// ---------------------------------------------------------------
static void appendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

// ---------------------------------------------------------------
// 解析器
// ---------------------------------------------------------------
namespace {

class Parser {
public:
    explicit Parser(const std::string& t) : p_(t.data()), end_(t.data() + t.size()) {}

    Value parse() {
        skipWs();
        Value v = parseValue();
        skipWs();
        if (p_ != end_) fail("trailing characters after JSON");
        return v;
    }

private:
    const char* p_;
    const char* end_;

    [[noreturn]] void fail(const char* msg) const {
        throw ParseError(std::string("JSON parse error: ") + msg);
    }

    void skipWs() {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
    }

    bool peek(char c) const { return p_ < end_ && *p_ == c; }
    char take() { if (p_ >= end_) fail("unexpected end"); return *p_++; }
    void expect(char c) { if (!peek(c)) fail("unexpected char"); ++p_; }

    Value parseValue() {
        if (p_ >= end_) fail("unexpected end");
        char c = *p_;
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': { Value v; v.type = Value::Type::String; v.s = parseString(); return v; }
            case 't': expectWord("true"); { Value v; v.type = Value::Type::Bool; v.b = true; return v; }
            case 'f': expectWord("false"); { Value v; v.type = Value::Type::Bool; v.b = false; return v; }
            case 'n': expectWord("null"); return Value{};
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
                fail("unexpected character");
        }
    }

    void expectWord(const char* w) {
        while (*w) { if (take() != *w) fail("bad literal"); ++w; }
    }

    Value parseObject() {
        expect('{');
        Value v; v.type = Value::Type::Object;
        skipWs();
        if (peek('}')) { ++p_; return v; }
        for (;;) {
            skipWs();
            if (!peek('"')) fail("object key must be string");
            std::string key = parseString();
            skipWs();
            expect(':');
            skipWs();
            v.obj[key] = parseValue();
            skipWs();
            if (peek(',')) { ++p_; continue; }
            if (peek('}')) { ++p_; return v; }
            fail("expected ',' or '}'");
        }
    }

    Value parseArray() {
        expect('[');
        Value v; v.type = Value::Type::Array;
        skipWs();
        if (peek(']')) { ++p_; return v; }
        for (;;) {
            skipWs();
            v.arr.push_back(parseValue());
            skipWs();
            if (peek(',')) { ++p_; continue; }
            if (peek(']')) { ++p_; return v; }
            fail("expected ',' or ']'");
        }
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (p_ >= end_) fail("unterminated string");
            char c = take();
            if (c == '"') return out;
            if (c == '\\') {
                char e = take();
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = parseHex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // 高代理项，期望紧跟低代理项
                            if (p_ + 1 < end_ && p_[0] == '\\' && p_[1] == 'u') {
                                p_ += 2;
                                uint32_t lo = parseHex4();
                                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                } else {
                                    cp = 0xFFFD;
                                }
                            } else {
                                cp = 0xFFFD;
                            }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: fail("bad escape");
                }
            } else if ((unsigned char)c < 0x20) {
                fail("control char in string");
            } else {
                out.push_back(c);
            }
        }
    }

    uint32_t parseHex4() {
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = take();
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }

    Value parseNumber() {
        const char* start = p_;
        if (peek('-')) ++p_;
        while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
        bool isDouble = false;
        if (peek('.')) {
            isDouble = true;
            ++p_;
            while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
        }
        if (peek('e') || peek('E')) {
            isDouble = true;
            ++p_;
            if (peek('+') || peek('-')) ++p_;
            while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
        }
        std::string num(start, p_ - start);
        Value v;
        if (isDouble) {
            v.type = Value::Type::Double;
            v.d = strtod(num.c_str(), nullptr);
        } else {
            v.type = Value::Type::Int;
            v.i = strtoll(num.c_str(), nullptr, 10);
        }
        return v;
    }
};

} // namespace

Value parse(const std::string& text) {
    Parser p(text);
    return p.parse();
}

// ---------------------------------------------------------------
// 序列化
// ---------------------------------------------------------------
namespace {

void dumpValue(const Value& v, std::string& out) {
    switch (v.type) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.b ? "true" : "false"; break;
        case Value::Type::Int: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)v.i);
            out += buf;
            break;
        }
        case Value::Type::Double: {
            char buf[64];
            if (std::isnan(v.d) || std::isinf(v.d)) {
                out += "null";
            } else {
                snprintf(buf, sizeof(buf), "%.17g", v.d);
                out += buf;
            }
            break;
        }
        case Value::Type::String: {
            out += '"';
            for (unsigned char c : v.s) {
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
                        } else {
                            out.push_back((char)c);
                        }
                }
            }
            out += '"';
            break;
        }
        case Value::Type::Array: {
            out += '[';
            bool first = true;
            for (const auto& e : v.arr) {
                if (!first) out += ',';
                first = false;
                dumpValue(e, out);
            }
            out += ']';
            break;
        }
        case Value::Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& [k, val] : v.obj) {
                if (!first) out += ',';
                first = false;
                out += '"';
                for (unsigned char c : k) {
                    if (c == '"' || c == '\\') out += '\\';
                    out.push_back((char)c);
                }
                out += '"';
                out += ':';
                dumpValue(val, out);
            }
            out += '}';
            break;
        }
    }
}

void dumpValuePretty(const Value& v, std::string& out, int indent, int depth) {
    auto ind = [&](int d) {
        for (int k = 0; k < (indent <= 0 ? 0 : indent) * d; ++k) out += ' ';
    };
    switch (v.type) {
        case Value::Type::Array: {
            if (v.arr.empty()) { out += "[]"; return; }
            out += "[\n";
            bool first = true;
            for (const auto& e : v.arr) {
                if (!first) out += ",\n";
                first = false;
                ind(depth + 1);
                dumpValuePretty(e, out, indent, depth + 1);
            }
            out += '\n'; ind(depth); out += ']';
            break;
        }
        case Value::Type::Object: {
            if (v.obj.empty()) { out += "{}"; return; }
            out += "{\n";
            bool first = true;
            for (const auto& [k, val] : v.obj) {
                if (!first) out += ",\n";
                first = false;
                ind(depth + 1);
                out += '"';
                for (unsigned char c : k) {
                    if (c == '"' || c == '\\') out += '\\';
                    out.push_back((char)c);
                }
                out += "\": ";
                dumpValuePretty(val, out, indent, depth + 1);
            }
            out += '\n'; ind(depth); out += '}';
            break;
        }
        default:
            dumpValue(v, out);
            break;
    }
}

} // namespace

std::string dump(const Value& v) {
    std::string out;
    dumpValue(v, out);
    return out;
}

std::string dumpPretty(const Value& v) {
    std::string out;
    dumpValuePretty(v, out, 2, 0);
    return out;
}

Value makeObject() { Value v; v.type = Value::Type::Object; return v; }
Value makeArray() { Value v; v.type = Value::Type::Array; return v; }
Value makeString(const std::string& s) { Value v; v.type = Value::Type::String; v.s = s; return v; }
Value makeInt(int64_t n) { Value v; v.type = Value::Type::Int; v.i = n; return v; }
Value makeDouble(double d) { Value v; v.type = Value::Type::Double; v.d = d; return v; }
Value makeBool(bool b) { Value v; v.type = Value::Type::Bool; v.b = b; return v; }
Value makeNull() { return Value{}; }

} // namespace json
