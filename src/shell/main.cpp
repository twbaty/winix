// src/shell/main.cpp
// Winix Shell v1.13.1 — True Background Jobs, Alias & History Persist, Env Expansion
// MinGW-friendly (no C++20-only APIs)

#include <windows.h>
#include <shlwapi.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <filesystem>

#pragma comment(lib, "Shlwapi.lib")

namespace fs = std::filesystem;

// ---------- utils
static inline std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static inline bool ieq(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    return true;
}

static std::string str_tolower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static bool str_ends_with(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string join_cmd_argv(const std::vector<std::string>& v, size_t start=0) {
    std::ostringstream oss;
    for (size_t i=start; i<v.size(); ++i) {
        if (i>start) oss << ' ';
        // naive quoting preservation
        if (v[i].find(' ') != std::string::npos || v[i].find('&')!=std::string::npos)
            oss << '"' << v[i] << '"';
        else
            oss << v[i];
    }
    return oss.str();
}

// Split a command line into tokens, respecting simple quotes "..." and '...'
static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool in_s = false, in_d = false;
    for (size_t i=0;i<line.size();++i) {
        char c = line[i];
        if (c=='"' && !in_s) { in_d = !in_d; cur.push_back(c); continue; }
        if (c=='\'' && !in_d) { in_s = !in_s; cur.push_back(c); continue; }
        if (!in_s && !in_d && (c==' '||c=='\t')) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---------- history
struct History {
    std::string path;
    size_t max_entries = 50;
    std::vector<std::string> entries;

    void load() {
        entries.clear();
        std::ifstream f(path);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            line = trim(line);
            if (!line.empty()) entries.push_back(line);
        }
        // de-dup while preserving order: last occurrence kept
        std::vector<std::string> dedup;
        std::map<std::string,bool> seen;
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (!seen[*it]) { seen[*it]=true; dedup.push_back(*it); }
        }
        std::reverse(dedup.begin(), dedup.end());
        entries.swap(dedup);
        if (entries.size() > max_entries) {
            entries.erase(entries.begin(), entries.end()-max_entries);
        }
    }

    void set_max_from_env() {
        char* m = std::getenv("WINIX_HISTORY_MAX");
        if (m && *m) {
            int v = std::atoi(m);
            if (v > 0 && v <= 5000) max_entries = (size_t)v;
        }
    }

    void add(const std::string &line) {
        std::string t = trim(line);
        if (t.empty()) return;
        if (!entries.empty() && entries.back() == t) {
            // avoid immediate duplicate
        } else {
            entries.push_back(t);
        }
        if (entries.size() > max_entries) {
            entries.erase(entries.begin());
        }
        flush();
    }

    void flush() {
        std::ofstream f(path, std::ios::trunc);
        for (auto &e : entries) f << e << "\n";
    }
};

// ---------- alias persistence
struct Aliases {
    std::string path;
    std::map<std::string,std::string> kv;

    void load() {
        kv.clear();
        std::ifstream f(path);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0]=='#') continue;
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string k = trim(line.substr(0,pos));
            std::string v = trim(line.substr(pos+1));
            if (!k.empty() && !v.empty()) kv[k]=v;
        }
    }
    void save() const {
        std::ofstream f(path, std::ios::trunc);
        for (auto &p : kv) {
            f << p.first << "=" << p.second << "\n";
        }
    }
};

// ---------- env expansion: %VAR% and $VAR
static std::string expand_env(const std::string &in) {
    std::string s = in;

    // %VAR% (Windows style)
    for (size_t i=0;i<s.size();) {
        if (s[i]=='%') {
            size_t j = s.find('%', i+1);
            if (j!=std::string::npos) {
                std::string key = s.substr(i+1, j-(i+1));
                if (!key.empty()) {
                    char* v = nullptr;
                    std::string up = key;
                    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                    v = std::getenv(up.c_str());
                    std::string val = v? std::string(v) : std::string();
                    s.replace(i, j-i+1, val);
                    i += val.size();
                    continue;
                }
            }
        }
        ++i;
    }

    // $VAR (POSIX-ish)
    for (size_t i=0;i<s.size();) {
        if (s[i]=='$') {
            size_t j=i+1;
            while (j<s.size() && (std::isalnum((unsigned char)s[j]) || s[j]=='_')) ++j;
            std::string key = s.substr(i+1, j-(i+1));
            if (!key.empty()) {
                char* v = std::getenv(key.c_str());
                std::string val = v? std::string(v) : std::string();
                s.replace(i, j-i, val);
                i += val.size();
                continue;
            }
        }
        ++i;
    }
    return s;
}

