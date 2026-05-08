#pragma once
#include "logger.h"

struct CompilerConfig {
    std::string compiler_bin = "g++";
    std::string cpp_standard = "c++23";
    std::vector<std::string> compile_options = {"-O2", "-Wall", "-Wextra"};
    std::vector<std::string> link_options;
    std::vector<std::string> macros;
    std::vector<std::string> include_paths;
    std::vector<std::string> library_paths;
    std::vector<std::string> libraries;
    std::map<std::string, std::vector<std::string>> profiles;
    bool enable_sanitizers = false;
    bool run_after_compile = false;
    bool temp_mode = false;
};

class ConfigManager {
    std::string config_file_path;

    static std::vector<std::string> split_csv(const std::string& v) {
        std::vector<std::string> out;
        std::istringstream iss(v);
        std::string token;
        while (std::getline(iss, token, ',')) {
            token = trim(token);
            if (!token.empty()) out.push_back(token);
        }
        return out;
    }

    static std::string join_csv(const std::vector<std::string>& v) {
        std::ostringstream ss;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) ss << ",";
            ss << v[i];
        }
        return ss.str();
    }

public:
    CompilerConfig config;

    ConfigManager() {
#ifdef _WIN32
        char* appdata = std::getenv("APPDATA");
        config_file_path = appdata ? std::string(appdata) + "\\cpp_compiler_config_v3.txt" : "cpp_compiler_config_v3.txt";
#else
        char* home = std::getenv("HOME");
        config_file_path = home ? std::string(home) + "/.cpp_compiler_config_v3" : ".cpp_compiler_config_v3";
#endif
        config.profiles["release"] = {"-O3", "-DNDEBUG"};
        config.profiles["debug-sanitizer"] = {"-g", "-O0", "-fsanitize=address,undefined"};
        config.profiles["contest"] = {"-O2", "-Wall", "-Wextra", "-DONLINE_JUDGE"};
    }

    bool save() {
        std::ofstream file(config_file_path);
        if (!file.is_open()) return false;
        file << "compiler=" << config.compiler_bin << "\n";
        file << "std=" << config.cpp_standard << "\n";
        file << "sanitizers=" << (config.enable_sanitizers ? "1" : "0") << "\n";
        file << "c_opts=" << join_csv(config.compile_options) << "\n";
        file << "l_opts=" << join_csv(config.link_options) << "\n";
        file << "macros=" << join_csv(config.macros) << "\n";
        file << "include=" << join_csv(config.include_paths) << "\n";
        file << "lib_paths=" << join_csv(config.library_paths) << "\n";
        file << "libs=" << join_csv(config.libraries) << "\n";
        for (const auto& [name, flags] : config.profiles) {
            file << "profile." << name << "=" << join_csv(flags) << "\n";
        }
        return true;
    }

    bool load() {
        std::ifstream file(config_file_path);
        if (!file.is_open()) return false;
        std::string line;
        while (std::getline(file, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = trim(line.substr(0, eq));
            std::string v = trim(line.substr(eq + 1));
            if (k == "compiler") config.compiler_bin = v;
            else if (k == "std") config.cpp_standard = v;
            else if (k == "sanitizers") config.enable_sanitizers = (v == "1");
            else if (k == "c_opts") config.compile_options = split_csv(v);
            else if (k == "l_opts") config.link_options = split_csv(v);
            else if (k == "macros") config.macros = split_csv(v);
            else if (k == "include") config.include_paths = split_csv(v);
            else if (k == "lib_paths") config.library_paths = split_csv(v);
            else if (k == "libs") config.libraries = split_csv(v);
            else if (starts_with(k, "profile.")) config.profiles[k.substr(8)] = split_csv(v);
        }
        return true;
    }
};
