// MlcData.h — Data/mlc 数据层（全局设置 / 游戏实例 / 单局设置 / 版本渠道分类）
//
// 存储布局（主程序目录 = light.exe 所在目录）：
//   <exe>\Data\mlc\
//     config\global.json               全局游戏设置
//     games\registry.json              游戏实例注册表（可多个文件夹、多个版本）
//     games\<id>\settings.json         单局游戏设置
//     cache\covers\<key>.png           内容封面缓存（模组/资源包/光影/世界/原理图/加载器）
//     cache\meta\*.json                Modrinth 元数据缓存
//     backups\<id>\*.zip               存档备份
//     logs\*.log                       任务日志
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mlc {

// ---------------------------------------------------------------
// 路径
// ---------------------------------------------------------------
std::wstring ExeDir();          // 主程序所在目录
std::wstring BaseDir();         // <Exe>\Data\mlc
std::wstring CoversDir();       // <Base>\cache\covers
std::wstring BackupsDir();      // <Base>\backups
std::wstring LogsDir();         // <Base>\logs
void EnsureDirs();              // 创建上述所有目录（不存在则创建）

// ---------------------------------------------------------------
// 版本渠道分类（下载页细分：正式版/测试版/快照版/愚人节/远古版）
// ---------------------------------------------------------------
enum class Channel { Release, PreRelease, Snapshot, AprilFools, OldBeta, OldAlpha };

// 依据 manifest 的 type/id/releaseTime 判定渠道
Channel ClassifyVersion(const std::string& id, const std::string& type,
                        const std::string& releaseTime = "");
const wchar_t* ChannelName(Channel c);
const char* ChannelKey(Channel c);

// 游戏名称校验 / 清洗：允许任何语言的字母与数字，符号只允许下划线 "_"
bool IsValidGameName(const std::wstring& name);std::wstring SanitizeGameName(const std::wstring& in);
// 从版本 id 生成默认名称（去掉文件名非法字符）
std::wstring DefaultNameFromVersion(const std::string& versionId);

// 离线玩家名规则（与 HMCL / 正版一致）：1–16 位，仅字母、数字、下划线
bool IsValidPlayerName(const std::wstring& name);

// ---------------------------------------------------------------
// 设置
// ---------------------------------------------------------------
struct Settings {                       // 全局游戏设置
    int maxMemoryMB = 4096;             // 全局内存
    std::string source = "auto";        // 下载源：auto（BMCLAPI 优先，失败回落官方）/ bmclapi / mojang
    bool isolate = false;               // 版本隔离（默认关：沿用 .minecraft 根目录的 mods/saves）
    bool checkIntegrity = true;         // 启动时检查游戏完整性
    bool autoRepair = true;             // 检查到缺失时自动修复
    bool backupSaves = false;           // 启动前自动备份存档
    int width = 854;                    // 默认窗口宽
    int height = 480;                   // 默认窗口高
    std::string javaPath;               // 全局 Java（空 = 自动检测）
    int javaMajor = 0;                  // 期望 Java 主版本（0 = 按版本 JSON 自动）
    // 微软登录用的 Azure 应用 ID（公共客户端，勾选设备码流）。
    // 留空 = 内置 Minecraft 启动器旧应用（能登录，但没有 refresh_token，约 24h 需重新登录）
    std::string msClientId;
    // 账号存储位置（已废弃：现在每个账号各自带 portable 标记，照 HMCL）。
    // 保留字段只为兼容旧配置，不再参与路径计算。
    std::string accountStorage = "portable";
    // 当前账号 key（uuid 或 name）。存在这里而不是账号文件里，
    // 这样账号在便携/全局之间迁移时不会丢掉「当前账号」。
    std::string currentAccount;
    int downloadThreads = 16;           // 下载线程基础值（资源会按 2 倍放大，上限 48）
    bool keepLauncherOpen = true;       // 启动游戏后保留启动器
    std::vector<std::pair<std::string, int>> servers;   // 服务器列表
    std::string directServer;           // 全局直连服务器（空 = 不直连）
    int directPort = 25565;
    // CurseForge（世界在线下载）
    std::string cfBase = "https://mod.mcimirror.top/curseforge/v1";  // 留空 = 官方接口
    std::string cfApiKey;               // 官方 API Key（可选，公共镜像不需要）
};

struct InstanceSettings {               // 单局游戏设置
    std::string javaPath;               // 空 = 用全局/自动
    int maxMemoryMB = 0;                // 0 = 用全局
    int width = 0;                      // 0 = 用全局默认
    int height = 0;                     // 0 = 用全局默认
    bool fullscreen = false;
    int isolate = -1;                   // -1 跟随全局, 0 关, 1 开
    int checkIntegrity = -1;            // -1 跟随全局, 0 关, 1 开
    std::string nickname;               // 该实例启动时使用的离线昵称（空 = 账号名）
    std::string server;                 // 直连服务器
    int serverPort = 25565;
    std::vector<std::string> jvmArgs;   // 额外 JVM 参数
    std::vector<std::string> gameArgs;  // 额外游戏参数
};

// 一个「游戏」= 文件夹 + 版本 id（HMCL 的实例）
struct Instance {
    std::string id;                     // 内部唯一 id
    std::string name;                   // 显示名（任意语言，符号仅 _）
    std::string folder;                 // 游戏根目录（.minecraft）
    std::string versionId;              // versions\<id>
    std::string loader;                 // "" / fabric / quilt / forge / neoforge
    std::string loaderVersion;
    std::string iconPath;               // 自定义封面（空 = 按渠道画）
    int64_t lastPlayed = 0;
    InstanceSettings settings;

    std::string versionsDir() const;
    std::string librariesDir() const;
    std::string assetsDir() const;
    bool isolateEnabled() const;        // 结合全局解析
    std::string dataDir() const;        // 隔离时 = versions\<id>，否则 = folder
    std::string modsDir() const;
    std::string savesDir() const;
    std::string resourcePacksDir() const;
    std::string shaderPacksDir() const;
    std::string schematicsDir() const;
};

// ---------------------------------------------------------------
// 账号（多账号保存 + 当前选择）
struct AccountRec {
    std::string type;          // "msa" / "offline"
    std::string name;          // 玩家名
    std::string uuid;          // 带横线 UUID
    std::string accessToken;   // Minecraft access token
    std::string refreshToken;  // 微软 refresh_token
    int64_t expiresAt = 0;     // token 过期时间（Unix 秒）
    int64_t lastUsed = 0;
    std::string skin;          // 皮肤文件名（在账号目录的 skins\ 下，空 = 无）
    std::string skinPath;      // 自定义皮肤绝对路径（优先于 skin）
    std::string cape;          // 披风文件名（skins\ 下）
    std::string capePath;      // 自定义披风绝对路径（优先于 cape）
    std::string skinModel;     // "classic"（默认）/ "slim"
    int64_t skinRemoteAt = 0;  // 上次从 Mojang 拉取皮肤的时间（0 = 没拉过）
    // 账号存哪里（照 HMCL 的 per-account portable）
    //   true  = 便携：<启动器目录>\Data\mlc\accounts（跟着启动器/整合包走）
    //   false = 全局：%APPDATA%\YXGLauncher（换启动器位置也还在）
    bool portable = true;
};

// 注册表（单例）
// ---------------------------------------------------------------
class Store {
public:
    static Store& I();

    Settings& spec();                       // 全局设置（可改）
    const Settings& spec() const;
    void SaveSettings();

    std::vector<Instance>& list();
    Instance* Find(const std::string& id);
    Instance* Current();
    std::string CurrentId() const;
    void SetCurrent(const std::string& id);

    // ---- 游戏文件夹（独立于实例：空文件夹也可以登记，作为下载目标）----
    std::vector<std::string>& folders();
    std::string CurrentFolder() const;
    void SetCurrentFolder(const std::string& folder);
    bool AddFolderPath(const std::string& folder, std::string& err);   // 允许空目录；不存在会创建
    bool RemoveFolderPath(const std::string& folder, std::string& err);
    void EnsureFolder(const std::string& folder);                      // 静默登记
    int CountInstancesInFolder(const std::string& folder);

    // 扫描 folder 下的 versions\*，把未登记过的版本加入实例表；返回新增数量
    // 注意：即使 0 个版本也会登记该文件夹（它可作为下载目标）
    int AddFolder(const std::wstring& folder, std::string& err);
    std::string AddInstance(const std::string& folder, const std::string& versionId,
                            const std::string& name);
    // 同一 folder + versionId 已存在就复用（装加载器重复点不会多出同名游戏）
    std::string EnsureInstance(const std::string& folder, const std::string& versionId);
    void RemoveInstance(const std::string& id);            // 仅从列表移除
    bool DeleteInstanceFiles(const std::string& id, std::string& err);  // 删除版本目录
    void SaveInstances();
    void SaveInstanceSettings(const Instance& inst);
    void Touch(const std::string& id);                     // 记录最近启动时间

    std::vector<std::string> VersionsInFolder(const std::string& folder);
    bool FolderRegistered(const std::string& folder);

    // ---------------------------------------------------------------
    // 实例列表「以磁盘为准」（照 HMCL：实例就是 versions\<id>，没有第二份注册表）
    // ---------------------------------------------------------------
    // 扫描所有已登记文件夹：补登记磁盘上新出现的版本，剔除版本目录已被删除的实例。
    // 手动删掉 versions\<id> 之后，这里会立刻把「已安装」和列表项一起清掉。
    bool SyncWithDisk();
    // 该实例是否应该出现在列表里。
    // HMCL 3.6 的实例模型里，加载器是「装进同一个游戏」的组件而不是新游戏：
    // 只被其它版本 inheritsFrom 引用的原版版本不单独算一个游戏（当前游戏除外，避免选中项消失）。
    bool IsVisibleInstance(const Instance& in) const;
    int HiddenParentCount() const;
    bool IsParentOnly(const std::string& folder, const std::string& versionId) const;
    // 装好加载器后：把同文件夹里的原版实例改指向加载器版本（同一个游戏，不新增实例）。
    // 返回实例 id（没有可复用的就新建一个）。
    std::string AdoptLoaderVersion(const std::string& folder, const std::string& loaderVersionId);
    // 版本 JSON 里的 inheritsFrom（空 = 不是继承版本）
    static std::string ParentOf(const std::string& folder, const std::string& versionId);
    // 版本 JSON 里的 id（读不到就用目录名）
    static std::string JsonIdOf(const std::string& folder, const std::string& versionId);
    // 默认游戏文件夹：<主程序>\.minecraft（等同 HMCL 便携模式），首次运行自动登记
    static std::string DefaultFolder();

    // ---- 账号 ----
    std::vector<AccountRec>& accounts();
    std::string AccountKey() const;                     // 当前账号 key（=uuid）
    void SetAccountKey(const std::string& key);
    AccountRec* FindAccount(const std::string& key);
    AccountRec* CurrentAccount();
    std::string UpsertAccount(const AccountRec& rec);   // 返回 key
    void RemoveAccount(const std::string& key);
    void SaveAccounts();
    void SaveFoldersFile();
    void ReloadAccounts();                              // 切换存储位置后重读

    // 账号目录（照 HMCL：便携 = 启动器目录，全局 = %APPDATA%\YXGLauncher）
    std::string PortableAccountsDir() const;            // UTF-8 绝对路径
    std::string GlobalAccountsDir() const;              // UTF-8 绝对路径
    std::wstring SkinsDirFor(bool portable) const;      // 对应一侧的 skins 目录
    std::string SkinFileOf(const AccountRec& a) const;  // 皮肤的绝对路径（空 = 无）
    // 实际生效的皮肤 / 披风路径（自定义路径优先，其次 skins\ 下的托管文件；空 = 无）
    std::wstring EffectiveSkin(const AccountRec& a) const;
    std::wstring EffectiveCape(const AccountRec& a) const;
    bool IsSlim(const AccountRec& a) const;
    // 把某个账号在「便携 / 全局」之间迁移（连同皮肤文件）；key = uuid 或 name
    bool MoveAccount(const std::string& key, bool toPortable, std::string& err);

    // 解析加载器信息（从 versions\<id>\<id>.json 的 id/inheritsFrom 推断）
    static void DetectLoader(Instance& inst);

private:
    Store();
    void Load();
    void LoadAccounts();
    void LoadInstanceSettings(Instance& inst);

    Settings m_spec;
    std::vector<Instance> m_inst;
    std::string m_current;
    std::vector<std::string> m_folders;
    std::string m_curFolder;
    std::vector<AccountRec> m_accs;
    std::string m_accKey;
    // folder → 该文件夹里「被别的版本 inheritsFrom 引用」的原版版本 id
    std::map<std::string, std::set<std::string>> m_parents;
    void RebuildParentCache();
    mutable std::mutex m_mu;
};

// ---------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------
std::wstring Utf8ToW(const std::string& s);
std::string WToUtf8(const std::wstring& s);
std::string JoinPath(const std::string& a, const std::string& b);
bool DirExists(const std::string& p);
bool FileExists(const std::string& p);
// 路径比较（统一分隔符、忽略结尾斜杠与大小写）：D:/a\.minecraft 与 D:\A\.minecraft 视为同一个
std::string NormalizePath(const std::string& p);
bool SamePath(const std::string& a, const std::string& b);
std::string ReadTextFile(const std::string& path);
bool WriteTextFile(const std::string& path, const std::string& text);
// 简单的文件日志（Data\mlc\logs\launcher.log，追加）
void Log(const std::string& line);
// 生成稳定短 id
std::string MakeId(const std::string& seed);

} // namespace mlc
