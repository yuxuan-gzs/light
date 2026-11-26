# YXG Minecraft Launcher

一款 **HMCL 风格 + PCL2 创新功能** 的《我的世界》第三方启动器，作为 **light 原 GUI 的侧边栏原生面板插件** 运行。

- **纯 C++ / GDI+ 自绘界面，零 JS**：不替换 light 主界面，而是在原 GUI 侧边栏新增一个「我的世界启动器」Tab。
- 插件类型：light 的 **YXG ABI 附属插件**（`Cache\gui\dll\*.dll`），导出 `YXG_RegisterPlugin` 并注册**原生面板**。
- 2.0 起改为 **「实例」模型**：不再有全局游戏目录 / 全局 Java 路径，改为「一个文件夹可以放多个游戏，可添加任意多个文件夹」。

## 页面与能力

| 页面 | 能力 |
|------|------|
| 首页 | 当前游戏卡片、**唯一一个启动按钮**（点开弹窗选实例 / 账号再启动）、我的游戏卡片网格、进度与状态、账号入口 |
| 版本管理 | 当前版本的管理中心，7 个标签页：模组 / 加载器 / 版本设置 / 资源包 / 光影 / 世界 / 原理图 |
| 版本选择 | **HMCL 式实例列表**：左侧游戏目录抽屉（全部 + 各文件夹，带实例数）+ 右侧实例行（图标 / 名称 / 版本 / 加载器 / 最近游玩 / 启动）。搜索、`显示原版组件`、`＋ 添加文件夹` / `移除` / `打开目录` / 检查完整性 / 删除实例。列表**以磁盘为准**（`versions\*` 扫描），手动删掉版本目录后条目会立刻消失 |
| 下载 | 5 个分类：游戏 / 模组 / 资源包 / 光影 / 世界；**内容下载有「下载到」实例选择器**，落盘位置与 HMCL 一致（`mods` / `resourcepacks` / `shaderpacks` / `saves`），界面直接显示完整目标路径；游戏细分 5 个渠道并按发布时间倒序；可改游戏名称；加载器带封面选择；世界对接 CurseForge（封面 + 一键安装） |
| 账号 | **多账号保存与切换**（`Data\\mlc\\accounts\\accounts.json`，记住「当前账号」下次启动直接用）；离线登录（精确离线 UUID）；微软正版登录（设备码，自动打开授权页，保存 refresh_token） |
| 全局设置 | 内存 / 下载源 / 版本隔离 / 完整性检查 / 自动修复 / 启动前备份存档 / 默认分辨率 / Java 自动检测 / 背景 / 服务器直连 / CurseForge 接口与 API Key |

### 版本管理（HMCL 式卡片 + 封面）

- **加载器封面**：Fabric（织布+梭）、Quilt（拼布）、Forge/NeoForge（铁砧）矢量 logo，也可放 `cache\covers\loader_<1..4>.png` 覆盖。
- **模组**：卡片网格带模组图标（从 jar 的 `fabric.mod.json` / `quilt.mod.json` / `mods.toml` / `assets/**/icon.png` 提取并缓存）；右下角开关启用/停用（`.jar` ↔ `.jar.disabled`）；检测/补充前置；跳转下载。
- **加载器**：Fabric / Quilt / Forge / NeoForge 四张带 logo 的卡片，安装 / 更新 / 卸载（各自版本下拉）；安装后会生成新版本并自动登记为游戏。
- **版本设置**（单局设置）：Java 覆盖、内存、分辨率、全屏、版本隔离、完整性检查（可单独开关 + 立即检查修复）、昵称、直连服务器、额外 JVM/游戏参数；保存到 `Data\mlc\games\<id>\settings.json`。
- **资源包**：卡片带 `pack.png` 封面，开关会改写 `options.txt` 的 `resourcePacks` 列表；支持从本地导入。
- **世界**：卡片用存档 `icon.png` 作封面，备份到 `Data\mlc\backups`，删除、打开目录、导入 zip。
- **原理图**：`.schematic` / `.litematic` / `.schem` / `.nbt` 卡片，导入 / 删除 / 打开目录。

### 下载

