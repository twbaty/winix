// src/shell/main.cpp
// Winix Shell — Stable Edition (matches aliases/line_editor/completion stubs provided)

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

#include "aliases.hpp"
#include "completion.hpp"
#include "line_editor.hpp"

namespace fs = std::filesystem;

// ---------- small utils ----------
static std::string trim(const std::string& in) {
    size_t a = 0, b = in.size();
    while (a < b && std::isspace((unsigned char)in[a])) ++a;
    while (b > a && std::isspace((unsigned char)in[b - 1])) --b;
    return in.substr(a, b - a);
}
static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}
static bool is_quoted(const std::string& s) {
    return s.size() >= 2 && ((s.front()=='"' && s.back()=='"') || (s.front()=='\'' && s.back()=='\''));
}
static std::string unquote(const std::string& s) { return is_quoted(s) ? s.substr(1, s.size()-2) : s; }

static std::string getenv_win(const std::string& name) {
    DWORD n = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
    if (!n) return {};
    std::string v(n, '\0');
    GetEnvironmentVariableA(name.c_str(), v.data(), n);
    if (!v.empty() && v.back()=='\0') v.pop_back();
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

// ---------- config/paths ----------
struct Config { size_t history_max = 100; };
struct Paths {
    std::string history_file;
    std::string aliases_file;
    std::string rc_file;
};
static Paths make_paths() {
    const auto home = user_home();
    Paths p;
    p.history_file = (fs::path(home) / ".winix_history.txt").string();
    p.aliases_file = (fs::path(home) / ".winix_aliases").string();
    p.rc_file      = (fs::path(home) / ".winixrc").string();
    return p;
}
static void load_rc(const Paths& paths, Config& cfg) {
    std::ifstream in(paths.rc_file);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0]=='#') continue;
        const auto eq = line.find('=');
        if (eq==std::string::npos) continue;
        auto k = to_lower(trim(line.substr(0, eq)));
        auto v = trim(line.substr(eq+1));
        if (k=="history_size") {
            try { size_t n = (size_t)std::stoul(v); if (n>0 && n<=5000) cfg.history_max = n; } catch (...) {}
        }
    }
}

// ---------- history ----------
struct History {
    std::vector<std::string> entries;
    size_t max_entries = 100;

    void load(const std::string& file) {
        entries.clear();
        std::ifstream in(file);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (!line.empty()) entries.push_back(line);
        }
        // dedupe keep latest
        std::vector<std::string> out;
        out.reserve(entries.size());
        for (auto it=entries.rbegin(); it!=entries.rend(); ++it) {
            if (std::find(out.begin(), out.end(), *it)==out.end()) out.push_back(*it);
        }
        std::reverse(out.begin(), out.end());
        entries.swap(out);
        if (entries.size()>max_entries) entries.erase(entries.begin(), entries.begin()+(entries.size()-max_entries));
    }
    void save(const std::string& file) const {
        std::ofstream out(file, std::ios::trunc);
        if (!out) return;
        for (auto& e : entries) out << e << "\n";
    }
    void add(const std::string& s) {
        const auto t = trim(s);
        if (t.empty()) return;
        entries.erase(std::remove(entries.begin(), entries.end(), t), entries.end());
        entries.push_back(t);
        if (entries.size()>max_entries) entries.erase(entries.begin(), entries.begin()+(entries.size()-max_entries));
    }
    void print() const {
        int i=1;
        for (auto& e : entries) std::cout << i++ << "  " << e << "\n";
    }
    void clear() { entries.clear(); }
};

// ---------- tokenization/expansion ----------
static std::vector<std::string> shell_tokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool in_s=false, in_d=false;
    for (char c : s) {
        if (c=='\'' && !in_d) { in_s=!in_s; cur.push_back(c); continue; }
        if (c=='"'  && !in_s) { in_d=!in_d; cur.push_back(c); continue; }
        if (!in_s && !in_d && std::isspace((unsigned char)c)) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static std::string expand_aliases_once(const std::string& line, const Aliases& aliases) {
    auto toks = shell_tokens(line);
    if (toks.empty()) return line;
    auto a = aliases.get(toks[0]);
    if (!a) return line;
    std::string rest;
    for (size_t i=1;i<toks.size();++i) { if (i>1) rest.push_back(' '); rest += toks[i]; }
    return rest.empty() ? *a : (*a + " " + rest);
}

static std::string expand_vars(const std::string& line) {
    std::string out;
    bool in_s=false, in_d=false;
    for (size_t i=0;i<line.size();++i) {
        char c=line[i];
        if (c=='\'' && !in_d){ in_s=!in_s; out.push_back(c); continue; }
        if (c=='"'  && !in_s){ in_d=!in_d; out.push_back(c); continue; }

        if (!in_s) {
            if (c=='%') {
                size_t j=i+1; while (j<line.size() && line[j] != '%') ++j;
                if (j<line.size() && line[j]=='%') { out += getenv_win(line.substr(i+1, j-(i+1))); i=j; continue; }
            }
            if (c=='$') {
                size_t j=i+1; while (j<line.size() && (std::isalnum((unsigned char)line[j]) || line[j]=='_')) ++j;
                if (j>i+1) { out += getenv_win(line.substr(i+1, j-(i+1))); i=j-1; continue; }
            }
        }
        out.push_back(c);
    }
    return out;
}

// ---------- process spawn ----------
static DWORD spawn_cmd(const std::string& command, bool wait) {
    std::string full = "cmd.exe /C " + command;

    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(NULL,(LPSTR)full.c_str(),NULL,NULL,TRUE,CREATE_NEW_PROCESS_GROUP,NULL,NULL,&si,&pi);
    if (!ok) {
        DWORD e = GetLastError();
        std::cerr << "Error: failed to start: " << command << " (code " << e << ")\n";
        return e ? e : 1;
    }
    if (!wait) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return 0; }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code=0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code;
}

