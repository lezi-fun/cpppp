# Contributing to c+++ | 贡献指南

感谢你对 c+++ 的兴趣！以下是一些参与贡献的指南。
Thank you for your interest in contributing to c+++!

## Reporting Bugs | 报告 Bug

- Use GitHub Issues to submit a bug report
- 使用 GitHub Issues 提交 bug
- Include: c+++ version / OS / compiler version / reproduction steps
- 请包含：c+++ 版本 / 操作系统 / 编译器版本 / 复现步骤
- If possible, attach a minimal reproducer
- 如果可能，附上最小复现示例

## Submitting a Pull Request | 提交 Pull Request

1. **Fork** this repo, create your feature/bugfix branch from `main`
2. **Fork** 本仓库，从 `main` 分支创建你的 feature/bugfix 分支
3. **Code style**: keep the existing style — 4-space indent, `{` on the same line
4. **代码风格**：保持现有风格即可 —— 缩进 4 空格，`{` 在同一行
5. **Build check**: make sure `g++ -std=c++17 -Wall -Wextra *.cpp -o c+++` passes with no new warnings
6. **编译检查**：确保 `g++ -std=c++17 -Wall -Wextra *.cpp -o c+++` 通过且无新增警告
7. **Commit messages**: clear and descriptive, in Chinese or English
8. **提交信息**：用中文或英文均可，清晰描述改动意图
9. **One PR, one focus** — don't mix unrelated changes
10. **单个 PR 聚焦一个改动** —— 不要把无关改动混在一起

## Development Environment | 开发环境

```bash
# Build | 编译
g++ -std=c++17 -O2 -Wall -Wextra -o c+++ *.cpp

# Run | 运行
./c+++ --help
./c+++ main.cpp -o app
```

## Manual Testing | 手动测试

The project doesn't have a unit test framework yet. Please describe in your PR which scenarios you manually tested:
项目暂未引入单元测试框架。请在 PR 描述中说明你手动测试了哪些场景：

- `--help` output | `--help` 正常输出
- `./c+++ --new <name>` creates a project skeleton | 生成项目骨架
- `./c+++ <files>` compiles single/multi-file programs | 编译单/多文件
- `--profile` switches profiles | 各种 profile 切换
- `--cmake` reads CMakeLists.txt | 读取 CMakeLists.txt

## License

MIT License — see [LICENSE](LICENSE) | 详见 [LICENSE](LICENSE)。
