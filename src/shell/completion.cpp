#include "completion.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

static const std::vector<std::string>& builtin_cmds() {
    static const std::vector<std::string> v = {
        "cd", "set", "alias", "unalias", "history",
        "exit", "quit"
    };
    return v;
}

static bool starts_with_ci(const std::string& s, const std::string& prefix) {
    if (prefix.size() > s.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i]))
            return false;
    }
    return true;
}

// Returns executable names from PATH directories that match the prefix.
static std::vector<std::string> path_command_matches(const std::string& prefix) {
    std::vector<std::string> out;
    const char *path_env = std::getenv("PATH");
    if (!path_env) return out;

    std::string path_str(path_env);
    std::string dir;
    for (size_t i = 0; i <= path_str.size(); ++i) {
        char c = (i < path_str.size()) ? path_str[i] : '\0';
        if (c == ';' || c == '\0') {
            if (!dir.empty()) {
                std::error_code ec;
                for (auto& entry : fs::directory_iterator(dir, ec)) {
                    if (ec) break;
                    auto p = entry.path();
                    auto ext = p.extension().string();
                    // Only .exe files
                    if (ext.size() != 4) { dir.clear(); continue; }
                    for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                    if (ext != ".exe") continue;
                    std::string stem = p.stem().string();
                    if (prefix.empty() || starts_with_ci(stem, prefix))
                        out.push_back(stem);
                }
                dir.clear();
            }
        } else {
            dir += c;
        }
    }
    return out;
}

// Returns filesystem entries that match the partial path prefix.
static std::vector<std::string> filesystem_matches(const std::string& partial) {
    std::string dir_str;
    std::string file_prefix;

    auto sep = partial.find_last_of("/\\");
    if (sep == std::string::npos) {
        dir_str     = "";          // search current directory
        file_prefix = partial;
    } else {
        dir_str     = partial.substr(0, sep + 1);   // include trailing separator
        file_prefix = partial.substr(sep + 1);
    }

    fs::path dir = dir_str.empty() ? fs::path(".") : fs::path(dir_str);

    std::vector<std::string> out;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        std::string name = entry.path().filename().string();
        if (file_prefix.empty() || starts_with_ci(name, file_prefix)) {
            std::string completion = dir_str + name;
            if (entry.is_directory(ec))
                completion += "\\";
            out.push_back(completion);
        }
    }
    return out;
}

std::vector<std::string> completion_matches(const std::string& partial,
                                            const Aliases& aliases)
{
    std::vector<std::string> out;

    // Builtins and aliases (useful as command completions for the first word).
    for (auto& b : builtin_cmds()) {
        if (partial.empty() || starts_with_ci(b, partial))
            out.push_back(b);
    }
    for (auto& name : aliases.names()) {
        if (partial.empty() || starts_with_ci(name, partial))
            out.push_back(name);
    }

    // PATH command matches (bare word with no path separator = command name).
    bool has_sep = partial.find_first_of("/\\") != std::string::npos;
    if (!has_sep) {
        for (auto& cmd : path_command_matches(partial))
            out.push_back(cmd);
    }

    // Filesystem matches (useful for both command paths and arguments).
    // Only attempted when there is something to match against.
    if (!partial.empty()) {
        for (auto& f : filesystem_matches(partial))
            out.push_back(f);
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
