# Contributing to c+++

感谢你对 c+++ 的兴趣！以下是一些参与贡献的指南。

## 报告 Bug

- 使用 GitHub Issues 提交 bug
- 请包含：c+++ 版本 / 操作系统 / 编译器版本 / 复现步骤
- 如果可能，附上最小复现示例

## 提交 Pull Request

1. **Fork** 本仓库，从 `main` 分支创建你的 feature/bugfix 分支
2. **代码风格**：保持现有风格即可 —— 缩进 4 空格，`{` 在同一行
3. **编译检查**：确保 `g++ -std=c++17 -Wall -Wextra *.cpp -o c+++` 通过且无新增警告
4. **提交信息**：用中文或英文均可，清晰描述改动意图
5. **单个 PR 聚焦一个改动** —— 不要把无关改动混在一起

## 开发环境

```bash
# 编译
g++ -std=c++17 -O2 -Wall -Wextra -o c+++ *.cpp

# 运行
./c+++ --help
./c+++ main.cpp -o app
```

## 测试

项目暂未引入单元测试框架。请在 PR 描述中说明你手动测试了哪些场景：

- `--help` 正常输出
- `./c+++ --new <name>` 生成项目骨架
- `./c+++ <files>` 编译单/多文件
- `--profile` 各种 profile 切换
- `--cmake` 读取 CMakeLists.txt

## License

MIT License — 详见 [LICENSE](LICENSE)。
