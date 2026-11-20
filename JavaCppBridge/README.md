# LangBridge — Java/C++ 双向代码转换器

一个用 C++ 17 编写的命令行工具：将 **Java 源文件自动转换为 C++ 源文件**，或将 **C++ 源文件自动转换为 Java 源文件**。内置词法/语法分析器与两类代码生成器，不依赖外部编译器即可完成转换；转换结果可被本机 g++/MSVC/javac 进一步编译运行验证。

> 用 `java2cpp`、`cpp2java` 两种模式覆盖以下特性：类与继承、接口/纯虚类、泛型（模板）、重载、静态成员、异常处理、增强 for / range-for、`String`↔`std::string`、`List/Map`↔`std::vector/std::map`、`System.out`↔`std::cout` 等通用语法子集（详见「支持范围」）。

---

## 目录结构

```
JavaCppBridge/
├── include/bridge.hpp        # 全工程统一头文件（AST、符号表、生成器接口）
├── src/                      # 源码
│   ├── lexer.cpp             # 词法分析：统一 Tokenizer（注释、字符串、数字、标识符…）
│   ├── parser.cpp            # 递归下降语法分析器（带行号错误报告）
│   ├── analyze.cpp           # 语义层：作用域符号表 + Java↔C++ 类型映射
│   ├── gen_common.cpp        # 两个生成器共用的辅助（类型名、容器改写、缩进等）
│   ├── gen_j2c.cpp           # Java → C++17 代码生成器
│   ├── gen_c2j.cpp           # C++ → Java 代码生成器
│   └── main.cpp              # 命令行入口（--mode/-i/-o/--stdin/--stdout/-h/-V）
├── examples/                 # 双向示例
│   ├── java/ShapeDemo.java   # Java 示例（接口/泛型/容器/异常/重载/switch…）
│   ├── cpp/ShapeDemo.cpp     # C++ 示例（纯虚类/模板/容器/字符串…）
│   └── cpp/FeatureProbe.cpp  # C++ 特性探针（预处理/命名空间/全局变量/枚举…）
├── tests/
│   ├── expect/               # 回归期望输出
│   │                         #   ShapeDemo_j2c.txt / ShapeDemo_c2j.txt
│   │                         #   FeatureProbe_c2j.txt
│   └── …probe.cpp            # 开发期探测件
├── tools/                    # 构建与回归脚本
│   ├── build.sh              # 跨平台构建（自动探测 cl / g++ / clang++）
│   ├── build_local_msvc.bat  # Windows 本机 MSVC 一键构建
│   ├── run_regression.sh     # 跨平台回归（g++/clang++ + javac）
│   ├── run_regression_msvc.bat # Windows 本机 MSVC 全量回归
│   └── normalize.ps1         # 行尾归一化（黄金文件比对前用）
├── Makefile                  # make 构建（g++/clang++，MSVC 请用 .bat）
└── build/                    # 构建产物与临时文件（可整体删除）
```

---

## 构建

环境要求：C++17 编译器（MSVC ≥ VS2017 或 g++/clang++）、可选 JDK（仅验证 Java 生成物时用）。

**Windows（MSVC，本机推荐）**

```bat
tools\build_local_msvc.bat
rem 产物：build\langbridge.exe
```

**跨平台（git-bash / Linux / macOS）**

```bash
bash tools/build.sh        # 自动探测 cl（需已在 PATH）→ g++ → clang++
# 或
make                      # 通过 Makefile 构建（会跳过 MSVC，见下方说明）
```

> 说明：MSVC 的 `cl` 需要 `vcvars64.bat` 提供的环境变量（INCLUDE/LIB），无法在普通 git-bash 中直接探测到可用实例，因此 Windows 上请使用 `tools/build_local_msvc.bat`（已内置该环境）或 `tools/run_regression_msvc.bat`。Makefile 与 `tools/build.sh` 面向 g++/clang++ 及已在 PATH 中的 cl。

---

## 用法

```
langbridge --mode <java2cpp|cpp2java> -i <输入文件> [-o <输出文件>]
langbridge --mode <java2cpp|cpp2java> --stdin [--stdout]
```

| 参数 | 说明 |
| --- | --- |
| `-m, --mode <mode>` | 转换方向：`java2cpp` 或 `cpp2java`（必填） |
| `-i, --input <file>` | 输入源文件 |
| `-o, --output <file>` | 输出文件；缺省时按输入文件名改扩展名（`.java`↔`.cpp`）写到当前目录 |
| `--stdin` / `--stdout` | 从标准输入读取 / 写入标准输出 |
| `-h, --help` | 帮助；`-V, --version` 版本 |

退出码：`0` 成功；`1` 转换或解析错误（错误信息含文件与行号）；`2` 参数错误。