- **游戏**：渠道细分 **正式版 / 测试版（pre、rc）/ 快照版 / 愚人节 / 远古版（alpha、beta）**，按 `releaseTime` 从新到旧；可改游戏名称（任意语言，符号只允许 `_`）；加载器用带 logo 的小卡片选择，右侧实时显示已选版本与安装目录。
- **模组**：按 HMCL `DownloadListPage` / `instances.DownloadPage` 重做——搜索卡片（名称 / 游戏版本 / 类别 / 排序）、40px 封面两行结果列表、翻页条（第一页 / 上一页 / 页码 / 下一页 / 最后一页 / 搜索）；点结果进入项目详情，按「推荐版本 - Minecraft x」与正式版/快照分组列出版本；点版本弹出确认框（更新日志 / 官方页面 / 依赖分组 / 文件名 / 安装到当前实例 / 下载到本地文件夹）。搜索每页 50 条，排序、facets、中文项目名映射、版本/依赖解析均照 HMCL；下载到当前实例的 `mods`，校验同名冲突并按 md5 > sha1 校验，失败重试 5 次。
- **资源包 / 光影**：HMCL 式卡片网格 + Modrinth 封面缓存；**中英双语检索**（中文关键词自动翻译成英文并行搜索后合并去重）。
- **世界**：
  - `CurseForge 在线`：搜索地球上最大的世界/地图库（classId=17），卡片带封面、游戏版本角标、一键下载并自动解压进当前游戏的 `saves`；支持翻页、中英双语检索（中文关键词自动翻成英文再搜）。
  - `本地存档`：管理已安装世界（icon.png 封面、备份、删除、zip 导入）。
  - 官方 `api.curseforge.com` 必须带 API Key（无 Key 403），默认走公共镜像 `mod.mcimirror.top/curseforge/v1`（免 Key、同结构）；两处都可在「全局设置 → CurseForge」里改。下载优先用 `fileId` 拼 forgecdn 直链（部分世界的 `downloadUrl` 是 null），失败再回退 API 的 `download-url`。

## 数据目录

全部数据都在 **主程序目录**（`light.exe` 所在目录）下的 `Data\mlc`，首次运行自动创建：

```
Data\mlc\
├── config\global.json            全局游戏设置
├── games\registry.json           游戏实例注册表（文件夹 + 版本 + 显示名 + 加载器）
├── games\<实例id>\settings.json   单局游戏设置
├── cache\covers\*.png            内容封面缓存（模组/资源包/光影/世界图标/在线封面）
├── cache\meta\                    元数据缓存
├── backups\<实例id>\*.zip         存档 / 世界备份
└── logs\launcher.log             任务日志
```

## 目录结构

```
MCLauncher/
├── MCLauncher.sln / .vcxproj    标准 VS 工程（x64, /std:c++20, /MT, /utf-8）
├── include/YXExtension.h        YXG ABI（含原生面板扩展）
├── src/
│   ├── dllmain.cpp              YXG ABI 入口 + 原生面板注册
│   ├── MlcData.*                Data\mlc 数据层：全局/单局设置、实例注册表、渠道分类、名称规则
│   ├── ContentLib.*             内容库：模组/资源包/光影/世界/原理图扫描、封面提取、启停、Modrinth 双语搜索
│   ├── Ui.*                     面板窗口：窗口过程、输入、异步事件、原生 EDIT 覆盖（中文输入法）
│   ├── UiNav.h                  侧边栏导航表 + 内容区布局常量
│   ├── UiPages.cpp              各页面布局（首页/版本管理/版本选择/下载/账号/设置）
│   ├── UiPaint.cpp              绘制：侧边栏、控件、卡片网格、下拉、弹窗、提示条
│   ├── UiActions.cpp            交互：按钮/卡片/下拉分发、文件选择、异步任务
│   ├── Theme.* / Background.*   主题 + 背景壁纸
│   ├── LauncherCore.*           版本 JSON/manifest 解析、规则、参数构建、下载编排、完整性检查
│   ├── MsAuth.*                 微软 OAuth 设备码 + XBL/XSTS/Minecraft token
│   ├── JavaDetect.*             Java 自动检测
│   ├── LoaderInstall.*          Fabric/Quilt/Forge/NeoForge 安装
│   ├── ModManager.*             Fabric/Forge 依赖解析
│   ├── Http.* / Json.* / Zip.* / Crypto.*  基础设施
│   └── LauncherApp.*            启动器状态机（安装/启动/完整性/账号 + 事件回调）
└── build.ps1 / build.bat        cl.exe/link.exe 手动构建脚本
```

## 部署

