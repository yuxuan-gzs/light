// DllBuilder.h
// 编译为 DLL：把积木程序生成为符合 light 项目 GUI 插件规范（YXG ABI）的 DLL 源码
#pragma once
#include <string>
#include "BlockModel.h"

namespace vcb {

class DllBuilder {
public:
    struct Options {
        std::string dllName;      // DLL 文件名（不含扩展名），建议 YXG_ 前缀
        std::string jsNamespace = "blocks"; // JS API 命名空间
        std::string version = "1.0.0";
        std::string author = "VCBlocks";
        std::string description;
        // 原生面板（light 侧边栏新 Tab）
        std::string panelTitle;              // 面板显示名（默认 = dllName）
        std::string panelIcon = "\xE2\x9C\xA8"; // emoji 图标（默认 ✨）
    };

    // 生成自包含的 DLL C 源码（内嵌 YXG ABI 定义，可直接编译）
    static std::string GenerateSource(const Program& p, const Options& opt, std::string& err);

    // 生成的 JS API 名称（<dllName>_run 规范化）
    static std::string ApiName(const Options& opt);
};

} // namespace vcb
