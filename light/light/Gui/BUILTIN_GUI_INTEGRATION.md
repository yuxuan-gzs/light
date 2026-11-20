# light.exe 内置 YXGuiPlugin 集成说明

## 本次源码变更

- `light.cpp` 在启动最早阶段读取 `Cache\\g.xayx`，以现有 `PASSWORD`（即 `sr5r4g464dry8k78665sglnjtr4m5z578tk78q`）进行 XOR 解密。
- 文件不存在时会创建并写入加密后的 `dkg33h4x2f24hdg5x2f2g4:FT12sf`，因此首次启动进入控制台模式。
- 只有解密内容严格等于 `dkg33h4x2f24hdg5x2f2g4:Tdsg2` 才进入 GUI 模式。
- GUI 模式先尝试进程内的 `YXGuiPlugin`；创建窗口失败才回退到 `PluginManager::FindGuiPlugin()` 返回的外部 provider。
- 系统托盘提供“显示主窗口 / 隐藏主窗口 / 退出”，双击图标显示主窗口；窗口关闭按钮默认隐藏到托盘。
- GUI 命令的提示路径更新优先发给当前实际运行的 provider，而不是再次从插件管理器查询外部 GUI。
- `SetGuiMode.ps1` 可生成正确加密的开关文件：`-EnableGui` 启用 GUI，`-Console` 强制控制台模式。
- `light_builtin_gui.targets` 将 `YXGuiPlugin`、`JSPlugin`、`ApiAiNative`、`ExtensionManager`、`PermissionManager` 和 QuickJS 运行时源文件加入主 EXE 的编译项，并链接 WebView2 静态 loader。

## 切换启动模式

在 `light.exe` 所在目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\SetGuiMode.ps1 -EnableGui
```

启用后重新启动 `light.exe`。切回控制台模式运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\SetGuiMode.ps1 -Console
```

程序首次启动时会自动创建默认的控制台模式 `Cache\g.xayx`；此脚本只是方便写入目标加密值，不是程序运行时的额外依赖。

## 集成到现有 Visual Studio 工程

压缩包没有包含 `light.vcxproj`，因此不能替你直接改现有项目文件或产出经过编译验证的 `light.exe`。请把本目录源码合并到实际的 `light` 工程目录，并在 `light.vcxproj` 中、`Microsoft.Cpp.targets` 导入之前加入：

```xml
<Import Project="$(ProjectDir)light_builtin_gui.targets" />
```

将 MSBuild 属性 `WebView2SDKDir` 指向 Microsoft.Web.WebView2 SDK 根目录（该目录下应有 `build\\native\\include` 和 `build\\native\\x64\\WebView2LoaderStatic.lib`）。建议以 x64、Unicode 配置编译。不要再把 `YXGuiPlugin.cpp` 编译成单独的 `YXGuiPlugin.dll` 来满足内置 GUI 的链接需求；它现在应当作为 `light.exe` 的一个编译单元。

此 targets 不替代主程序原有项目配置。原工程仍需保留 `light.cpp` 及它已有的依赖和链接设置（例如 `PluginManager`、`NetworkInterceptor`、`NotifyWindow`、zlib/minizip、LZMA、WinDivert 等）。

## 当前输入包的限制

上传的 ZIP 不含 `light.vcxproj`，也不含 `PluginManager.h/.cpp`、`NetworkInterceptor.h/.cpp`、`NotifyWindow.h/.cpp`、`PluginTemplateGenerator.h/.cpp`、`LightApi.h/.cpp` 和部分第三方库/SDK。因此本包能提供源代码修改和构建项集成文件，但无法在当前输入材料上完成完整的 Windows 编译或验证最终 EXE。

此外，压缩包缺失 `PluginManager` 的实现，因而无法在这里改写它的目录扫描策略来做到“控制台模式完全跳过 GUI DLL 的加载、同时仍然加载普通 DLL 插件”。当前 `light.cpp` 在控制台模式下不会创建或启动 GUI 窗口；`PluginManager::LoadPluginsFromDirectory` 仍保留原有扫描行为。若要做到连 GUI DLL 本身也不加载，需要在完整工程的 `PluginManager` 实现中增加按启动模式过滤 GUI provider 的逻辑。

“零 DLL 发布”指把这些自有 GUI/JS/权限/扩展管理代码编译进 `light.exe`，不再发布 `YXGuiPlugin.dll` 等自有模块。WebView2 仍需要目标系统安装 WebView2 Runtime；`WebView2LoaderStatic.lib` 是构建时静态链接依赖，不应作为发布 DLL。

## 本轮补充：QuickJS 头文件冲突与工程导入

最新编译错误清单中包含 `JSRuntime` / `JSValue` 类型冲突，以及大量成员函数被误解析为非成员函数的连锁报错。本版本补充修正：

- 删除 `Gui/JSPlugin.h` 中不必要的 `struct JSValue;` 前置声明。QuickJS-NG 在 32 位配置下可能把 `JSValue` 定义为 `uint64_t`，前置声明同名 `struct` 会导致类型冲突和大量后续语法报错。`JSRuntime` 与 `JSContext` 的不透明声明保留。
- 修正根目录和 `Gui/` 目录下的 `light_builtin_gui.targets`，确保只把 QuickJS C 运行时文件加入编译，不重复添加 `Gui/*.cpp`。
- 将根目录 `light_builtin_gui.targets` 自动导入 `light.vcxproj`，位置在 `Microsoft.Cpp.targets` 之前；同时为 WebView2 Loader 静态库区分 x64 与 Win32/x86 库目录。

本地结构检查确认 `.vcxproj`、`.vcxproj.filters` 和 `.targets` 均为有效 XML，且包内没有重复添加 GUI `.cpp`。当前容器没有 Windows SDK/MSVC 和 WebView2 SDK，仍无法在这里完成实际 Windows 编译；此修复不代表已生成可发布的 `light.exe`。建议在 Visual Studio 选择 `x64 | Release`，执行“清理解决方案”后再重新生成。若仍出现错误，请从“生成输出”复制最靠前的实际 C/C++ 编译错误，而不是 IntelliSense 错误列表。
