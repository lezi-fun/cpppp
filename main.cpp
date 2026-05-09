#include "builder.h"
#include "menusystem.h"
#include "agent.h"
#include "cmake.h"

static void show_help() {
    std::cout << "用法: c+++ <files...> [options]\n\n"
              << "构建:\n"
              << "  c+++ main.cpp utils.cpp -o app\n"
              << "  c+++ main.cpp --out-dir build\n"
              << "  c+++ lib.cpp -o mylib.so --shared\n"
              << "  c+++ main.cpp --target lib.cpp -o mylib.so --shared\n"
              << "  c+++ --profile release | --profile debug-sanitizer | --profile contest\n"
              << "  c+++ main.cpp --pch pch.hpp --auto-deps --git-changed\n\n"
              << "选项:\n"
              << "  -D X, -IX, -LX, -lX      宏、include、库路径、库\n"
              << "  --analyze                调用 clang-tidy 或 cppcheck\n"
              << "  --format                 调用 clang-format -i\n"
              << "  --export-compile-commands 生成 compile_commands.json\n"
              << "  -r, --run                编译后运行\n"
              << "  --time                   显示运行耗时\n"
              << "  --bench N                运行/对拍 N 次\n"
              << "  --valgrind               运行时使用 valgrind\n"
              << "  --diff brute.cpp --gen gen.cpp  对拍模式\n\n"
              << "项目:\n"
              << "  c+++                     读取 c+++.toml 或 CMakeLists.txt\n"
              << "  c+++ --cmake [target]    从 CMakeLists.txt 构建\n"
              << "  c+++ --new my_project    生成模板项目\n"
              << "  c+++ -s                  交互式配置菜单\n"
              << "  c+++ --show              显示当前配置\n"
              << "  c+++ --agent <cmd>       AI Agent 非交互模式\n";
}

static bool create_project(const std::string& name) {
    fs::path root = name;
    fs::create_directories(root / "src");
    fs::create_directories(root / "include");
    fs::create_directories(root / "tests");
    std::ofstream(root / "src" / "main.cpp") << "#include <iostream>\n\nint main() {\n    std::cout << \"Hello c+++\" << std::endl;\n    return 0;\n}\n";
    std::ofstream(root / "c+++.toml")
        << "[build]\n"
        << "compiler = \"g++\"\n"
        << "std = \"c++23\"\n"
        << "sources = [\"src/*.cpp\"]\n"
        << "include = [\"include\"]\n"
        << "output = \"build/app\"\n\n"
        << "[profile.release]\n"
        << "flags = [\"-O3\", \"-flto\", \"-march=native\"]\n\n"
        << "[profile.debug]\n"
        << "flags = [\"-g\", \"-O0\", \"-fsanitize=address,undefined\"]\n";
    Logger::log(Logger::SUCCESS, "已生成项目: " + root.string());
    return true;
}

static void show_config(ConfigManager& cm) {
    cm.load();
    std::cout << "编译器: " << cm.config.compiler_bin
              << "\n标准: " << cm.config.cpp_standard
              << "\nSanitizers: " << (cm.config.enable_sanitizers ? "开启" : "关闭")
              << "\n编译选项: ";
    for (auto& o : cm.config.compile_options) std::cout << o << " ";
    std::cout << "\n链接选项: ";
    for (auto& o : cm.config.link_options) std::cout << o << " ";
    std::cout << "\n宏定义: ";
    for (auto& o : cm.config.macros) std::cout << "-D" << o << " ";
    std::cout << "\nInclude: ";
    for (auto& o : cm.config.include_paths) std::cout << "-I" << o << " ";
    std::cout << "\n库路径: ";
    for (auto& o : cm.config.library_paths) std::cout << "-L" << o << " ";
    std::cout << "\n库: ";
    for (auto& o : cm.config.libraries) std::cout << "-l" << o << " ";
    std::cout << "\nProfiles: ";
    for (auto& [name, _] : cm.config.profiles) std::cout << name << " ";
    std::cout << "\n";
}

static int clean_cache(bool json = false) {
    bool cache_existed = fs::exists(".c+++_cache");
    std::error_code ec;
    fs::remove_all(".c+++_cache", ec);
    fs::remove("compile_commands.json", ec);
    if (json) {
        std::cout << "{\"cleaned\":" << (cache_existed ? "true" : "false") << ",\"error\":" << (ec ? "true" : "false") << "}\n";
    } else if (ec) {
        Logger::log(Logger::ERROR, "清理失败: " + ec.message());
        return 1;
    } else {
        Logger::log(Logger::SUCCESS, "清理完成");
    }
    return ec ? 1 : 0;
}

