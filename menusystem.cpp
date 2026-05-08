#include "menusystem.h"

Key read_key() {
#ifdef _WIN32
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        c = _getch();
        switch (c) {
            case 72: return Key::UP;
            case 80: return Key::DOWN;
            case 75: return Key::LEFT;
            case 77: return Key::RIGHT;
            default: return Key::UNKNOWN;
        }
    } else {
        if (c == 13) return Key::ENTER;
        if (c == 'q' || c == 'Q') return Key::Q;
        if (c == 'w' || c == 'W') return Key::W;
        if (c == 's' || c == 'S') return Key::S;
        if (c == 'a' || c == 'A') return Key::A;
        if (c == 'd' || c == 'D') return Key::D;
        return Key::UNKNOWN;
    }
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int c = getchar();
    if (c == 27) {
        c = getchar();
        if (c == '[') {
            c = getchar();
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            switch (c) {
                case 'A': return Key::UP;
                case 'B': return Key::DOWN;
                case 'C': return Key::RIGHT;
                case 'D': return Key::LEFT;
                default: return Key::UNKNOWN;
            }
        } else {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            return Key::UNKNOWN;
        }
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        if (c == 10 || c == 13) return Key::ENTER;
        if (c == 'q' || c == 'Q') return Key::Q;
        if (c == 'w' || c == 'W') return Key::W;
        if (c == 's' || c == 'S') return Key::S;
        if (c == 'a' || c == 'A') return Key::A;
        if (c == 'd' || c == 'D') return Key::D;
        return Key::UNKNOWN;
    }
#endif
}

MenuSystem::MenuSystem(ConfigManager& mgr) : cm(mgr) {}

void MenuSystem::clear_screen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

bool MenuSystem::file_exists(const std::string& path) {
    return fs::exists(path) && fs::is_regular_file(path);
}

std::vector<std::string> MenuSystem::find_executables(const std::vector<std::string>& names) {
    std::vector<std::string> result;
    std::string path_env;
    char* penv = std::getenv("PATH");
    if (penv) path_env = penv;
    if (path_env.empty()) return result;

#ifdef _WIN32
    const char delim = ';';
#else
    const char delim = ':';
#endif

    std::istringstream iss(path_env);
    std::string dir;
    while (std::getline(iss, dir, delim)) {
        if (dir.empty()) continue;
        for (const auto& name : names) {
            std::string full_path = dir + PATH_SEPARATOR + name;
#ifdef _WIN32
            if (full_path.find(".exe") == std::string::npos) full_path += ".exe";
#endif
            if (file_exists(full_path)) {
                result.push_back(name);
                break;
            }
        }
    }

    std::set<std::string> seen;
    std::vector<std::string> unique;
    for (auto& r : result) if (seen.insert(r).second) unique.push_back(r);
    return unique;
}

std::string MenuSystem::get_compiler_version(const std::string& compiler_cmd) {
    std::string version_cmd;
#ifdef _WIN32
    version_cmd = "\"" + compiler_cmd + "\" --version";
#else
    version_cmd = compiler_cmd + " --version 2>&1";
#endif

    FILE* pipe = POPEN(version_cmd.c_str(), "r");
    if (!pipe) return "";
    char buffer[256];
    std::string first_line;
    if (fgets(buffer, sizeof(buffer), pipe)) {
        first_line = buffer;
        while (!first_line.empty() && (first_line.back() == '\n' || first_line.back() == '\r'))
            first_line.pop_back();
    }
    PCLOSE(pipe);
    return first_line;
}

std::vector<std::pair<std::string, std::string>> MenuSystem::detect_compilers() {
    std::vector<std::string> candidates = {
        "g++-13", "g++-12", "g++-11", "g++-10", "g++",
        "clang++-18", "clang++-17", "clang++-15", "clang++",
        "c++", "zig"
    };

    auto found = find_executables(candidates);
    std::vector<std::pair<std::string, std::string>> compilers;
    for (const auto& cmd : found) {
        std::string version = get_compiler_version(cmd);
        std::string display = version.empty() ? cmd : version;
        if (display.size() > 70) display = display.substr(0, 67) + "...";
        compilers.emplace_back(cmd, display);
    }
    return compilers;
}

