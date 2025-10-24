#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include <conio.h>
#include <fstream>
#include <windows.h>
#include <io.h>
#include <algorithm>

namespace fs = std::filesystem;

//─────────────────────────────────────────────
// Helper: redraw prompt line
static void redraw(const std::string& prompt, const std::string& line) {
    std::cout << "\r\033[K" << prompt << line << std::flush;
}

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    std::cout << "Winix Shell v0.5 (Full I/O + Tab)\n";

#ifdef _WIN32
    // Enable ANSI color output globally
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif

//─────────────────────────────────────────────
// Helper: case-insensitive prefix match for completion
static std::vector<std::string> complete_in_cwd(const std::string& prefix) {
    std::vector<std::string> matches;
    std::string lowerPrefix = prefix;
    std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), ::tolower);

    for (const auto& entry : fs::directory_iterator(fs::current_path())) {
        std::string name = entry.path().filename().string();
        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        if (lowerName.rfind(lowerPrefix, 0) == 0)
            matches.push_back(name);
    }

    std::sort(matches.begin(), matches.end());
    return matches;
}

//─────────────────────────────────────────────
// Unified command reader (interactive or redirected)
static std::string read_command(const std::string& prompt,
                                std::vector<std::string>& history,
                                int& historyIndex) {
    std::string line;
    bool interactive = _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));

    if (!interactive) {
        std::getline(std::cin, line);
        return line;
    }

    std::cout << prompt << std::flush;
    int ch;
    bool tabOnce = false;
    std::string lastPrefix;

    while ((ch = _getch()) != '\r') { // Enter key
        if (ch == 27) { // ESC clears line
            line.clear();
            redraw(prompt, line);
            tabOnce = false;
            continue;
        }
        if (ch == 8) { // Backspace
            if (!line.empty()) {
                line.pop_back();
                std::cout << "\b \b" << std::flush;
            }
            tabOnce = false;
            continue;
        }
        if (ch == 0 || ch == 224) { // Arrows
            ch = _getch();
            if (ch == 72) { // Up
                if (historyIndex < (int)history.size() - 1) historyIndex++;
                if (historyIndex >= 0)
                    line = history[(int)history.size() - 1 - historyIndex];
            } else if (ch == 80) { // Down
                if (historyIndex > 0) historyIndex--;
                else if (historyIndex == 0) historyIndex = -1;
                if (historyIndex >= 0)
                    line = history[(int)history.size() - 1 - historyIndex];
                else
                    line.clear();
            }
            redraw(prompt, line);
            tabOnce = false;
            continue;
        }
        if (ch == '\t') { // Tab completion
            size_t cut = line.find_last_of(' ');
            std::string prefix = (cut == std::string::npos) ? line : line.substr(cut + 1);

            auto matches = complete_in_cwd(prefix);

            if (matches.empty()) {
                tabOnce = false;
                continue;
            }
            if (matches.size() == 1) {
                line = (cut == std::string::npos)
                         ? matches[0]
                         : line.substr(0, cut + 1) + matches[0];
                redraw(prompt, line);
                tabOnce = false;
                continue;
            }
            if (tabOnce && lastPrefix == prefix) {
                std::cout << "\n";
                int col = 0;
                for (const auto& m : matches) {
                    std::cout << m << "\t";
                    if (++col % 4 == 0) std::cout << "\n";
                }
                std::cout << "\n";
                redraw(prompt, line);
                tabOnce = false;
            } else {
                tabOnce = true;
                lastPrefix = prefix;
            }
            continue;
        }

        // Printable character
        line.push_back((char)ch);
        std::cout << (char)ch << std::flush;
        tabOnce = false;
    }

    std::cout << "\n";
    return line;
}

//─────────────────────────────────────────────
// MAIN
int main() {
    std::cout << "Winix Shell v0.5 (Full I/O + Tab)\n";

    #ifdef _WIN32
    // Enable ANSI color output globally in this shell session
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif

    std::vector<std::string> searchPaths = { ".", "build", "bin" };
    std::vector<std::string> history;
    {
        std::ifstream histFile("winix_history.txt");
        std::string histLine;
        while (std::getline(histFile, histLine))
            if (!histLine.empty()) history.push_back(histLine);
    }

    int historyIndex = -1;

    while (true) {
        std::string prompt = "\033[1;32m[Winix]\033[0m " + fs::current_path().string() + " > ";
        std::string line = read_command(prompt, history, historyIndex);
        if (line.empty()) continue;

        history.push_back(line);
        historyIndex = -1;

        // Handle redirection manually
        std::string command = line;
        std::string redirectFile;
        bool appendMode = false;
        size_t pos = line.find(">>");
        if (pos != std::string::npos) {
            appendMode = true;
        } else {
            pos = line.find('>');
        }
        if (pos != std::string::npos) {
            redirectFile = line.substr(pos + (appendMode ? 2 : 1));
            command = line.substr(0, pos);
            redirectFile.erase(0, redirectFile.find_first_not_of(" \t"));
            redirectFile.erase(redirectFile.find_last_not_of(" \t") + 1);
        }

        FILE* redir = nullptr;
        int old_fd = -1;
        if (!redirectFile.empty()) {
            redir = fopen(redirectFile.c_str(), appendMode ? "a" : "w");
            if (!redir) {
                std::cerr << "Cannot open redirect file: " << redirectFile << "\n";
                continue;
            }
            old_fd = _dup(_fileno(stdout));
            _dup2(_fileno(redir), _fileno(stdout));
        }

        std::istringstream iss(command);
        std::string cmd;
        iss >> cmd;

        if (cmd == "exit" || cmd == "quit") {
            std::cout << "Goodbye.\n";
            break;
        } else if (cmd == "pwd") {
            std::cout << fs::current_path().string() << "\n";
        } else if (cmd == "cd") {
            std::string path;
            if (!(iss >> path)) {
                std::cout << "Usage: cd <dir>\n";
            } else {
                try { fs::current_path(path); }
                catch (std::exception& e) { std::cerr << "cd: " << e.what() << "\n"; }
            }
        } else if (cmd == "echo") {
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            std::cout << rest << "\n";
        } else if (cmd == "clear") {
            system("cls");
        } else if (cmd == "help") {
            std::cout << "Built-in commands:\n"
                      << "  cd <dir>\n  pwd\n  echo <text>\n  clear\n  help\n  exit\n";
        } else {
            // External executables
            bool found = false;
            for (const auto& p : searchPaths) {
                std::string full = (fs::path(p) / (cmd + ".exe")).string();
                if (fs::exists(full)) {
                    std::string args = command.substr(cmd.size());
                    if (!args.empty() && args[0] == ' ') args.erase(0, 1);
                    std::string fullCmd = full + " " + args;
                    if (std::system(fullCmd.c_str()) == -1)
                        std::cerr << "Error executing: " << full << "\n";
                    found = true;
                    break;
                }
            }
            if (!found)
                std::cerr << cmd << ": command not found\n";
        }

        if (redir) {
            fflush(stdout);
            _dup2(old_fd, _fileno(stdout));
            fclose(redir);
            _close(old_fd);
        }
    }

    // Save history
    {
        std::ofstream histFile("winix_history.txt", std::ios::trunc);
        for (const auto& h : history)
            histFile << h << "\n";
    }

    return 0;
}
