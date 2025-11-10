// src/shell/main.cpp
// Winix Shell v1.13 — Persistent History & Aliases
// Build: CMake + MinGW/Clang (C++17)

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

#include "line_editor.hpp"   // <-- new (Step 1)
#include "completion.hpp"    // <-- new (Step 1)

namespace fs = std::filesystem;

// -------------------------------
// Small utilities
// -------------------------------
static bool str_ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string trim(const std::string& in) {
    size_t a = 0, b = in.size();
    while (a < b && std::isspace((unsigned char)in[a])) ++a;
    while (b > a && std::isspace((unsigned char)in[b - 1])) --b;
    return in.substr(a, b - a);
}

static bool is_quoted(const std::string& s) {
    return s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                             (s.front() == '\'' && s.back() == '\''));
}

static std::string unquote(const std::string& s) {
    if (is_quoted(s)) return s.substr(1, s.size() - 2);
    return s;
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
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
    // Fallback to current dir if no profile
    return fs::current_path().string();
}

static std::string prompt() {
    try {
        return "\033[1;32m[Winix]\033[0m " + fs::current_path().string() + " > ";
    } catch (...) {
        return "\033[1;32m[Winix]\033[0m > ";
    }
}

// -------------------------------
// Persistent files / config
// -------------------------------
struct Config {
    size_t history_max = 50; // default; user can override via .winixrc
};

struct Paths {
    std::string history_file;
    std::string aliases_file;
    std::string rc_file;
};

static Paths make_paths() {
    std::string base = user_home();
    Paths p;
    p.history_file = (fs::path(base) / ".winix_history.txt").string();
    p.aliases_file = (fs::path(base) / ".winix_aliases").string();
    p.rc_file      = (fs::path(base) / ".winixrc").string();
    return p;
}

static void load_rc(const Paths& paths, Config& cfg) {
    std::ifstream in(paths.rc_file);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = trim(line.substr(0, eq));
        auto val = trim(line.substr(eq + 1));
        key = to_lower(key);
        if (key == "history_size") {
            try {
                size_t v = (size_t)std::stoul(val);
                if (v > 0 && v <= 5000) cfg.history_max = v;
            } catch (...) {}
        }
    }
}

// -------------------------------
// History (no duplicates, capped)
// -------------------------------
struct History {
    std::vector<std::string> entries;
    size_t max_entries = 50;

    void dedupe_keep_latest() {
        std::vector<std::string> out;
        out.reserve(entries.size());
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            const std::string& s = *it;
            if (std::find(out.begin(), out.end(), s) == out.end()) {
                out.push_back(s);
            }
        }
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
        dedupe_keep_latest();
        if (entries.size() > max_entries) {
            entries.erase(entries.begin(),
                          entries.begin() + (entries.size() - max_entries));
        }
    }

    void save(const std::string& file) const {
        std::ofstream out(file, std::ios::trunc);
        if (!out) return;
        for (const auto& e : entries) out << e << "\n";
    }

    void add(const std::string& cmd) {
        auto s = trim(cmd);
        if (s.empty()) return;
        entries.erase(std::remove(entries.begin(), entries.end(), s), entries.end());
        entries.push_back(s);
        if (entries.size() > max_entries) {
            entries.erase(entries.begin(),
                          entries.begin() + (entries.size() - max_entries));
        }
    }

    void print() const {
        int idx = 1;
        for (auto& e : entries) {
            std::cout << idx++ << "  " << e << "\n";
        }
    }

    void clear() {
        entries.clear();
    }
};

// -------------------------------
// Aliases (persisted)
// -------------------------------
struct Aliases {
    std::map<std::string, std::string> a;

    void load(const std::string& file) {
        a.clear();
        std::ifstream in(file);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            auto name = trim(line.substr(0, eq));
            auto val  = trim(line.substr(eq + 1));
            val = unquote(val);
            if (!name.empty() && !val.empty()) a[name] = val;
        }
    }

    void save(const std::string& file) const {
        std::ofstream out(file, std::ios::trunc);
        if (!out) return;
        for (auto& kv : a) {
            out << kv.first << "=" << kv.second << "\n";
        }
    }

    void set(const std::string& name, const std::string& value) {
        a[name] = value;
    }

    bool remove(const std::string& name) {
        return a.erase(name) > 0;
    }

    std::optional<std::string> get(const std::string& name) const {
        auto it = a.find(name);
        if (it == a.end()) return std::nullopt;
        return it->second;
    }

    void print() const {
        for (auto& kv : a) {
            std::cout << "alias " << kv.first << "=\"" << kv.second << "\"\n";
        }
    }
};

// -------------------------------
// Tokenization / simple parsing
// -------------------------------
static std::vector<std::string> split_top_level(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    bool in_s = false, in_d = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\'' && !in_d) { in_s = !in_s; cur.push_back(c); continue; }
        if (c == '"'  && !in_s) { in_d = !in_d; cur.push_back(c); continue; }
        if (!in_s && !in_d && c == sep) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(trim(cur));
    return out;
}

