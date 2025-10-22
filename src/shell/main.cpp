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

// ── helpers ─────────────────────────────────────────────
static void redraw(const std::string& prompt, const std::string& line) {
    std::cout << "\r\033[K" << prompt << line;
}

static std::vector<std::string> split_command(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> parts;
    std::string token;
    while (iss >> token)
        parts.push_back(token);
    return parts;
}

static bool handle_redirection(std::string& line, FILE** redirectFile, bool& appendMode) {
    size_t pos = line.find(">>");
    if (pos != std::string::npos) {
        appendMode = true;
    } else {
        pos = line.find('>');
        if (pos == std::string::npos)
            return false;
    }

    std::string command = line.substr(0, pos);
    std::string file = line.substr(pos + (appendMode ? 2 : 1));
    // trim spaces
    file.erase(0, file.find_first_not_of(" \t"));
    file.erase(file.find_last_not_of(" \t") + 1);

    if (file.empty()) {
        std::cerr << "Syntax error: missing filename after >\n";
        return false;
    }

    *redirectFile = fopen(file.c_str(), appendMode ? "a" : "w");
    if (!*redirectFile) {
        std::cerr << "Error: cannot open file '" << file << "'\n";
        return false;
    }

    line = command;
    return true;
}
// ────────────────────────────────────────────────────────


int main() {
    std::string line;
    std::cout << "Winix Shell v0.4\n";

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
        std::getline(std::cin, line);

        if (line.empty()) continue;

        // save to history
        history.push_back(line);
        historyIndex = -1;

        // handle output redirection
        FILE* redirectFile = nullptr;
        bool appendMode = false;
        bool redirected = handle_redirection(line, &redirectFile, appendMode);

        int old_fd = -1;
        if (redirected) {
            old_fd = _dup(_fileno(stdout));
            _dup2(_fileno(redirectFile), _fileno(stdout));
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "exit" || cmd == "quit") {
            std::cout << "Goodbye.\n";
            if (redirected) {
                fflush(stdout);
                _dup2(old_fd, _fileno(stdout));
                fclose(redirectFile);
                _close(old_fd);
            }
            break;
        }

        if (cmd == "pwd") {
            std::cout << fs::current_path().string() << "\n";
        } else if (cmd == "echo") {
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            std::cout << rest << "\n";
        } else if (cmd == "cd") {
            std::string path;
            if (!(iss >> path)) { std::cout << "Usage: cd <dir>\n"; }
            else {
                try { fs::current_path(path); }
                catch (std::exception &e) { std::cerr << "cd: " << e.what() << "\n"; }
            }
        } else if (cmd == "clear") {
            system("cls");
        } else if (cmd == "help") {
            std::cout << "Built-in commands:\n"
                      << "  cd <dir>\n  pwd\n  echo <text>\n"
                      << "  clear\n  help\n  exit\n";
        } else {
            // external executables
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

        // restore stdout
        if (redirected) {
            fflush(stdout);
            _dup2(old_fd, _fileno(stdout));
            fclose(redirectFile);
            _close(old_fd);
        }
    }

    // save history
    {
        std::ofstream histFile("winix_history.txt", std::ios::trunc);
        for (const auto& c : history) histFile << c << "\n";
    }

    return 0;
}