static DWORD run_segment(const std::string& seg) {
    auto t = shell_tokens(seg);
    if (!t.empty() && to_lower(t[0])=="cd") {
        if (t.size()==1) {
            std::error_code ec;
            fs::current_path(user_home(), ec);
            if (ec) std::cerr << "cd: " << ec.message() << "\n";
            return 0;
        }
        std::string target = unquote(t[1]);
        std::error_code ec;
        fs::current_path(target, ec);
        if (ec) { std::cerr << "cd: " << target << ": " << ec.message() << "\n"; return 1; }
        return 0;
    }
    return spawn_cmd(seg, /*wait*/true);
}

// ---------- builtins ----------
static bool handle_builtin(const std::string& raw, Aliases& aliases, const Paths& paths, History& hist) {
    const auto line = trim(raw);
    if (line.empty()) return true;

    // set NAME=VALUE
    if (to_lower(line).rfind("set ",0) == 0) {
        auto rest = trim(line.substr(4));
        auto eq = rest.find('=');
        if (eq==std::string::npos) { std::cerr << "Usage: set NAME=VALUE\n"; return true; }
        auto name = trim(rest.substr(0,eq));
        auto val  = trim(rest.substr(eq+1));
        val = unquote(val);
        if (name.empty()) { std::cerr << "set: NAME missing\n"; return true; }
        if (!setenv_win(name, val)) std::cerr << "set: failed for " << name << "\n";
        return true;
    }

    // history / history -c
    if (to_lower(line)=="history") { hist.print(); return true; }
    if (to_lower(line)=="history -c") { hist.clear(); hist.save(paths.history_file); return true; }

    // alias (print)
    if (to_lower(line)=="alias") {
        for (auto& name : aliases.names()) {
            auto v = aliases.get(name);
            if (v) std::cout << "alias " << name << "=\"" << *v << "\"\n";
        }
        return true;
    }

    // unalias NAME
    if (to_lower(line).rfind("unalias ",0) == 0) {
        auto name = trim(line.substr(8));
        if (name.empty()) { std::cerr << "Usage: unalias NAME\n"; return true; }
        if (!aliases.remove(name)) std::cerr << "unalias: " << name << ": not found\n";
        else aliases.save(paths.aliases_file);
        return true;
    }

    // alias NAME="value"
    if (to_lower(line).rfind("alias ",0) == 0) {
        auto spec = trim(line.substr(6));
        auto eq = spec.find('=');
        if (eq==std::string::npos) { std::cerr << "Usage: alias name=\"command ...\"\n"; return true; }
        auto name = trim(spec.substr(0,eq));
        auto val  = unquote(trim(spec.substr(eq+1)));
        if (name.empty() || val.empty()) { std::cerr << "alias: invalid format\n"; return true; }
        aliases.set(name, val);
        aliases.save(paths.aliases_file);
        return true;
    }

    return false; // not a builtin
}

// ---------- prompt ----------
static std::string prompt() {
    try {
        std::string cwd = fs::current_path().string();
        return "\x1b[32m[Winix] " + cwd + " >\x1b[0m ";
    }
    catch (...) {
        return "\x1b[32m[Winix] >\x1b[0m ";
    }
}

// ---------- main ----------
int main() {
    #ifdef _WIN32
    // Enable ANSI escape sequences (VT mode)
    {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD outMode = 0;
        if (GetConsoleMode(hOut, &outMode)) {
            outMode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
            SetConsoleMode(hOut, outMode);
        }

        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD inMode = 0;
        if (GetConsoleMode(hIn, &inMode)) {
            inMode |= 0x0200; // ENABLE_VIRTUAL_TERMINAL_INPUT
            SetConsoleMode(hIn, inMode);
        }
    }
    #endif

    SetConsoleOutputCP(CP_UTF8);
    // Enable ANSI color (VT sequences)
    DWORD mode = 0;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleMode(hOut, &mode)) {
        mode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
        SetConsoleMode(hOut, mode);
    }

    std::ios::sync_with_stdio(false);
    std::cout << "Winix Shell — Stable Edition\n";

    Config cfg;
    auto paths = make_paths();
    load_rc(paths, cfg);

    History hist;
    hist.max_entries = cfg.history_max;
    hist.load(paths.history_file);

    Aliases aliases;
    aliases.load(paths.aliases_file);

    LineEditor editor([&](const std::string& partial){
        return completion_matches(partial, aliases);
    });

    while (true) {
        auto in = editor.read_line(prompt());
        if (!in.has_value()) break;
        auto original = trim(*in);
        if (original.empty()) continue;

        auto lower = to_lower(original);
        if (lower=="exit" || lower=="quit") break;

        // expand
        auto ali = expand_aliases_once(original, aliases);
        auto exp = expand_vars(ali);

        // builtins?
        if (handle_builtin(exp, aliases, paths, hist)) {
            if (to_lower(original)!="history") { hist.add(original); hist.save(paths.history_file); }
            continue;
        }

        // external
        (void)run_segment(exp);

        hist.add(original);
        hist.save(paths.history_file);
    }

    return 0;
}
