// ModManager.h — Mod 前置检测与补充（PCL2-pro 创新功能）
#pragma once

#include <string>
#include <vector>

namespace modmgr {

struct ModInfo {
    std::wstring file;              // jar 文件名
    std::string id;                 // mod id
    std::string name;
    std::string loader;             // "fabric" / "forge" / ""
    std::vector<std::string> depends; // 依赖的 mod id
};

// 扫描 mods 目录，解析每个 jar 的 id / 依赖
std::vector<ModInfo> ScanMods(const std::wstring& modsDir);

// 检测缺失的前置（依赖 id 不在已安装 mod 中，且排除 minecraft/java/loader 等内置 id）
std::vector<std::string> DetectMissing(const std::wstring& modsDir);

// 已知前置库别名（用于把常见依赖名映射到 Modrinth 搜索词）
struct Preset { const char* id; const char* hint; };
extern const std::vector<Preset>& Presets();

// Modrinth 搜索结果
struct SearchResult {
    std::string slug;
    std::string title;
    std::string description;
    std::string iconUrl;
};

// 通过 Modrinth API 搜索（projectType: "mod"/"resourcepack"/"shader"/"datapack"）
std::vector<SearchResult> SearchModrinth(const std::string& query, const std::string& projectType, std::string& err);

// 下载 mod 的最新匹配版本到 modsDir（按 slug）。返回下载后的文件名。
bool DownloadMod(const std::string& slug, const std::wstring& modsDir,
                 const std::string& loader, const std::string& mcVersion,
                 std::wstring& outFile, std::string& err);

} // namespace modmgr
