#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <windows.h>
#include <cstdlib>
#include <algorithm>

namespace fs = std::filesystem;

// ──────────────────────────────────────────────
// Enable ANSI + UTF-8 mode
static void enable_vt_mode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

// ──────────────────────────────────────────────
// Print prompt
static void print_prompt() {
    std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > " << std::flush;
}

// ──────────────────────────────────────────────
// Tokenize input
static std::vector<std::string> split(const std::string &s) {
    std::istringstream iss(s);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

// ──────────────────────────────────────────────
// Tab completion
static std::vector<std::string> complete_in_cwd(const std::string &prefix) {
    std::vector<std::string> matches;
    for (auto &entry : fs::directory_iterator(fs::current_path())) {
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0)
            matches.push_back(name);
    }
    return matches;
}

// ──────────────────────────────────────────────
// Redraw helper — clears entire line before rewrite
static void redraw_line(const std::string &input) {
    std::cout << "\r";                          // Return to line start
    print_prompt();                            // Print prompt again
    std::cout << std::string(200, ' ') << "\r"; // Overwrite previous text
    print_prompt();                            // Print prompt again cleanly
    std::cout << input << std::flush;
}

// ──────────────────────────────────────────────
// Input handler — raw input events, full redraw, no pagination
static std::string read_input(std::vector<std::string> &history, int &historyIndex) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldMode;
    GetConsoleMode(hIn, &oldMode);
    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT);

    std::string input;
    INPUT_RECORD record;
    DWORD count;

    while (true) {
        ReadConsoleInput(hIn, &record, 1, &count);
        if (record.EventType != KEY_EVENT) continue;
        const KEY_EVENT_RECORD &key = record.Event.KeyEvent;
        if (!key.bKeyDown) continue;

        switch (key.wVirtualKeyCode) {
        case VK_RETURN:
            std::cout << "\n";
            SetConsoleMode(hIn, oldMode);
            return input;

        case VK_BACK:
            if (!input.empty()) {
                input.pop_back();
                std::cout << "\b \b" << std::flush;
            }
            break;

        case VK_TAB: {
            std::string prefix;
            size_t pos = input.find_last_of(' ');
            prefix = (pos == std::string::npos) ? input : input.substr(pos + 1);
            auto matches = complete_in_cwd(prefix);
            if (matches.size() == 1) {
                std::string suffix = matches[0].substr(prefix.size());
                input += suffix;
                std::cout << suffix << std::flush;
            } else if (!matches.empty()) {
                std::cout << "\n";
                for (auto &m : matches)
                    std::cout << "  " << m << "\n";
                redraw_line(input);
            }
            break;
        }

        case VK_UP:
            if (historyIndex > 0) {
                historyIndex--;
                input = history[historyIndex];
                redraw_line(input); // full overwrite fix
            }
            break;

        case VK_DOWN:
            if (historyIndex + 1 < (int)history.size()) {
                historyIndex++;
                input = history[historyIndex];
            } else {
                historyIndex = history.size();
                input.clear();
            }
            redraw_line(input); // full overwrite fix
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
// Execute commands
static void execute_command(const std::vector<std::string> &tokens) {
    if (tokens.empty()) return;
    if (tokens[0] == "cd") {
        if (tokens.size() > 1) {
            try { fs::current_path(tokens[1]); }
            catch (...) { std::cerr << "cd: cannot access " << tokens[1] << "\n"; }
        }
        return;
    }
    if (tokens[0] == "exit") {
        std::cout << "Goodbye.\n";
        exit(0);
    }

    std::string cmd;
    for (auto &t : tokens) {
        if (!cmd.empty()) cmd += " ";
        cmd += t;
    }
    system(cmd.c_str());
}

// ──────────────────────────────────────────────
// Main
int main() {
    enable_vt_mode();
    std::vector<std::string> history;
    int historyIndex = 0;

    std::cout << "Winix Shell v0.9 (Raw Input + Full Redraw)\n";

    std::string path = std::getenv("PATH") ? std::getenv("PATH") : "";
    path += ";build";
    _putenv_s("PATH", path.c_str());

    while (true) {
        print_prompt();
        std::string input = read_input(history, historyIndex);
        if (input.empty()) continue;
        history.push_back(input);
        historyIndex = history.size();
        execute_command(split(input));
    }
}