示例：

```bash
# Java → C++
build/langbridge --mode java2cpp -i examples/java/ShapeDemo.java -o out.cpp
# C++ → Java
build/langbridge --mode cpp2java -i examples/cpp/ShapeDemo.cpp -o Out.java
# 管道
cat in.java | build/langbridge --mode java2cpp --stdin --stdout > out.cpp
```

转换产物可直接交本机编译器使用：

```bash
g++ -std=c++17 -o demo out.cpp && ./demo       # 验证生成的 C++
javac -encoding UTF-8 Out.java && java Out     # 验证生成的 Java（类名固定为 Main 时存为 Main.java）
```

---

## 支持范围

**双向公共子集**

- 类、继承（单继承）、`public/private/protected` 成员、静态成员与静态方法
- 构造、析构（Java 侧 `finalize` 不映射）
- 方法重载（按参数个数/类型区分）、覆写
- 基本类型（`int/long/double/float/boolean/char/byte/short`）与字面量
- `String` ↔ `std::string`：`length/size`、`charAt`↔`operator[]`/`at`、`substring`↔`substr`、`indexOf`↔`find`、`equals`↔`==`（重载比较）、`startsWith/endsWith` 等
- 容器：`List`↔`std::vector`、`Map`↔`std::map`，增删/遍历/取值 API 改写
- 控制流：`if/else`、`for`、`while`、`do-while`、`switch`（Java String switch 降级为 if-else 链）、`break/continue`
- 表达式：四则/比较/逻辑/三目/自增自减/一元、括号与优先级
- 异常：`try/catch/finally` ↔ `try/catch` + `std::runtime_error`（`finally` 语义近似，见限制）
- 泛型（Java）↔ 模板（C++），含受限类型参数与泛型方法
- 接口（Java）↔ 纯虚基类（C++）；`implements` ↔ `: public VirtualBase`
- 静态字段常量（`static final int` ↔ `static constexpr` / `static const`）
- 数组（元素类型 + 长度可映射的基础子集），`new int[n]` 初始化
- 注释、包名/`using namespace` 处理的保守策略（见限制）

**Java→C++ 额外能力**

- `System.out.println/print` → `std::cout << … << endl`（含换行/不换行两种）
- `String` 方法族改写（`toUpperCase/toLowerCase` 等简单直译除外，数学函数不做 std 前缀映射）
- `Integer.parseInt` → `std::stoi`，`Math.PI` 类常量映射
- 增强 `for (T x : list)` → `for (auto&& x : vec)`；`Map.entrySet()` 迭代 → `kv.first/kv.second`

**C++→Java 额外能力**

- `std::cout << x << endl` → `System.out.println` 链式改写（含 `endl` 结尾分支）
- `std::vector<T>` → `ArrayList<T>`（含 `push_back`→`add`、`size`→`size` 等）
- `std::map` → `HashMap`（`m[k]` 读 → `get`、写 → `put`）、`std::string` 方法映射
- 纯虚类/抽象 → `abstract class`，析构函数 `~X()` → 不生成（按 Java 惯例）
- `std::stoi` → `Integer.parseInt`，`std::invalid_argument` → `NumberFormatException` 等异常改写
- **真预处理器**：对象宏与函数宏展开（含 hideset 防递归）、`#if/#ifdef/#ifndef/#elif/#else/#endif`
  求值（支持 `defined X` 与 `defined(X)`、`==`、`>=`、`&&`、`||`、`!`、`<<` 等）、`#include` 收集为注释、
  `#pragma` 静默丢弃、内建 Win32 宏表（`RGB`、`TRUE/FALSE`、`WM_APP`、`HRESULT` 等）
- **字符串字面量**：`L"…"` / `u8"…"` / `u"…"` / `U"…"` 前缀，以及相邻字面量拼接（`"a" "b"` → `"ab"`）
- **类外成员函数定义**：`void Foo::bar()`、`Foo::Foo()`、`Foo::~Foo()` 会挂回目标类；
  目标类不在本文件时按警告合成空类以保住函数体
- **文件作用域全局变量 / 自由函数** → 折叠为 `Main` 类的 `static` 字段与 `public static` 方法
  （`extern` 声明与定义会合并，`main` 变成 `public static void main(String[] argv)`）
- **命名空间** → 同名的持有者类（`YXStyle::DARK_BG` → `YXStyle.DARK_BG`）；命名空间内的
  全局变量与自由函数在同类内部互相引用时会自动补上 `Holder.` 前缀
- **枚举** → Java `enum`；枚举常量在自身体之外引用时自动补 `Enum.CONST`；
  枚举到整型的 C 风格转换改写为 `.ordinal()`
