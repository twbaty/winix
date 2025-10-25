#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <windows.h>
#include <conio.h>
#include <cstdlib>
#include <algorithm>
#include <regex>
#include <process.h>
#include <fstream>

namespace fs = std::filesystem;

constexpr int HISTORY_LIMIT = 50;
const std::string HISTORY_FILE = "winix_history.txt";

// ──────────────────────────────────────────────
// Enable UTF-8 and ANSI color
static void enable_vt_mode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

// ──────────────────────────────────────────────
static void print_prompt() {
    std::cout << "\033[1;32m[Winix]\033[0m "
              << fs::current_path().string() << " > " << std::flush;
}

// ──────────────────────────────────────────────
// Quote-aware tokenizer
static std::vector<std::string> split_command(const std::string &line) {
    std::vector<std::string> out;
    std::string token;
    bool inQuote = false; char quote = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if ((c == '"' || c == '\'') && !inQuote) { inQuote = true; quote = c; }
        else if (inQuote && c == quote) inQuote = false;
        else if (!inQuote && isspace((unsigned char)c)) {
            if (!token.empty()) { out.push_back(token); token.clear(); }
        } else token.push_back(c);
    }
    if (!token.empty()) out.push_back(token);
    return out;
}

// ──────────────────────────────────────────────
// Wildcard expansion
static std::vector<std::string> expand_wildcards(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    for (auto &a : args) {
        if (a.find('*') == std::string::npos && a.find('?') == std::string::npos) {
            out.push_back(a);
            continue;
        }
        std::regex pat(std::regex_replace(std::regex_replace(a, std::regex("\\*"), ".*"),
                                          std::regex("\\?"), "."));
        bool matched = false;
        for (auto &e : fs::directory_iterator(fs::current_path())) {
            std::string name = e.path().filename().string();
            if (std::regex_match(name, pat)) {
                out.push_back(name);
                matched = true;
            }
        }
        if (!matched) out.push_back(a);
    }
    return out;
}

// ──────────────────────────────────────────────
// Tab completion
static std::vector<std::string> complete_in_cwd(const std::string &prefix) {
    std::vector<std::string> matches;
    for (auto &e : fs::directory_iterator(fs::current_path())) {
        std::string name = e.path().filename().string();
        if (name.rfind(prefix, 0) == 0)
            matches.push_back(name);
    }
    return matches;
}

// ──────────────────────────────────────────────
// Load/save history
static void load_history(std::vector<std::string>& h) {
    std::ifstream f(HISTORY_FILE);
    std::string line;
    while (getline(f, line)) if (!line.empty()) h.push_back(line);
    if (h.size() > HISTORY_LIMIT)
        h.erase(h.begin(), h.end() - HISTORY_LIMIT);
}
static void save_history(const std::vector<std::string>& h) {
    std::ofstream f(HISTORY_FILE, std::ios::trunc);
    size_t start = (h.size() > HISTORY_LIMIT) ? h.size() - HISTORY_LIMIT : 0;
    for (size_t i = start; i < h.size(); ++i) f << h[i] << "\n";
}

// ──────────────────────────────────────────────
// Simple input with history + tab
static std::string read_input(std::vector<std::string>& hist, int& idx) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldMode; GetConsoleMode(hin, &oldMode);
    SetConsoleMode(hin, ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT);

    std::string input; INPUT_RECORD rec; DWORD count;

    while (true) {
        ReadConsoleInput(hin, &rec, 1, &count);
        if (rec.EventType != KEY_EVENT) continue;
        const KEY_EVENT_RECORD& key = rec.Event.KeyEvent;
        if (!key.bKeyDown) continue;

        switch (key.wVirtualKeyCode) {
        case VK_RETURN:
            std::cout << "\n"; SetConsoleMode(hin, oldMode); return input;

        case VK_BACK:
            if (!input.empty()) { input.pop_back(); std::cout << "\b \b" << std::flush; }
            break;

        case VK_TAB: {
            std::string prefix;
            size_t pos = input.find_last_of(' ');
            prefix = (pos == std::string::npos) ? input : input.substr(pos + 1);
            auto matches = complete_in_cwd(prefix);
            if (matches.size() == 1) {
                auto suf = matches[0].substr(prefix.size());
                input += suf; std::cout << suf << std::flush;
            } else if (!matches.empty()) {
                std::cout << "\n";
                for (auto &m : matches) std::cout << "  " << m << "\n";
                print_prompt(); std::cout << input << std::flush;
            }
            break;
        }

        case VK_UP:
            if (idx > 0) { idx--; input = hist[idx]; print_prompt(); std::cout << input << std::flush; }
            break;
        case VK_DOWN:
            if (idx + 1 < (int)hist.size()) { idx++; input = hist[idx]; }
            else { idx = hist.size(); input.clear(); }
            print_prompt(); std::cout << input << std::flush;
            break;

        default:
            if (key.uChar.AsciiChar >= 32 && key.uChar.AsciiChar < 127) {
                input.push_back(key.uChar.AsciiChar);
                std::cout << key.uChar.AsciiChar << std::flush;
            }
            break;
        }
    }
}

// ──────────────────────────────────────────────
// Execute
static void execute_command(std::vector<std::string> args) {
    if (args.empty()) return;

    // built-ins
    if (args[0] == "exit") { std::cout << "Goodbye.\n"; exit(0); }
    if (args[0] == "cd") {
        if (args.size() < 2) return;
        try { fs::current_path(args[1]); }
        catch (...) { std::cerr << "cd: cannot access " << args[1] << "\n"; }
        return;
    }
    if (args[0] == "echo") {
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) std::cout << " ";
            std::cout << args[i];
        }
        std::cout << "\n"; return;
    }

    // resolve full path to command
    std::string cmdPath = (fs::path("build") / (args[0] + ".exe")).string();
    if (!fs::exists(cmdPath)) cmdPath = args[0]; // fallback to PATH

    std::vector<char*> argv;
    for (auto &a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    int r = _spawnvp(_P_WAIT, cmdPath.c_str(), argv.data());
    if (r == -1)
        std::cerr << args[0] << ": command not found or failed\n";
}

// ──────────────────────────────────────────────
int main() {
    enable_vt_mode();
    std::cout << "Winix Shell v1.4 (spawn fix, history cap, completion)\n";

    std::vector<std::string> history; load_history(history);
    int hIndex = history.size();

    std::string path = std::getenv("PATH") ? std::getenv("PATH") : "";
    path += ";build";
    _putenv_s("PATH", path.c_str());

    while (true) {
        print_prompt();
        std::string line = read_input(history, hIndex);
        if (line.empty()) continue;

        history.push_back(line); hIndex = history.size(); save_history(history);
        auto args = expand_wildcards(split_command(line));
        execute_command(args);
    }
}
