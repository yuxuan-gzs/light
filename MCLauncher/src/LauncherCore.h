// LauncherCore.h — Minecraft 启动器核心：版本清单/版本模型/参数构建/安装编排
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>

namespace mc {

// ---------------------------------------------------------------
// 下载源
// ---------------------------------------------------------------
// Auto = 优先 BMCLAPI，失败自动回落到官方（HMCL 的「自动选择」）
enum class DownloadSource { Auto, Bmclapi, Mojang };
const char* sourceName(DownloadSource s);
// 将官方 URL 重写为对应镜像（Mojang 源返回原 URL）
std::string mirrorUrl(const std::string& url, DownloadSource s);
// 各源的基础地址（用于拼 maven/资源 URL）
std::string sourceBase(DownloadSource s);        // 如 https://bmclapi2.bangbang93.com
std::string manifestUrl(DownloadSource s);       // 版本清单地址

// 下载并发数（基础值；库里用 min(24, n)，资源用 clamp(2n, 8, 48)）
void setDownloadConcurrency(int n);

// ---------------------------------------------------------------
// 版本清单
// ---------------------------------------------------------------
struct ManifestVersion { std::string id, type, url, releaseTime; };
struct VersionManifest {
    std::string latestRelease, latestSnapshot;
    std::vector<ManifestVersion> versions;
};
bool fetchManifest(DownloadSource s, VersionManifest& out, std::string& err);

// ---------------------------------------------------------------
// 版本模型
// ---------------------------------------------------------------
struct Artifact {
    std::string path;   // downloads 里的相对路径（权威路径，含 classifier）
    std::string url;
    std::string sha1;
    int64_t size = -1;
};

struct LibRule {
    std::string action;        // "allow" / "disallow"
    std::string osName;        // "windows"/"osx"/"linux"，空表示不限制
    std::string osArch;        // "x86"/"x86_64"
    std::string osVersion;     // 正则，如 "^10\\."
    // JSON 里的 features 条件（1.20.2+ 的 --quickPlay* / --demo / --width 全靠它门控）
    std::map<std::string, bool> features;
};

struct Library {
    std::string name;                          // maven 坐标（可能带 classifier 第 4 段）
    std::string group, artifactName, version, nameClassifier;
    std::vector<LibRule> rules;
    Artifact artifact;                         // 主 artifact
    std::map<std::string, Artifact> classifiers;
    std::string nativesKey;                    // natives[windows] 原始值（可能含 ${arch}）
    std::string nativeClassifier;               // 解析后的 natives classifier（空 = 不是需要解压的原生库）
    std::vector<std::string> extractExclude;

