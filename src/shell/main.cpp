// src/shell/main.cpp
// Winix Shell — Stable Edition (final, corrected)

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

// --------------------------------------------------
// Utility helpers
// --------------------------------------------------
static std::string trim(const std::string& in) {
    size_t a = 0, b = in.size();
    while (a < b && std::isspace((unsigned char)in[a])) ++a;
    while (b > a && std::isspace((unsigned char)in[b - 1])) --b;
    return in.substr(a, b - a);
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static bool is_quoted(const std::string& s) {
    return s.size() >= 2 &&
        ((s.front()=='"' && s.back()=='"') ||
         (s.front()=='\'' && s.back()=='\''));
}

static std::string unquote(const std::string& s) {
    return is_quoted(s) ? s.substr(1, s.size()-2) : s;
}

// --------------------------------------------------
// Glob expansion
// --------------------------------------------------
static bool has_glob(const std::string& s) {
    return s.find('*') != std::string::npos || s.find('?') != std::string::npos;
}

// Expand a single glob pattern to matching paths (sorted).
// Returns empty if no matches; caller passes through literally.
static std::vector<std::string> glob_one(const std::string& pattern) {
    // Reject globs in non-final path components (too complex for now).
    size_t sep = std::string::npos;
    for (size_t i = pattern.size(); i-- > 0; )
        if (pattern[i] == '/' || pattern[i] == '\\') { sep = i; break; }
    if (sep != std::string::npos && has_glob(pattern.substr(0, sep)))
        return {};

    // Keep the directory prefix to prepend to each result.
    std::string prefix = (sep == std::string::npos) ? "" : pattern.substr(0, sep + 1);

    // FindFirstFile wants backslashes.
    std::string fsearch = pattern;
    for (char& c : fsearch) if (c == '/') c = '\\';

    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(fsearch.c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return {};

    std::vector<std::string> results;
    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
            continue;
        results.push_back(prefix + ffd.cFileName);
    } while (FindNextFileA(h, &ffd));

    FindClose(h);
    std::sort(results.begin(), results.end());
    return results;
}

// Expand glob tokens in a token list.
// Quoted tokens: quotes stripped, no expansion.
// Unquoted glob tokens: expanded; if no match, left literal.
static std::vector<std::string> glob_expand(const std::vector<std::string>& tokens) {
    std::vector<std::string> out;
    out.reserve(tokens.size());
    for (const auto& tok : tokens) {
        if (is_quoted(tok)) { out.push_back(unquote(tok)); continue; }
        if (!has_glob(tok))  { out.push_back(tok);         continue; }
        auto matches = glob_one(tok);
        if (matches.empty()) out.push_back(tok);
        else for (auto& m : matches) out.push_back(m);
    }
    return out;
}

// Quote an argument for a Windows command line.
// Must quote: spaces, embedded quotes, and glob chars (* ?) — the latter
// because MinGW's CRT startup code expands unquoted glob patterns in argv.
static std::string quote_arg(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \"*?") == std::string::npos)
        return a;
    std::string r = "\"";
    for (char c : a) { if (c == '"') r += "\\\""; else r += c; }
    r += '"';
    return r;
}

static std::string getenv_win(const std::string& name) {
    DWORD n = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
    if (!n) return {};
    std::string v(n, '\0');
    GetEnvironmentVariableA(name.c_str(), v.data(), n);
    if (!v.empty() && v.back() == '\0') v.pop_back();
    return v;
}

static bool setenv_win(const std::string& n, const std::string& v) {
    return SetEnvironmentVariableA(n.c_str(), v.c_str()) != 0;
}

static std::string user_home() {
    auto u = getenv_win("USERPROFILE");
    if (!u.empty()) return u;
    return fs::current_path().string();
}

// --------------------------------------------------
// Paths + config
// --------------------------------------------------
struct Config {
    size_t history_max  = 100;
    bool case_sensitive = false;
    // PS1 format string — expanded at each prompt render.
    // Supports: \u \h \w \W \$ \t \d \n \e \[ \]
    std::string ps1 = "\\[\\e[32m\\][Winix] \\w >\\[\\e[0m\\] ";
};

struct Paths {
    std::string history_file;
    std::string aliases_file;
    std::string rc_file;

    std::string bin_dir;        // where winix.exe lives
    std::string coreutils_dir;  // build/coreutils/
};

static Paths make_paths() {
    const auto home = user_home();
    Paths p;

    p.history_file = (fs::path(home) / ".winix_history.txt").string();
    p.aliases_file = (fs::path(home) / ".winix_aliases").string();
    p.rc_file      = (fs::path(home) / ".winixrc").string();

    // Find winix.exe dir
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    fs::path exe = fs::path(buf).parent_path();

    p.bin_dir = exe.string();
    p.coreutils_dir = (exe.parent_path() / "coreutils").string();

    return p;
}

static void load_rc(const Paths& paths, Config& cfg) {
    std::ifstream in(paths.rc_file);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0]=='#') continue;

        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        auto k = to_lower(trim(line.substr(0, pos)));
        auto v = trim(line.substr(pos+1));

        if (k == "history_size") {
            try {
                size_t n = std::stoul(v);
                if (n > 0 && n <= 5000) cfg.history_max = n;
            } catch (...) {}
        } else if (k == "case") {
            cfg.case_sensitive = (to_lower(v) == "on");
        } else if (k == "ps1") {
            cfg.ps1 = v;  // stored as raw format string
        }
    }
}

static void save_rc(const Paths& paths, const Config& cfg) {
    std::ofstream out(paths.rc_file, std::ios::trunc);
    if (!out) return;
    out << "history_size=" << cfg.history_max << "\n";
    out << "case=" << (cfg.case_sensitive ? "on" : "off") << "\n";
    out << "ps1=" << cfg.ps1 << "\n";
}

// --------------------------------------------------
// History
// --------------------------------------------------
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

        // dedupe from newest → oldest, keep newest
        std::vector<std::string> out;
        out.reserve(entries.size());
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (std::find(out.begin(), out.end(), *it) == out.end())
                out.push_back(*it);
        }
        std::reverse(out.begin(), out.end());
        entries.swap(out);

        if (entries.size() > max_entries)
            entries.erase(entries.begin(),
                          entries.begin() + (entries.size() - max_entries));
    }

    void save(const std::string& file) const {
        std::ofstream out(file, std::ios::trunc);
        if (!out) return;
        for (auto& e : entries) out << e << "\n";
    }

    void add(const std::string& s) {
        auto t = trim(s);
        if (t.empty()) return;

        entries.erase(
            std::remove(entries.begin(), entries.end(), t),
            entries.end()
        );
        entries.push_back(t);

        if (entries.size() > max_entries)
            entries.erase(entries.begin(),
                          entries.begin() + (entries.size() - max_entries));
    }

    void print() const {
        int i = 1;
        for (auto& e : entries)
            std::cout << i++ << "  " << e << "\n";
    }

    void clear() { entries.clear(); }
};

// --------------------------------------------------
// Tokenization + variable expansion
// --------------------------------------------------
static std::vector<std::string> shell_tokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool in_s=false, in_d=false;

    for (char c : s) {
        if (c=='\'' && !in_d) { in_s=!in_s; cur.push_back(c); continue; }
        if (c=='"'  && !in_s) { in_d=!in_d; cur.push_back(c); continue; }

        if (!in_s && !in_d && std::isspace((unsigned char)c)) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }

    if (!cur.empty()) out.push_back(cur);
    return out;
}

// --------------------------------------------------
// Shell-local variables  (VAR=value assignments)
// --------------------------------------------------
static std::map<std::string, std::string> g_shell_vars;
static std::vector<std::string> g_positional; // $1...$N positional params (0-indexed)
static std::map<std::string, std::vector<std::string>> g_functions; // user-defined functions

// local VAR scope stack — one frame per active function call.
// Each frame maps variable name -> saved outer value (nullopt = didn't exist).
struct LocalFrame {
    std::map<std::string, std::optional<std::string>> saved;
};
static std::vector<LocalFrame> g_local_stack;

// Run a command string and capture its stdout output.
static std::string capture_command(const std::string& cmd) {
    FILE* fp = _popen(cmd.c_str(), "r");
    if (!fp) return {};
    std::string result;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) result += buf;
    _pclose(fp);
    // trim trailing newlines (POSIX $() behaviour)
    while (!result.empty() && (result.back()=='\n' || result.back()=='\r'))
        result.pop_back();
    return result;
}

static std::string expand_aliases_once(const std::string& line, const Aliases& a) {
    auto toks = shell_tokens(line);
    if (toks.empty()) return line;

    auto val = a.get(toks[0]);
    if (!val) return line;

    std::string rest;
    for (size_t i=1;i<toks.size();++i) {
        if (i>1) rest.push_back(' ');
        rest += toks[i];
    }

    return rest.empty() ? *val : (*val + " " + rest);
}

// --------------------------------------------------
// Arithmetic evaluator for $(( expr ))
// --------------------------------------------------
static long long eval_arith(const std::string& s) {
    const char* p = s.c_str();
    std::function<long long()> parse_expr, parse_term, parse_unary, parse_primary;

    auto skip = [&]() { while (*p == ' ' || *p == '\t') ++p; };

    parse_primary = [&]() -> long long {
        skip();
        if (*p == '(') {
            ++p;
            long long v = parse_expr();
            skip();
            if (*p == ')') ++p;
            return v;
        }
        char* end;
        long long v = strtoll(p, &end, 10);
        if (end > p) p = end;
        return v;
    };

    parse_unary = [&]() -> long long {
        skip();
        if (*p == '-') { ++p; return -parse_unary(); }
        if (*p == '+') { ++p; return  parse_unary(); }
        return parse_primary();
    };

    parse_term = [&]() -> long long {
        long long v = parse_unary();
        for (;;) {
            skip();
            if (*p == '*') { ++p; v *= parse_unary(); }
            else if (*p == '/') { ++p; long long d = parse_unary(); v = d ? v/d : 0; }
            else if (*p == '%') { ++p; long long d = parse_unary(); v = d ? v%d : 0; }
            else break;
        }
        return v;
    };

    parse_expr = [&]() -> long long {
        long long v = parse_term();
        for (;;) {
            skip();
            if      (*p == '+') { ++p; v += parse_term(); }
            else if (*p == '-') { ++p; v -= parse_term(); }
            else break;
        }
        return v;
    };

    return parse_expr();
}

