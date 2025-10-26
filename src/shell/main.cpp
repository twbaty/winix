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
    return "\033[1;32m[Winix]\033[0m " + fs::current_path().string() + " > ";
}

static void redraw_line(const std::string& p, const std::string& buf, size_t cursor) {
    std::cout << "\r\033[K" << p << buf;
    size_t target = p.size() + cursor;
    size_t current = p.size() + buf.size();
    if (current > target) {
        std::cout << std::string(current - target, '\b');
    }
    std::cout.flush();
}

// ========== Small utils ==========
static bool is_exe_path(const std::string& s) {
    return s.find('\\') != std::string::npos || s.find('/') != std::string::npos;
}

static bool ends_with_casei(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    auto a = s.end() - suf.size();
    for (size_t i=0;i<suf.size();++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)suf[i])) return false;
    return true;
}

static std::string join_cmdline(const std::vector<std::string>& argv) {
    std::ostringstream oss;
    for (size_t i = 0; i < argv.size(); ++i) {
        const std::string& a = argv[i];
        bool needQuote = a.find_first_of(" \t\"&|<>^") != std::string::npos;
        if (i) oss << ' ';
        if (!needQuote) { oss << a; continue; }
        oss << '"';
        for (char c : a) { if (c == '"') oss << '\\'; oss << c; }
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
        out.push_back(in[i++]);
    }
    return out;
}

