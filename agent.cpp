#include "agent.h"

static bool agent_file_exists(const std::string& path) {
    return fs::exists(path) && fs::is_regular_file(path);
}

static std::vector<std::string> agent_find_executables(const std::vector<std::string>& names) {
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
            if (agent_file_exists(full_path)) { result.push_back(name); break; }
        }
    }
    std::set<std::string> seen;
    std::vector<std::string> unique;
    for (auto& r : result) if (seen.insert(r).second) unique.push_back(r);
    return unique;
}

static std::string agent_get_compiler_version(const std::string& compiler_cmd) {
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

std::string AgentMode::json_str(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    out += "\"";
    return out;
}

std::string AgentMode::json_arr(const std::vector<std::string>& v) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) ss << ",";
        ss << json_str(v[i]);
    }
    ss << "]";
    return ss.str();
}

std::string AgentMode::json_bool(bool b) {
    return b ? "true" : "false";
}

AgentMode::AgentMode(ConfigManager& mgr) : cm(mgr) {}

void AgentMode::cmd_config_show() {
    cm.load();
    std::cout << "{\n";
    std::cout << "  \"compiler\": " << json_str(cm.config.compiler_bin) << ",\n";
    std::cout << "  \"std\": " << json_str(cm.config.cpp_standard) << ",\n";
    std::cout << "  \"sanitizers\": " << json_bool(cm.config.enable_sanitizers) << ",\n";
    std::cout << "  \"compile_options\": " << json_arr(cm.config.compile_options) << ",\n";
    std::cout << "  \"link_options\": " << json_arr(cm.config.link_options) << ",\n";
    std::cout << "  \"macros\": " << json_arr(cm.config.macros) << ",\n";
    std::cout << "  \"include_paths\": " << json_arr(cm.config.include_paths) << ",\n";
    std::cout << "  \"library_paths\": " << json_arr(cm.config.library_paths) << ",\n";
    std::cout << "  \"libraries\": " << json_arr(cm.config.libraries) << ",\n";
    std::cout << "  \"run_after_compile\": " << json_bool(cm.config.run_after_compile) << ",\n";
    std::cout << "  \"temp_mode\": " << json_bool(cm.config.temp_mode) << ",\n";
    std::cout << "  \"profiles\": {";
    bool first = true;
    for (const auto& [name, flags] : cm.config.profiles) {
        if (!first) std::cout << ",";
        first = false;
        std::cout << "\n    " << json_str(name) << ": " << json_arr(flags);
    }
    if (!cm.config.profiles.empty()) std::cout << "\n  ";
    std::cout << "}\n";
    std::cout << "}\n";
}

void AgentMode::cmd_config_set(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "usage: --agent config set <key> <value>\nkeys: compiler, std\n";
        return;
    }
    cm.load();
    const std::string& key = args[0];
    const std::string& val = args[1];
    if (key == "compiler") cm.config.compiler_bin = val;
    else if (key == "std") cm.config.cpp_standard = val;
    else { std::cerr << "unknown key: " << key << "\n"; return; }
    cm.save();
    std::cout << "{\"status\":\"ok\",\"key\":" << json_str(key) << ",\"value\":" << json_str(val) << "}\n";
}

void AgentMode::cmd_config_add(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "usage: --agent config add <type> <value>\ntypes: copt, lopt, macro, include, libpath, lib\n";
        return;
    }
    cm.load();
    const std::string& type = args[0];
    const std::string& val  = args[1];
    if (type == "copt")      cm.config.compile_options.push_back(val);
    else if (type == "lopt") cm.config.link_options.push_back(val);
    else if (type == "macro")    cm.config.macros.push_back(val);
    else if (type == "include")  cm.config.include_paths.push_back(val);
    else if (type == "libpath")  cm.config.library_paths.push_back(val);
    else if (type == "lib")      cm.config.libraries.push_back(val);
    else { std::cerr << "unknown type: " << type << "\n"; return; }
    cm.save();
    std::cout << "{\"status\":\"ok\",\"action\":\"add\",\"type\":" << json_str(type) << ",\"value\":" << json_str(val) << "}\n";
}