int MenuSystem::select_from_list(const std::string& title,
                                  const std::vector<std::string>& options,
                                  const std::string& extra_info) {
    int selected = 0;
    while (true) {
        clear_screen();
        Logger::print_title(title);
        if (!extra_info.empty()) std::cout << extra_info << "\n";

        for (size_t i = 0; i < options.size(); ++i) {
            if (static_cast<int>(i) == selected)
                std::cout << "\033[7m > " << options[i] << " \033[0m\n";
            else
                std::cout << "   " << options[i] << "\n";
        }
        std::cout << "\n使用 W/S 或 ↑/↓ 移动, Enter 确认, Q 返回\n";

        Key k = read_key();
        if (k == Key::UP || k == Key::W) {
            if (selected > 0) selected--;
            else selected = static_cast<int>(options.size()) - 1;
        } else if (k == Key::DOWN || k == Key::S) {
            if (selected < static_cast<int>(options.size()) - 1) selected++;
            else selected = 0;
        } else if (k == Key::ENTER) {
            return selected;
        } else if (k == Key::Q) {
            return -1;
        }
    }
}

bool MenuSystem::test_hello_world() {
    const std::string test_src = "___cpp_plus_plus_test.cpp";
    const std::string test_exe =
#ifdef _WIN32
        "___cpp_plus_plus_test.exe";
#else
        "___cpp_plus_plus_test";
#endif

    std::ofstream out(test_src);
    if (!out) {
        Logger::log(Logger::ERROR, "无法创建测试文件");
        return false;
    }
    out << "#include <iostream>\n"
           "int main() { std::cout << \"Hello, World!\" << std::endl; return 0; }\n";
    out.close();

    std::stringstream cmd;
    cmd << cm.config.compiler_bin << " -std=" << cm.config.cpp_standard;
    for (const auto& opt : cm.config.compile_options) cmd << " " << opt;
    if (cm.config.enable_sanitizers) cmd << " -fsanitize=address,undefined -g";
    for (const auto& opt : cm.config.link_options) cmd << " " << opt;
    cmd << " \"" << test_src << "\" -o \"" << test_exe << "\"";

    int ret = system(cmd.str().c_str());

    std::remove(test_src.c_str());
    std::remove(test_exe.c_str());

    if (ret == 0) {
        Logger::log(Logger::SUCCESS, "Hello World 测试编译通过！配置有效。");
        return true;
    } else {
        Logger::log(Logger::ERROR, "Hello World 测试编译失败！请检查配置。");
        return false;
    }
}

void MenuSystem::run_config_mode() {
    cm.load();
    while (true) {
        std::string status = "当前配置预览:\n";
        status += "  编译器: \033[33m" + cm.config.compiler_bin + "\033[0m\n";
        status += "  标准:   \033[33m" + cm.config.cpp_standard + "\033[0m\n";
        status += "  Sanitizers: " + std::string(cm.config.enable_sanitizers ? "\033[32m已开启\033[0m" : "\033[90m已关闭\033[0m") + "\n";
        status += "  编译选项: ";
        if (cm.config.compile_options.empty()) status += "\033[90m(无)\033[0m";
        for (auto& o : cm.config.compile_options) status += o + " ";
        status += "\n  链接选项: ";
        if (cm.config.link_options.empty()) status += "\033[90m(无)\033[0m";
        for (auto& o : cm.config.link_options) status += "\033[36m" + o + "\033[0m ";

        std::vector<std::string> menu_options = {
            "切换 C++ 标准 (Current: " + cm.config.cpp_standard + ")",
            "管理编译选项",
            "管理链接选项",
            "切换编译器 (Current: " + cm.config.compiler_bin + ")",
            "开关 Sanitizers (Current: " + std::string(cm.config.enable_sanitizers ? "ON" : "OFF") + ")",
            "保存配置并退出",
            "直接退出(不保存)"
        };

        int choice = select_from_list("C++ 编译器配置中心", menu_options, status);
        if (choice == -1) return;

        switch (choice) {
            case 0: menu_standard(); break;
            case 1: menu_compile_options(); break;
            case 2: menu_link_options(); break;
            case 3: menu_compiler(); break;
            case 4: cm.config.enable_sanitizers = !cm.config.enable_sanitizers; break;
            case 5:
                if (cm.save()) {
                    Logger::log(Logger::SUCCESS, "配置已保存!");
                    test_hello_world();
                }
                return;
            case 6: return;
            default: break;
        }
    }
}