static std::vector<std::string> shell_tokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool in_s = false, in_d = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\'' && !in_d) { in_s = !in_s; cur.push_back(c); continue; }
        if (c == '"'  && !in_s) { in_d = !in_d; cur.push_back(c); continue; }
        if (!in_s && !in_d && std::isspace((unsigned char)c)) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Expand aliases like Bash: if first token matches, replace it with alias text
static std::string expand_aliases_once(const std::string& line, const Aliases& aliases) {
    auto toks = shell_tokens(line);
    if (toks.empty()) return line;
    auto name = toks[0];
    auto ali = aliases.get(name);
    if (!ali) return line;

    std::string rest;
    for (size_t i = 1; i < toks.size(); ++i) {
        if (i > 1) rest.push_back(' ');
        rest += toks[i];
    }
    std::string expanded = *ali;
    if (!rest.empty()) {
        expanded.push_back(' ');
        expanded += rest;
    }
    return expanded;
}

// Very light variable expansion: %VAR% and $VAR
static std::string expand_vars(const std::string& line) {
    std::string out;
    bool in_s = false, in_d = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\'' && !in_d) { in_s = !in_s; out.push_back(c); continue; }
        if (c == '"'  && !in_s) { in_d = !in_d; out.push_back(c); continue; }

        if (!in_s) {
            if (c == '%') {
                size_t j = i + 1;
                while (j < line.size() && line[j] != '%') ++j;
                if (j < line.size() && line[j] == '%') {
                    auto name = line.substr(i + 1, j - (i + 1));
                    auto val = getenv_win(name);
                    out += val;
                    i = j;
                    continue;
                }
            }
            if (c == '$') {
                size_t j = i + 1;
                while (j < line.size() && (std::isalnum((unsigned char)line[j]) || line[j] == '_')) ++j;
                if (j > i + 1) {
                    auto name = line.substr(i + 1, j - (i + 1));
                    auto val = getenv_win(name);
                    out += val;
                    i = j - 1;
                    continue;
                }
            }
        }
        out.push_back(c);
    }
    return out;
}