1. 把 `bin\x64\Release\YXG_MinecraftLauncher.dll` 复制到 light 的 `Cache\gui\dll\` 目录（覆盖旧版本前先备份）。
2. （可选）同名 `.txt` 作为插件介绍、同名 `.png` 作为商店封面。
3. 重启 light → 侧边栏出现「我的世界启动器」，点击即打开。

## 编译

MSBuild 被环境安全策略拦截，改用 `build.ps1`（cl.exe/link.exe 手动构建）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

> zlib 目标文件已存在时会跳过重新编译（避免 `c1.dll` 偶发无法执行导致构建中断）。

## 账号（多账号 + 正版登录）

- 登录成功（离线或微软）都会**自动保存**到 `Data\mlc\accounts\accounts.json` 并设为当前账号；
- 下次启动自动载入当前账号，无需重新登录；
- **微软登录**：设备码流程（microsoft.com/link 输码，不接触密码），按钮一次走完 + 自动打开浏览器；
- **续期**：
  - 内置应用 ID（默认）：微软不允许该应用申请 `offline_access`，**拿不到 refresh_token**，
    令牌约 24 小时有效，过期后到账号页点一次「微软登录」即可；
  - 若在「全局设置 → 微软登录应用 ID」填了自己的 Azure 公共客户端 ID（并勾选设备码流），
    则会保存 `refresh_token`，启动游戏时令牌快过期会自动续期（refresh → XBL → XSTS → Minecraft）；
- 账号页可列出全部账号（离线账号显示 UUID 前缀），点一行 +「使用选中账号」切换，「删除选中」移除。

## 启动流程（与 HMCL 对齐）

1. 读 `versions/<id>/<id>.json`，递归合并 `inheritsFrom`；
2. 按 `rules` 过滤库与参数：`os.name` / `os.arch` / `os.version`（正则）+ **`features`**
   （`has_custom_resolution`=true，`is_demo_user` / `has_quick_plays_support` / `is_quick_play_*`=false。
   1.20.2+ 的 `--demo`、`--width/--height`、`--quickPlay*` 全靠这个门控，漏判会让多个
   `--quickPlay*` 同时进命令行，客户端直接抛 “Only one quick play option can be specified”）；
3. classpath = 过滤后 `downloads.artifact.path` 存在的库 + client jar（去重）；
4. natives：把 `natives` 字段指向的 classifier jar 解压到 `<实例目录>\natives-windows`（启动前清空）；
5. `arguments.jvm/game`（或旧版 `minecraftArguments`）做占位符替换：
   `${natives_directory}` `${classpath}` `${game_directory}` `${assets_root}` `${assets_index_name}`
   `${auth_player_name}` `${auth_uuid}` `${auth_access_token}` `${user_type}` `${version_type}`
   `${game_assets}`（1.5/1.6 的 virtual 目录）`${path}`（log4j 配置）等；
6. 追加 `-Xmx/-Xms`、`-Dlog4j2.formatMsgNoLookups=true`、`-Djava.library.path`（缺失时兜底）、用户自定义参数；
7. `CreateProcessW` 启动，stdout/stderr 重定向到游戏目录 `logs\yxg-launcher.log`；
8. **启动成功与否以「窗口出现」为准**：进程创建后先报「启动中…（已 N 秒）」，后台每 100ms
   `EnumWindows` 找该 PID 的可见顶层窗口（>64×64）；窗口出现才算「游戏已启动」；
   窗口出现前进程就退出 → 直接报「启动失败（退出码 X）：<从日志尾提取的 Description/异常行>」；
   180 秒无窗口但进程仍在 → 视为「运行中（可能还在加载）」。

## CurseForge 世界的下载链路

1. `GET {base}/v1/mods/search?gameId=432&classId=17&sortField=2&sortOrder=desc&index=<n>&pageSize=12[&searchFilter=<q>]`
2. 从 `latestFilesIndexes` 里挑 `filename` 以 `.zip` 结尾、`releaseType==1` 的最新文件
3. 下载：`https://mediafilez.forgecdn.net/files/<fileId/1000>/<fileId%1000>/<urlencoded 文件名>`
   （API 的 `downloadUrl` 为 null 时用这条；失败再问 `/mods/{id}/files/{fid}/download-url`）
4. 解压到 `saves/<项目名>/`，若压缩包只包一层文件夹会自动提升一级

> 封面按 4 张一批增量返回，结果先出、图后补，不会被图片下载卡住。

## 账号页（2026-11-25 重构）

- **便携 / 全局账号（照 HMCL：每个账号各自一份）**：
  - 账号记录里带 `portable` 标记。便携账号存在 `<主程序>\Data\mlc\accounts`（跟着启动器/整合包走），
    全局账号存在 `%APPDATA%\YXGLauncher\accounts`（换启动器位置也还在）；
  - 账号列表每行的副标题会标出「便携 / 全局」；选中账号后点列表下方的
    「移到全局 / 移到便携」即可**只迁移这一个账号**，连同它的皮肤与披风一起搬过去（不是复制，原侧会删掉）；
  - 两个账号文件合并读取、按 key 去重（同 key 便携优先），按 portable 分文件写回；
  - 当前账号 key 存在 `config\global.json` 的 `currentAccount`，所以账号迁移不会丢掉「当前账号」；
  - 兼容旧版：旧数据没有 `portable` 字段，从便携文件读到的默认便携、全局文件的默认全局；
    旧版切换时用的是「复制」，若皮肤只在一侧，读取时会跨侧兜底找到；
    `%APPDATA%\YXGLauncher\accounts` 这个老路径保持不变，升级后原全局账号不会消失。
