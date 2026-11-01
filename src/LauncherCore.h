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
enum class DownloadSource { Mojang, Bmclapi, Mcbbs };
const char* sourceName(DownloadSource s);
// 将官方 URL 重写为对应镜像（Mojang 源返回原 URL）
std::string mirrorUrl(const std::string& url, DownloadSource s);
// 各源的基础地址（用于拼 maven/资源 URL）
std::string sourceBase(DownloadSource s);        // 如 https://bmclapi2.bangbang93.com
std::string manifestUrl(DownloadSource s);       // 版本清单地址

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
    std::string path;   // 可选：显式相对路径（downloads 里通常没有，maven 才有）
    std::string url;
    std::string sha1;
    int64_t size = -1;
};

struct LibRule {
    std::string action;        // "allow" / "disallow"
    std::string osName;        // "windows"/"osx"/"linux"，空表示不限制
    std::string osArch;        // "x86"/"x86_64"
    std::string osVersion;     // 正则，如 "^10\\."
};

struct Library {
    std::string name;
    std::string group, artifactName, version;   // 由 name 解析（maven 坐标）
    std::vector<LibRule> rules;
    Artifact artifact;                          // 主 jar
    std::map<std::string, Artifact> classifiers;
    std::string nativesKey;                 // natives[os] 对应的 classifier 名
    std::vector<std::string> extractExclude;

    bool appliesTo(const std::string& osName, const std::string& osArch) const;
    std::string mavenPath() const;                                  // group/artifact/version/artifact-version.jar
    std::string classifierPath(const std::string& cls) const;       // .../artifact-version-<cls>.jar
    // 是否有主 jar（应进入 classpath / 需下载主 artifact）。
    // 现代库：downloads.artifact 存在即为真；纯 natives 库（有 natives/classifiers 而无 artifact）为假；
    // 旧版库（无 downloads 字段）：无 natives 且无 classifiers 时按主 jar 处理。
    bool hasMainArtifact() const {
        return !artifact.url.empty() || (nativesKey.empty() && classifiers.empty());
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
    std::string loggingClientUrl, loggingClientSha1;
};

bool parseMavenName(const std::string& name, std::string& g, std::string& a, std::string& v);

// 读取本地版本 JSON 并递归展开 inheritsFrom 继承。
// versionsDir 通常为 <gameDir>/versions；读取 <versionsDir>/<id>/<id>.json
bool loadVersion(const std::string& versionsDir, const std::string& id,
                 Version& out, std::string& err);

// ---------------------------------------------------------------
// 启动参数构建
// ---------------------------------------------------------------
struct LaunchOptions {
    std::string gameDir;      // .minecraft 根目录（libraries/assets/versions 共享）
    std::string dataDir;      // 游戏数据目录（saves/config/mods）；空 = 用 gameDir（版本隔离时指向独立目录）
    std::string javaPath;     // java.exe/javaw.exe 绝对路径
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

// 单条参数占位符替换
std::string substituteArg(const std::string& in, const Version& v, const LaunchOptions& opt,
                          const std::string& classpath, const std::string& nativesDir);

// ---------------------------------------------------------------
// 资源索引
// ---------------------------------------------------------------
struct AssetObject { std::string hash; int64_t size = 0; };
using AssetIndex = std::map<std::string, AssetObject>;
bool parseAssetIndex(const std::string& jsonText, AssetIndex& out);

// ---------------------------------------------------------------
// 安装编排（下载版本 JSON / client jar / libraries / natives / assets）
// ---------------------------------------------------------------
struct InstallProgress {
    std::string phase;      // "version"/"libraries"/"assets"
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

} // namespace mc
