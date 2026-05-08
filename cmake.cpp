#include "cmake.h"
#include "compiler.h"
#include "builder.h"

// ---- helpers ----

static std::string unquote(const std::string& s) {
    std::string v = trim(s);
    if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
        return v.substr(1, v.size() - 2);
    return v;
}

// Split a CMake argument list, respecting quotes and nested parens lightly.
static std::vector<std::string> split_cmake_args(const std::string& body) {
    std::vector<std::string> args;
    std::string cur;
    bool in_quote = false;
    char quote_char = 0;
    int paren_depth = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (in_quote) {
            if (c == quote_char) { in_quote = false; continue; }
            cur += c;
        } else if ((c == '"' || c == '\'') && paren_depth == 0) {
            in_quote = true;
            quote_char = c;
        } else if (c == '(') {
            paren_depth++;
            if (paren_depth == 1) continue; // skip opening paren of command
            cur += c;
        } else if (c == ')') {
            paren_depth--;
            if (paren_depth == 0) {
                if (!cur.empty()) { args.push_back(unquote(cur)); cur.clear(); }
                continue;
            }
            cur += c;
        } else if (c == '#' && paren_depth == 0 && cur.empty()) {
            // Comment — skip rest of line
            break;
        } else if (std::isspace(static_cast<unsigned char>(c)) && paren_depth == 0) {
            if (!cur.empty()) { args.push_back(unquote(cur)); cur.clear(); }
        } else if (paren_depth >= 0) {
            cur += c;
        }
    }
    if (!cur.empty()) args.push_back(unquote(cur));
    return args;
}

// Expand ${VAR} references using a variable map
static std::string expand_cmake_vars(const std::string& s, const std::map<std::string, std::string>& vars) {
    std::string result;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') {
            size_t end = s.find('}', i + 2);
            if (end != std::string::npos) {
                std::string varname = s.substr(i + 2, end - i - 2);
                auto it = vars.find(varname);
                if (it != vars.end()) result += it->second;
                else result += "${" + varname + "}"; // keep unresolved
                i = end + 1;
                continue;
            }
        }
        result += s[i];
        ++i;
    }
    return result;
}

// Expand file(GLOB ...) — naive: only handles simple GLOB without CONFIGURE_DEPENDS
static std::vector<std::string> expand_cmake_glob(const std::vector<std::string>& args) {
    std::vector<std::string> result;
    if (args.size() < 2) return result;
    // args[0] might be variable name for GLOB result assignment, or GLOB keyword
    size_t start = 0;
    if (args[0] == "GLOB" || args[0] == "GLOB_RECURSE") start = 1;
    else start = 2; // VAR GLOB pattern...
    for (size_t i = start; i < args.size(); ++i) {
        auto expanded = expand_pattern(args[i]);
        result.insert(result.end(), expanded.begin(), expanded.end());
    }
    return result;
}

// ---- parse ----

