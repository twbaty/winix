#include "completion.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <windows.h>

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

// Scan one directory for .exe stems matching prefix; appends to out.
static void scan_dir_for_exes(const std::string& dir,
                               const std::string& prefix,
                               std::vector<std::string>& out) {
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        auto p   = entry.path();
        auto ext = p.extension().string();
        if (ext.size() != 4) continue;
        for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
        if (ext != ".exe") continue;
        std::string stem = p.stem().string();
        if (prefix.empty() || starts_with_ci(stem, prefix))
            out.push_back(stem);
    }
}

// Returns executable names from PATH directories that match the prefix,
// plus executables co-located with winix.exe (handles dev/build workflows).
static std::vector<std::string> path_command_matches(const std::string& prefix) {
    std::vector<std::string> out;

    // 1. Directory containing the running winix.exe (covers build\ layout).
    char exe_path[MAX_PATH] = {};
    if (GetModuleFileNameA(NULL, exe_path, MAX_PATH)) {
        std::string exe_dir(exe_path);
        auto sep = exe_dir.find_last_of("/\\");
        if (sep != std::string::npos)
            exe_dir = exe_dir.substr(0, sep);
        scan_dir_for_exes(exe_dir, prefix, out);
    }

    // 2. Every directory on PATH.
    const char *path_env = std::getenv("PATH");
    if (path_env) {
        std::string path_str(path_env);
        std::string dir;
        for (size_t i = 0; i <= path_str.size(); ++i) {
            char c = (i < path_str.size()) ? path_str[i] : '\0';
            if (c == ';' || c == '\0') {
                if (!dir.empty()) {
                    scan_dir_for_exes(dir, prefix, out);
                    dir.clear();
                }
            } else {
                dir += c;
            }
        }
    }

    // 3. Explicit probe via SearchPathA for the exact typed name.
    //    Catches App Execution Aliases (Store apps, WindowsApps stubs) that
    //    don't enumerate via directory_iterator due to reparse point handling.
    if (!prefix.empty()) {
        char found[MAX_PATH] = {};
        std::string exact = prefix + ".exe";
        if (SearchPathA(NULL, exact.c_str(), NULL, MAX_PATH, found, NULL) > 0) {
            std::string full(found);
            auto sep = full.find_last_of("/\\");
            std::string stem = (sep != std::string::npos) ? full.substr(sep + 1) : full;
            auto dot = stem.rfind('.');
            if (dot != std::string::npos) stem = stem.substr(0, dot);
            out.push_back(stem);
        }
    }

    // 4. Scan WindowsApps explicitly using FindFirstFileA (bypasses the
    //    reparse-point issue that causes directory_iterator to skip stubs).
    {
        char winapps[MAX_PATH] = {};
        if (ExpandEnvironmentStringsA(
                "%LOCALAPPDATA%\\Microsoft\\WindowsApps", winapps, MAX_PATH) > 0) {
            std::string pattern = std::string(winapps) + "\\" +
                                  (prefix.empty() ? "*" : prefix + "*") + ".exe";
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    std::string name(fd.cFileName);
                    auto dot = name.rfind('.');
                    if (dot != std::string::npos) name = name.substr(0, dot);
                    if (prefix.empty() || starts_with_ci(name, prefix))
                        out.push_back(name);
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
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
