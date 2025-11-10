// src/shell/main.cpp
// Winix Shell v1.13 — Modular Aliases + Completion + LineEditor
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

#include "aliases.hpp"
#include "line_editor.hpp"
#include "completion.hpp"

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
    size_t history_max = 50;
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
// History
// -------------------------------

struct History {
    std::vector<std::string> entries;
    size_t max_entries = 50;

    void dedupe_keep_latest() {
        std::vector<std::string> out;
        out.reserve(entries.size());
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (std::find(out.begin(), out.end(), *it) == out.end())
                out.push_back(*it);
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
        for (auto& e : entries) out << e << "\n";
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

    void clear() { entries.clear(); }

    void print() const {
        int idx = 1;
        for (auto& e : entries) std::cout << idx++ << "  " << e << "\n";
    }
};

// -------------------------------
// Tokenization / parsing (unchanged)
// -------------------------------

static std::vector<std::string> split_top_level(const std::string& s, char sep) {
    std::vector<std::string> out; std::string cur;
    bool in_s = false, in_d = false;
    for (char c : s) {
        if (c=='\'' && !in_d) { in_s=!in_s; cur.push_back(c); continue; }
        if (c=='"'  && !in_s) { in_d=!in_d; cur.push_back(c); continue; }
        if (!in_s && !in_d && c==sep) {
            out.push_back(trim(cur)); cur.clear();
        } else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(trim(cur));
    return out;
}

static std::vector<std::string> shell_tokens(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    bool in_s = false, in_d = false;
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

// Expand aliases: only first token
static std::string expand_aliases_once(const std::string& line, const Aliases& aliases) {
    auto toks = shell_tokens(line);
    if (toks.empty()) return line;
    auto ali = aliases.get(toks[0]);
    if (!ali) return line;

    std::string rest;
    for (size_t i = 1; i < toks.size(); ++i) {
        if (i > 1) rest += ' ';
        rest += toks[i];
    }

    std::string expanded = *ali;
    if (!rest.empty()) {
        expanded += " " + rest;
    }
    return expanded;
}

// Light var expansion: %VAR% and $VAR
static std::string expand_vars(const std::string& line) {
    std::string out;
    bool in_s=false, in_d=false;
    for (size_t i=0;i<line.size();++i) {
        char c=line[i];
        if (c=='\'' && !in_d){ in_s=!in_s; out.push_back(c); continue; }
        if (c=='"'  && !in_s){ in_d=!in_d; out.push_back(c); continue; }

        if (!in_s) {
            if (c=='%') {
                size_t j=i+1; while(j<line.size() && line[j] != '%') j++;
                if (j<line.size()) {
                    out += getenv_win(line.substr(i+1, j-i-1));
                    i=j; continue;
                }
            }
            if (c=='$') {
                size_t j=i+1;
                while(j<line.size() &&
                      (std::isalnum((unsigned char)line[j]) || line[j]=='_')) j++;
                if (j>i+1) {
                    out += getenv_win(line.substr(i+1, j-i-1));
                    i=j-1; continue;
                }
            }
        }
        out.push_back(c);
    }
    return out;
}

// -------------------------------
// Execution logic (unchanged)
// -------------------------------

static DWORD spawn_cmd(const std::string& cmd, bool wait) {
    std::string full = "cmd.exe /C " + cmd;

    STARTUPINFOA si{}; si.cb=sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(NULL,
        (LPSTR)full.c_str(), NULL, NULL, TRUE,
        CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi);

    if (!ok) {
        DWORD e=GetLastError();
        std::cerr<<"Error starting: "<<cmd<<" (code "<<e<<")\n";
        return e ? e : 1;
    }

    if (!wait) {
        std::cout<<"[background] PID "<<pi.dwProcessId<<" started\n";
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code=0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static DWORD run_chain_segment(const std::string& seg, bool background) {
    if (seg.find('|') != std::string::npos) {
        std::cout<<"Pipelines '|' not implemented.\n";
        return 1;
    }

    auto t=shell_tokens(seg);
    if (!t.empty() && to_lower(t[0])=="cd") {
        if (t.size()==1) {
            fs::current_path(user_home(), std::error_code{});
            return 0;
        }
        std::string target=unquote(t[1]);
        std::error_code ec;
        fs::current_path(target, ec);
        if (ec) std::cerr<<"cd: "<<ec.message()<<"\n";
        return ec ? 1 : 0;
    }

    return spawn_cmd(seg, !background);
}

static DWORD execute_with_ops(const std::string& line) {
    std::string s=trim(line);
    bool trailing_bg=false;
    if (!s.empty() && s.back()=='&') {
        trailing_bg=true; s.pop_back(); s=trim(s);
    }

    // Split on &&
    std::vector<std::string> segments;
    {
        bool in_s=false,in_d=false;
        std::string cur;
        for (size_t i=0;i<s.size();) {
            char c=s[i];
            if (c=='\'' && !in_d){ in_s=!in_s; cur.push_back(c); i++; continue; }
            if (c=='"'  && !in_s){ in_d=!in_d; cur.push_back(c); i++; continue; }

            if (!in_s && !in_d && i+1<s.size() && c=='&' && s[i+1]=='&') {
                segments.push_back(trim(cur)); cur.clear(); i+=2; continue;
            }
            cur.push_back(c); i++;
        }
        if (!cur.empty()) segments.push_back(trim(cur));
    }

    if (segments.empty()) return 0;
    DWORD last=0;

    for (size_t i=0;i<segments.size();++i) {
        bool is_last=(i+1==segments.size());
        std::string seg=segments[i];

        bool in_s=false,in_d=false; size_t amp=std::string::npos;
        for (size_t j=0;j<seg.size();++j) {
            char c=seg[j];
            if (c=='\'' && !in_d){ in_s=!in_s; continue; }
            if (c=='"'  && !in_s){ in_d=!in_d; continue; }
            if (!in_s && !in_d && c=='&'){ amp=j; break; }
        }

        if (amp!=std::string::npos) {
            std::string left=trim(seg.substr(0,amp));
            std::string right=trim(seg.substr(amp+1));

            if (!left.empty()) run_chain_segment(left, true);
            if (!right.empty()) {
                last=run_chain_segment(right, false);
                if (last!=0) return last;
            }
        } else {
            bool background=(trailing_bg && is_last);
            last=run_chain_segment(seg, background);
            if (last!=0 && !is_last) return last;
        }
    }

    return last;
}

// -------------------------------
// Builtins
// -------------------------------

static bool handle_builtin(const std::string& raw, Aliases& aliases,
                           const Paths& paths, History& hist) {
    auto line=trim(raw);
    if (line.empty()) return true;

    // set NAME=VALUE
    if (to_lower(line.rfind("set ",0)==0 ? "set " : "") == "set ") {
        std::string rest=trim(line.substr(4));
        auto eq=rest.find('=');
        if (eq==std::string::npos) {
            std::cerr<<"Usage: set NAME=VALUE\n"; return true;
        }
        std::string name=trim(rest.substr(0,eq));
        std::string val =trim(rest.substr(eq+1));
        val=unquote(val);
        if (!setenv_win(name,val))
            std::cerr<<"set failed for "<<name<<"\n";
        return true;
    }

    // history
    if (to_lower(line)=="history"){ hist.print(); return true; }
    if (to_lower(line)=="history -c"){ hist.clear(); hist.save(paths.history_file); return true; }

    // alias
    if (to_lower(line)=="alias"){ aliases.print(); return true; }

    if (to_lower(line.rfind("unalias ",0)==0 ? "unalias " : "") == "unalias ") {
        std::string name=trim(line.substr(8));
        if (!aliases.remove(name))
            std::cerr<<"unalias: "<<name<<": not found\n";
        else
            aliases.save(paths.aliases_file);
        return true;
    }

    if (to_lower(line.rfind("alias ",0)==0 ? "alias " : "") == "alias ") {
        std::string spec=trim(line.substr(6));
        auto eq=spec.find('=');
        if (eq==std::string::npos) {
            std::cerr<<"Usage: alias NAME=\"VALUE\"\n"; return true;
        }
        std::string name=trim(spec.substr(0,eq));
        std::string val =trim(spec.substr(eq+1));
        val=unquote(val);
        aliases.set(name,val);
        aliases.save(paths.aliases_file);
        return true;
    }

    return false;
}

// -------------------------------
// REPL
// -------------------------------

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::ios::sync_with_stdio(false);

    std::cout << "Winix Shell v1.13 — Modular Edition\n";

    Config cfg;
    Paths paths=make_paths();
    load_rc(paths,cfg);

    History history; history.max_entries=cfg.history_max;
    history.load(paths.history_file);

    Aliases aliases;
    aliases.load(paths.aliases_file);

    // NEW: Create the line editor with completion callback
    LineEditor editor([&](const std::string& partial){
        return completion_matches(partial, aliases);
    });

    while (true) {
        auto line_opt = editor.read_line(prompt());
        if (!line_opt) break;

        std::string line = trim(*line_opt);
        if (line.empty()) continue;

        std::string lower = to_lower(line);
        if (lower=="exit" || lower=="quit") {
            std::cout<<"Exiting Winix...\n";
            break;
        }

        std::string ali_expanded = expand_aliases_once(line, aliases);
        std::string expanded = expand_vars(ali_expanded);

        if (handle_builtin(expanded, aliases, paths, history)) {
            history.add(line);
            history.save(paths.history_file);
            continue;
        }

        execute_with_ops(expanded);
        history.add(line);
        history.save(paths.history_file);
    }

    return 0;
}
