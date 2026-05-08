#pragma once
#include "build.h"
#include "compiler.h"

class Builder {
    ConfigManager& confMgr;
    BuildOptions opts;
    std::vector<BuildTarget> targets;
    fs::path cache_dir = ".c+++_cache";
    std::unordered_map<std::string, std::string> cache_state;
    int last_warning_count = 0;
    int last_error_count = 0;

    static bool looks_like_output_dir(const std::string& value);
    void add_unique(std::vector<std::string>& v, const std::string& s);
    void load_cache();
    void save_cache();
    std::vector<std::string> base_compile_args(bool shared) const;
    std::vector<std::string> link_args(bool shared) const;
    fs::path object_path_for(const fs::path& src) const;
    std::string signature_for(const fs::path& src, const std::vector<std::string>& compile_args) const;
    std::string colorize_compiler_output(const std::string& out);
    void error_navigation(const std::string& out);
    void print_stats(bool ok, double seconds, const fs::path& out, int warnings);
    fs::path resolve_output(const BuildTarget& t);
    std::vector<std::string> find_project_cpp_for_header(const fs::path& header);
    fs::path resolve_include(const fs::path& base, const std::string& inc);
    void auto_discover_deps(BuildTarget& t);
    void filter_git_changed(BuildTarget& t);
    bool build_pch();
    void detect_package_managers();
    bool run_tool_on_sources(const std::string& tool_name, const std::vector<std::string>& sources);
    bool export_compile_commands(const std::vector<BuildTarget>& ts);
    bool compile_target(BuildTarget& t, fs::path* output_path = nullptr);
    int run_executable(const fs::path& exe);
    bool diff_test();

public:
    Builder(ConfigManager& cm);

    bool parse_args(int argc, char* argv[]);
    bool execute();
};
