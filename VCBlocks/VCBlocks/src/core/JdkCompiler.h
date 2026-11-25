// JdkCompiler.h
// Java 编译后端：检测本机 JDK（javac/java），调用 javac 编译积木生成的 Java 代码。
// 注：Java 编译器/运行时体积巨大（几十 MB），无法像 TCC 那样内嵌进 DLL，
//     故采用「检测系统 JDK + 调用 javac」的方式，本机需已安装 JDK。
#pragma once
#include <string>

namespace vcb {

class JdkCompiler {
public:
    // 检测本机 JDK（javac/java），返回是否可用
    static bool Init(std::string& err);
    static bool IsAvailable();
    static std::wstring JavacPath();
    static std::wstring JavaPath();

    // 编译单个 Java 源文件（.java）为 class，输出到 outDir
    static bool Compile(const std::wstring& srcPath, const std::wstring& outDir, std::string& log);
    // 运行主类（java -cp classDir className），捕获 stdout/stderr 到 log
    static bool Run(const std::wstring& classDir, const std::string& className, std::string& log);
};

} // namespace vcb