void AgentMode::cmd_config_del(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "usage: --agent config del <type> <value>\ntypes: copt, lopt, macro, include, libpath, lib\n";
        return;
    }
    cm.load();
    const std::string& type = args[0];
    const std::string& val  = args[1];
    std::vector<std::string>* target = nullptr;
    if (type == "copt")      target = &cm.config.compile_options;
    else if (type == "lopt") target = &cm.config.link_options;
    else if (type == "macro")    target = &cm.config.macros;
    else if (type == "include")  target = &cm.config.include_paths;
    else if (type == "libpath")  target = &cm.config.library_paths;
    else if (type == "lib")      target = &cm.config.libraries;
    else { std::cerr << "unknown type: " << type << "\n"; return; }
    auto it = std::find(target->begin(), target->end(), val);
    if (it == target->end()) { std::cerr << "not found: " << val << "\n"; return; }
    target->erase(it);
    cm.save();
    std::cout << "{\"status\":\"ok\",\"action\":\"del\",\"type\":" << json_str(type) << ",\"value\":" << json_str(val) << "}\n";
}

void AgentMode::cmd_config_sanitizers(const std::vector<std::string>& args) {
    if (args.empty()) { std::cerr << "usage: --agent config sanitizers on|off\n"; return; }
    cm.load();
    const std::string& v = args[0];
    if (v == "on" || v == "1" || v == "true")  cm.config.enable_sanitizers = true;
    else if (v == "off" || v == "0" || v == "false") cm.config.enable_sanitizers = false;
    else { std::cerr << "expected on/off\n"; return; }
    cm.save();
    std::cout << "{\"status\":\"ok\",\"sanitizers\":" << json_bool(cm.config.enable_sanitizers) << "}\n";
}

void AgentMode::cmd_config_preset(const std::vector<std::string>& args) {
    if (args.empty()) { std::cerr << "usage: --agent config preset <name>\n"; return; }
    cm.load();
    const std::string& name = args[0];
    if (name == "debug") {
        cm.config.compile_options = {"-g", "-O0", "-Wall"};
        cm.config.enable_sanitizers = true;
    } else if (name == "release") {
        cm.config.compile_options = {"-O2", "-Wall", "-Wextra"};
        cm.config.enable_sanitizers = false;
    } else if (name == "contest") {
        cm.config.compile_options = {"-O2", "-Wall", "-Wextra", "-DONLINE_JUDGE"};
        cm.config.enable_sanitizers = false;
    } else { std::cerr << "unknown preset: " << name << "\n"; return; }
    cm.save();
    std::cout << "{\"status\":\"ok\",\"preset\":" << json_str(name) << "}\n";
}

void AgentMode::cmd_config_save() {
    cm.save();
    std::cout << "{\"status\":\"ok\",\"action\":\"saved\"}\n";
}
void AgentMode::cmd_config_load() {
    if (cm.load())
        std::cout << "{\"status\":\"ok\",\"action\":\"loaded\"}\n";
    else
        std::cout << "{\"status\":\"ok\",\"action\":\"no_file\"}\n";
}