static std::string expand_vars(const std::string& line, int last_exit = 0) {
    std::string out;
    bool in_s=false, in_d=false;

    for (size_t i=0; i<line.size(); ++i) {
        char c = line[i];

        if (c=='\'' && !in_d) { in_s=!in_s; out.push_back(c); continue; }
        if (c=='"'  && !in_s) { in_d=!in_d; out.push_back(c); continue; }

        if (!in_s && !in_d) {
            // ~ at word boundary → home directory
            if (c == '~') {
                bool at_word = (i == 0 || std::isspace((unsigned char)line[i-1]));
                if (at_word) {
                    size_t j = i + 1;
                    if (j >= line.size() || line[j]=='/' || line[j]=='\\' ||
                        std::isspace((unsigned char)line[j])) {
                        out += user_home();
                        continue;
                    }
                }
            }
        }

        if (!in_s) {
            // %VAR%
            if (c == '%') {
                size_t j=i+1;
                while (j<line.size() && line[j] != '%') ++j;
                if (j < line.size()) {
                    out += getenv_win(line.substr(i+1, j-(i+1)));
                    i=j;
                    continue;
                }
            }
            // $? — last exit code
            if (c == '$' && i+1 < line.size() && line[i+1] == '?') {
                out += std::to_string(last_exit);
                i++;
                continue;
            }
            // $# — count of positional params
            if (c == '$' && i+1 < line.size() && line[i+1] == '#') {
                out += std::to_string(g_positional.size());
                i++;
                continue;
            }
            // $@ — all positional params space-separated
            if (c == '$' && i+1 < line.size() && line[i+1] == '@') {
                for (size_t k = 0; k < g_positional.size(); ++k) {
                    if (k) out += ' ';
                    out += g_positional[k];
                }
                i++;
                continue;
            }
            // $0-$9 — positional params ($0 = "winix")
            if (c == '$' && i+1 < line.size() && std::isdigit((unsigned char)line[i+1])) {
                int n = line[i+1] - '0';
                if (n == 0) out += "winix";
                else if (n <= (int)g_positional.size()) out += g_positional[n - 1];
                i++;
                continue;
            }
            // $(( expr )) — arithmetic expansion (must check before $( cmd ))
            if (c == '$' && i+1 < line.size() && line[i+1] == '(' &&
                            i+2 < line.size() && line[i+2] == '(') {
                size_t j = i + 3;
                int depth = 0;
                while (j < line.size()) {
                    if (line[j] == '(') { ++depth; ++j; continue; }
                    if (line[j] == ')') {
                        if (depth == 0 && j+1 < line.size() && line[j+1] == ')') break;
                        --depth;
                    }
                    ++j;
                }
                std::string expr = line.substr(i + 3, j - (i + 3));
                std::string expanded_expr = expand_vars(expr, last_exit);
                out += std::to_string(eval_arith(expanded_expr));
                i = j + 1; // skip past "))"
                continue;
            }
            // $( cmd ) — command substitution
            if (c == '$' && i+1 < line.size() && line[i+1] == '(') {
                int depth = 1;
                size_t j = i + 2;
                while (j < line.size() && depth > 0) {
                    if (line[j] == '(') depth++;
                    else if (line[j] == ')') depth--;
                    j++;
                }
                std::string subcmd = line.substr(i+2, j-1 - (i+2));
                out += capture_command(subcmd);
                i = j - 1;
                continue;
            }
            // ${ ... } — braced variable with optional operators
            if (c == '$' && i+1 < line.size() && line[i+1] == '{') {
                size_t j = i + 2;
                while (j < line.size() && line[j] != '}') ++j;
                if (j < line.size()) {
                    std::string inner = line.substr(i+2, j-(i+2));
                    i = j; // advance past '}'

                    // ${#VAR} — string length
                    if (!inner.empty() && inner[0] == '#') {
                        std::string name = inner.substr(1);
                        auto it = g_shell_vars.find(name);
                        std::string val = (it != g_shell_vars.end()) ? it->second : getenv_win(name);
                        out += std::to_string(val.size());
                        continue;
                    }

                    // ${VAR:-def}, ${VAR:=val}, ${VAR:+val}, ${VAR:?msg}
                    size_t op_pos = inner.find(':');
                    if (op_pos != std::string::npos && op_pos + 1 < inner.size()) {
                        char op = inner[op_pos + 1];
                        std::string name = inner.substr(0, op_pos);
                        std::string arg  = expand_vars(inner.substr(op_pos + 2), last_exit);
                        auto it = g_shell_vars.find(name);
                        std::string val = (it != g_shell_vars.end()) ? it->second : getenv_win(name);
                        bool is_set = !val.empty();
                        if (op == '-') {
                            out += is_set ? val : arg;
                        } else if (op == '=') {
                            if (!is_set) { g_shell_vars[name] = arg; val = arg; }
                            out += val;
                        } else if (op == '+') {
                            out += is_set ? arg : "";
                        } else if (op == '?') {
                            if (!is_set)
                                std::cerr << "winix: " << name << ": "
                                          << (arg.empty() ? "parameter null or not set" : arg) << "\n";
                            out += val;
                        } else {
                            out += val; // unknown op — treat as plain
                        }
                        continue;
                    }

                    // Plain ${VAR}
                    auto it = g_shell_vars.find(inner);
                    out += (it != g_shell_vars.end()) ? it->second : getenv_win(inner);
                    continue;
                }
            }
            // $VAR — shell var first, then env
            if (c == '$') {
                size_t j=i+1;
                while (j<line.size() &&
                       (std::isalnum((unsigned char)line[j]) ||
                        line[j]=='_'))
                    ++j;
                if (j > i+1) {
                    std::string name = line.substr(i+1, j-(i+1));
                    auto it = g_shell_vars.find(name);
                    out += (it != g_shell_vars.end()) ? it->second : getenv_win(name);
                    i=j-1;
                    continue;
                }
            }
        }
        out.push_back(c);
    }
    return out;
}