static std::string normalize_path(const std::string& s) {
    std::string out = s;
    std::replace(out.begin(), out.end(), '\\', '/');
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
        if (c == '\\' && i+1 < line.size()) { cur.push_back(line[++i]); continue; }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ========== Globbing (* ?) (cwd only) ==========
static bool match_wild(const std::string& name, const std::string& pat) {
    size_t n = 0, p = 0, star = std::string::npos, ss = 0;
    while (n < name.size()) {
        if (p < pat.size() && (pat[p] == '?' ||
           std::tolower((unsigned char)pat[p]) == std::tolower((unsigned char)name[n]))) {
            ++n; ++p;
        } else if (p < pat.size() && pat[p] == '*') {
            star = p++;
            ss = n;
        } else if (star != std::string::npos) {
            p = star + 1;
            n = ++ss;
        } else return false;
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

static std::vector<std::string> expand_globs_one(const std::string& token) {
    if (token.find('*') == std::string::npos && token.find('?') == std::string::npos)
        return { token };
    std::vector<std::string> matches;
    for (auto& e : fs::directory_iterator(fs::current_path())) {
        std::string name = e.path().filename().string();
        if (match_wild(name, token)) matches.push_back(name);
    }
    if (matches.empty()) return { token };
    std::sort(matches.begin(), matches.end());
    return matches;
}

static std::vector<std::string> expand_globs(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    for (auto& a : args) {
        auto v = expand_globs_one(a);
        out.insert(out.end(), v.begin(), v.end());
    }
    return out;
}

// ========== PATH search ==========
static std::string find_on_path(const std::string& cmd) {
    if (is_exe_path(cmd)) {
        std::string p = cmd;
        if (!ends_with_casei(p, ".exe")) p += ".exe";
        if (fs::exists(p)) return p;
        return cmd;
    }
    char* envp = std::getenv("PATH");
    std::string path = envp ? envp : "";
    std::stringstream ss(path);
    std::string d;
    while (std::getline(ss, d, ';')) {
        if (d.empty()) continue;
        std::string base = (fs::path(d) / cmd).string();
        if (fs::exists(base + ".exe")) return base + ".exe";
        if (fs::exists(base)) return base;
    }
    if (fs::exists(cmd)) return cmd;
    if (fs::exists(cmd + ".exe")) return cmd + ".exe";
    return cmd;
}

// ========== Pipes & Redirection ==========
struct Redir {
    std::string inPath, outPath;
    bool append = false;
};

static void split_redirs(std::vector<std::string>& tokens, Redir& r) {
    for (size_t i=0;i<tokens.size();) {
        if (tokens[i] == "<" && i+1 < tokens.size()) {
            r.inPath = tokens[i+1]; tokens.erase(tokens.begin()+i, tokens.begin()+i+2); continue;
        } else if (tokens[i] == ">>" && i+1 < tokens.size()) {
            r.outPath = tokens[i+1]; r.append = true; tokens.erase(tokens.begin()+i, tokens.begin()+i+2); continue;
        } else if (tokens[i] == ">" && i+1 < tokens.size()) {
            r.outPath = tokens[i+1]; r.append = false; tokens.erase(tokens.begin()+i, tokens.begin()+i+2); continue;
        }
        ++i;
    }
}

static bool spawn_proc(const std::vector<std::string>& argv,
                       HANDLE hIn, HANDLE hOut,
                       PROCESS_INFORMATION& pi)
{
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = hIn  ? hIn  : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hOut ? hOut : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    std::vector<std::string> withPath = argv;
    withPath[0] = find_on_path(withPath[0]);
    std::string cmdline = join_cmdline(withPath);
    std::vector<char> buf(cmdline.begin(), cmdline.end());
    buf.push_back('\0');

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    BOOL ok = CreateProcessA(
        nullptr, buf.data(),
        nullptr, nullptr,
        TRUE, 0, nullptr, nullptr,
        &si, &pi
    );
    return ok == TRUE;
}

static bool exec_pipeline(std::vector<std::string> argv) {
    // Split by '|'
    std::vector<std::vector<std::string>> stages(1);
    for (auto& t : argv) {
        if (t == "|") stages.emplace_back();
        else stages.back().push_back(t);
    }

    // Builtins (single stage)
    if (stages.size() == 1 && !stages[0].empty()) {
        const std::string& cmd = stages[0][0];
        if (cmd == "cd") {
            if (stages[0].size() > 1) {
                try { fs::current_path(stages[0][1]); }
                catch (...) { std::cerr << "cd: cannot access " << stages[0][1] << "\n"; }
            }
            return true;
        } else if (cmd == "pwd") {
            std::cout << fs::current_path().string() << "\n"; return true;
        } else if (cmd == "clear" || cmd == "cls") {
            system("cls"); return true;
        } else if (cmd == "exit" || cmd == "quit") {
            std::cout << "Goodbye.\n"; exit(0);
        }
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    std::vector<PROCESS_INFORMATION> procs(stages.size());
    HANDLE hPrevRead = nullptr;

    for (size_t i = 0; i < stages.size(); ++i) {
        std::vector<std::string> args = stages[i];
        Redir r{}; split_redirs(args, r);

        for (auto& a : args) a = normalize_path(expand_env_once(a));
        args = expand_globs(args);
        if (args.empty()) return true;

        HANDLE hIn = hPrevRead;
        HANDLE hOut = nullptr;
        HANDLE hFileIn = nullptr, hFileOut = nullptr;

        if (i == 0 && !r.inPath.empty()) {
            hFileIn = CreateFileA(r.inPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFileIn == INVALID_HANDLE_VALUE) {
                std::cerr << "Cannot open input: " << r.inPath << "\n";
                if (hPrevRead) CloseHandle(hPrevRead);
                return true;
            }
            hIn = hFileIn;
        }

        if (i == stages.size()-1 && !r.outPath.empty()) {
            DWORD disp = r.append ? OPEN_ALWAYS : CREATE_ALWAYS;
            hFileOut = CreateFileA(r.outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                   &sa, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFileOut == INVALID_HANDLE_VALUE) {
                std::cerr << "Cannot open output: " << r.outPath << "\n";
                if (hPrevRead) CloseHandle(hPrevRead);
                if (hFileIn && hFileIn!=INVALID_HANDLE_VALUE) CloseHandle(hFileIn);
                return true;
            }
            if (r.append) SetFilePointer(hFileOut, 0, nullptr, FILE_END);
            hOut = hFileOut;
        }

        HANDLE thisRead = nullptr, thisWrite = nullptr;
        if (i < stages.size()-1) {
            if (!CreatePipe(&thisRead, &thisWrite, &sa, 0)) {
                std::cerr << "CreatePipe failed.\n";
                if (hPrevRead) CloseHandle(hPrevRead);
                if (hFileIn && hFileIn!=INVALID_HANDLE_VALUE) CloseHandle(hFileIn);
                if (hFileOut && hFileOut!=INVALID_HANDLE_VALUE) CloseHandle(hFileOut);
                return true;
            }
            hOut = thisWrite;
        }

        PROCESS_INFORMATION pi{};
        if (!spawn_proc(args, hIn, hOut, pi)) {
            std::cerr << "Error: failed to start: " << args[0] << "\n";
            if (hPrevRead) CloseHandle(hPrevRead);
            if (hFileIn && hFileIn!=INVALID_HANDLE_VALUE) CloseHandle(hFileIn);
            if (hFileOut && hFileOut!=INVALID_HANDLE_VALUE) CloseHandle(hFileOut);
            if (thisRead) CloseHandle(thisRead);
            if (thisWrite) CloseHandle(thisWrite);
            return true;
        }
        procs[i] = pi;

        if (hIn && hIn != GetStdHandle(STD_INPUT_HANDLE)) CloseHandle(hIn);
        if (hOut && hOut != GetStdHandle(STD_OUTPUT_HANDLE)) CloseHandle(hOut);

        hPrevRead = thisRead;
        CloseHandle(pi.hThread);
    }

    if (hPrevRead) CloseHandle(hPrevRead);
    for (auto& p : procs) {
        WaitForSingleObject(p.hProcess, INFINITE);
        CloseHandle(p.hProcess);
    }
    return true;
}

// ========== Deep TAB completion ==========
static std::vector<std::string> complete_path(const std::string& token) {
    std::string norm = token;
    std::replace(norm.begin(), norm.end(), '\\', '/');

    size_t slash = norm.find_last_of('/');
    std::string dirPart = (slash == std::string::npos) ? "" : norm.substr(0, slash + 1);
    std::string base    = (slash == std::string::npos) ? norm : norm.substr(slash + 1);

    fs::path dir = dirPart.empty() ? fs::current_path() : fs::path(dirPart);
    std::vector<std::string> matches;

    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return matches;

    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        std::string name = e.path().filename().string();
        if (name.rfind(base, 0) == 0) {
            std::string full = dirPart + name;
            if (e.is_directory(ec)) full += "/";
            matches.push_back(full);
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

static void tab_complete(const std::string& p, std::string& buf, size_t& cursor) {
    size_t start = buf.find_last_of(" \t", cursor ? cursor - 1 : 0);
    if (start == std::string::npos) start = 0; else start += 1;
    std::string token = buf.substr(start, cursor - start);

    auto matches = complete_path(token);
    if (matches.empty()) return;

    if (matches.size() == 1) {
        const std::string& m = matches[0];
        buf.erase(start, token.size());
        buf.insert(start, m);
        cursor = start + m.size();
        redraw_line(p, buf, cursor);
    } else {
        std::cout << "\n";
        int col = 0;
        for (const auto& m : matches) {
            std::cout << m << "\t";
            if (++col % 4 == 0) std::cout << "\n";
        }
        std::cout << "\n";
        redraw_line(p, buf, cursor);
    }
}

// ========== Line editor ==========
static std::string edit_line(const std::string& p,
                             std::vector<std::string>& history,
                             int& histIndex)
{
    std::string buf;
    size_t cursor = 0;
    std::cout << p << std::flush;

    while (true) {
        int ch = _getch();

        if (ch == 13) { // ENTER
            std::cout << "\n";
            return buf;
        }
        else if (ch == 8) { // BACKSPACE
            if (cursor > 0) {
                buf.erase(buf.begin() + (cursor - 1));
                --cursor;
                redraw_line(p, buf, cursor);
            }
        }
        else if (ch == 9) { // TAB
            tab_complete(p, buf, cursor);
        }
        else if (ch == 224 || ch == 0) { // extended keys
            int code = _getch();
            if (code == 75) { // LEFT
                if (cursor > 0) { --cursor; redraw_line(p, buf, cursor); }
            } else if (code == 77) { // RIGHT
                if (cursor < buf.size()) { ++cursor; redraw_line(p, buf, cursor); }
            } else if (code == 71) { // HOME
                cursor = 0; redraw_line(p, buf, cursor);
            } else if (code == 79) { // END
                cursor = buf.size(); redraw_line(p, buf, cursor);
            } else if (code == 83) { // DEL
                if (cursor < buf.size()) { buf.erase(buf.begin() + cursor); redraw_line(p, buf, cursor); }
            } else if (code == 72) { // UP
                if (histIndex > 0) {
                    histIndex--;
                    buf = history[histIndex];
                    cursor = buf.size();
                    redraw_line(p, buf, cursor);
                }
            } else if (code == 80) { // DOWN
                if (histIndex + 1 < (int)history.size()) {
                    histIndex++;
                    buf = history[histIndex];
                    cursor = buf.size();
                } else {
                    histIndex = (int)history.size();
                    buf.clear();
                    cursor = 0;
                }
                redraw_line(p, buf, cursor);
            }
        }
        else if (ch == 1) { // Ctrl+A
            cursor = 0; redraw_line(p, buf, cursor);
        }
        else if (ch == 5) { // Ctrl+E
            cursor = buf.size(); redraw_line(p, buf, cursor);
        }
        else if (ch == 21) { // Ctrl+U
            buf.erase(0, cursor);
            cursor = 0; redraw_line(p, buf, cursor);
        }
        else if (ch == 11) { // Ctrl+K
            buf.erase(cursor);
            redraw_line(p, buf, cursor);
        }
        else if (std::isprint((unsigned char)ch)) {
            buf.insert(buf.begin() + cursor, (char)ch);
            ++cursor;
            redraw_line(p, buf, cursor);
        }
    }
}

// ========== Command line -> Exec ==========
static bool run_command_line(const std::string& lineRaw) {
    if (lineRaw.empty()) return true;
    std::string line = expand_env_once(lineRaw);
    auto tokens = tokenize(line);
    for (auto &t : tokens) t = normalize_path(t);

    if (!tokens.empty() && tokens[0] == "cd" && tokens.size() == 1) {
        std::cout << fs::current_path().string() << "\n"; return true;
    }
    if (!tokens.empty() && (tokens[0] == "exit" || tokens[0] == "quit")) {
        std::cout << "Goodbye.\n"; exit(0);
    }
    if (!tokens.empty() && (tokens[0] == "clear" || tokens[0] == "cls") && tokens.size()==1) {
        system("cls"); return true;
    }

    return exec_pipeline(tokens);
}

// ========== History ==========
static void load_history(std::vector<std::string>& history, const std::string& path, size_t cap=500) {
    std::ifstream f(path);
    std::string line;
    std::vector<std::string> tmp;
    while (std::getline(f, line)) if (!line.empty()) tmp.push_back(line);
    if (tmp.size() > cap) tmp.erase(tmp.begin(), tmp.begin() + (tmp.size()-cap));
    history = std::move(tmp);
}

static void save_history(const std::vector<std::string>& history, const std::string& path, size_t cap=500) {
    std::ofstream f(path, std::ios::trunc);
    size_t start = history.size() > cap ? history.size() - cap : 0;
    for (size_t i=start;i<history.size();++i) f << history[i] << "\n";
}

// ========== main ==========
int main() {
    enable_vt_mode();
    std::cout << "Winix Shell v1.7 (globbing + deep TAB + pipes)\n";

    std::vector<std::string> history;
    load_history(history, "winix_history.txt", 500);
    int histIndex = (int)history.size();

    while (true) {
        std::string p = prompt();
        std::string line = edit_line(p, history, histIndex);
        if (line.empty()) continue;
        history.push_back(line);
        histIndex = (int)history.size();
        run_command_line(line);
        save_history(history, "winix_history.txt", 500);
    }
}
