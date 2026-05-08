# c+++ — 现代化 C++ 编译系统

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](https://en.cppreference.com/)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)]()

一个**单源文件即可编译**的 C++ 构建系统。集成交互式配置菜单、AI Agent 非交互模式、增量编译、项目 TOML 支持、对拍测试、编译器自动扫描等功能。

```
c+++ main.cpp utils.cpp -o app -r
```

## 特性

- **零依赖** — 单二进制，不依赖 cmake/make/ninja，只需一个 C++17 编译器即可自举
- **交互式菜单** — `c+++ -s` 进入全键盘菜单，高亮选择、编译器扫描、Hello World 测试
- **AI Agent 模式** — `c+++ --agent` 输出 JSON，零交互，适合 LLM/AI Agent 调用
- **增量编译** — `.c+++_cache` 目录存储对象签名，只重编译变更文件
- **项目 TOML** — 读取 `c+++.toml` 自动识别项目结构、profile、依赖
- **对拍测试** — `--diff brute.cpp --gen gen.cpp` 自动生成数据对比暴力与正解
- **Profile 系统** — 内置 `release` / `debug-sanitizer` / `contest`，可自定义
- **代码分析** — `--analyze` 调用 clang-tidy/cppcheck，`--format` 调用 clang-format
- **compile_commands.json** — `--export-compile-commands` 一键导出给 LSP
- **CMake 兼容** — 读取 `CMakeLists.txt` 自动提取源文件、编译选项、include 路径、链接库并构建
- **模板项目** — `--new my_project` 生成完整项目骨架

## 快速开始

```bash
# 编译 c+++ 自身
g++ -std=c++17 -o c+++ main.cpp builder.cpp menusystem.cpp agent.cpp cmake.cpp

# 安装到 PATH
cp c+++ ~/.local/bin/

# 编译单个文件
c+++ main.cpp

# 编译并运行
c+++ main.cpp -r

# 编译多个文件，指定输出
c+++ main.cpp utils.cpp -o myapp

# 进入交互式配置菜单
c+++ -s

# 查看当前配置
c+++ --show

# 创建新项目
c+++ --new my_project
```

## 使用方式

### 正常构建

```bash
c+++ <files...> [options]
```

| 选项 | 说明 |
|------|------|
| `-o <path>` | 输出文件路径 |
| `--out-dir <dir>` | 输出到目录 |
| `-r`, `--run` | 编译后运行 |
| `-t`, `--temp` | 运行后删除二进制 |
| `--profile <name>` | 使用预设 profile |
| `--pch <header>` | 预编译头 |
| `--auto-deps` | 自动发现依赖源文件 |
| `--git-changed` | 只编译 git diff 中变更的文件 |
| `--analyze` | 调用 clang-tidy / cppcheck |
| `--format` | 调用 clang-format -i |
| `--export-compile-commands` | 生成 compile_commands.json |
| `--time` | 显示运行耗时 |
| `--bench N` | 运行/对拍 N 次 |
| `--valgrind` | 运行时使用 valgrind |
| `--diff <brute> --gen <gen>` | 对拍模式 |
| `--shared` | 编译动态库 |
| `--target` | 分隔多 target |
| `-DX`, `-IX`, `-LX`, `-lX` | 宏 / include / 库路径 / 库 |

### 交互式菜单

```bash
c+++ -s
```

支持：
- 切换 C++ 标准（c++11 ~ c++23）
- 管理编译/链接选项（添加、删除、预设、清空）
- 切换编译器（自动扫描 PATH，显示版本，支持自定义）
- 开关 Sanitizers
- 保存后自动 Hello World 测试编译

操作：`W/S` 或 `↑/↓` 移动，`Enter` 确认，`Q` 返回。

### AI Agent 模式

```bash
c+++ --agent <command> [args...]
```

所有输出为 JSON，零交互，适合 AI Agent / LLM 调用。

```bash
# 查看全部配置
c+++ --agent config show

# 设置编译器 / 标准
c+++ --agent config set compiler g++
c+++ --agent config set std c++23

# 管理选项
c+++ --agent config add copt -march=native
c+++ --agent config del copt -Wall

# 开关 sanitizers
c+++ --agent config sanitizers on

# 应用预设
c+++ --agent config preset release

# 扫描系统中的编译器
c+++ --agent scan

# 非交互构建
c+++ --agent build main.cpp utils.cpp -o myapp

# 构建并运行
c+++ --agent run main.cpp --time
```

完整帮助：`c+++ --agent`

### CMake 支持

自动解析 `CMakeLists.txt`，提取源文件、include 路径、编译选项、链接库并直接构建。

```bash
# 显式指定 CMake 模式
c+++ --cmake

# 指定 target
c+++ --cmake <target_name>

# 自动检测：目录下有 CMakeLists.txt 且无 c+++.toml 时自动使用
c+++
```

支持的 CMake 指令：
`add_executable` | `add_library` | `target_sources` | `target_include_directories` | `target_compile_options` | `target_compile_definitions` | `target_compile_features` | `target_link_libraries` | `set(CMAKE_CXX_STANDARD ...)` | `include_directories` | `file(GLOB ...)`

## 项目结构

```
.
├── logger.h          # 日志系统 + 工具函数 + read_nav_key
├── compiler.h        # CompilerConfig + ConfigManager (配置持久化)
├── build.h           # BuildTarget / BuildOptions / ProjectToml + TOML 解析
├── builder.h         # Builder 声明
├── builder.cpp       # Builder 实现 (构建引擎核心)
├── menusystem.h      # MenuSystem 声明 + Key 枚举 + read_key
├── menusystem.cpp    # MenuSystem 实现 (交互式菜单)
├── agent.h           # AgentMode 声明
├── agent.cpp         # AgentMode 实现 (AI Agent 非交互模式)
├── cmake.h           # CMakeLists.txt 解析器声明
├── cmake.cpp         # CMakeLists.txt 解析器实现
├── main.cpp          # 入口 + show_help / create_project
└── c+++.toml         # (可选) 项目配置文件
```

## 自举

c+++ 可以编译自己：

```bash
c+++ main.cpp builder.cpp menusystem.cpp agent.cpp -o c+++_self
```

## 许可证

MIT
