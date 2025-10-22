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

// --- helpers ---
static void redraw(const std::string& prompt, const std::string& line) {
    std::cout << "\r\033[K" << prompt << line;
}

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include <conio.h>
#include <fstream>
#include <windows.h>

namespace fs = std::filesystem;

// --- helpers ---
static void redraw(const std::string& prompt, const std::string& line) {
    std::cout << "\r\033[K" << prompt << line;
}

static std::vector<std::string> complete_in_cwd(const std::string& prefix) {
    std::vector<std::string> matches;
    for (const auto& entry : fs::directory_iterator(fs::current_path())) {
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) matches.push_back(name);
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

int main() {
    std::string line;
    std::cout << "Winix Shell v0.3\n";

    std::vector<std::string> searchPaths = { ".", "build", "bin" };

    std::vector<std::string> history;
    {   // load history
        std::ifstream histFile("winix_history.txt");
        std::string histLine;
        while (std::getline(histFile, histLine))
            if (!histLine.empty()) history.push_back(histLine);
    }

    int historyIndex = -1;

    while (true) {
        std::string prompt = "\033[1;32m[Winix]\033[0m " + fs::current_path().string() + " > ";
        std::cout << prompt;
        line.clear();
        int ch;

        // cursor floor to protect prompt
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
        SHORT promptStartX = info.dwCursorPosition.X;


int main() {
    std::string line;
    std::cout << "Winix Shell v0.3\n";

    std::vector<std::string> searchPaths = { ".", "build", "bin" };

    std::vector<std::string> history;
    {   // load history
        std::ifstream histFile("winix_history.txt");
        std::string histLine;
        while (std::getline(histFile, histLine))
            if (!histLine.empty()) history.push_back(histLine);
    }

    int historyIndex = -1;

    while (true) {
        std::string prompt = "\033[1;32m[Winix]\033[0m " + fs::current_path().string() + " > ";
        std::cout << prompt;
        line.clear();
        int ch;

        // cursor floor to protect prompt
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
        SHORT promptStartX = info.dwCursorPosition.X;

        // tab state
        bool tabOnce = false;
        std::string lastPrefix;

        // -------- raw input loop ----------
        while ((ch = _getch()) != '\r') {
            if (ch == 27) { // ESC clear
                line.clear();
                redraw(prompt, line);
                tabOnce = false;
                continue;
            }

            if (ch == 8) { // Backspace (don’t chew prompt)
                GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
                if (!line.empty() && info.dwCursorPosition.X > promptStartX) {
                    line.pop_back();
                    std::cout << "\b \b";
                } else {
                    redraw(prompt, line);
                }
                tabOnce = false;
                continue;
            }

            // Arrow / function keys
            if (ch == 0 || ch == 224) {
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

            // --- TAB completion (single = complete, double = list) ---
            if (ch == '\t') {
                size_t cut = line.find_last_of(' ');
                std::string prefix = (cut == std::string::npos) ? line : line.substr(cut + 1);

                auto matches = complete_in_cwd(prefix);

                if (matches.empty()) {
                    tabOnce = false; // nothing to do
                    continue;
                }

                if (matches.size() == 1) {
                    // replace token with full match
                    line = (cut == std::string::npos)
                         ? matches[0]
                         : line.substr(0, cut + 1) + matches[0];
                    redraw(prompt, line);
                    tabOnce = false;
                    continue;
                }

                // multiple matches
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

            // printable char
            std::cout << (char)ch;
            line.push_back((char)ch);
            tabOnce = false;
        }
        // ----------------------------------

        std::cout << "\n";

        if (!line.empty()) { history.push_back(line); historyIndex = -1; }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "exit" || cmd == "quit") {
            std::cout << "Goodbye.\n";
            break;
        }
        if (cmd == "pwd") { std::cout << fs::current_path().string() << "\n"; continue; }
        if (cmd == "echo") {
            std::string rest; std::getline(iss, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0,1);
            std::cout << rest << "\n"; continue;
        }
        if (cmd == "cd") {
            std::string path;
            if (!(iss >> path)) { std::cout << "Usage: cd <dir>\n"; continue; }
            try { fs::current_path(path); }
            catch (std::exception& e) { std::cerr << "cd: " << e.what() << "\n"; }
            continue;
        }
#ifdef _WIN32
        if (cmd == "clear") { system("cls"); continue; }
#else
        if (cmd == "clear") { system("clear"); continue; }
#endif
        if (cmd == "help") {
            std::cout << "Built-in commands:\n"
                      << "  cd <dir>\n  pwd\n  echo <text>\n  clear\n  help\n  exit\n";
            continue;
        }

        // external executables (searchPaths)
        bool found = false;
        for (const auto& p : searchPaths) {
            std::string full = (fs::path(p) / (cmd + ".exe")).string();
            if (fs::exists(full)) {
                std::string args = line.substr(cmd.size());
                if (!args.empty() && args[0] == ' ') args.erase(0,1);
                std::string fullCmd = full + " " + args;
                if (std::system(fullCmd.c_str()) == -1)
                    std::cerr << "Error executing: " << full << "\n";
                found = true;
                break;
            }
        }
        if (!found && !cmd.empty())
            std::cerr << cmd << ": command not found\n";
    }

    // save history
    {
        std::ofstream histFile("winix_history.txt", std::ios::trunc);
        for (const auto& c : history) histFile << c << "\n";
    }

    return 0;
}
