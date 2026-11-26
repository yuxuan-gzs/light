# YXG Light 项目工作区总览

![](icon/icon.png)

> 基于 `D:\YX` 目录下全部工作区真实代码整理的工程文档，供 GitHub 阅读。
> 主项目 **light** 为核心，周边 15 个工作区构成完整生态。

| 项目 | 语言 | 角色 |
| --- | --- | --- |
| **light** | C++17 / C / JavaScript | 主程序：命令行 + GUI 双形态安全工具箱与插件宿主 |
| MCLauncher | C++ / JavaScript | 我的世界启动器（YXG GUI 插件） |
| VCBlocks | C++ | 可视化积木编程（TCC/MSVC 双编译链） |
| JavaCppBridge | C++17 | Java ↔ C++ 双向源码转换器 |
| ModMigrator | C++ | 模组代码迁移工具（JavaParser 桥接） |
| ToMCreatorForge | C++ | MCreator → Forge 源码转换 |
| BinaryHexEditor | C++ | 二进制编辑器（深色主题） |
| HexForge | C++ | 十六进制编辑器（撤销栈） |
| Registry | C++ | 注册表管理器（Xayx 加密） |
| Minecraft_C_IP_B | C++ | Minecraft 服务器 IP 嗅探（WinPcap） |
| cs | C++ | Bing 搜索下载链接工具 |
| bt | C++ | 安装/文件关联引导程序 |
| ConsoleApplication1 | C++ | Java 源码批量替换器 |
| yuxuangzs-Service-light | C++ | LightService Windows 系统服务 |
| E-C | （空） | 仅构建残留，无源码 |

---

## 目录