// ---------- prompt
static std::string prompt() {
    try {
        return "\033[1;32m[Winix]\033[0m " + fs::current_path().string() + " > ";
    } catch (...) {
        return "\033[1;32m[Winix]\033[0m > ";
    }
}

// ---------- process launch helpers
static DWORD spawn_cmd(const std::string& cmdline, bool wait, HANDLE *phProcessOut=nullptr) {
    // Launch `cmd.exe /C <cmdline>`
    std::string full = "cmd.exe /C " + cmdline;
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    // Inherit console handles for I/O
    BOOL ok = CreateProcessA(
        NULL,
        (LPSTR)full.c_str(),
        NULL, NULL, TRUE, 0, NULL, NULL,
        &si, &pi
    );
    if (!ok) {
        std::cerr << "Failed to launch: " << cmdline << " (err " << GetLastError() << ")\n";
        return (DWORD)-1;
    }
    DWORD pid = pi.dwProcessId;
    if (wait) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code=0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return code;
    } else {
        if (phProcessOut) *phProcessOut = pi.hProcess;
        CloseHandle(pi.hThread);
        // leave process handle open only if caller asked; otherwise close
        if (!phProcessOut) CloseHandle(pi.hProcess);
        return 0;
    }
}

// ---------- builtins
static bool is_builtin(const std::string &cmd) {
    std::string c = str_tolower(cmd);
    return c=="cd" || c=="dir" || c=="ls" || c=="echo" || c=="set" ||
           c=="alias" || c=="unalias" || c=="exit" || c=="quit" || c=="history";
}

