// LoaderInstall.h — Fabric / Quilt 加载器安装器
// 通过 meta API 的 profile/json 端点直接获取标准 launcher version JSON，
// 写入 versions 目录并下载其 libraries（HMCL 的「自动安装」核心能力）。
#pragma once

#include <string>
#include <vector>
#include <functional>

namespace loader {

struct LoaderVersion {
    std::string version;   // 如 "0.15.11"
    bool stable = false;
};

// 获取某 MC 版本可用的加载器版本列表（quilt=true 用 Quilt meta，否则 Fabric）
std::vector<LoaderVersion> FetchVersions(const std::string& mcVersion,
                                         bool quilt, std::string& err);

// 构造安装后的版本 id（如 "fabric-loader-0.15.11-1.20.4"）
std::string VersionId(const std::string& mcVersion,
                      const std::string& loaderVersion, bool quilt);

// 安装加载器：下载 profile JSON → 写 version JSON → 下载 libraries。
// 返回安装后的版本 id；失败返回空并写 err。
// log 回调用于进度/日志（可选）。
std::string Install(const std::string& mcVersion, const std::string& loaderVersion,
                    bool quilt, const std::string& versionsDir,
                    const std::string& librariesDir,
                    const std::function<void(const std::string&)>& log,
                    std::string& err);

// ============================================================
// Forge（NeoForge 兼容）—— 需下载 installer jar 并运行 java
// ============================================================

// 获取某 MC 版本可用的 Forge 版本列表（完整名，如 "1.20.1-47.3.0"，倒序）
std::vector<std::string> FetchForgeVersions(const std::string& mcVersion,
                                            std::string& err);

// 安装 Forge：下载 installer jar → 运行 java -jar installer --installClient <gameDir>。
// fullVersion 形如 "1.20.1-47.3.0"；javaPath 为空则自动检测。
// 返回 true 表示安装成功（版本 id 由 installer 决定，安装后需刷新版本列表）。
bool InstallForge(const std::string& fullVersion,
                  const std::string& gameDir,
                  const std::string& javaPath,
                  const std::function<void(const std::string&)>& log,
                  std::string& err);

// ============================================================
// NeoForge（1.20.2+ 的 Forge 替代品）
// ============================================================

// 获取某 MC 版本可用的 NeoForge 版本列表（版本号去掉 "1." 前缀，如 1.21.4 → 21.4.x）
std::vector<std::string> FetchNeoForgeVersions(const std::string& mcVersion,
                                               std::string& err);

// 安装 NeoForge（与 Forge 相同：下载 installer → 运行 java）
bool InstallNeoForge(const std::string& fullVersion,
                     const std::string& gameDir,
                     const std::string& javaPath,
                     const std::function<void(const std::string&)>& log,
                     std::string& err);

} // namespace loader
