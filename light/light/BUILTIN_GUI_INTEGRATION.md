# light.exe 内置 GUI / 编译错误修复说明

## 本包针对的编译错误

根据 `粘贴的文本 (1)(4).txt`，大量 C3861/C2039/C2065 错误的共同根因是 `Gui/JSPlugin.h` 与 `Gui/JSPlugin.cpp` 版本不匹配：实现里已经使用了 manifest、QuickJS runtime、控件视觉状态、权限回调、HTTP Promise、扩展管理和 WebView 桥接成员，但头文件未声明它们。本包重建了对应接口和成员，并补充 `HttpTaskDoneMsg` 与 `ApiSlot`。

另外，本包修复：

- `Gui/PluginInterface.h` 改为转发到项目根目录的 `PluginInterface.h`，避免 `IPlugin`、`IGuiProvider`、`PluginInfo` 等 ABI 类型被定义两次。根目录的 `PluginInterface.h` 必须保留，不能用本转发头替换它。
- `JSPlugin.cpp` 在包含 Winsock/Windows 头文件前定义 `NOMINMAX`，避免 Windows `max` 宏干扰 `std::max`。
- `YXGuiPlugin.cpp` 显式使用 `Gdiplus::Rect` 消除 `Graphics::FillRectangle` 重载歧义。
- `light.cpp` 将 `LoadIconW` 的资源参数改为宽字符资源指针，避免 `LPSTR` 到 `LPCWSTR` 类型不匹配。
- `light_builtin_gui.targets` 不再重复加入 `.filters` 中已经属于 `light` 项目的 `Gui/*.cpp`，只加入当前 filters 未列出的 QuickJS C 运行时源文件。

## 文件放置

将本包中的 `light.cpp`、`quickjs*.c/.h`、`dtoa.*`、`libregexp.*`、`libunicode.*` 等放到项目根目录；将 `Gui/` 目录中的文件覆盖到项目的 `Gui/` 目录。保留原有根目录 `PluginInterface.h` 和主程序的其他依赖文件。

在 `light.vcxproj` 的 `Microsoft.Cpp.targets` 导入之前添加：

```xml
<Import Project="$(ProjectDir)light_builtin_gui.targets" />
```

为 `WebView2SDKDir` 设置 Microsoft.Web.WebView2 SDK 路径，并使用 x64 / Unicode 配置。不要再把同一份 `Gui/*.cpp` 通过另一组 `<ClCompile Include>` 重复加入项目；你上传的 `light.vcxproj.filters` 已列出了这些 GUI 源文件。

## 启动模式

