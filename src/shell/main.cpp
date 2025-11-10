// Winix Shell — Stable Baseline Edition
// No completion, no advanced line editor.
// This version is guaranteed to start and run correctly.

#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// -------------------------------
// Small utilities
// -------------------------------
static std::string trim(const std::string& in) {
    size_t a = 0, b = in.size();
    while (a < b && std::isspace((unsigned char)in[a])) ++a;
    while (b > a && std::isspace((unsigned char)in[b - 1])) --b;
    return in.substr(a, b - a);
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static std::string unquote(const std::string& s) {
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') ||
         (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

static std::string getenv_win(const std::string& name) {
    DWORD needed = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
    if (!needed) return {};
    std::string v(needed, '\0');
    GetEnvironmentVariableA(name.c_str(), v.data(), needed);
    if (!v.empty() && v.back() == '\0') v.pop_back();
    return v;
}

static bool setenv_win(const std::string& name, const std::string& value) {
    return SetEnvironmentVariableA(name.c_str(), value.c_str()) != 0;
}

static std::string user_home() {
    auto u = getenv_win("USERPROFILE");
    if (!u.empty()) return u;
    return fs::current_path().string();
}

// -------------------------------
// Minimal prompt + reader
// -------------------------------
static std::string prompt() {
    try {
        return "[Winix] " + fs::current_path().string() + " > ";
    }
    catch (...) {
        return "[Winix] > ";
    }
}

// ✅ Simple guaranteed-good line reader
static std::string read_line_fallback(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    return line;
}

// -------------------------------
// Config paths
// -------------------------------
struct Paths {
    std::string history_file;
    std::string aliases_file;
};

static Paths make_paths() {
    std::string base = user_home();
    Paths p;
    p.history_file = (fs::path(base) / ".winix_history.txt").string();
    p.aliases_file = (fs::path(base) / ".winix_aliases").string();
    return p;
}

// -------------------------------
// History
// -------------------------------
struct History {
    std::vector<std::string> entries;
    size_t max_entries = 50;

    void dedupe() {
        std::vector<std::string> out;
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
            if (std::find(out.begin(), out.end(), *it) == out.end())
                out.push_back(*it);
        std::reverse(out.begin(), out.end());
        entries.swap(out);
    }

    void load(const std::string& file) {
        entries.clear();
        std::ifstream in(file);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (!line.empty()) entries.push_back(line);
        }
        dedupe();
        if (entries.size() > max_entries)
            entries.erase(entries.begin(),
                entries.begin() + (entries.size() - max_entries));
    }

    void save(const std::string& file) const {
        std::ofstream out(file, std::ios::trunc);
        for (auto& e : entries) out << e << "\n";
    }

    void add(std::string s) {
        s = trim(s);
        if (s.empty()) return;
        entries.erase(std::remove(entries.begin(), entries.end(), s), entries.end());
        entries.push_back(s);
        if (entries.size() > max_entries)
            entries.erase(entries.begin(),
                entries.begin() + (entries.size() - max_entries));
    }

    void print() const {
        int i = 1;
        for (auto& e : entries)
            std::cout << i++ << "  " << e << "\n";
    }
};

// -------------------------------
// Aliases
// -------------------------------
struct Aliases {
    std::map<std::string, std::string> a;

    void load(const std::string& file) {
        a.clear();
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            auto name = trim(line.substr(0, eq));
            auto val = trim(line.substr(eq + 1));
            val = unquote(val);

            if (!name.empty() && !val.empty())
                a[name] = val;
        }
    }

    void save(const std::string& file) const {
        std::ofstream out(file, std::ios::trunc);
        for (auto& kv : a)
            out << kv.first << "=" << kv.second << "\n";
    }

    void print() const {
        for (auto& kv : a)
            std::cout << "alias " << kv.first << "=\"" << kv.second << "\"\n";
    }

    std::optional<std::string> get(const std::string& name) const {
        auto it = a.find(name);
        if (it == a.end()) return std::nullopt;
        return it->second;
    }

    void set(const std::string& n, const std::string& v) { a[n] = v; }
    bool remove(const std::string& n) { return a.erase(n) > 0; }
};

// -------------------------------
// Basic tokenization
// -------------------------------
static std::vector<std::string> shell_tokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool qs = false, qd = false;

    for (char c : s) {
        if (c == '\'' && !qd) { qs = !qs; cur.push_back(c); continue; }
        if (c == '"' && !qs) { qd = !qd; cur.push_back(c); continue; }

        if (!qs && !qd && std::isspace((unsigned char)c)) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);

    return out;
}