// -------------------------------
// Execution
// -------------------------------
static DWORD spawn_cmd(const std::string& command, bool wait, HANDLE inheritStdHandles = NULL) {
    std::string full = "cmd.exe /C " + command;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(
        NULL,
        (LPSTR)full.c_str(),
        NULL, NULL, TRUE,
        CREATE_NEW_PROCESS_GROUP,
        NULL, NULL,
        &si, &pi
    );
    if (!ok) {
        DWORD e = GetLastError();
        std::cerr << "Error: failed to start: " << command << " (code " << e << ")\n";
        return e ? e : 1;
    }

    if (!wait) {
        std::cout << "[background] PID " << pi.dwProcessId << " started\n";
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static DWORD run_chain_segment(const std::string& seg, bool background) {
    if (seg.find('|') != std::string::npos) {
        std::cout << "Pipelines '|' not implemented yet.\n";
        return 1;
    }
    auto t = shell_tokens(seg);
    if (!t.empty() && to_lower(t[0]) == "cd") {
        if (t.size() == 1) {
            std::string home = user_home();
            std::error_code ec;
            fs::current_path(home, ec);
            if (ec) std::cerr << "cd: " << ec.message() << "\n";
            return 0;
        }
        std::string target = unquote(t[1]);
        std::error_code ec;
        fs::current_path(target, ec);
        if (ec) {
            std::cerr << "cd: " << target << ": " << ec.message() << "\n";
            return 1;
        }
        return 0;
    }
    return spawn_cmd(seg, !background);
}

// Support for && and &
static DWORD execute_with_ops(const std::string& line) {
    std::string s = trim(line);
    bool trailing_bg = false;
    if (!s.empty() && s.back() == '&') {
        trailing_bg = true;
        s.pop_back();
        s = trim(s);
    }

    auto segments = split_top_level(s, 0);
    segments.clear();
    {
        std::string cur;
        bool in_s=false, in_d=false;
        for (size_t i=0;i<s.size();) {
            if (s[i]=='\'' && !in_d){ in_s=!in_s; cur.push_back(s[i++]); continue; }
            if (s[i]=='"'  && !in_s){ in_d=!in_d; cur.push_back(s[i++]); continue; }
            if (!in_s && !in_d && i+1<s.size() && s[i]=='&' && s[i+1]=='&') {
                segments.push_back(trim(cur));
                cur.clear();
                i+=2;
                continue;
            }
            cur.push_back(s[i++]);
        }
        if (!cur.empty()) segments.push_back(trim(cur));
    }

    if (segments.empty()) return 0;

    DWORD last = 0;
    for (size_t i = 0; i < segments.size(); ++i) {
        bool is_last = (i + 1 == segments.size());
        std::string seg = segments[i];

        bool in_s=false, in_d=false;
        size_t amp = std::string::npos;
        for (size_t j=0;j<seg.size();++j){
            char c = seg[j];
            if (c=='\'' && !in_d){ in_s=!in_s; continue; }
            if (c=='"'  && !in_s){ in_d=!in_d; continue; }
            if (!in_s && !in_d && c=='&'){ amp = j; break; }
        }
        if (amp != std::string::npos) {
            std::string left = trim(seg.substr(0, amp));
            std::string right = trim(seg.substr(amp + 1));
            if (!left.empty()) run_chain_segment(left, /*background*/true);
            if (!right.empty()) {
                last = run_chain_segment(right, /*background*/false);
                if (last != 0) return last;
            }
        } else {
            bool background = (trailing_bg && is_last);
            last = run_chain_segment(seg, background);
            if (last != 0 && !is_last) return last; // short-circuit for &&
        }
    }
    return last;
}

// -------------------------------
// Builtins
// -------------------------------
/* Builtins:
   - set NAME=VALUE
   - alias
   - alias name="value"
   - unalias name
   - history | history -c
*/
static bool handle_builtin(const std::string& raw, Aliases& aliases, const Paths& paths,
                           History& hist) {
    auto line = trim(raw);
    if (line.empty()) return true; // treat empty line as handled

    // set NAME=VALUE
    if (to_lower(line.rfind("set ", 0) == 0 ? "set " : "") == "set ") {
        auto rest = trim(line.substr(4));
        auto eq = rest.find('=');
        if (eq == std::string::npos) {
            std::cerr << "Usage: set NAME=VALUE\n";
            return true;
        }
        std::string name = trim(rest.substr(0, eq));
        std::string value = trim(rest.substr(eq + 1));
        value = unquote(value);
        if (name.empty()) {
            std::cerr << "set: NAME missing\n";
            return true;
        }
        if (!setenv_win(name, value)) {
            std::cerr << "set: failed for " << name << "\n";
        }
        return true;
    }

    // history
    if (to_lower(line) == "history") {
        hist.print();
        return true;
    }
    if (to_lower(line) == "history -c") {
        hist.clear();
        hist.save(paths.history_file);
        return true;
    }

    // alias (print)
    if (to_lower(line) == "alias") {
        aliases.print();
        return true;
    }

    // unalias NAME
    if (to_lower(line.rfind("unalias ", 0) == 0 ? "unalias " : "") == "unalias ") {
        auto name = trim(line.substr(8));
        if (name.empty()) {
            std::cerr << "Usage: unalias NAME\n";
            return true;
        }
        if (!aliases.remove(name)) {
            std::cerr << "unalias: " << name << ": not found\n";
        } else {
            aliases.save(paths.aliases_file);
        }
        return true;
    }

    // alias NAME="value"
    if (to_lower(line.rfind("alias ", 0) == 0 ? "alias " : "") == "alias ") {
        auto spec = trim(line.substr(6));
        auto eq = spec.find('=');
        if (eq == std::string::npos) {
            std::cerr << "Usage: alias name=\"command ...\"\n";
            return true;
        }
        std::string name = trim(spec.substr(0, eq));
        std::string val  = trim(spec.substr(eq + 1));
        val = unquote(val);
        if (name.empty() || val.empty()) {
            std::cerr << "alias: invalid format\n";
            return true;
        }
        aliases.set(name, val);
        aliases.save(paths.aliases_file);
        return true;
    }

    return false; // not a builtin
}

// -------------------------------
// REPL
// -------------------------------
int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::ios::sync_with_stdio(false);

    std::cout << "Winix Shell v1.13 — Persistent History & Aliases\n";

    Config cfg;
    Paths paths = make_paths();
    load_rc(paths, cfg);

    History history;
    history.max_entries = cfg.history_max;
    history.load(paths.history_file);

    Aliases aliases;
    aliases.load(paths.aliases_file);

    while (true) {
        // Read line with tab completion (aliases + filesystem)
        std::string line = read_line_with_completion(
            prompt(),
            [&](const std::string& partial){
                return completion_matches(partial, aliases);
            }
        );

        auto original = trim(line);
        if (original.empty()) continue;

        // Graceful shell exit
        auto lower = to_lower(original);
        if (lower == "exit" || lower == "quit") {
            std::cout << "Exiting Winix Shell...\n";
            break;
        }

        // Alias + variable expansion
        std::string ali_expanded = expand_aliases_once(original, aliases);
        std::string expanded = expand_vars(ali_expanded);

        // Builtins
        if (handle_builtin(expanded, aliases, paths, history)) {
            if (!original.empty() && to_lower(trim(original)) != "history") {
                history.add(original);
                history.save(paths.history_file);
            }
            continue;
        }

        // External
        DWORD code = execute_with_ops(expanded);
        (void)code;

        // History
        history.add(original);
        history.save(paths.history_file);
    }

    return 0;
}
