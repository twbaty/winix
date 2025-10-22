#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    std::string line;
    std::cout << "Winix Shell v0.3\n";

    while (true) {
        std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > ";
        if (!std::getline(std::cin, line) || line.empty())
            continue;

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
        int result = std::system(line.c_str());
        if (result == -1)
            std::cerr << "Command failed: " << cmd << "\n";
    }
    return 0;
}
