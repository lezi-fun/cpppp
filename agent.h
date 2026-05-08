#pragma once
#include "compiler.h"
#include "builder.h"

class AgentMode {
    ConfigManager& cm;

    static std::string json_str(const std::string& s);
    static std::string json_arr(const std::vector<std::string>& v);
    static std::string json_bool(bool b);

    void cmd_config_show();
    void cmd_config_set(const std::vector<std::string>& args);
    void cmd_config_add(const std::vector<std::string>& args);
    void cmd_config_del(const std::vector<std::string>& args);
    void cmd_config_sanitizers(const std::vector<std::string>& args);
    void cmd_config_preset(const std::vector<std::string>& args);
    void cmd_config_save();
    void cmd_config_load();
    void cmd_scan();
    void cmd_build(const std::vector<std::string>& args);
    void cmd_run(const std::vector<std::string>& args);

public:
    AgentMode(ConfigManager& mgr);
    int execute(int argc, char* argv[]);
    static void show_help();
};
