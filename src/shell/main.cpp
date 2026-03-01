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
            // $VAR
            if (c == '$') {
                size_t j=i+1;
                while (j<line.size() &&
                       (std::isalnum((unsigned char)line[j]) ||
                        line[j]=='_'))
                    ++j;
                if (j > i+1) {
                    out += getenv_win(line.substr(i+1, j-(i+1)));
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
// Process spawning
// --------------------------------------------------
// Spawn a process via cmd.exe /C (used for system PATH fallback).
static DWORD spawn_cmd(const std::string& command, bool wait,
                       HANDLE h_in  = INVALID_HANDLE_VALUE,
                       HANDLE h_out = INVALID_HANDLE_VALUE,
                       HANDLE h_err = INVALID_HANDLE_VALUE) {
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
        CREATE_NEW_PROCESS_GROUP,
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

// Spawn a known executable directly (bypasses cmd.exe so arguments are
// never mangled by cmd's quoting rules — important for paths with spaces).
static DWORD spawn_direct(const std::string& exe_path,
                          const std::vector<std::string>& args, bool wait,
                          HANDLE h_in  = INVALID_HANDLE_VALUE,
                          HANDLE h_out = INVALID_HANDLE_VALUE,
                          HANDLE h_err = INVALID_HANDLE_VALUE) {
    // Build command line: "exe_path" arg1 arg2 ...
    std::string cmdline = "\"" + exe_path + "\"";
    for (auto& a : args) cmdline += " " + a;

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
static DWORD run_segment(const std::string& seg, const Paths& paths) {
    // Parse redirection operators out first
    Redirects redir;
    std::string clean = parse_redirects(seg, redir);

    auto t = shell_tokens(clean);
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
            code = spawn_direct(p.string(), rest_args, true, h_in, h_out, h_err);
            close_redirs();
            return code;
        }
    }

    // Check Winix bin dir
    {
        fs::path p = fs::path(paths.bin_dir) / (cmd + ".exe");
        if (fs::exists(p)) {
            code = spawn_direct(p.string(), rest_args, true, h_in, h_out, h_err);
            close_redirs();
            return code;
        }
    }

    // Fallback: system PATH via cmd.exe
    code = spawn_cmd(clean, true, h_in, h_out, h_err);
    close_redirs();
    return code;
}

static DWORD run_pipeline(const std::vector<std::string>& segs, const Paths& paths) {
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

    for (int i = 0; i < n; ++i) {
        auto t = shell_tokens(segs[i]);
        if (t.empty()) continue;

        HANDLE raw_in  = (i == 0)     ? GetStdHandle(STD_INPUT_HANDLE)  : pipes[i-1].read;
        HANDLE raw_out = (i == n - 1) ? GetStdHandle(STD_OUTPUT_HANDLE) : pipes[i].write;

        HANDLE h_in  = make_inheritable(raw_in);
        HANDLE h_out = make_inheritable(raw_out);

        std::string exe = resolve_exe(t[0], paths);
        std::string cmdline;
        const char* app = nullptr;

        if (!exe.empty()) {
            cmdline = "\"" + exe + "\"";
            for (size_t j = 1; j < t.size(); ++j) cmdline += " " + t[j];
            app = exe.c_str();
        } else {
            cmdline = "cmd.exe /C " + segs[i];
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

        if (!ok) {
            DWORD e = GetLastError();
            std::cerr << "pipeline: failed to start '" << t[0]
                      << "' (code " << e << ")\n";
        } else {
            CloseHandle(pi.hThread);
            procs.push_back(pi.hProcess);
        }
    }

    // Close all pipe ends in parent so children receive EOF
    for (auto& p : pipes) {
        CloseHandle(p.read);
        CloseHandle(p.write);
    }

    DWORD last_code = 0;
    for (size_t i = 0; i < procs.size(); ++i) {
        WaitForSingleObject(procs[i], INFINITE);
        if (i == procs.size() - 1)
            GetExitCodeProcess(procs[i], &last_code);
        CloseHandle(procs[i]);
    }

    return last_code;
}

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
int main() {
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

    std::cout << "Winix Shell — Stable Edition\n";

    Config cfg;
    auto paths = make_paths();
    load_rc(paths, cfg);

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
        auto in = editor.read_line(prompt(cfg));
        if (!in.has_value()) break;

        auto original = trim(*in);
        if (original.empty()) continue;

        auto ll = to_lower(original);
        if (ll == "exit" || ll == "quit") break;

        auto expanded = expand_vars(
                            expand_aliases_once(original, aliases), last_exit);

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

            auto segs = split_pipe(cc.cmd);
            if (segs.size() > 1)
                last_exit = (int)run_pipeline(segs, paths);
            else
                last_exit = (int)run_segment(cc.cmd, paths);
        }

        hist.add(original);
        hist.save(paths.history_file);
    }

    return 0;
}