void MenuSystem::menu_standard() {
    std::vector<std::string> standards = {"c++11", "c++14", "c++17", "c++20", "c++23"};
    int choice = select_from_list("选择 C++ 标准", standards, "当前: " + cm.config.cpp_standard);
    if (choice >= 0 && choice < static_cast<int>(standards.size()))
        cm.config.cpp_standard = standards[choice];
}

void MenuSystem::menu_compiler() {
    clear_screen();
    std::cout << "正在扫描系统中的编译器...\n";

    auto detected = detect_compilers();
    std::vector<std::string> options;
    std::vector<std::string> commands;

    options.push_back("保持当前: " + cm.config.compiler_bin);
    commands.push_back(cm.config.compiler_bin);

    for (const auto& comp : detected) {
        options.push_back(comp.second);
        commands.push_back(comp.first);
    }
    options.push_back("自定义输入...");
    commands.push_back("**custom**");

    int choice = select_from_list("切换编译器", options, "当前: " + cm.config.compiler_bin);
    if (choice < 0) return;

    if (choice == static_cast<int>(commands.size()) - 1) {
        std::cout << "输入编译器路径/命令: ";
        std::getline(std::cin, cm.config.compiler_bin);
    } else {
        cm.config.compiler_bin = commands[choice];
    }
}

void MenuSystem::menu_compile_options() {
    while (true) {
        std::string extra = "当前编译选项:\n";
        if (cm.config.compile_options.empty()) extra += "  [空]\n";
        else {
            for (size_t i = 0; i < cm.config.compile_options.size(); ++i)
                extra += "  " + std::to_string(i+1) + ". " + cm.config.compile_options[i] + "\n";
        }
        std::vector<std::string> options = {
            "添加自定义选项",
            "删除指定选项",
            "预设: Debug (-g -O0 -Wall)",
            "预设: Release (-O2 -Wall -Wextra)",
            "清空所有选项",
            "返回主菜单"
        };
        int choice = select_from_list("编译选项管理", options, extra);
        if (choice < 0) return;

        switch (choice) {
            case 0: {
                std::cout << "请输入编译选项 (如 -march=native): ";
                std::string opt;
                std::getline(std::cin, opt);
                if (!opt.empty()) cm.config.compile_options.push_back(opt);
                break;
            }
            case 1: {
                if (cm.config.compile_options.empty()) break;
                std::vector<std::string> del_opts = cm.config.compile_options;
                int idx = select_from_list("选择要删除的选项", del_opts, "当前编译选项列表:");
                if (idx >= 0 && idx < static_cast<int>(cm.config.compile_options.size()))
                    cm.config.compile_options.erase(cm.config.compile_options.begin() + idx);
                break;
            }
            case 2: cm.config.compile_options = {"-g", "-O0", "-Wall"}; break;
            case 3: cm.config.compile_options = {"-O2", "-Wall", "-Wextra"}; break;
            case 4: cm.config.compile_options.clear(); break;
            case 5: return;
            default: break;
        }
    }
}

void MenuSystem::menu_link_options() {
    while (true) {
        std::string extra = "当前链接选项:\n";
        if (cm.config.link_options.empty()) extra += "  [空]\n";
        else {
            for (size_t i = 0; i < cm.config.link_options.size(); ++i)
                extra += "  " + std::to_string(i+1) + ". " + cm.config.link_options[i] + "\n";
        }
        std::vector<std::string> options = {
            "添加新选项",
            "删除指定选项",
            "清空所有链接选项",
            "返回主菜单"
        };
        int choice = select_from_list("链接选项管理", options, extra);
        if (choice < 0) return;

        switch (choice) {
            case 0: {
                std::cout << "请输入链接参数: ";
                std::string opt;
                std::getline(std::cin, opt);
                if (!opt.empty()) cm.config.link_options.push_back(opt);
                break;
            }
            case 1: {
                if (cm.config.link_options.empty()) break;
                std::vector<std::string> del_opts = cm.config.link_options;
                int idx = select_from_list("选择要删除的选项", del_opts, "当前链接选项列表:");
                if (idx >= 0 && idx < static_cast<int>(cm.config.link_options.size()))
                    cm.config.link_options.erase(cm.config.link_options.begin() + idx);
                break;
            }
            case 2: cm.config.link_options.clear(); break;
            case 3: return;
            default: break;
        }
    }
}
