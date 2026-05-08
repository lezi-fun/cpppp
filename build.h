#pragma once
#include "logger.h"

struct BuildTarget {
    std::vector<std::string> sources;
    std::string output;
    bool output_is_dir = false;
    bool shared = false;
};

struct BuildOptions {
    bool auto_deps = false;
    bool git_changed = false;
    bool analyze = false;
    bool format = false;
    bool export_compile_commands = false;
    bool measure_time = false;
    bool valgrind = false;
    int bench = 1;
    std::string profile;
    std::string pch;
    std::string diff_source;
    std::string gen_source;
    std::vector<std::string> macros;
    std::vector<std::string> include_paths;
    std::vector<std::string> library_paths;
    std::vector<std::string> libraries;
};

struct ProjectToml {
    bool found = false;
    std::string compiler;
    std::string stdver;
    std::vector<std::string> sources;
    std::vector<std::string> include_paths;
    std::vector<std::string> library_paths;
    std::vector<std::string> libraries;
    std::vector<std::string> flags;
    std::vector<std::string> link_flags;
    std::string output;
    std::map<std::string, std::vector<std::string>> profiles;
};

static inline std::vector<std::string> parse_toml_array(const std::string& value) {
    std::vector<std::string> out;
    std::regex item("\"([^\"]*)\"");
    for (std::sregex_iterator it(value.begin(), value.end(), item), end; it != end; ++it) {
        out.push_back((*it)[1].str());
    }
    return out;
}

static inline std::string parse_toml_string(const std::string& value) {
    std::string v = trim(value);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') return v.substr(1, v.size() - 2);
    return v;
}

static inline ProjectToml load_project_toml() {
    ProjectToml p;
    fs::path path = "c+++.toml";
    if (!fs::exists(path)) return p;
    p.found = true;

    std::ifstream in(path);
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        size_t comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (section == "build") {
            if (key == "compiler") p.compiler = parse_toml_string(value);
            else if (key == "std") p.stdver = parse_toml_string(value);
            else if (key == "sources") p.sources = parse_toml_array(value);
            else if (key == "include") p.include_paths = parse_toml_array(value);
            else if (key == "library_paths" || key == "lib_paths") p.library_paths = parse_toml_array(value);
            else if (key == "libraries" || key == "libs") p.libraries = parse_toml_array(value);
            else if (key == "flags") p.flags = parse_toml_array(value);
            else if (key == "link_flags") p.link_flags = parse_toml_array(value);
            else if (key == "output") p.output = parse_toml_string(value);
        } else if (starts_with(section, "profile.") && key == "flags") {
            p.profiles[section.substr(8)] = parse_toml_array(value);
        }
    }
    return p;
}

static inline std::vector<std::string> expand_pattern(const std::string& pattern) {
    std::vector<std::string> out;
    if (pattern.find('*') == std::string::npos) {
        if (fs::exists(pattern)) out.push_back(pattern);
        return out;
    }
    fs::path p(pattern);
    fs::path dir = p.parent_path().empty() ? "." : p.parent_path();
    std::string name = p.filename().string();
    std::string regex_text = std::regex_replace(name, std::regex(R"(\.)"), R"(\.)");
    regex_text = std::regex_replace(regex_text, std::regex(R"(\*)"), ".*");
    std::regex re(regex_text);
    if (!fs::exists(dir)) return out;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (fs::is_regular_file(e) && std::regex_match(e.path().filename().string(), re)) {
            out.push_back(e.path().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}