static std::vector<std::string> split_pipe(const std::string& s) {
    std::vector<std::string> segs;
    std::string cur;
    bool in_s = false, in_d = false;
    for (char c : s) {
        if (c == '\'' && !in_d) { in_s = !in_s; cur.push_back(c); continue; }
        if (c == '"'  && !in_s) { in_d = !in_d; cur.push_back(c); continue; }
        if (!in_s && !in_d && c == '|') { segs.push_back(trim(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    segs.push_back(trim(cur));
    return segs;
}

// --------------------------------------------------
// Ctrl+C handling
// --------------------------------------------------
// Console control handler — runs in a dedicated Windows thread.
// Returning TRUE suppresses the default behaviour (which would exit the shell).
// Children in the same console group receive the event automatically and exit
// via their own default handlers; we just need to keep the shell alive.
static BOOL WINAPI console_ctrl_handler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        // Ensure the next prompt starts on a fresh line after any partial output.
        HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD  w    = 0;
        WriteConsoleA(hout, "\n", 1, &w, nullptr);
        return TRUE;   // shell stays alive
    }
    return FALSE;      // let other events (CTRL_CLOSE, etc.) use the default
}

// --------------------------------------------------
// Background job tracking
// --------------------------------------------------
struct Job {
    int         id;
    HANDLE      hProcess;
    DWORD       pid;
    std::string cmd;
};
static std::vector<Job> g_jobs;
static int              g_next_jid = 1;

static void job_add(HANDLE hproc, DWORD pid, const std::string& cmd) {
    int id = g_next_jid++;
    g_jobs.push_back({id, hproc, pid, cmd});
    std::cout << "[" << id << "] " << pid << "\n";
}

static void jobs_reap_notify() {
    for (auto it = g_jobs.begin(); it != g_jobs.end(); ) {
        if (WaitForSingleObject(it->hProcess, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(it->hProcess, &code);
            CloseHandle(it->hProcess);
            std::cout << "\n[" << it->id << "]  Done\t\t" << it->cmd << "\n";
            it = g_jobs.erase(it);
        } else {
            ++it;
        }
    }
}

// --------------------------------------------------
// Process spawning
// --------------------------------------------------
// Spawn a process via cmd.exe /C (used for system PATH fallback).
static DWORD spawn_cmd(const std::string& command, bool wait,
                       HANDLE h_in  = INVALID_HANDLE_VALUE,
                       HANDLE h_out = INVALID_HANDLE_VALUE,
                       HANDLE h_err = INVALID_HANDLE_VALUE,
                       HANDLE* out_hproc = nullptr,
                       DWORD*  out_pid   = nullptr) {
    std::string full = "cmd.exe /C " + command;

    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput  = (h_in  != INVALID_HANDLE_VALUE) ? h_in  : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = (h_out != INVALID_HANDLE_VALUE) ? h_out : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = (h_err != INVALID_HANDLE_VALUE) ? h_err : GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(
        NULL, (LPSTR)full.c_str(),
        NULL, NULL, TRUE,
        0,          // same console group — Ctrl+C reaches the child
        NULL, NULL, &si, &pi
    );

    if (!ok) {
        DWORD e = GetLastError();
        std::cerr << "Error starting: " << command
                  << " (code " << e << ")\n";
        return e ? e : 1;
    }

    if (!wait) {
        CloseHandle(pi.hThread);
        if (out_hproc) { *out_hproc = pi.hProcess; if (out_pid) *out_pid = pi.dwProcessId; }
        else            CloseHandle(pi.hProcess);
        return 0;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

// Spawn a known executable directly (bypasses cmd.exe so arguments are
// never mangled by cmd's quoting rules — important for paths with spaces).
static DWORD spawn_direct(const std::string& exe_path,
                          const std::vector<std::string>& args, bool wait,
                          HANDLE h_in      = INVALID_HANDLE_VALUE,
                          HANDLE h_out     = INVALID_HANDLE_VALUE,
                          HANDLE h_err     = INVALID_HANDLE_VALUE,
                          HANDLE* out_hproc = nullptr,
                          DWORD*  out_pid   = nullptr) {
    // Build command line: "exe_path" arg1 arg2 ...
    std::string cmdline = "\"" + exe_path + "\"";
    for (auto& a : args) cmdline += " " + quote_arg(a);

    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput  = (h_in  != INVALID_HANDLE_VALUE) ? h_in  : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = (h_out != INVALID_HANDLE_VALUE) ? h_out : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = (h_err != INVALID_HANDLE_VALUE) ? h_err : GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(
        exe_path.c_str(), (LPSTR)cmdline.c_str(),
        NULL, NULL, TRUE,
        0,
        NULL, NULL, &si, &pi
    );

    if (!ok) {
        DWORD e = GetLastError();
        std::cerr << "Error starting: " << exe_path
                  << " (code " << e << ")\n";
        return e ? e : 1;
    }

    if (!wait) {
        CloseHandle(pi.hThread);
        if (out_hproc) { *out_hproc = pi.hProcess; if (out_pid) *out_pid = pi.dwProcessId; }
        else            CloseHandle(pi.hProcess);
        return 0;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static HANDLE make_inheritable(HANDLE h) {
    HANDLE dup = INVALID_HANDLE_VALUE;
    DuplicateHandle(GetCurrentProcess(), h,
                    GetCurrentProcess(), &dup,
                    0, TRUE, DUPLICATE_SAME_ACCESS);
    return dup;
}

static std::string resolve_exe(const std::string& cmd, const Paths& paths) {
    { fs::path p = fs::path(paths.coreutils_dir) / (cmd + ".exe");
      if (fs::exists(p)) return p.string(); }
    { fs::path p = fs::path(paths.bin_dir) / (cmd + ".exe");
      if (fs::exists(p)) return p.string(); }
    char buf[MAX_PATH] = {};
    if (SearchPathA(NULL, (cmd + ".exe").c_str(), NULL, MAX_PATH, buf, NULL) > 0)
        return std::string(buf);
    return {};
}

// --------------------------------------------------
// Redirection
// --------------------------------------------------
struct Redirects {
    std::string in_file;
    std::string out_file;
    std::string err_file;
    bool out_append = false;
};

// Strip >, >>, <, 2> operators from cmd and populate r.
// Returns the cleaned command string.
static std::string parse_redirects(const std::string& cmd, Redirects& r) {
    auto tokens = shell_tokens(cmd);
    std::vector<std::string> kept;

    for (size_t i = 0; i < tokens.size(); i++) {
        const auto& t = tokens[i];

        // Standalone operators with separate filename token
        if ((t == ">" || t == ">>" || t == "<" || t == "2>") && i+1 < tokens.size()) {
            std::string f = unquote(tokens[++i]);
            if (t == ">")   { r.out_file = f; r.out_append = false; }
            else if (t == ">>")  { r.out_file = f; r.out_append = true;  }
            else if (t == "<")   { r.in_file  = f; }
            else if (t == "2>")  { r.err_file = f; }
            continue;
        }
        // Attached: >file  >>file  2>file  <file
        if (t.size() > 1 && t[0] == '>' && t[1] == '>') {
            r.out_file = unquote(t.substr(2)); r.out_append = true;  continue;
        }
        if (t.size() > 1 && t[0] == '>') {
            r.out_file = unquote(t.substr(1)); r.out_append = false; continue;
        }
        if (t.size() > 1 && t[0] == '<') {
            r.in_file  = unquote(t.substr(1)); continue;
        }
        if (t.size() > 2 && t[0]=='2' && t[1]=='>') {
            r.err_file = unquote(t.substr(2)); continue;
        }
        kept.push_back(t);
    }

    std::string result;
    for (size_t i = 0; i < kept.size(); i++) {
        if (i) result += ' ';
        result += kept[i];
    }
    return result;
}

// Open a file handle for redirection; made inheritable for child processes.
static HANDLE open_redir(const std::string& path, bool write, bool append) {
    DWORD access    = write ? GENERIC_WRITE : GENERIC_READ;
    DWORD creation  = write ? (append ? OPEN_ALWAYS : CREATE_ALWAYS) : OPEN_EXISTING;
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE h = CreateFileA(path.c_str(), access, FILE_SHARE_READ|FILE_SHARE_WRITE,
                           &sa, creation, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE && write && append)
        SetFilePointer(h, 0, NULL, FILE_END);
    return h;
}

// --------------------------------------------------
// Command chaining  (;  &&  ||)
// --------------------------------------------------
enum class ChainOp { FIRST, ALWAYS, AND, OR };

struct ChainedCmd { std::string cmd; ChainOp op; };

static std::vector<ChainedCmd> split_chain(const std::string& s) {
    std::vector<ChainedCmd> result;
    std::string cur;
    bool in_s = false, in_d = false;
    ChainOp pending = ChainOp::FIRST;

    auto flush = [&]() {
        auto t = trim(cur);
        if (!t.empty()) result.push_back({t, pending});
        cur.clear();
    };

    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '\'' && !in_d) { in_s = !in_s; cur += c; continue; }
        if (c == '"'  && !in_s) { in_d = !in_d; cur += c; continue; }
        if (in_s || in_d)       { cur += c; continue; }

        // || — OR (must check before single |)
        if (c == '|' && i+1 < s.size() && s[i+1] == '|') {
            flush(); pending = ChainOp::OR; i++; continue;
        }
        // && — AND
        if (c == '&' && i+1 < s.size() && s[i+1] == '&') {
            flush(); pending = ChainOp::AND; i++; continue;
        }
        // ; — always
        if (c == ';') {
            flush(); pending = ChainOp::ALWAYS; continue;
        }
        cur += c;
    }
    flush();
    return result;
}

// --------------------------------------------------
// Resolve & run external commands
// --------------------------------------------------
static DWORD run_segment(const std::string& seg, const Paths& paths,
                         bool bg = false, HANDLE* out_hproc = nullptr, DWORD* out_pid = nullptr) {
    // Parse redirection operators out first
    Redirects redir;
    std::string clean = parse_redirects(seg, redir);

    auto t = glob_expand(shell_tokens(clean));
    if (t.empty()) return 0;

    // Open redirect handles (inheritable, so children can use them)
    HANDLE h_in  = INVALID_HANDLE_VALUE;
    HANDLE h_out = INVALID_HANDLE_VALUE;
    HANDLE h_err = INVALID_HANDLE_VALUE;

    if (!redir.in_file.empty()) {
        h_in = open_redir(redir.in_file, false, false);
        if (h_in == INVALID_HANDLE_VALUE) {
            std::cerr << "winix: cannot open '" << redir.in_file << "' for reading\n";
            return 1;
        }
    }
    if (!redir.out_file.empty()) {
        h_out = open_redir(redir.out_file, true, redir.out_append);
        if (h_out == INVALID_HANDLE_VALUE) {
            std::cerr << "winix: cannot open '" << redir.out_file << "' for writing\n";
            if (h_in != INVALID_HANDLE_VALUE) CloseHandle(h_in);
            return 1;
        }
    }
    if (!redir.err_file.empty()) {
        h_err = open_redir(redir.err_file, true, false);
        if (h_err == INVALID_HANDLE_VALUE) {
            std::cerr << "winix: cannot open '" << redir.err_file << "' for writing\n";
            if (h_in  != INVALID_HANDLE_VALUE) CloseHandle(h_in);
            if (h_out != INVALID_HANDLE_VALUE) CloseHandle(h_out);
            return 1;
        }
    }

    auto close_redirs = [&]() {
        if (h_in  != INVALID_HANDLE_VALUE) CloseHandle(h_in);
        if (h_out != INVALID_HANDLE_VALUE) CloseHandle(h_out);
        if (h_err != INVALID_HANDLE_VALUE) CloseHandle(h_err);
    };

    // handle cd (builtin — redirection is a no-op for it)
    if (to_lower(t[0]) == "cd") {
        close_redirs();
        if (t.size() == 1) {
            std::error_code ec;
            fs::current_path(user_home(), ec);
            if (ec) std::cerr << "cd: " << ec.message() << "\n";
            return 0;
        }
        std::string target = unquote(t[1]);
        std::error_code ec;
        fs::current_path(target, ec);
        if (ec)
            std::cerr << "cd: " << target << ": " << ec.message() << "\n";
        return 0;
    }

    const std::string cmd = t[0];
    std::vector<std::string> rest_args(t.begin() + 1, t.end());
    DWORD code = 0;

    // Check coreutils dir
    {
        fs::path p = fs::path(paths.coreutils_dir) / (cmd + ".exe");
        if (fs::exists(p)) {
            code = spawn_direct(p.string(), rest_args, !bg, h_in, h_out, h_err, out_hproc, out_pid);
            close_redirs();
            return code;
        }
    }

    // Check Winix bin dir
    {
        fs::path p = fs::path(paths.bin_dir) / (cmd + ".exe");
        if (fs::exists(p)) {
            code = spawn_direct(p.string(), rest_args, !bg, h_in, h_out, h_err, out_hproc, out_pid);
            close_redirs();
            return code;
        }
    }

    // Fallback: system PATH via cmd.exe
    code = spawn_cmd(clean, !bg, h_in, h_out, h_err, out_hproc, out_pid);
    close_redirs();
    return code;
}

static DWORD run_pipeline(const std::vector<std::string>& segs, const Paths& paths,
                          bool bg = false, HANDLE* out_hproc = nullptr, DWORD* out_pid = nullptr) {
    int n = (int)segs.size();

    struct Pipe { HANDLE read, write; };
    std::vector<Pipe> pipes(n - 1);

    // Create n-1 anonymous pipes with non-inheritable originals
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    for (int i = 0; i < n - 1; ++i) {
        if (!CreatePipe(&pipes[i].read, &pipes[i].write, &sa, 0)) {
            std::cerr << "pipeline: CreatePipe failed\n";
            // Close any already-opened pipes
            for (int j = 0; j < i; ++j) {
                CloseHandle(pipes[j].read);
                CloseHandle(pipes[j].write);
            }
            return 1;
        }
    }

    std::vector<HANDLE> procs;
    std::vector<DWORD>  pids;

    for (int i = 0; i < n; ++i) {
        // Strip >, <, 2> redirects from each segment (e.g. first seg may have < file)
        Redirects redir;
        std::string clean_seg = parse_redirects(segs[i], redir);
        auto t = glob_expand(shell_tokens(clean_seg));
        if (t.empty()) continue;

        HANDLE raw_in  = (i == 0)     ? GetStdHandle(STD_INPUT_HANDLE)  : pipes[i-1].read;
        HANDLE raw_out = (i == n - 1) ? GetStdHandle(STD_OUTPUT_HANDLE) : pipes[i].write;

        // Apply per-segment file redirects (< overrides pipe stdin, > overrides pipe stdout)
        HANDLE h_redir_in  = INVALID_HANDLE_VALUE;
        HANDLE h_redir_out = INVALID_HANDLE_VALUE;
        HANDLE h_redir_err = INVALID_HANDLE_VALUE;
        if (!redir.in_file.empty()) {
            h_redir_in = open_redir(redir.in_file, false, false);
            if (h_redir_in != INVALID_HANDLE_VALUE) raw_in = h_redir_in;
        }
        if (!redir.out_file.empty()) {
            h_redir_out = open_redir(redir.out_file, true, redir.out_append);
            if (h_redir_out != INVALID_HANDLE_VALUE) raw_out = h_redir_out;
        }
        if (!redir.err_file.empty()) {
            h_redir_err = open_redir(redir.err_file, true, false);
        }

        HANDLE h_in  = make_inheritable(raw_in);
        HANDLE h_out = make_inheritable(raw_out);

        std::string exe = resolve_exe(t[0], paths);
        std::string cmdline;
        const char* app = nullptr;

        if (!exe.empty()) {
            cmdline = "\"" + exe + "\"";
            for (size_t j = 1; j < t.size(); ++j) cmdline += " " + quote_arg(t[j]);
            app = exe.c_str();
        } else {
            cmdline = "cmd.exe /C " + clean_seg;
            app = nullptr;
        }

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput  = h_in;
        si.hStdOutput = h_out;
        si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

        PROCESS_INFORMATION pi{};
        BOOL ok = CreateProcessA(
            app, (LPSTR)cmdline.c_str(),
            NULL, NULL, TRUE, 0,
            NULL, NULL, &si, &pi
        );

        CloseHandle(h_in);
        CloseHandle(h_out);
        if (h_redir_in  != INVALID_HANDLE_VALUE) CloseHandle(h_redir_in);
        if (h_redir_out != INVALID_HANDLE_VALUE) CloseHandle(h_redir_out);
        if (h_redir_err != INVALID_HANDLE_VALUE) CloseHandle(h_redir_err);

        if (!ok) {
            DWORD e = GetLastError();
            std::cerr << "pipeline: failed to start '" << t[0]
                      << "' (code " << e << ")\n";
        } else {
            CloseHandle(pi.hThread);
            procs.push_back(pi.hProcess);
            pids.push_back(pi.dwProcessId);
        }
    }

    // Close all pipe ends in parent so children receive EOF
    for (auto& p : pipes) {
        CloseHandle(p.read);
        CloseHandle(p.write);
    }

    DWORD last_code = 0;
    for (size_t i = 0; i < procs.size(); ++i) {
        bool is_last = (i == procs.size() - 1);
        if (bg && is_last) {
            // Background: hand off last process handle to caller
            if (out_hproc) { *out_hproc = procs[i]; if (out_pid) *out_pid = pids[i]; }
            else            CloseHandle(procs[i]);
        } else {
            WaitForSingleObject(procs[i], INFINITE);
            if (is_last) GetExitCodeProcess(procs[i], &last_code);
            CloseHandle(procs[i]);
        }
    }

    return last_code;
}

// --------------------------------------------------
// Scripting support — data structures + block helpers
// --------------------------------------------------
struct ScriptState {
    bool do_break    = false;
    bool do_continue = false;
    bool do_return   = false;
    int  return_val  = 0;
};

// Returns net block-depth change for one line (for multi-line REPL buffering).
static int block_depth_change(const std::string& line) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') return 0;
    auto toks = shell_tokens(t);
    if (toks.empty()) return 0;
    const auto& kw = toks[0];
    if (kw == "if" || kw == "for" || kw == "while" || kw == "case") return 1;
    if (kw == "fi" || kw == "done" || kw == "esac")                 return -1;
    if (kw == "{")                                    return 1;
    if (kw == "}")                                    return -1;
    // "name() {" or "function foo {" — line ends with {
    if (t.back() == '{') return 1;
    return 0;
}

// Returns true if line is a function definition header; sets fname.
static bool is_func_def(const std::string& line, std::string& fname) {
    std::string t = trim(line);
    auto toks = shell_tokens(t);
    if (toks.empty()) return false;
    // "function name" or "function name {"
    if (toks[0] == "function" && toks.size() >= 2) { fname = toks[1]; return true; }
    // "name()" or "name() {"
    const std::string& f = toks[0];
    if (f.size() >= 2 && f.substr(f.size() - 2) == "()") {
        fname = f.substr(0, f.size() - 2);
        return true;
    }
    return false;
}

// Forward declarations (implementations follow handle_builtin)
static bool handle_builtin(const std::string&, Aliases&, const Paths&, History&, Config&);
static int  run_command_line(const std::string&, const Paths&, Aliases&, History*, Config*, int);
static int  script_exec_lines(const std::vector<std::string>&, const Paths&, Aliases&,
                               History*, Config*, int, ScriptState&);

// --------------------------------------------------
// Help
// --------------------------------------------------
static void print_help() {
    // Colors
    const char* GRN  = "\x1b[32m";   // section headers
    const char* CYN  = "\x1b[36m";   // command names
    const char* YLW  = "\x1b[33m";   // flags/hints
    const char* DIM  = "\x1b[2m";    // descriptions
    const char* RST  = "\x1b[0m";

    auto section = [&](const char* title) {
        std::cout << "\n" << GRN << "  " << title << RST << "\n";
    };

    struct Cmd { const char* name; const char* flags; const char* desc; };
    auto row = [&](const Cmd& c) {
        std::cout << "    " << CYN << std::left;
        // pad name to 10 chars
        std::string n(c.name);
        std::cout << n;
        for (int i = (int)n.size(); i < 10; ++i) std::cout << ' ';
        std::cout << RST << YLW;
        std::string f(c.flags);
        std::cout << f;
        for (int i = (int)f.size(); i < 18; ++i) std::cout << ' ';
        std::cout << RST << DIM << c.desc << RST << "\n";
    };

    std::cout << GRN
              << "╔══════════════════════════════════════════════════╗\n"
              << "║            Winix Shell — Command Reference        ║\n"
              << "╚══════════════════════════════════════════════════╝"
              << RST << "\n";

    section("SHELL BUILTINS");
    for (auto& c : (Cmd[]){
        {"cd",      "[dir]",           "change directory (no arg = home)"},
        {"alias",   "[name[=value]]",  "set or list aliases"},
        {"unalias", "<name>",          "remove an alias"},
        {"set",     "<NAME=VALUE>",    "set env var or shell option (case, PS1)"},
        {"history", "[-c]",            "show or clear command history"},
        {"exit",    "",                "quit the shell"},
        {"help",    "",                "show this reference card"},
    }) row(c);

    section("FILES & DIRECTORIES");
    for (auto& c : (Cmd[]){
        {"ls",      "[-alh]",          "list directory contents"},
        {"pwd",     "",                "print working directory"},
        {"cat",     "[-n] <file>",     "print file contents"},
        {"cp",      "[-r] <src> <dst>","copy file or directory"},
        {"mv",      "[-fv] <src> <dst>","move / rename"},
        {"rm",      "[-rf] <path>",    "remove file or directory"},
        {"mkdir",   "[-p] <dir>",      "create directory"},
        {"rmdir",   "<dir>",           "remove empty directory"},
        {"touch",   "<file>",          "create or update timestamp"},
        {"stat",    "<file>",          "show file metadata"},
        {"chmod",   "<mode> <file>",   "change file permissions"},
        {"chown",   "<owner> <file>",  "change file owner"},
        {"du",      "[-sh] [path]",    "disk usage"},
        {"df",      "[-h]",            "disk free space"},
    }) row(c);

    section("TEXT PROCESSING");
    for (auto& c : (Cmd[]){
        {"grep",    "[-i] <pat> [file]","search for pattern"},
        {"wc",      "[-lwc] [file]",   "count lines, words, chars"},
        {"sort",    "[-ruf] [file]",   "sort lines"},
        {"uniq",    "[-cd] [file]",    "filter duplicate lines"},
        {"head",    "[-n N] [file]",   "first N lines (default 10)"},
        {"tail",    "[-n N] [file]",   "last N lines (default 10)"},
        {"more",    "<file>",          "page through a file"},
        {"less",    "<file>",          "page through a file (scrollable)"},
        {"tee",     "<file>",          "read stdin, write to file + stdout"},
    }) row(c);

    section("SYSTEM & INFO");
    for (auto& c : (Cmd[]){
        {"ps",      "",                "list running processes"},
        {"kill",    "<pid>",           "terminate a process"},
        {"whoami",  "",                "print current username"},
        {"uname",   "[-a]",            "system information"},
        {"uptime",  "",                "system uptime"},
        {"date",    "",                "current date and time"},
        {"env",     "",                "print environment variables"},
        {"ver",     "",                "Winix version info"},
    }) row(c);

    section("UTILITIES");
    for (auto& c : (Cmd[]){
        {"echo",    "[-ne] <text>",    "print text"},
        {"printf",  "<fmt> [args]",    "formatted print"},
        {"sleep",   "<seconds>",       "pause for N seconds"},
        {"which",   "<cmd>",           "locate a command"},
        {"basename","<path>",          "filename portion of path"},
        {"dirname", "<path>",          "directory portion of path"},
        {"true",    "",                "exit 0"},
        {"false",   "",                "exit 1"},
    }) row(c);

    section("PIPING, CHAINING & REDIRECTION");
    std::cout << "    " << DIM
              << "cmd1 | cmd2        pipe output of cmd1 into cmd2\n"
              << "    cmd1 && cmd2       run cmd2 only if cmd1 succeeds\n"
              << "    cmd1 || cmd2       run cmd2 only if cmd1 fails\n"
              << "    cmd1 ; cmd2        run cmd2 regardless\n"
              << "    cmd > file         redirect stdout to file (overwrite)\n"
              << "    cmd >> file        redirect stdout to file (append)\n"
              << "    cmd < file         read stdin from file\n"
              << "    cmd 2> file        redirect stderr to file\n"
              << "    echo $?            last exit code\n"
              << "    cd ~/path          tilde expands to home directory\n"
              << RST;

    std::cout << "\n" << DIM
              << "  Tip: use Tab for completion, ↑/↓ for history\n"
              << RST << "\n";
}

// --------------------------------------------------
// Builtins
// --------------------------------------------------
static bool handle_builtin(
    const std::string& raw,
    Aliases& aliases,
    const Paths& paths,
    History& hist,
    Config& cfg
) {
    auto line = trim(raw);
    if (line.empty()) return true;

    std::string ll = to_lower(line);

    // Case-aware comparison helpers.
    // Builtins always match case-insensitively when case_sensitive=false.
    auto match  = [&](const std::string& s) {
        return cfg.case_sensitive ? (line == s) : (ll == s);
    };
    auto starts = [&](const std::string& s) {
        return cfg.case_sensitive
            ? (line.rfind(s, 0) == 0)
            : (ll.rfind(s, 0) == 0);
    };

    // set NAME=VALUE  (intercept shell toggles before env-var fallthrough)
    if (starts("set ")) {
        auto rest = trim(line.substr(4));
        auto eq = rest.find('=');
        if (eq == std::string::npos) {
            std::cerr << "Usage: set NAME=VALUE\n";
            return true;
        }
        auto name = trim(rest.substr(0, eq));
        auto val  = unquote(trim(rest.substr(eq+1)));

        // Shell toggle: case
        if (to_lower(name) == "case") {
            auto v = to_lower(val);
            if (v == "on" || v == "off") {
                cfg.case_sensitive = (v == "on");
                // Propagate to child processes via environment variable.
                setenv_win("WINIX_CASE", v);
                std::cout << "case sensitivity: " << v << "\n";
                save_rc(paths, cfg);
            } else {
                std::cerr << "set: case must be 'on' or 'off'\n";
            }
            return true;
        }

        // PS1 prompt string
        if (name == "PS1" || to_lower(name) == "ps1") {
            cfg.ps1 = val;
            save_rc(paths, cfg);
            return true;
        }

        // Otherwise set as an environment variable
        if (!setenv_win(name, val))
            std::cerr << "set: failed for " << name << "\n";
        return true;
    }

    // help
    if (match("help")) { print_help(); return true; }

    // history
    if (match("history"))    { hist.print(); return true; }
    if (match("history -c")) {
        hist.clear();
        hist.save(paths.history_file);
        return true;
    }

    // alias print all
    if (match("alias")) {
        for (auto& name : aliases.names()) {
            auto v = aliases.get(name);
            if (v)
                std::cout << "alias " << name << "=\""
                          << *v << "\"\n";
        }
        return true;
    }

    // unalias NAME
    if (starts("unalias ")) {
        auto name = trim(line.substr(8));
        if (!aliases.remove(name))
            std::cerr << "unalias: " << name << ": not found\n";
        else
            aliases.save(paths.aliases_file);
        return true;
    }

    // cls / clear
    if (match("cls") || match("clear")) {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(h, &csbi)) {
            DWORD cells = csbi.dwSize.X * csbi.dwSize.Y;
            COORD origin = {0, 0};
            DWORD written;
            FillConsoleOutputCharacterA(h, ' ', cells, origin, &written);
            FillConsoleOutputAttribute(h, csbi.wAttributes, cells, origin, &written);
            SetConsoleCursorPosition(h, origin);
        } else {
            std::cout << "\033[2J\033[H";
            std::cout.flush();
        }
        return true;
    }

    // vars — list shell-local variables
    if (match("vars")) {
        for (auto& kv : g_shell_vars)
            std::cout << kv.first << "=" << kv.second << "\n";
        return true;
    }

    // unset VAR — remove shell variable
    if (starts("unset ")) {
        auto name = trim(line.substr(6));
        g_shell_vars.erase(name);
        return true;
    }

    // jobs
    if (match("jobs")) {
        if (g_jobs.empty()) { std::cout << "No background jobs.\n"; return true; }
        for (auto& j : g_jobs) {
            bool done = (WaitForSingleObject(j.hProcess, 0) == WAIT_OBJECT_0);
            std::cout << "[" << j.id << "]  "
                      << (done ? "Done    " : "Running ") << "\t"
                      << j.cmd << "\n";
        }
        jobs_reap_notify();
        return true;
    }

    // fg [N]  — bring background job to foreground
    if (match("fg") || starts("fg ")) {
        if (g_jobs.empty()) { std::cerr << "fg: no current jobs\n"; return true; }
        int target_id = -1;
        if (starts("fg ")) {
            try { target_id = std::stoi(trim(line.substr(3))); } catch (...) {}
        }
        for (auto it = g_jobs.begin(); it != g_jobs.end(); ++it) {
            if (target_id < 0 || it->id == target_id) {
                std::cout << it->cmd << "\n";
                WaitForSingleObject(it->hProcess, INFINITE);
                CloseHandle(it->hProcess);
                g_jobs.erase(it);
                return true;
            }
        }
        std::cerr << "fg: no such job: " << target_id << "\n";
        return true;
    }

    // wait [%N | PID] — wait for background job(s)
    if (match("wait") || starts("wait ")) {
        if (starts("wait ")) {
            std::string arg = trim(line.substr(5));
            int target = -1;
            bool by_jid = (!arg.empty() && arg[0] == '%');
            try { target = std::stoi(by_jid ? arg.substr(1) : arg); } catch (...) {}
            for (auto it = g_jobs.begin(); it != g_jobs.end(); ++it) {
                bool hit = by_jid ? (it->id == target) : ((int)it->pid == target);
                if (hit) {
                    WaitForSingleObject(it->hProcess, INFINITE);
                    CloseHandle(it->hProcess);
                    it = g_jobs.erase(it);
                    return true;
                }
            }
            std::cerr << "wait: no such job: " << arg << "\n";
        } else {
            // wait with no args — wait for all background jobs
            for (auto& j : g_jobs) {
                WaitForSingleObject(j.hProcess, INFINITE);
                CloseHandle(j.hProcess);
            }
            g_jobs.clear();
        }
        return true;
    }

    // source / . — execute a shell script file
    if (starts("source ") || (line.size() >= 2 && line[0] == '.' && line[1] == ' ')) {
        std::string arg = trim(line.substr(starts("source ") ? 7 : 2));
        arg = unquote(arg);
        std::ifstream fin(arg);
        if (!fin) {
            std::cerr << "source: " << arg << ": No such file or directory\n";
            return true;
        }
        std::vector<std::string> slines;
        std::string sl;
        while (std::getline(fin, sl)) {
            if (!sl.empty() && sl.back() == '\r') sl.pop_back();
            slines.push_back(sl);
        }
        ScriptState ss;
        script_exec_lines(slines, paths, aliases, &hist, &cfg, 0, ss);
        return true;
    }

    // read [-p "prompt"] VAR
    if (starts("read ") || match("read")) {
        std::string rest = trim(line.size() > 4 ? line.substr(4) : "");
        std::string prompt_str;
        if (rest.rfind("-p ", 0) == 0) {
            rest = trim(rest.substr(3));
            if (!rest.empty() && (rest[0] == '"' || rest[0] == '\'')) {
                char q = rest[0];
                size_t end = rest.find(q, 1);
                if (end != std::string::npos) {
                    prompt_str = rest.substr(1, end - 1);
                    rest = trim(rest.substr(end + 1));
                }
            }
        }
        std::string var_name = trim(rest);
        if (!prompt_str.empty()) { std::cout << prompt_str; std::cout.flush(); }
        std::string val;
        if (std::getline(std::cin, val)) {
            if (!val.empty() && val.back() == '\r') val.pop_back();
        }
        if (!var_name.empty()) g_shell_vars[var_name] = val;
        return true;
    }

    // local VAR  /  local VAR=value  (only meaningful inside a function)
    if (starts("local ") || match("local")) {
        if (g_local_stack.empty()) return true; // no-op outside functions
        std::string rest = trim(line.size() > 5 ? line.substr(5) : "");
        auto& frame = g_local_stack.back();
        size_t eq = rest.find('=');
        std::string name = trim(eq != std::string::npos ? rest.substr(0, eq) : rest);
        if (name.empty()) return true;
        // Save the outer value once per variable per frame
        if (frame.saved.find(name) == frame.saved.end()) {
            auto it = g_shell_vars.find(name);
            frame.saved[name] = (it != g_shell_vars.end())
                ? std::optional<std::string>(it->second)
                : std::nullopt;
        }
        // Assign value if provided; otherwise leave current value (making it local)
        if (eq != std::string::npos)
            g_shell_vars[name] = expand_vars(rest.substr(eq + 1), 0);
        return true;
    }

    // alias name="value"
    if (starts("alias ")) {
        auto spec = trim(line.substr(6));
        auto eq = spec.find('=');
        if (eq == std::string::npos) {
            // No '=' — treat as a lookup for a single alias.
            auto val = aliases.get(spec);
            if (val)
                std::cout << "alias " << spec << "=\"" << *val << "\"\n";
            else
                std::cerr << "alias: " << spec << ": not found\n";
            return true;
        }

        auto name = unquote(trim(spec.substr(0, eq)));
        auto val  = unquote(trim(spec.substr(eq+1)));

        aliases.set(name, val);
        aliases.save(paths.aliases_file);
        return true;
    }

    return false;
}

// --------------------------------------------------
// run_command_line — execute one (already-expanded) command line
// --------------------------------------------------
static int run_command_line(const std::string& raw, const Paths& paths,
                             Aliases& aliases, History* hist, Config* cfg,
                             int last_exit) {
    std::string line = trim(raw);
    if (line.empty()) return last_exit;

    // VAR=value assignment
    {
        size_t eq = line.find('=');
        if (eq != std::string::npos && eq > 0) {
            std::string name = line.substr(0, eq);
            bool valid = !std::isdigit((unsigned char)name[0]);
            for (char ch : name)
                if (!std::isalnum((unsigned char)ch) && ch != '_') { valid = false; break; }
            if (valid && name.find(' ') == std::string::npos) {
                g_shell_vars[name] = line.substr(eq + 1);
                return 0;
            }
        }
    }

    // source / .
    {
        auto toks = shell_tokens(line);
        if (!toks.empty() && (toks[0] == "source" || toks[0] == ".") && toks.size() >= 2) {
            std::string path_arg = unquote(toks[1]);
            std::ifstream fin(path_arg);
            if (!fin) {
                std::cerr << "source: " << path_arg << ": No such file or directory\n";
                return 1;
            }
            std::vector<std::string> slines;
            std::string sl;
            while (std::getline(fin, sl)) {
                if (!sl.empty() && sl.back() == '\r') sl.pop_back();
                slines.push_back(sl);
            }
            ScriptState ss;
            return script_exec_lines(slines, paths, aliases, hist, cfg, last_exit, ss);
        }

        // User-defined function call
        if (!toks.empty() && g_functions.count(toks[0])) {
            auto old_pos = g_positional;
            g_positional.assign(toks.begin() + 1, toks.end());
            g_local_stack.push_back({});   // push new local-variable frame
            ScriptState ss;
            int rc = script_exec_lines(g_functions[toks[0]], paths, aliases,
                                        hist, cfg, last_exit, ss);
            if (ss.do_return) rc = ss.return_val;
            // Restore all variables that were declared local in this frame
            for (auto& [name, saved] : g_local_stack.back().saved) {
                if (saved.has_value()) g_shell_vars[name] = *saved;
                else                   g_shell_vars.erase(name);
            }
            g_local_stack.pop_back();
            g_positional = old_pos;
            return rc;
        }
    }

    // Builtins
    static History  s_dummy_hist;
    static Config   s_dummy_cfg;
    History& href = hist ? *hist : s_dummy_hist;
    Config&  cref = cfg  ? *cfg  : s_dummy_cfg;
    if (handle_builtin(line, aliases, paths, href, cref))
        return 0;

    // Script invocation: ./foo.sh, ./foo, ../foo.sh, or name.sh searched in PATH
    {
        auto toks = shell_tokens(line);
        if (!toks.empty()) {
            std::string cmd0 = toks[0];
            bool is_relative = (cmd0.rfind("./", 0) == 0 || cmd0.rfind("../", 0) == 0);
            bool is_absolute = (!cmd0.empty() && (cmd0[0] == '/' || cmd0[0] == '\\' ||
                                (cmd0.size() > 1 && cmd0[1] == ':')));
            bool has_sh_ext  = (cmd0.size() > 3 &&
                                to_lower(cmd0.substr(cmd0.size() - 3)) == ".sh");

            std::string script_path;

            if (is_relative || is_absolute) {
                // Explicit path — try as-is, then with .sh appended
                if (fs::exists(cmd0))
                    script_path = cmd0;
                else if (!has_sh_ext && fs::exists(cmd0 + ".sh"))
                    script_path = cmd0 + ".sh";
            } else if (has_sh_ext) {
                // name.sh without ./ — search PATH dirs (not cwd, matching Linux)
                char buf[MAX_PATH] = {};
                if (SearchPathA(NULL, cmd0.c_str(), NULL, MAX_PATH, buf, NULL) > 0)
                    script_path = buf;
            }

            if (!script_path.empty()) {
                std::ifstream fin(script_path);
                if (fin) {
                    std::vector<std::string> slines;
                    std::string sl;
                    std::string interp_line;
                    bool first = true;
                    while (std::getline(fin, sl)) {
                        if (!sl.empty() && sl.back() == '\r') sl.pop_back();
                        if (first && sl.rfind("#!", 0) == 0) {
                            interp_line = trim(sl.substr(2));
                            first = false;
                            continue;
                        }
                        first = false;
                        slines.push_back(sl);
                    }

                    // Parse shebang to determine interpreter
                    bool use_winix = true;
                    std::string ext_interp;
                    if (!interp_line.empty()) {
                        std::istringstream iss(interp_line);
                        std::string iexe_tok, iarg_tok;
                        iss >> iexe_tok;
                        iss >> iarg_tok;
                        std::string iname = fs::path(iexe_tok).filename().string();
                        // Strip .exe suffix for comparison
                        if (iname.size() > 4 && to_lower(iname.substr(iname.size()-4)) == ".exe")
                            iname = iname.substr(0, iname.size() - 4);
                        // Handle "env <interp>" form
                        if (iname == "env" && !iarg_tok.empty()) {
                            iname = fs::path(iarg_tok).filename().string();
                            if (iname.size() > 4 && to_lower(iname.substr(iname.size()-4)) == ".exe")
                                iname = iname.substr(0, iname.size() - 4);
                            iexe_tok = iarg_tok;
                        }
                        if (iname == "winix" || iname == "sh" || iname == "bash" || iname == "dash") {
                            use_winix = true;
                        } else {
                            use_winix = false;
                            ext_interp = iexe_tok;
                        }
                    }

                    if (use_winix) {
                        auto old_pos = g_positional;
                        g_positional.assign(toks.begin() + 1, toks.end());
                        ScriptState ss;
                        int rc = script_exec_lines(slines, paths, aliases, hist, cfg, last_exit, ss);
                        if (ss.do_return) rc = ss.return_val;
                        g_positional = old_pos;
                        return rc;
                    } else {
                        // Find the interpreter executable in PATH
                        std::string iexe = resolve_exe(fs::path(ext_interp).filename().string(), paths);
                        if (iexe.empty()) {
                            std::string fname = fs::path(ext_interp).filename().string();
                            char buf[MAX_PATH] = {};
                            if (SearchPathA(NULL, fname.c_str(), NULL, MAX_PATH, buf, NULL) > 0)
                                iexe = buf;
                        }
                        if (iexe.empty()) {
                            std::cerr << "winix: " << ext_interp << ": interpreter not found\n";
                            return 127;
                        }
                        std::vector<std::string> iargs = {script_path};
                        for (size_t i = 1; i < toks.size(); i++) iargs.push_back(toks[i]);
                        return (int)spawn_direct(iexe, iargs, true);
                    }
                }
            }
        }
    }

    // Chain splitting (;, &&, ||) then exec
    auto chain = split_chain(line);
    int rc = last_exit;
    for (auto& cc : chain) {
        bool run = false;
        switch (cc.op) {
            case ChainOp::FIRST:
            case ChainOp::ALWAYS: run = true;       break;
            case ChainOp::AND:    run = (rc == 0);  break;
            case ChainOp::OR:     run = (rc != 0);  break;
        }
        if (!run) continue;

        std::string cmd_str = trim(cc.cmd);
        bool bg = false;
        if (!cmd_str.empty() && cmd_str.back() == '&') {
            bg = true;
            cmd_str = trim(cmd_str.substr(0, cmd_str.size() - 1));
        }

        HANDLE bg_hproc = INVALID_HANDLE_VALUE;
        DWORD  bg_pid   = 0;
        auto segs = split_pipe(cmd_str);
        if (segs.size() > 1)
            rc = (int)run_pipeline(segs, paths, bg, &bg_hproc, &bg_pid);
        else
            rc = (int)run_segment(cmd_str, paths, bg, &bg_hproc, &bg_pid);
        if (bg && bg_hproc != INVALID_HANDLE_VALUE)
            job_add(bg_hproc, bg_pid, cmd_str);
    }
    return rc;
}

// --------------------------------------------------
// script_exec_lines — interpret a vector of script lines
// --------------------------------------------------

struct Branch { std::string cond; std::vector<std::string> body; };

// Wildcard pattern matching for case/esac (supports * and ?)
static bool case_match(const char* w, const char* p) {
    if (*p == '\0') return *w == '\0';
    if (*p == '*') { do { if (case_match(w, p+1)) return true; } while (*w++ != '\0'); return false; }
    if (*p == '?' && *w != '\0') return case_match(w+1, p+1);
    if (*p == *w)  return case_match(w+1, p+1);
    return false;
}

// --------------------------------------------------
// Here-doc detection
// --------------------------------------------------
// pos     = position of "<<" in the line
// end_pos = position just past the full "<<DELIM" token (so suffix can be appended)
// delim   = the delimiter word
// expand  = whether to expand $VAR in body (false for <<'DELIM')
struct HereDoc { size_t pos; size_t end_pos; std::string delim; bool expand; };

// Scan a command line for <<WORD or <<'WORD'.
// Returns HereDoc with pos=npos if none found.
static HereDoc detect_heredoc(const std::string& line) {
    bool in_s = false, in_d = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '\'' && !in_d) { in_s = !in_s; continue; }
        if (c == '"'  && !in_s) { in_d = !in_d; continue; }
        if (in_s || in_d) continue;
        if (c != '<' || i+1 >= line.size() || line[i+1] != '<') continue;
        if (i+2 < line.size() && line[i+2] == '<') continue; // skip <<<
        // Found <<
        std::string rest = trim(line.substr(i + 2));
        bool expand = true;
        std::string delim;
        size_t delim_start = i + 2; // position in line where delimiter token starts
        if (!rest.empty() && (rest[0] == '\'' || rest[0] == '"')) {
            char q = rest[0];
            size_t end = rest.find(q, 1);
            if (end != std::string::npos) {
                delim    = rest.substr(1, end - 1);
                expand   = (q == '"');
                // +1 opening quote, +end+1 closing quote
                delim_start += 1 + end + 1;
            }
        } else {
            size_t sp = rest.find_first_of(" \t");
            delim  = (sp != std::string::npos) ? rest.substr(0, sp) : rest;
            expand = true;
            delim_start += delim.size();
        }
        if (!delim.empty()) return {i, delim_start, delim, expand};
    }
    return {std::string::npos, 0, "", true};
}

// Collect lines from lines[start] until block depth returns to 0.
// Returns index of the closing-keyword line (not added to body).
static size_t collect_until_closed(const std::vector<std::string>& lines,
                                    size_t start, std::vector<std::string>& body) {
    int depth = 1;
    size_t i = start;
    while (i < lines.size()) {
        int delta = block_depth_change(lines[i]);
        depth += delta;
        if (depth <= 0) return i;
        body.push_back(lines[i]);
        i++;
    }
    return i;
}

// Parse an if block starting at lines[start] (first line AFTER the "if" line).
// init_cond = condition extracted from the if line.
// Returns index of the "fi" line.
static size_t parse_if_block(const std::vector<std::string>& lines, size_t start,
                               const std::string& init_cond,
                               std::vector<Branch>& branches) {
    branches.push_back({init_cond, {}});
    int depth = 1;
    size_t i = start;
    while (i < lines.size()) {
        std::string l = trim(lines[i]);
        auto toks = shell_tokens(l);
        if (depth == 1 && !toks.empty()) {
            if (toks[0] == "fi")   return i;
            if (toks[0] == "then") { i++; continue; }  // lone "then" line
            if (toks[0] == "else") {
                branches.push_back({"", {}});
                i++;
                continue;
            }
            if (toks[0] == "elif") {
                std::string cond = trim(l.substr(4));
                size_t tp = cond.rfind(" then");
                if (tp != std::string::npos) cond = trim(cond.substr(0, tp));
                size_t sc = cond.rfind(';');
                if (sc != std::string::npos) cond = trim(cond.substr(0, sc));
                branches.push_back({cond, {}});
                i++;
                continue;
            }
        }
        int delta = block_depth_change(lines[i]);
        depth += delta;
        branches.back().body.push_back(lines[i]);
        i++;
    }
    return i;
}

static int script_exec_lines(const std::vector<std::string>& lines,
                               const Paths& paths, Aliases& aliases,
                               History* hist, Config* cfg,
                               int last_exit, ScriptState& ss) {
    size_t i = 0;
    while (i < lines.size()) {
        if (ss.do_break || ss.do_continue || ss.do_return) break;

        std::string l = trim(lines[i]);
        if (l.empty() || l[0] == '#') { i++; continue; }

        auto toks = shell_tokens(l);
        if (toks.empty()) { i++; continue; }

        // Flow-control keywords
        if (toks[0] == "break")    { ss.do_break = true;  return last_exit; }
        if (toks[0] == "continue") { ss.do_continue = true; return last_exit; }
        if (toks[0] == "return") {
            ss.do_return  = true;
            ss.return_val = (toks.size() > 1) ? std::stoi(toks[1]) : last_exit;
            return ss.return_val;
        }
        if (toks[0] == "exit") {
            int code = (toks.size() > 1) ? std::stoi(toks[1]) : 0;
            std::exit(code);
        }

        // if block
        if (toks[0] == "if") {
            // Extract condition from "if COND; then" or "if COND"
            std::string cond = trim(l.substr(2));
            size_t tp = cond.rfind(" then");
            if (tp != std::string::npos) cond = trim(cond.substr(0, tp));
            size_t sc = cond.rfind(';');
            if (sc != std::string::npos) cond = trim(cond.substr(0, sc));

            size_t body_start = i + 1;
            if (body_start < lines.size() && trim(lines[body_start]) == "then")
                body_start++;

            std::vector<Branch> branches;
            size_t fi_idx = parse_if_block(lines, body_start, cond, branches);

            bool executed = false;
            for (auto& br : branches) {
                if (executed) break;
                if (br.cond.empty()) {
                    last_exit = script_exec_lines(br.body, paths, aliases,
                                                   hist, cfg, last_exit, ss);
                    executed = true;
                } else {
                    std::string ec = expand_vars(expand_aliases_once(br.cond, aliases),
                                                  last_exit);
                    int rc = run_command_line(ec, paths, aliases, hist, cfg, last_exit);
                    if (rc == 0) {
                        last_exit = script_exec_lines(br.body, paths, aliases,
                                                       hist, cfg, last_exit, ss);
                        executed = true;
                    }
                }
            }
            i = (fi_idx < lines.size()) ? fi_idx + 1 : lines.size();
            continue;
        }

        // for loop: "for VAR in items...; do"
        if (toks[0] == "for") {
            std::string var = (toks.size() >= 2) ? toks[1] : "";
            std::vector<std::string> items;
            bool in_list = false;
            for (size_t j = 2; j < toks.size(); ++j) {
                if (toks[j] == "in")             { in_list = true; continue; }
                if (toks[j] == "do" || toks[j] == ";") continue;
                if (in_list) items.push_back(expand_vars(toks[j], last_exit));
            }

            size_t body_start = i + 1;
            if (body_start < lines.size() && trim(lines[body_start]) == "do")
                body_start++;

            std::vector<std::string> body;
            size_t done_idx = collect_until_closed(lines, body_start, body);

            for (auto& item : items) {
                g_shell_vars[var] = item;
                ScriptState inner;
                last_exit = script_exec_lines(body, paths, aliases, hist, cfg, last_exit, inner);
                if (inner.do_break) break;
                if (inner.do_return) { ss = inner; break; }
            }
            i = (done_idx < lines.size()) ? done_idx + 1 : lines.size();
            continue;
        }

        // while loop: "while COND; do"
        if (toks[0] == "while") {
            std::string cond = trim(l.substr(5));
            size_t sc = cond.rfind(';');
            if (sc != std::string::npos) cond = trim(cond.substr(0, sc));
            if (cond.size() >= 3 && cond.substr(cond.size() - 3) == " do")
                cond = trim(cond.substr(0, cond.size() - 3));

            size_t body_start = i + 1;
            if (body_start < lines.size() && trim(lines[body_start]) == "do")
                body_start++;

            std::vector<std::string> body;
            size_t done_idx = collect_until_closed(lines, body_start, body);

            for (;;) {
                std::string ec = expand_vars(expand_aliases_once(cond, aliases), last_exit);
                int rc = run_command_line(ec, paths, aliases, hist, cfg, last_exit);
                if (rc != 0) break;
                ScriptState inner;
                last_exit = script_exec_lines(body, paths, aliases, hist, cfg, last_exit, inner);
                if (inner.do_break) break;
                if (inner.do_return) { ss = inner; break; }
            }
            i = (done_idx < lines.size()) ? done_idx + 1 : lines.size();
            continue;
        }

        // case WORD in ... esac
        if (toks[0] == "case") {
            // Extract WORD (tokens between "case" and "in")
            std::string word;
            for (size_t j = 1; j < toks.size(); ++j) {
                if (toks[j] == "in") break;
                if (!word.empty()) word += ' ';
                word += expand_vars(toks[j], last_exit);
            }

            // Collect lines until matching "esac"
            size_t j = i + 1;
            std::vector<std::string> case_lines;
            int depth = 1;
            while (j < lines.size()) {
                auto lt = shell_tokens(trim(lines[j]));
                if (!lt.empty()) {
                    if (lt[0] == "case") depth++;
                    else if (lt[0] == "esac") { --depth; if (depth == 0) break; }
                }
                case_lines.push_back(lines[j]);
                j++;
            }
            size_t esac_idx = j;

            // Execute matching arm
            bool matched = false;
            size_t k = 0;
            while (k < case_lines.size()) {
                std::string cl = trim(case_lines[k]);
                if (cl.empty() || cl == "in" || cl == ";;") { k++; continue; }

                // A pattern line contains ')' — find its position
                size_t paren = cl.find(')');
                if (paren == std::string::npos) { k++; continue; }

                std::string pat_str    = trim(cl.substr(0, paren));
                std::string inline_body = trim(cl.substr(paren + 1));
                bool inline_done = false;
                if (inline_body.size() >= 2 &&
                    inline_body.substr(inline_body.size() - 2) == ";;") {
                    inline_body = trim(inline_body.substr(0, inline_body.size() - 2));
                    inline_done = true;
                }
                k++;

                // Check if word matches any "|"-separated pattern
                bool this_matched = false;
                if (!matched) {
                    size_t ps = 0;
                    while (ps <= pat_str.size()) {
                        size_t pe = pat_str.find('|', ps);
                        if (pe == std::string::npos) pe = pat_str.size();
                        std::string pat = trim(pat_str.substr(ps, pe - ps));
                        if (case_match(word.c_str(), pat.c_str())) { this_matched = true; break; }
                        ps = pe + 1;
                    }
                }

                // Collect multi-line body until ";;"
                std::vector<std::string> arm_body;
                if (!inline_body.empty()) arm_body.push_back(inline_body);
                if (!inline_done) {
                    while (k < case_lines.size()) {
                        std::string bl = trim(case_lines[k]);
                        if (bl == ";;") { k++; break; }
                        if (bl.size() >= 2 && bl.substr(bl.size()-2) == ";;") {
                            std::string before = trim(bl.substr(0, bl.size()-2));
                            if (!before.empty()) arm_body.push_back(before);
                            k++; break;
                        }
                        arm_body.push_back(case_lines[k]);
                        k++;
                    }
                }

                if (this_matched) {
                    last_exit = script_exec_lines(arm_body, paths, aliases, hist, cfg, last_exit, ss);
                    matched = true;
                }
            }
            i = (esac_idx < lines.size()) ? esac_idx + 1 : lines.size();
            continue;
        }

        // Function definition: "name() {" or "function name {"
        {
            std::string fname;
            if (is_func_def(l, fname)) {
                size_t body_start = i + 1;
                // Skip standalone "{" line if function header doesn't end with "{"
                if (l.back() != '{') {
                    if (body_start < lines.size() && trim(lines[body_start]) == "{")
                        body_start++;
                }
                // Collect body lines until matching "}"
                std::vector<std::string> fbody;
                size_t end_idx = body_start;
                while (end_idx < lines.size() && trim(lines[end_idx]) != "}")
                    fbody.push_back(lines[end_idx++]);
                g_functions[fname] = fbody;
                i = (end_idx < lines.size()) ? end_idx + 1 : lines.size();
                continue;
            }
        }

        // Skip lone control keywords (then/do/else/fi/done/esac handled above)
        if (toks[0] == "then" || toks[0] == "do"   || toks[0] == "else" ||
            toks[0] == "fi"   || toks[0] == "done"  || toks[0] == "esac" ||
            toks[0] == "in"   || toks[0] == ";;") {
            i++;
            continue;
        }

        // Here-doc: "cmd <<DELIM ... DELIM"
        {
            HereDoc hd = detect_heredoc(l);
            if (hd.pos != std::string::npos) {
                // Collect body lines until the delimiter appears alone on a line
                std::string content;
                size_t j = i + 1;
                while (j < lines.size()) {
                    std::string bl = lines[j];
                    if (!bl.empty() && bl.back() == '\r') bl.pop_back();
                    if (trim(bl) == hd.delim) { j++; break; }
                    content += (hd.expand ? expand_vars(bl, last_exit) : bl) + "\n";
                    j++;
                }
                i = j; // resume after delimiter line (no extra i++ below)

                // Write body to a temp file and redirect stdin from it
                char tmpdir[MAX_PATH], tmpfile[MAX_PATH];
                GetTempPathA(MAX_PATH, tmpdir);
                GetTempFileNameA(tmpdir, "hd", 0, tmpfile);
                if (FILE* f = fopen(tmpfile, "wb")) {
                    fwrite(content.c_str(), 1, content.size(), f);
                    fclose(f);
                }

                // Replace "<<DELIM" with "< tmpfile", keeping any suffix (e.g. "| grep x")
                std::string suffix = (hd.end_pos < l.size()) ? l.substr(hd.end_pos) : "";
                std::string cmd_part = trim(l.substr(0, hd.pos)) + " < " + tmpfile + suffix;
                std::string ex = expand_vars(expand_aliases_once(cmd_part, aliases), last_exit);
                last_exit = run_command_line(ex, paths, aliases, hist, cfg, last_exit);
                DeleteFileA(tmpfile);
                continue;
            }
        }

        // Regular command
        std::string expanded = expand_vars(expand_aliases_once(l, aliases), last_exit);
        last_exit = run_command_line(expanded, paths, aliases, hist, cfg, last_exit);
        i++;
    }
    return last_exit;
}

// --------------------------------------------------
// Prompt — PS1 expansion
// --------------------------------------------------
static std::string ps1_username() {
    char buf[256] = {};
    DWORD n = sizeof(buf);
    GetUserNameA(buf, &n);
    return std::string(buf);
}

static std::string ps1_hostname() {
    char buf[256] = {};
    DWORD n = sizeof(buf);
    GetComputerNameA(buf, &n);
    return std::string(buf);
}

static std::string ps1_cwd() {
    try {
        std::string cwd  = fs::current_path().string();
        std::string home = user_home();
        // Normalise to forward slashes
        std::replace(cwd.begin(),  cwd.end(),  '\\', '/');
        std::replace(home.begin(), home.end(), '\\', '/');
        if (!home.empty() && cwd.rfind(home, 0) == 0)
            return "~" + cwd.substr(home.size());
        return cwd;
    } catch (...) { return "?"; }
}

static std::string ps1_cwd_base() {
    try {
        auto name = fs::current_path().filename().string();
        return name.empty() ? "/" : name;
    } catch (...) { return "?"; }
}

static std::string ps1_time() {
    time_t t = time(nullptr);
    struct tm* tm = localtime(&t);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    return std::string(buf);
}

static std::string ps1_date() {
    time_t t = time(nullptr);
    struct tm* tm = localtime(&t);
    char buf[16];
    strftime(buf, sizeof(buf), "%a %b %d", tm);
    return std::string(buf);
}

// Expand a PS1 format string into the rendered prompt.
// Supported sequences:
//   \u  username        \h  hostname
//   \w  cwd (with ~)    \W  cwd basename
//   \$  $ (always)      \n  newline         \t  HH:MM:SS
//   \d  "Mon Jan 01"    \e  ESC             \\ backslash
//   \[  begin non-printing (stripped)       \]  end non-printing (stripped)
static std::string expand_ps1(const std::string& ps1) {
    std::string out;
    for (size_t i = 0; i < ps1.size(); ++i) {
        if (ps1[i] != '\\' || i + 1 >= ps1.size()) {
            out += ps1[i];
            continue;
        }
        ++i;
        switch (ps1[i]) {
            case 'u':  out += ps1_username();  break;
            case 'h':  out += ps1_hostname();  break;
            case 'w':  out += ps1_cwd();       break;
            case 'W':  out += ps1_cwd_base();  break;
            case '$':  out += '$';             break;
            case 'n':  out += '\n';            break;
            case 't':  out += ps1_time();      break;
            case 'd':  out += ps1_date();      break;
            case 'e':  out += '\x1b';          break;
            case '[':  /* strip — non-printing start */ break;
            case ']':  /* strip — non-printing end   */ break;
            case '\\': out += '\\';            break;
            default:   out += '\\'; out += ps1[i]; break;
        }
    }
    return out;
}

static std::string prompt(const Config& cfg) {
    return expand_ps1(cfg.ps1);
}

// --------------------------------------------------
// Main
// --------------------------------------------------
int main(int argc, char* argv[]) {
#ifdef _WIN32
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD outMode = 0;
    if (GetConsoleMode(hOut, &outMode)) {
        outMode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
        SetConsoleMode(hOut, outMode);
    }

    // DO NOT enable ENABLE_VIRTUAL_TERMINAL_INPUT
    // It destroys arrow keys in standard CMD.exe.
}
#endif


    SetConsoleOutputCP(CP_UTF8);
    std::ios::sync_with_stdio(false);

    // -C <dir>: start shell in specified directory (used by "Open Winix here")
    int argi = 1;
    if (argc >= 3 && std::string(argv[1]) == "-C") {
        if (!SetCurrentDirectoryA(argv[2])) {
            std::cerr << "winix: -C: " << argv[2] << ": no such directory\n";
            return 1;
        }
        argi = 3;
    }

    // Script file execution: winix [-C dir] script.sh [arg1 arg2 ...]
    if (argi < argc) {
        std::ifstream fin(argv[argi]);
        if (!fin) {
            std::cerr << "winix: " << argv[argi] << ": No such file or directory\n";
            return 1;
        }
        for (int k = argi + 1; k < argc; ++k)
            g_positional.push_back(argv[k]);
        std::vector<std::string> slines;
        std::string sl;
        while (std::getline(fin, sl)) {
            if (!sl.empty() && sl.back() == '\r') sl.pop_back();
            if (slines.empty() && sl.rfind("#!", 0) == 0) continue; // skip shebang
            slines.push_back(sl);
        }
        Config cfg2;
        auto paths2 = make_paths();
        load_rc(paths2, cfg2);
        setenv_win("WINIX_CASE", cfg2.case_sensitive ? "on" : "off");
        Aliases aliases2;
        aliases2.load(paths2.aliases_file);
        ScriptState ss;
        return script_exec_lines(slines, paths2, aliases2, nullptr, &cfg2, 0, ss);
    }

    // Intercept Ctrl+C / Ctrl+Break: kill the foreground child (which is in
    // the same console group and receives the event automatically), but keep
    // the shell itself alive.
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    std::cout << "Winix Shell — Stable Edition\n";

    Config cfg;
    auto paths = make_paths();
    load_rc(paths, cfg);

    // Export case setting so coreutils inherit the correct default.
    setenv_win("WINIX_CASE", cfg.case_sensitive ? "on" : "off");

    History hist;
    hist.max_entries = cfg.history_max;
    hist.load(paths.history_file);

    Aliases aliases;
    aliases.load(paths.aliases_file);

    LineEditor editor(
        [&](const std::string& partial){ return completion_matches(partial, aliases); },
        &hist.entries
    );

    int last_exit = 0;

    while (true) {
        jobs_reap_notify();
        auto in = editor.read_line(prompt(cfg));
        if (!in.has_value()) break;

        auto original = trim(*in);
        if (original.empty()) continue;

        auto ll = to_lower(original);
        if (ll == "exit" || ll == "quit") break;

        // Here-doc: collect body lines until the delimiter
        {
            HereDoc hd = detect_heredoc(original);
            if (hd.pos != std::string::npos) {
                std::vector<std::string> hd_lines;
                hd_lines.push_back(original);
                for (;;) {
                    auto cont = editor.read_line("> ");
                    if (!cont.has_value()) break;
                    hd_lines.push_back(*cont);
                    if (trim(*cont) == hd.delim) break;
                }
                hist.add(original);
                hist.save(paths.history_file);
                ScriptState ss;
                last_exit = script_exec_lines(hd_lines, paths, aliases,
                                               &hist, &cfg, last_exit, ss);
                continue;
            }
        }

        // Multi-line block: if/for/while/function — buffer until depth reaches 0
        {
            int depth = block_depth_change(original);
            if (depth > 0) {
                std::vector<std::string> block_lines;
                block_lines.push_back(original);
                while (depth > 0) {
                    auto cont = editor.read_line("> ");
                    if (!cont.has_value()) break;
                    block_lines.push_back(*cont);
                    depth += block_depth_change(*cont);
                }
                hist.add(original);
                hist.save(paths.history_file);
                ScriptState ss;
                last_exit = script_exec_lines(block_lines, paths, aliases,
                                               &hist, &cfg, last_exit, ss);
                continue;
            }
        }

        auto expanded = expand_vars(
                            expand_aliases_once(original, aliases), last_exit);

        // VAR=value — shell variable assignment (no spaces around =, valid identifier)
        {
            size_t eq = expanded.find('=');
            bool is_assign = false;
            if (eq != std::string::npos && eq > 0) {
                std::string name = expanded.substr(0, eq);
                bool valid = !std::isdigit((unsigned char)name[0]);
                for (char ch : name)
                    if (!std::isalnum((unsigned char)ch) && ch != '_') { valid = false; break; }
                // Only treat as assignment if there's no space before '=' (no command name)
                if (valid && name.find(' ') == std::string::npos) {
                    g_shell_vars[name] = expanded.substr(eq + 1);
                    last_exit = 0;
                    is_assign = true;
                }
            }
            if (is_assign) {
                hist.add(original);
                hist.save(paths.history_file);
                continue;
            }
        }

        // Single builtin — handle before chain splitting
        if (handle_builtin(expanded, aliases, paths, hist, cfg)) {
            if (ll != "history") {
                hist.add(original);
                hist.save(paths.history_file);
            }
            continue;
        }

        // Split on ; && || then execute each segment conditionally
        auto chain = split_chain(expanded);
        for (auto& cc : chain) {
            bool run = false;
            switch (cc.op) {
                case ChainOp::FIRST:
                case ChainOp::ALWAYS: run = true;              break;
                case ChainOp::AND:    run = (last_exit == 0);  break;
                case ChainOp::OR:     run = (last_exit != 0);  break;
            }
            if (!run) continue;

            // Detect trailing & (background operator)
            std::string cmd_str = trim(cc.cmd);
            bool bg = false;
            if (!cmd_str.empty() && cmd_str.back() == '&') {
                bg = true;
                cmd_str = trim(cmd_str.substr(0, cmd_str.size() - 1));
            }

            HANDLE bg_hproc = INVALID_HANDLE_VALUE;
            DWORD  bg_pid   = 0;

            auto segs = split_pipe(cmd_str);
            if (segs.size() > 1)
                last_exit = (int)run_pipeline(segs, paths, bg, &bg_hproc, &bg_pid);
            else
                last_exit = (int)run_segment(cmd_str, paths, bg, &bg_hproc, &bg_pid);

            if (bg && bg_hproc != INVALID_HANDLE_VALUE)
                job_add(bg_hproc, bg_pid, cmd_str);
        }

        hist.add(original);
        hist.save(paths.history_file);
    }

    return 0;
}