    bool appliesTo(const std::string& osName, const std::string& osArch) const;
    // maven 相对路径（name 里带 classifier 时会拼上 <artifact>-<version>-<classifier>.jar）
    std::string mavenPath() const;
    std::string classifierPath(const std::string& cls) const;
    // 真正的磁盘相对路径：downloads.artifact.path 优先（缺了才会算错路径）
    std::string filePath() const;
    // 是否需要把 classifier jar 解压到 natives 目录（老格式 natives 字段）
    bool isNative() const { return !nativeClassifier.empty(); }
    // 是否有独立主 jar（应进入 classpath / 需下载主 artifact）
    bool hasMainArtifact() const {
        if (!artifact.url.empty() || !artifact.path.empty()) return true;
        return !isNative() && classifiers.empty();
    }
};

struct Version {
    std::string id;
    std::string inheritsFrom;
    std::string mainClass;
    std::string assets;                  // 资源索引名
    std::string minecraftArguments;      // legacy 单一字符串
    std::vector<std::string> gameArgs;   // arguments.game（展开后）
    std::vector<std::string> jvmArgs;    // arguments.jvm（展开后）
    Artifact clientJar;
    std::string clientJarId;             // 拥有 client jar 的版本 id
    std::string assetIndexUrl, assetIndexSha1;
    int javaMajor = 8;
    std::vector<Library> libraries;
    // log4j 配置（1.7+）：下载后放在 assets/log_configs/<文件名>，供 ${path} 使用
    std::string loggingClientUrl, loggingClientSha1;
    std::string loggingClientPath;       // 相对 gameDir 的路径（如 assets/log_configs/client-1.12.xml）
};

bool parseMavenName(const std::string& name, std::string& g, std::string& a, std::string& v);
// 完整解析（第 4 段作为 classifier）
void parseMavenArtifact(const std::string& name, std::string& g, std::string& a,
                        std::string& v, std::string& classifier);

// 读取本地版本 JSON 并递归展开 inheritsFrom 继承。
// versionsDir 通常为 <gameDir>/versions；读取 <versionsDir>/<id>/<id>.json
bool loadVersion(const std::string& versionsDir, const std::string& id,
                 Version& out, std::string& err);

// 沿本地 JSON 的 inheritsFrom 链走到最底层，返回真正的原版版本 id
// （fabric-loader-0.16.5-1.20.1 → 1.20.1）。本地 JSON 缺失 / 解析失败返回空串。
// 装加载器时必须用它：拿加载器版本的 id 去请求 meta 一定 404。
std::string baseVersionId(const std::string& versionsDir, const std::string& id);

// ---------------------------------------------------------------
// 启动参数构建
// ---------------------------------------------------------------
struct LaunchOptions {
    std::string gameDir;      // .minecraft 根目录（libraries/assets/versions 共享）
    std::string dataDir;      // 游戏数据目录（saves/config/mods）；空 = 用 gameDir（版本隔离时指向独立目录）
    std::string javaPath;     // java.exe/javaw.exe 绝对路径
    std::string nativesDir;   // natives 解压目录（空 = <dataDir>\natives-windows，与 HMCL 一致）
    std::string authName;
    std::string authUuid;     // 带横线
    std::string authToken;
    std::string userType = "legacy";   // "msa" 或 "legacy"
    std::string versionType = "release";
    int width = 854;
    int height = 480;
    int maxMemoryMB = 2048;
    std::string server;       // 直连服务器地址（空 = 不直连）
    int serverPort = 25565;
    std::vector<std::string> extraJvmArgs;
    std::vector<std::string> extraGameArgs;
    std::function<void(const std::string&)> log;
};

// 构建完整启动命令行（不含 javaPath 本身，返回从第一个参数开始的列表）
// 返回如：{"-Xmx2048M", ..., "-cp", "<classpath>", "<mainClass>", "--username", ...}
bool buildLaunchCommand(const Version& v, const LaunchOptions& opt,
                        std::vector<std::string>& outArgs, std::string& err);

// 计算 classpath 字符串（以 ; 分隔），不含 natives
std::string buildClasspath(const Version& v, const std::string& gameDir);

// 默认 natives 目录：<dataDir>\natives-windows（HMCL 的 instanceRoot/natives-<platform> 做法）
std::string defaultNativesDir(const std::string& dataDir);

// 把版本里所有 natives jar 解压到 nativesDir（**先清空目录**，逐库应用 extract.exclude，
// 跳过 .sha1/.git）。与 HMCL 一样在「启动时」做，安装阶段不解压。
bool decompressNatives(const Version& v, const std::string& librariesDir,
                       const std::string& nativesDir, std::string& err);

// 单条参数占位符替换
std::string substituteArg(const std::string& in, const Version& v, const LaunchOptions& opt,
                          const std::string& classpath, const std::string& nativesDir);

// ---------------------------------------------------------------
// 资源索引
// ---------------------------------------------------------------
struct AssetObject { std::string hash; int64_t size = 0; };
using AssetIndex = std::map<std::string, AssetObject>;
bool parseAssetIndex(const std::string& jsonText, AssetIndex& out);
// 索引是否为虚拟资源（1.5 及更早需要生成 assets/virtual/<index>）
bool assetIndexIsVirtual(const std::string& jsonText, std::string& indexName);

// ---------------------------------------------------------------
// 安装编排（下载版本 JSON / client jar / libraries / natives / assets）
// ---------------------------------------------------------------
struct InstallProgress {
    std::string phase;      // "version"/"client"/"libraries"/"assets"/"extract"
    int64_t done = 0;
    int64_t total = 0;
    std::string current;
    bool finished = false;
    std::string error;
};

// 安装版本。versionsDir/librariesDir/assetsDir 为 .minecraft 下的子目录绝对路径。
// 返回 false 或 progress 返回 false 表示中止；错误写入 err。
bool installVersion(const std::string& versionsDir, const std::string& librariesDir,
                    const std::string& assetsDir, const VersionManifest& mf,
                    const std::string& versionId, DownloadSource src,
                    const std::function<bool(const InstallProgress&)>& progress,
                    std::string& err);

// 只补齐「已有版本 JSON」的文件（不依赖版本清单；供启动前修复用）
bool repairVersionFiles(const std::string& versionsDir, const std::string& librariesDir,
                        const std::string& assetsDir, const std::string& versionId,
                        DownloadSource src,
                        const std::function<bool(const InstallProgress&)>& progress,
                        std::string& err);

// ---------------------------------------------------------------
// 完整性检查（启动前检查）
// ---------------------------------------------------------------
struct IntegrityReport {
    bool ok = false;
    bool jsonMissing = false;
    bool clientJarMissing = false;
    int totalLibs = 0, missingLibs = 0;
    int totalAssets = 0, missingAssets = 0;
    int missingNatives = 0;
    std::string detail;                  // 人类可读摘要
    std::vector<std::string> missing;    // 前 40 条缺失项
};

// 检查版本 JSON / client jar / libraries / natives / assets 是否齐全
bool checkIntegrity(const std::string& versionsDir, const std::string& librariesDir,
                    const std::string& assetsDir, const std::string& versionId,
                    IntegrityReport& rep, std::string& err);

} // namespace mc
