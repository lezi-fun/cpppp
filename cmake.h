#pragma once
#include "logger.h"
#include <string>
#include <vector>
#include <map>

// Best-effort CMakeLists.txt parser.
// Handles common patterns: add_executable, target_*, set(), file(GLOB).
// Skips: if/else/endif, foreach, function/macro, generator expressions.

struct CMakeTarget {
    std::string name;
    std::string type;             // "executable" or "library"
    std::vector<std::string> sources;
    std::vector<std::string> include_dirs;
    std::vector<std::string> link_dirs;
    std::vector<std::string> link_libs;
    std::vector<std::string> compile_options;
    std::vector<std::string> defines;
    std::string stdver;
    std::string output_name;
};

struct CMakeProject {
    bool found = false;
    std::string project_name;
    std::string default_stdver;
    std::vector<CMakeTarget> targets;

    // Directory-scoped settings (applied to all targets)
    std::vector<std::string> global_include_dirs;
    std::vector<std::string> global_link_dirs;
    std::vector<std::string> global_compile_options;
    std::vector<std::string> global_defines;

    // Returns a target by name, or nullptr
    const CMakeTarget* find_target(const std::string& name) const;

    // Pick a reasonable default target (first executable, or first target)
    const CMakeTarget* default_target() const;
};

// Parse CMakeLists.txt in the given directory (or current dir if empty).
// Returns a CMakeProject with .found = true on success.
CMakeProject parse_cmake(const std::string& dir = ".");

// Build using the parsed CMake project.
// If target_name is empty, picks the default target.
// Returns exit code (0 = success).
int build_cmake(CMakeProject& proj, const std::string& target_name = "");