// -------------------------------
// Execution helpers
// -------------------------------
static DWORD spawn_cmd(const std::string& command) {
    std::string full = "cmd.exe /C " + command;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(
        NULL,
        (LPSTR)full.c_str(),
        NULL, NULL, TRUE,
        0,
        NULL, NULL,
        &si, &pi
    );
    if (!ok) {
        DWORD e = GetLastError();
        std::cerr << "Error: failed to start: " << command
                  << " (code " << e << ")\n";
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

// -------------------------------
// Builtins
// -------------------------------
static bool handle_builtin(const std::string& raw,
                           Aliases& aliases,
                           const Paths& paths,
                           History& hist)
{
    auto line = trim(raw);
    if (line.empty()) return true;

    // exit, quit
    if (to_lower(line) == "exit" || to_lower(line) == "quit") {
        std::cout << "Exiting Winix...\n";
        ExitProcess(0);
    }

    // history
    if (to_lower(line) == "history") {
        hist.print();
        return true;
    }

    // history -c
    if (to_lower(line) == "history -c") {
        hist.entries.clear();
        hist.save(paths.history_file);
        return true;
    }

    // alias
    if (to_lower(line) == "alias") {
        aliases.print();
        return true;
    }

    // unalias NAME
    if (line.rfind("unalias ", 0) == 0) {
        auto name = trim(line.substr(8));
        if (!aliases.remove(name))
            std::cerr << "unalias: not found\n";
        else
            aliases.save(paths.aliases_file);
        return true;
    }

    // alias NAME="value"
    if (line.rfind("alias ", 0) == 0) {
        auto spec = trim(line.substr(6));
        auto eq = spec.find('=');
        if (eq == std::string::npos) {
            std::cerr << "Usage: alias name=\"value\"\n";
            return true;
        }
        auto name = trim(spec.substr(0, eq));
        auto val  = trim(spec.substr(eq + 1));
        val = unquote(val);

        aliases.set(name, val);
        aliases.save(paths.aliases_file);
        return true;
    }

    // set NAME=VALUE
    if (line.rfind("set ", 0) == 0) {
        auto rest = trim(line.substr(4));
        auto eq = rest.find('=');
        if (eq == std::string::npos) {
            std::cerr << "Usage: set NAME=VALUE\n";
            return true;
        }
        auto name = trim(rest.substr(0, eq));
        auto val  = trim(rest.substr(eq + 1));
        val = unquote(val);

        setenv_win(name, val);
        return true;
    }

    return false;
}

// -------------------------------
// REPL
// -------------------------------
int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "Winix Shell — Stable Edition\n";

    Paths paths = make_paths();

    History hist;
    hist.load(paths.history_file);

    Aliases aliases;
    aliases.load(paths.aliases_file);

    while (true) {
        std::string line = read_line_fallback(prompt());
        auto original = trim(line);
        if (original.empty()) continue;

        // builtins
        if (handle_builtin(original, aliases, paths, hist)) {
            hist.add(original);
            hist.save(paths.history_file);
            continue;
        }

        // external commands
        spawn_cmd(original);

        hist.add(original);
        hist.save(paths.history_file);
    }

    return 0;
}
