#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include <conio.h>
#include <fstream>
#include <windows.h>
#include <algorithm>

namespace fs = std::filesystem;

//──────────────────────────────────────────────
// Enable ANSI color globally
#ifdef _WIN32
static void enable_vt_mode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
}
#endif

//──────────────────────────────────────────────
// Prompt + completion helpers
static void print_prompt() {
    std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > ";
}

static std::vector<std::string> complete_in_cwd(const std::string &prefix) {
    std::vector<std::string> results;
    for (auto &entry : fs::directory_iterator(fs::current_path())) {
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0)
            results.push_back(name);
    }
    return results;
}

//──────────────────────────────────────────────
// Readline-style input w/ history support
static std::string read_input(std::vector<std::string> &history, int &historyIndex) {
    std::string input;
    int ch;

    while ((ch = _getch()) != '\r') {
        if (ch == 8) { // Backspace
            if (!input.empty()) {
                input.pop_back();
                std::cout << "\b \b";
            }
        } else if (ch == 224) { // Arrow keys
            ch = _getch();
            if (ch == 72 && historyIndex > 0) { // Up
                --historyIndex;
                input = history[historyIndex];
                std::cout << "\r";
                print_prompt();
                std::cout << input << " \b";
            } else if (ch == 80 && historyIndex + 1 < (int)history.size()) { // Down
                ++historyIndex;
                input = history[historyIndex];
                std::cout << "\r";
                print_prompt();
                std::cout << input << " \b";
            }
        } else if (isprint(ch)) {
            input.push_back((char)ch);
            std::cout << (char)ch;
        }
    }
    std::cout << std::endl;
    return input;
}

//──────────────────────────────────────────────
// Tokenizer
static std::vector<std::string> split(const std::string &s) {
    std::istringstream iss(s);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token)
        tokens.push_back(token);
    return tokens;
}

//──────────────────────────────────────────────
// Command execution
