#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    std::string line;
    std::cout << "Winix Shell v0.2\n";

    while (true) {
        std::cout << "winix> ";
        if (!std::getline(std::cin, line)) break;

        // trim
        if (line.empty()) continue;

        // exit
        if (line == "exit" || line == "quit") {
            std::cout << "Goodbye.\n";
            break;
        }

        // help
        if (line == "help") {
            std::cout << "Available commands:\n"
                      << "  pwd, echo, help, exit\n"
                      << "You can also run any system command.\n";
            continue;
        }

        // Build executable path (prefer local ./build)
        //std::string localPath = "build\\usr\\bin\\" + line + ".exe";
        std::string localPath = "build\\" + line + ".exe";
        std::string command;

        if (fs::exists(localPath)) {
            command = localPath;
        } else {
            command = line;  // try system PATH
        }

        int result = std::system(command.c_str());
        if (result == -1) {
            std::cerr << "Command failed: " << line << "\n";
        }
    }

    return 0;
}
