// MsvcCompiler.h
// C++ 编译后端：检测本机 MSVC（cl.exe），编译积木生成的 C++ 代码为 exe。
// 注：C++ 无轻量级"自带"编译器（TCC 无 C++ 前端），故走「检测系统 MSVC + 调用 cl.exe」。
#pragma once
#include <string>

namespace vcb {

class MsvcCompiler {
public:
    // 检测本机 MSVC（cl.exe、INCLUDE、LIB），返回是否可用
    static bool Init(std::string& err);
    static bool IsAvailable();
    static std::wstring ClPath();

    // 编译 C++ 源文件为 exe（extraFlags：附加编译参数，如 /MT /O2 /W3 等）
    static bool CompileFile(const std::wstring& srcPath, const std::wstring& outExe, std::string& log,
                            const std::string& extraFlags = "");
};

} // namespace vcb
