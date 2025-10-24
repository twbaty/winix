#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <conio.h>
#include <windows.h>
#include <cstdlib>
#include <algorithm>

namespace fs = std::filesystem;

// ──────────────────────────────────────────────
// Enable ANSI + UTF-8 mode on Windows
static void enable_vt_mode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
    // Set codepage to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

// ──────────────────────────────────────────────
// Print prompt
static void print_prompt() {
    std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > ";
}

// ──────────────────────────────────────────────
// Tokenize
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
        if (name.rfind(prefix, 0) == 0) matches.push_back(name);
    }
    return matches;
}

// ──────────────────────────────────────────────
// Input with history + arrows
static std::string read_input(std::vector<std::string> &history, int &historyIndex) {
    std::string input;
    int ch;

    while (true) {
        ch = _getch();

        // ENTER
        if (ch == '\r') {
            std::cout << "\n";
            break;
        }

        // BACKSPACE
        else if (ch == 8) {
            if (!input.empty()) {
                input.pop_back();
                std::cout << "\b \b";
            }
        }

        // TAB completion
        else if (ch == 9) {
            auto matches = complete_in_cwd(input);
            if (matches.size() == 1) {
                std::string suffix = matches[0].substr(input.size());
                std::cout << suffix;
                input += suffix;
            } else if (!matches.empty()) {
                std::cout << "\n";
                for (auto &m : matches) std::cout << "  " << m << "\n";
                print_prompt();
                std::cout << input;
            }
        }

        // ARROW KEYS
        else if (ch == 224) {
            ch = _getch();
            if (ch == 72) { // UP
                if (historyIndex > 0) {
                    historyIndex--;
                    input = history[historyIndex];
                    std::cout << "\r";
                    print_prompt();
                    std::cout << std::string(200, ' ') << "\r";
                    print_prompt();
                    std::cout << input;
                }
            } else if (ch == 80) { // DOWN
                if (historyIndex + 1 < (int)history.size()) {
                    historyIndex++;
                    input = history[historyIndex];
                } else {
                    historyIndex = history.size();
                    input.clear();
                }
                std::cout << "\r";
                print_prompt();
                std::cout << std::string(200, ' ') << "\r";
                print_prompt();
                std::cout << input;
            }
        }

        // PRINTABLE CHARACTER
        else if (isprint(ch)) {
            input.push_back((char)ch);
            std::cout << (char)ch;
        }
    }

    return input;
}


// ──────────────────────────────────────────────
// Execute commands
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

    std::cout << "Winix Shell v0.5 (Full I/O + Tab)\n";

    // Add build dir to PATH
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

    return 0;
}