- [仓库布局](#仓库布局)
- [light 主项目](#light-主项目)
  - [进程形态与运行模式](#进程形态与运行模式)
  - [main 启动序列详解](#main-启动序列详解)
  - [主程序 lightcpp](#主程序-lightcpp)
  - [命令分发系统](#命令分发系统)
  - [文件加密与 XZYX 压缩体系](#文件加密与-xyx-压缩体系)
  - [系统服务与命名管道](#系统服务与命名管道)
  - [文件夹锁定](#文件夹锁定)
  - [网络拦截子系统](#网络拦截子系统)
  - [NetworkInterceptor 深度解析](#networkinterceptor-深度解析)
  - [插件系统](#插件系统)
  - [GUI 插件体系](#gui-插件体系)
  - [YXGuiPlugin 宿主插件全景](#yxguiplugin-宿主插件全景)
  - [JSPlugin 运行时全景](#jsplugin-运行时全景)
  - [通知窗口](#通知窗口)
  - [插件模板生成](#插件模板生成)
  - [嵌入引擎与第三方依赖](#嵌入引擎与第三方依赖)
  - [测试工程](#测试工程)
  - [构建与配置](#构建与配置)
- [周边工作区](#周边工作区)
  - [MCLauncher](#mclauncher)
  - [VCBlocks](#vcblocks)
  - [JavaCppBridge](#javacppbridge)
  - [ModMigrator](#modmigrator)
  - [ToMCreatorForge](#tomcreatorforge)
  - [BinaryHexEditor](#binaryhexeditor)
  - [HexForge](#hexforge)
  - [Registry](#registry)
  - [Minecraft_C_IP_B](#minecraft_c_ip_b)
  - [cs](#cs)
  - [bt](#bt)
  - [ConsoleApplication1](#consoleapplication1)
  - [yuxuangzs-Service-light](#yuxuangzs-service-light)
- [使用示例](#使用示例)
- [编码规范与工程约定](#编码规范与工程约定)
- [工程依赖关系图](#工程依赖关系图)
- [构建与验证快速指南](#构建与验证快速指南)
- [自动化脚本](#自动化脚本)
- [已知边界与限制](#已知边界与限制)
- [文档与参考](#文档与参考)
- [贡献与扩展指引](#贡献与扩展指引)
- [致谢与生态总结](#致谢与生态总结)
- [许可证](#许可证)

---

## 仓库布局

`D:\YX` 是一个多工作区聚合仓库，根目录直接展开各独立项目，彼此通过统一的插件 ABI（`YXExtension.h`）、`xayx/xbyx/xcyx` 加密文件族与 `Cache\gui\dll` 插件商店机制相互关联。典型的协作方式是：light 作为宿主，其余项目作为插件 DLL、命令行工具或系统服务被加载、调用或托管。

```text
D:\YX\
├── light\                    # 主项目（本仓库核心）
│   ├── light\                # 主程序工程（目标产物 light.exe）
│   │   ├── light.cpp         # 入口与命令分发（7490 行）
│   │   ├── PluginManager.cpp # 插件加载与生命周期（1265 行）
│   │   ├── NetworkInterceptor.cpp # WinDivert 网络拦截（2202 行）
│   │   ├── NotifyWindow.cpp  # 拦截通知气泡（392 行）
│   │   ├── PluginTemplateGenerator.cpp # 插件模板生成（895 行）
│   │   ├── LightApi.cpp      # C 语言导出 API（218 行）
│   │   ├── Gui\              # GUI 插件体系（WebView2 / QuickJS，约 1.9 万行）
│   │   ├── YXGuiPlugin.h     # GUI 宿主整合头
│   │   ├── LightApi.h        # 插件 API 声明
│   │   ├── LightApi.def      # DLL 导出符号定义
│   │   ├── cmd.ico           # 程序图标资源
│   │   ├── BUILTIN_GUI_INTEGRATION.md   # 内置 GUI 集成说明
│   │   ├── COMPILE_FIX_V4.md            # 编译修复记录（第 4 版）
│   │   ├── QUICKJS_MSVC_FIX.md          # QuickJS 在 MSVC 下的修复记录
│   │   ├── SetGuiMode.ps1    # 控制台/GUI 模式切换脚本
│   │   ├── build_one.ps1     # 一键构建脚本
│   │   ├── YXGuiToggle.h / YXGuiToggleLoader.h  # GUI 运行时切换句柄
│   │   ├── _link_objs.txt    # 链接对象清单
│   │   └── LzmaLib.lib / WebView2LoaderStatic.lib  # 预编译依赖
│   ├── lzma\C\               # LZMA SDK（嵌入式压缩）
│   ├── tests\NetworkInterceptorTests\ # 拦截器单元测试
│   └── 新建文件夹\            # WFP 内核态驱动原型（SecurityCore / WfpCallout）
├── MCLauncher\               # 我的世界启动器（GUI 插件，约 30 个源文件）
├── VCBlocks\                 # 积木编程（GUI 插件，16+ 核心模块）
├── JavaCppBridge\            # C++17 语言转换器（LangBridge）
├── ModMigrator\              # 模组迁移工具（GUI + JavaParser 桥）
├── ToMCreatorForge\          # MCreator 转 Forge（480 行主文件 + 5 子模块）
├── BinaryHexEditor\          # 二进制编辑器（3034 行修复版）
├── HexForge\                 # 十六进制编辑器（1219 行）
├── Registry\                 # 注册表管理器（约 1510 行）
├── Minecraft_C_IP_B\         # MC 服务器 IP 嗅探（763 行）
├── cs\                       # Bing 搜索工具（661 行）
├── bt\                       # 安装引导程序（249 行）
├── ConsoleApplication1\      # Java 源码替换器（310 行）
├── yuxuangzs-Service-light\  # Windows 系统服务（125 行）
├── log\icon.png
├── .gitignore
├── LICENSE                   # The Unlicense（公有领域）
├── bt.bat                    # 自动提交推送脚本
├── restart-dsh.cmd / .ps1    # DSH 桌面重启脚本（CMD 与 PowerShell 双版本）
└── light.slnx                # 解决方案（x64/x86 双平台）
```

根目录其余文件说明：`wmi-test.txt`、`restarter-out.txt` 为运行期输出；`.workbuddy\memory\` 存放工作区记忆；`light\.dumate\inbox` 为 AI 协作收件箱目录；`E-C` 仅存 `.vs` 构建残留。`D:\YX` 本身在 git 管理下，远端为 `https://github.com/yuxuan-gzs/light.git`，默认分支 `main`，由根目录 `bt.bat` 负责自动提交与推送。

`.gitignore` 的忽略策略体现了工程的构建习惯：编译产物（`*.obj`、`*.exp`、`*.zip`、`*.log`）、IDE 输出（`.vs/`、`x64/`、`Debug/`、`Release/` 及 `ModMigrator/x64/`、`ModMigrator/.vs/`、`ToMCreatorForge/.vs/`、`light/.vs/` 等逐个工作区显式列出）、第三方目录（`Full Port Network Packet Capture Tool/`）均不入库；`**/.vs/` 兜底规则覆盖所有嵌套目录。可见仓库刻意保持"源码与脚本入库、产物与中间态隔离"的干净结构——这与 `bt.bat` 的 `git add -A` 自动提交策略配合，能保证每次推送的都是可复查的源码改动。

---

## light 主项目

### 进程形态与运行模式

light 是一个单可执行文件的 Windows 安全工具箱：以 `light.exe` 为宿主，根据 `Cache\g.xayx` 中的加密标记决定启动形态。该标记由 `SetGuiMode.ps1 -EnableGui` 写入，值为 `dkg33h4x2f24hdg5x2f2g4:FT12sf`（默认控制台模式），解密后等于 `dkg33h4x2f24hdg5x2f2g4:Tdsg2` 即进入 GUI 模式并隐藏控制台窗口（`IsGuiModeConfigured` 判断）。若内置 GUI 插件（`YXGuiPlugin`）加载失败，会自动回退到 `PluginManager::FindGuiPlugin` 查找磁盘上的其他 GUI 插件——这一回退设计让宿主与具体 GUI 解耦：即使内置界面损坏，用户仍可通过其他 GUI 插件完成操作。

程序支持**命令行直入模式**：非管理员环境下直接执行单条命令后退出，不初始化 GUI、不隐藏控制台、不启动服务。该模式的核心流程是加载 `plugins\` 目录插件与 API 插件（`SilentLoadApiPlugins`），执行命令（`ExecuteOneCommand`），最后统一卸载并关停 API：

```cpp
// 命令行直入模式核心流程（light.cpp，main 尾部）
if (!IsRunningAsAdmin()) {
    // 非管理员：直接执行单条命令后退出
    LoadPluginsFromDirectory(cd + "\\plugins\\", true);
    SilentLoadApiPlugins(cd);
    g_NetworkInterceptor.Start();       // 拦截引擎随命令行联动
    ExecuteOneCommand(...);             // 执行用户命令
    UnloadAllPlugins();                 // 卸载全部插件
    LightApi_Shutdown();                // 关停 API 层
    g_NetworkInterceptor.Stop();        // 停止抓包并清空规则
}
```

这种"命令到进程"的一次性模型保证：不常驻、无残留、权限面最小，适合被其他程序（如服务、批处理、计划任务）安全调用。管理员环境下则进入完整初始化：安装/连接系统服务、加载 GUI 插件、建立托盘交互。程序运行期还通过 `a1.vbs` 等辅助脚本配合 `Full_Port_Network_Packet_Capture_Tool.exe`（全端口数据包捕获工具）完成抓包协作。

### main 启动序列详解

`main()` 的实际执行顺序（依据 light.cpp 末尾的 main 实现）可拆成如下阶段，每一阶段都对应一组真实存在的函数与常量：

1. **路径定位与基本属性读取**：`GetCurrentProgramPath()` 得到程序所在目录 `cd`；`IsRunningAsAdmin()` 检查当前令牌是否具备管理员权限；`SetColor()`、`LoadColorConfig()` 完成控制台配色初始化（含从配置文件读取配色方案）。路径常量集中定义：`lightPath = cd + "light.exe"`、`exePath = cd + "\\rppcst\\CEL.exe"`、`vbsPath = cd + "\\rppcst\\a1.vbs"`、`fullPortTool = cd + "\\rppcst\\Full_Port_Network_Packet_Capture_Tool.exe"`、`registryPath = cd + "\\rppcst\\sys.xayx"`、`filePath = "C:\\Program Files\\.light\\light.xbyx"`、`betPath = cd + "\\rppcst\\light.exe"`、`Registry = cd + "\\Registry.exe"`、`BinaryHexEditor = cd + "\\BinaryHexEditor.exe"`。

2. **非管理员直入分支**：如前所述，非管理员直接走单命令模式，不触碰服务与 GUI。

3. **管理员分支——提权与特权**：`ElevateCurrentProgram()` 在需要时以 UAC 重新拉起自身；`EnableSecurityPrivileges()` 开启 `SeTakeOwnershipPrivilege`、`SeRestorePrivilege` 等安全特权（文件锁定与驱动操作的前置条件）。

4. **安全库版本与安装标记**：`UpdateSecurityNeValue_it` 系列函数维护安全策略数值；`CheckAndInstallService()` 检查 `LightService` 是否已注册，未注册则 `InstallService()` 打开服务控制管理器（`OpenSCManagerA(SC_MANAGER_ALL_ACCESS)`）并 `CreateServiceA` 创建系统服务；随后 `StartLightService()`（`OpenSCManagerA(SC_MANAGER_CONNECT)`）与 `StopLightService()` 按需启停。若命名管道连接失败（758–774 行片段），程序会按既定路径降级处理并打印诊断信息。

5. **配置环境与文件夹锁定**：`peizui()` 写入运行期配置；`LockFolderWithTrustedInstaller` 系函数锁定目标目录（见"文件夹锁定"一节）。

6. **安全核心与通知系统**：`SecurityCore` 初始化；`NotifyWindow` 设置 `SetDisplayDuration(3000)`（气泡显示 3 秒）与 `SetMaxQueueSize(10)`（最多排队 10 条通知，超出合并）。

7. **数据包拦截引擎**（GUI 模式下同样生效）：`NetworkInterceptor` 从 `whitelist.json` 逐行读取白名单（跳过以 `#` 开头的注释行与空行）；`ClearAllBlocks()` 清空历史规则；`SetBlockCallback(...)` 注册拦截回调，回调体拼装 `"[拦截] 进程 (PID:x) -> domain"` 格式的消息并触发通知窗；随后 `Start()` 打开 WinDivert 句柄并启动抓包线程。

8. **恶意网址拦截库远程更新**：构造安全库 URL `https://raw.githubusercontent.com/yuxuan-gzs/Forge-NeoForge-and-Fabric/security/PM%20%E6%81%B6%E6%84%8F%E7%BD%91%E5%9D%80%E6%8B%A6%E6%88%AA%E5%BA%93Pro_V" + securityNe + ".zip"`，`CheckRemoteFileExists` 探测远端存在后调用 `securityCore.UpdateFromGitHub()` 拉取并合并恶意域名库——拦截规则集可以在不升级主程序的前提下持续更新。

9. **插件体系装载**：`PluginManager` 扫描并加载插件，`PluginLoadCallback` 汇报逐项加载进度；`LoadPluginsFromDirectory(cd + "\\plugins\\")`（带 UI 反馈）与 `SilentLoadApiPlugins(cd)`（静默加载 API 插件）双通道装载；`FindGuiPlugin` 在 GUI 插件列表中定位可用宿主。

10. **主循环**：GUI 模式下 `IsGuiModeConfigured()` 成立即 `ShowWindow(hConsoleWindow, SW_HIDE)` 隐藏控制台；否则保持控制台并进入 `PrintPrompt() + getline + ExecuteOneCommand()` 的交互循环，直到输入 `exit` 命令退出。

11. **退出清理**：按 `UnloadAllPlugins(true)` → `LightApi_Shutdown()` → `NetworkInterceptor::Stop()` 的顺序释放资源，托盘图标、通知线程、抓包线程全部同步回收。

### 主程序 lightcpp

`light.cpp` 是全仓库最核心的文件，共 7490 行，承担：命令解析与分发、XZYX 加密压缩、系统服务管理、文件夹锁定、网络拦截命令、下载器、注册表操作与 GUI 启动判断。其内部约 80 个函数，按职责可归为十大类：

| 函数 | 类别 | 作用 |
| --- | --- | --- |
| `CheckAndInstallService` / `StartLightService` / `StopLightService` / `InstallService` | 服务管理 | 安装/启停 Windows 服务管家 |
| `SendToService` | 服务管理 | 通过命名管道向服务发送命令 |
| `LockFolderWithTrustedInstaller` / `Lock_ProtectFolderRecursively` / `Lock_RestoreFolderRecursively` / `UnlockFolderForAdmin` / `RestoreFolderPermission` | 文件保护 | 取得 TrustedInstaller 权限并递归锁定/恢复文件夹 |
| `XorEncryptDecrypt` | 加密 | 异或加解密原语 |
| `CompressFolderToXZYXSecure` / `ExtractXZYXFileSecure` / `ListXZYXContent` | 加密 | XZYX 安全压缩/解压/内容列表 |
| `DownloadFileWithProgress` | 网络 | 带进度回调的 HTTP 下载 |
| `HandleBlockCommand` … `HandleUnblockCommand` | 网络拦截 CLI | 黑名单增删 |
| `HandleShowBlocksCommand` / `HandleClearBlocksCommand` | 网络拦截 CLI | 拦截列表查看/清空 |
| `HandleWhitelistCommand` / `HandleUnwhitelistCommand` | 网络拦截 CLI | 白名单管理 |
| `HandleCompressCommand` / `HandleExtractCommand` / `HandleListCommand` | 加密 CLI | 压缩相关命令 |
| `HandleDownloadCommand` | 网络 CLI | 下载命令 |
| `HandleReloadPluginsCommand` / `HandleCreatePluginCommand` | 插件 CLI | 插件热重载 / 模板生成 |
| `HandleLockCommand` / `HandleUnlockCommand` | 文件保护 CLI | 文件夹锁定/解锁 |
| `HandleRegistryCommand` / `HandleBinaryHexEditorCommand` / `HandleCELCommand` / `HandleFPNPCTCommand` | 工具启动 CLI | 拉起注册表管理器、二进制编辑器、CEL、数据包捕获工具 |
| `GenerateCVID` | 工具 | 生成 32 位十六进制客户端 ID |
| `SetColor` / `LoadColorConfig` | UI | 控制台配色与配色配置加载 |
| `IsRunningAsAdmin` / `ElevateCurrentProgram` / `EnableSecurityPrivileges` / `UpdateSecurityNeValue` | 权限 | 管理员校验、UAC 提权、安全特权开启、安全策略值更新 |
| `help` / `about` / `peizui` / `uninstall` | 内置命令 | 帮助、关于、配置、卸载 |

静态关键数据：加密密钥 `XZYX_INTERNAL_KEY = "LightXZYX2026Secure!@#$%^&*64seressg64regt64ndzwq4tmo5p"`（`light.cpp` 第 111 行）；注册文件路径 `registryPath = cd + "\\rppcst\\sys.xayx"`；`HKEY_LOCAL_MACHINE` 下的 `xayx / xbyx / xcyx / about` 四个注册表键用于记录安装与级别信息。

### 命令分发系统

程序通过命令行第一参数分派到对应处理函数，形成一张"命令 → 处理器"的表驱动结构。所有处理器均遵循同一约定：解析子参数 → 调用底层模块 → 输出结果到控制台。命令分发发生在 `ExecuteOneCommand`，以 `if / else if` 链逐项匹配参数名：

| 命令 | 处理器（行号） | 底层调用 |
| --- | --- | --- |
| `help` | 内置 | 命令帮助 |
| `showblocks` | `HandleShowBlocksCommand`(4786) | 遍历 `GetStats().blockedDomains` |
| `clearblocks` | `HandleClearBlocksCommand`(4809) | `ClearAllBlocks()` |
| `exit` | 内置 | 退出主循环 |
| `cel` | `HandleCELCommand`(4862) | 启动 `rppcst\CEL.exe` |
| `uninstall` | `HandleUninstallCommand`(4868) | 触发卸载流程 |
| `fpnpc` | `HandleFPNPCTCommand`(4873) | 启动 `Full_Port_Network_Packet_Capture_Tool.exe` |
| `about` | `HandleAboutCommand`(4879) | 版本与版权信息 |
| `fill` | `HandleFillCommand`(4883) | 填充/补全指令 |
| `dump` | `HandleDumpCommand`(4914) | 数据导出 |
| `lock <目录>` | `HandleLockCommand`(4994) | `LockFolderWithTrustedInstaller` 系 |
| `unlock <目录>` | `HandleUnlockCommand`(5053) | `UnlockFolderForAdmin` / `RestoreFolderPermission` |
| `registry` | `HandleRegistryCommand`(5106) | 启动 Registry.exe |
| `hex` | `HandleBinaryHexEditorCommand`(5145) | 启动 BinaryHexEditor.exe |
| `restorefolderpermission` | `HandleRestoreFolderPermissionCommand`(5184) | `RestoreFolderPermission` |
| `compress <路径>` | `HandleCompressCommand`(5188) | `CompressFolderToXZYXSecure` |
| `extract <xzyx>` | `HandleExtractCommand`(5279) | `ExtractXZYXFileSecure` |
| `list <xzyx>` | `HandleListCommand`(5328) | `ListXZYXContent` |
| `download <url> <out>` | `HandleDownloadCommand`(5340) | `DownloadFileWithProgress` |

### 下载体系与进度反馈

`download <url> <out>` 底层是 `DownloadFileWithProgress`，入口为多线程分块下载 `DownloadFileWithProgressMulti`（light.cpp 第 3029 行）：

- **先探测再分块**：`GetFileSizeFromURL` 先行解析内容长度；探测失败（≤0）则退回单线程 `DownloadFileWithProgressSingle`（`URLDownloadToFileA` + 回调对象轮询，最多等待 600×100ms，即 60 秒）；
- **自适应线程数**：`<10MB` 用 2 线程，`<50MB` 用 4 线程，更大用 8 线程；`chunkSize = fileSize / threadCount`，每个线程经 `DownloadChunk` 按 `Range` 首尾写同一文件（`CreateFileA` 预建后 `FILE_SHARE_WRITE` 共享写）；
- **原子进度**：`atomic<long long> totalDownloaded` 跨线程累加，主循环每 500ms 读一次并换算百分比，用 `\r` 回车在控制台原位刷新 `下载进度: N%`，到 100% 跳出后 `join` 全部线程，输出 `下载完成!`；
- **内存下载通道**：另有 `DownloadFileWinHTTP` 专用路径（第 3076 行）——全程宽字符、**手写 URL 解析**（按 `https://`/`http://` 前缀切分主机与路径，刻意不依赖 `WinHttpCrackUrl` 以规避 UNICODE 宏与 `LPCWSTR` 的不兼容坑），内存中完成下载后一次性写盘，避免半成品文件；带 302 重定向跟随，User-Agent 伪装为 Edge 浏览器（`Mozilla/5.0 ... Edg/`），适合对 UA 敏感的资源站。

该体系服务于插件模板拉取、安全库更新、mod 下载等多处：`HandleDownloadCommand`（第 5340 行）解析 `download <url> <out>`，把命令行参数转成 URL 与落盘路径后复用上述同一入口；安全库 zip 更新流程（第 5437/5540/5570 行）同样经由 `DownloadFileWithProgress` 拉取并检验。多线程分块 + 单线程回退双路并存的策略，兼顾了"大文件吞吐"与"小文件/弱兼容服务器的可用性"。
| `reloadplugins` | `HandleReloadPluginsCommand`(5586) | `ReloadAllPlugins` |
| `reloadplugin <id>` | `HandleReloadPluginCommand`(5603) | 单插件热重载 |
| `loadplugin <path>` | `HandleLoadPluginCommand`(5630) | 加载单个插件 |
| `plugins` | `HandlePluginsCommand` | 列出已加载插件 |
| `createplugin <名称>` | `HandleCreatePluginCommand` | `PluginTemplateGenerator` |
| `guiprovider` | `HandleGuiProviderCommand` | 列出/选择 GUI 提供者 |
| `block <域名> [秒]` | `HandleBlockCommand`(4717) | `BlockDomain(domain, timeoutMs)` |
| `unblock <域名>` | `HandleUnblockCommand`(4766) | `UnblockDomain` |
| `whitelist <域名>` | `HandleWhitelistCommand`(4822) | `AddWhitelistDomain` |
| `unwhitelist <域名>` | `HandleUnwhitelistCommand`(4842) | `RemoveWhitelistDomain` |

### ExecuteOneCommand 的分发机制

命令分发的真正入口是 `ExecuteOneCommand`（light.cpp 第 248691 字符附近的原型定义）：函数签名携带 `cd`（当前目录，引用传递，`cd` 命令可修改）、`SecurityCore*`（安全核心）、`PluginManager*`（插件管理器）、`pluginsDir`、`registryPath`、`folderPath` 六路上下文——处理器无需自行获取这些依赖，全部由调用方注入。执行体先 `TrimCopy` 去掉首尾空白，再做三类前缀匹配：**裸前缀命令**（`command.rfind("block ", 0) == 0 || command.rfind("Block ", 0) == 0`——同时接受小写与大写两种形态，`cd`/`unblock`/`whitelist`/`unwhitelist` 同理）；**精确全等命令**（`help`/`exit`/`showblocks`/`clearblocks`/`CEL`/`uninstall` 等调用 `command == "..."` 严格比较）；**特殊前缀**——`cmd ` 前缀把其后内容交给 `RunCommandViaCmd` 在 light 窗口内执行原生 cmd 指令，单独的 `cmd`（三个字符）则 `LaunchInteractiveCmd` 打开交互式控制台，形成"light 命令 + 原生 cmd 命令"双命令空间。每个匹配分支都 `return true`，仅在 `exit` 返回 `false` 通知主循环退出；`help` 分支在打印内置帮助后还会调用 `ExecutePluginCommand(command)` 把帮助请求透传给插件层，让插件自定义命令也能出现在帮助列表。匹配不到任何内置命令时，命令会下沉到插件管理器的 `ExecutePluginCommand` 或安全库的命令通道，保证未知命令"有兜底、不静默"。

### HandleGuiProviderCommand 与插件热管理

`HandleGuiProviderCommand` 负责在多个 GUI 提供者之间选择：宿主维护 `g_guiProvider` 全局指针，`guiprovider` 命令无参数时列出全部已注册提供者（内置 `YXGuiPlugin` 与外部 GUI 插件），带参数时切换目标提供者并重启对应界面上下文。`HandlePluginsCommand` 遍历 `pluginManager` 的插件表，逐行打印 `id / name / version / enabled` 状态；`HandleLoadPluginCommand(5630)` 接受绝对路径或相对路径，调用 `LoadPlugin` 后把加载结果（成功或错误原因）回显；`HandleReloadPluginsCommand(5586)` 走 `ReloadAllPlugins` 全量重载，`HandleReloadPluginCommand(5603)` 按插件 ID 定向热重载单个插件——后两者是插件开发者的主力调试手段，配合 VCBlocks 的 `replace_plugin.bat` 与 MCLauncher 的构造流程形成"改码 → 编译 → 热替换 → 运行观察"的循环。`HandleCreatePluginCommand` 把剩余参数（插件名、可选配置）透传给 `PluginTemplateGenerator`，落盘即完成脚手架——无需手动编写 CMake 与 manifest。
几个处理器的行为细节（均来自源码实际实现）：

- **`HandleBlockCommand`**（第 4717 行）：语法 `block <域名> [时间(秒)]`。先检查目标域名是否命中白名单——若在白名单中则打印警告并**拒绝拦截**（放行优先策略）；否则将秒数乘 1000 得到毫秒超时，调用 `BlockDomain(domain, timeoutMs)`（不传时间则永久拦截），命中后 `g_NotifyWindow->ShowBlockNotification("System", domain)` 弹出系统级通知。超时型规则由内部 `CleanupExpiredStreams` 定时清理，实现"临时封禁自动过期"。

- **`HandleShowBlocksCommand`**（第 4786 行）：读取拦截引擎统计 `GetStats()`，若 `blockedDomains` 为空打印 `(空)`；否则逐条输出 `✗ 域名 (拦截 N 次)`（第二项为累计拦截次数），末尾给出总数与提示「重启程序后黑名单自动清空」——这确认了黑名单是**进程内临时态**：仅存在于内存，不落盘、不跨会话，重启即恢复干净的网络环境，适合临时封禁与家长控制场景。

- **`HandleFillCommand`**（light.cpp 第 4883 行）：fill 是"填充清理"语义——读取注册表文件并沿 `{ "HKEY_LOCAL_MACHINE", "xbyx" }` 路径查 `fill` 值；值为 `air` 时，等待用户在控制台输入一行路径，随后 `DeleteFilesInDirectory(deletePath)` 递归清空该目录；值非空时打印当前 fill 值；两者都未命中则提示"未找到 fill 值"。该命令是注册表与文件系统联动的典型案例：配置存在 `sys.xayx` 加密注册表中，行为却作用于磁盘目录。
- **`HandleDumpCommand`**（第 4914 行）：调试导出命令。先校验注册表文件存在性与非空（`seekg` 到末尾取大小），`ReadRegistryFile` 解密后检查是否含 `NODE:` 关键字做格式自检（存在输出 ✓ 成功标记，缺失输出 ⚠ 警告），随后写出**两份导出物**到 `Cache\decrypted_hex.txt` 与 `Cache\decrypted_raw.txt`：前者按行编号，把不可打印字符（除 `\t`/`\r`/`\n` 外的小于 32 的字符）替换为空格后输出为"十六进制 + ASCII 混合显示"的可读文本，后者以二进制模式原样落盘——一份给人读、一份给工具读，覆盖面完整。
- **`HandleLockCommand`**（第 4994 行）：文件夹锁定四步走：非管理员先 `ElevateCurrentProgram()` 提权；`ResolvePath` 解析路径并校验目录存在性与属性（`GetFileAttributesA`，要求是目录而非文件）；控制台 y/n 二次确认（防误操作）；`EnableSecurityPrivileges()` 开启必要安全特权后调用 `Lock_ProtectFolderRecursively` 递归改写 ACL 达到"不可访问"效果。解锁 `HandleUnlockCommand` 对称执行：`Lock_RestoreFolderRecursively` 逐层恢复权限。两者全程 `StartSpinner/StopSpinner` 旋转动画提示，失败时分别提示"检查是否有其他程序正在使用该文件夹"与"请检查文件夹权限状态"。
- **`HandleUninstallCommand`**（第 4868 行）：先 `RestoreFolderPermission(folderPath)` 归还文件夹权限再执行 `uninstall()`，避免"文件夹被锁导致卸载残留"的经典坑位。
- **`HandleRegistryCommand` / `HandleBinaryHexEditorCommand`**（第 5106/5145 行）：两命令形态一致——询问"是否等待关闭后再继续？(y/n)"，接受 `y/yes/是` 或 `n/no/否`（含大小写与首尾空白剔除），经 `LaunchProgram(exe, waitForCompletion)` 拉起 `Registry.exe` / `BinaryHexEditor.exe`；`LaunchProgram` 用 `ShellExecuteExA` + `open` 动词 + `SEE_MASK_NOCLOSEPROCESS` 启动，按需 `WaitForSingleObject(INFINITE)` 等待完成，避免外部工具与主进程抢占控制台输出。
- **`HandleCELCommand` / `HandleFPNPCTCommand`**（第 4862/4873 行）：`system()` 直接启动 `rppcst\CEL.exe` 与 `rppcst\Full_Port_Network_Packet_Capture_Tool.exe`，封装对外部工具的"一键拉起"语义- **`HandleShowBlocksCommand`**：读取 `GetStats().blockedDomains`，若为空打印"当前无拦截"，否则逐行列出域名。
- **`HandleCompressCommand`**：语法 `compress "<文件或文件夹路径>" [-d 输出目录] [-c 名称]`。默认输出目录为 `GetCacheXzyxPath()`（即 `Cache\ysXzyxs`）；`IsValidFileName` 校验输出名合法性；解析完成后 `StartSpinner()` 启动旋转动画，调用 `CompressFolderToXZYXSecure(resolvedInput, outputFile, true)`（第三参数为覆盖允许开关），完成后 `StopSpinner()`。输出文件扩展名为 `.xzyx`。
- **`HandleExtractCommand`**：语法 `extract "<xzyx文件>" [<输出文件夹>]`，输出目录缺省时取压缩包文件名加 `_extracted` 后缀。
- **`HandleListCommand`**：不解压直接枚举容器内条目清单。

命令系统的分层清晰：CLI 层只做参数解析与结果输出，业务逻辑全部下沉到 `NetworkInterceptor`、加密模块与服务封装中，保证同一能力既能被命令行调用，也能被 GUI 插件、API 插件复用。

### 文件加密与 XZYX 压缩体系

light 生态使用自定义加密容器格式，扩展名按安全级别分三级：

| 扩展名 | 级别 | 说明 |
| --- | --- | --- |
| `.xayx` | 1 级文件 | 常规加密文件（注册数据、配置、GUI 标记等） |
| `.xbyx` | 2 级文件 | 二级加密文件（如 `C:\Program Files\.light\light.xbyx`） |
| `.xcyx` | 3 级文件 | 三级加密文件 |

容器压缩输出使用 `.xzyx` 扩展名（`Cache\ysXzyxs` 目录），与三级文件扩展名对应：`xzyx` 代表"压缩档"，`xayx/xbyx/xcyx` 代表"加密文件"。

实现依赖三条技术链：

1. **异或加密原语** `XorEncryptDecrypt`：以 `XZYX_INTERNAL_KEY` 为密钥做循环异或，是轻量混淆层；该原语同时被 `yuxuangzs-Service-light`（管道命令混淆，密钥 `LightPipeKey2024!@#`）与 `Registry`（`XayxEncryptor`，密码 `sr5r4g464dry8k78665sglnjtr4m5z578tk78q`）复用，是整个生态的加密公共子集；其实现（light.cpp 第 793 行起）极为精简——对空数据或空密钥直接返回原文，否则逐字节 `result[i] = result[i] ^ key[i % keyLen]` 循环异或，明文长度与密文长度一致，属于对称混淆而非强加密；配套的 `ReadRegistryFile` 以二进制方式读入整文件（先 `seekg` 到文件尾取大小、空文件返回空串），随后用 `PASSWORD` 密钥整体异或还原明文文本；
2. **LZMA 压缩**：仓库内嵌 `lzma/C` 完整 SDK，链接 `LzmaLib.lib`；`CompressFolderToXZYXSecure` 在异或前先对整目录做 LZMA 压缩，`ExtractXZYXFileSecure` 解压后校验头信息，`ListXZYXContent` 可不解压直接枚举内容——典型"先压缩、后混淆"的容器思路，兼顾体积与保密性；

### XZYX 容器二进制格式

实际实现中 `.xzyx` 容器是一个扁平的二进制流，无中央目录、无局部文件头，全部条目顺序堆放，结构紧凑：

1. **文件头**：开头 4 字节小端 `uint32` 记录条目总数 `totalItems`；
2. **每条目**：4 字节 `nameLen` + 名称字节串 + 1 字节 `isDir`（0 文件 / 1 目录）+ 8 字节原始大小 `origSize` + 8 字节压缩后大小 `compSize` + `compSize` 字节数据；目录条目两者均为 0；
3. **压缩粒度**：逐文件压缩而非整体压缩，`SecureCompressor::CompressSecure` 对每个文件独立压缩，**若压缩结果不小于原文则直接存放原文**（`output = input`），保证容器体积不会因"压缩失败反而膨胀"；
4. **目录结构**：目录本身不存数据，仅以 `isDir=1` 占位；文件名内嵌完整相对路径（含 `\` 分隔），目录层级由路径串隐含。

压缩端 `CompressFolderToXZYXSecure` 的收集逻辑用 `FindFirstFileA/FindNextFileA` 递归遍历输入树（跳过 `.` 与 `..`），单个文件 `ifstream` 二进制读入；`includeFolder` 为 true 时把顶层文件夹名作为第一个目录条目。输出端 `ExtractXZYXFileSecure` 按同一布局顺序解析：先读总数，再逐条读 `nameLen/名称/isDir/origSize/compSize`，目录条目立即 `CreateDirectoryA`，文件条目先找出最近分隔符自动创建父目录，再解压写盘；单条解压失败 `continue` 跳过而不终止整个解压，给出错误行号后继续处理其余条目。`ListXZYXContent` 则只读头部与条目元数据、跳过 `compSize` 字节数据（`pos += compSize`），依据名称中 `\` 的数量计算深度，输出带树枝符号（`├──`/`└──`/`│`）与 `📁/📄` 图标的树形图，末尾打印 `总计: N 个项目 (文件: X, 目录: Y)`，实现"不解压直接预览"。

### 三层压缩栈

真实链路为 `SecureCompressor → LZMACompressor → UltraCompressor` 纵深三层的选择器式压缩：

- **SecureCompressor**：门面层。调 `LZMACompressor::CompressLZMA`，失败或结果不小于原文则回落原文；`DecompressSecure` 先尝试 `DecompressLZMA`，失败则将输入原样放回并 `resize(origSize)`——**接受"未压缩原文"的容器也能正确解压**，两个方向均可降级；
- **LZMACompressor**：调用内嵌 `LzmaLib`（`LzmaCompress`，参数 `level=9`、字典 `1<<24`、`lc=3 lp=0 pb=2`、`fb=273`、`numThreads=1`），输出为 `LZMA_PROPS_SIZE` 字节属性头（props）拼接压缩流；解压时先取 props 再 `LzmaUncompress`。缓冲区预算为 `srcLen / 3 + 1MB` 的膨胀预留，规避极端输入下目标缓冲不足；
- **UltraCompressor**：内置五原语 `DeltaEncode`（相邻差分）、`LZ77Tiny`（小型滑动窗口匹配）、`RLEEncode`（行程编码）、`HuffmanEncode`（哈夫曼表）与 `BitPack`（比特紧凑）；压缩时并行生成四条候选流水线（Delta→LZ77→RLE→Huffman→BitPack、LZ77→RLE→Huffman→BitPack、Huffman→BitPack、RLE→Huffman→BitPack），逐条记录体积，选取最小者，方法号 0–3 写入输出第 8 字节（紧接 `origSize` 之后）；若最小者仍不小于原文，则写 `0xFF` 标记原文直接存储；解压按方法号反向执行 `BitUnpack → HuffmanDecode → RLEDecode →（LZ77TinyDecode → DeltaDecode 按方法）`。
3. **zlib / minizip**：`pragma comment(lib, ...)` 链接 zlib-1.3.2 与 minizip，用于 zip 打包场景（如插件模板 `PackToZip`、ModMigrator 的 `exportZip` 开关）。

与加密体系配套的是文件关联安装：**系统注册文件 `sys.xayx` 的内部格式**：`ReadRegistryFile` 解密后得到的并非二进制结构，而是一份**带标记的文本**，标记语法为 `NODE:<节点名>`、`ISKEY:0/1`、`VALUE:<值>`，每条记录以换行结尾。`FindValueInRegistry(data, path, valueName, valueIndex)` 沿 `NODE:` 链逐级定位：先在明文里查找 `"NODE:" + 路径首节点`，命中后继续向后查找下一级节点，找到目标节点后扫描其后的 `ISKEY:` 字段（`ISKEY:0` 表示该节点是值而非子键），再在 `VALUE:` 前缀之后截取到换行符为止得到值；若同名值出现多次（例如同名注册表项的多个值），用 `valueIndex` 选取第 N 个。`GetSecurityNeValue` 即沿 `{ "HKEY_LOCAL_MACHINE", "xayx" }` 路径读取 `security_Ne` 字段，得到形如 `1.1.1` 的安全库版本号——这正是远程恶意域名库更新 URL 中拼接的版本段：

```cpp
// 安全库 URL（light.cpp 实际拼接）
"https://raw.githubusercontent.com/yuxuan-gzs/Forge-NeoForge-and-Fabric/security/"
"PM%20%E6%81%B6%E6%84%8F%E7%BD%91%E5%9D%80%E6%8B%A6%E6%88%AA%E5%BA%93Pro_V"
+ securityNe + ".zip"
```

`UpdateSecurityNeValue` 的更新逻辑同样值得注意：读取 `security_Ne` 旧值后，仅提取其中的数字字符（`isdigit` 过滤），把 `1.1.1` 拆成三位整数，末位加一后重组为 `1.1.2` 写回；写回过程是"整个明文重建 → 整体异或 → `ofstream` 二进制落盘"，保证文件在任一时刻要么是旧版本、要么是新版本，不存在半写入状态。`UpdateSecurityNeValue_it` 则是一个快捷变体，直接把版本号写死为 `1.1.2`。整个更新流程伴随控制台分色日志：`[安全配置]` 前缀 + `L_error / L_success / L_default` 三色输出，便于在终端中直接观察版本号递增过程。安全库版本的远程探测由 `CheckRemoteFileExists` 完成：`WinHttpOpen(L"Mozilla/5.0", ...)` 以浏览器 UA 建立会话，按 `https://`/`http://` 拆分出主机与路径，`WinHttpConnect` 后发起 **HEAD** 请求（`WinHttpOpenRequest(hConnect, L"HEAD", ...)`），再用 `WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER` 查询状态码，200 即认为远端存在，随后 `securityCore.UpdateFromGitHub()` 拉取并合并规则集。

与加密体系配套的是文件关联安装：`bt.exe` 安装程序在 `C:\Program Files\.light\` 下创建**隐藏目录**、写入**隐藏文件** `light.xbyx`，把 `favicon.ico` 复制为系统图标，注册 `.xayx / .xbyx / .xcyx` 三种文件类型默认此打开方式为 `BinaryHexEditor.exe`（`RegisterFileType`），完成后 `SHChangeNotify` 刷新资源管理器图标缓存。整个注册过程由 `bt.cpp` 的 `RequireAdmin` 以管理员权限执行，避免注册表权限问题。

### 系统服务与命名管道

服务端工程 `yuxuangzs-Service-light` 以 125 行 C++ 实现了一个最小化 Windows 服务 `LightService`，其作用是把"需要管理员/系统权限的操作"与普通用户进程隔离：

- **命名管道**：`\\.\pipe\LightPipe`，`PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT`，缓冲区 4096/4096 字节——服务端 `CreateNamedPipeA` 后进入 `ConnectNamedPipe` 循环，一个客户端一个对话；
- **通信协议**：客户端发送 `run:<命令行>` 前缀，服务端解析后调用 `RunAsUser` 以**活动用户身份**启动进程——注意这里是"以用户身份运行"，而非以服务身份运行，用于解决服务在 Session 0 中无法显示交互界面、无法访问用户桌面的经典问题；
- **身份转交链**：`WTSGetActiveConsoleSessionId`（取当前控制台会话）→ `WTSQueryUserToken`（取该会话用户令牌）→ `DuplicateTokenEx`（复制为主令牌）→ `CreateEnvironmentBlock`（建立用户环境块）→ `CreateProcessAsUserA` 在 `winsta0\default` 桌面以 `CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE` 启动 → `WaitForSingleObject(INFINITE)` 等待进程退出后返回结果；
- **异或保护**：管道命令以 `PIPE_KEY = "LightPipeKey2024!@#"` 做异或混淆，防止明文命令在管道中被简单截获；
- **控制处理**：`ServiceCtrlHandler` 处理 `SERVICE_CONTROL_STOP`，支持标准 SCM 停止流程；
- **回包协议**：命令执行成功回 `OK`，失败回 `FAIL`。

宿主侧 `CheckAndInstallService` 负责将该 exe 注册为系统服务（`OpenSCManagerA(SC_MANAGER_ALL_ACCESS)` → `CreateServiceA` 链路），`StartLightService / StopLightService` 启停（连接时仅需 `SC_MANAGER_CONNECT` 权限），`SendToService` 封装管道写读并在超时后返回。主程序在需要提权持久操作（如文件夹锁定后的自检）时经由此通道中转，避免每次操作都弹 UAC。接入管道之前的握手逻辑在 `SendToService`（light.cpp 第 757 行）中实现：先 `StartLightService()` 确保服务在线，把命令经 `XorPipe(command)` 异或混淆后，循环 `CreateFileA("\\.\pipe\LightPipe", GENERIC_READ | GENERIC_WRITE, ...)` 尝试连接——最多重试 20 次、每次间隔 500 毫秒（合计最多等待 10 秒），可容忍服务启动的瞬态延迟；连接成功后 `WriteFile` 写入密文，`ReadFile` 读回最多 4096 字节应答，再 `XorPipe` 解密得到明文结果，`CloseHandle` 后 `StopLightService()` 收尾。整个过程"启服务 → 加密 → 写管道 → 读管道 → 解密 → 停服务"是成对出现的，服务不会因一次调用而常驻，减少攻击面。

宿主侧启动外部程序统一走 `LaunchProgram(exePath, waitForCompletion)`：`ShellExecuteExA` 以 `open` 动词、`SEE_MASK_NOCLOSEPROCESS` 掩码启动，取得进程句柄后按需 `WaitForSingleObject(INFINITE)` 等待完成并 `CloseHandle` 释放——`cel / fpnpc / registry / hex` 等命令拉起的外部工具均复用此函数。

### 文件夹锁定

为避免用户文件被误删/篡改，light 实现了基于 Windows ACL 的文件夹锁定四件套：

1. `LockFolderWithTrustedInstaller`：先通过 `EnableSecurityPrivileges` 开启 `SeTakeOwnershipPrivilege`、`SeRestorePrivilege` 等安全特权，再接管文件夹所有权；
2. `Lock_ProtectFolderRecursively`：递归遍历子目录，改写 DACL，仅保留 System / TrustedInstaller 的完全控制，从而对普通账户与管理员均"拒绝写入/删除"——即使以管理员身份打开资源管理器也无法改动；
3. `Lock_RestoreFolderRecursively`：还原原始 DACL；`UnlockFolderForAdmin` 供管理员快速解锁；
4. `RestoreFolderPermission`：完整归还所有权与权限，保证解锁后文件属性无残留。

该功能与 `UpdateSecurityNeValue` 配合：后者将系统安全策略值（如与 UAC、防护相关的注册表值）更新为推荐配置，形成"先加固系统、再锁定文件"的纵深防御。锁定目标典型为个人资料、项目源码等易被勒索软件或误操作破坏的目录。

### 网络拦截子系统

网络拦截是 light 的功能亮点，分**用户态 WinDivert 方案**与**内核态 WFP 驱动原型**两层：

**用户态：`NetworkInterceptor.cpp`（2202 行）+ `NetworkInterceptor.h`**

- 依赖 `WinDivert-2.2.2-A`，头文件内直接 `#pragma comment(lib, "../../../WinDivert-2.2.2-A/x64/WinDivert.lib")` 链接 x64 版导入库；
- 采用单例模式：`NetworkInterceptor::instance()` 返回全局实例，另有全局指针 `g_NetworkInterceptor` 供辅助代码引用；`.cpp` 内匿名命名空间提供 `ToLower`、`NormalizeDomain`（去首尾空白 + 转小写）等纯工具函数，保证域名匹配不区分大小写、不受空白干扰；
- **接口全景**（头文件公开的完整方法集）：

| 方法 | 语义 |
| --- | --- |
| `Start / Stop` | 启动/停止抓包线程与拦截引擎 |
| `BlockDomain(domain, timeoutMs)` | 将域名加入黑名单（支持毫秒级超时自动过期） |
| `BlockDomainForProcess(domain, processName)` | 仅对指定进程名生效的域名拦截 |
| `UnblockDomain(domain)` | 移除域名黑名单 |
| `ClearAllBlocks()` | 清空全部黑名单规则 |
| `IsBlocked(domain)` | 判断域名是否命中 |
| `LoadBlockList(...)` / `LoadBlockListFromFile(...)` | 从内存/文件加载规则集（黑名单 + 白名单双列表） |
| `AddBlockIP(ip)` / `AddBlockIPRange(range)` / `RemoveBlockIP(ip)` | 精确 IP 与网段拦截（内核态接口预留） |
| `UintToIP(ip)` | uint32 转点分十进制字符串（快速比对辅助） |
| `GetStats()` | 取 `TrafficStats` 统计 |
| `ResetStats()` | 重置统计计数 |
| `SetBlockCallback(cb)` | 注册拦截回调（供通知窗弹窗） |
| `InjectBlockedPacket(pkt)` | 向连接注入 RST 阻断包 |
| `AddWhitelistDomain / RemoveWhitelistDomain / IsWhitelisted` | 白名单增删查 |
| `ShouldBlockIP(ip)` / `IsIPInRange(ip, cidr)` | IP 级决策 |

- 统计结构 `TrafficStats{ totalPackets, blockedPackets, allowedPackets, blockedDomains }` 提供四个计数维度，供 `showblocks` 与 GUI 面板展示拦截成效；
- 拦截回调 `BlockCallback(domain, processName, pid)` 携带触发域名、进程名与 PID，直接驱动 `NotifyWindow` 弹出告警气泡；
- 调试计数：`g_debugPacketCount`、`g_debugTlsCount`、`g_debugBlockedCount`、`g_debugInit` 分别统计包总量、TLS 握手数、拦截数与初始化状态，是测试工程断言的基础。

**内核态原型：`light\新建文件夹\`**

- `SecurityCore.h / .cpp`：暴露 `IsDomainInFixedBlacklist`（固定黑名单检查）、`IsUrlSafe`（URL 安全判定）、`LogBlockedUrl`（拦截日志登记）、`GetRiskDomains`（获取风险域名列表）四个包装函数，`.cpp` 实现体注释声明留在 `light.cpp`——即用户态安全策略在宿主侧实现，驱动侧只做执行；
- `WfpCallout.h / .c`：基于 WFP（Windows Filtering Platform）的 callout 驱动，核心常量：

```c
// WfpCallout.h 关键常量
#define DEVICE_NAME      L"\\Device\\WfpCallout"
#define SYM_LINK_NAME    L"\\DosDevices\\WfpCallout"
#define IOCTL_ADD_BLOCK_IP    0x800   // 添加拦截 IP
#define IOCTL_REMOVE_BLOCK_IP 0x801   // 移除拦截 IP
#define IOCTL_CLEAR_ALL       0x802   // 清空全部
#define IOCTL_GET_STATS       0x803   // 读取统计
#define MAX_IP_LIST_SIZE      1024    // 规则表上限
```

- 驱动函数：`DriverEntry / DriverUnload` 负责加载卸载；`RegisterWfpCallout / UnregisterWfpCallout` 注册/注销 callout；`Classify` 是分类回调（每条出站连接必经）；`IsIpBlockedLocked / AddBlockedIP / RemoveBlockedIP / ClearAllBlockedIPs` 是规则表操作；`HandleDeviceControl` 处理应用层 IOCTL；
- 挂载点：`FWPM_LAYER_ALE_AUTH_CONNECT_V4`（IPv4 出站连接授权层），动作 `FWP_ACTION_CALLOUT_TERMINATING`（终止连接），条件 `FWPM_CONDITION_IP_REMOTE_ADDRESS`，`notifyFn/flowDeleteFn = NULL`——即在连接建立前检查对端 IP，命中则直接终止，**应用层完全感知不到**；
- IO 结构：`BLOCK_IP_ENTRY{ IP, Timeout }`（支持带超时的临时规则）、`BLOCK_IP_LIST`（上限 1024 条）、`STATS_DATA{ TotalBlocked, ActiveRules }`（累计拦截数与活跃规则数）；
- 配套：`WfpCallout.inf` 驱动安装描述文件（用于 `dpinst` 或 `RUNDLL32` 安装），`output.xzyx` 为编译输出产物备查。

两层方案的协同策略：用户态负责**域名级**精细拦截与 UI 反馈（弹窗、统计、白名单）；内核态负责**IP 级**硬拦截（即使应用层被绕过也无法出网）。用户态 `AddBlockIP / AddBlockIPRange / UintToIP` 正是为与内核驱动 IPC 预留的接口。

**驱动注册管线**（`WfpCallout.c` 第 157 行起）：`RegisterWfpCallout` 先 `FwpmEngineOpen` 打开过滤引擎会话，随后构造 `FWPS_CALLOUT0` 结构——`calloutKey` 用内联 GUID 标识，`classifyFn` 指向分类回调 `Classify`，`notifyFn` 与 `flowDeleteFn` 显式置 `NULL`（不做流生命周期跟踪）；`FwpsCalloutRegister` 注册成功后再 `FwpmFilterAdd` 挂过滤条件：`filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4`，可读名 `Light WFP Callout`，动作 `FWP_ACTION_CALLOUT_TERMINATING`——每条 IPv4 出站连接建立时都会经过分类回调，命中黑名单即被终止。若 `FwpmFilterAdd` 失败，代码会立即 `FwpsCalloutUnregisterById` 回滚已注册的 callout，保证失败后不残留半挂载状态；成功则打印 `[WfpCallout] WFP Callout registered successfully`。分类回调里对命中项执行 `DbgPrint` 打出格式串 `[WfpCallout] BLOCKED: IP %d.%d.%d.%d` 的内核调试日志，把拦截事件落进 DbgView 便于联调。`DriverEntry` 负责 `IoCreateDevice` 创建设备对象（对应 `\Device\WfpCallout`）与 `IoCreateSymbolicLink` 建立 DOS 符号链接（`\DosDevices\WfpCallout`）供应用层 `CreateFile` 打开；`HandleDeviceControl` 分发四个 IOCTL——`IOCTL_ADD_BLOCK_IP` 先做 `g_BlockedIPCount >= MAX_IP_LIST_SIZE` 上限检查，满则拒绝新规则并返回错误，避免数组越界；`IOCTL_GET_STATS` 返回 `STATS_DATA{ TotalBlocked, ActiveRules }`，与用户态 `GetStats().blockedDomains` 形成「域名黑白名单 + 内核 IP 规则表」两套统计口径。注入规则时驱动侧 `AddBlockedIP` 需要加锁遍历 `g_BlockedIPs` 数组（`IsIpBlockedLocked` 就是带锁查询），与用户态 `std::unordered_map` 的并发模型相比，这是典型的「定长数组 + 锁」内核风格。

### NetworkInterceptor 深度解析

### NetworkInterceptor 深度解析

本节依据 `NetworkInterceptor.cpp` 第 125–2202 行的函数实现逐段还原拦截引擎的工作机制，包含流重组、协议识别、进程归属、规则匹配与 RST 注入五条主线。

**1. TCP 流重组与域名提取**

- `CreateStreamKey(125)`：由五元组（源/目的 IP、源/目的端口、协议号）生成流键，用于在 `unordered_map` 中索引半开连接上下文；
- `ProcessTcpSegment(186)`：按 TCP 顺序号把分散段拼装、检查首段是否携带客户端问候（ClientHello），并判定当前载荷归属哪条流；
- `ExtractSNIFromStream(458)`：在完整流缓冲中定位 TLS ClientHello 的 Server Name Indication 扩展，逐字节解析 `server_name` 列表，得到目标主机名；
- `ExtractHTTPHostFromStream(624)`：对无 TLS 的明文 HTTP 流，在缓冲中查找 `Host:` 头并截取值；
- `CleanupExpiredStreams(166)`：定时清除超过存活周期的流上下文，防止内存无限增长。

**2. DNS 请求解析**

`ExtractDNSDomain(1084)` 是域名来源的第一路（未加密 DNS 查询）：

- 仅处理 IPv4/UDP 且载荷长度 ≥ 12 字节的报文（DNS 头最小长度）；
- `queryId = ntohs(dns[0..1])` 取事务 ID；`flags & 0x8000` 判断为响应包则跳过（只拦查询）；
- `questionCount == 0` 直接返回 false（无问题节）；
- 从第 12 字节开始逐 label 解析 QNAME：拒绝 `0xC0` 压缩指针（防解引用攻击），label 长度超过 63 即截断，总域名长度上限 253，遇 0 终止；
- 解析结果过 `NormalizeDomain` 归一化（小写 + 去空白）后交给规则匹配。

真正的强制兜底发生在 `ProcessPacket` 返回域名之后：若 `ExtractDNSDomain` 成功命中黑名单，网关直接伪造一条 **DNS 应答**注入给请求方——`SendDNSResponseToRequester(1437)` 构造 `NXDOMAIN`/`0.0.0.0` 应答使解析失败；`SendDNSResponse(1549)` 是通用应答发送原语。这一路在 **DNS 层**就终结了恶意域名，比等待 TCP 连接再 RST 更早。

**3. 逐包识别（legacy 通道）**

对无法完整重组流的场景，`ProcessPacket(944)` 内联解析：

- `ExtractHTTPHost(1183)`：单包内直接扫描 `Host:` 头（明文 HTTP）；
- `ExtractTLSSNI(1264)`：单包内扫描 TLS 记录与 SNI（快速路径）；
- 解析失败才走上文的流缓存路径，两路互为补充。

**4. 进程归属**

`GetPacketProcessId(1664)` 通过 WinDivert 的 `WINDIVERT_LAYER_NETWORK` 附带信息查询该端口的 PID，`GetProcessName(1716)` 再由 PID 解析进程名；`UpdatePidCache(722)` / `GetPidFromCache(797)` 维护 PID 缓存避免频繁查表。域名命中后，拦截回调携带真实进程名与 PID，通知窗得以显示"哪个程序在访问被拦域名"。

**5. 规则匹配与注入**

- `BlockDomain(1751) / BlockDomainForProcess(1791) / UnblockDomain(1821) / ClearAllBlocks(1835)`：黑名单增删清；
- `IsBlocked(1847) / IsDomainInBlockList(1878) / MatchDomainPattern(1926)`：精确/通配/子域匹配三种模式；
- `LoadBlockListFromFile(1979)`：从磁盘文件批量装载（`whitelist.json` 亦在此装载路径）；
- `AddWhitelistDomain(2065) / RemoveWhitelistDomain(2080) / IsWhitelisted(2092)`：白名单优先于黑名单；
- `AddBlockIP(2108) / AddBlockIPRange(2116) / RemoveBlockIP(2125) / ShouldBlockIP(2132) / IsIPInRange(2150) / UintToIP(2176)`：IP 级规则（CIDR 范围判定），供内核态驱动同步使用；
- `SendSingleRST(1559) / SendTCPReset(1604)`：构造并注入 TCP RST 段；`InjectBlockedPacket` 是 WinDivert 注入入口；
- `GetStats(2000) / ResetStats(2007) / StatsThread(2015)`：统计与周期刷新线程；
- `CleanCache(2055)`：综合清理流缓存、IP 缓存与 PID 缓存。

**6. 抓包主循环**

`Start(851)` 的过滤串为：

```cpp
"outbound && !impostor && ip.DstAddr != 127.0.0.1 && ip.DstAddr != 0.0.0.0 "
"&& (tcp || udp.PayloadLength > 0)"
```

`WinDivertOpen(filter, WINDIVERT_LAYER_NETWORK, 0, 0)` 打开句柄后创建 `interceptorThread` 与 `statsThread` 两个线程。`InterceptorThread(909)` 以 0xFFFF 字节缓冲循环 `WinDivertRecv`，每收到一包 `g_debugPacketCount++` 后交 `ProcessPacket` 处理：解析出目标域名字符串 → `IsBlocked` 命中 → `InjectBlockedPacket` 注入 RST 并 `g_debugBlockedCount++`，未命中则放行，`GetStats().allowedPackets` 计数。`Stop(885)` 执行 `WinDivertClose` → 线程 `join` → `ClearAllBlocks()`，保证退出后不残留任何规则。### 插件系统

light 以插件为核心扩展机制，分四层：

**1. C 导出 API（`LightApi.h` / `LightApi.cpp` / `LightApi.def`）**

对外导出 5 个 C 函数，供同进程插件 DLL 通过 `GetModuleHandle(NULL) + GetProcAddress` 动态调用——不强制插件链接导入库，解耦宿主版本：

```cpp
// LightApi.h 导出清单
LightApi_Init();                    // 初始化宿主 API 层
LightApi_Shutdown();                // 关停 API 层
LightApi_HttpRequest(...);          // 同步 HTTP 请求（WinHTTP）
LightApi_HttpRequestStream(...);    // 流式 HTTP 请求（大响应）
LightApi_Free(...);                 // 释放响应内存
```

HTTP 层基于 WinHTTP 实现，默认超时 30000ms；`LightApi.def` 显式声明导出符号，保证符号名稳定。`LightApi.cpp` 共 218 行，保持极薄封装：参数校验 → WinHTTP 调用 → 内存管理，把第三方实现的复杂度全部隔离在宿主内。

### LightApi WinHTTP 桥的内部构造

`LightApi.cpp` 的 HTTP 层虽然只有 218 行，内部设计却层次分明：`Utf8ToWide` 负责把调用方传入的 UTF-8 字符串转换为宽字符；`ParseUrl` 使用 `WinHttpCrackUrl` 解析 URL 的协议、主机、端口与路径（主机缓冲 256 字符、路径缓冲 4096 字符，SOCKS/HTTP/HTTPS 由 `nScheme` 区分），解析失败返回 `-3`；核心执行器 `DoRequest` 是同步与流式两个入口共享的公共路径：参数校验失败依次返回 `-1`（空 URL）与 `-2`（宽字符转换失败），会话句柄 `g_session` 由 `WinHttpOpen(L"LightApi/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, ...)` 惰性创建（失败 `-4`），`WinHttpConnect` 失败 `-5`，`WinHttpOpenRequest` 失败 `-6`——负号返回值让插件侧无需区分 errno 即可快速定位失败阶段。超时通过 `WinHttpSetTimeouts(hReq, timeout, timeout, timeout, timeout)` 五段统一设置，默认 `kDefaultTimeoutMs = 30000` 毫秒；请求头由调用方以 `\r\n` 分隔的原始字符串传入，无额外头时使用 `WINHTTP_NO_ADDITIONAL_HEADERS`；响应体按 `bodyOut` 是否为 `NULL` 决定一次性缓冲或流式分块回调 `LightApiChunkFn`——大响应走流式出口避免整包驻留内存。`LightApi_Init / LightApi_Shutdown` 管理会话生命周期，同进程插件 DLL 通过 `GetProcAddress` 动态绑定这五个符号，不链接导入库，宿主升级时插件二进制无需重编。



**2. 插件管理器（`PluginManager.h` / `PluginManager.cpp`，1265 行）**

- 数据结构 `PluginRecord{ id, name, version, author, description, path, isApiPlugin, isGuiPlugin, isFromZip, enabled }`：一条记录描述一个插件，区分 API 插件与 GUI 插件、原生目录插件与 zip 插件；
- 扫描结果 `ApiPluginScanResult{ safe, signedPlugin, reason, dependencies, localDependencies }`：含安全标记、签名校验、依赖解析——加载前先做依赖分析，缺依赖的插件给出 `reason`；
- 生命周期方法：`LoadPlugin` / `LoadPluginFromZip`（支持 zip 内加载）/ `UnloadPlugin` / `ReloadPlugin`（单插件热重载）/ `ReloadAllPlugins` / `LoadPluginsFromDirectory(WithUI)` / `LoadApiPluginsFromDirectory`；
- 插件加载回调 `PluginLoadCallback` 向 GUI 汇报加载进度，支持"加载中动画 + 错误提示

### PluginManager 的 PE 安全检查链

除生命周期管理外，`PluginManager.cpp` 还内置一条针对 DLL 插件的静态安全检查链，全部基于纯解析实现，不依赖杀毒引擎：`ReadWholeFile` 把 DLL 读入内存缓冲区；`RvaToOffset` 实现 RVA（相对虚拟地址）到文件偏移的换算，供后续读取导入表；`ReadPeImports` 解析 PE 的 Import Directory，提取插件依赖的全部外部 DLL 名；`IsSignedTrusted` 校验数字签名信任链；`IsDangerousImport` 维护一张内置黑名单 `kBlocked[]`（覆盖 `createre` 前缀等创建进程/写自启动项的危险 API 族），导入表中出现黑名单符号即判定危险；`ImportTableDangerous` 汇总签名与导入双重结论，给出 `reason` 说明。加载前还会执行依赖预解析：`FindLocalDependency` 在插件目录内查找缺失的本地依赖，`ScanRecursive` 以 `visited` 集合防环深度遍历传递依赖并把结果写入 `ApiPluginScanResult::localDependencies`，`PreloadLocalDependencies` 按依赖顺序预加载，避免运行时因依赖未就绪而崩溃。配合 `IsDllAlreadyLoaded` 的去重检查与 `ExtractZipToTemp`（zip 插件先解压到临时目录再加载，`GetTempPluginDir` 用插件 ID 命名、`CleanupTempDirs` 卸载后清理），形成"解包 → 静态体检 → 依赖解析 → 预加载 → 注册"的完整安全加载链。加载回调 `PluginLoadCallback` 在链路的每个关键节点触发，GUI 侧据此渲染"加载中动画 / 成功 / 错误"三种状态。

"的界面反馈。

**3. 插件接口（`PluginInterface.h`）**——C++ 虚接口，定义插件必须实现的能力：

```cpp
struct IPlugin {
    virtual PluginInfo GetInfo() = 0;              // 元信息
    virtual bool Initialize() = 0;                 // 初始化
    virtual void Shutdown() = 0;                   // 关停
    virtual std::vector<PluginCommand> GetCommands() = 0; // 命令清单
    virtual bool OnLoad() = 0;                     // 加载回调
    virtual void OnUnload() = 0;                   // 卸载回调
    virtual bool OnCommand(const std::string& cmd, ...) = 0; // 命令执行
    virtual void SetUIManager(PluginUIManager* mgr) = 0;     // 注入 UI 管理器
};

struct IGuiProvider {
    LayoutType GetLayoutType();    // Custom / Tabbed / Simple 三种布局
    std::wstring GetWindowTitle();
    std::pair<int,int> GetWindowSize(); // 默认 900x600
};
```

`IPlugin` 让任何插件都能"注册命令、接收命令"；`IGuiProvider` 定义 GUI 插件与宿主窗口的契约——宿主按 `LayoutType` 决定以标签页（Tabbed）、简单面板（Simple）还是完全自定义（Custom）方式托管插件窗口。

**4. 扩展 ABI（`YXExtension.h`，版本 `YXG_API_VERSION = 1`）**

外部 DLL 通过导出 `YXG_RegisterPlugin` / `YXG_UnregisterPlugin` 与宿主对接，核心类型包括 `YXG_PluginContext`（插件上下文）、`YXG_JSValue`（带类型 tag 的 JS 值）、`YXG_ControlDesc`（控件描述）、`YXG_HttpRequest`（HTTP 请求描述）。约定：扩展 DLL 统一放入 `plugins\` 目录，导出符号建议加 `YXG_` 前缀避免冲突。该 ABI 被 VCBlocks、MCLauncher 等 GUI 插件共同使用（它们各自内嵌一份 `include/YXExtension.h`）。

`ExtensionManager`（380 行）是 ABI 层的运行时管家：`ScanAndLoad` / `ScanAndLoadGuiDll` 扫描目录识别扩展；`RegisterHook / UnregisterHook / RunHooks / HasHooks` 管理钩子注册表（宿主事件 → 扩展回调）；`JSApiRecord` 将扩展暴露的 JS API 登记入 `ApiSlot`；`NativePanelRecord` 登记原生面板扩展。

### GUI 插件体系

GUI 体系位于 `light\light\Gui\` 目录，是 light 中代码量最大的子系统（合计约 1.9 万行），以 WebView2 为渲染宿主、QuickJS 为脚本引擎、原生命令为能力后端，构成"原生 C++ 宿主 + JS 插件 + HTML 页面"的完整 AI 对话式界面。

**组件清单与职责**

| 文件 | 行数 | 职责 |
| --- | --- | --- |
| `YXGuiPlugin.cpp` | 7886 | GUI 宿主插件：整合全部子系统；定义控件 ID 段（`IDC_HELP_EDIT=1001` 起、AI 区 `IDC_AI_BASE=2000`、扩展控件 `3000+`、设置控件 `4000+`） |
| `JSPlugin.cpp` | 4459 | JS 插件运行时：QuickJS 上下文、`HttpResult`、`WM_YXGUI_HTTP_DONE = WM_USER+731` 消息、`ApiSlot{ns,name,kind,origFn}` |
| `ApiAiNative.cpp` | 3900 | `com.yx.api_ai` 的原生 C++ 实现（不再创建 QuickJS、不 Eval `main.js`），提供 `renderMain / renderChat / renderWall / renderAbout` 四个页面渲染 |
| `GuiDllStore.cpp` | 1955 | 插件商店：卡片网格 + 搜索 + 加载动画 + 一键下载 |
| `PluginCenter.cpp` | 626 | 插件中心面板：`PluginCenterItem` 四类（内置功能 / JS 扩展 / API 指令集 / GUI 插件），自绘卡片 + 滚动条 |
| `ExtensionManager.cpp` | 380 | 扩展扫描与钩子管理（见插件系统一节） |
| `PermissionManager.cpp` | 256 | 权限授权单例 |

**权限管理（`PermissionManager.h`）**

单例设计，带本地持久化：`Load / Save`、`IsPermanentlyGranted` / `SetPermanentlyGranted` / `Revoke` / `ListGranted`。敏感操作（HTTP 请求、文件访问、拦截规则修改）弹窗询问，`Prompt` 返回三态：`0=拒绝`、`1=仅本次`、`2=记住（永久授权）`。授权记录持久化后，下次同操作直接放行——这是"JS 插件可审计、可撤销"的安全底座。

**插件中心与商店**

- `PluginCenterPanel`：以 `Toggle/Action` 回调驱动自绘卡片 + 自定义滚动条，卡片按四类分组展示（内置功能 / JS 扩展 / API 指令集 / GUI 插件），支持启用/停用开关；
- `GuiDllStore`：下载来源三种 `GuiDllRepo`：`github`（远程仓库）/ `local`（本地目录）/ `http list.txt`（HTTP 列表文件）；`GuiDllPluginHost` 管理 `Cache\gui\dll` 目录，扩展名 `.dll.N` 表示**禁用**（N 为数字序号），伴生文件 `.txt / .png / .jpg / .mp4` 提供插件说明、封面图与演示视频——每个 GUI 插件是一个"DLL + 元数据 + 媒体"的复合包；插件商店 UI 提供卡片网格、搜索框与加载动画，一键下载后自动注册到插件中心。

**商店项与仓库的数据模型**（`GuiDllStore.h` 第 31–55 行）逐字段定义了插件包的结构：`GuiDllStoreItem{ name（插件显示名 = DLL 文件名去扩展名）, description（同名 .txt 内容）, dllUrl, txtUrl, logoUrl, videoUrl, logoPath（本地已缓存 logo 路径，用于绘制）, videoPath, installed, enabled, installing }`——一个卡片项完整携带"程序体 + 说明 + 封面 + 演示视频"四元组，任意媒体缺失都可在 UI 层优雅降级。`GuiDllRepo{ display, type（0=github / 1=本地目录 / 2=http 基址 list.txt）, owner, repo, branch, path }` 区分三路来源：github 型用 `owner/repo/branch` 定位，本地型直接给目录路径，HTTP 型给 `list.txt` 基址。`GuiDllPluginHost` 的核心操作：`DllDir()` 返回 `Cache\gui\dll` 绝对路径（目录不存在时自动创建）；`LoadAll()` 扫描并加载所有 `*.dll`（跳过 `.dll.N`）；`SetEnabled(name, enabled)` 通过对 DLL 文件**改名**（`.dll` ↔ `.dll.N`）实现启用/停用——零写入、零注册表，重启后状态天然保持；`Description(name)` 读取同名 `.txt` 作为介绍；`RunHooks(YXG_HookType, pluginId, in, out)` 派发宿主事件到插件；`HasJSApi(ns, name)` 供 JS 层探测插件是否暴露某 API。

**插件中心面板**（`PluginCenter.h`）以 `PluginCenterItem{ id, name, 类别, enabled, builtin, canToggle, canOpenFolder, canReload }` 为数据单元——`builtin` 区分内置功能与外部插件，`canToggle / canOpenFolder / canReload` 三位标志位控制卡片上的操作按钮。`PluginCenterPanel` 提供 `Create / Destroy / SetItems / Show / Refresh` 生命周期方法，以及 `SetToggleCallback / SetActionCallback` 两个回调注入点（开关切换 → `onToggle`，打开文件夹/重载 → `onAction`）；自绘管线为 `Paint → DrawCard → DrawScrollbar` 三段，`ClampScroll / UpdateScrollInfo / InvalidateAll` 支撑自定义滚动条，`NotifyToggle / NotifyAction` 由点击坐标反查卡片索引并触发回调——整套面板不依赖任何现成控件库，全部 GDI 自绘。

**构建注意事项**：`GuiDllStore.h` 前置声明 WebView2 类型；`YXGuiPlugin.h` 通过 `JSPlugin / PermissionManager / JSWebViewBridge / PluginCenter / GuiDllStore` 五路整合；仓库内附 `BUILTIN_GUI_INTEGRATION.md`、`COMPILE_FIX_V4.md`、`QUICKJS_MSVC_FIX.md` 三份文档记录了 GUI 集成、编译修复与 QuickJS 在 MSVC 下的适配过程（QuickJS 官方 Makefile 面向 GCC，MSVC 下需修 `__declspec`、内联汇编与正则/Unicode 模块的编译选项）。

### ExtensionManager 扩展装载器

`Gui/ExtensionManager.cpp`（380 行）是 YXGuiPlugin v3.1 的扩展装载器：负责扫描、加载、卸载与调度同级目录下 的 `YXG_*.dll` 扩展插件，是「宿主-扩展」ABI 的落地执行者。

**扫描装载**：`ScanAndLoad` 用 `FindFirstFileW/FindNextFileW` 匹配 `YXG_*.dll` 通配符并逐项 `LoadOne`；另一入口 `ScanAndLoadGuiDll` 面向 GUI 插件目录，匹配 `*.dll` 后**严格校验扩展名**——用 `_wcsicmp` 比较文件名末 4 字符必须等于 `.dll`，从而跳过 `name.dll.N` 这类禁用后缀（`N` 为数字序号）——同一约定与 `GuiDllStore` 的「改名即禁用」机制完全一致，两个子系统共享一套插件停用哲学。`LoadOne` 内部流程：`LoadLibraryW` 载入 DLL → `GetProcAddress` 依次取 `YXG_RegisterPlugin / YXG_UnregisterPlugin / YXG_GetExtensionInfo` 三个导出 → `BuildApiTable` 组装 `YXG_ExtensionApi` 结构体 → 调用 `reg(&api, YXG_API_VERSION, &ctx)` 取得插件上下文；任一环节失败即 `FreeLibrary` 释放并放弃该插件；成功则把句柄、上下文与注册/注销函数存入 `ExtDllRecord`，并调用 `info()` 读取插件名与版本，通过 `OutputDebugStringA` 打印 `[YXExtension] Loaded: <name> v<version>` 调试信息。

**钩子调度**：`RegisterHook` 把 `{callback, ctx, priority}` 存入对应 `YXG_HookType` 的链表中后立刻按优先级降序 `std::sort`，保证高优先级插件先收到事件；`RunHooks` 沿链顺序调用，**任一回调返回非零即短路停止**，实现「谁处理谁消费」的拦截语义；`UnregisterHook` 用 `remove_if` 精确按 `ctx+callback` 对移除，杜绝误删。

**JS API 注册表**：`RegisterJSApi` 以 `(namespace, name)` 为键去重——同键已存在则覆盖（后注册优先），否则追加；`HasJSApi` 供 JS 层在调用前探测可用性，`CallJSApi` 找到匹配项后透传 `argc/argv` 并回填 `ret`，`GetJSApiNames / GetNamespaces` 用去重遍历输出 API 清单——这让页面端可以动态发现扩展能力，构成「插件注册 API → 网页查询 API → 网页调用 API」的完整链路。

**原生面板**：`RegisterNativePanel` 校验 `YXG_PanelInfo` 的 id/name/create 三者非空后登记，同 id 覆盖、后注册优先（与 JS API 覆盖语义一致），`GetNativePanels` 输出全部面板记录供宿主在主窗口侧边栏挂载。

**JS 值紧凑编码**：`YXG_JSValue` 用两个 64 位 `bits` 字段打包：`bits[0]` 低 8 位存 tag（`YXG_JS_TAG_*`），其余 56 位与 `bits[1]` 低 8 位拼成 64 位值（整数原值 / bool 0|1 / 字符串指针），`bits[1]` 其余位恒 0 保留；`ExtValueOf / ExtSetValue` 负责编解码，`JSValueGetTagImpl` 对越界 tag 一律归一为 `UNDEFINED`，防御非法内存布局。`BuildApiTable` 一次性把钩子、JS API、原生面板、日志（`LogInfo/LogError` 走 `OutputDebugStringW`）、宿主信息（`GetPluginsDir`、`GetHostVersion` 返回 `3.1.0`）等 20 余个函数指针注入 `YXG_ExtensionApi`，插件侧只与这张表交互，不直接触碰宿主内部对象——ABI 边界清晰，扩展与宿主可分别独立升级。`UnloadAll` 按逆序先调 `unregFn` 通知插件释放资源、再 `FreeLibrary` 卸载模块、最后清空 hooks/JS API/原生面板三张表，卸载路径与加载路径完全对称，插件重复加载或宿主退出都不会残留句柄。

### YXGuiPlugin 宿主插件全景

`YXGuiPlugin.cpp`（7886 行）是内置 GUI 提供的最大单体文件，至少包含以下方法组（按 `Gui/YXGuiPlugin.cpp` 实际符号清单归纳）：

**配置与持久化**：`GetConfigFilePath`（定位配置文件）、`ResetConfigToDefault`（恢复默认）、`LoadConfig` / `SaveConfig` / `ApplyConfig`（读/写/应用，三者成对出现保证配置变更即时生效）。

**帮助与密码体系**：常量 `HELP_PASSWORD = "sr5r4g464dry8k78665sglnjtr4m5z578tk78q"` 与 `HELP_REGISTRY_RELATIVE = "\\rppcst\\sys.xayx"` 表明帮助文本经注册表加密档保护：`Help_Xor` 解密、`Help_ReadRegistryFile` 读取 `sys.xayx`、`Help_GetHelpText` 取正文、`Help_Populate` 填充到帮助编辑框——帮助内容不落明文，与 XZYX 加密族一脉相承。

**多标签工作区**：`SetupTabEvents` 初始化标签事件；`AddNewTab` / `SwitchToTab` / `CloseTab` 管理标签生命周期；`ResizeAllTabs` 在窗口尺寸变化时重排；`UpdateTabBar` 重绘标签条；`HitTestTabBar` 命中测试；`TabBarWndProc` 标签条窗口过程（自绘交互）。

**网络请求桥**：`RequestDownloadViaMainProgram` 把页面下载请求转交给主程序 `DownloadFileWithProgress`，`OnDownloadCompleted` 完成后回调通知页面——下载逻辑与 UI 线程解耦。

**文件与路径工具**：`GetExeDir`（程序目录）、`GetCacheSubDir`（缓存子目录）、`AnsiToWide` / `WideToUtf8`（编码转换，跨 GBK/UTF-8 边界）。

**扩展系统整合**：`ExtListSubclassProc` 子类化扩展列表控件；`ExtractExtIdFromUrl` 从网页 URL 提取扩展 ID；`ScanExtensions` / `ParseExtensionManifest` / `LoadAllExtensions` 扫描并解析扩展清单；`CreateSidebar / LayoutSidebar / RefreshSidebar / SidebarWndProc / HitTestSidebar` 实现侧边栏；`ShowExtensionManager` 打开扩展管理面板。

**输入与控件**：`YXGuiInputDialogProc / ShowInputDialog` 模态输入框；控件 ID 区段划分清晰：`NATIVE_PANEL_BASE=1000` 起为原生面板、`IDC_HELP_EDIT=1001` 起为帮助区、`IDC_AI_BASE=2000` 为 AI 对话区、`3000+` 为扩展控件、`4000+` 为设置控件。

**CRX 资源容器**：`CrxStripper_WriteZip` 处理 CRX 格式资源（剥壳后写 zip），`WaitForManifest` 等待 manifest 解析完成，`JsonExtractString / JsonExtractStringArray` JSON 辅助读取。

**窗口管理**：`Initialize / Shutdown` 生命周期对、`GetWindowTitle / GetWindowSize`（`IGuiProvider` 契约实现）、`FormatCdDisplay`（格式化当前目录显示）、`LayoutCdAndInput`（重排目录栏与输入框）、`UpdatePrompt`（更新提示符）、`CleanupGdiResources`（GDI 资源释放，防句柄泄漏）。

### JSPlugin 运行时全景

### ApiAiNative 原生 AI 页面

`ApiAiNative.cpp`（3900 行）实现 `com.yx.api_ai` 的**原生 C++ 版本**：与早期"QuickJS + `main.js`"的路线不同，该实现**不再创建 QuickJS 上下文、不 Eval 任何 JS 脚本**，四个页面（`renderMain / renderChat / renderWall / renderAbout`）全部由原生控件拼装渲染，性能与稳定性都更可控。核心类型 `ApiAiNativePlugin` 的方法集：

- **生命周期**：`CallOnLoad`（加载时初始化资源）、`CallOnViewShow(hostContainer, w, h)`（视图显示时按容器尺寸 `renderMain()` 首绘）、`CallOnViewHide`、`CallOnResize(w, h)`（尺寸变化重绘）、`CallOnUnload`（卸载与内存回收）；
- **模式切换**：`CallOnControlEvent(controlId, code)` 响应控件事件，`mode_api` 与 `mode_web` 两个控件 ID 对应 AI 模式与网页模式（`m_mode = 0 / 1`），切换后立即 `renderMain()` 重建界面；其余事件（按钮、输入框等）同样以控件 ID 分派到对应处理；
- **富文本**：`HandleRichLinkTag(tag)` 解析富文本链接标签，把页面中的超链接/代码片断翻译成原生控件动作；
- **异步窗口**：`EnsureAsyncWnd()` 惰性创建隐藏异步窗口，`AsyncWndProcStatic / AsyncWndProc`（static 转发 + 实例处理两段）承接后台线程投递的消息——网络请求完成、定时任务到期等异步事件经此窗口回流主线程；
- **HTTP 回注**：`CallHttpDone(seq, JSPlugin::HttpResult&)` 按序号把 `HttpResult` 分发给挂起的请求者，与 JS 侧 `WM_YXGUI_HTTP_DONE` 消息机制共用同一套序号约定；
- **原生控件工厂**：`MakeLabel(id, text, ...)`、`MakePanel(id, x, y, w, h, ...)` 直接创建 Win32 静态文本与面板——render 系列页面由这两个原语拼装成完整 UI 树，不经过 HTML/CSS 渲染管线。

这一设计的意义在于：AI 对话页作为最高频、最强调交互一致的界面，直接用原生控件实现后，不再受 WebView2 加载延迟与 JS 运行时故障影响；其余低频率页面（如插件的扩展页）仍走 WebView2 路线，形成"高频页面原生化、低频页面网页化"的折中架构。

### JSPlugin 运行时全景

`JSPlugin.cpp`（4459 行）承载 QuickJS 插件运行时，方法覆盖"装载 → 桥接 → 事件 → 生命周期"全链路：

- **装载**：`ParseManifestJson(176)` 解析插件描述文件；`ReadManifest(192)` 读取；`InitRuntimeAndApis(215)` 创建 QuickJS 运行时并注入宿主 API；`Load(273)` 目录加载；`LoadBuiltin(383)` 内置插件（manifest + 入口脚本）；`Unload(413)` 卸载；`SetMetaInfo(303)` 注入元信息；
- **扩展**：`ScanAndLoadExtensions(449)` 扫描外部扩展目录；`UnloadExtensions(462)` 卸载扩展；`GetExtensionApi(468)` 取扩展 API；`RunHooks(475)` 触发钩子；`IsJSApiOverridden(486)` 判断 API 是否被扩展覆盖；`CallExtensionJSApi(492)` 调用扩展 JS API；
- **控件与样式**：`SetGlassPad(812)`（毛玻璃面板）、`AddImagePath(1113)`（图片路径注册）、`RememberControlType(1509) / CtrlTypeOf(1512)`（控件类型记忆）、`GetCtrlColor / SetCtrlColor / IsCtrlHover / SetCtrlHover`（控件调色与悬停态，`CtrlColor{ bg, fg, border, active, tint, swatchColor }` 六字段）、`BrushFor(1532) / FlatFont(1539)`（画刷与扁平字体）、`ClearControlStyles(1553)`（样式清空）；
- **壁纸**：`ApplyWallpaper(1567)`（应用壁纸，模式枚举 `none / solid / image / cycle / video / lively`）、`RefreshCycleFiles(1626) / CycleNext(1677)`（轮播刷新与切换）、`ClearWallpaperCache(1683)`、`AcquireParentBgSnapshot(1693) / ClearBgCache(1719)`（父窗口背景快照）；
- **脚本执行**：`EvalFile(1728) / EvalString(1738)`、`CallGlobalFunction(1785)`；
- **页面生命周期回调**：`CallOnLoad(1817) / CallOnViewShow(1823) / CallOnViewHide(1851) / CallOnResize(1856) / CallOnUnload(1874)`——宿主窗口显隐与缩放均同步通知 JS；
- **权限与异步**：`RequestPermission(1879)` 弹授权窗；`CallHttpDone(1927)` 完成 HTTP 回调；**关键机制**：HTTP 完成经 `HttpTaskDoneMsg`（`WM_YXGUI_HTTP_DONE = WM_USER+731`）`PostMessage` 回主线程后再 `resolve` Promise——保证 JS 侧 `await` 语义正确且不阻塞 UI 线程；`PendingHttpPromise` 保存挂起 Promise；
- **原生控件**：`CallOnControlEvent(1973)` 控件事件回调；`CreateNativeControl(2065)` 创建原生控件（`MutableControls` 可变控件列表供 JS 遍历）。

### 通知窗口

`NotifyWindow.cpp`（392 行）+ `NotifyWindow.h` 实现系统级拦截通知：`NotificationMsg` 结构携带域名、进程名、PID 与时间；`ShowBlockNotification(Ex)` 弹出气泡；`SetBatchMode` 开启批处理模式（同批多次拦截合并为一条展示，避免刷屏）；`SetDisplayDuration` 控制显示时长；滑动动画出场；独立消息线程保证弹窗不阻塞主 UI 与抓包循环。拦截回调 `BlockCallback` 即在此触发弹窗，用户可直接看到"哪个进程访问了哪个被拦域名"。宿主初始化时 `SetDisplayDuration(3000)`、`SetMaxQueueSize(10)`，队列溢出时合并展示。

### 通知窗口的自绘与动画细节

`NotifyWindow.cpp` 的窗口创建有讲究：窗口类名为 `LightNotifyWindow`，图标取系统 `IDI_SHIELD`（盾牌），样式 `WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE`——工具窗不进任务栏、置顶不被覆盖、分层窗口支持透明、不抢焦点；窗口尺寸固定 340×80，默认起点在屏幕右缘外（`slideStartX = screenWidth + 10`），目标位置是右下角（`slideEndX = screenWidth - 340`、`slideEndY = screenHeight - 130`）。动画由两个定时器驱动：`WM_TIMER` 的 `wParam == 1` 启动滑出、`wParam == 2` 每 16ms（约 60fps）执行 `UpdateAnimation`；滑入阶段每帧把窗口左移 `SLIDE_SPEED = 15` 像素，同时用 `SetLayeredWindowAttributes` 把透明度从 0 渐升到 220，抵达目标点后切换为显示时长定时器（默认 `displayDuration = 3000`）。`WM_PAINT` 走双缓冲：先在内存 DC 绘制深色背景（`RGB(30,30,35)`）、左侧 4 像素红色警示条（`RGB(255,60,60)`）、1 像素描边，再以 SimSun 16 号字体绘制"安全拦截"标题与域名正文，最后 `BitBlt` 一次上屏——全程无闪烁。窗口不响应点击聚焦：`WM_NCHITTEST` 固定返回 `HTCAPTION`，左右键按下即滑出隐藏；`StringToWide` 用 `MultiByteToWideChar(CP_ACP)` 处理 GBK 边界编码。线程模型上，独立消息线程在构造时启动，`PostMessageW` 投递通知消息，`GetInstance` 单例通过 `g_NotifyWindow` 全局指针暴露给拦截回调，拦截事件经 `ShowBlockNotificationEx(processName, domain, ipAddress, port)` 四次元消息入队——抓包主循环、UI 线程与通知窗口互不阻塞。

### 插件模板生成

`PluginTemplateGenerator.h / .cpp`（895 行）为开发者提供脚手架能力：`GenerateTemplate` / `GenerateApiTemplate` 可生成 `plugin.json`（描述文件）、C++ 源码（`.cpp/.h`）、`CMakeLists.txt`、`README.md`、构建脚本与资源文件，再通过 `PackToZip` 一步打成 zip 交给 `LoadPluginFromZip` 加载——从模板到可插拔插件只需 `light createplugin <名称>` 一条命令，极大降低插件开发门槛。

### 插件模板生成器的内部管线

`PluginTemplateGenerator.cpp`（895 行）把"生成一个可编译插件"拆成一组可独立重用的子步骤：`GenerateTemplate` 是普通插件入口，`CreateDirectoryStructure` 先按 `baseDir` 递归建目录骨架，随后依次调用 `GeneratePluginJson`（写入 `plugin.json`，用 `std::strftime("%Y-%m-%d")` 生成当日日期戳后才写盘）、`GeneratePluginCpp`、`GeneratePluginH`、`GenerateCMakeLists`（为 MSVC 与 MinGW 对齐生成 `CMakeLists.txt`）、`GenerateReadme`、`GenerateBuildScript`（产出 `.bat/.ps1` 与 `.gitignore`，后者保证模板仓库不提交中间产物）、`GenerateResourceFiles`（默认配置文件与数据文件）。API 插件入口 `GenerateApiTemplate` 则输出另一套骨架：`include/PluginInterface.h` 以二进制模式写出接口声明（含 `std::function<void(const std::vector<std::string>&)>` 回调型命令槽），`src/<dllName>.cpp` 提供 DLL 入口与命令注册，根目录附 `CMakeLists.txt`、`build.bat` 与 `README.md`。最后 `PackToZip` 走 minizip 路径：`zipOpen` → 递归 `fs::directory_iterator` 收集文件 → 用 `fs::relative` 计算相对路径、把 `/` 统一替换为 `\` 保持 Windows 语义 → `zipOpenNewFileInZip` 以 `Z_DEFLATED` 逐个写入 → 结束时用 `fs::file_size` 打印产物大小。整套管线输出即 `LoadPluginFromZip` 的输入，构成"生成 → 打包 → 加载"的闭环，开发者从零到可调试插件只差 `light createplugin` 一条命令。

### 嵌入引擎与第三方依赖

light 全量内嵌 **QuickJS** 引擎（`quickjs.c/h`、`libregexp`、`libunicode`、`dtoa`、`cutils`、`builtin-iterator-zip` 等），保证 JS 能力零外部动态依赖；压缩侧内嵌 LZMA SDK（`lzma/C`）；网络侧依赖 WinDivert；GUI 侧依赖 `WebView2LoaderStatic.lib`（约 10.7MB 静态链接库）；`light.slnx` 定义 `x64/x86` 双平台，主工程为 `light/light.vcxproj`。`pragma comment(lib, ...)` 汇总链接 zlib、minizip、`WinDivert.lib`、`iphlpapi`、`psapi`。`light.rc` 引用 `cmd.ico` 作为程序图标。`_link_objs.txt` 记录链接对象清单，配合 `build_one.ps1` 完成一键构建。

### 测试工程

`tests\NetworkInterceptorTests\` 为拦截器的独立测试工程（`NetworkInterceptorTests.cpp` + `.vcxproj` 工程文件）：可直接对 `BlockDomain / IsBlocked / UnblockDomain / LoadBlockList / IPToUint / AddBlockIPRange` 等核心逻辑做自动化断言，与主程序共享同一份 `NetworkInterceptor.h` 接口，保证被测代码与发布代码完全一致。测试配合 `g_debug*` 调试计数验证抓包循环的包分流行为。测试工程独立成 `.vcxproj`，不参与主程序链接，可脱离 GUI 单独在 CI 中运行。`NetworkInterceptorTests.cpp` 的实测主流程展示了一条完整的冒烟链路：

```cpp
// NetworkInterceptorTests.cpp（真实断言内容）
auto interceptor = NetworkInterceptor::GetInstance();
if (!interceptor->Start()) { std::cout << "Start failed" << std::endl; return 1; }
interceptor->BlockDomain("example.com", 0);              // 添加临时拦截规则
std::this_thread::sleep_for(std::chrono::seconds(10));   // 等待抓包线程捕获流量
auto stats = interceptor->GetStats();
std::cout << "Blocked packets: " << stats.blockedPackets << std::endl; // 断言拦截计数
interceptor->Stop();
```

先 `Start` 打开 WinDivert 句柄并断言成功，再 `BlockDomain("example.com", 0)` 添加无超时规则，睡眠 10 秒让抓包线程实际运行并累积统计，最后从 `GetStats().blockedPackets` 读取拦截包数验证引擎确实在吞吐流量——若本机恰好有程序访问 example.com 域名，该计数将大于零；即便没有命中，`Start`/`Stop` 的成对执行与统计结构的可读性也已验证。测试工程与发布代码共享同一份 `NetworkInterceptor.h`，保证"被测即所发布"；`.vcxproj` 独立于主程序链接，可在 CI 中无 GUI 依赖地单独构建运行。

除该工程外，生态内的自动化验证还有两条主线：**VCBlocks selftest**（17 个独立测试进程覆盖编译器与存储层）与 **LangBridge 黄金回归**（`tests/expect\` + `normalize.ps1` 归一化后二进制 diff），共同构成"宿主-插件-工具链"三级测试体系。

### 构建与配置

- **解决方案**：`light.slnx`（x64/x86 双平台），另提供 `build_one.ps1` 一键构建脚本；
- **模式切换**：`SetGuiMode.ps1 -EnableGui / -Console` 写入 `Cache\g.xayx` 加密标记切换控制台/GUI 形态；`YXGuiToggle.h / YXGuiToggleLoader.h` 提供运行时切换句柄；
- **托盘交互**：GUI 模式下提供托盘显示/隐藏/退出、双击托盘显示主窗口，退出逻辑走 `UnloadAllPlugins + LightApi_Shutdown` 干净关停。

---

## 周边工作区

### MCLauncher

> 插件元数据：id `yxg-minecraft-launcher-ui`，name「我的世界启动器」，version `2.0.0`，author `YXG`，entry `main.js`

MCLauncher 是 light 生态中最完整的 GUI 插件之一（约 30 个 C++ 源文件 + `ui/main.js` + `ui/plugin.json`），是一套 HMCL 风格（品牌绿配色 + 卡片式布局）的 Minecraft 启动器界面。项目自带 355 行 `README.md`，提供 `build.bat / build.ps1` 双构建脚本；源码中含多个 `.bak` 备份文件，`include/YXExtension.h`（与 light 同一 ABI）完成宿主对接，`dllmain.cpp` 提供 DLL 入口。

**实例模型**：一个文件夹即一个游戏实例（支持多版本并存），`folders.json` 记录实例列表，`accounts.json` 保存 Microsoft 账号登录信息；为隔离不同整合包，采用"实例目录 + 独立版本配置"的模型。`ui/plugin.json` 的完整清单（MCLauncher 实际文件内容）明示了能力范围：

```json
{
  "id": "yxg-minecraft-launcher-ui",
  "name": "我的世界启动器",
  "version": "2.0.0",
  "author": "YXG",
  "description": "HMCL 风格 Minecraft 启动器：多文件夹实例管理、单局设置、完整性检查、模组/资源包/光影/世界/原理图管理、加载器安装、Modrinth 双语搜索。界面由原生 DLL（YXG_MinecraftLauncher.dll）自绘。",
  "icon": "MC",
  "entry": "main.js"
}
```

注意描述中"界面由原生 DLL 自绘"——即该插件的界面主体并不依赖 `main.js` 渲染 HTML（`entry` 字段仍保留用于兼容），而是由 `YXG_MinecraftLauncher.dll` 直接在宿主窗口内自绘控件，与 ApiAiNative 的原生化路线一致。`src/ModManager.cpp` 负责 mods 目录扫描与启停管理；`ContentLib`/`AddonRepo` 对接 Modrinth 双语（中文/英文）搜索接口；`LoaderInstall` 安装 Fabric/Quilt/NeoForge 加载器，`ForgeInstall` 走 Forge 官方安装流程（`LoaderInstall.cpp` 旁另有 `.bak_forge_rewrite` 备份记录 Forge 重写历史）。

**用户界面**（`ui/main.js` + `ui/` 目录）：共 7 个页面——首页 / 版本管理 / 版本选择 / 下载 / 账号 / 全局设置；版本管理页含 6 个标签（版本列表、安装新版本、Mod 管理、资源包、世界、存档）。整体采用浅色 + 品牌色卡片式布局，导航在 `UiNav` 中分发，页面渲染集中在 `UiPages`，绘制原语集中在 `UiPaint`。

**C++ 模块**（`src/` 实际文件清单）：

| 模块 | 职责 |
| --- | --- |
| `LauncherApp.cpp / LauncherCore.cpp` | 启动器主循环与核心逻辑（版本解析、参数组装、进程拉起） |
| `MlcData.cpp / .h` | 实例/版本/账号数据结构 |
| `MsAuth.cpp` | Microsoft 账号 OAuth 登录（`Crypto.cpp` 负责令牌加密存储） |
| `JavaDetect.cpp / .h` | 扫描本机 Java 环境（注册表 + 常见路径） |
| `ForgeInstall.cpp` | Forge 安装流程（附 `.bak_forge_rewrite` 重写备份） |
| `LoaderInstall.cpp` | Fabric / Quilt / NeoForge 加载器安装 |
| `ModManager.cpp / .h` | Mod 管理与加载（mods 目录扫描、启用/禁用） |
| `ContentLib.cpp / .h` | 内容库（Modrinth 双语搜索对接） |
| `AddonRepo.cpp / .h` | 附加包仓库接口与实现 |
| `AddonNamesData.cpp` | 附加包名称数据维护 |
| `Skin.cpp / .h`、`SkinNet.cpp / .h` | 皮肤查看与网络皮肤（`Http` 拉取材质） |
| `Background.cpp / .h` | 背景图（含浅色/深色变体资源） |
| `Theme.cpp / .h` | 主题切换（配色集合） |
| `LoaderIcons.cpp / .h` | 加载器图标绘制 |
| `Ui.cpp`（附 `.bak`）/ `Ui.h` | 主界面逻辑 |
| `Http.cpp / .h`、`Json.cpp / .h` | 网络、配置工具层 |
| `Crypto.cpp / .h` | 加密工具（令牌与账号数据保护） |

**Microsoft 账号登录链路**（`MsAuth.cpp`，15682 字符）：完整实现 OAuth 设备码流。`StartDeviceCodeFlow` 依据是否配置自定义 ClientId 选择两套端点——默认走 `kLegacyConnectUrl`（`response_type=device_code` 老式微软账号协议），配置了自定义 ID 则切 Azure AD 端点（`grant_type=urn:ietf:params:oauth:grant-type:device_code`），`tokenUrl()`/`scopeValue()` 同步切换；请求体用 `application/x-www-form-urlencoded` 编码，30 秒超时，HTTP ≥400 时尝试解析 `error`/`error_description` 把微软原始错误带给用户（如自定义 ID 未开启公共客户端）。成功后从 JSON 提取 `user_code`（用户显示码）、`verification_uri`（验证页，缺省回退 `https://www.microsoft.com/link`）、`device_code`（轮询凭据）、`expires_in`（默认 900 秒）与 `interval`（轮询间隔，默认 5 秒）。`PollForToken` 按 `interval` 周期轮询授权状态；拿到令牌后进入 Xbox 系认证链：`xblAuthenticate` 把 MS token 换成 XBL 令牌与 `uhs`（用户哈希），`minecraftLogin` 用 XSTS 令牌换 `mcToken`，`FetchProfile` 拉取玩家档案（UUID 与皮肤数据）组装成 `Account`；`Refresh` 用长期刷新令牌静默续期，`FullLogin` 串联"设备码 → 轮询 → XBL → XSTS → MC → Profile"全流程，回调 `onDeviceCode` 把验证码与链接交 UI 展示。错误处理里 `xerrText` 把 XSTS 错误码翻译成中文提示：`2148916233` 无 Xbox 档案（提示去 xbox.com 创建）、`2148916235` 地区不支持、`2148916236/237` 需成人验证、`2148916238` 未成年需家庭组、`2148916227` 账号被封——登录失败时用户可以明确知道该去哪解决，而不是面对裸错误码。

**Java 运行时检测与自动安装**（`JavaDetect.cpp`，15016 字符）：`DetectAll` 枚举 `HKEY_LOCAL_MACHINE` 下 `SOFTWARE\JavaSoft\Java Runtime Environment` 与 `Java Development Kit` 两组子键（并经 `KEY_WOW64_64KEY` 访问原生 64 位视图，另补 `WOW6432Node` 路径兼顾 32 位视角），`enumSubKeys`/`queryValue` 封装 `RegEnumKeyExA`/`RegQueryValueExA` 读取版本号与安装路径；`probeJava` 对候选 `java.exe` 先解析同目录上级的 `release` 文件（Java 9+ 以 `JAVA_VERSION=` 记录版本），再把 `1.8.0_401 → 8`、`17.0.10 → 17` 的主版本换算交给 `major` 字段，并读取 PE 头（`e_lfanew` 偏移 0x3C → PE 签名 → OPTIONAL_HEADER 的 `Magic`）判断 64 位；`DetectBest(minMajor, &satisfied)` 按"版本最高且满足最低要求"选择最优运行时，`DetectExact(major)` 精确匹配指定大版本，`IsValidJava` 做存在性校验，`RuntimeRootW` 返回宽字符根路径，`findJavaw` 定位 `javaw.exe`（无控制台窗口版）。`FindRuntime(major)` 先本地探测、失败则走 `InstallRuntime` 下载官方运行时：`mirrorOf` 把微软/甲骨文下载 URL 映射到国内镜像（提升可达性），进度经 `RuntimeProgress` 回调上报，安装完成后返回 `outPath`——启动器因此能做到"检测不到 Java 时全自动补齐"，无需用户手工装 JRE。
**API/工具层**：`AddonRepo.h` 定义附加包仓库接口，`AddonNamesData.cpp` 维护名称数据；`tools/` 下 `gen_loader_icons.py` 生成加载器封面（Fabric/Quilt/Forge/NeoForge 图标），`gen_mod_data.py` 生成 Mod 元数据，`strip_loader_funcs.py` 精简加载器导出函数。构建产物通过 `build.ps1` 打包为 `Cache\gui\dll` 可识别的复合插件包。项目同时提供 `build.bat`（cmd 环境）与 `build.ps1`（PowerShell 环境）两套一键构建入口，并自带 `README.md` 说明部署步骤；`ui/plugin.json` 声明 id、名称、版本、作者与入口文件，宿主据此识别并加载。

### VCBlocks

VCBlocks 是"可视化积木编程"GUI 插件：把拖拽式积木翻译成 C/C++ 源码并现场编译成 DLL，让用户不写代码也能产出原生模块。**注意**：源码实际位于 `VCBlocks\VCBlocks\src\`（外层 `VCBlocks\` 仅含解决方案与脚本），工程结构为：`include/YXExtension.h`（245 行，与 light 同款插件 ABI）、`resource.h`、`src/PluginEntry.cpp`（92 行插件入口）、`src/core/` 与 `src/ui/` 两个目录。

**`src/core/` 核心模块**：

| 模块 | 职责 |
| --- | --- |
| `AppSettings.cpp / .h` | 应用配置（`settings.json` 读写） |
| `BlockLibrary.cpp / .h` | 积木库定义（积木种类与参数元数据） |
| `BlockModel.cpp / .h` | 积木模型（类型/参数/连接点） |
| `CodeGenerator.cpp / .h` | 积木 → C/C++ 源码生成器 |
| `Decompiler.cpp / .h` | C 源码 → 积木反向还原（支持逆向展示） |
| `DllBuilder.cpp / .h` | 编译产物的 DLL 打包与签名准备 |
| `TccCompiler.cpp / .h` | TinyCC 编译链封装（内嵌 `third_party/tcc` 完整 include 套件） |
| `MsvcCompiler.cpp / .h` | MSVC `cl.exe` 编译链封装 |
| `JdkCompiler.cpp / .h` | Java 编译链（`javac`）封装 |
| `LibraryScanner.cpp / .h` | 系统库扫描（头文件索引） |
| `LibraryStore.cpp / .h` | 库缓存存储 |
| `ProjectStore.cpp / .h` | 工程持久化（`.vblocks` 工程文件） |
| `Translator.cpp / .h` | 积木语义到目标语言的转换 |
| `CompileConfig.h` | 编译配置（工具链路径等） |
| `Http.cpp / .h` | 网络（下载缺失工具链） |
| `Json.cpp / .h` | JSON 工具层 |
| `util.cpp / .h` | 通用工具函数 |

**`src/ui/`**：`MainWindow.cpp / .h`（1939 行，主工作区窗口）、`LibraryStorePanel.cpp / .h`（库面板）、`StartupDialog.cpp / .h`（欢迎/新建工程对话框）、`Theme.cpp / .h`（91 行主题）。

**三条编译链**并存：TinyCC（极速、无依赖，适合轻量验证）、MSVC（正式发布，产出优化 DLL）、JDK（Java 积木目标）。`selftest/` 目录（位于 `VCBlocks/selftest`）含 17 个独立测试进程：`builtintest`（内置积木库）、`cpptest`（C++ 生成）、`diag`（诊断）、`dirtest`（目录遍历）、`dlltest`（DLL 加载）、`gditest`（GDI 绘制）、`gen_dll`（DLL 生成端到端）、`headertest`（头文件扫描）、`host_test`（宿主 ABI）、`javatest`（Java 编译链）、`lines_test`（行定位）、`selftest`（自检主程序）、`settest`（设置存储）、`storetest`（库存储）、`tcc_test`（TCC 编译）、`tccres_test`（TCC 资源编译，验证 `.rc` 处理）、`tctest`（TCC 行为）——覆盖编译、链接、DLL 注册、存储、TCC 行为、资源编译等维度，`build_and_test.ps1` 一键串联全部测试；`build/Cache/vC/` 缓存 `settings.json`、`lang/core.json` 与 `tcc/include`。

**三条编译链的差异实现**（均来自对应 `.cpp` 的真实逻辑）：

**MSVC 链（`MsvcCompiler.cpp`）**走「找编译器再拼命令行」的经典路线：`Init` 先在固定目录有限深度内查找 `VC\Tools\MSVC\<ver>\bin\Hostx64\x64\cl.exe`（版本号目录不写死，扫描发现），找不到再遍历各盘符下的 `X:\Microsoft Visual Studio` 继续搜；找到后把 `cl.exe` 完整路径缓存到全局 `g_cl`，并以去掉 `\bin\Hostx64\x64\cl.exe` 后缀的方式推导工具集根目录。编译时用 `CreateProcessW` 拉起进程，参数为 `/nologo /EHsc /utf-8 /MT /W3 /Fe:<outExe> <srcPath>`——`/MT` 静态链接运行时保证产物免装 VC 运行库即可分发，`/utf-8` 与全仓编码约定一致，避免源代码中的中文注释因代码页不符而报 C4819 警告。stdout/stderr 合并回收，失败时从进程退出码与输出中提炼错误信息返回给上层。

**TinyCC 链（`TccCompiler.cpp`）**完全不同：TinyCC 以 `libtcc.dll` 形式**打包进插件资源**，运行时才 `LoadLibraryW` 加载（先试程序根目录，再试 `tcc\` 子目录），并用手工维护的函数指针表逐项 `GetProcAddress` 解析 `tcc_new / tcc_add_include_path / tcc_add_library_path / tcc_compile_string / tcc_output_file` 等导出，`tcc_new`、`tcc_compile_string`、`tcc_output_file` 三者缺一即判定不可用。编译时调用 `tcc_new()` 创建 `TCCState`，把内嵌的 include 根与用户附加头文件目录 `tcc_add_include_path` 进去，随后**直接编译内存中的源码字符串**——这正是 `CompileToExe(code, outPath)` / `CompileToDll(code, outPath)` 接收 `std::string code` 而非文件路径的原因：积木编辑器把方块图生成 C 源码后无需落地即可即时编译，编译速度与内存周转都优于磁盘往返，适合「边拖边验证」的交互场景。

**JDK 链（`JdkCompiler.cpp`）**面向 Java 积木目标：`Init` 依次探测两处——先读 `JAVA_HOME` 环境变量（`GetEnvironmentVariableW` 用 4096 字符缓冲），在其 bin 目录找 `javac.exe` 并顺带推导同目录 `java.exe`；未命中再沿 `PATH` 查找（覆盖 Oracle 安装器的 javapath shim）。`Compile` 组装 `javac -encoding UTF-8 -d <outDir> <srcPath>` 参数——`-encoding UTF-8` 显式指定源码编码，与中文环境下默认 GBK 的文件系统编码解耦；`Run` 则组装 `java -cp <classDir> <className>`。进程输出统一按 ANSI（ACP）解码为 UTF-8 后拼接，保证控制台中文提示不乱码。三链共用 `DllBuilder` 的产物打包与 `LibraryScanner` 的系统库索引，构成「一套积木、三条后端、统一产物模型」的编译矩阵。

**主窗口控件结构**（`src/ui/MainWindow.cpp`，1939 行）可在源码中直接还原：

- `PromptProc`：通用输入提示对话框（静态文本 + EDIT 输入框 + 确定/取消按钮），`NewProjectProc` 注册类名 `VCBlocksNewProject`，内建"项目名称 EDIT + 目标语言 COMBOBOX 下拉（C/C++/Java 三选一，`CBS_DROPDOWNLIST`）+ 确定/取消"，控件 ID 从 200 起编号；
- `SettingsProc`（类名 `VCBlocksSettings`）：设置对话框含三个复选框——`编译后自动运行`（`BS_AUTOCHECKBOX`）、`生成调试符号`、`YXG 插件格式(.dll 时)`（决定产物是否按插件 DLL 格式输出），另含颜色 EDIT（主题色自定义）与"选择图片"按钮（背景图）；`ShowSettingsDialog(parent, CompileConfig&, AppSettings&)` 将界面选择写回配置对象；
- 主面板 `MainWindow::CreatePanel` 注册类名 `VCBlocksPanelWnd`：创建语言 COMBOBOX、目标平台 COMBOBOX、参数 EDIT（`ES_AUTOHSCROLL`）与 TREEVIEW 积木树（`WC_TREEVIEWW`），事件统一进入 `WndProc → HandleMessage`；
- `IsContainer(const Block& b)` 判断某积木是否容器（可容纳子积木嵌套）；
- `RunExecutable(exePath, output)` 运行编译产物并捕获 stdout/stdout 到字符串，`ParseErrorLine(log)` 从编译器日志文本解析错误行并定位行列号，用于在画布上高亮出错积木。

`replace_plugin.bat` 把构建产物 DLL 直接热替换进宿主插件目录，配合宿主 `reloadplugin` 命令实现"改代码 → 编译 → 热替换 → 界面刷新"的快捷迭代闭环。

典型工作流：`StartupDialog` 新建工程 → `BlockLibrary` 选择积木 → 画布编辑 `BlockModel` → `CodeGenerator` 生成源码 → `TccCompiler/MsvcCompiler` 编译 → `DllBuilder` 打包 → 宿主 `RegisterHook` 注册钩子 → 运行期 `RunHooks` 执行。

**TCC 自动打包机制**：`tools/pack_tcc.py` 把 `third_party\tcc` 目录打包成单个 bundle 文件（`VCBlocks\VCBlocks\tcc_bundle.bin`），供 `.rc` 资源嵌入——产物无需随行携带 TCC 安装目录。bundle 格式为：`u32 文件数 N`，随后 N 个记录 `[u32 路径长度][路径 utf8][u32 大小][内容]`；只打包编译必需的项：`libtcc.dll`、`include/` 全部头文件、`lib/libtcc1-64.a` 以及 `lib/` 下的 `.def` 导入库，兼顾体积与可编译性。运行时 `TccCompiler` 从资源解出 bundle 到临时目录即可就地编译。

**构建与自检脚本**：根目录 `VCBlocks.sln` 为解决方案；`build_and_test.ps1` 一键构建并跑全部 selftest；`build_check.ps1` 构建前检查（工具链与依赖就绪性）；`replace_plugin.bat` 把构建产物 DLL 热替换进宿主插件目录；`x64/` 存放 64 位构建产物；`third_party/tcc` 内嵌 TinyCC 完整发行（含 `include`、`lib`、`libtcc` 文档与示例）。

### JavaCppBridge

JavaCppBridge（README 内称 **LangBridge**）是一个 C++17 命令行双向语言转换器：`java2cpp / cpp2java` 两条管线共用同一套前端，位于 `JavaCppBridge/` 工作区。源码结构：

- **词法**：`src/lexer.cpp`——分词器，产出 token 流；
- **语法**：`src/parser.cpp`——递归下降解析，产出 AST；
- **语义**：`src/analyze.cpp`——符号表与类型检查；
- **公共后端**：`src/gen_common.cpp`——两目标共用的代码生成工具（缩进、命名、注释迁移）；
- **J→C**：`src/gen_j2c.cpp`——Java 语法树到 C++ 代码的发射器；
- **C→J**：`src/gen_c2j.cpp`——C++ 语法树到 Java 代码的发射器；
- **入口**：`src/main.cpp`——参数解析与管线调度；
- **公共头**：`include/bridge.hpp`——全部模块的类型声明；
- **示例**：`examples/java/ShapeDemo.java`、`examples/cpp/ShapeDemo.cpp`、`examples/cpp/FeatureProbe.cpp`——覆盖类、继承、接口、模板等特性探针；
- **回归**：`tests/expect/` 存黄金输出文件，`tools/run_regression.sh`（POSIX）与 `run_regression_msvc.bat`（Windows）把当前输出与黄金输出逐行对比，`tools/normalize.ps1` 先做空白与路径归一化再比较，保证跨平台可复现；
- **构建**：`build.sh` / `build_local_msvc.bat` / `Makefile` 三套构建入口，服务不同工具链与 CI 场景。`Makefile` 自动探测 `cl (MSVC) → g++ → clang++` 三种编译器，按探测顺序选择；`run_regression_msvc.bat` 展示了一条完整的端到端回归链路：先用 `cl` 构建 CLI，再翻译两个示例工程，随后分别用 MSVC 编译生成的 C++ 代码、用 `javac`（若可用）编译生成的 Java 代码，最后与 `tests\expect\` 黄金输出做二进制级 diff 并输出 PASS/FAIL 汇总；`normalize.ps1` 解决跨平台输出不稳定问题——Windows 上 Java 的 `println` 输出 CRLF、C++ 侧输出 LF，且末行是否带换行取决于重定向方式，因此该脚本统一转换为纯 LF 且恰好一个结尾换行，保证黄金对比可复现。

设计意图：模组/插件生态常在 Java 与 C++ 间迁移（如 Forge 模组转原生实现），LangBridge 提供确定性的源码级转换，配合黄金回归保证转换稳定性。

### ModMigrator

ModMigrator 是图形化模组迁移工具，帮助把旧接口模组迁移到新 API。`main.cpp` 以 `WinMain` 进入：`InitCommonControlsEx(ICC_PROGRESS_CLASS | ICC_BAR_CLASSES)` 初始化进度条控件后创建 `ModMigratorGUI::Create/Run`。核心数据结构（`ModMigrator.h`）：

- `Config{ javaHome, libPath, verbose, keepTempFiles }`：运行配置（Java 主目录、依赖库路径、详细日志开关、是否保留临时文件）；
- `ConvertOptions{ includeSource, includeResources, exportZip, generateEvents, generateCaps, verbose }`：转换开关（含源码、含资源、导出 zip、生成事件、生成能力注册、详细输出）；
- `ElementInfo{ name, type, registryName, files, metadata, isBlock, isEvent, isCapability, isRecipe }`：元素级信息（注册名、关联文件、元数据、元素类别：方块/事件/能力/配方）；
- `ModInfo{ modId, modName, modVersion, modDescription, modAuthor }`：模组元数据。

`JavaParserBridge`（`JavaParserBridge.h`）封装对 Java 解析器的进程桥接：`SetJavaHome / SetClassPath / AddToClassPath / ParseMod / IsJavaAvailable / GetJavaVersion / GetExeDirectory / GetDefaultClassPath`——运行期自动探测 Java 环境，把模组 jar 解包后用 `javac`/解析器提取每个 `ElementInfo`，再按 `ConvertOptions` 逐项迁移。GUI 侧提供进度条、日志窗与预览，最终按 `exportZip` 开关产出迁移后的 zip 工程。

### ToMCreatorForge

ToMCreatorForge（480 行主文件 + 5 个子模块）把 MCreator 生成的源码迁移到 Forge 框架。主窗口含 7 个复选框：`Events`（事件迁移）、`Caps`（缩略语大写化，如 tick→Tick 的方法名规范）、`Network`（网络初始化迁移）、`Zip`（打包输出）、`Source`（源码保留）、`Docs`（文档生成）、`Remember`（记忆上次配置）；界面含日志区、进度显示与配置保存/加载按钮，`manifestdependency` 声明 Common-Controls 6.0 启用新式控件外观。子模块：

- `ConverterCore`（574 行）：转换主流程（入口点，串联解析与生成）；
- `EventParser`（189 行）：MCreator 事件注册解析；
- `JavaParser`（244 行）：Java 源码解析（方法、字段、注解）；
- `ModMetadataParser`（278 行）：模组元数据解析（mods.toml / 注解）；
- `ConfigManager`（113 行）：配置持久化。

与 ModMigrator 的分工：后者面向"任意模组迁移"，前者专攻 "MCreator 工程 → Forge 标准工程" 的自动重构。

### BinaryHexEditor

修复版二进制编辑器（3034 行，位于 `BinaryHexEditor/BinaryHexEditor/` 子目录，含 `framework.h`、`Resource.h`、`targetver.h` 等配套头），核心特性：**UTF-8 全字符支持**（十六进制区与文本区同步滚动、全字符集编辑）、新建文件、直接编辑与滚动条修复（长文件滚动定位准确）。视觉效果走深色主题：

```cpp
// BinaryHexEditor.cpp 主题色定义（节选）
Background RGB(30, 30, 30)     // 窗口底色
Surface    RGB(45, 45, 45)     // 面板底色
Address    RGB(86, 156, 214)   // 地址栏（蓝）
HexText    RGB(206, 145, 120)  // 十六进制文本（橙）
BinText1   RGB(100, 220, 200)  // 二进制文本（青）
Cursor     RGB(38, 79, 120)    // 光标（深蓝）
FoundBg    RGB(255, 165, 0)    // 搜索结果高亮（橙）
```

依赖 GDI+、comctl32、comdlg32、shlwapi。它同时是 `.xayx / .xbyx / .xcyx` 加密文件的默认查看器（由 bt.exe 注册文件关联），被 `light hex` 命令直接拉起。

### HexForge

HexForge 是轻量十六进制编辑器（1219 行，位于 `HexForge/HexForge/` 子目录），基于 RichEdit + commctrl，使用 `fstream / vector / map` 管理数据。核心为**撤销栈** `UndoAction`：

```cpp
// HexForge.cpp 撤销操作类型
UNDO_INSERT   // 插入
UNDO_DELETE   // 删除
UNDO_MODIFY   // 修改
```

支持大文件边界控制（`MAX_FILE_SIZE` 上限，防止恶意超大文件拖垮内存），界面资源由 `resource.h` 定义，与 BinaryHexEditor 形成"轻量/重型"互补。

### Registry

Registry（约 1510 行）是带安全层的注册表管理器：`RegistryManager g_registryManager` 管理读写，主窗口由 `TreeView + ListView + EditValue` 三区组成（`g_hTreeView / g_hListView / g_hEditValue`）——左侧树浏览键、右侧列表查看值、底部编辑区改值。独特点是内置 `XayxEncryptor`：密码常量 `"sr5r4g464dry8k78665sglnjtr4m5z578tk78q"`，通过 `XorEncryptDecrypt / Encrypt` 对敏感注册表值加密存储（与 XZYX 体系同一异或算法家族，`HELP_PASSWORD` 亦复用同一口令字面量），调试期用 `OutputDebugStringA` 输出加解密轨迹。该工具由 `light registry` 命令启动，用于维护 `HKEY_LOCAL_MACHINE` 下的 xayx 系列安全键值。

### Minecraft_C_IP_B

Minecraft 服务器 IP 检测工具（763 行，文件头注「Minecraft服务器IP检测工具.cpp」）：**自带完整 WinPcap 类型定义**（`pcap_t、pcap_pkthdr、bpf_insn、bpf_program、pcap_addr、pcap_if、pcap_rmtauth`），不依赖外部头文件即可编译——这是为规避"目标机未装 WinPcap SDK"而做的自包含设计；配合 `winsock2 / ws2tcpip / iphlpapi`，链接 `ws2_32 + iphlpapi`，用于捕获并解析 Minecraft 协议包、还原服务器地址。与 light 的 `fpnpc`（`Full_Port_Network_Packet_Capture_Tool.exe`）功能互补，侧重 MC 协议识别。

### cs

Bing 搜索下载链接提取工具（661 行），核心为 `BingSearch` 类（590 行 `main`）：基于 WinHTTP 发起搜索，`SetConsoleUTF8` 保证中文输出不乱码，`UrlEncode` 处理查询参数，`GenerateRandomString / GenerateCVID`（32 位十六进制）伪装会话指纹以模拟真实浏览器，`GetCurrentTimeString` 为日志打时间戳；从 Bing 结果页 `ExtractAndDisplayLinks` 提取真实下载链接供用户选择，`SaveLog` 保存检索记录。适用于"搜软件下载站但不想被反爬"的自动化场景。

### bt

bt.exe（249 行）是 light 的安装引导与文件关联程序，函数集：`SetColor` 配色、`GetProgramDir / GetParentDir` 定位安装目录、`RequireAdmin` 请求 UAC、`CopyIconToSystem` 复制图标、`RegisterFileType` 注册三级文件类型、`RegisterApplication` 注册为系统应用。执行流程：

1. 创建 `C:\Program Files\.light` **隐藏目录**；
2. 写入 `light.xbyx` **隐藏文件**（安装标记 / 二级加密档）；
3. 复制 `favicon.ico` 为系统图标；
4. `RegisterFileType` 注册 `.xayx（1级）/ .xbyx（2级）/ .xcyx（3级）` 关联，默认打开方式为 `BinaryHexEditor.exe`；
5. `SHChangeNotify` 刷新资源管理器；
6. 唤起卸载器 `rppcst\light_Uninstall.exe`。

### ConsoleApplication1

Java 源码批量替换器（310 行），核心 `JavaCodeReplacer`：`readFile / writeFile / backupFile`（自动生成 `.backup` 备份）三步完成对源码文件的定点替换，用于批量迁移 `ItemStack` 等 API 调用（如 `setItem` → `setStack` 类改写）。与 ModMigrator / ToMCreatorForge 场景互补（后者做结构级转换，前者做文本级批量替换）。

### yuxuangzs-Service-light

见 [系统服务与命名管道](#系统服务与命名管道)——它是 light 提权与后台任务的执行底座：宿主把需要 SYSTEM 级权限的操作封装成 `run:` 命令下发，服务以交互会话用户身份拉起目标进程并等待结果，全程走异或混淆的命名管道。

---

## 使用示例

### 命令行（控制台模式）

```bat
:: 拦截某个域名，阻止其被访问
light block ads.example.com

:: 仅阻止特定进程访问某域名
light block-for-process telemetry.example.com game.exe

:: 查看当前黑名单，清空全部拦截规则
light showblocks
light clearblocks

:: 白名单：放行被误拦的域名
light whitelist update.example.com
light unwhitelist update.example.com

:: 加密压缩整个目录为 .xzyx 容器，或反向解出
light compress E:\Projects\game E:\backup\game.xzyx
light extract E:\backup\game.xzyx E:\Projects\game-restored
light list E:\backup\game.xzyx

:: 带进度下载文件
light download https://example.com/installer.exe D:\tmp\installer.exe

:: 锁定/解锁文件夹（拒绝删除与写入，可对抗误操作）
light lock D:\Documents\Private
light unlock D:\Documents\Private

:: 插件与工具
light reloadplugins        :: 热重载全部插件
light createplugin MyTool   :: 生成插件工程模板
light registry              :: 打开注册表安全管理器
light hex                   :: 打开二进制编辑器
light cel                   :: 打开 CEL 工具
light fpnpc                 :: 打开全端口数据包捕获工具
light about                 :: 版本信息
light help                  :: 命令帮助
```

命令直入模式无需管理员权限即可执行常规操作；涉及提权、服务或文件夹锁定的操作由 `IsRunningAsAdmin` 检查后自动请求 UAC（`ElevateCurrentProgram`）或经 `SendToService` 转交 `LightService` 处理。

### GUI 插件链路

1. `light` 启动后根据 `Cache\g.xayx` 加密标记进入 GUI 形态（`SetGuiMode.ps1 -EnableGui` 可切换）；
2. `PluginManager` 扫描 `plugins\` 与 `Cache\gui\dll`，`GuiDllStore` 提供商店页面，一键下载后自动注册；
3. 某 GUI 插件被用户打开时，宿主创建原生窗口，`IGuiProvider::GetLayoutType()` 决定托管布局（标签页 / 简单面板 / 自定义），`YXGuiPlugin` 内建 WebView2 页面承载 JS 界面；
4. JS 插件调用 `LightApi_HttpRequest` 或通过 `ApiSlot` 触发原生能力时，`PermissionManager::Prompt` 弹出授权窗（拒绝 / 仅本次 / 记住）；
5. 插件执行网络拦截、加密容器、文件操作等能力后，结果经 `WM_YXGUI_HTTP_DONE` 异步回注 JS Promise，页面 `await` 后渲染；
6. 退出时按 `UnloadAllPlugins → LightApi_Shutdown` 顺序干净关停，托盘图标同步退出。

### 插件开发起步（模板生成器产物）

执行 `light createplugin MyTool` 后，`PluginTemplateGenerator` 会在插件目录落下一份**可直接编译的工程骨架**，其真实结构与关键约定如下：

```text
plugins/MyTool/
├── include/PluginInterface.h   # ABI 双向接口（二进制写出，含版本常量）
├── src/                        # 插件实现源文件
├── CMakeLists.txt              # 构建脚本（OUTPUT_NAME 与去除 lib 前缀）
├── build.bat                   # Windows 一键构建
├── plugin.json                 # 插件清单（元数据）
└── README.md                   # 插件自述模板
```

`plugin.json` 由代码逐字段生成：`id / name / version / author / description / website / date`，其中 `website` 直接拼装为 `https://github.com/{author}/{pluginName}`，`date` 用 `std::strftime("%Y-%m-%d")` 取当天日期——插件一生成就带上了可发布的元信息。生成的 `src` 侧源码自带 `m_initialized / m_pluginName / m_version / m_author` 四个成员变量与初始化列表，并已把 `info.id`、`info.description` 填充为清单值。`include/PluginInterface.h` 以二进制模式写出，内置 `kName / kId / kVersion / kAuthor / kDescription` 一组 `constexpr` 常量与一个最小可运行示例（`hello.description = "Say hello from <dllName>"`），新插件打开即可见注册信息，无需手工补接口声明。

`CMakeLists.txt` 中 `OUTPUT_NAME` 设为 DLL 文件名、`PREFIX ""` 去除生成的 `lib` 前缀，保证产物名与 `plugin.json` 的 `id` 严格对齐——`PluginManager` 按文件名定位插件，命名不一致会导致加载失败。脚手架还生成 `build.bat`（一键调用 cmake + MSVC）与插件自述 `README.md`。骨架落盘后，`PackToZip` 走 `zipOpen` → `fs::directory_iterator` 递归收集 → `fs::relative` 计算相对路径 → `std::replace` 统一分隔符（`/` → `\\`）→ `zipOpenNewFileInZip`（压缩级别 `Z_DEFLATED`）→ 打印 `fs::file_size` 确认体积，一键产出可分发的 `.zip` 插件包；该包投入 `Cache\gui\dll` 或商店仓库即完成发布，形成「生成模板 → 实现逻辑 → 构建打包 → 商店分发」的完整闭环。

### 服务与提权生命周期

与宿主交互的权限敏感操作遵循统一的三段式：`CheckAndInstallService` 按需安装服务，`StartLightService / StopLightService` 按需启停；需要 SYSTEM 级动作时宿主向 `\\.\pipe\LightPipe` 写入异或加密的命令帧，服务端 `ConnectNamedPipe` 循环等待、`ReadFile` 取出后按 `run:` 前缀解析并以交互用户身份执行，完成后写回结果帧。`uninstall` 命令则反向清理：停服务、删注册表键、删安装目录。

## 编码规范与工程约定

从全部工作区源码中归纳出的统一工程约定，新代码应保持一致：

- **语言与平台**：C++17（主），C（WFP 驱动 / QuickJS 嵌入），JavaScript（GUI 页面与插件逻辑）；仅面向 Windows（WinHTTP、WinDivert、WebView2、WFP、注册表 API）；
- **编译工具链**：原生 MSVC（`light.slnx`、各 `.vcxproj`），脚本层统一 `.ps1 / .bat / .sh` 三态覆盖（PowerShell 主、批处理兜底、sh 供 POSIX 回归）；
- **编码与行尾**：源码 UTF-8 + CRLF，中文注释直接内嵌；控制台程序在运行时显式设置 UTF-8 输出（`SetConsoleUTF8`）保证中文终端不乱码；
- **命名**：导出符号统一 `YXG_` 前缀与 `LightApi_` 前缀；宿主命令处理器 `HandleXxxCommand`；工具类集中 `XxxManager`、`XxxStore`、`XxxParser` 命名；私有工具函数放匿名命名空间（`ToLower / NormalizeDomain` 等）；
- **加密族**：敏感数据一律走 `XorEncryptDecrypt` 异或混淆，密钥不落明文配置文件；容器落盘统一 `.xayx / .xbyx / .xcyx` 三级扩展名；
- **单例模式**：跨模块共享状态（网络拦截器、权限管理器、注册表管理器）使用单例 + 全局指针（`g_NetworkInterceptor`、`g_registryManager`、`g_SecurityCore`）双入口，便于测试桩替换；
- **权限最小化**：需要提权时先 `IsRunningAsAdmin` 检查，非管理员走 UAC；需要 SYSTEM 级操作时经命名管道转交 `LightService`，宿主不常驻高权限；
- **测试文化**：核心算法（拦截器、编译器、转换器）均配独立测试工程或 `selftest` 黄金回归，构建脚本与测试脚本成对提供（`build.sh` ↔ `build_local_msvc.bat`）。

## 工程依赖关系图

```text
                    ┌─────────────────────────────┐
                    │        light.exe 宿主        │
                    │  命令分发 · XZYX · 服务 · 插件 │
                    └───────┬──────────┬──────────┘
                            │          │
              ┌─────────────▼───┐  ┌───▼──────────────────┐
              │  插件 ABI        │  │  LightService         │
              │  YXExtension.h  │  │  命名管道 · RunAsUser  │
              └──────┬──────────┘  └──────────────────────┘
        ┌────────────┼──────────────┐
        ▼            ▼              ▼
   MCLauncher    VCBlocks      GuiDllStore
   (GUI 插件)   (GUI 插件)    (商店 · 远程下载)
```

右侧独立工作区（BinaryHexEditor、HexForge、Registry、bt、cs、JavaCppBridge、ModMigrator、ToMCreatorForge、Minecraft_C_IP_B、ConsoleApplication1）既可被 `light` 命令拉起（hex / registry / cel / fpnpc / createplugin 等），也可独立构建运行。

## 构建与验证快速指南

按依赖顺序从零构建整个生态的推荐步骤（全部脚本均为工作区内真实存在的入口）：

1. **主程序 light**：使用 `light.slnx`（VS 2022+，x64/x86 均可）打开编译，或执行 `light\light\build_one.ps1`；产物为 `light\light\Debug|Release\light.exe`。需预先放置 WinDivert-2.2.2-A、zlib-1.3.2、WebView2 SDK 与 `LzmaLib.lib`（构建机路径见 `.vcxproj` 的 include/lib 配置）；
2. **生成 GUI 标记与形态切换**：`light\light\SetGuiMode.ps1 -EnableGui` 写入 `Cache\g.xayx` 标记；`-Console` 恢复控制台形态；
3. **GUI 插件（MCLauncher）**：在 `MCLauncher\` 下执行 `build.ps1`（或 `build.bat`），产出符合 `Cache\gui\dll` 约定的复合插件包，放入 `Cache\gui\dll` 后由 `GuiDllStore` 自动扫描；
4. **GUI 插件（VCBlocks）**：先运行 `tools\pack_tcc.py` 生成 `tcc_bundle.bin` 资源，再用 VS 打开 `VCBlocks.sln` 构建，或执行 `build_and_test.ps1`（构建 + 全量 selftest）；首次启动可执行 `selftest` 系列验证编译链（`tcc_test`、`javatest` 等）；
5. **LangBridge**：`JavaCppBridge\` 下执行 `Makefile`（自动探测 cl/g++/clang++）或 `tools\build_local_msvc.bat`；验证执行 `tools\run_regression_msvc.bat` 或 `tools\run_regression.sh`，对照 `tests\expect\` 黄金输出；
6. **ModMigrator / ToMCreatorForge / Registry / BinaryHexEditor / HexForge / Minecraft_C_IP_B / cs / bt / ConsoleApplication1**：各 `*.vcxproj` 分别用 VS 打开编译；`bt.exe` 编译后以管理员运行即完成 `xayx/xbyx/xcyx` 文件关联注册；
7. **系统服务（可选）**：编译 `yuxuangzs-Service-light`，由 `light` 的 `CheckAndInstallService` 注册为 `LightService`；
8. **回归汇总**：`tests\NetworkInterceptorTests` 独立工程验证拦截逻辑；VCBlocks `selftest`（17 项）验证编译与存储；LangBridge 回归验证双向转换。

所有工程共享同一套 MSVC 工具集与 SDK 路径约定（参见 `run_regression_msvc.bat` 中的示例路径结构），脚本统一 `setlocal` / 显式 `cd /d` 定位根目录，保证在任何工作目录下执行都与仓库根相对路径一致。

## 自动化脚本

仓库根目录提供两组维护脚本：

**`bt.bat`** —— 一键自动提交推送（与 `bt.exe` 同名不同物，意为"backup tool"）：

```bat
REMOTE_URL=https://github.com/yuxuan-gzs/light.git
BRANCH=main
git add -A
git commit -m "Auto commit: upload folders and source files"
git push -u origin main
```

执行后本地全部变更（含新增工作区）自动入库并推送到 `main` 分支，是 "开发 → 提交 → 同步" 的零摩擦流水线。

**`restart-dsh.cmd / restart-dsh.ps1`** —— 重启 `D:\yuxuangzs\DSH Desktop\DSH Desktop.exe`，日志写入 `D:\YX\whale-restart.log`；双脚本互为 fallback（cmd 调 PowerShell，PowerShell 直连），适用不同调用方（计划任务 / 手动 / 服务）。

**`light\light\SetGuiMode.ps1` / `build_one.ps1`** —— GUI 模式切换与一键构建（见 [构建与配置](#构建与配置)）。

---

## 已知边界与限制

以下限制均来自代码结构与实际运行环境的客观约束，供使用者与贡献者参考：

- **系统服务**：`yuxuangzs-Service-light` 需要管理员安装（`CheckAndInstallService`），服务与宿主通过 `\\.\pipe\LightPipe` 单管道通信，同一时刻仅支持单客户端连接，重负载场景需注意排队；
- **网络拦截**：用户态 WinDivert 方案要求进程以管理员权限运行（WinDivert 驱动加载依赖）；域名级拦截基于 TLS/HTTP/DNS 特征识别，IPv6 明文流量与纯 IP 直连需配合内核态 `WfpCallout`（`FWPM_LAYER_ALE_AUTH_CONNECT_V4` 仅覆盖 IPv4 出站连接）；DNS 层拦截只覆盖 UDP（IPv4）查询，TCP 型 DNS（DoT）不在 `ExtractDNSDomain` 处理范围；`ProcessTcpSegment` 的流重组依赖顺序号连续性，重度乱序的 TCP 分段可能落入超时清理路径；TLS 版本低于 1.2 的 ClientHello 无 SNI 扩展，只能靠 IP 规则兜底；
- **内核驱动**：`WfpCallout` 为原型实现，`MAX_IP_LIST_SIZE` 上限 1024 条规则；正式环境需 WHQL/测试签名并更新 `WfpCallout.inf`；
- **GUI 集成**：GUI 形态依赖 WebView2 运行时，`Cache\g.xayx` 加密标记决定启动形态，若标记损坏会回退 `FindGuiPlugin`；QuickJS 为内嵌源码，MSVC 编译时需按 `QUICKJS_MSVC_FIX.md` 应用修复；
- **加密容器**：`.xayx/.xbyx/.xcyx` 为自研混淆格式（异或 + LZMA），密钥以常量形式内嵌于二进制（`XZYX_INTERNAL_KEY`），适合防误用/防篡改场景，不适合对抗专业逆向；
- **文件锁定**：`LockFolderWithTrustedInstaller` 依赖 TrustedInstaller 所有权接管，文件系统需为 NTFS；跨卷移动锁定目录前必须先 `unlock`；
- **编译依赖**：主工程链接 WinDivert-2.2.2-A、zlib-1.3.2、minizip、LzmaLib、WebView2LoaderStatic，构建机需预先放置对应 SDK（`_link_objs.txt` 记录了链接对象清单）；
- **E-C 工作区**：当前目录仅存 `.vs` 构建残留，无任何 `.cpp/.h` 源码，本文档不描述其实现。

## 文档与参考

工作区内自带多份有价值的工程文档与新手指南，整理如下：

| 文档 | 位置 | 内容 |
| --- | --- | --- |
| 内置 GUI 集成说明 | `light\light\BUILTIN_GUI_INTEGRATION.md` | `Cache\g.xayx` 标记机制、GUI/控制台切换、托盘行为与回退策略 |
| 编译修复记录 | `light\light\COMPILE_FIX_V4.md` | 第 4 版编译问题的成因与修复步骤，含链接对象清单核对 |
| QuickJS MSVC 适配 | `light\light\QUICKJS_MSVC_FIX.md` | QuickJS 源码在 MSVC 下的编译选项、补丁与规避方法 |
| 启动器自述 | `MCLauncher\README.md`（355 行） | 启动器功能、页面、实例模型与部署说明 |
| 转换器说明 | `JavaCppBridge\README.md` | LangBridge 用法、管线结构与回归说明 |

这些文档与源码同库维护，是排查编译问题、理解集成机制的权威依据；新贡献者建议先读 `BUILTIN_GUI_INTEGRATION.md` 了解宿主形态，再按本文档的 [构建与验证快速指南](#构建与验证快速指南) 逐步搭建环境。若在阅读或编译中发现问题，可参照 `COMPILE_FIX_V4.md` 的格式记录复现步骤与修复方案，回写仓库以沉淀经验。

## 贡献与扩展指引

欢迎以三种方式参与本仓库：

1. **CLI 命令**：`light <command>` 系列直接调用宿主能力（拦截、压缩、锁定、下载）；
2. **API 插件**：用 `light createplugin <名称>` 生成模板，实现 `IPlugin` 接口，交给 `PluginManager` 加载；
3. **GUI 插件**：实现 `IGuiProvider` + `YXG_RegisterPlugin`，打包进 `Cache\gui\dll`，即可出现在插件商店与插件中心。

所有新增能力建议遵循既有约定：统一走 `YXExtension.h` ABI、使用 `YXG_` 前缀、敏感数据走 XZYX 加密族、功能先命令行后 GUI、带 `selftest` 或独立测试工程验证。

---

## 致谢与生态总结

light 自 2024 年起持续演进，形成了"一个宿主、多个工作区、统一 ABI、三级加密、双形态界面"的独特架构：命令行与控制台模式保证脚本化与自动化友好，GUI 插件体系借助 WebView2 与 QuickJS 让 JS 开发者也能直接使用 C++ 原生能力（网络拦截、加密容器、系统服务）；VCBlocks 与 LangBridge 打通了"积木 → 源码 → 编译 → DLL"与"Java ↔ C++"两条生产力链路；ModMigrator、ToMCreatorForge、ConsoleApplication1 则聚焦 Minecraft 模组生态的代码迁移与批量改写。整个仓库以 The Unlicense 释放进公有领域，欢迎任何形式的使用与再分发。

**快速索引**：主程序 `light/light/`（入口 `light.cpp`）→ 拦截 `NetworkInterceptor` → 加密 `XZYX` 族 → 插件 ABI `YXExtension.h` → GUI `Gui/` → 生态 `MCLauncher/`、`VCBlocks/` → 服务 `yuxuangzs-Service-light/` → 工具集其余工作区。

---

## 许可证

本仓库以 **（见根目录 `LICENSE`）** 发布：所有代码进入公有领域，可自由使用、修改、分发除light项目以外项目。在适用法律范围内不提供任何担保。仓库内引用的 WinDivert、QuickJS、LZMA、zlib、TinyCC、WebView2 等第三方组件，各自遵循其上游许可证条款，使用时请一并遵守。

*本文档基于 `D:\YX` 工作区实际源码整理，符号名、行数、接口、常量均取自代码文件本身，未虚构未验证的实现细节。*
