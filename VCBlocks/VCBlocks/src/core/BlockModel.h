// BlockModel.h
// 积木数据模型：积木类型、参数、嵌套，以及程序（积木集合）的序列化
#pragma once
#include <string>
#include <vector>
#include <utility>
#include "Json.h"

namespace vcb {

// 参数类型
enum class ParamType {
    Text,      // 文本字面量（变量名、打印内容等）
    Number,    // 数字字面量
    Boolean,   // 布尔字面量
    Reporter   // 内嵌一个“值”积木（表达式）
};

// 积木参数槽
struct Param {
    ParamType type = ParamType::Text;
    std::string text;      // Text/Number/Boolean 的字面量文本
    bool hasReporter = false;
    struct Block* reporter = nullptr; // 指向所在 Block 内嵌的 reporter（由 Block 管理内存）
};

// 积木
struct Block {
    std::string typeId;         // 稳定标识，如 "print"/"if"/"add"
    std::string category;       // 分类，如 "output"/"control"/"math"
    std::string label;          // 英文模板，含 {0}/{1} 占位符

    std::vector<Param> params;          // 参数槽（顺序）
    std::vector<Block> reporters;       // 内嵌值积木（每个 Reporter 参数占用一个）
    std::vector<Block> children;        // 子语句栈（if/repeat/while 体）
    std::vector<Block> elseChildren;    // ifElse 的 else 分支

    // 拷贝后必须重新链接 params 中的 reporter 指针（指向自身的 reporters 容器）
    Block() = default;
    Block(const Block& o)
        : typeId(o.typeId), category(o.category), label(o.label),
          params(o.params), reporters(o.reporters),
          children(o.children), elseChildren(o.elseChildren) {
        RelinkReporters();
    }
    Block& operator=(const Block& o) {
        if (this != &o) {
            typeId = o.typeId; category = o.category; label = o.label;
            params = o.params; reporters = o.reporters;
            children = o.children; elseChildren = o.elseChildren;
            RelinkReporters();
        }
        return *this;
    }

    // 将 params 中的 reporter 指针重绑到 reporters 向量（加载/拷贝后调用）
    void RelinkReporters();

    // 便捷：新增一个文本参数
    void AddText(const std::string& t) { Param p; p.type = ParamType::Text; p.text = t; params.push_back(p); }
    void AddNumber(const std::string& t) { Param p; p.type = ParamType::Number; p.text = t; params.push_back(p); }
    void AddBool(const std::string& t) { Param p; p.type = ParamType::Boolean; p.text = t; params.push_back(p); }
    void AddReporter(Block b) {
        Param p; p.type = ParamType::Reporter;
        p.hasReporter = true;
        reporters.push_back(std::move(b));
        p.reporter = &reporters.back();
        params.push_back(p);
    }

    std::string ToJson() const;
    JsonValue ToJsonValue() const;   // 直接构建 JsonValue（避免字符串往返）
    static bool FromJson(const JsonValue& v, Block& out, std::string& err);
};

// 程序 = 顶层积木列表
struct Program {
    std::string name;
    std::vector<Block> blocks;

    std::string ToJsonString() const;
    bool FromJsonString(const std::string& s, std::string& err);

    // 收集所有变量名（用于生成 C 声明）
    std::vector<std::string> CollectVariables() const;
};

} // namespace vcb