- **离线模式**：只有一个按钮，点开弹窗填名字创建（1–16 位字母/数字/下划线，
  `UUID = MD5("OfflinePlayer:"+名字)`）。
- **微软登录**：只剩一个按钮；设备码弹浏览器后自动轮询，窗口里显示代码，不再需要额外按钮。
- **皮肤**（操作方式对齐 HMCL：选中账号 → 右侧预览 + 一排操作按钮）：
  - 列表里选中账号 → 右侧显示该账号的皮肤预览（**拖动旋转、滚轮缩放**，程序化 3D 小人，
    6 部件 36 面 + 可选的披风薄片）；
  - **选择皮肤**：读 64×64 / 64×32 PNG（高清整数倍也行），复制到 `<账号目录>\skins\<key>.png`（托管，换皮肤覆盖）；
  - **皮肤路径**：直接引用任意位置的 PNG，不复制（原文件被移动/删除会失效）；
  - **经典 / 纤细**：切换 4px / 3px 手臂模型（slim = Alex），保存为 `skinModel`；
  - **披风…**：引用任意位置的 64×32 PNG，预览里画成背后薄片（双面）；
  - **上传正版**：仅微软账号。`POST https://api.minecraftservices.com/minecraft/profile/skins`，
    `Authorization: Bearer <MC token>`，multipart（`variant=classic|slim` + `file=skin.png`）；
    令牌过期且是当前账号时会先自动续期再上传；
  - **正版同步**：仅微软账号。`GET https://sessionserver.mojang.com/session/minecraft/profile/<uuid32>`
    → `properties[].value` 做 base64 → `textures.SKIN.url` / `textures.CAPE.url` → 下载 PNG 到 `skins\`。
    选中正版账号且本地没皮肤时会**自动拉一次**（每个账号每次运行只自动试一次，也可手动点）。
  - 账号列表与左侧边栏的头像都直接取自皮肤的头像区域（含帽子层）。
  - 说明：**离线账号的皮肤只在本机显示**（头像/预览）；游戏内显示需要 authlib-injector 皮肤站或
    CustomSkinLoader 之类的模组。正版账号点「上传正版」后游戏内即时生效。
- 已移除：「打开账号文件」「打开授权页面」「复制代码」「续期当前正版账号」及对应标题文字。

## 加载器安装（2026-11-25 重写为 HMCL 式）

原实现是「下载官方 installer → `java -jar installer --installClient`」，这条路在国内基本不可用：
installer 会自己直连 Mojang / Forge 下载几百 MB，失败时只给一个退出码。现在按 HMCL 的做法重写：

1. 只把 installer 当**压缩包**用，**不运行它的 main**；
2. 读 `install_profile.json`：
   - **新格式**（含 `spec`，1.13+）：先把 installer 内 `maven/` 里自带的库解出来，其余库按
     `downloads.artifact` 从官方 + BMCLAPI 镜像下载 → 读 installer 内 `version.json` 写成版本 JSON →
     按 `processors` 逐个 `java -cp <classpath> <Main-Class> <args>`；
     `sides` 不含 `client` 的步骤跳过；产物已存在就跳过（重复安装很快）；
   - **旧格式**（含 `install` + `versionInfo`，≤1.12.2）：从 installer 解压 `install.filePath`
     的 universal jar 到 `libraries/`，把 `versionInfo` 直接写成版本 JSON —— **完全不需要 Java**；
3. `DOWNLOAD_MOJMAPS` 这个纯下载步骤改成从版本 JSON 的 `downloads.client_mappings` 走 BMCLAPI 镜像
   （官方 installer 有 8 分钟里大半耗在这）；
4. Java 按 MC 版本挑：`≤1.16.5 → 8`、`1.17~1.20.4 → 17`、`1.20.5+ → 21`，有精确版本优先用精确版本
   （21 不用 25），缺失时明确报错而不是硬用高版本跑崩；
5. 安装步骤的输出写到 `<游戏目录>\logs\loader-install.log`，失败时把 `Exception / Caused by / Error`
   的行带进错误提示。

版本 id 一律从 installer 的 `version.json` 取（如 `1.21.11-forge-61.2.5`），安装后自动登记并切换为当前游戏；
重复安装同一个加载器版本不会多出第二个「游戏」（`EnsureInstance` 复用）。

`RefreshLoaderVersions` / 安装按钮取 MC 版本时一律先沿 `inheritsFrom` 回溯到原版 id
（`mc::baseVersionId`）——实例本身是 `fabric-loader-0.16.5-1.20.1` 时，直接拿这个 id 去请求 meta 必然 404。

## 下载页内容搜索（2026-11-25）

- **没输入关键词时不再空白**：像 HMCL 一样自动列出该分类的**全部热门项目**，每页 20 条，
  提供「← 上一页 / 下一页 →」；进入下载页或切换分类（模组/资源包/光影）时自动加载第一页。
  空查询只按 `project_type` 过滤、按下载量排（不再套当前实例的 MC 版本/加载器，
  否则 1.21.11 这类新版本会一页都没有）。
- **中文查得到**：新增完整模组名短语词典并优先匹配（「机械动力」→ `create`，
  不再是拆词成 `mechanical`）；「等价交换 → ee」「暮色森林 → twilight forest」等同理。
- **过滤兜底**：带 `versions:1.21.11` + `categories:fabric` 过滤后结果少于 5 条时，
  再发一次只按类型的请求补结果（分页时不补，避免重复）。

## 模组 / 资源包 / 光影 / 世界（2026-11-25 按 HMCL 数据模型重写）

- **世界**：像 HMCL 的 `World` 类一样解析 `level.dat`（gzip NBT），卡片显示真实世界名、
  MC 版本、最后游玩日期，角标显示版本；带 Forge 的 `fml` 数据跳读错位时有按 tag 头定位的兜底。
  兼容 `special_level.dat`（20w14infinite）。
- **资源包**：启用状态和优先级都从 `options.txt` 的 `resourcePacks` 列表读（不再自己造 `.disabled`），
  列表按该顺序排（越靠上优先级越高），停用的排最后；新增「↑ 上移 / ↓ 下移」按钮调整顺序
  （`content::Move` 改列表顺序并让 `vanilla` 保持在末尾）；解析 `pack.mcmeta` 的 `pack_format` 与
  `description`（支持 `{"text":...}` 形式和 § 颜色符）。
- **模组**：元数据解析补齐 Fabric (`fabric.mod.json`) / Quilt (`quilt.mod.json`) /
  Forge & NeoForge (`META-INF/mods.toml`) / 老版 (`mcmod.info`)，显示真实名称、简介、版本、运行环境；
  启用/禁用仍是 `.disabled` 后缀（与 HMCL 一致）。
- **光影**：新增独立「光影」标签（原来只有数据表没有页面），像 HMCL 一样只做浏览 / 导入 / 打开目录，
  不由启动器启停（游戏内 `options.txt` 的 `shaderPack` 决定）；解析 `pack.mcmeta` 显示格式版本和描述。

## 该 MC 版本需要多少 Java

| MC | Java |
|---|---|
| ≤ 1.16.5 | 8 |
| 1.17 ~ 1.20.4 | 17 |
| 1.20.5 / 1.21+ | 21 |

## 缺少 Java 时自动下载（2026-11-25）

启动前如果找不到满足版本要求的 Java（按 `javaVersion.majorVersion` 判断），会弹窗询问；
同意后用 Mojang 官方 `java-runtime` 清单下载对应版本（jre-legacy → 8，gamma/beta → 17，delta → 21，epsilon → 25），
装到 `Data\mlc\java\<major>`，文件走 BMCLAPI 镜像 + sha1 校验，进度显示在首页状态栏。
下次启动会优先复用这份运行时（`resolveJava` 第 2 顺位）。

## 本轮修复（2026-11-26：实例模型 / 下载落盘 / 启动弹窗，全面对齐 HMCL）

### 实例模型：以磁盘为准，加载器不再多出一个「游戏」

参考 `HMCL/src/main/java/org/jackhuang/hmcl/game/` 的做法（`GameInstance` / `GameInstancePatch` /
`getDisplayInstances()`，加载器是**装进同一个实例的组件**）：

- **`Store::SyncWithDisk()`**：每次 `Rebuild()` 前先扫描所有已登记文件夹的 `versions\*`，
  补登记新版本、**剔除版本目录已被删除的实例**。所以「安装后手动删掉版本目录」不会再显示成已安装，
  列表里也不会留下找不到的项；磁盘「已安装」角标本来就是按 `<folder>\versions\<id>\<id>.json` 判断的。
  - 文件夹整个不见了（移动硬盘没插）时跳过对账，避免误删记录；
  - 同文件夹 + 同版本的多条旧记录会**自动合并**（旧版把 `D:/x` 和 `D:\x` 当成两个目录）；
  - 当前实例指向被删掉的版本时，自动回落到列表第一个。
- **父版本只算「组件」**：被别的版本 `inheritsFrom` 引用、自己又不再被单独引用的原版版本
  （`1.20.1` 之于 `fabric-loader-0.20.1-1.21.11`）不单独列成一个游戏，
  当前实例如果恰好是这种组件版本，会自动切到继承它的加载器版本。
  版本选择页有 `显示原版组件` 开关可以临时看到它们。
- **`Store::AdoptLoaderVersion()`**：安装 / 升级加载器后，把**同一个实例**的 `versionId` 换到加载器版本，
  保留用户起的名字、单局设置和最近游玩时间；旧版「原版 + 加载器」两条记录会被合并成一条。
  卸载加载器（`App::removeLoaderVersion`）则把它退回 `inheritsFrom` 的原版版本，游戏本身不会消失。
- 路径比较统一走 `mlc::SamePath()`（分隔符 / 结尾斜杠 / 大小写不敏感），避免同一目录被判成两个。
- 首次运行默认游戏目录改为 **`<主程序>\.minecraft`**（等同 HMCL 便携模式），不再落到 `Data\mlc\games\default`。

### 下载落盘（照 HMCL 的 runDirectory 模型）

- 下载页新增 **「下载到」实例选择器**（模组 / 资源包 / 光影 / 世界共用），旁边直接显示解析后的完整路径。
- 落盘目录和 HMCL 完全一致：`mods` / `resourcepacks` / `shaderpacks` / `saves`，
  且随「版本隔离」开关在 `<游戏目录>` 与 `versions\<id>` 之间切换。
- 模组 / 资源包 / 光影请求 Modrinth 时的 `game_versions` 过滤改用**原版版本号**
  （`mc::baseVersionId` 回溯 `inheritsFrom`）。之前直接拿 `fabric-loader-0.20.1-1.21.11` 去查，
  结果一条都匹配不到——这也是「模组下载装不上」的主要成因。
- 模组下载确认框里写明真实保存位置（`保存到 <实例>\mods`），世界仍然解压进 `saves\<世界名>`。

### 启动入口：只留一个按钮 + 弹窗

- 首页去掉「版本设置 / 版本选择」两个小按钮，只保留一个大的「▶ 启动游戏」。
- 点它弹出**启动弹窗**：实例列表（选中即切换当前游戏）+ 账号下拉（可直接切账号）+ 启动 / 取消；
  游戏运行中按钮变成「■ 结束游戏」。

### 版本选择 = HMCL 实例列表

- 左侧「游戏目录」抽屉：`全部实例` + 每个文件夹（显示实例数），点击筛选。
- 右侧：搜索框（实例名 / 版本号）、`显示原版组件` 开关、工具栏（启动 / 检查完整性 / 打开目录 / 删除实例）、
  实例行（自定义图标或加载器 logo、名称、`版本 … · 加载器 … · 最近游玩 …`、行尾启动按钮）。

### 皮肤预览不再模糊

- 皮肤是 8×8 的像素画，头像（侧边栏 / 账号列表）与 3D 小人的贴图改用
  `InterpolationModeNearestNeighbor`，放大后是硬边像素，和 HMCL 一致（之前用默认双线性插值，40px 头像糊成一团）。

## 本轮修复（2026-11-24 第二轮：登录 / natives / 图标）

### 正版登录（原来完全用不了，已实测确认原因）
- 旧代码用 `login.microsoftonline.com/consumers/oauth2/v2.0/devicecode` + 内置 ID `00000000402b5328`，
  微软直接返回 **AADSTS700016（Application ... was not found）** —— 那个端点只认 Azure AD 注册的应用，
  内置 ID 是旧版 MSA 应用，永远拿不到设备码。已实测复现。
- 改为 MSA 自己的设备码端点（实测可用）：
  - 设备码：`POST https://login.live.com/oauth20_connect.srf`
    （`client_id=00000000402b5328&scope=service::user.auth.xboxlive.com::MBI_SSL&response_type=device_code`）
  - 轮询：`POST https://login.live.com/oauth20_token.srf`（`grant_type=urn:ietf:params:oauth:grant-type:device_code`）
  - 返回 `authorization_pending` / `user_code` / `verification_uri=https://www.microsoft.com/link`，均已验证。