CMakeProject parse_cmake(const std::string& dir) {
    CMakeProject proj;
    fs::path cmake_path = fs::path(dir) / "CMakeLists.txt";
    if (!fs::exists(cmake_path)) return proj;
    proj.found = true;

    std::ifstream in(cmake_path);
    if (!in.is_open()) { proj.found = false; return proj; }

    // Read entire file content
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();

    // First pass: collect set() variables
    std::map<std::string, std::string> vars;
    // Also collect GLOB results
    std::map<std::string, std::vector<std::string>> glob_vars;

    // We'll do a simple line-based regex approach
    // Actually, let's find commands by matching pattern: command_name(...)
    std::regex cmd_re(R"(\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\()");

    // Build a list of (position, command_name, body_start) by scanning
    struct CmdInfo { size_t pos; size_t body_start; std::string name; };
    std::vector<CmdInfo> cmds;
    {
        std::sregex_iterator it(content.begin(), content.end(), cmd_re), end;
        for (; it != end; ++it) {
            CmdInfo ci;
            ci.pos = it->position();
            ci.name = (*it)[1].str();
            ci.body_start = ci.pos + it->length();
            cmds.push_back(ci);
        }
    }

    // Function to extract body text (handle nested parens)
    auto extract_body = [&](size_t start) -> std::string {
        int depth = 1;
        for (size_t i = start; i < content.size(); ++i) {
            if (content[i] == '(') depth++;
            else if (content[i] == ')') {
                depth--;
                if (depth == 0) return content.substr(start, i - start);
            }
        }
        return content.substr(start); // unmatched
    };

    // First pass: handle set(), file(GLOB), project(), set(CMAKE_CXX_STANDARD ...)
    for (auto& ci : cmds) {
        std::string body_text = extract_body(ci.body_start);
        auto args = split_cmake_args(body_text); // re-parse body
        if (args.empty()) continue;

        if (ci.name == "set" && args.size() >= 2) {
            std::string val = args[1];
            for (size_t i = 2; i < args.size(); ++i) val += " " + args[i];
            vars[args[0]] = val;
            if (args[0] == "CMAKE_CXX_STANDARD") proj.default_stdver = "c++" + args[1];
        }
        else if (ci.name == "project" && args.size() >= 1) {
            proj.project_name = args[0];
            // project(name VERSION ... LANGUAGES CXX)
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i] == "VERSION" && i + 1 < args.size()) { /* skip */ }
            }
        }
        else if (ci.name == "file" && args.size() >= 2) {
            if (args[0] == "GLOB" || args[0] == "GLOB_RECURSE") {
                std::string varname = args[0] == "GLOB" ? (args.size() >= 3 ? args[1] : "") : (args.size() >= 3 ? args[1] : "");
                std::vector<std::string> fargs(args.begin() + 1, args.end());
                if (args[0] == "GLOB" && args.size() >= 3) fargs = std::vector<std::string>(args.begin() + 2, args.end());
                auto files = expand_cmake_glob(fargs);
                if (!varname.empty()) glob_vars[varname] = files;
            }
        }
    }

    // Second pass: handle target_* and add_* commands
    CMakeTarget* current_target = nullptr;

    for (auto& ci : cmds) {
        std::string body_text = extract_body(ci.body_start);
        // Expand variables before splitting
        body_text = expand_cmake_vars(body_text, vars);
        auto args = split_cmake_args(body_text);
        if (args.empty()) continue;

        if (ci.name == "add_executable" && args.size() >= 2) {
            CMakeTarget t;
            t.name = args[0];
            t.type = "executable";
            for (size_t i = 1; i < args.size(); ++i) t.sources.push_back(args[i]);
            proj.targets.push_back(t);
            current_target = &proj.targets.back();
        }
        else if (ci.name == "add_library" && args.size() >= 2) {
            CMakeTarget t;
            t.name = args[0];
            t.type = "library";
            for (size_t i = 1; i < args.size(); ++i) t.sources.push_back(args[i]);
            proj.targets.push_back(t);
            current_target = &proj.targets.back();
        }
        else if (ci.name == "target_sources" && args.size() >= 2) {
            std::string tname = args[0];
            for (auto& t : proj.targets) {
                if (t.name == tname) {
                    // args[1] might be PUBLIC/PRIVATE/INTERFACE
                    size_t si = 1;
                    if (si < args.size() && (args[si] == "PUBLIC" || args[si] == "PRIVATE" || args[si] == "INTERFACE")) si++;
                    for (; si < args.size(); ++si) t.sources.push_back(args[si]);
                }
            }
        }
        else if (ci.name == "target_include_directories" && args.size() >= 2) {
            std::string tname = args[0];
            for (auto& t : proj.targets) {
                if (t.name == tname) {
                    size_t si = 1;
                    if (si < args.size() && (args[si] == "PUBLIC" || args[si] == "PRIVATE" || args[si] == "INTERFACE")) si++;
                    for (; si < args.size(); ++si) t.include_dirs.push_back(args[si]);
                }
            }
        }
        else if (ci.name == "target_link_libraries" && args.size() >= 2) {
            std::string tname = args[0];
            for (auto& t : proj.targets) {
                if (t.name == tname) {
                    size_t si = 1;
                    if (si < args.size() && (args[si] == "PUBLIC" || args[si] == "PRIVATE" || args[si] == "INTERFACE")) si++;
                    for (; si < args.size(); ++si) t.link_libs.push_back(args[si]);
                }
            }
        }
        else if (ci.name == "target_link_directories" && args.size() >= 2) {
            std::string tname = args[0];
            for (auto& t : proj.targets) {
                if (t.name == tname) {
                    size_t si = 1;
                    if (si < args.size() && (args[si] == "PUBLIC" || args[si] == "PRIVATE" || args[si] == "INTERFACE")) si++;
                    for (; si < args.size(); ++si) t.link_dirs.push_back(args[si]);
                }
            }
        }
        else if (ci.name == "target_compile_options" && args.size() >= 2) {
            std::string tname = args[0];
            for (auto& t : proj.targets) {
                if (t.name == tname) {
                    size_t si = 1;
                    if (si < args.size() && (args[si] == "PUBLIC" || args[si] == "PRIVATE" || args[si] == "INTERFACE")) si++;
                    for (; si < args.size(); ++si) t.compile_options.push_back(args[si]);
                }
            }
        }
        else if (ci.name == "target_compile_definitions" && args.size() >= 2) {
            std::string tname = args[0];
            for (auto& t : proj.targets) {
                if (t.name == tname) {
                    size_t si = 1;
                    if (si < args.size() && (args[si] == "PUBLIC" || args[si] == "PRIVATE" || args[si] == "INTERFACE")) si++;
                    for (; si < args.size(); ++si) t.defines.push_back(args[si]);
                }
            }
        }
        else if (ci.name == "target_compile_features" && args.size() >= 2) {
            std::string tname = args[0];
            for (auto& t : proj.targets) {
                if (t.name == tname) {
                    for (size_t si = 1; si < args.size(); ++si) {
                        if (args[si].rfind("cxx_std_", 0) == 0)
                            t.stdver = "c++" + args[si].substr(8);
                    }
                }
            }
        }
        else if (ci.name == "include_directories") {
            for (auto& a : args) proj.global_include_dirs.push_back(a);
        }
        else if (ci.name == "link_directories") {
            for (auto& a : args) proj.global_link_dirs.push_back(a);
        }
        else if (ci.name == "add_compile_options") {
            for (auto& a : args) proj.global_compile_options.push_back(a);
        }
        else if (ci.name == "add_definitions") {
            for (auto& a : args) proj.global_defines.push_back(a);
        }
    }

    // Expand file(GLOB) results into target sources
    for (auto& t : proj.targets) {
        std::vector<std::string> expanded_sources;
        for (auto& s : t.sources) {
            if (s.find("${") != std::string::npos) {
                // Try glob_vars
                std::string vname = s;
                // Extract ${VAR} from string
                std::regex var_re(R"(\$\{([^}]+)\})");
                std::smatch m;
                if (std::regex_match(s, m, var_re)) {
                    auto it = glob_vars.find(m[1].str());
                    if (it != glob_vars.end()) {
                        for (auto& f : it->second) expanded_sources.push_back(f);
                        continue;
                    }
                }
            }
            expanded_sources.push_back(s);
        }
        t.sources = expanded_sources;
    }

    // Merge global settings into each target
    for (auto& t : proj.targets) {
        for (auto& d : proj.global_include_dirs) t.include_dirs.push_back(d);
        for (auto& d : proj.global_link_dirs) t.link_dirs.push_back(d);
        for (auto& o : proj.global_compile_options) t.compile_options.push_back(o);
        for (auto& d : proj.global_defines) t.defines.push_back(d);
        if (t.stdver.empty()) t.stdver = proj.default_stdver;
    }

    return proj;
}

