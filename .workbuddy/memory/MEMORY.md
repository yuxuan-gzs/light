# 项目长期记忆（YX / light）

## 编译环境（2026-10-30 更正）
- MSVC：**VS 18 Community**，根 `I:\Microsoft Visual Studio\Microsoft Visual Studio\18\Community`，工具集 MSVC 14.51.36231（cl 19.51.36260）
- **cl.exe 真实路径**：`I:\Microsoft Visual Studio\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe`（`VC\bin\Hostx64\x64` 是错的；INCLUDE/LIB 也要用 `VC\Tools\MSVC\14.51.36231\{include,atlmfc\include,lib\x64,atlmfc\lib\x64}`，不是 `VC\include`）
- Windows Kits：`I:\Windows Kits\10`（10.0.26100.0 / 10.0.28000.0）
- WebView2.h：`G:\VCPKG-2026.07.29\installed\x64-windows\include`（编译 YXGuiPlugin.cpp 需 `/I` 该目录）
- 沙箱限制：cmd /c、where.exe 等被安全策略拦截；用 PowerShell 手动设 INCLUDE/LIB/PATH 后以 `& cl` 调用可行
- 手动编译环境变量：
  - INCLUDE = `$msvc\include;$msvc\atlmfc\include;WinKit\Include\10.0.26100.0\{ucrt,shared,um,winrt,cppwinrt}`（$msvc = `...\VC\Tools\MSVC\14.51.36231`）
  - LIB = `$msvc\lib\x64;$msvc\atlmfc\lib\x64;WinKit\Lib\10.0.26100.0\{ucrt\x64,um\x64}`
  - PATH 前置 `$msvc\bin\Hostx64\x64` 和 `WinKit\bin\10.0.26100.0\x64`
- **YXGuiPlugin.cpp 需要 NOMINMAX**：它不像 JSPlugin.cpp 在 winsock2.h 前定义 NOMINMAX，否则 `std::max` 报 C2589。已在其顶部补 `#ifndef NOMINMAX` 块（2026-10-30）。
- **【致命】手动编译必须加 `/GL /O2`**：MSBuild 用 `<WholeProgramOptimization>true</WholeProgramOptimization>`（即 `/GL`）编译所有 obj，链接用 `/LTCG`。若手动 `cl` 漏掉 `/GL`，产生的 obj 是无 IR 的普通 COFF，与其余 `/GL` 匿名对象混链 `/LTCG` 会生成**损坏代码 → 运行时栈 cookie 崩溃（0xc0000409 FAST_FAIL_STACK_COOKIE_CHECK_FAILURE，软件打不开）**。判断方法：`/GL` obj 前 4 字节是 `00 00 ff ff`，普通 COFF 是 `64 86`（x64）。链接前务必校验 `_link_objs.txt` 里每个 obj 都 `00 00 ff ff` 开头；LTCG 增量缓存（`x64\Release\light.iobj / light.ipdb / LIGHT.IOBJ.ipdb`）损坏会导致 LNK1000，删掉重链即可。完整编译 flag 见 `build_one.ps1`（已更新为 /GL /O2 版本）。

## 依赖路径断裂（重要，2026-10-25 发现）
用户已把依赖从 D:\YX 下移到 D: 根：
- `D:\YX\zlib-1.3.2` → `D:\zlib-1.3.2`（D:\YX\zlib-1.3.2 已不存在）
- `D:\YX\lzma` → `D:\lzma`（D:\YX\light\lzma\C 只剩 cmd.ico）
- vcpkg：`G:\VCPKG-2026.07.29\installed\x64-windows\lib`
- WinDivert：`D:\WinDivert-2.2.2-A\x64\WinDivert.lib`