- **调用约定与导出宏**：`CALLBACK/WINAPI/APIENTRY/__stdcall/__declspec(...)` 等跳过；
  未在文件内定义、形如 `PLUGIN_API` 的全大写未知宏也按导出说明符跳过（否则整条声明会失败）
- 表达式细节：一元 `&`/`*`、`>>`/`>>=`/`<<=`、`static_cast/reinterpret_cast/const_cast/dynamic_cast`、
  `::name` 全局限定、带模板实参的调用、`catch (...)` → `catch (Throwable)`、`(void)x` → 空语句
- 类型映射补充：`std::wstring`→`String`、`std::atomic<T>`→`T`、`std::thread`→`Thread`、
  `std::function`→`Object`，以及常用 Windows typedef（`HANDLE/HWND/DWORD/BOOL/HRESULT/COLORREF/…`）

---

## 已知限制

以下场景**不支持或为近似转换**，使用前请留意：

1. **绑定到变量的 lambda**（`auto f = [](int x){ return x + x; }; f(v);`）无法转换：Java 不能对
   lambda 使用 `var` 推断，也没有结构化函数类型，生成物 `var f = (int x) -> …` 无法通过 `javac`。
   直接内联传给函数的 lambda 同样受此限制。
2. `sizeof` 无 Java 对应物 → 发射为 `0`；`c_str()` → `null`；`std::swap` → `null`。
3. 迭代器 `begin()/end()` 不支持，请用 range-for；`std::pair/tuple/array` 退化为 `Object`；`instanceof` 未支持。
4. 显式枚举常量值（`MODE_FAST = 1`）会被丢弃，Java 侧按声明顺序取 `ordinal()`，
   因此枚举到整型的转换结果可能与原值不同。
5. Java 包私有（无修饰符）成员 → C++ `private`；`finally` 语句块被跳过（仅近似保留 try/catch 语义）。
6. Java `String switch` → C++ 中按 if-else 链近似；`switch` 内浮点/字符串跳转不保真。
7. 多态与对象**值语义近似**：基类指针/引用虚调用保留虚函数机制，但对象按值拷贝的场合语义不完全等价。
8. `String.split` 按字面分隔符处理，不做正则；`Scanner` 输入流 API 近似。
9. Java 数组字面量 `new int[]{…}` 解析不支持（建议改用循环填充）。
10. C++ → Java 时，生成文件固定为 `public class Main`（转换结果需存为 `Main.java` 才能用 `javac` 直接编译）；
    命名空间持有者类是非 `public` 的，因此不能单独成文件。
11. 接口成员 `public` 修饰符、泛型类模板头、Java 静态字段引用回退为 `Class::field`：由生成器强制/补齐处理，风格与手写代码不同。
12. 运算符重载、`friend`、模板特化/偏特化、`enum class` 的部分用法、`goto`、位域、`union`
    不在公共子集内，会被跳过并给出警告（不会中断整个文件的转换）。
13. `#include` 只收集不展开：被包含头文件里的宏/类型若未在文件内定义，相关声明可能退化为导出宏跳过
    或未知类型近似。外部宏可在转换前用 `-D` 之类方式自行展开为源码再输入。

---

## 测试与回归

```bash
# Windows（MSVC + javac 全量回归：构建 CLI→双向转换→编译→运行→比对期望输出）
tools\run_regression_msvc.bat

# 跨平台（自动用 g++/clang++ 编译运行生成的 C++；有 javac 则验证 Java）
bash tools/run_regression.sh
```

回归脚本会遍历 `examples/` 下的全部示例：

1. `examples/java/*.java` → 生成 C++，用本机编译器编译并运行，与 `tests/expect/<名>_j2c.txt` 比对；
2. `examples/cpp/*.cpp` → 生成 Java，用 `javac` 编译并运行，与 `tests/expect/<名>_c2j.txt` 比对；
3. 汇总 PASS/FAIL。

目前覆盖 `ShapeDemo`（双向）与 `FeatureProbe`（C++→Java）。`FeatureProbe.cpp` 本身也是合法的
C++17 程序，`cl /std:c++17` 编译运行的结果与生成的 Java 运行结果逐字节一致。

> 比对前会用 `tools/normalize.ps1` 把实测输出归一化为「LF + 末尾一个换行」，
> 否则 Windows 上 Java `println` 的 CRLF 会让黄金文件比对误报。

## 已知环境提示

- 本仓库开发环境（Windows + MSVC 14.51 / VS2022 Community）已验证：生成的 C++ 经 `cl` 编译运行输出与 Java 侧输出语义一致（见 `tests/expect/`）。
- IDE 内若缺少 `/I include`（如 Visual Studio Code + clangd），源码会报“bridge.hpp not found”一类噪音，以 `cl` / `g++` 真实编译结果为准。