static int run_builtin(const std::vector<std::string>& argv, Aliases& aliases, History& hist) {
    if (argv.empty()) return 0;
    std::string c = str_tolower(argv[0]);

    if (c=="exit" || c=="quit") {
        std::exit(0);
    }

    if (c=="cd") {
        if (argv.size()<2) {
            const char* h = std::getenv("USERPROFILE");
            if (!h) h = std::getenv("HOMEDRIVE");
            if (!h) h = "C:\\";
            SetCurrentDirectoryA(h);
            return 0;
        }
        std::error_code ec;
        fs::current_path(argv[1], ec);
        if (ec) std::cerr << "cd: " << ec.message() << "\n";
        return ec ? 1 : 0;
    }

    if (c=="dir") {
        // pass through to cmd so formatting matches Windows
        std::string tail = join_cmd_argv(argv, 1);
        return (int)spawn_cmd("dir " + tail, /*wait*/true);
    }

    if (c=="ls") {
        // simple ls emulation (one column by default)
        try {
            for (auto& p : fs::directory_iterator(fs::current_path())) {
                std::cout << p.path().filename().string() << "\n";
            }
        } catch (const std::exception &e) {
            std::cerr << "ls: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    if (c=="echo") {
        if (argv.size()>1) {
            std::string rest = join_cmd_argv(argv, 1);
            std::cout << expand_env(rest) << "\n";
        } else {
            std::cout << "\n";
        }
        return 0;
    }

    if (c=="set") {
        // syntax: set NAME=VALUE
        if (argv.size()<2) {
            // show env
            // Not enumerating all easily here, keep minimal
            std::cerr << "set: usage set NAME=VALUE\n";
            return 1;
        }
        std::string a = argv[1];
        auto pos = a.find('=');
        if (pos==std::string::npos || pos==0) {
            std::cerr << "set: usage set NAME=VALUE\n";
            return 1;
        }
        std::string k = a.substr(0,pos);
        std::string v = a.substr(pos+1);
        if (!SetEnvironmentVariableA(k.c_str(), v.c_str())) {
            std::cerr << "set: failed (" << GetLastError() << ")\n";
            return 1;
        }
        return 0;
    }

    if (c=="alias") {
        if (argv.size()==1) {
            // list
            for (auto &p : aliases.kv) {
                std::cout << "alias " << p.first << "=\"" << p.second << "\"\n";
            }
            return 0;
        }
        // alias NAME="value"
        // accept NAME=value or NAME="value"
        for (size_t i=1;i<argv.size();++i) {
            std::string spec = argv[i];
            auto pos = spec.find('=');
            if (pos==std::string::npos || pos==0) {
                std::cerr << "alias: bad format, use alias name=\"value\"\n";
                return 1;
            }
            std::string name = spec.substr(0,pos);
            std::string value = spec.substr(pos+1);
            value = trim(value);
            if (!value.empty() && (value.front()=='"' || value.front()=='\'')) {
                if (value.size()>=2 && value.back()==value.front())
                    value = value.substr(1, value.size()-2);
            }
            aliases.kv[name]=value;
        }
        aliases.save();
        return 0;
    }

    if (c=="unalias") {
        if (argv.size()<2) { std::cerr << "unalias: name required\n"; return 1; }
        aliases.kv.erase(argv[1]);
        aliases.save();
        return 0;
    }

    if (c=="history") {
        int idx=1;
        for (auto &e : hist.entries) {
            std::cout << idx++ << "  " << e << "\n";
        }
        return 0;
    }

    return 0;
}

// Expand first-token alias like Bash (word-level)
static std::string expand_alias_line(const std::string& line, const Aliases& aliases) {
    auto toks = tokenize(line);
    if (toks.empty()) return line;
    auto it = aliases.kv.find(toks[0]);
    if (it == aliases.kv.end()) return line;

    std::string expanded = it->second;
    if (toks.size()>1) {
        expanded += " ";
        std::ostringstream oss;
        for (size_t i=1;i<toks.size();++i) {
            if (i>1) oss << ' ';
            oss << toks[i];
        }
        expanded += oss.str();
    }
    return expanded;
}

// Execute a single (no operators) command
static DWORD run_simple_command(const std::string& cmdline_raw, Aliases& aliases, History& hist) {
    std::string cmdline = trim(cmdline_raw);
    if (cmdline.empty()) return 0;

    // Alias expansion (Bash-like: only first word)
    cmdline = expand_alias_line(cmdline, aliases);

    // Tokenize after alias expansion (for builtins)
    auto argv = tokenize(cmdline);
    if (argv.empty()) return 0;

    // Builtins
    if (is_builtin(argv[0])) {
        return (DWORD)run_builtin(argv, aliases, hist);
    }

    // External: launch via cmd /C (so globs, quotes, %var% handled by cmd)
    return spawn_cmd(cmdline, /*wait*/true);
}

// Parse & execute with minimal operator support: background '&', reject others for now
static DWORD execute_with_ops(const std::string& line, Aliases& aliases, History& hist) {
    std::string s = trim(line);
    if (s.empty()) return 0;

    // reject pipes/and/or for now
    if (s.find('|') != std::string::npos) {
        std::cerr << "Pipelines '|' not implemented yet.\n";
        return 1;
    }
    if (s.find("&&") != std::string::npos || s.find("||") != std::string::npos) {
        std::cerr << "Chained operators '&&'/'||' not implemented yet.\n";
        return 1;
    }

    // background operator '&' : split at first unquoted '&'
    bool in_s=false, in_d=false;
    size_t amp = std::string::npos;
    for (size_t i=0;i<s.size();++i) {
        char c=s[i];
        if (c=='"' && !in_s) in_d=!in_d;
        else if (c=='\'' && !in_d) in_s=!in_s;
        else if (c=='&' && !in_s && !in_d) { amp=i; break; }
    }

    if (amp == std::string::npos) {
        return run_simple_command(s, aliases, hist);
    }

    std::string left = trim(s.substr(0, amp));
    std::string right = trim(s.substr(amp+1));

    // Launch LEFT in background (no wait)
    if (!left.empty()) {
        HANDLE hProc = NULL;
        DWORD code = spawn_cmd(left, /*wait*/false, &hProc);
        if (code==(DWORD)-1) {
            // failed spawn; still try right
        } else {
            DWORD pid = 0;
            if (hProc) {
                pid = GetProcessId(hProc);
                CloseHandle(hProc);
            }
            std::cout << "[background] PID " << pid << " started\n";
        }
    }

    // Execute RIGHT synchronously in foreground
    if (!right.empty()) {
        return run_simple_command(right, aliases, hist);
    }
    return 0;
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::cout << "Winix Shell v1.13.1 — True Background Jobs\n";

    // setup paths
    std::string base = fs::current_path().string();
    std::string hist_path = (fs::path(base) / "winix_history.txt").string();
    std::string alias_path = (fs::path(base) / "winix_aliases.txt").string();

    History hist{hist_path};
    hist.set_max_from_env(); // env WINIX_HISTORY_MAX, default 50
    hist.load();

    Aliases aliases{alias_path};
    aliases.load();

    // basic interactive loop
    for (;;) {
        std::cout << prompt();
        std::string line;
        if (!std::getline(std::cin, line)) break;

        line = trim(line);
        if (line.empty()) continue;

        // store in history
        hist.add(line);

        // Expand environment variables before execution for echo and external commands
        std::string expanded = expand_env(line);

        // Execute with operators handling (& only)
        (void)execute_with_ops(expanded, aliases, hist);
    }
    return 0;
}