- 登录流程改成 HMCL 那样「一个按钮走完」：点「微软登录」→ 后台取设备码 → **自动打开浏览器**并显示代码 →
  后台每 5 秒轮询，成功/失败都有明确提示（含 XErr 中文解释：没有 Xbox 档案、未成年家庭组、地区不支持、
  封禁、未购买 Java 版等）。账号页还有「复制代码 / 打开授权页面」。
- 限制说明：该应用不能叠加 `wl.offline_access`（微软报 invalid_scope），所以**没有 refresh_token**，
  令牌约 24 小时，过期需重新登录。设置页新增「微软登录应用 ID（可选）」：填自己的 Azure 公共客户端 ID
  就走 consumers v2.0 + offline_access，可自动续期。
- 离线账号规则对齐 HMCL：名字 1–16 位字母/数字/下划线；`UUID = MD5("OfflinePlayer:"+名字)`，
  账号列表里直接显示 UUID 前缀便于核对。

### natives（.dll）位置与解压 —— 对齐 HMCL
- HMCL：natives 目录 = `instanceRoot/natives-<platform>`，**每次启动**先清空再解压，
  逐库应用 `extract.exclude`，跳过 `.sha1`/`.git`，且 `-Djava.library.path` 指向它。
- 本项目原来解压到 `versions/<id>/natives` 且只在安装时解压一次（旧 dll 会残留）。
  现在改为：`<实例目录>/natives-windows`（版本隔离时在 `versions/<id>/natives-windows`），
  启动前清空重解压，应用 `extract.exclude`，natives jar 缺失会被完整性检查单独统计。
