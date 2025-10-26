#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <conio.h>
#include <windows.h>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <regex>

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
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

// ──────────────────────────────────────────────
// Print prompt
static void print_prompt() {
    std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > ";
    std::cout.flush();
}

// ──────────────────────────────────────────────
// Split input into tokens with quote support
static std::vector<std::string> split(const std::string &s) {
    std::vector<std::string> tokens;
    std::string token;
    bool inQuotes = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (isspace(c) && !inQuotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

// ──────────────────────────────────────────────
// Expand environment variables ($VAR or %VAR%)
static std::string expand_vars(const std::string &input) {
    std::string result = input;
    std::regex var_pattern(R"(\$([A-Za-z0-9_]+)|%([A-Za-z0-9_]+)%)");
    std::smatch match;
    std::string::const_iterator searchStart(result.cbegin());
    std::string expanded;

    while (std::regex_search(searchStart, result.cend(), match, var_pattern)) {
        expanded += match.prefix();
        std::string var = match[1].matched ? match[1].str() : match[2].str();
        const char *val = std::getenv(var.c_str());
        expanded += (val ? val : "");
        searchStart = match.suffix().first;
    }
    expanded += std::string(searchStart, result.cend());
    return expanded;
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
// Redraw prompt + current line
static void redraw(const std::string &input, size_t cursor) {
    std::cout << "\r";
    print_prompt();
    std::cout << input << " ";
    std::cout << "\r";
    print_prompt();
    if (cursor > 0) std::cout << input.substr(0, cursor);
    std::cout.flush();
}

// ──────────────────────────────────────────────
// Input with history + arrows + Ctrl keys
static std::string read_input(std::vector<std::string> &history, int &historyIndex) {
    std::string input;
    size_t cursor = 0;
    int ch = 0;

    while (true) {
        ch = _getch();

        if (ch == 13) { // ENTER
            std::cout << "\n";
            break;
        }
        else if (ch == 8) { // BACKSPACE
            if (cursor > 0) {
                input.erase(cursor - 1, 1);
                cursor--;
                redraw(input, cursor);
            }
        }
        else if (ch == 1) { cursor = 0; redraw(input, cursor); }         // Ctrl+A
        else if (ch == 5) { cursor = input.size(); redraw(input, cursor); } // Ctrl+E
        else if (ch == 21) { input.erase(0, cursor); cursor = 0; redraw(input, cursor); } // Ctrl+U
        else if (ch == 11) { input.erase(cursor); redraw(input, cursor); } // Ctrl+K
        else if (ch == 12) { system("cls"); print_prompt(); std::cout << input; } // Ctrl+L

        // TAB completion
        else if (ch == 9) {
            std::string prefix;
            size_t pos = input.find_last_of(' ');
            prefix = (pos == std::string::npos) ? input : input.substr(pos + 1);

            auto matches = complete_in_cwd(prefix);
            if (matches.size() == 1) {
                std::string suffix = matches[0].substr(prefix.size());
                input.insert(cursor, suffix);
                cursor += suffix.size();
                redraw(input, cursor);
            } else if (!matches.empty()) {
                std::cout << "\n";
                for (auto &m : matches) std::cout << "  " << m << "\n";
                redraw(input, cursor);
            }
        }

        // ARROW KEYS
        else if (ch == 224) {
            ch = _getch();
            if (ch == 75 && cursor > 0) { cursor--; redraw(input, cursor); }          // ←
            else if (ch == 77 && cursor < input.size()) { cursor++; redraw(input, cursor); } // →
            else if (ch == 71) { cursor = 0; redraw(input, cursor); }                 // Home
            else if (ch == 79) { cursor = input.size(); redraw(input, cursor); }      // End
            else if (ch == 72) { // ↑
                if (historyIndex > 0) {
                    historyIndex--;
                    input = history[historyIndex];
                    cursor = input.size();
                    redraw(input, cursor);
                }
            } else if (ch == 80) { // ↓
                if (historyIndex + 1 < (int)history.size()) {
                    historyIndex++;
                    input = history[historyIndex];
                } else {
                    historyIndex = history.size();
                    input.clear();
                }
                cursor = input.size();
                redraw(input, cursor);
            }
        }

        // PRINTABLE CHAR
        else if (isprint(ch)) {
            input.insert(cursor, 1, (char)ch);
            cursor++;
            redraw(input, cursor);
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
            try { fs::current_path(tokens[1]); }
            catch (...) { std::cerr << "cd: cannot access " << tokens[1] << std::endl; }
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

    cmd = expand_vars(cmd);
    system(cmd.c_str());
}

// ──────────────────────────────────────────────
// Main
int main() {
    enable_vt_mode();

    std::vector<std::string> history;
    int historyIndex = 0;

    // Load history
    {
        std::ifstream hf("winix_history.txt");
        std::string line;
        while (std::getline(hf, line))
            if (!line.empty()) history.push_back(line);
        if (history.size() > 50)
            history.erase(history.begin(), history.end() - 50);
    }

    std::cout << "Winix Shell v1.6 (quoted args + var expansion)\n";

    std::string path = std::getenv("PATH") ? std::getenv("PATH") : "";
    path += ";build";
    _putenv_s("PATH", path.c_str());

    while (true) {
        print_prompt();
        std::string input = read_input(history, historyIndex);
        if (input.empty()) continue;
        history.push_back(input);
        historyIndex = history.size();

        // Save history to disk
        std::ofstream hf("winix_history.txt", std::ios::trunc);
        int start = std::max(0, (int)history.size() - 50);
        for (size_t i = start; i < history.size(); ++i)
            hf << history[i] << "\n";

        execute_command(split(input));
    }

    return 0;
}
