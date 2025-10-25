#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <conio.h>
#include <windows.h>
#include <algorithm>

namespace fs = std::filesystem;

//───────────────────────────────────────────────
// Enable UTF-8 + ANSI
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

//───────────────────────────────────────────────
static void print_prompt() {
    std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > ";
    std::cout.flush();
}

//───────────────────────────────────────────────
static std::vector<std::string> split_command(const std::string &cmd) {
    std::vector<std::string> tokens;
    std::string token;
    bool in_quotes = false;
    for (char c : cmd) {
        if (c == '"') in_quotes = !in_quotes;
        else if (isspace((unsigned char)c) && !in_quotes) {
            if (!token.empty()) { tokens.push_back(token); token.clear(); }
        } else token.push_back(c);
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

//───────────────────────────────────────────────
static void load_history(std::vector<std::string> &h) {
    std::ifstream f("winix_history.txt");
    std::string line;
    while (std::getline(f, line)) if (!line.empty()) h.push_back(line);
    if (h.size() > 50) h.erase(h.begin(), h.end() - 50);
}
static void save_history(const std::vector<std::string> &h) {
    std::ofstream f("winix_history.txt", std::ios::trunc);
    int start = (h.size() > 50) ? h.size() - 50 : 0;
    for (size_t i = start; i < h.size(); ++i) f << h[i] << "\n";
}

//───────────────────────────────────────────────
// Path-aware tab completion
static std::vector<std::string> complete_in_cwd(const std::string &prefix) {
    std::vector<std::string> matches;
    fs::path base = fs::current_path();
    std::string stem = prefix;
    std::replace(stem.begin(), stem.end(), '\\', '/');
    size_t slash = stem.find_last_of('/');
    if (slash != std::string::npos) {
        base /= stem.substr(0, slash);
        stem = stem.substr(slash + 1);
    }

    if (fs::exists(base) && fs::is_directory(base)) {
        for (auto &entry : fs::directory_iterator(base)) {
            std::string name = entry.path().filename().string();
            if (name.rfind(stem, 0) == 0) {
                if (fs::is_directory(entry)) name += '/';
                matches.push_back(
                    (slash != std::string::npos ? prefix.substr(0, slash + 1) : "") + name);
            }
        }
    }
    return matches;
}

//───────────────────────────────────────────────
// Proper console redraw (no CR/LF)
static void clear_line() {
    std::cout << "\r" << std::string(300, ' ') << "\r";
    print_prompt();
}

static std::string read_input(std::vector<std::string> &history, int &historyIndex) {
    std::string input;
    size_t cursor = 0;

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD originalMode = 0;
    GetConsoleMode(hIn, &originalMode);

    // Raw input mode: turn off line buffering & echo, keep processed input
    DWORD rawMode = originalMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    SetConsoleMode(hIn, rawMode | ENABLE_PROCESSED_INPUT);

    auto redraw = [&](const std::string &in, size_t pos) {
        clear_line();
        std::cout << in;
        std::cout.flush();
        if (pos < in.size()) {
            size_t moveLeft = in.size() - pos;
            for (size_t i = 0; i < moveLeft; ++i) std::cout << "\b";
        }
        std::cout.flush();
    };

    while (true) {
        int ch = _getch();

        // ENTER
        if (ch == 13) {
            std::cout << "\n";
            break;
        }

        // BACKSPACE
        else if (ch == 8 && cursor > 0) {
            input.erase(cursor - 1, 1);
            cursor--;
            redraw(input, cursor);
        }

        // ARROWS
        else if (ch == 224) {
            ch = _getch();
            if (ch == 75 && cursor > 0) { cursor--; redraw(input, cursor); }
            else if (ch == 77 && cursor < input.size()) { cursor++; redraw(input, cursor); }
            else if (ch == 72) { // UP
                if (historyIndex > 0) {
                    historyIndex--;
                    input = history[historyIndex];
                    cursor = input.size();
                    redraw(input, cursor);
                }
            } else if (ch == 80) { // DOWN
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

        // TAB completion
        else if (ch == 9) {
            size_t pos = input.find_last_of(' ');
            std::string prefix = (pos == std::string::npos) ? input : input.substr(pos + 1);
            auto matches = complete_in_cwd(prefix);
            if (matches.size() == 1) {
                std::string suffix = matches[0].substr(prefix.size());
                input.insert(cursor, suffix);
                cursor += suffix.size();
                redraw(input, cursor);
            } else if (!matches.empty()) {
                std::cout << "\n";
                for (auto &m : matches) std::cout << "  " << m << "\n";
                print_prompt();
                std::cout << input;
                std::cout.flush();
            }
        }

        // PRINTABLE
        else if (isprint(ch)) {
            input.insert(cursor, 1, (char)ch);
            cursor++;
            redraw(input, cursor);
        }
    }

    SetConsoleMode(hIn, originalMode); // restore
    return input;
}

//───────────────────────────────────────────────
static void execute_command(const std::vector<std::string> &tokens) {
    if (tokens.empty()) return;
    const std::string &cmd = tokens[0];

    if (cmd == "exit") { std::cout << "Goodbye.\n"; exit(0); }

    if (cmd == "cd") {
        if (tokens.size() > 1) {
            try { fs::current_path(tokens[1]); }
            catch (...) { std::cerr << "cd: cannot access " << tokens[1] << "\n"; }
        }
        return;
    }

    std::string command;
    for (auto &t : tokens) {
        if (!command.empty()) command += " ";
        if (t.find(' ') != std::string::npos) command += '"' + t + '"';
        else command += t;
    }

    system(command.c_str());
}

//───────────────────────────────────────────────
int main() {
    enable_vt_mode();
    std::cout << "Winix Shell v1.5d (true raw mode, no LF, full redraw)\n";

    std::vector<std::string> history;
    load_history(history);
    int historyIndex = history.size();

    while (true) {
        print_prompt();
        std::string input = read_input(history, historyIndex);
        if (input.empty()) continue;
        history.push_back(input);
        if (history.size() > 50) history.erase(history.begin());
        historyIndex = history.size();
        execute_command(split_command(input));
        save_history(history);
    }
}
