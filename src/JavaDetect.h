// JavaDetect.h — 检测本机已安装的 Java 运行时
#pragma once

#include <string>
#include <vector>

namespace java {

struct JavaRuntime {
    std::string path;       // java.exe / javaw.exe 绝对路径
    std::string home;       // JRE/JDK 根目录
    std::string version;    // 版本字符串，如 "1.8.0_401" 或 "17.0.10"
    int major = 0;          // 主版本号，如 8 / 17 / 21
    bool is64 = false;
};

// 检测所有可用 Java；优先排序（64 位优先，其次版本号降序）
std::vector<JavaRuntime> DetectAll();

// 返回最合适的 Java（偏好 >= minMajor 的 64 位），找不到返回空
JavaRuntime DetectBest(int minMajor = 8);

// 校验给定路径是否是有效 Java：空字符串视为有效（表示自动检测）；
// 目录则检查 bin/java.exe 或 bin/javaw.exe，文件则检查是否存在。
bool IsValidJava(const std::string& path);

} // namespace java
