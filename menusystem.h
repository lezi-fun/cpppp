#pragma once
#include "compiler.h"

enum class Key {
    UP, DOWN, LEFT, RIGHT, ENTER, Q, W, S, A, D, UNKNOWN
};

// Cross-platform unbuffered single-key read (for MenuSystem)
// Coexists with read_nav_key() in logger.h — both are independent.
Key read_key();

class MenuSystem {
    ConfigManager& cm;

    // Compiler detection helpers
    static bool file_exists(const std::string& path);
    static std::vector<std::string> find_executables(const std::vector<std::string>& names);
    static std::string get_compiler_version(const std::string& compiler_cmd);
    std::vector<std::pair<std::string, std::string>> detect_compilers();

    // Hello World test compile after save
    bool test_hello_world();

    // Interactive list selector (returns index or -1 on Q)
    int select_from_list(const std::string& title,
                         const std::vector<std::string>& options,
                         const std::string& extra_info = "");

    // Sub-menus
    void menu_standard();
    void menu_compiler();
    void menu_compile_options();
    void menu_link_options();

public:
    MenuSystem(ConfigManager& mgr);

    void clear_screen();
    void run_config_mode();
};
