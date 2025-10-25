#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <conio.h>
#include <windows.h>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

//───────────────────────────────────────────────
// Console + UTF-8 support
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

//───────────────────────────────────────────────
// Prompt
static void print_prompt() {
    std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > ";
}

//───────────────────────────────────────────────
// Tokenizer respecting quotes
static std::vector<std::string> split_command(const std::string &cmd) {
    std::vector<std::string> tokens;
    std::string token;
    bool in_quotes = false;
    for (size_t i = 0; i < cmd.size(); ++i) {
        char c = cmd[i];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (isspace((unsigned char)c) && !in_quotes) {
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

//───────────────────────────────────────────────
// History load/save
static void load_history(std::vector<std::string> &history) {
    std::ifstream file("winix_history.txt");
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) history.push_back(line);
    }
    if (history.size() > 50) history.erase(history.begin(), history.end() - 50);
}

static void save_history(const std::vector<std::string> &history) {
    std::ofstream file("winix_history.txt", std::ios::trunc);
    int start = (history.size() > 50) ? history.size() - 50 : 0;
    for (size_t i = start; i < history.size(); ++i)
        file << history[i] << "\n";
}

//───────────────────────────────────────────────
// Tab completion (current dir)
static std::vector<std::string> complete_in_cwd(const std::string &prefix) {
    std::vector<std::string> matches;
    for (auto &entry : fs::directory_iterator(fs::current_path())) {
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0)
            matches.push_back(name);
    }
    return matches;
}

//───────────────────────────────────────────────
// Read input with history + tab + arrows
static std::string read_input(std::vector<std::string> &history, int &historyIndex) {
    std::string input;
    int ch = 0;

    while (true) {
        ch = _getch();

        // ENTER
        if (ch == 13) {
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
            size_t pos = input.find_last_of(' ');
            std::string prefix = (pos == std::string::npos) ? input : input.substr(pos + 1);
            auto matches = complete_in_cwd(prefix);

            if (matches.size() == 1) {
                std::string suffix = matches[0].substr(prefix.size());
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

            auto clear_line = [&]() {
                std::cout << "\r";
                print_prompt();
                std::cout << std::string(200, ' ') << "\r";
                print_prompt();
            };

            if (ch == 72) { // UP
                if (historyIndex > 0) {
                    historyIndex--;
                    input = history[historyIndex];
                    clear_line();
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
                clear_line();
                std::cout << input;
            }
            std::cout.flush();
        }

        // PRINTABLE CHARACTER
        else if (isprint(ch)) {
            input.push_back((char)ch);
            std::cout << (char)ch;
        }
    }

    return input;
}

//───────────────────────────────────────────────
// Execute command via PATH
static void execute_command(const std::vector<std::string> &tokens) {
    if (tokens.empty()) return;

    const std::string &cmd = tokens[0];
    if (cmd == "exit") {
        std::cout << "Goodbye.\n";
        save_history(tokens);
        exit(0);
    }

    if (cmd == "cd") {
        if (tokens.size() > 1) {
            try {
                fs::current_path(tokens[1]);
            } catch (...) {
                std::cerr << "cd: cannot access " << tokens[1] << "\n";
            }
        }
        return;
    }

    // Build command string
    std::string command;
    for (auto &t : tokens) {
        if (!command.empty()) command += " ";
        if (t.find(' ') != std::string::npos)
            command += '"' + t + '"';
        else
            command += t;
    }

    // Resolve using PATH like POSIX
    char *env_path = getenv("PATH");
    std::vector<std::string> search_paths;
    if (env_path) {
        std::istringstream iss(env_path);
        std::string segment;
        while (std::getline(iss, segment, ';')) search_paths.push_back(segment);
    }

    bool found = false;
    for (const auto &dir : search_paths) {
        fs::path full = fs::path(dir) / (cmd + ".exe");
        if (fs::exists(full)) {
            found = true;
            command.replace(0, cmd.size(), full.string());
            break;
        }
    }

    if (!found && !fs::exists(cmd) && !fs::exists(cmd + ".exe")) {
        std::cerr << cmd << ": command not found\n";
        return;
    }

    system(command.c_str());
}

//───────────────────────────────────────────────
// Main
int main() {
    enable_vt_mode();
    std::vector<std::string> history;
    load_history(history);
    int historyIndex = history.size();

    std::cout << "Winix Shell v1.4 (spawn fix, history cap, completion)\n";

    while (true) {
        print_prompt();
        std::string input = read_input(history, historyIndex);
        if (input.empty()) continue;

        auto tokens = split_command(input);
        history.push_back(input);
        if (history.size() > 50) history.erase(history.begin());
        historyIndex = history.size();
        execute_command(tokens);
        save_history(history);
    }
}
