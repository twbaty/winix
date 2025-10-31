// Winix Shell v1.12.2 — CMD Builtins + Smart Tabs (C++17 Safe)
#include <windows.h>
#include <conio.h>
#include <io.h>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <map>
#include <algorithm>

namespace fs = std::filesystem;

// ==== helpers ==========================================================
static bool str_ends_with(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// ==== console / prompt =================================================
static void enable_vt_mode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

static std::string prompt() {
    try {
        return "\033[1;32m[Winix]\033[0m " + fs::current_path().string() + " > ";
    } catch (...) {
        return "\033[1;32m[Winix]\033[0m > ";
    }
}

static void redraw_line(const std::string &p, const std::string &buf, size_t cur) {
    std::cout << "\r\033[K" << p << buf;
    size_t tgt = p.size() + cur, now = p.size() + buf.size();
    if (now > tgt) std::cout << std::string(now - tgt, '\b');
    std::cout.flush();
}

// ==== env expansion ====================================================
static std::string expand_env_once(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '%') {
            size_t j = in.find('%', i + 1);
            if (j != std::string::npos) {
                std::string name = in.substr(i + 1, j - (i + 1));
                char buf[32767];
                DWORD len = GetEnvironmentVariableA(name.c_str(), buf, sizeof(buf));
                if (len > 0 && len < sizeof(buf)) out.append(buf, len);
                i = j + 1;
                continue;
            }
        } else if (in[i] == '$') {
            size_t j = i + 1;
            while (j < in.size() && (isalnum((unsigned char)in[j]) || in[j] == '_')) ++j;
            std::string name = in.substr(i + 1, j - (i + 1));
            char buf[32767];
            DWORD len = GetEnvironmentVariableA(name.c_str(), buf, sizeof(buf));
            if (len > 0 && len < sizeof(buf)) out.append(buf, len);
            i = j;
            continue;
        }
        out.push_back(in[i++]);
    }
    return out;
}

// ==== tokenization =====================================================
static std::vector<std::string> tokenize(const std::string &line) {
    std::vector<std::string> out; std::string cur; bool inQ = false;
    for (char c : line) {
        if (c == '"') { inQ = !inQ; continue; }
        if (!inQ && std::isspace((unsigned char)c)) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ==== proc spawn =======================================================
struct Redir { std::string in, out; bool append = false; };

static bool spawn_proc(const std::string &cmdline, bool background, const Redir &) {
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, (LPSTR)cmdline.c_str(),
        nullptr, nullptr, TRUE,
        background ? DETACHED_PROCESS | CREATE_NO_WINDOW : 0,
        nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    if (background) {
        std::cout << "[background] PID " << pi.dwProcessId << " started\n";
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return true;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    return true;
}

// ==== run_simple_command ===============================================
static DWORD run_simple_command(std::vector<std::string> argv, const Redir &, bool background) {
    if (argv.empty()) return 0;
    const std::string &cmd = argv[0];

    // builtins ----------------------------------------------------------
    if (cmd == "cd") {
        if (argv.size() > 1) {
            try { fs::current_path(argv[1]); }
            catch (...) { std::cerr << "cd: No such file or directory\n"; }
        } else std::cout << fs::current_path().string() << "\n";
        return 0;
    }
    if (cmd == "exit" || cmd == "quit") {
        std::cout << "Goodbye.\n"; exit(0);
    }
    if (cmd == "set") {
        if (argv.size() == 1) {
            LPCH env = GetEnvironmentStringsA(), cur = env;
            while (*cur) {
                std::cout << cur << "\n";
                while (*cur++) {}
            }
            FreeEnvironmentStringsA(env);
            return 0;
        }
        auto eq = argv[1].find('=');
        if (eq != std::string::npos) {
            std::string key = argv[1].substr(0, eq);
            std::string val = argv[1].substr(eq + 1);
            SetEnvironmentVariableA(key.c_str(), val.c_str());
        }
        return 0;
    }

    // external ----------------------------------------------------------
    std::ostringstream oss;
    for (size_t i = 0; i < argv.size(); ++i) {
        const std::string &a = argv[i];
        bool need_q = a.find_first_of(" \t\"") != std::string::npos;
        if (!need_q) oss << a;
        else {
            oss << '"';
            for (char c : a) { if (c == '"') oss << '\\'; oss << c; }
            oss << '"';
        }
        if (i + 1 < argv.size()) oss << " ";
    }

    std::string cmdline = oss.str();
    bool needs_cmd = true;
    if (cmdline.find('\\') != std::string::npos || cmdline.find('/') != std::string::npos)
        needs_cmd = false;
    else if (cmdline.size() > 4 && (
        str_ends_with(cmdline, ".exe") || str_ends_with(cmdline, ".bat") || str_ends_with(cmdline, ".cmd")))
        needs_cmd = false;

    if (needs_cmd) cmdline = "cmd /C " + cmdline;

    spawn_proc(cmdline, background, {});
    return 0;
}

// ==== line editor & tab completion =====================================
static void tab_complete(std::string &buf, size_t &cursor) {
    bool inQ = false; size_t start = 0;
    for (size_t i = 0; i < cursor; ++i) {
        if (buf[i] == '"') inQ = !inQ;
        if (!inQ && std::isspace((unsigned char)buf[i])) start = i + 1;
    }
    std::string prefix = buf.substr(start, cursor - start);
    fs::path base = fs::current_path();
    fs::path dir = base; std::string pref = prefix;
    size_t slash = prefix.find_last_of("/\\");
    if (slash != std::string::npos) {
        dir = base / prefix.substr(0, slash);
        pref = prefix.substr(slash + 1);
    }
    if (!fs::exists(dir)) return;

    std::vector<std::string> matches;
    for (auto &e : fs::directory_iterator(dir)) {
        std::string n = e.path().filename().string();
        if (n.rfind(pref, 0) == 0)
            matches.push_back(n + (e.is_directory() ? "\\" : ""));
    }
    if (matches.empty()) return;
    std::sort(matches.begin(), matches.end());

    auto redraw = [&]() {
        std::cout << "\n";
        int col = 0;
        for (auto &m : matches) {
            std::cout << m << "\t";
            if (++col % 4 == 0) std::cout << "\n";
        }
        std::cout << "\n";
        redraw_line(prompt(), buf, cursor);
    };

    if (matches.size() == 1) {
        std::string add = matches[0].substr(pref.size());
        buf.insert(cursor, add);
        cursor += add.size();
        redraw_line(prompt(), buf, cursor);
    } else redraw();
}

static std::string edit_line(const std::string &p) {
    std::string buf; size_t cur = 0;
    std::cout << p << std::flush;
    while (true) {
        int ch = _getch();
        if (ch == 13) { std::cout << "\n"; return buf; }
        else if (ch == 8) {
            if (cur > 0) { buf.erase(buf.begin() + cur - 1); cur--; redraw_line(p, buf, cur); }
        } else if (ch == 9) {
            tab_complete(buf, cur); redraw_line(p, buf, cur);
        } else if (std::isprint((unsigned char)ch)) {
            buf.insert(buf.begin() + cur, (char)ch); cur++; redraw_line(p, buf, cur);
        }
    }
}

// ==== main =============================================================
int main() {
    enable_vt_mode();
    std::cout << "Winix Shell v1.12.2 — CMD Builtins + Smart Tabs\n";

    while (true) {
        std::string line = edit_line(prompt());
        if (line.empty()) continue;
        line = expand_env_once(line);
        auto args = tokenize(line);
        run_simple_command(args, {}, false);
    }
}
