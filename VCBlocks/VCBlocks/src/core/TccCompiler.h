// TccCompiler.h
// 内置编译系统：封装 TinyCC（libtcc），在插件内编译 C 代码为 exe/dll，不依赖外部 MSVC。
#pragma once
#include <string>

namespace vcb {

class TccCompiler {
public:
    // 初始化（动态加载 libtcc.dll 并定位 tcc 的 include/lib 目录），返回是否可用
    static bool Init(std::string& err);
    static bool IsAvailable();
    static std::wstring TccRootDir();  // tcc 目录绝对路径（插件 DLL 旁的 tcc\）

    // 编译 C 源码为 exe（extraInclude：额外的头文件搜索目录，如 libs\<lang>）
    static bool CompileToExe(const std::string& code, const std::wstring& outPath, std::string& log,
                             const std::wstring& extraInclude = L"");
    // 编译 C 源码为 dll
    static bool CompileToDll(const std::string& code, const std::wstring& outPath, std::string& log,
                             const std::wstring& extraInclude = L"");

private:
    struct Impl;
};

} // namespace vcb