static int doctor(ConfigManager& cm) {
    cm.load();
    Logger::print_title("Doctor 环境诊断");
    std::cout << "compiler: " << cm.config.compiler_bin << "\n";
    std::cout << "standard: " << cm.config.cpp_standard << "\n";
    std::cout << "compiler_found: " << (command_exists(cm.config.compiler_bin) ? "yes" : "no") << "\n";
    int rc = 1;
    std::string ver = run_capture(cm.config.compiler_bin + " --version", &rc);
    std::istringstream vin(ver);
    std::string first;
    std::getline(vin, first);
    std::cout << "compiler_version: " << first << "\n";
    std::cout << "git: " << (command_exists("git") ? "yes" : "no") << "\n";
    std::cout << "clang-format: " << (command_exists("clang-format") ? "yes" : "no") << "\n";
    std::cout << "clang-tidy: " << (command_exists("clang-tidy") ? "yes" : "no") << "\n";
    std::cout << "cppcheck: " << (command_exists("cppcheck") ? "yes" : "no") << "\n";
    std::cout << "valgrind: " << (command_exists("valgrind") ? "yes" : "no") << "\n";
    std::cout << "cache_dir: " << (fs::exists(".c+++_cache") ? "exists" : "missing") << "\n";
    fs::path src = "___cpp_doctor_test.cpp";
    fs::path exe = "___cpp_doctor_test";
#ifdef _WIN32
    exe += ".exe";
#endif
    std::ofstream(src) << "#include <iostream>\nint main(){std::cout<<\"hello\";return 0;}\n";
    std::vector<std::string> args = {cm.config.compiler_bin, "-std=" + cm.config.cpp_standard, src.string(), "-o", exe.string()};
    run_capture(join_args(args), &rc);
    fs::remove(src);
    fs::remove(exe);
    std::cout << "hello_world: " << (rc == 0 ? "ok" : "failed") << "\n";
    return rc == 0 ? 0 : 1;
}

int main(int argc, char* argv[]) {
    Logger::setup_console();
    ConfigManager confMgr;

    if (argc >= 2) {
        std::string arg1 = argv[1];
        if (arg1 == "-h" || arg1 == "--help") {
            show_help();
            return 0;
        }
        if (arg1 == "-s" || arg1 == "--set" || arg1 == "--config") {
            MenuSystem menu(confMgr);
            menu.run_config_mode();
            return 0;
        }
        if (arg1 == "--show") {
            show_config(confMgr);
            return 0;
        }
        if (arg1 == "doctor") return doctor(confMgr);
        if (arg1 == "clean") return clean_cache(false);
        if (arg1 == "--agent") {
            AgentMode agent(confMgr);
            return agent.execute(argc, argv);
        }
        if (arg1 == "--new") {
            if (argc < 3) {
                Logger::log(Logger::ERROR, "--new 需要项目名");
                return 1;
            }
            return create_project(argv[2]) ? 0 : 1;
        }
        if (arg1 == "--cmake") {
            auto proj = parse_cmake(".");
            if (!proj.found) {
                Logger::log(Logger::ERROR, "未找到 CMakeLists.txt");
                return 1;
            }
            std::string target = argc >= 3 ? argv[2] : "";
            Logger::log(Logger::INFO, "CMake 项目: " + proj.project_name);
            Logger::log(Logger::INFO, "Targets: " + std::to_string(proj.targets.size()) + " 个");
            return build_cmake(proj, target);
        }
    }

    // No args: try c+++.toml -> CMakeLists.txt -> show help
    if (argc == 1) {
        if (!fs::exists("c+++.toml") && fs::exists("CMakeLists.txt")) {
            auto proj = parse_cmake(".");
            if (proj.found && !proj.targets.empty()) {
                Logger::log(Logger::INFO, "自动检测到 CMakeLists.txt，开始构建...");
                Logger::log(Logger::INFO, "项目: " + proj.project_name + " | Targets: " + std::to_string(proj.targets.size()));
                return build_cmake(proj, "");
            }
        }
        if (!fs::exists("c+++.toml")) {
            show_help();
            return 0;
        }
    }

    Builder builder(confMgr);
    if (!builder.parse_args(argc, argv)) {
        Logger::log(Logger::ERROR, "参数解析失败，请检查源文件或使用 --help");
        return 1;
    }
    return builder.execute() ? 0 : 1;
}
