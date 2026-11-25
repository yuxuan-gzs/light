// JavaDetect.h — 检测本机已安装的 Java 运行时，并可下载官方运行时
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

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

// 返回最合适的 Java（偏好 >= minMajor 的 64 位），找不到返回空。
// satisfied 非空时返回「是否真的找到了 >= minMajor 的运行时」
JavaRuntime DetectBest(int minMajor = 8, bool* satisfied = nullptr);

// 精确主版本（8 只认 8，不会返回 17/21）。装老版本 Forge 时 installer 在 17+ 下必崩，
// 所以这里必须精确匹配。返回空表示没装这个版本。
JavaRuntime DetectExact(int major);

// 探测单个 java.exe / javaw.exe
bool Probe(const std::string& exePath, JavaRuntime& out);

// 校验给定路径是否是有效 Java：空字符串视为有效（表示自动检测）；
// 目录则检查 bin/java.exe 或 bin/javaw.exe，文件则检查是否存在。
bool IsValidJava(const std::string& path);

// ---- 自动下载官方运行时（Mojang java-runtime 清单，国内走 BMCLAPI 镜像）----
// 已下载的运行时根目录（<主程序>\Data\mlc\java）
std::wstring RuntimeRootW();
// 查找已下载的、major 匹配的运行时；返回 javaw.exe 路径（空 = 没装）
std::string FindRuntime(int major);
// 进度回调：stage 为阶段文字，done/total 为文件数；返回 false 表示取消
using RuntimeProgress = std::function<bool(const std::string& stage, int64_t done, int64_t total)>;
// 下载并解压（本质是逐个文件下载）一个 major 版本的运行时；成功写入 outPath
bool InstallRuntime(int major, const RuntimeProgress& progress, std::string& outPath,
                    std::string& err);

} // namespace java
