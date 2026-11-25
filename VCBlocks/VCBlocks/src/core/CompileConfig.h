// CompileConfig.h
// 编译配置：正式版/测试版、运行库（/MT /MTd）、优化、警告等级。
// 用于 C/C++ 编译后端（TCC/MSVC），持久化在 settings.json。
#pragma once
#include <string>

namespace vcb {

// 构建配置：正式版 / 测试版
enum class BuildConfig {
    Release = 0,   // 正式版：/O2 /MT，无调试信息
    Debug   = 1,   // 测试版：/Od /MTd，含调试信息
};

// 运行库
enum class RuntimeLib {
    StaticRelease = 0,   // /MT  静态链接，正式
    StaticDebug   = 1,   // /MTd 静态链接，调试
};

// 输出类型（针对 C/C++）：可执行文件 / 动态链接库
enum class OutputType {
    Exe = 0,   // 打包为 .exe 可执行文件
    Dll = 1,   // 打包为 .dll 动态链接库
};

struct CompileConfig {
    BuildConfig config = BuildConfig::Release;
    RuntimeLib runtime = RuntimeLib::StaticRelease;
    OutputType output = OutputType::Exe;   // 用户选择打包为 .dll 还是 .exe
    bool pluginDll = false;                 // 输出 .dll 时，是否生成 YXG 插件格式（导出 YXG_RegisterPlugin 等）
    int optimize = 2;      // 优化等级 0-3（Od/O1/O2/Ox）
    int warnLevel = 3;     // 警告等级 /W0-/W4
    bool runAfterBuild = false;   // 编译后自动运行
    bool symbols = false;         // 生成调试符号（Debug 时为 true）

    // 生成编译参数（MSVC 风格，/MT /MTd /O2 等）
    std::string MsBuildFlags() const {
        std::string f;
        if (config == BuildConfig::Debug) {
            f += "/Od /Zi";
            f += (runtime == RuntimeLib::StaticDebug) ? " /MTd" : " /MDd";
        } else {
            f += (optimize >= 3 ? " /Ox" : optimize >= 2 ? " /O2" : optimize >= 1 ? " /O1" : " /Od");
            f += (runtime == RuntimeLib::StaticDebug) ? " /MTd" : " /MT";
        }
        if (warnLevel >= 0 && warnLevel <= 4) f += " /W" + std::to_string(warnLevel);
        return f;
    }
};

} // namespace vcb
