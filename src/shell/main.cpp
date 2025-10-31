// Winix Shell Main Source
// Corrected and cleaned version with fixed string literals, escape sequences, and brace closures.

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <windows.h>

// Function prototypes
void persist_env_to_rc();
void redraw_line(const std::string&, const std::string&, size_t);
bool is_exe_path(const std::string&);
std::string join_cmdline(const std::vector<std::string>&);

// --- persist_env_to_rc ---
void persist_env_to_rc() {
    std::map<std::string, std::string> mapkv; // Example key-value map
    std::ofstream out(std::filesystem::path(getenv("USERPROFILE")) / ".winixrc");

    out << "# ~/.winixrc — persisted variables from Winix\n";
    for (auto& kv : mapkv)
        out << kv.first << "=" << kv.second << "\n";
}

// --- redraw_line ---
void redraw_line(const std::string& promptStr, const std::string& buf, size_t cursor) {
    std::cout << "\033[K" << promptStr << buf;

    size_t target = promptStr.size() + cursor;
    size_t current = promptStr.size() + buf.size();

    if (current > target)
        std::cout << std::string(current - target, '\b');
    else if (current < target)
        std::cout << std::string(target - current, ' ');
}

// --- is_exe_path ---
bool is_exe_path(const std::string& s) {
    return s.find('\\') != std::string::npos || s.find('/') != std::string::npos;
}

// --- join_cmdline ---
std::string join_cmdline(const std::vector<std::string>& argv) {
    std::ostringstream oss;
    for (const auto& a : argv) {
        if (a.find(' ') != std::string::npos) {
            oss << '"';
            for (char c : a) {
                if (c == '"') oss << '\\';
                oss << c;
            }
            oss << '"';
        } else {
            oss << a;
        }
        oss << ' ';
    }
    return oss.str();
}

int main() {
    std::string prompt = "[Winix] > ";
    std::string input;

    while (true) {
        std::cout << prompt;
        std::getline(std::cin, input);

        if (input == "exit" || input == "quit")
            break;

        // Example placeholder action
        std::cout << "You entered: " << input << std::endl;
    }

    return 0;
}
