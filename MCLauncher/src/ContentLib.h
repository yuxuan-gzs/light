// ContentLib.h — 内容库：模组 / 资源包 / 光影 / 世界 / 原理图 的扫描、封面、启停、下载
// 布局对齐 HMCL：卡片网格 + 封面 + 启用开关；Modrinth 搜索支持中英双语关键词。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace content {

enum class Kind { Mods, ResourcePacks, Shaders, Worlds, Schematics };

// 一条内容（一个文件或一个目录）
struct Item {
    std::string key;        // 磁盘上的名字（模组带 .jar，世界是目录名）
    std::string title;      // 展示名
    std::string desc;       // 副标题（版本/作者/大小等）
    std::string cover;      // 本地封面绝对路径（空 = 用占位图案）
    std::string badge;      // 角标（加载器 / 版本）
    bool enabled = true;
    bool toggleable = true; // 世界/原理图不可启停
    int64_t size = 0;
};

// 扫描目录。cacheSub 用于封面提取缓存（如 "mods"）
std::vector<Item> Scan(const std::string& dir, Kind kind);
// 启用/停用（模组改扩展名 .disabled；资源包改 options.txt）
bool Toggle(const std::string& dir, Kind kind, const std::string& key, bool enable,
            std::string& err);
// 资源包上下排序（HMCL 的做法：改 options.txt 里 resourcePacks 列表的顺序，越靠前优先级越高）
bool Move(const std::string& dir, Kind kind, const std::string& key, bool up,
          std::string& err);
// 删除条目（文件或目录）
bool Remove(const std::string& dir, Kind kind, const std::string& key, std::string& err);
// 从本地文件导入（世界支持 .zip 解压为文件夹）
// preferName：世界的目标文件夹名（空 = 用压缩包文件名）
bool Import(const std::string& dir, Kind kind, const std::wstring& srcFile,
            std::string& outKey, std::string& err,
            const std::string& preferName = "");
// 备份世界目录到 Data/mlc/backups/<tag>/
bool BackupWorld(const std::string& worldDir, const std::string& tag, std::string& outPath,
                 std::string& err);
// 打开所在目录
void OpenFolder(const std::string& dir);

// ---------------------------------------------------------------
// Modrinth
// ---------------------------------------------------------------
struct Hit {
    std::string slug;
    std::string title;
    std::string desc;
    std::string iconUrl;
    std::string author;
    std::string cover;      // 下载到本地缓存后的路径
    int64_t downloads = 0;
};

// projectType: mod / resourcepack / shader / datapack / modpack
// loader: fabric / quilt / forge / neoforge（模组/整合包用，其余传空）
// mcVersion: 版本过滤（空 = 不过滤）
// 多语言：查询词含中文时自动翻译成英文并混合搜索
// query 为空 = 像 HMCL 一样显示该分类的全部热门项目（不加版本/加载器过滤）
// offset 用于分页（每页 limit 条）
std::vector<Hit> Search(const std::string& query, const std::string& projectType,
                        const std::string& loader, const std::string& mcVersion,
                        int limit, int offset, std::string& err);

// 下载到 destDir，outFile 返回落地文件名
bool Download(const std::string& slug, Kind kind, const std::string& destDir,
              const std::string& loader, const std::string& mcVersion,
              std::string& outFile, std::string& err);

// 中文关键词 → 英文（供 Modrinth 英文检索）
std::string TranslateQuery(const std::string& q);
bool HasNonAscii(const std::string& s);

// ---------------------------------------------------------------
// CurseForge（世界 / 地图在线下载）
// ---------------------------------------------------------------
// 官方 api.curseforge.com 必须带 API Key（无 Key 返回 403）；
// 默认走公共镜像 mod.mcimirror.top（同结构、免 Key），可在设置里改。
namespace cf {

// CurseForge 类目 id（Minecraft, gameId=432）
constexpr int kGameId = 432;
constexpr int kClassWorlds = 17;
constexpr int kClassResourcePacks = 12;
constexpr int kClassShaders = 6552;

struct WorldHit {
    int modId = 0;
    int fileId = 0;
    std::string name;
    std::string summary;
    std::string fileName;
    std::string gameVersion;   // 最新文件对应的游戏版本
    std::string author;
    std::string website;
    std::string cover;         // 本地缓存后的封面路径
    std::string coverUrl;
    int64_t downloads = 0;
};

// base 留空 = 用官方地址；apiKey 留空 = 不带 x-api-key
std::vector<WorldHit> SearchWorlds(const std::string& query, int index, int pageSize,
                                   const std::string& base, const std::string& apiKey,
                                   std::string& err);

// 下载并解压到 savesDir；outName 返回生成的存档目录名
bool InstallWorld(const WorldHit& hit, const std::string& savesDir,
                  const std::string& base, const std::string& apiKey,
                  std::string& outName, std::string& err);

// 由 fileId + 文件名拼 forgecdn 直链（API 返回 downloadUrl 为 null 时用）
std::string CdnUrl(int fileId, const std::string& fileName);

} // namespace cf

// 封面：把 URL 下载到 Data\mlc\cache\covers\，返回本地路径（失败返回空）
std::string CacheCover(const std::string& url, const std::string& key);
// 封面：从 jar/zip 里提取图片到缓存，返回本地路径（失败返回空）
std::string ExtractCover(const std::string& archive, const std::string& entryName,
                         const std::string& key);

} // namespace content
