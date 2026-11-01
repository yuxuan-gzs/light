# YXG Minecraft Launcher

一款 **HMCL 风格 + PCL2 创新功能** 的《我的世界》第三方启动器，作为 **light 原 GUI 的侧边栏原生面板插件** 运行。

- **纯 C++ / GDI+ 自绘界面，零 JS**：不替换 light 主界面，而是在原 GUI 侧边栏新增一个「⛏ 我的世界启动器」Tab。
- 插件类型：light 的 **YXG ABI 附属插件**（`Cache\gui\dll\*.dll`），导出 `YXG_RegisterPlugin` 并注册**原生面板**。

## 功能

| 页面 | 能力 |
|------|------|
| 启动 | 版本选择 → 内存分配滑块 → 一键启动 + 捕获游戏日志 |
| 版本 | 本地版本 + 远程版本（可搜索）+ 下载安装（BMCLAPI / MCBBS / Mojang 三源）+ 进度条 |
| 账号 | 离线登录（精确离线 UUID）+ 微软正版登录（设备码流程） |
| Mod | PCL2 创新·Mod 前置补充：扫描 mods → 解析 Fabric/Forge 依赖 → 检测缺失前置 → Modrinth 下载 |
| 设置 | 游戏目录、下载源、Java 自动检测、内存；PCL2 创新·背景设置（纯色/单图/多图轮播 + 磨砂玻璃） |

## 目录结构

```
MCLauncher/
├── MCLauncher.sln / .vcxproj    标准 VS 工程（v145, x64, /std:c++20, /MT, /utf-8）
├── include/YXExtension.h        YXG ABI（含原生面板扩展）
├── src/
│   ├── dllmain.cpp              YXG ABI 入口 + 原生面板注册
│   ├── Ui.*                     自绘面板窗口（WS_CHILD，5 页）
│   ├── Theme.* / Background.*   主题 + 背景壁纸
│   ├── LauncherCore.*           版本 JSON/manifest 解析、规则、参数构建、下载编排
│   ├── MsAuth.*                 微软 OAuth 设备码 + XBL/XSTS/Minecraft token
│   ├── JavaDetect.*             Java 自动检测
│   ├── ModManager.*             Fabric/Forge 依赖 + Modrinth
│   ├── Http.* / Json.* / Zip.* / Crypto.*  基础设施
│   └── LauncherApp.*            启动器状态机（异步任务 + 事件回调）
└── build.ps1                    cl.exe/link.exe 手动构建脚本
```

## 部署

1. 把 `bin\x64\Release\YXG_MinecraftLauncher.dll` 复制到 light 的 `Cache\gui\dll\` 目录。
2. （可选）同名 `YXG_MinecraftLauncher.txt` 作为插件介绍。
3. 重启 light → 侧边栏出现「⛏ 我的世界启动器」，点击即打开。

> 要求 light.exe 已包含「原生面板」宿主支持（本仓库同步修改了 `Gui\YXExtension.h`、`Gui\ExtensionManager.*`、`Gui\GuiDllStore.*`、`Gui\YXGuiPlugin.*`）。

## 编译

MSBuild 被环境安全策略拦截，改用 `build.ps1`（cl.exe/link.exe 手动构建）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

## 说明

- **HMCL 兼容**：遵循标准 Mojang version JSON + `.minecraft` 目录（versions/libraries/assets），可与 HMCL 已下载内容互通。
- **PCL2 闭源**（无法直接下载代码），其「背景设置 / Mod 前置补充」为按功能描述自行实现的等价能力；**HMCL 开源**（Java/GPL）可作参考。
- 覆盖 1.6+ 版本（`downloads` 元数据格式）；微软登录、真实下载/启动、Modrinth 下载需联网联调。