void AgentMode::cmd_scan() {
    std::vector<std::string> candidates = {
        "g++-13","g++-12","g++-11","g++-10","g++",
        "clang++-18","clang++-17","clang++-15","clang++",
        "c++","zig"
    };
    auto found = agent_find_executables(candidates);
    std::cout << "{\n  \"compilers\": [\n";
    for (size_t i = 0; i < found.size(); ++i) {
        std::string ver = agent_get_compiler_version(found[i]);
        std::cout << "    {\"name\":" << json_str(found[i])
                  << ",\"version\":" << json_str(ver) << "}";
        if (i + 1 < found.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "  ]\n}\n";
}

void AgentMode::cmd_build(const std::vector<std::string>& args) {
    std::vector<char*> argv_storage;
    argv_storage.push_back(const_cast<char*>("c+++"));
    for (const auto& a : args) argv_storage.push_back(const_cast<char*>(a.data()));
    int argc = static_cast<int>(argv_storage.size());
    char** argv = argv_storage.data();

    Builder builder(cm);
    if (!builder.parse_args(argc, argv)) {
        std::cerr << "parse_args failed\n";
        return;
    }
    builder.execute();
}

void AgentMode::cmd_clean() {
    bool existed = fs::exists(".c+++_cache");
    std::error_code ec;
    fs::remove_all(".c+++_cache", ec);
    fs::remove("compile_commands.json", ec);
    std::cout << "{\"cleaned\":" << (existed ? "true" : "false") << ",\"error\":" << (ec ? "true" : "false") << "}\n";
}

void AgentMode::cmd_doctor() {
    cm.load();
    int rc = 1;
    std::string ver = run_capture(cm.config.compiler_bin + " --version", &rc);
    std::istringstream vin(ver);
    std::string first;
    std::getline(vin, first);
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
    std::cout << "{\n";
    std::cout << "  \"compiler\": " << json_str(cm.config.compiler_bin) << ",\n";
    std::cout << "  \"standard\": " << json_str(cm.config.cpp_standard) << ",\n";
    std::cout << "  \"compiler_found\": " << json_bool(command_exists(cm.config.compiler_bin)) << ",\n";
    std::cout << "  \"compiler_version\": " << json_str(first) << ",\n";
    std::cout << "  \"git\": " << json_bool(command_exists("git")) << ",\n";
    std::cout << "  \"clang_format\": " << json_bool(command_exists("clang-format")) << ",\n";
    std::cout << "  \"clang_tidy\": " << json_bool(command_exists("clang-tidy")) << ",\n";
    std::cout << "  \"cppcheck\": " << json_bool(command_exists("cppcheck")) << ",\n";
    std::cout << "  \"valgrind\": " << json_bool(command_exists("valgrind")) << ",\n";
    std::cout << "  \"cache_dir\": " << json_bool(fs::exists(".c+++_cache")) << ",\n";
    std::cout << "  \"hello_world\": " << json_bool(rc == 0) << "\n";
    std::cout << "}\n";
}

void AgentMode::cmd_run(const std::vector<std::string>& args) {
    std::vector<char*> argv_storage;
    argv_storage.push_back(const_cast<char*>("c+++"));
    argv_storage.push_back(const_cast<char*>("-r"));
    for (const auto& a : args) argv_storage.push_back(const_cast<char*>(a.data()));
    int argc = static_cast<int>(argv_storage.size());
    char** argv = argv_storage.data();

    Builder builder(cm);
    if (!builder.parse_args(argc, argv)) {
        std::cerr << "parse_args failed\n";
        return;
    }
    builder.execute();
}

void AgentMode::show_help() {
    std::cout << R"(c+++ --agent <command> [args...]

AI Agent 非交互模式 — 所有操作用位置参数，输出 JSON。

配置管理:
  --agent config show                    查看全部配置 (JSON)
  --agent config set <key> <value>       设置 compiler | std
  --agent config add <type> <value>      添加: copt | lopt | macro | include | libpath | lib
  --agent config del <type> <value>      删除: copt | lopt | macro | include | libpath | lib
  --agent config sanitizers on|off       开关 Sanitizers
  --agent config preset debug|release|contest  应用预设
  --agent config save                    保存配置到文件
  --agent config load                    从文件重新加载

编译器扫描/诊断:
  --agent scan                           扫描 PATH 中的编译器 (JSON)
  --agent doctor                         环境诊断 (JSON)
  --agent clean                          清理缓存 (JSON)

构建:
  --agent build <files...> [options]     非交互构建
  --agent run <files...> [options]       构建并运行 (自动 -r)

示例:
  --agent config show
  --agent config set compiler g++
  --agent config add copt -march=native
  --agent config del copt -Wall
  --agent config sanitizers on
  --agent config preset release
  --agent scan
  --agent build main.cpp utils.cpp -o myapp
  --agent run main.cpp --time
)";
}

int AgentMode::execute(int argc, char* argv[]) {
    if (argc < 3) { show_help(); return 1; }
    std::string cmd = argv[2];

    std::vector<std::string> args;
    for (int i = 3; i < argc; ++i) args.push_back(argv[i]);

    if (cmd == "config") {
        if (args.empty()) { cmd_config_show(); return 0; }
        std::string sub = args[0];
        args.erase(args.begin());
        if (sub == "show")      cmd_config_show();
        else if (sub == "set")  cmd_config_set(args);
        else if (sub == "add")  cmd_config_add(args);
        else if (sub == "del")  cmd_config_del(args);
        else if (sub == "sanitizers") cmd_config_sanitizers(args);
        else if (sub == "preset")     cmd_config_preset(args);
        else if (sub == "save")       cmd_config_save();
        else if (sub == "load")       cmd_config_load();
        else { std::cerr << "unknown config subcommand: " << sub << "\n"; return 1; }
        return 0;
    }

    if (cmd == "scan") { cmd_scan(); return 0; }
    if (cmd == "doctor") { cmd_doctor(); return 0; }
    if (cmd == "clean") { cmd_clean(); return 0; }
    if (cmd == "build") { cmd_build(args); return 0; }
    if (cmd == "run") { cmd_run(args); return 0; }

    show_help();
    return 1;
}
