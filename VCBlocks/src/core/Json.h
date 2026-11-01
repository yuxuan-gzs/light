// Json.h
// 轻量 JSON 解析/序列化（自包含，无第三方依赖）
#pragma once
#include <string>
#include <vector>
#include <map>

namespace vcb {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool   b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    bool IsNull()   const { return type == Type::Null; }
    bool IsBool()   const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray()  const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    bool Has(const std::string& k) const { return IsObject() && obj.count(k) > 0; }
    const JsonValue& Get(const std::string& k) const;
    JsonValue& GetRef(const std::string& k);

    std::string AsString(const std::string& def = "") const;
    double AsNumber(double def = 0.0) const;
    int AsInt(int def = 0) const;
    bool AsBool(bool def = false) const;

    static JsonValue MakeObject() { JsonValue v; v.type = Type::Object; return v; }
    static JsonValue MakeArray()  { JsonValue v; v.type = Type::Array; return v; }
    static JsonValue MakeString(const std::string& s) { JsonValue v; v.type = Type::String; v.str = s; return v; }
    static JsonValue MakeNumber(double n) { JsonValue v; v.type = Type::Number; v.num = n; return v; }
    static JsonValue MakeBool(bool b) { JsonValue v; v.type = Type::Bool; v.b = b; return v; }

    JsonValue& operator[](const std::string& k) { return GetRef(k); }
    void Set(const std::string& k, const JsonValue& v);
    void Push(const JsonValue& v) { arr.push_back(v); }
};

// 解析；失败返回 false 并填 err
bool ParseJson(const std::string& text, JsonValue& out, std::string& err);

// 序列化（带缩进）
std::string DumpJson(const JsonValue& v, int indent = 0);

} // namespace vcb
