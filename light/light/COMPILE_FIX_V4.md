# light.exe 第四轮编译错误修复

## 本轮针对的错误

这轮按 `粘贴的文本 (1)(6).txt` 的实际编译日志处理，不只是关闭警告：

- `Gui/JSPlugin.cpp:1010` 的 `wcsicmp` 改为 MSVC 对应的 `_wcsicmp`，并显式包含 `<wchar.h>`。
- `JSPlugin.h` 增加 `IsPermanentlyAuthorized()`、`GetVersion()`、`GetFolderPath()`、`GetPluginList()` 声明，解决实现代码调用了不存在的成员而引发的连锁错误。
- 补充 `JSPlugin::AllocControlId()`，为原生控件分配原子递增的子控件 ID。
- 实现 `GetPluginList()`：从当前插件目录的同级目录读取 `plugin.json` 的显示名；原生/内置插件则以 `light.exe` 目录下的 `Cache\gui` 为扫描位置，并确保当前插件名包含在结果中。
- `Gui/YXGuiPlugin.h` 改为显式包含 `../PluginInterface.h`，避免 `Gui/PluginInterface.h` 与根目录同名 ABI 头之间的查找歧义；根 ABI 中另加显式 `PluginUIManager` 前置声明。

## 应用

本 ZIP 是以用户最新上传的 `当前代码.zip` 为基础的完整源码快照。可以整体备份后覆盖，也可以使用同包中的 `light_compile_fix_v4.patch` 只合并差异。

在 Visual Studio 中对原有 `x64 | Release` 配置执行“清理解决方案”再“重新生成”。

## 验证边界

已检查修改文件的文本结构、相关成员声明/定义对应关系，以及 `.vcxproj` / `.targets` XML 是否有效。当前执行环境没有 Windows SDK、MSVC 和 WebView2 SDK，因此没有完成真实 Windows 编译；不能据此宣称已经生成或验证 `light.exe`。若仍报错，请发最新日志中最前面的真实 C/C++ 编译错误，以便继续针对实际代码修复。
