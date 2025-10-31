// main.cpp — Winix Shell v1.12.3 (Bash-like alias + history + tabs + env)
// Build: Windows only. Requires -std=gnu++17 or c++17.
// MinGW: g++ main.cpp -lShlwapi -o winix.exe

#include <windows.h>
#include <conio.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// ---------- Utilities ----------
static bool str_ends_with(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size()
        && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string to_utf8(const std::wstring &ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), len, nullptr, nullptr);
    return out;
}

static std::wstring to_wide(const std::string &s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

static void enable_vt_mode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

// ---------- Prompt ----------
static std::string prompt() {
    try {
        return "\033[1;32m[Winix]\033[0m " + fs::current_path().string() + " > ";
    } catch (...) {
        return "\033[1;32m[Winix]\033[0m > ";
    }
}

// ---------- Line Editing ----------
static void redraw_line(const std::string &p, const std::string &buf, size_t cur) {
    static const char *CLR = "\33[2K\r";
    std::cout << CLR << p << buf;
    // move cursor back to cur
    size_t tail = buf.size() - cur;
    if (tail) {
        std::cout << std::string(tail, '\b');
    }
    std::cout.flush();
}

static bool is_word_char(char c) {
    return !std::isspace((unsigned char)c);
}

static void extract_token_bounds(const std::string &buf, size_t cur, size_t &start, size_t &end) {
    // current token around cursor
    if (buf.empty()) { start = end = 0; return; }
    if (cur > buf.size()) cur = buf.size();
    size_t s = cur;
    size_t e = cur;
    while (s > 0 && is_word_char(buf[s-1])) s--;
    while (e < buf.size() && is_word_char(buf[e])) e++;
    start = s; end = e;
}

static void tab_complete(std::string &buf, size_t &cur) {
    size_t s=0, e=0;
    extract_token_bounds(buf, cur, s, e);
    std::string token = buf.substr(s, e - s);
    if (token.empty()) return;

    // Expand environment in token for paths like %USERPROFILE%\...
    // Keep it simple: don’t expand here; completion is filesystem-only on literal token.
    std::vector<std::string> cands;

    // If first token and equals known command alias like 'ls', we skip completion.
    // Here we only complete file system paths.
    fs::path base;
    std::string pattern = token;
    try {
        fs::path p = fs::path(pattern);
        fs::path dir = p.parent_path();
        std::string stem = p.filename().string();
        if (dir.empty()) dir = fs::current_path();
        if (fs::exists(dir) && fs::is_directory(dir)) {
            for (auto &entry : fs::directory_iterator(dir)) {
                std::string name = entry.path().filename().string();
                if (name.size() >= stem.size()
                    && std::equal(stem.begin(), stem.end(), name.begin(),
                                  [](char a, char b){ return std::tolower((unsigned char)a)==std::tolower((unsigned char)b); })) {
                    // Reconstruct suggestion (keep directory prefix if provided)
                    fs::path suggestion = entry.path().filename();
                    cands.push_back(suggestion.string() + (entry.is_directory() ? "\\" : ""));
                }
            }
            std::sort(cands.begin(), cands.end());
        }
    } catch (...) {
        return;
    }

    if (cands.empty()) return;
    if (cands.size() == 1) {
        // Replace token with single candidate
        std::string rep = cands[0];
        buf.replace(s, e - s, rep);
        cur = s + rep.size();
        return;
    }

    // Find common prefix
    std::string pref = cands[0];
    for (size_t i=1; i<cands.size(); ++i) {
        size_t j=0;
        while (j < pref.size() && j < cands[i].size() && std::tolower((unsigned char)pref[j]) == std::tolower((unsigned char)cands[i][j])) j++;
        pref.resize(j);
        if (pref.empty()) break;
    }
    if (pref.size() > 0 && pref.size() > (e - s)) {
        buf.replace(s, e - s, pref);
        cur = s + pref.size();
    } else {
        // Show candidates
        std::cout << "\n";
        for (auto &c : cands) std::cout << c << "  ";
        std::cout << "\n";
    }
}

static std::string edit_line(const std::string &p,
                             std::vector<std::string> &history,
                             int &histIndex) {
    std::string buf; size_t cur = 0;
    std::cout << p << std::flush;

    while (true) {
        int ch = _getch();
        if (ch == 13) {                // ENTER
            std::cout << "\n";
            return buf;
        } else if (ch == 3) {          // Ctrl+C
            std::cout << "^C\n";
            buf.clear(); return buf;
        } else if (ch == 8) {          // BACKSPACE
            if (cur > 0) { buf.erase(buf.begin() + cur - 1); cur--; redraw_line(p, buf, cur); }
        } else if (ch == 9) {          // TAB
            tab_complete(buf, cur); redraw_line(p, buf, cur);
        } else if (ch == 224 || ch == 0) { // Extended keys
            int code = _getch();
            if (code == 72) { // UP
                if (histIndex > 0) histIndex--;
                if (histIndex >= 0 && histIndex < (int)history.size()) {
                    buf = history[histIndex];
                    cur = buf.size();
                    redraw_line(p, buf, cur);
                }
            } else if (code == 80) { // DOWN
                if (histIndex + 1 < (int)history.size()) {
                    histIndex++;
                    buf = history[histIndex];
                } else { histIndex = history.size(); buf.clear(); }
                cur = buf.size(); redraw_line(p, buf, cur);
            } else if (code == 75) { // LEFT
                if (cur > 0) cur--; redraw_line(p, buf, cur);
            } else if (code == 77) { // RIGHT
                if (cur < buf.size()) cur++; redraw_line(p, buf, cur);
            } else if (code == 83) { // DEL
                if (cur < buf.size()) { buf.erase(buf.begin() + cur); redraw_line(p, buf, cur); }
            }
        } else if (std::isprint((unsigned char)ch)) {
            buf.insert(buf.begin() + cur, (char)ch);
            cur++; redraw_line(p, buf, cur);
        }
    }
}

// ---------- Environment Expansion ----------
static std::string get_env_var(const std::string &name) {
    DWORD size = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
    if (size == 0) return {};
    std::string val(size, '\0');
    DWORD got = GetEnvironmentVariableA(name.c_str(), val.data(), size);
    if (got > 0 && got < size) val.resize(got);
    return val;
}

static std::string expand_percent_vars(const std::string &s) {
    std::string out; out.reserve(s.size());
    size_t i=0, n=s.size();
    while (i<n) {
        if (s[i]=='%') {
            size_t j = s.find('%', i+1);
            if (j != std::string::npos) {
                std::string name = s.substr(i+1, j-(i+1));
                std::string val = get_env_var(name);
                out += val;
                i = j+1;
                continue;
            }
        }
        out.push_back(s[i]); i++;
    }
    return out;
}

static bool is_env_name_char(char c) {
    return std::isalnum((unsigned char)c) || c=='_' ;
}

static std::string expand_dollar_vars(const std::string &s) {
    std::string out; out.reserve(s.size());
    for (size_t i=0; i<s.size();) {
        if (s[i] == '$') {
            size_t j = i+1;
            if (j < s.size() && s[j]=='{') {
                size_t k = s.find('}', j+1);
                if (k != std::string::npos) {
                    std::string name = s.substr(j+1, k-(j+1));
                    out += get_env_var(name);
                    i = k+1; continue;
                }
            }
            size_t k = j;
            while (k < s.size() && is_env_name_char(s[k])) k++;
            if (k>j) {
                std::string name = s.substr(j, k-j);
                out += get_env_var(name);
                i = k; continue;
            }
        }
        out.push_back(s[i]); i++;
    }
    return out;
}

static std::string expand_env_once(const std::string &in) {
    // One pass of %VAR% then $VAR
    return expand_dollar_vars(expand_percent_vars(in));
}

// ---------- Tokenization ----------
static std::vector<std::string> tokenize(const std::string &line) {
    std::vector<std::string> argv;
    std::string cur;
    bool in_single=false, in_double=false;
    for (size_t i=0; i<line.size(); ++i) {
        char c = line[i];
        if (in_single) {
            if (c=='\'') in_single = false;
            else cur.push_back(c);
        } else if (in_double) {
            if (c=='"') in_double = false;
            else cur.push_back(c);
        } else {
            if (std::isspace((unsigned char)c)) {
                if (!cur.empty()) { argv.push_back(cur); cur.clear(); }
            } else if (c=='\'') {
                in_single = true;
            } else if (c=='"') {
                in_double = true;
            } else {
                cur.push_back(c);
            }
        }
    }
    if (!cur.empty()) argv.push_back(cur);
    return argv;
}

// ---------- Aliases ----------
static std::unordered_map<std::string,std::string> g_aliases;

static std::string first_word(const std::string &s) {
    size_t i=0; while (i<s.size() && std::isspace((unsigned char)s[i])) i++;
    size_t j=i; while (j<s.size() && !std::isspace((unsigned char)s[j])) j++;
    return s.substr(i, j-i);
}

static std::string ltrim(const std::string &s) {
    size_t i=0; while (i<s.size() && std::isspace((unsigned char)s[i])) i++;
    return s.substr(i);
}

static std::string alias_expand_line(const std::string &line) {
    // Bash-like: repeatedly expand the first word if it’s an alias.
    std::string out = line;
    for (int depth=0; depth<10; ++depth) {
        std::string fw = first_word(out);
        if (fw.empty()) break;
        auto it = g_aliases.find(fw);
        if (it == g_aliases.end()) break;

        // replace first word with alias body
        std::string body = it->second;
        // find start/end of fw
        size_t i=0; while (i<out.size() && std::isspace((unsigned char)out[i])) i++;
        size_t j=i; while (j<out.size() && !std::isspace((unsigned char)out[j])) j++;
        std::string rest = out.substr(j);
        out = body + rest;
    }
    return out;
}

// ---------- Builtins ----------
static int builtin_echo(const std::vector<std::string> &args) {
    // Join with spaces; variables already expanded in command line pre-tokenization
    for (size_t i=1; i<args.size(); ++i) {
        if (i>1) std::cout << " ";
        std::cout << args[i];
    }
    std::cout << "\n";
    return 0;
}

static int builtin_cd(const std::vector<std::string> &args) {
    std::string target;
    if (args.size() < 2) {
        char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOMEPATH");
        if (!home) return 0;
        target = home;
    } else {
        target = args[1];
        // support "cd -" toggling not implemented, keep minimal
    }
    try {
        fs::current_path(target);
    } catch (...) {
        std::cout << "cd: No such file or directory\n";
    }
    return 0;
}

static int builtin_set(const std::vector<std::string> &args) {
    // Accept either: set NAME=VALUE  OR just 'set' to list
    if (args.size() == 1) {
        // list env
        LPCH env = GetEnvironmentStringsA();
        if (!env) return 0;
        for (LPCH v = env; *v; ) {
            std::string line = v;
            std::cout << line << "\n";
            v += (line.size() + 1);
        }
        FreeEnvironmentStringsA(env);
        return 0;
    }
    // NAME=VALUE in args[1]
    auto &kv = args[1];
    size_t eq = kv.find('=');
    if (eq == std::string::npos || eq==0) {
        std::cout << "set: use NAME=VALUE\n";
        return 1;
    }
    std::string name = kv.substr(0, eq);
    std::string val  = kv.substr(eq+1);
    if (_putenv_s(name.c_str(), val.c_str()) != 0) {
        std::cout << "set: failed\n";
        return 1;
    }
    return 0;
}

static int builtin_alias(const std::vector<std::string> &args) {
    if (args.size() == 1) {
        for (auto &kv : g_aliases) {
            std::cout << "alias " << kv.first << "=\"" << kv.second << "\"\n";
        }
        return 0;
    }
    // alias NAME="VALUE"  or NAME=VALUE (quotes optional if no spaces)
    for (size_t i=1; i<args.size(); ++i) {
        std::string a = args[i];
        size_t eq = a.find('=');
        if (eq == std::string::npos || eq==0) {
            std::cout << "alias: invalid '" << a << "'\n";
            continue;
        }
        std::string name = a.substr(0, eq);
        std::string val  = a.substr(eq+1);
        // strip optional quotes if present
        if (val.size() >= 2 && ((val.front()=='"' && val.back()=='"') || (val.front()=='\'' && val.back()=='\''))) {
            val = val.substr(1, val.size()-2);
        }
        g_aliases[name] = val;
    }
    return 0;
}

static int builtin_unalias(const std::vector<std::string> &args) {
    if (args.size() < 2) {
        std::cout << "unalias: name required\n";
        return 1;
    }
    for (size_t i=1; i<args.size(); ++i) g_aliases.erase(args[i]);
    return 0;
}

// ---------- Process Exec ----------
struct Redir { /* placeholder for future redirs */ };

static DWORD run_simple_command(std::vector<std::string> argv, const Redir&, bool background) {
    if (argv.empty()) return 0;

    // Builtins
    std::string cmd = argv[0];
    if (cmd == "exit") { ExitProcess(0); }
    if (cmd == "echo") return builtin_echo(argv);
    if (cmd == "cd")   return builtin_cd(argv);
    if (cmd == "set")  return builtin_set(argv);
    if (cmd == "alias") return builtin_alias(argv);
    if (cmd == "unalias") return builtin_unalias(argv);
    if (cmd == "ls") { // map to dir /b
        argv[0] = "cmd";
        argv.insert(argv.begin()+1, "/c");
        argv.insert(argv.begin()+2, "dir");
        argv.insert(argv.begin()+3, "/b");
    }
    else if (cmd == "dir") {
        // let cmd /c dir ...
        argv.insert(argv.begin(), "/c");
        argv.insert(argv.begin(), "cmd");
    }

    // Build command line
    std::ostringstream oss;
    for (size_t i=0; i<argv.size(); ++i) {
        if (i) oss << " ";
        bool needQ = argv[i].find_first_of(" \t\"") != std::string::npos;
        if (needQ) {
            oss << "\"";
            for (char c : argv[i]) {
                if (c=='"') oss << "\\\"";
                else oss << c;
            }
            oss << "\"";
        } else oss << argv[i];
    }
    std::string cmdline = oss.str();

    // If user typed something like "prog" with no extension, let CreateProcess search PATHEXT
    // Easiest is to invoke via cmd /c for non-.exe/.bat/.cmd to mimic cmd behavior.
    bool run_via_cmd = false;
    if (!(str_ends_with(argv[0], ".exe") || str_ends_with(argv[0], ".bat") || str_ends_with(argv[0], ".cmd"))) {
        run_via_cmd = true;
    }

    std::string final = cmdline;
    if (run_via_cmd) {
        final = "cmd /c " + cmdline;
    }

    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    DWORD flags = 0;
    if (background) flags |= CREATE_NEW_PROCESS_GROUP; // simple “bg”

    std::string mutable_cmd = final;
    if (!CreateProcessA(
            nullptr,
            mutable_cmd.data(),
            nullptr, nullptr,
            FALSE,
            flags,
            nullptr,
            nullptr,
            &si, &pi)) {
        std::cout << "Error: failed to start: " << argv[0] << "\n";
        return GetLastError();
    }

    if (background) {
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

// ---------- Parser for &, minimal; (no pipes/&&/|| yet) ----------
static void split_bg(const std::string &line, std::string &front, bool &bg) {
    // If line ends with unquoted &, run in background
    bool in_s=false, in_d=false;
    size_t last = std::string::npos;
    for (size_t i=0; i<line.size(); ++i) {
        char c = line[i];
        if (!in_d && c=='\'' ) in_s = !in_s;
        else if (!in_s && c=='"' ) in_d = !in_d;
    }
    bg = false;
    // trim spaces
    size_t end = line.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) { front.clear(); return; }
    size_t start = line.find_first_not_of(" \t\r\n");
    std::string trimmed = line.substr(start, end - start + 1);
    if (!in_s && !in_d && !trimmed.empty() && trimmed.back()=='&') {
        bg = true;
        // remove trailing &
        size_t k = trimmed.size()-1;
        while (k>0 && std::isspace((unsigned char)trimmed[k-1])) k--;
        // remove the '&'
        if (k>0) {
            // Make sure we only drop the last ampersand
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n&")+1);
            // Or simply:
            trimmed.pop_back();
        }
        // re-trim
        size_t e2 = trimmed.find_last_not_of(" \t\r\n");
        if (e2==std::string::npos) trimmed.clear();
        else trimmed = trimmed.substr(0, e2+1);
    }
    front = trimmed;
}

// ---------- Main ----------
int main() {
    enable_vt_mode();
    std::cout << "Winix Shell v1.12.3 — Bash-like Aliases, Env, Tabs, History\n";

    std::vector<std::string> history;
    int histIndex = 0;

    while (true) {
        std::string line = edit_line(prompt(), history, histIndex);
        if (line.empty()) continue;

        // Store history
        history.push_back(line);
        histIndex = (int)history.size();

        // Alias expansion BEFORE tokenization
        line = alias_expand_line(line);

        // Background check
        std::string front; bool bg=false;
        split_bg(line, front, bg);
        if (front.empty()) continue;

        // One-pass env expansion
        front = expand_env_once(front);

        // Tokenize
        auto args = tokenize(front);

        // Guard: pipelines not implemented yet
        if (std::find(args.begin(), args.end(), "|") != args.end()) {
            std::cout << "Pipelines '|' not implemented yet.\n";
            continue;
        }

        run_simple_command(std::move(args), {}, bg);
    }
}
