#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include <conio.h>
#include <fstream>
#include <windows.h>   // <-- added for cursor position

namespace fs = std::filesystem;

int main() {
    std::string line;
    std::cout << "Winix Shell v0.3\n";

    std::vector<std::string> searchPaths = {
        ".", "build", "bin"
    };

    std::vector<std::string> history;

    // Load history from file
    {
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

        // save starting cursor position to protect prompt area
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
        SHORT promptStartX = info.dwCursorPosition.X;

        // ── raw input loop ───────────────────────────────
        while ((ch = _getch()) != '\r') {          // Enter
            if (ch == 27) {                        // Escape clears line
                line.clear();
                std::cout << "\r\033[K" << prompt;
                continue;
            }

            if (ch == 8) {                         // Backspace
                GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
                // only allow erase if cursor is beyond prompt
                if (!line.empty() && info.dwCursorPosition.X > promptStartX) {
                    line.pop_back();
                    std::cout << "\b \b";
                }
                continue;
            }

            if (ch == 0 || ch == 224) {            // Arrow keys
                ch = _getch();
                if (ch == 72) {                    // Up
                    if (historyIndex < (int)history.size() - 1) historyIndex++;
                    if (historyIndex >= 0)
                        line = history[history.size() - 1 - historyIndex];
                } else if (ch == 80) {             // Down
                    if (historyIndex > 0) historyIndex--;
                    else if (historyIndex == 0) historyIndex = -1;
                    if (historyIndex >= 0)
                        line = history[history.size() - 1 - historyIndex];
                    else line.clear();
                }

                std::cout << "\r\033[K" << prompt << line;
                continue;
            }

            std::cout << (char)ch;
            line.push_back((char)ch);
        }
        // ────────────────────────────────────────────────

        std::cout << "\n";

        if (!line.empty()) {
            history.push_back(line);
            historyIndex = -1;
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "exit" || cmd == "quit") {
            std::cout << "Goodbye.\n";
            break;
        }
        if (cmd == "pwd") {
            std::cout << fs::current_path().string() << "\n";
            continue;
        }
        if (cmd == "echo") {
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            std::cout << rest << "\n";
            continue;
        }
        if (cmd == "cd") {
            std::string path;
            if (!(iss >> path)) { std::cout << "Usage: cd <dir>\n"; continue; }
            try { fs::current_path(path); }
            catch (std::exception &e) { std::cerr << "cd: " << e.what() << "\n"; }
            continue;
        }
        if (cmd == "clear") {
#ifdef _WIN32
            system("cls");
#else
            system("clear");
#endif
            continue;
        }
        if (cmd == "help") {
            std::cout << "Built-in commands:\n"
                      << "  cd <dir>\n  pwd\n  echo <text>\n  clear\n  help\n  exit\n";
            continue;
        }

        // fallback: external executable
        bool found = false;
        for (const auto &p : searchPaths) {
            std::string full = (fs::path(p) / (cmd + ".exe")).string();
            if (fs::exists(full)) {
                std::string args = line.substr(cmd.size());
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

    // Save history on exit
    {
        std::ofstream histFile("winix_history.txt", std::ios::trunc);
        for (const auto &cmd : history)
            histFile << cmd << "\n";
    }

    return 0;
}