- 1.19+ 的 natives 是普通 classpath 库（`downloads.artifact` + `natives-windows` 类名），不解压、直接进 classpath；
  只有老格式（`natives` 字段 + classifiers）才解压。

### 加载器图标 —— 直接用 HMCL 的图
- HMCL 用的是 `assets/img/{fabric,quilt,forge,neoforge,legacyfabric}[@2x].png`。
- 现在把 HMCL 的 4 张 `@2x.png`（fabric/quilt/forge/neoforge）**内嵌进 DLL**（`src/LoaderIcons.cpp`，
  由 `tools/gen_loader_icons.py` 生成），GDI+ 解码后等比居中绘制；矢量绘制保留为解码失败时的兜底；
  `cache\covers\loader_<n>.png` 仍可覆盖。

### 版本 JSON 的用法（与 HMCL 相同）
启动全程只读 Mojang 标准 JSON，没有任何私有格式：
`version_manifest_v2.json` → `versions/<id>/<id>.json` → 递归合并 `inheritsFrom` →
`rules`（os/arch/version）过滤 → `arguments.jvm/game` 或 `minecraftArguments` 占位符替换 →
`libraries[].downloads.artifact.path` / `classifiers` → `assetIndex`（含 `virtual`/`map_to_resources`）→
`logging.client.file` → `javaVersion.majorVersion`。因此把 HMCL 的 `.minecraft` 直接指过来即可互通。