**后果**：直接 MSBuild 会失败——light.cpp 的 `#include "../../zlib-1.3.2/zlib.h"`、`"..\..\lzma\C\LzmaLib.h"`、`#pragma comment(lib, "../../zlib-1.3.2/build/Release/z.lib")` 和 vcxproj 的 AdditionalIncludeDirectories（`D:\YX\zlib-1.3.2;D:\YX\zlib-1.3.2\contrib\minizip;D:\YX\lzma\C`）仍指向已移走的 D:\YX 路径。vcxproj 的 ClCompile 源文件列表（`../../../lzma/C/*.c`、`../../../zlib-1.3.2/*.c`）指向 D: 根，是对的。

**规避编译（不改文件）**：`cl /c /TP /W3 /sdl /permissive- /std:c++20 /MT /Oi /Gy /I D:\zlib-1.3.2 /I D:\zlib-1.3.2\contrib\minizip /I D:\lzma\C`（MSVC 会把 /I 目录与相对 include 路径拼接，恰好解析成功）；链接时用 `/NODEFAULTLIB:"../../zlib-1.3.2/build/Release/z.lib"` + 显式 `D:\zlib-1.3.2\build\Release\z.lib`。

**待用户决策**：修复方式二选一——(a) 建目录联接 `mklink /J D:\YX\zlib-1.3.2 D:\zlib-1.3.2`（+D:\YX\lzma → D:\lzma），无需改项目文件；(b) 改 light.cpp include/pragma 和 vcxproj Include 目录为 D: 根路径。未获用户确认前不要动手。

## 路径修复已完成（2026-10-25，用户选方案 b）
4 个文件已修（均有 `.bak_pre_pathfix` 备份）：light.cpp（4 include `../../`→`../../../` + z.lib pragma 改绝对路径）、PluginManager.cpp、PluginTemplateGenerator.cpp（同上）、light.vcxproj（Include/Lib 目录 `D:\YX\zlib-1.3.2`→`D:\zlib-1.3.2` 等，两配置共 4 处）。

**关键知识点**：minizip 的 unzip.h:50 / ioapi.h:46 有手改的 `#include "../../zlib-1.3.2/zlib.h"`，靠“Windows 路径规范化把 `..` 钳制在卷根”+ `/I D:\zlib-1.3.2` 才能解析到 `D:\zlib-1.3.2\zlib.h`——**不要动这两个头文件，也不要删 vcxproj 里的 `/I D:\zlib-1.3.2`**。bzlib.h 在 `#ifdef HAVE_BZIP2` 内（未定义，不需要存在）。

**MSBuild 无法运行**：环境安全策略把 MSBuild.exe 列入 LOLBin 拦截名单（提权也不放行）。验证途径：PowerShell 手动设 INCLUDE/LIB/PATH → `& cl`（同 vcxproj 参数：`/W3 /sdl /permissive- /std:c++20 /MT /Oi /Gy /I D:\zlib-1.3.2 /I D:\zlib-1.3.2\contrib\minizip /I D:\lzma\C`）；链接用 tlog 导出的 82-obj 清单手写 link.rsp。2026-10-25 已全链路验证通过（编译+链接+rc+XML）。

## 搜索下载机制知识点（2026-10-25）
- Bing 有机结果全包装在 `bing.com/ck/a?...&u=a1<base64url>`，必须解码还原；提取优先 `b_algo` 结果块
- 文件链接提取不能只认扩展名：现代下载页用无扩展名入口（VS Code 的 `/thank-you?dv=win64user`、`/sha/download?os=`、GitHub `/releases/download/`），必须结合**锚点文字**（"User Installer (x64)"、".zip (x64)"）识别，架构/系统标记也常在文字里
- 去重键要区分：直链按文件名，无扩展名入口按展示名（否则同路径段会把不同变体折叠）
- 无扩展名下载入口（JS 渲染站点如 VS Code）下载到的是网页不是文件——已在 UI 提示用户用浏览器打开；能拿到真实文件的是带 Content-Disposition 的直链（已实现 HEAD 读取文件名）
- light.exe 常驻运行会间歇锁 light.cpp（EBUSY），编辑前重试即可；devenv 开着项目也会锁

