// BlockLibrary.h
// 内置积木库：积木定义、分类定义、默认积木工厂
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include "BlockModel.h"

namespace vcb {

// 积木定义（模板）
struct BlockDef {
    std::string typeId;
    std::string category;              // 分类 id
    std::string label;                 // 英文模板（含 {0}/{1}）
    std::vector<ParamType> paramTypes; // 参数槽类型
    bool hasChildren = false;          // 是否含 then 子栈
    bool hasElse = false;              // 是否含 else 子栈
    bool isReporter = false;           // 是否为“值”积木（表达式）
};

// 分类定义
struct CategoryDef {
    std::string id;
    std::string label;   // 英文标签
    uint32_t color;      // ARGB
};

class BlockLibrary {
public:
    static const std::vector<CategoryDef>& Categories();
    static const std::vector<BlockDef>& Blocks();
    static const BlockDef* Find(const std::string& typeId);
    static const CategoryDef* FindCategory(const std::string& id);
    static std::vector<BlockDef> BlocksInCategory(const std::string& cat);

    // 根据定义创建一个带默认参数的积木
    static Block MakeBlock(const std::string& typeId);
    // 创建默认示例程序
    static Program MakeSampleProgram();

    // 判断某 typeId 是否为“值”积木
    static bool IsReporter(const std::string& typeId);

    // ---- 头文件/库函数动态积木（「头文件」分类）----
    // funcs: 函数名 -> 中文显示名；headers: 已安装头文件名（用于生成 #include）。
    // 调用后侧边栏出现「头文件」分类。
    static void SetHeaderFunctions(const std::vector<std::pair<std::string, std::string>>& funcs,
                                   const std::vector<std::string>& headers);
    static const std::vector<BlockDef>& HeaderBlocks();
    static const std::vector<std::string>& HeaderIncludes();
    // typeId "call_<name>" -> 函数名；非头文件积木返回空串
    static std::string HeaderFuncName(const std::string& typeId);
    // 是否为「调用函数」通用积木（typeId == "callfunc"）
    static bool IsCallFunc(const std::string& typeId);
};

} // namespace vcb
