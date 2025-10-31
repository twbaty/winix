// Winix Shell v1.10 — Native Logic (&&, ||, &), Env Handling, and Safe Color Prompt
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

// ========== Console / Prompt ==========
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

static void redraw_line(const std::string& promptStr, const std::string& buf, size_t cursor) {
    std::cout << "\r\033[K" << promptStr << buf;
    size_t target = promptStr.size() + cursor;
    size_t current = promptStr.size() + buf.size();
    if (current > target)
        std::cout << std::string(current - target, '\b');
    std::cout.flush();
}

// ========== Utilities ==========
static bool is_exe_path(const std::string& s) {
    return s.find('\\') != std::string::npos || s.find('/') != std::string::npos;
}

static std::string join_cmdline(const std::vector<std::string>& argv) {
    std::ostringstream oss;
    for (size_t i = 0; i < argv.size(); ++i) {
        const std::string& a = argv[i];
        bool needQuote = a.find_first_of(" \t\"&|<>^") != std::string::npos;
        if (i) oss << ' ';
        if (!needQuote) { oss << a; continue; }
        oss << '"';
        for (char c : a) {
            if (c == '"') oss << '\\';
            oss << c;
        }
        oss << '"';
    }
    return oss.str();
}

static std::string expand_env_once(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i=0;i<in.size();) {
        if (in[i] == '%') {
            size_t j = in.find('%', i+1);
            if (j != std::string::npos) {
                std::string name = in.substr(i+1, j-(i+1));
                const char* v = std::getenv(name.c_str());
                if (v) out += v;
                i = j+1;
                continue;
            }
        }
        else if (in[i] == '$') {
            size_t j = i+1;
            while (j < in.size() && (isalnum((unsigned char)in[j]) || in[j]=='_')) ++j;
            std::string name = in.substr(i+1, j-(i+1));
            const char* v = std::getenv(name.c_str());
            if (v) out += v;
            i = j;
            continue;
        }
        out.push_back(in[i++]);
    }
    return out;
}

// ========== Tokenizer ==========
static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (size_t i=0;i<line.size();++i) {
        char c = line[i];
        if (c == '"') { inQuotes = !inQuotes; continue; }
        if (!inQuotes && std::isspace((unsigned char)c)) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }
        // detect &&, ||, &
        if (!inQuotes && (c=='&' || c=='|')) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            if (i+1 < line.size() && line[i+1]==c) {
                out.push_back(std::string(2,c)); ++i;
            } else {
                out.push_back(std::string(1,c));
            }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ========== Execution ==========
static bool run_single(const std::vector<std::string>& argv);

static bool exec_command_chain(std::vector<std::string> tokens) {
    std::vector<std::vector<std::string>> segments;
    std::vector<std::string> cur;
    std::vector<std::string> ops;
    for (auto& t : tokens) {
        if (t=="&&" || t=="||" || t=="&") { segments.push_back(cur); cur.clear(); ops.push_back(t); }
        else cur.push_back(t);
    }
    if (!cur.empty()) segments.push_back(cur);

    DWORD lastCode = 0;
    for (size_t i=0;i<segments.size();++i) {
        bool bg = (i < ops.size() && ops[i] == "&");
        bool shouldRun = true;
        if (i>0 && ops[i-1]=="&&" && lastCode!=0) shouldRun = false;
        if (i>0 && ops[i-1]=="||" && lastCode==0) shouldRun = false;
        if (!shouldRun) continue;

        if (bg) {
            std::vector<std::string> a = segments[i];
            std::string cmdline = join_cmdline(a);
            std::vector<char> buf(cmdline.begin(), cmdline.end());
            buf.push_back('\0');
            STARTUPINFOA si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            BOOL ok = CreateProcessA(nullptr, buf.data(), nullptr,nullptr,TRUE,
                CREATE_NEW_CONSOLE,nullptr,nullptr,&si,&pi);
            if (ok) {
                std::cout << "[background] PID " << pi.dwProcessId << " started\n";
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            } else {
                std::cerr << "Failed background start: " << a[0] << "\n";
            }
            continue;
        }

        if (!segments[i].empty()) {
            bool ok = run_single(segments[i]);
            lastCode = ok ? 0 : 1;
        }
    }
    return true;
}

// Minimal process runner
static bool run_single(const std::vector<std::string>& argv) {
    if (argv.empty()) return true;
    const std::string& cmd = argv[0];
    if (cmd == "cd") {
        if (argv.size()>1) {
            try { fs::current_path(argv[1]); }
            catch (...) { std::cerr << "cd: cannot access " << argv[1] << "\n"; }
        }
        return true;
    }
    if (cmd == "exit" || cmd == "quit") { std::cout << "Goodbye.\n"; exit(0); }

    std::string cmdline = join_cmdline(argv);
    std::vector<char> buf(cmdline.begin(), cmdline.end());
    buf.push_back('\0');
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, buf.data(), nullptr,nullptr,TRUE,0,nullptr,nullptr,&si,&pi);
    if (!ok) {
        std::cerr << "Error: failed to start: " << argv[0] << "\n";
        return false;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code=0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return code==0;
}

// ========== Line Editor ==========
static std::string edit_line(const std::string& promptStr) {
    std::string buf;
    size_t cursor = 0;
    std::cout << promptStr << std::flush;
    while (true) {
        int ch = _getch();
        if (ch == 13) { std::cout << "\n"; return buf; }
        else if (ch == 8) {
            if (cursor>0) { buf.erase(buf.begin()+cursor-1); cursor--; redraw_line(promptStr, buf, cursor); }
        }
        else if (std::isprint((unsigned char)ch)) {
            buf.insert(buf.begin()+cursor,(char)ch);
            cursor++; redraw_line(promptStr, buf, cursor);
        }
    }
}

// ========== Main ==========
int main() {
    enable_vt_mode();
    std::cout << "Winix Shell v1.10 — Native Logic & Env Stable\n";

    while (true) {
        std::string line = edit_line(prompt());
        if (line.empty()) continue;
        line = expand_env_once(line);
        auto tokens = tokenize(line);
        exec_command_chain(tokens);
    }
}