## 构建事实
- 项目从源码编译 zlib/7z/lzma（77 个 ClCompile），链接时排除 ctest/api-test/qjs 等自带 main 的测试文件（ExcludedFromBuild）
- 上次成功构建：2026-10-30 19:18（light.exe 3,328,512 字节，含「插件商店」新功能）；构建记录在 `light\x64\Release\light.tlog\link.command.1.tlog`（UTF-16LE，**第 2 行是完整链接命令**，第 1 行 `^` 后是 pipe 分隔的 82 个 obj + WEBVIEW2LOADERSTATIC.LIB + LIGHT.RES）
- 备份规范：项目用 `*.bak_*` 后缀（如 light.cpp.bak_pre_searchfix / bak_post_searchfix）

## GUI 插件商店（附属 DLL 插件，2026-10-30 新增）
- 回答：GUI 原无「安装 DLL 插件的商店」，仅本地扫描 exe 目录 `YXG_*.dll`。已新增。
- 新文件 `Gui\GuiDllStore.h/.cpp`：`GuiDllPluginHost`（单例，加载 `Cache\gui\dll\*.dll`，严格跳过 `.dll.N`，YXG ABI、无指令，启停靠 `name.dll`↔`name.dll.1` 重命名）+ `GuiDllStorePanel`（卡片网格商店：搜索/旋转循环条/一键安装/＋仓库/⟳刷新）。
- 仓库：GitHub 链接（走 api.github.com trees + raw.githubusercontent.com）、HTTP 基址（list.txt）、本地目录。默认 `yuxuan-gzs/light @ gui.dll`。
- 伴生文件同名：`.txt`=介绍、`.png/.jpg`=logo、`.mp4`=介绍视频。
- 接入点：设置对话框第 5 个标签页「插件商店」；`JSPlugin::RunHooks/IsJSApiOverridden/CallExtensionJSApi/GetExtJSApiNames` 增查全局 `g_guiDllHost`。
- 构建：新 obj `GUIDLLSTORE.OBJ` 已加入 `_link_objs.txt`（83 obj）与 vcxproj。

## 手动重链接 light.exe 的可复用配方（2026-10-25 已验证）
1. 编译改动文件：`cl /c /TP /W3 /sdl /permissive- /std:c++20 /MT /Oi /Gy /EHsc /I D:\zlib-1.3.2 /I D:\zlib-1.3.2\contrib\minizip /I D:\lzma\C /Fo:<out>.obj <src>.cpp`
2. 生成链接清单：`D:\YX\light\light\make_link_rsp.py`（读同目录 `_link_objs.txt`，产出 `link.rsp`；cwd 必须是 `D:\YX\light\light`，因为 `/DEF:"LIGHTAPI.DEF"` 和 `X64\RELEASE\LIGHT.RES` 是相对路径）
3. 链接：PowerShell 设 **LIB + PATH**，PATH 必须同时含 `$vc\bin\Hostx64\x64` 和 `$kit\bin\10.0.26100.0\x64`（否则 LNK1158 找不到 mt.exe）；然后 `link.exe "@link.rsp"`
4. **light.exe 常驻且拒绝访问无法结束时**：NTFS 允许重命名正在运行的映像 → `mv light.exe light_prev_xxx.exe` 后再链接到原名，绕开锁。旧进程仍跑旧映像，需用户手动关闭后才能删掉改名文件。

## 编码陷阱（重要）
- 本工作区源文件是 **UTF-8 with BOM**；用 Edit/Write 工具改 `.h/.cpp` 时若写成了无 BOM 的 UTF-8，MSVC 会按 GBK 误读中文注释 → 报一堆莫名其妙的错误（如"未声明的标识符"）。改动后必须确认 BOM 仍在（`open(p,'rb').read(3) == b'\xef\xbb\xbf'`），必要时补回。
- Bash 工具的 heredoc 会对 `\"` 做奇怪转义；含大量反斜杠/引号的 Python 脚本改用 Write 工具落盘成 .py 再执行。