### JSON 与 XML 的分工（加载器版本列表）
HMCL 现在几乎全用 JSON，XML 只剩 log4j2 配置这一个用处；本项目已按同样思路调整：

| 来源 | 接口 | 格式 |
|------|------|------|
| 原版版本清单 | `launchermeta/bmclapi .../version_manifest_v2.json` | JSON |
| Fabric / Quilt | `meta.fabricmc.net/v2/versions/loader/<mc>` | JSON |
| **Forge（新）** | `bmclapi2.bangbang93.com/forge/minecraft/<mc>`（带 `build/version/branch`） | **JSON** |
| Forge（备用） | `files.minecraftforge.net/.../promotions_slim.json`（latest/recommended） | JSON |
| Forge（最后兜底） | `maven-metadata.xml` | XML |
| **NeoForge（新）** | `maven.neoforged.net/api/maven/versions/releases/net/neoforged/{neoforge,forge}` | **JSON** |
| NeoForge（兜底） | BMCLAPI `maven-metadata.xml` | XML |
| 日志配置 | Mojang `logging.client.file`（log4j2 XML） | XML |

- Forge 的 `1.7.10-10.13.4.1614-1.7.10` 这种带 branch 的坐标，现在按 `mcversion-version[-branch]` 正确拼装；
- NeoForge 1.20.1 走旧 group `net/neoforged/forge`（artifact 名也是 `forge`），1.20.2+ 走 `neoforge`；
- 版本列表按 build/版本号倒序，"最新"就是列表第一项。

## 本轮修复（2026-11-24 第一轮：下载 / 启动 / 版本列表）

### 下载逻辑
- **库路径严重错误（根因）**：旧代码只用 maven 坐标拼路径，忽略 `downloads.artifact.path`。
  `org.lwjgl:lwjgl:3.3.3:natives-windows` 这类带 classifier 的库会被解成 `lwjgl-3.3.3.jar`，
  于是 **natives jar 被写进主 jar 的位置，把主 jar 覆盖掉**；1.19+ 的 natives 库也因此永远进不了
  classpath。现在一律以 `artifact.path` 为准，maven 坐标只做兜底。
- **natives 的 `${arch}` 没替换**：`natives-windows-${arch}` 会被当成字面量去下载 → 404 → 整个安装失败。
  现在替换成 64/32，并支持「去掉 arch 后缀」「找 native* 键」两级回退。
- 区分两种 natives：老格式（`natives` 字段）解压到 `versions/<id>/natives`；1.19+ 的 classifier 库
  是普通库，必须进 classpath（否则 `UnsatisfiedLinkError`）。
