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

// ──────────────────────────────────────────────
// Enable ANSI/VT color globally on Windows
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

// ──────────────────────────────────────────────
// Simple helpers (these are unchanged)
static std::vector<std::string> complete_in_cwd(const std::string &prefix) {
    std::vector<std::string> results;
    for (const auto &entry : fs::directory_iterator(fs::current_path())) {
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0)
            results.push_back(name);
    }
    return results;
}

static void print_prompt() {
    std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > ";
}

// ──────────────────────────────────────────────
// Basic readline-style input with history
static std::string read_input(std::vector<std::string> &history, int &historyIndex) {
    std::string input;
    int ch;

    while ((ch = _getch()) != '\r') { // Enter key
        if (ch == 8) { // Backspace
            if (!input.empty()) {
                input.pop_back();
                std::cout << "\b \b";
            }
        } else if (ch == 224) { // Arrow keys
            ch = _getch();
            if (ch == 72 && historyIndex > 0) { // Up arrow
                --historyIndex;
                input = history[historyIndex];
                std::cout << "\r";
                print_prompt();
                std::cout << input << " \b";
            } else if (ch == 80 && historyIndex + 1 < (int)history.size()) { // Down arrow
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

// ──────────────────────────────────────────────
// Tokenizer
static std::vector<std::string> split(const std::string &s) {
    std::istringstream iss(s);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token)
        tokens.push_back(token);
    return tokens;
}

// ──────────────────────────────────────────────
// Command dispatcher
static void execute_command(const std::vector<std::string> &tokens) {
    if (tokens.empty()) return;

    if (tokens[0] == "cd") {
        if (tokens.size() > 1) {
            try {
                fs::current_path(tokens[1]);
            } catch (...) {
                std::cerr << "cd: cannot access " << tokens[1] << std::endl;
            }
        }
    } else if (tokens[0] == "exit") {
        exit(0);
    } else {
        std::string cmd;
        for (auto &t : tokens) {
            if (!cmd.empty()) cmd += " ";
            cmd += t;
        }
        system(cmd.c_str());
    }
}

// ──────────────────────────────────────────────
// Main
int main() {
    std::vector<std::string> history;
    int historyIndex = 0;

#ifdef _WIN32
    enable_vt_mode(); // enable color
#endif

    std::cout << "Winix Shell v0.5 (Full I/O + Tab)\n";

    while (true) {
        print_prompt();
        std::string input = read_input(history, historyIndex);
        if (input.empty()) continue;
        history.push_back(input);
        historyIndex = history.size();
        execute_command(split(input));
    }

    return 0;
}
