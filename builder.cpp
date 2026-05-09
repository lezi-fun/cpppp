#include "builder.h"
#include <future>
#include <mutex>

Builder::Builder(ConfigManager& cm) : confMgr(cm) {}

bool Builder::looks_like_output_dir(const std::string& value) {
    fs::path p(value);
    if (fs::exists(p) && fs::is_directory(p)) return true;
    if (ends_with(value, "/") || ends_with(value, "\\")) return true;
    std::string base = p.filename().string();
    return base == "build" || base == "dist" || base == "out" || base == "bin";
}

void Builder::add_unique(std::vector<std::string>& v, const std::string& s) {
    if (!s.empty() && std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

void Builder::load_cache() {
    cache_state.clear();
    std::ifstream in(cache_dir / "state.txt");
    std::string line;
    while (std::getline(in, line)) {
        size_t tab = line.find('\t');
        if (tab != std::string::npos) cache_state[line.substr(0, tab)] = line.substr(tab + 1);
    }
}

void Builder::save_cache() {
    fs::create_directories(cache_dir);
    std::ofstream out(cache_dir / "state.txt");
    for (const auto& [obj, sig] : cache_state) out << obj << "\t" << sig << "\n";
}

std::vector<std::string> Builder::base_compile_args(bool shared) const {
    std::vector<std::string> args;
    args.push_back(confMgr.config.compiler_bin);
    args.push_back("-std=" + confMgr.config.cpp_standard);
    for (const auto& p : confMgr.config.include_paths) args.push_back("-I" + p);
    for (const auto& p : opts.include_paths) args.push_back("-I" + p);
    for (const auto& m : confMgr.config.macros) args.push_back("-D" + m);
    for (const auto& m : opts.macros) args.push_back("-D" + m);
    for (const auto& o : confMgr.config.compile_options) args.push_back(o);
    if (confMgr.config.enable_sanitizers) {
        args.push_back("-fsanitize=address,undefined");
        args.push_back("-g");
    }
    if (!opts.profile.empty()) {
        auto it = confMgr.config.profiles.find(opts.profile);
        if (it != confMgr.config.profiles.end()) {
            for (const auto& f : it->second) args.push_back(f);
        } else {
            Logger::log(Logger::WARNING, "未找到 profile: " + opts.profile);
        }
    }
    if (shared) args.push_back("-fPIC");
    if (!opts.pch.empty()) {
        args.push_back("-include");
        args.push_back(opts.pch);
    }
    return args;
}

std::vector<std::string> Builder::link_args(bool shared) const {
    std::vector<std::string> args;
    for (const auto& o : confMgr.config.link_options) args.push_back(o);
    for (const auto& p : confMgr.config.library_paths) args.push_back("-L" + p);
    for (const auto& p : opts.library_paths) args.push_back("-L" + p);
    for (const auto& l : confMgr.config.libraries) args.push_back("-l" + l);
    for (const auto& l : opts.libraries) args.push_back("-l" + l);
    if (shared) args.push_back("-shared");
    return args;
}

fs::path Builder::object_path_for(const fs::path& src) const {
    return cache_dir / "obj" / (sanitize_path(src) + ".o");
}

std::string Builder::signature_for(const fs::path& src, const std::vector<std::string>& compile_args) const {
    std::ostringstream ss;
    ss << fs::absolute(src).string() << "|" << file_hash(src);
    for (const auto& a : compile_args) ss << "|" << a;
    if (!opts.pch.empty() && fs::exists(opts.pch)) ss << "|pch:" << file_hash(opts.pch);
    return std::to_string(std::hash<std::string>{}(ss.str()));
}

std::string Builder::colorize_compiler_output(const std::string& out) {
    std::istringstream in(out);
    std::ostringstream colored;
    std::string line;
    last_warning_count = 0;
    last_error_count = 0;
    while (std::getline(in, line)) {
        if (line.find(" error:") != std::string::npos || line.find(": error:") != std::string::npos) {
            ++last_error_count;
            colored << "\033[91m" << line << "\033[0m\n";
        } else if (line.find(" warning:") != std::string::npos || line.find(": warning:") != std::string::npos) {
            ++last_warning_count;
            colored << "\033[93m" << line << "\033[0m\n";
        } else if (line.find(" note:") != std::string::npos || line.find(": note:") != std::string::npos) {
            colored << "\033[96m" << line << "\033[0m\n";
        } else {
            colored << line << "\n";
        }
    }
    return colored.str();
}

void Builder::error_navigation(const std::string& out) {
    std::regex re(R"(([^:\n]+):([0-9]+):([0-9]+)?:?\s*(fatal error|error|warning):\s*(.*))");
    std::vector<std::tuple<std::string, std::string, std::string>> errors;
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        std::smatch m;
        if (std::regex_search(line, m, re)) errors.emplace_back(m[1].str(), m[2].str(), line);
    }
    if (errors.empty()) return;
    Logger::print_title("错误导航");
    for (size_t i = 0; i < errors.size(); ++i) {
        std::cout << i + 1 << ". " << std::get<2>(errors[i]) << "\n";
    }
    const char* editor = std::getenv("EDITOR");
    if (!editor) editor = std::getenv("VISUAL");
    if (!editor) {
        Logger::log(Logger::HINT, "设置 EDITOR 或 VISUAL 后可输入编号直接打开对应行。");
        return;
    }
    int selected = 0;
    while (true) {
        std::cout << "\033[2J\033[H";
        Logger::print_title("错误导航");
        for (size_t i = 0; i < errors.size(); ++i) {
            if (static_cast<int>(i) == selected) std::cout << "\033[7m> " << std::get<2>(errors[i]) << "\033[0m\n";
            else std::cout << "  " << std::get<2>(errors[i]) << "\n";
        }
        std::cout << "\n使用 W/S 或 ↑/↓ 移动，Enter 打开，Q 跳过\n";
        int key = read_nav_key();
        if (key == 9) return;
        if (key == 0) break;
        if (key < 0) selected = selected == 0 ? static_cast<int>(errors.size()) - 1 : selected - 1;
        if (key > 0 && key != 2) selected = selected + 1 >= static_cast<int>(errors.size()) ? 0 : selected + 1;
    }
    std::string cmd = std::string(editor) + " +" + std::get<1>(errors[selected]) + " " + shell_quote(std::get<0>(errors[selected]));
    system(cmd.c_str());
}

void Builder::print_stats(bool ok, double seconds, const fs::path& out, int warnings) {
    std::uintmax_t size = 0;
    if (ok && fs::exists(out) && fs::is_regular_file(out)) size = fs::file_size(out);
    std::cout << "\033[90m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m\n";
    std::cout << "  " << (ok ? "\033[92m编译成功\033[0m" : "\033[91m编译失败\033[0m") << "\n";
    std::cout << "  耗时: " << std::fixed << std::setprecision(2) << seconds << "s\n";
    if (ok) std::cout << "  二进制大小: " << (size / 1024.0) << " KB\n";
    std::cout << "  警告: " << warnings << " 条\n";
    if (ok) std::cout << "  输出: " << out.string() << "\n";
    std::cout << "\033[90m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m\n";
}

fs::path Builder::resolve_output(const BuildTarget& t) {
    fs::path first = t.sources.empty() ? "a" : fs::path(t.sources.front()).stem();
    std::string default_name = first.string();
#ifdef _WIN32
    if (!t.shared) default_name += ".exe";
    else default_name += ".dll";
#else
    if (t.shared && !starts_with(default_name, "lib")) default_name = "lib" + default_name + ".so";
#endif
    if (t.output.empty()) return default_name;
    if (t.output_is_dir) {
        fs::create_directories(t.output);
        return fs::path(t.output) / default_name;
    }
    fs::path out = t.output;
    if (out.has_parent_path()) fs::create_directories(out.parent_path());
    return out;
}

std::vector<std::string> Builder::find_project_cpp_for_header(const fs::path& header) {
    std::vector<std::string> out;
    std::string stem = header.stem().string();
    for (const auto& e : fs::recursive_directory_iterator(".")) {
        if (!fs::is_regular_file(e) || !is_cpp_source(e.path())) continue;
        fs::path p = e.path();
        if (p.stem() == stem) out.push_back(p.string());
    }
    return out;
}

fs::path Builder::resolve_include(const fs::path& base, const std::string& inc) {
    fs::path direct = base.parent_path() / inc;
    if (fs::exists(direct)) return direct;
    for (const auto& p : confMgr.config.include_paths) {
        fs::path candidate = fs::path(p) / inc;
        if (fs::exists(candidate)) return candidate;
    }
    for (const auto& p : opts.include_paths) {
        fs::path candidate = fs::path(p) / inc;
        if (fs::exists(candidate)) return candidate;
    }
    return {};
}

void Builder::auto_discover_deps(BuildTarget& t) {
    std::set<std::string> known(t.sources.begin(), t.sources.end());
    std::regex inc_re(R"(^\s*#\s*include\s*"([^"]+)\")");
    for (size_t i = 0; i < t.sources.size(); ++i) {
        fs::path src = t.sources[i];
        std::ifstream in(src);
        std::string line;
        while (std::getline(in, line)) {
            std::smatch m;
            if (!std::regex_search(line, m, inc_re)) continue;
            fs::path h = resolve_include(src, m[1].str());
            if (h.empty() || !is_header(h)) continue;
            for (const auto& cpp : find_project_cpp_for_header(h)) {
                if (known.insert(cpp).second) {
                    Logger::log(Logger::HINT, "auto-deps 加入: " + cpp);
                    t.sources.push_back(cpp);
                }
            }
        }
    }
}

void Builder::filter_git_changed(BuildTarget& t) {
    int rc = 1;
    std::string out = run_capture("git diff --name-only HEAD", &rc);
    if (rc != 0) {
        Logger::log(Logger::WARNING, "git diff 不可用，跳过 --git-changed");
        return;
    }
    std::set<std::string> changed;
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        if (is_cpp_source(line)) changed.insert(fs::weakly_canonical(line).string());
    }
    std::vector<std::string> filtered;
    for (const auto& s : t.sources) {
        std::error_code ec;
        std::string canon = fs::weakly_canonical(s, ec).string();
        if (changed.count(canon)) filtered.push_back(s);
    }
    t.sources = filtered;
}

bool Builder::build_pch() {
    if (opts.pch.empty()) return true;
    if (!fs::exists(opts.pch)) {
        Logger::log(Logger::ERROR, "PCH 文件不存在: " + opts.pch);
        return false;
    }
    fs::path gch = fs::path(opts.pch).string() + ".gch";
    bool need = !fs::exists(gch) || fs::last_write_time(gch) < fs::last_write_time(opts.pch);
    if (!need) return true;
    std::vector<std::string> args = {confMgr.config.compiler_bin, "-std=" + confMgr.config.cpp_standard, "-x", "c++-header", opts.pch, "-o", gch.string()};
    for (const auto& p : confMgr.config.include_paths) args.push_back("-I" + p);
    for (const auto& p : opts.include_paths) args.push_back("-I" + p);
    Logger::log(Logger::INFO, "生成预编译头: " + gch.string());
    int rc = 1;
    std::string out = run_capture(join_args(args), &rc);
    if (!out.empty()) std::cout << colorize_compiler_output(out);
    return rc == 0;
}

void Builder::detect_package_managers() {
    if (fs::exists("vcpkg.json")) {
        Logger::log(Logger::HINT, "检测到 vcpkg.json；如设置 VCPKG_ROOT，会自动加入 installed/<triplet>/include/lib。");
        const char* root = std::getenv("VCPKG_ROOT");
        if (root) {
#ifdef _WIN32
            std::string triplet = "x64-windows";
#else
            std::string triplet = "x64-osx";
#endif
            fs::path installed = fs::path(root) / "installed" / triplet;
            if (fs::exists(installed / "include")) add_unique(opts.include_paths, (installed / "include").string());
            if (fs::exists(installed / "lib")) add_unique(opts.library_paths, (installed / "lib").string());
        }
    }
    if (fs::exists("conanfile.txt") || fs::exists("conanfile.py")) {
        Logger::log(Logger::HINT, "检测到 Conan 项目；会尝试读取 conanbuildinfo.txt 中的 include/lib。");
        fs::path info = "conanbuildinfo.txt";
        if (fs::exists(info)) {
            std::ifstream in(info);
            std::string section, line;
            while (std::getline(in, line)) {
                line = trim(line);
                if (line.empty()) continue;
                if (line.front() == '[' && line.back() == ']') section = line.substr(1, line.size() - 2);
                else if (section == "includedirs") add_unique(opts.include_paths, line);
                else if (section == "libdirs") add_unique(opts.library_paths, line);
                else if (section == "libs") add_unique(opts.libraries, line);
            }
        }
    }
}

bool Builder::run_tool_on_sources(const std::string& tool_name, const std::vector<std::string>& sources) {
    if (sources.empty()) return true;
    if (tool_name == "format") {
        if (!command_exists("clang-format")) {
            Logger::log(Logger::ERROR, "未找到 clang-format");
            return false;
        }
        for (const auto& s : sources) {
            int rc = system(("clang-format -i " + shell_quote(s)).c_str());
            if (rc != 0) return false;
        }
        Logger::log(Logger::SUCCESS, "格式化完成");
        return true;
    }
    if (command_exists("clang-tidy")) {
        for (const auto& s : sources) {
            std::vector<std::string> args = {"clang-tidy", s, "--"};
            auto base = base_compile_args(false);
            args.insert(args.end(), base.begin() + 1, base.end());
            int rc = system(join_args(args).c_str());
            if (rc != 0) return false;
        }
        return true;
    }
    if (command_exists("cppcheck")) {
        std::vector<std::string> args = {"cppcheck", "--enable=warning,style,performance,portability"};
        args.insert(args.end(), sources.begin(), sources.end());
        return system(join_args(args).c_str()) == 0;
    }
    Logger::log(Logger::ERROR, "未找到 clang-tidy 或 cppcheck");
    return false;
}

bool Builder::export_compile_commands(const std::vector<BuildTarget>& ts) {
    std::ofstream out("compile_commands.json");
    if (!out) return false;
    out << "[\n";
    bool first = true;
    for (const auto& t : ts) {
        auto args = base_compile_args(t.shared);
        for (const auto& s : t.sources) {
            std::vector<std::string> cmd = args;
            cmd.push_back("-c");
            cmd.push_back(fs::absolute(s).string());
            cmd.push_back("-o");
            cmd.push_back(fs::absolute(object_path_for(s)).string());
            if (!first) out << ",\n";
            first = false;
            out << "  {\n";
            out << "    \"directory\": \"" << json_escape(fs::current_path().string()) << "\",\n";
            out << "    \"file\": \"" << json_escape(fs::absolute(s).string()) << "\",\n";
            out << "    \"command\": \"" << json_escape(join_args(cmd)) << "\"\n";
            out << "  }";
        }
    }
    out << "\n]\n";
    Logger::log(Logger::SUCCESS, "已生成 compile_commands.json");
    return true;
}

bool Builder::compile_target(BuildTarget& t, fs::path* output_path) {
    if (opts.auto_deps) auto_discover_deps(t);
    if (opts.git_changed) filter_git_changed(t);
    if (t.sources.empty()) {
        Logger::log(Logger::WARNING, "没有需要编译的源文件");
        return true;
    }
    for (const auto& s : t.sources) {
        if (!fs::exists(s)) {
            Logger::log(Logger::ERROR, "文件不存在: " + s);
            return false;
        }
        if (!is_cpp_source(s)) {
            Logger::log(Logger::ERROR, "不是 C++ 源文件: " + s);
            return false;
        }
    }

    if (!build_pch()) return false;

    fs::path out = resolve_output(t);
    if (output_path) *output_path = out;
    fs::create_directories(cache_dir / "obj");
    std::vector<std::string> objects;
    auto start = std::chrono::steady_clock::now();
    int warnings = 0;

    auto compile_base = base_compile_args(t.shared);
    if (opts.dry_run) {
        Logger::print_title("DRY-RUN 构建计划");
        std::vector<std::string> dry_objects;
        for (const auto& src : t.sources) {
            fs::path obj = object_path_for(src);
            dry_objects.push_back(obj.string());
            std::vector<std::string> args = compile_base;
            args.push_back("-c");
            args.push_back(src);
            args.push_back("-o");
            args.push_back(obj.string());
            std::cout << join_args(args) << "\n";
        }
        std::vector<std::string> link = {confMgr.config.compiler_bin};
        for (const auto& o : dry_objects) link.push_back(o);
        link.push_back("-o");
        link.push_back(out.string());
        auto largs = link_args(t.shared);
        link.insert(link.end(), largs.begin(), largs.end());
        std::cout << join_args(link) << "\n";
        Logger::log(Logger::HINT, "DRY-RUN: 未执行任何编译/链接命令");
        return true;
    }
    struct CompileResult { bool ok; std::string src; std::string obj; std::string sig; std::string raw; int warnings; };
    std::mutex cache_mutex;
    auto compile_one = [&](const std::string& src) -> CompileResult {
        fs::path obj = object_path_for(src);
        std::string sig = signature_for(src, compile_base);
        std::string key = obj.string();
        {
            std::lock_guard<std::mutex> lock(cache_mutex);
            if (fs::exists(obj) && cache_state[key] == sig) {
                return {true, src, obj.string(), sig, "", 0};
            }
        }
        fs::create_directories(obj.parent_path());
        std::vector<std::string> args = compile_base;
        args.push_back("-c");
        args.push_back(src);
        args.push_back("-o");
        args.push_back(obj.string());
        int rc = 1;
        std::string raw = run_capture(join_args(args), &rc);
        int wc = 0;
        std::istringstream rin(raw);
        std::string line;
        while (std::getline(rin, line)) {
            if (line.find(" warning:") != std::string::npos || line.find(": warning:") != std::string::npos) ++wc;
        }
        return {rc == 0, src, obj.string(), sig, raw, wc};
    };

    for (const auto& src : t.sources) objects.push_back(object_path_for(src).string());

    std::vector<CompileResult> results;
    if (opts.jobs <= 1 || t.sources.size() <= 1) {
        for (const auto& src : t.sources) {
            Logger::log(Logger::INFO, "编译: " + src);
            results.push_back(compile_one(src));
        }
    } else {
        Logger::log(Logger::INFO, "并行编译: -j " + std::to_string(opts.jobs));
        std::vector<std::future<CompileResult>> futures;
        size_t next = 0;
        while (next < t.sources.size() || !futures.empty()) {
            while (next < t.sources.size() && futures.size() < static_cast<size_t>(opts.jobs)) {
                std::string src = t.sources[next++];
                Logger::log(Logger::INFO, "编译: " + src);
                futures.push_back(std::async(std::launch::async, compile_one, src));
            }
            results.push_back(futures.front().get());
            futures.erase(futures.begin());
        }
    }

    for (const auto& r : results) {
        if (r.raw.empty() && r.ok) {
            Logger::log(Logger::LOG_DEBUG, "增量跳过: " + r.src);
        }
        if (!r.raw.empty()) std::cout << colorize_compiler_output(r.raw);
        warnings += r.warnings;
        if (!r.ok) {
            error_navigation(r.raw);
            auto end = std::chrono::steady_clock::now();
            print_stats(false, std::chrono::duration<double>(end - start).count(), out, warnings);
            return false;
        }
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache_state[r.obj] = r.sig;
    }

    std::vector<std::string> link = {confMgr.config.compiler_bin};
    for (const auto& o : objects) link.push_back(o);
    link.push_back("-o");
    link.push_back(out.string());
    auto largs = link_args(t.shared);
    link.insert(link.end(), largs.begin(), largs.end());

    Logger::log(Logger::INFO, "链接: " + out.string());
    int rc = 1;
    std::string raw = run_capture(join_args(link), &rc);
    if (!raw.empty()) std::cout << colorize_compiler_output(raw);
    warnings += last_warning_count;
    auto end = std::chrono::steady_clock::now();
    bool ok = rc == 0;
    if (!ok) error_navigation(raw);
    print_stats(ok, std::chrono::duration<double>(end - start).count(), out, warnings);
    return ok;
}

int Builder::run_executable(const fs::path& exe) {
    if (!confMgr.config.run_after_compile) return 0;
    std::vector<double> times;
    int last = 0;
    int rounds = std::max(1, opts.bench);
    for (int i = 0; i < rounds; ++i) {
        Logger::print_title(rounds > 1 ? "程序运行输出 #" + std::to_string(i + 1) : "程序运行输出");
        std::string cmd;
        if (opts.valgrind) {
            if (!command_exists("valgrind")) {
                Logger::log(Logger::ERROR, "未找到 valgrind");
                return 1;
            }
            cmd = "valgrind " + shell_quote(exe.string());
        } else {
#ifdef _WIN32
            cmd = shell_quote(exe.string());
#else
            cmd = executable_invocation(exe);
#endif
        }
        auto start = std::chrono::steady_clock::now();
        last = system(cmd.c_str());
        auto end = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    if (opts.measure_time || opts.bench > 1) {
        double sum = 0;
        for (double t : times) sum += t;
        std::cout << "\n运行耗时: " << std::fixed << std::setprecision(3) << (sum / times.size()) << "ms";
        if (opts.bench > 1) std::cout << " (平均, " << opts.bench << " 次)";
        std::cout << "\n";
    }
    if (confMgr.config.temp_mode) {
        std::remove(exe.string().c_str());
        Logger::log(Logger::HINT, "已清理临时文件");
    }
    return last;
}

bool Builder::diff_test() {
    if (opts.diff_source.empty() || opts.gen_source.empty() || targets.empty()) return false;
    BuildTarget fast = targets.front();
    BuildTarget brute;
    brute.sources = {opts.diff_source};
    brute.output = (cache_dir / "diff_brute").string();
    BuildTarget gen;
    gen.sources = {opts.gen_source};
    gen.output = (cache_dir / "diff_gen").string();
    fs::path fast_exe, brute_exe, gen_exe;
    Logger::print_title("对拍构建");
    if (!compile_target(fast, &fast_exe) || !compile_target(brute, &brute_exe) || !compile_target(gen, &gen_exe)) return true;
    fs::create_directories(cache_dir / "diff");
    for (int i = 1; i <= std::max(1, opts.bench); ++i) {
        fs::path input = cache_dir / "diff" / "input.txt";
        fs::path out1 = cache_dir / "diff" / "fast.txt";
        fs::path out2 = cache_dir / "diff" / "brute.txt";
        system((executable_invocation(gen_exe) + " > " + shell_quote(input.string())).c_str());
        system((executable_invocation(fast_exe) + " < " + shell_quote(input.string()) + " > " + shell_quote(out1.string())).c_str());
        system((executable_invocation(brute_exe) + " < " + shell_quote(input.string()) + " > " + shell_quote(out2.string())).c_str());
        if (read_file(out1) != read_file(out2)) {
            Logger::log(Logger::ERROR, "第 " + std::to_string(i) + " 组数据不一致，输入保存在 " + input.string());
            std::cout << "--- fast ---\n" << read_file(out1) << "--- brute ---\n" << read_file(out2);
            return true;
        }
        Logger::log(Logger::SUCCESS, "对拍通过 #" + std::to_string(i));
    }
    return true;
}

bool Builder::parse_args(int argc, char* argv[]) {
    confMgr.load();
    ProjectToml project = load_project_toml();
    if (project.found) {
        if (!project.compiler.empty()) confMgr.config.compiler_bin = project.compiler;
        if (!project.stdver.empty()) confMgr.config.cpp_standard = project.stdver;
        for (const auto& f : project.flags) confMgr.config.compile_options.push_back(f);
        for (const auto& f : project.link_flags) confMgr.config.link_options.push_back(f);
        for (const auto& p : project.include_paths) add_unique(confMgr.config.include_paths, p);
        for (const auto& p : project.library_paths) add_unique(confMgr.config.library_paths, p);
        for (const auto& l : project.libraries) add_unique(confMgr.config.libraries, l);
        for (const auto& [name, flags] : project.profiles) confMgr.config.profiles[name] = flags;
    }

    BuildTarget current;
    if (argc == 1 && project.found) {
        for (const auto& pat : project.sources) {
            auto expanded = expand_pattern(pat);
            current.sources.insert(current.sources.end(), expanded.begin(), expanded.end());
        }
        current.output = project.output;
        targets.push_back(current);
        detect_package_managers();
        return true;
    }
    if (argc < 2) return false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto need_value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                Logger::log(Logger::ERROR, name + " 需要一个参数");
                return "";
            }
            return argv[++i];
        };

        if (arg == "--target") {
            if (!current.sources.empty() || !current.output.empty()) targets.push_back(current);
            current = BuildTarget{};
        } else if (arg == "-r" || arg == "--run") confMgr.config.run_after_compile = true;
        else if (arg == "-t" || arg == "--temp") confMgr.config.temp_mode = true;
        else if (arg == "-rt" || arg == "-tr") {
            confMgr.config.run_after_compile = true;
            confMgr.config.temp_mode = true;
        } else if (arg == "-o") {
            std::string value = need_value("-o");
            current.output = value;
            current.output_is_dir = looks_like_output_dir(value);
        } else if (arg == "--out-dir") {
            current.output = need_value("--out-dir");
            current.output_is_dir = true;
        } else if (arg == "--shared") current.shared = true;
        else if (arg == "--pch") opts.pch = need_value("--pch");
        else if (arg == "--auto-deps") opts.auto_deps = true;
        else if (arg == "--git-changed") opts.git_changed = true;
        else if (arg == "--profile") opts.profile = need_value("--profile");
        else if (arg == "--analyze") opts.analyze = true;
        else if (arg == "--format") opts.format = true;
        else if (arg == "--time") opts.measure_time = true;
        else if (arg == "--bench") opts.bench = std::max(1, std::atoi(need_value("--bench").c_str()));
        else if (arg == "--valgrind") opts.valgrind = true;
        else if (arg == "--dry-run") opts.dry_run = true;
        else if (arg == "-j" || arg == "--jobs") opts.jobs = std::max(1, std::atoi(need_value(arg).c_str()));
        else if (starts_with(arg, "-j") && arg.size() > 2) opts.jobs = std::max(1, std::atoi(arg.substr(2).c_str()));
        else if (arg == "--diff") opts.diff_source = need_value("--diff");
        else if (arg == "--gen") opts.gen_source = need_value("--gen");
        else if (arg == "--export-compile-commands") opts.export_compile_commands = true;
        else if (starts_with(arg, "-D")) opts.macros.push_back(arg.size() > 2 ? arg.substr(2) : need_value("-D"));
        else if (starts_with(arg, "-I")) opts.include_paths.push_back(arg.size() > 2 ? arg.substr(2) : need_value("-I"));
        else if (starts_with(arg, "-L")) opts.library_paths.push_back(arg.size() > 2 ? arg.substr(2) : need_value("-L"));
        else if (starts_with(arg, "-l")) opts.libraries.push_back(arg.size() > 2 ? arg.substr(2) : need_value("-l"));
        else if (arg == "-h" || arg == "--help" || arg == "--show" || arg == "-s" || arg == "--set" || arg == "--config") {
            return false;
        } else if (!arg.empty() && arg[0] == '-') {
            Logger::log(Logger::WARNING, "忽略未知选项: " + arg);
        } else {
            current.sources.push_back(arg);
        }
    }
    if (!current.sources.empty() || !current.output.empty()) targets.push_back(current);
    if (targets.empty() && project.found) {
        for (const auto& pat : project.sources) {
            auto expanded = expand_pattern(pat);
            current.sources.insert(current.sources.end(), expanded.begin(), expanded.end());
        }
        current.output = project.output;
        targets.push_back(current);
    }
    detect_package_managers();
    return !targets.empty();
}

bool Builder::execute() {
    load_cache();
    std::vector<std::string> all_sources;
    for (auto& t : targets) {
        if (opts.auto_deps) auto_discover_deps(t);
        all_sources.insert(all_sources.end(), t.sources.begin(), t.sources.end());
    }
    if (opts.format && !run_tool_on_sources("format", all_sources)) return false;
    if (opts.analyze && !run_tool_on_sources("analyze", all_sources)) return false;
    if (opts.export_compile_commands && !export_compile_commands(targets)) return false;
    if (!opts.diff_source.empty() || !opts.gen_source.empty()) {
        if (opts.diff_source.empty() || opts.gen_source.empty()) {
            Logger::log(Logger::ERROR, "--diff 和 --gen 必须同时使用");
            return false;
        }
        bool handled = diff_test();
        save_cache();
        return handled;
    }
    bool ok = true;
    fs::path first_output;
    for (size_t i = 0; i < targets.size(); ++i) {
        fs::path out;
        ok = compile_target(targets[i], &out) && ok;
        if (i == 0) first_output = out;
    }
    save_cache();
    if (ok && !targets.empty()) run_executable(first_output);
    return ok;
}