// ---- CMakeProject methods ----

const CMakeTarget* CMakeProject::find_target(const std::string& name) const {
    for (auto& t : targets)
        if (t.name == name) return &t;
    return nullptr;
}

const CMakeTarget* CMakeProject::default_target() const {
    for (auto& t : targets)
        if (t.type == "executable") return &t;
    if (!targets.empty()) return &targets[0];
    return nullptr;
}

// ---- build ----

int build_cmake(CMakeProject& proj, const std::string& target_name) {
    const CMakeTarget* t = target_name.empty()
        ? proj.default_target()
        : proj.find_target(target_name);

    if (!t) {
        Logger::log(Logger::ERROR, "未找到 CMake target: " + (target_name.empty() ? "(默认)" : target_name));
        return 1;
    }

    Logger::log(Logger::INFO, "从 CMakeLists.txt 构建 target: " + t->name + " (" + t->type + ")");
    if (!t->stdver.empty()) Logger::log(Logger::INFO, "C++ 标准: " + t->stdver);

    // Build a mock argc/argv for Builder — only sources and output
    std::vector<std::string> fake_args = {"c+++", "-r"};

    // Source files
    for (auto& s : t->sources) fake_args.push_back(s);

    // Output
    if (!t->output_name.empty()) {
        fake_args.push_back("-o");
        fake_args.push_back(t->output_name);
    }

    // Convert to char* array
    std::vector<char*> argv_store;
    for (auto& a : fake_args) argv_store.push_back(const_cast<char*>(a.data()));
    int argc = static_cast<int>(argv_store.size());
    char** argv = argv_store.data();

    // parse_args first (handles sources, output, run flags)
    // CMake config is set AFTER because parse_args calls load() internally
    ConfigManager cm;
    Builder builder(cm);
    if (!builder.parse_args(argc, argv)) {
        Logger::log(Logger::ERROR, "CMake 项目构建失败：参数解析错误");
        return 1;
    }

    // Now override with CMake-extracted settings
    cm.load();
    if (!t->stdver.empty()) cm.config.cpp_standard = t->stdver;
    for (auto& d : t->include_dirs) cm.config.include_paths.push_back(d);
    for (auto& d : t->defines) {
        std::string def = d;
        if (def.rfind("-D", 0) == 0) def = def.substr(2);
        cm.config.macros.push_back(def);
    }
    for (auto& o : t->compile_options) cm.config.compile_options.push_back(o);
    for (auto& d : t->link_dirs) cm.config.library_paths.push_back(d);
    for (auto& l : t->link_libs) {
        std::string lib = l;
        if (lib.rfind("-l", 0) == 0) lib = lib.substr(2);
        if (lib.find("::") != std::string::npos) {
            Logger::log(Logger::WARNING, "跳过 CMake 导入 target 引用: " + lib);
            continue;
        }
        cm.config.libraries.push_back(lib);
    }

    return builder.execute() ? 0 : 1;
}