- client jar / 资源索引 / 库文件全部走 **sha1 校验**，不匹配就删掉重下；不再「文件存在即视为完好」。
- 下载失败不再整批中止：单个文件失败会继续下完其余文件，最后汇总报错（原来一个 404 就全废）。
- 补 **log4j 配置**（1.7+，`logging.client.file`）与参数里的 `${path}` 替换。
- 补 **1.5/1.6 的虚拟资源**：资源索引带 `virtual`/`map_to_resources` 时生成 `assets/virtual/<index>`，
  `${game_assets}` 指向它。
- natives 解压前**清空目录**，避免旧版本残留的 dll 混用。
- 「MCBBS 镜像」站点已下线，下载源改为 **BMCLAPI / Mojang 官方**（旧配置里的 `mcbbs` 自动回落 BMCLAPI）。

### 启动逻辑
- classpath 去重、按 `artifact.path` 取文件、natives 库不再误入 classpath。
- 缺文件 **不再拦死启动**（旧逻辑完整性检查一旦有缺失就直接拒绝启动）；改为提示 + 自动修复后继续。
- 修复时不再依赖版本清单（`repairVersionFiles`），加载器版本也能就地修。
- Java 选择以版本 JSON 的 `javaVersion.majorVersion` 为准（1.17+→17、1.20.5+→21），指定的 Java 失效时自动回退检测。
- 补 `-Dlog4j2.formatMsgNoLookups=true`（log4j RCE 缓解）。
- 空参数写成 `""`：否则 `--clientId ""` 会把后面的 `--xuid` 当成值，参数整体错位。
- `@argfile` 只在 Java 9+ 使用（Java 8 不支持，用了会把文件当普通参数）。
- 超时/退出码提示更明确，异常退出时提示去游戏目录下的 `logs\yxg-launcher.log` 看原因。

### 界面
- **版本列表改为平铺**（HMCL 式整行）：新增 `Widget::Rows` 控件 —— 左侧图标、版本号、发布日期/渠道、
  行尾角标（已安装）、行尾按钮；「下载 → 游戏」与「版本选择」都用它，宽窗口不再只挤 3 列卡片。
- 列表滚动位置在页面重建后保留（之前点一下列表就跳回顶部）。
- **加载器图标重绘**：Fabric（线轴）/ Quilt（拼布）/ Forge / NeoForge（铁砧）改为居中正方形 logo，
  不再把整张封面涂成色块；仍有 `cache\covers\loader_<n>.png` 覆盖机制。
- 加载器页「已安装」判断大小写 bug 修复（`inst->loader` 是小写，之前永远匹配不上）。
- 加载器卡片右下角直接「安装 / 卸载」；未取版本列表时点安装会自动拉列表。
- 下载后默认名称不再被清洗成 `1_20_1`（未改名时保持 `1.20.1`）。

## 上轮修复的已知 Bug

- **ZIP 解析错位**：中央目录 / 本地头的「修改时间+日期」是各 2 字节，旧代码按 4+4 读取，导致 `ForEach/ReadEntry/ExtractAll` 全部错位 —— 模组元数据、模组封面、世界 zip 导入完全失效。已重写 `Zip.cpp`（统一解析、ZIP64 回退、数据描述符兜底、分隔符归一、路径穿越防护）。
- 首页版本下拉不继承当前选择，点「启动」必提示「请先选择版本」。
- 版本隔离目录指向 `versions\<id>\data`，导致隔离后 mods/存档路径不符合 HMCL 习惯（现为 `versions\<id>`）。
- 模组/存档页永远读全局 `mods`/`saves`，忽略实例与隔离设置。
- 后台线程直接改写 UI 正在读的 vector（搜索结果、加载器版本）→ 改为事件携带 JSON，在 UI 线程写入。
- 迁移旧 `%APPDATA%\YXGLauncher\config.json` 的 `versionSettings` 会在内存里残留、退出即丢。
- `Ui` 析构不销毁窗口，插件重载后残留看不见的子窗口。
- 文字输入无输入法支持（中文名无法输入）→ 改用隐藏原生 `EDIT` 覆盖层。
- 按钮文字固定白色，浅色主题下看不清 → 按底色亮度自动选黑/白。
- 侧边栏 emoji 在缺少 emoji 字体时显示成方块 → 改为圆点图标 + 账号名首字。

## 说明

- **HMCL 兼容**：遵循标准 Mojang version JSON + `.minecraft` 目录（versions/libraries/assets），可与 HMCL 已下载内容互通。
- **PCL2 闭源**，其「背景设置 / Mod 前置补充」为按功能描述自行实现的等价能力；**HMCL 开源**（Java/GPL）可作参考。
- 覆盖 1.6+ 版本（`downloads` 元数据格式）；微软登录、真实下载/启动、Modrinth 下载需联网联调。
