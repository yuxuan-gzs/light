# QuickJS MSVC 编译诊断修复

## 本次修改

- 仅对 `quickjs.c` 禁用 MSVC C4146。QuickJS 在 BigInt 与 LEB128 等位运算中会有意使用无符号整数的模运算/位级行为；不改第三方算法实现，避免改变引擎语义。
- 仅对 `quickjs-libc.c` 定义 `_CRT_NONSTDC_NO_WARNINGS`，并将 C4996 排除在该翻译单元的编译诊断之外。这样 `fdopen`、`fileno`、`open`、`close`、`lseek`、`write`、`read`、`isatty`、`unlink`、`strdup` 等 POSIX CRT 名称不再因 MSVC 弃用提示而中断构建。
- 不在全项目范围关闭 C4146/C4996；主程序与其他插件仍保留原本的警告设置。

## 应用方法

1. 将更新后的 `light_builtin_gui.targets` 放到 `light.vcxproj` 同目录，覆盖旧版本。
2. 确认 `light.vcxproj` 在 `Microsoft.Cpp.targets` 前导入它：

   `<Import Project="$(ProjectDir)light_builtin_gui.targets" />`

3. 在 Visual Studio 中选择原来的目标配置，执行“清理解决方案”后重新生成。

本次修改是编译配置层面的针对性处理，不修改 `quickjs.c`/`quickjs-libc.c` 的引擎源码。当前环境无法执行 Windows MSVC，因此仍需在你的 Visual Studio 环境验证实际构建。
