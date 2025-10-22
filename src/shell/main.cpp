#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include <conio.h>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    std::string line;
    std::cout << "Winix Shell v0.3\n";

    std::vector<std::string> searchPaths = {
        ".",        // current directory
        "build",    // build output folder
        "bin"       // future installed binaries
    };

    std::vector<std::string> history;
    // Load history from file
{
    std::ifstream histFile("winix_history.txt");
    std::string histLine;
    while (std::getline(histFile, histLine)) {
        if (!histLine.empty())
            history.push_back(histLine);
    }
    histFile.close();
}

    int historyIndex = -1;
    
    while (true) {
        std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > ";
line.clear();
int ch;

while ((ch = _getch()) != '\r') { // Enter key ends input
    if (ch == 27) { // Escape key clears line
        line.clear();
        std::cout << "\r\033[K"; // clear line
        std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > ";
        continue;
    }
    if (ch == 8 && !line.empty()) { // Backspace
        line.pop_back();
        std::cout << "\b \b";
        continue;
    }
if (ch == 0 || ch == 224) { // Arrow keys
    ch = _getch();

    if (ch == 72) { // Up arrow
        if (historyIndex < (int)history.size() - 1)
            historyIndex++;
        if (historyIndex >= 0)
            line = history[history.size() - 1 - historyIndex];
    }
    else if (ch == 80) { // Down arrow
        if (historyIndex > 0)
            historyIndex--;
        else if (historyIndex == 0)
            historyIndex = -1;

        if (historyIndex >= 0)
            line = history[history.size() - 1 - historyIndex];
        else
            line.clear();
    }

    std::cout << "\r\033[K\033[1;32m[Winix]\033[0m "
              << fs::current_path().string() << " > " << line;
    continue;
}


        std::cout << "\n";
        // Save command to in-memory history immediately after input
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

        // ---- cd ----
        if (cmd == "cd") {
            std::string path;
            if (!(iss >> path)) {
                std::cout << "Usage: cd <directory>\n";
            continue;
        }
        try {
            std::filesystem::current_path(path);
        } catch (std::exception &e) {
            std::cerr << "cd: " << e.what() << "\n";
        }
        continue;
        }

        // ---- clear ----
        if (cmd == "clear") {
        #ifdef _WIN32
            system("cls");
        #else
            system("clear");
        #endif
            continue;
        }

        // ---- help ----
        if (cmd == "help") {
            std::cout << "Built-in commands:\n"
                      << "  cd <dir>     - change directory\n"
                      << "  pwd          - print current directory\n"
                      << "  echo <text>  - print text\n"
                      << "  clear        - clear the screen\n"
                      << "  help         - show this help message\n"
                      << "  exit         - exit Winix shell\n";
            continue;
        }

        
        // fallback: try to execute as system command
        bool found = false;
        for (const auto &p : searchPaths) {
        std::string full = (fs::path(p) / (cmd + ".exe")).string();
        if (fs::exists(full)) {
            // Capture the rest of the line after the command name
            std::string args = line.substr(cmd.size());
            if (!args.empty() && args[0] == ' ')
                args.erase(0, 1);

            // Build full command line with args
            std::string fullCmd = full + " " + args;

            int result = std::system(fullCmd.c_str());
            if (result == -1)
                std::cerr << "Error executing: " << full << "\n";

            found = true;
            break;
            }
        }

if (!found) {
    std::cerr << cmd << ": command not found\n";
}
    // Save non-empty command to history
if (!line.empty()) {
    history.push_back(line);
    historyIndex = -1;
}

    }
// Save history to file
{
    std::ofstream histFile("winix_history.txt", std::ios::trunc);
    for (const auto& cmd : history)
        histFile << cmd << "\n";
    histFile.close();
}

    return 0;
}
