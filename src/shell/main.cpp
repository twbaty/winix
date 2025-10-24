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
// Enable UTF-8 and ANSI color output
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
// Tokenize (quote-aware)
static std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> tokens;
    std::string token;
    bool inQuote = false;
    char quoteChar = 0;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if ((c == '"' || c == '\'') && !inQuote) {
            inQuote = true;
            quoteChar = c;
        } else if (inQuote && c == quoteChar) {
            inQuote = false;
        } else if (!inQuote && isspace((unsigned char)c)) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

// ──────────────────────────────────────────────
// Wildcard expansion (*, ?)
static std::vector<std::string> expand_wildcards(const std::vector<std::string>& tokens) {
    std::vector<std::string> expanded;
    for (const auto& t : tokens) {
        if (t.find('*') == std::string::npos && t.find('?') == std::string::npos) {
            expanded.push_back(t);
            continue;
        }
        std::regex pattern(std::regex_replace(std::regex_replace(t, std::regex("\\*"), ".*"), std::regex("\\?"), "."));
        bool matched = false;
        for (auto& e : fs::directory_iterator(fs::current_path())) {
            std::string name = e.path().filename().string();
            if (std::regex_match(name, pattern)) {
                expanded.push_back(name);
                matched = true;
            }
        }
        if (!matched) expanded.push_back(t);
    }
    return expanded;
}

// ──────────────────────────────────────────────
// Tab completion
static std::vector<std::string> complete_in_cwd(const std::string& prefix) {
    std::vector<std::string> matches;
    for (auto& entry : fs::directory_iterator(fs::current_path())) {
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0)
            matches.push_back(name);
    }
    return matches;
}

// ──────────────────────────────────────────────
// Redraw current line cleanly
static void redraw_line(const std::string& input) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);

    COORD start = {0, csbi.dwCursorPosition.Y};
    SetConsoleCursorPosition(hOut, start);

    print_prompt();
    std::cout << input << std::flush;

    GetConsoleScreenBufferInfo(hOut, &csbi);
    DWORD written, clearLen = csbi.dwSize.X - csbi.dwCursorPosition.X;
    FillConsoleOutputCharacter(hOut, ' ', clearLen, csbi.dwCursorPosition, &written);
    SetConsoleCursorPosition(hOut, csbi.dwCursorPosition);
}

// ──────────────────────────────────────────────
// Load and save history
static void load_history(std::vector<std::string>& history) {
    std::ifstream file(HISTORY_FILE);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) history.push_back(line);
    }
    if (history.size() > HISTORY_LIMIT)
        history.erase(history.begin(), history.end() - HISTORY_LIMIT);
}

static void save_history(const std::vector<std::string>& history) {
    std::ofstream file(HISTORY_FILE, std::ios::trunc);
    size_t start = (history.size() > HISTORY_LIMIT) ? history.size() - HISTORY_LIMIT : 0;
    for (size_t i = start; i < history.size(); ++i)
        file << history[i] << "\n";
}

// ──────────────────────────────────────────────
// Input with history, arrows, tab
static std::string read_input(std::vector<std::string>& history, int& historyIndex) {
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
        const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
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
                for (auto& m : matches) std::cout << "  " << m << "\n";
                redraw_line(input);
            }
            break;
        }

        case VK_UP:
            if (historyIndex > 0) {
                historyIndex--;
                input = history[historyIndex];
                redraw_line(input);
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
            redraw_line(input);
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
// Execute command (direct spawn)
static void execute_command(const std::vector<std::string>& tokens) {
    if (tokens.empty()) return;

    // Built-ins
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

    // Convert to argv format for _spawnvp
    std::vector<char*> argv;
    for (auto& t : tokens)
        argv.push_back(const_cast<char*>(t.c_str()));
    argv.push_back(nullptr);

    int result = _spawnvp(_P_WAIT, argv[0], argv.data());
    if (result == -1)
        std::cerr << tokens[0] << ": command not found or failed\n";
}

// ──────────────────────────────────────────────
// Main
int main() {
    enable_vt_mode();
    std::cout << "Winix Shell v1.3 (spawn, history, UTF-8, completion)\n";

    std::vector<std::string> history;
    load_history(history);
    int historyIndex = history.size();

    std::string path = std::getenv("PATH") ? std::getenv("PATH") : "";
    path += ";build";
    _putenv_s("PATH", path.c_str());

    while (true) {
        print_prompt();
        std::string input = read_input(history, historyIndex);
        if (input.empty()) continue;

        history.push_back(input);
        historyIndex = history.size();
        save_history(history);

        auto tokens = split(input);
        tokens = expand_wildcards(tokens);
        execute_command(tokens);
    }
}
