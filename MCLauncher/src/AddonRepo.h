// AddonRepo.h — HMCL 兼容的远程附加组件仓库（Modrinth）
// 搜索/排序/分页、中文项目映射、版本分组、依赖、下载校验，均对齐 HMCL 的实现。
#pragma once

#include <string>
#include <vector>

#include "Http.h"

namespace addon {

// 与 RemoteAddonRepository.SortType 一一对应，顺序不能改（UI 下拉按此顺序）。
enum class Sort { Relevancy = 0, Popularity, DateCreated, LastUpdated, TotalDownloads };

struct Category {
    std::string id;          // Modrinth category name，如 fabric / adventure
    std::string localized;   // 中文显示名（HMCL I18N）
};

struct Hit {
    std::string slug;
    std::string projectId;
    std::string title;
    std::string desc;
    std::string iconUrl;
    std::string author;
    std::vector<std::string> categories;  // display_categories，已按 HMCL TAG_COMPARATOR 排序
    std::string cover;       // 本地缓存后的封面路径（UI 层填充）
    std::string pageUrl;
};

struct SearchResult {
    std::vector<Hit> hits;
    int totalPages = 0;
};

struct Dependency {
    enum class Type { Required, Optional, Embedded, Incompatible, Broken };
    Type type = Type::Required;
    std::string projectId;
    std::string versionId;
    std::string name;        // 异步解析出的项目名
};

struct Version {
    std::string id;
    std::string projectId;
    std::string name;        // HMCL 用 name（不是 version_number）作标题
    std::string versionNumber;
    std::string datePublished;
    std::string versionType; // release / beta / alpha
    std::string fileName;
    std::string url;
    std::string md5, sha1, sha256, sha512;
    std::vector<std::string> gameVersions;
    std::vector<std::string> loaders;
    std::vector<Dependency> dependencies;
};

// 搜索。page 从 0 开始，每页固定 50 条（与 HMCL DownloadListPage.search 一致）。
// gameVersion / category 留空表示不筛选。
SearchResult Search(const std::string& query, const std::string& gameVersion,
                    const std::string& category, int page, Sort sort, std::string& err);

// GET /v2/tag/category，只留 project_type == mod，按 HMCL 的本地化表翻译。
std::vector<Category> GetCategories(std::string& err);

// GET /v2/project/{id}/version?include_changelog=false
std::vector<Version> GetVersions(const std::string& projectId, std::string& err);

// 依赖项目名解析（HMCL 的 resolveDependency）；带进程内缓存。
std::string ResolveProjectName(const std::string& projectId);

// GET /v2/version/{id} 的 changelog 字段。
std::string GetChangelog(const std::string& versionId, std::string& err);

// 下载到指定完整路径（不覆盖已存在文件由调用方负责，与 HMCL 的弹窗校验一致）。
// 按 HMCL 的顺序校验 md5 > sha1 > sha256 > sha512；失败重试最多 5 次。
bool Download(const Version& v, const std::string& destPath, std::string& err,
              http::ProgressFn progress = nullptr);

// ---- 本地化 / 兼容判断 ----
// 中文项目名（MC 百科映射表），无映射时返回原英文标题。
std::string LocalizedTitle(const std::string& projectIdOrSlug, const std::string& fallback);
std::string LocalizedCategory(const std::string& id);
std::string TranslateLoader(const std::string& loader);
bool IsKnownLoader(const std::string& s);

// 当前实例支持的加载器集合（含 HMCL 的 Quilt→Fabric / NeoForge 1.20.1→Forge /
// Kilt / Sinytra Connector 兼容特例）。modsDir 用于探测已安装的 kilt/connector。
std::vector<std::string> SupportedLoaders(const std::string& loader,
                                          const std::string& gameVersion,
                                          const std::string& modsDir);

// 实例版本: MC 正式版 id -> 是否快照（用于「Minecraft x (快照)」分组标题）
bool IsSnapshotVersion(const std::string& gameVersion);

} // namespace addon