- 第一次启动时，`Cache\g.xayx` 不存在会写入加密的 `dkg33h4x2f24hdg5x2f2g4:FT12sf`，默认控制台模式。
- 只有解密后严格等于 `dkg33h4x2f24hdg5x2f2g4:Tdsg2` 才进入 GUI 模式。
- 在 `light.exe` 同目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\SetGuiMode.ps1 -EnableGui
powershell -ExecutionPolicy Bypass -File .\SetGuiMode.ps1 -Console
```

- GUI 启动先尝试内置 `YXGuiPlugin`，内置创建失败才回退 `PluginManager::FindGuiPlugin()`。
- GUI 主窗口提供托盘显示/隐藏/退出，双击托盘图标会显示窗口。

## 验证边界

本包按编译错误清单修正了可由上传源码确认的问题，但当前上传内容不包含 `light.vcxproj`、根目录 `PluginInterface.h` 的完整内容、`PluginManager` 和若干主程序依赖/SDK，因此不能在这里运行真实的 MSVC 工程构建，也不能宣称已生成或验证最终 `light.exe`。集成后如果出现新错误，请提供新的完整首批编译错误；优先处理最早出现的错误，而不是最后的连锁错误。

“零 DLL 发布”指不再发布自有的 `YXGuiPlugin.dll` / JS 插件宿主 DLL；其它 `plugins\*.dll` 普通插件仍由主程序现有插件管理器加载。WebView2 运行时依然需要安装在目标系统上，静态 Loader 库只负责避免额外发布 `WebView2Loader.dll`。

## 本轮补充：QuickJS 头文件冲突与工程导入

最新编译错误清单中包含 `JSRuntime` / `JSValue` 类型冲突，以及大量成员函数被误解析为非成员函数的连锁报错。本版本补充修正：

- 删除 `Gui/JSPlugin.h` 中不必要的 `struct JSValue;` 前置声明。QuickJS-NG 在 32 位配置下可能把 `JSValue` 定义为 `uint64_t`，前置声明同名 `struct` 会导致类型冲突和大量后续语法报错。`JSRuntime` 与 `JSContext` 的不透明声明保留。
- 修正根目录和 `Gui/` 目录下的 `light_builtin_gui.targets`，确保只把 QuickJS C 运行时文件加入编译，不重复添加 `Gui/*.cpp`。
- 将根目录 `light_builtin_gui.targets` 自动导入 `light.vcxproj`，位置在 `Microsoft.Cpp.targets` 之前；同时为 WebView2 Loader 静态库区分 x64 与 Win32/x86 库目录。

本地结构检查确认 `.vcxproj`、`.vcxproj.filters` 和 `.targets` 均为有效 XML，且包内没有重复添加 GUI `.cpp`。当前容器没有 Windows SDK/MSVC 和 WebView2 SDK，仍无法在这里完成实际 Windows 编译；此修复不代表已生成可发布的 `light.exe`。建议在 Visual Studio 选择 `x64 | Release`，执行“清理解决方案”后再重新生成。若仍出现错误，请从“生成输出”复制最靠前的实际 C/C++ 编译错误，而不是 IntelliSense 错误列表。

## 本轮大修（2026-10）改动说明

### 1. 外置 GUI 插件加载修复
- `PluginManager::LoadPluginDll` 不再强制要求 `CreatePlugin`：只有 `CreateGuiProvider` 的 GUI-only DLL 现在也能被加载并注册为 GUI 提供者（旧机制会直接加载失败）。
- `light.cpp` 的 GUI 启动顺序改为按偏好配置：`Cache\gui_provider.txt`（`auto` = 外置优先、无外置回退内置；`external` = 外置优先；`builtin` = 强制内置）。可用命令 `guiprovider <auto|external|builtin>` 切换，重启生效。
- 与内置 GUI 同 ID（`com.yx.gui`）的旧版外置 DLL 会被视为重复界面自动跳过，避免旧 UI 掩盖新界面。
- 修复 PluginManager 回调在持有互斥锁时调用（死锁隐患）、`SetUIManager`/`RegisterGuiProvider` 改为锁外回调。

### 2. api_*.dll 新增指令集（静默 + 安全扫描）
- 启动时（打印“正在初始化配置状态...”阶段）静默扫描程序目录下所有 `api_*.dll`：校验 PE 合法性/位数、递归扫描导入的附属 DLL（同目录、`plugins\`、`api_deps\`、`lib\`）、检测高风险导入（CreateRemoteThread / WriteProcessMemory / SetWindowsHookEx 等）、数字签名识别；不安全的插件静默拒绝（仅 OutputDebugString 记录）。
- 模板生成：控制台 `createplugin <名称> -api` 生成完整可编译工程（`include\PluginInterface.h` + `src\api_<名称>.cpp` + CMake + build.bat + README）；内置 GUI 的“设置 → 插件中心”页也有“＋ 生成 API 插件模板”按钮。
- 附带修复 `PluginTemplateGenerator` 引用未初始化的 `Dir` 导致 ZIP 输出路径为空的漏洞；ZIP 解压增加 Zip Slip 防护；插件重复加载/重复 ID 检测修复。

### 3. 内置 GUI 界面修改
- 左侧侧边栏按钮改为紧凑卡片：高度 = 宽度的一半，直角无圆角；新增“光语 AI”内置入口。
- **API_AI 彻底内置化并更名为「光语 AI」**：不再作为 JS 插件出现在插件列表；id 改为 `builtin.light.ai`；旧配置 `Cache\gui\api_ai.m` 自动迁移为 `light_ai.m`；历史/壁纸路径保持兼容。
- 设置面板重构为现代化样式：左侧深色导航栏（自绘）、内容区大标题 + 副标题、自绘圆角按钮/列表行、可调整窗口大小；“插件中心”页内置新的 `PluginCenterPanel` 类（卡片式列表、启用/停用开关、打开目录/重载、右键菜单、滚动条），统一管理内置功能（光语 AI）、api_ 指令集、GUI 插件与 JS 扩展。
- 修复设置窗口析构时未销毁子窗口、窗口句柄泄漏等问题。

### 4. 命令行为新增
- `light.exe xxx`：直接进入指令功能并退出（非管理员、不初始化 GUI/服务/网络拦截）。
- 指令前加 `cmd `：把剩余部分交给 cmd 指令集执行，输出显示在 light 窗口内（控制台模式直接附着当前控制台，GUI 模式捕获输出写入 GUI 控制台）。
- 未知指令回退：在当前 cd 目录查找同名文件（含 PATHEXT 扩展），找到后打开；`.bat`/`.cmd` 会在 light 窗口内执行并把未知指令参数传入。
- 修复 `main` 中配置初始化分支 `else if (!fileExists)` 永远不可达（死代码）导致首次环境配置不执行的问题。
