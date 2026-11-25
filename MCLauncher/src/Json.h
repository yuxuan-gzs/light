// Json.h — 轻量 JSON 解析/序列化（无第三方依赖，支持 UTF-8）
// 类型：Null / Bool / Int / Double / String / Array / Object
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>

namespace json {

struct ParseError : std::runtime_error {
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

struct Value {
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    Type type = Type::Null;
    bool b = false;
    int64_t i = 0;
    double d = 0.0;
    std::string s;
    std::vector<Value> arr;
    std::map<std::string, Value, std::less<>> obj;

    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isInt() const { return type == Type::Int; }
    bool isDouble() const { return type == Type::Double; }
    bool isNumber() const { return type == Type::Int || type == Type::Double; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    double asDouble() const { return type == Type::Int ? (double)i : d; }
    int64_t asInt() const { return type == Type::Int ? i : (int64_t)d; }
    bool asBool() const { return b; }
    const std::string& asString() const { return s; }

    // 对象访问：返回默认空值引用（便于链式 .get()），不抛异常
    const Value& get(const char* key) const {
        static const Value kEmpty;
        if (type != Type::Object) return kEmpty;
        auto it = obj.find(key);
        return it == obj.end() ? kEmpty : it->second;
    }
    bool has(const char* key) const {
        return type == Type::Object && obj.find(key) != obj.end();
    }
    // 数组访问
    const Value& at(size_t idx) const {
        static const Value kEmpty;
        if (type != Type::Array || idx >= arr.size()) return kEmpty;
        return arr[idx];
    }
    size_t size() const { return type == Type::Array ? arr.size() : 0; }
};

// 解析（失败抛 ParseError）
Value parse(const std::string& text);

// 序列化为紧凑 JSON
std::string dump(const Value& v);

// 序列化为带缩进的 JSON
std::string dumpPretty(const Value& v);

// 便捷构造
Value makeObject();
Value makeArray();
Value makeString(const std::string& s);
Value makeInt(int64_t n);
Value makeDouble(double d);
Value makeBool(bool b);
Value makeNull();

} // namespace json
