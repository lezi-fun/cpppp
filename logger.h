#pragma once
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#define PATH_SEPARATOR "\\"
#define POPEN _popen
#define PCLOSE _pclose
#else
#include <termios.h>
#include <unistd.h>
#define PATH_SEPARATOR "/"
#define POPEN popen
#define PCLOSE pclose
#endif

namespace fs = std::filesystem;

class Logger {
public:
    enum Level { INFO, SUCCESS, WARNING, ERROR, HINT, LOG_DEBUG };

    static void setup_console() {
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
#endif
    }

    static void log(Level level, const std::string& msg) {
        switch (level) {
            case INFO: std::cout << "\033[94m[INFO] \033[0m" << msg << "\n"; break;
            case SUCCESS: std::cout << "\033[92m[DONE] \033[0m" << msg << "\n"; break;
            case WARNING: std::cout << "\033[93m[WARN] \033[0m" << msg << "\n"; break;
            case ERROR: std::cerr << "\033[91m[ERR ] \033[0m" << msg << "\n"; break;
            case HINT: std::cout << "\033[96m[HINT] \033[0m" << msg << "\n"; break;
            case LOG_DEBUG: std::cout << "\033[90m[DEBG] \033[0m" << msg << "\n"; break;
        }
    }

    static void print_title(const std::string& title) {
        std::cout << "\033[1;36m" << title << "\033[0m\n";
        std::cout << std::string(44, '-') << "\n";
        std::cout.flush();
    }
};

// ---- 工具函数 ----
static inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static inline bool starts_with(const std::string& s, const std::string& p) {
    return s.rfind(p, 0) == 0;
}

static inline bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

static inline std::string shell_quote(const std::string& s) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : s) out += (c == '"') ? "\\\"" : std::string(1, c);
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
#endif
}

static inline std::string join_args(const std::vector<std::string>& args) {
    std::ostringstream ss;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) ss << " ";
        ss << shell_quote(args[i]);
    }
    return ss.str();
}

static inline std::string executable_invocation(const fs::path& exe) {
    std::string path = exe.string();
#ifdef _WIN32
    return shell_quote(path);
#else
    if (exe.is_absolute()) return shell_quote(path);
    return shell_quote("./" + path);
#endif
}

static inline std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

static inline bool is_cpp_source(const fs::path& p) {
    static const std::set<std::string> exts = {".cpp", ".cxx", ".cc", ".c++", ".C", ".cp"};
    return exts.count(p.extension().string()) > 0;
}

static inline bool is_header(const fs::path& p) {
    static const std::set<std::string> exts = {".hpp", ".hh", ".hxx", ".h", ".ipp"};
    return exts.count(p.extension().string()) > 0;
}

static inline std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static inline std::string file_hash(const fs::path& p) {
    std::string data = read_file(p);
    auto h = std::hash<std::string>{}(data);
    std::ostringstream ss;
    ss << std::hex << h;
    return ss.str();
}

static inline std::string sanitize_path(const fs::path& p) {
    std::string s = fs::absolute(p).lexically_normal().string();
    for (char& c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) c = '_';
    }
    return s;
}

static inline std::string run_capture(const std::string& cmd, int* code = nullptr) {
    std::string full = cmd + " 2>&1";
    FILE* pipe = POPEN(full.c_str(), "r");
    if (!pipe) {
        if (code) *code = -1;
        return "";
    }
    char buffer[4096];
    std::string out;
    while (fgets(buffer, sizeof(buffer), pipe)) out += buffer;
    int rc = PCLOSE(pipe);
    if (code) *code = rc;
    return out;
}

static inline bool command_exists(const std::string& cmd) {
#ifdef _WIN32
    std::string probe = "where " + shell_quote(cmd);
#else
    std::string probe = "command -v " + shell_quote(cmd);
#endif
    int rc = 1;
    run_capture(probe, &rc);
    return rc == 0;
}

static inline int read_nav_key() {
#ifdef _WIN32
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        c = _getch();
        if (c == 72) return -1;
        if (c == 80) return 1;
        return 2;
    }
    if (c == 13) return 0;
    if (c == 'q' || c == 'Q') return 9;
    if (c == 'w' || c == 'W') return -1;
    if (c == 's' || c == 'S') return 1;
    return 2;
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int c = getchar();
    if (c == 27) {
        int c2 = getchar();
        int c3 = getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        if (c2 == '[' && c3 == 'A') return -1;
        if (c2 == '[' && c3 == 'B') return 1;
        return 2;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    if (c == 10 || c == 13) return 0;
    if (c == 'q' || c == 'Q') return 9;
    if (c == 'w' || c == 'W') return -1;
    if (c == 's' || c == 'S') return 1;
    return 2;
#endif
}
