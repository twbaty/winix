// Winix Shell v1.9 — POSIX Parity (phase 1)
// Additions over v1.8:
// - Path-aware, recursive-ish TAB completion (subdirectory traversal)
// - Contextual completion for `cd` (directories only)
// - ~, %VAR%, $VAR and ${VAR} expansion (runtime + completion)
// - `set VAR=value`, `set` (list), `export [VAR[=value]]`
// - Persistent env via ~/.winixrc (simple KEY=VALUE lines)
// - cd arg now honors env/tilde expansion

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
#include <unordered_set>

namespace fs = std::filesystem;

// Track env keys we set/export to persist into ~/.winixrc
static std::unordered_set<std::string> g_modified_env;

// ========== Helpers: home dir, rc path ==========
static std::string getenv_str(const char* k) {
    const char* v = std::getenv(k); return v ? std::string(v) : std::string();
}

static std::string home_dir() {
    std::string h = getenv_str("USERPROFILE");
    if (h.empty()) h = getenv_str("HOME");
    if (h.empty()) {
        try { h = fs::current_path().string(); } catch (...) { h = "."; }
    }
    return h;
}

static std::string rc_path() {
    return (fs::path(home_dir()) / ".winixrc").string();
}

static void load_rc() {
    std::ifstream f(rc_path());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0] == '#' || line[0] == ';') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0, pos);
        std::string v = line.substr(pos + 1);
        // trim spaces around k
        auto ltrim = [](std::string& s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch){return !std::isspace(ch);})); };
        auto rtrim = [](std::string& s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch){return !std::isspace(ch);}).base(), s.end()); };
        ltrim(k); rtrim(k);
        // don't trim value; allow spaces
        if (!k.empty()) {
            _putenv_s(k.c_str(), v.c_str());
        }
    }
}

static void persist_env_to_rc() {
    if (g_modified_env.empty()) return;
    // Merge existing ~/.winixrc with modified entries
    std::map<std::string, std::string> mapkv;
    {
        std::ifstream f(rc_path());
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0]=='#' || line[0]==';') continue;
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string k = line.substr(0,pos);
            std::string v = line.substr(pos+1);
            // trim key
            auto ltrim = [](std::string& s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch){return !std::isspace(ch);})); };
            auto rtrim = [](std::string& s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch){return !std::isspace(ch);}).base(), s.end()); };
            ltrim(k); rtrim(k);
            mapkv[k] = v;
        }
    }
    for (auto& k : g_modified_env) {
        std::string v = getenv_str(k.c_str());
        mapkv[k] = v; // overwrite/add
    }
    // write back
    std::ofstream out(rc_path(), std::ios::trunc);
    out << "# ~/.winixrc — persisted variables from Winix\n";
    for (auto& [k,v] : mapkv) out << k << "=" << v << "\n";
}

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

static void redraw_line(const std::string& promptStr, const std::string& buf, size_t cursor) {
    std::cout << "\r\033[K" << promptStr << buf;
    size_t target = promptStr.size() + cursor;
    size_t current = promptStr.size() + buf.size();
    if (current > target) {
        std::cout << std::string(current - target, '\b');
    }
    std::cout.flush();
}

// ========== Utility ==========
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
        for (char c : a) {
            if (c == '"') oss << '\\';
            oss << c;
        }
        oss << '"';
    }
    return oss.str();
}

// Percent expansion (legacy)
static std::string expand_percent_vars(const std::string& in) {
    std::string out; out.reserve(in.size());
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

// $VAR and ${VAR}
static std::string expand_dollar_vars(const std::string& in) {
    std::string out; out.reserve(in.size());
    for (size_t i=0;i<in.size();) {
        if (in[i] == '$') {
            if (i+1 < in.size() && in[i+1] == '{') {
                size_t j = in.find('}', i+2);
                if (j != std::string::npos) {
                    std::string name = in.substr(i+2, j-(i+2));
                    const char* v = std::getenv(name.c_str());
                    if (v) out += v;
                    i = j+1; continue;
                }
            }
            // $NAME pattern
            size_t j = i+1;
            while (j < in.size() && (std::isalnum((unsigned char)in[j]) || in[j]=='_')) j++;
            if (j > i+1) {
                std::string name = in.substr(i+1, j-(i+1));
                const char* v = std::getenv(name.c_str());
                if (v) out += v;
                i = j; continue;
            }
        }
        out.push_back(in[i++]);
    }
    return out;
}

static std::string expand_tilde(const std::string& in) {
    if (in.empty()) return in;
    // Only expand leading ~ or ~\\ or ~/
    if (in[0] == '~') {
        std::string rest = in.substr(1);
        return home_dir() + rest;
    }
    return in;
}

static std::string expand_all_once(const std::string& in) {
    // Order: ~ then % then $
    return expand_dollar_vars(expand_percent_vars(expand_tilde(in)));
}

// ========== Tokenizer (quotes, escapes) ==========
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

// ========== Globbing (* ?) ==========
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

    // Support patterns with directory components
    fs::path p(token);
    fs::path dir = p.has_parent_path() ? p.parent_path() : fs::current_path();
    std::string pat = p.filename().string();

    std::vector<std::string> matches;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        std::string name = e.path().filename().string();
        if (match_wild(name, pat)) {
            matches.push_back((dir / name).string());
        }
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
    if (cmd.empty()) return cmd;
    if (is_exe_path(cmd)) {
        std::string p = cmd;
        if (!ends_with_casei(p, ".exe")) p += ".exe";
        if (fs::exists(p)) return p;
        return cmd;
    }
    char* envp = std::getenv("PATH");
    std::string path = envp ? envp : "";
    std::vector<std::string> dirs;
    std::stringstream ss(path);
    std::string item;
    while (std::getline(ss, item, ';')) if (!item.empty()) dirs.push_back(item);

    auto try_file = [&](const std::string& base)->std::string{
        if (fs::exists(base)) return base;
        return {};
    };

    for (auto& d : dirs) {
        std::string base1 = (fs::path(d) / cmd).string();
        std::string base2 = base1 + ".exe";
        if (auto p = try_file(base2); !p.empty()) return p;
        if (auto p = try_file(base1); !p.empty()) return p;
    }
    if (fs::exists(cmd)) return cmd;
    if (fs::exists(cmd + ".exe")) return cmd + ".exe";
    return cmd;
}

// ========== Pipes & Redirection ==========
struct Redir {
    std::string inPath;
    std::string outPath;
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
        nullptr,
        buf.data(),
        nullptr, nullptr,
        TRUE,
        0, nullptr, nullptr,
        &si, &pi
    );
    return ok == TRUE;
}

// Fallback: run whole line through CMD (DOS builtins, &&, ||, etc.)
static bool run_via_cmd(const std::string& wholeLine) {
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    std::string full = "cmd.exe /C " + wholeLine;
    std::vector<char> buf(full.begin(), full.end());
    buf.push_back('\0');

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(
        nullptr, buf.data(),
        nullptr, nullptr,
        TRUE, 0, nullptr, nullptr,
        &si, &pi
    );
    if (!ok) return false;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    return true;
}

static bool exec_pipeline(std::vector<std::string> argv) {
    // Split by '|'
    std::vector<std::vector<std::string>> stages;
    stages.emplace_back();
    for (auto& t : argv) {
        if (t == "|") stages.emplace_back();
        else stages.back().push_back(t);
    }

    // Built-ins (only single stage)
    if (stages.size() == 1 && !stages[0].empty()) {
        const std::string& cmd = stages[0][0];
        if (cmd == "cd") {
            if (stages[0].size() > 1) {
                std::string target = expand_all_once(stages[0][1]);
                try { fs::current_path(target); }
                catch (...) { std::cerr << "cd: cannot access " << stages[0][1] << "\n"; }
            } else {
                std::cout << fs::current_path().string() << "\n";
            }
            return true;
        }
        if (cmd == "clear" || cmd == "cls") {
            system("cls");
            return true;
        }
        if (cmd == "exit" || cmd == "quit") {
            std::cout << "Goodbye.\n";
            persist_env_to_rc();
            exit(0);
        }
        if (cmd == "set") {
            if (stages[0].size() == 1) {
                // list env
                LPCH env = GetEnvironmentStringsA();
                if (env) {
                    for (LPCH v = env; *v; ) {
                        std::string line(v);
                        std::cout << line << "\n";
                        v += line.size() + 1;
                    }
                    FreeEnvironmentStringsA(env);
                }
                return true;
            } else {
                // allow: set VAR=value (no spaces around '=')
                for (size_t i=1;i<stages[0].size();++i) {
                    const std::string &arg = stages[0][i];
                    auto pos = arg.find('=');
                    if (pos == std::string::npos) {
                        std::cerr << "set: expected VAR=value, got '" << arg << "'\n";
                        continue;
                    }
                    std::string k = arg.substr(0,pos);
                    std::string v = arg.substr(pos+1);
                    _putenv_s(k.c_str(), v.c_str());
                    g_modified_env.insert(k);
                }
                return true;
            }
        }
        if (cmd == "export") {
            // export VAR or export VAR=value
            if (stages[0].size() == 1) {
                // show exported we track
                for (auto &k : g_modified_env) std::cout << k << "=" << getenv_str(k.c_str()) << "\n";
                return true;
            }
            for (size_t i=1;i<stages[0].size();++i) {
                std::string a = stages[0][i];
                auto pos = a.find('=');
                if (pos == std::string::npos) {
                    // ensure exists
                    std::string v = getenv_str(a.c_str());
                    _putenv_s(a.c_str(), v.c_str());
                    g_modified_env.insert(a);
                } else {
                    std::string k = a.substr(0,pos);
                    std::string v = a.substr(pos+1);
                    _putenv_s(k.c_str(), v.c_str());
                    g_modified_env.insert(k);
                }
            }
            return true;
        }
    }

    // Prepare pipes and spawn each stage
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    std::vector<PROCESS_INFORMATION> procs(stages.size());
    HANDLE hPrevRead = nullptr;

    for (size_t i = 0; i < stages.size(); ++i) {
        std::vector<std::string> args = stages[i];
        Redir r{};
        split_redirs(args, r);

        // env + glob expansion (post redir-strip)
        for (auto& a : args) a = expand_all_once(a);
        args = expand_globs(args);
        if (args.empty()) return true;

        // std handles for this stage
        HANDLE hIn = hPrevRead;
        HANDLE hOut = nullptr;

        HANDLE hFileIn = nullptr, hFileOut = nullptr;

        if (i == 0 && !r.inPath.empty()) {
            std::string inPath = expand_all_once(r.inPath);
            hFileIn = CreateFileA(inPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFileIn == INVALID_HANDLE_VALUE) {
                std::cerr << "Cannot open input: " << r.inPath << "\n";
                if (hPrevRead) CloseHandle(hPrevRead);
                return true;
            }
            hIn = hFileIn;
        }

        if (i == stages.size()-1 && !r.outPath.empty()) {
            std::string outPath = expand_all_once(r.outPath);
            DWORD disp = r.append ? OPEN_ALWAYS : CREATE_ALWAYS;
            hFileOut = CreateFileA(outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                   &sa, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFileOut == INVALID_HANDLE_VALUE) {
                std::cerr << "Cannot open output: " << r.outPath << "\n";
                if (hPrevRead) CloseHandle(hPrevRead);
                if (hFileIn && hFileIn != INVALID_HANDLE_VALUE) CloseHandle(hFileIn);
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
                if (hFileIn && hFileIn != INVALID_HANDLE_VALUE) CloseHandle(hFileIn);
                if (hFileOut && hFileOut != INVALID_HANDLE_VALUE) CloseHandle(hFileOut);
                return true;
            }
            hOut = thisWrite;
        }

        PROCESS_INFORMATION pi{};
        if (!spawn_proc(args, hIn, hOut, pi)) {
            // If single-stage and spawn failed, try CMD fallback (handles DOS builtins)
            if (stages.size() == 1) {
                if (hPrevRead) CloseHandle(hPrevRead);
                if (thisRead) CloseHandle(thisRead);
                if (thisWrite) CloseHandle(thisWrite);
                if (hFileIn && hFileIn != INVALID_HANDLE_VALUE) CloseHandle(hFileIn);
                if (hFileOut && hFileOut != INVALID_HANDLE_VALUE) CloseHandle(hFileOut);
                return false; // let caller run_via_cmd(line)
            }
            std::cerr << "Error: failed to start: " << args[0] << "\n";
            if (hPrevRead) CloseHandle(hPrevRead);
            if (thisRead) CloseHandle(thisRead);
            if (thisWrite) CloseHandle(thisWrite);
            if (hFileIn && hFileIn != INVALID_HANDLE_VALUE) CloseHandle(hFileIn);
            if (hFileOut && hFileOut != INVALID_HANDLE_VALUE) CloseHandle(hFileOut);
            return true;
        }
        procs[i] = pi;

        // Parent closes our copies no longer needed
        if (hIn && hIn != GetStdHandle(STD_INPUT_HANDLE)) CloseHandle(hIn);
        if (hOut && hOut != GetStdHandle(STD_OUTPUT_HANDLE)) CloseHandle(hOut);

        // Important: close our write-end immediately so downstream sees EOF correctly
        if (thisWrite) { CloseHandle(thisWrite); }

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

// ========== Quote & Context-aware TAB completion ==========

static bool is_dir_safe(const fs::path& p) {
    std::error_code ec; return fs::is_directory(p, ec);
}

static std::vector<std::string> list_matches_in_dir(const fs::path& dir, const std::string& base, bool dirsOnly) {
    std::vector<std::string> v;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        std::string name = e.path().filename().string();
        if (name.rfind(base, 0) == 0) {
            if (dirsOnly && !is_dir_safe(e.path())) continue;
            v.push_back(name + (is_dir_safe(e.path()) ? std::string(1, fs::path::preferred_separator) : std::string()));
        }
    }
    std::sort(v.begin(), v.end());
    return v;
}

static fs::path resolve_for_completion(const std::string& rawPrefix, std::string& baseOut) {
    // Expand ~, %VAR%, $VAR for directory resolution only
    std::string expanded = expand_all_once(rawPrefix);
    fs::path p(expanded);
    if (rawPrefix.empty()) { baseOut.clear(); return fs::current_path(); }

    if (p.has_parent_path()) {
        baseOut = p.filename().string();
        return p.parent_path();
    } else {
        baseOut = expanded; // no separator present: search in CWD
        return fs::current_path();
    }
}

static void insert_completion(std::string& buf, size_t tokenStart, size_t& cursor, const std::string& completedTail) {
    buf.insert(cursor, completedTail);
    cursor += completedTail.size();
}

static void tab_complete(std::string& buf, size_t& cursor) {
    // Determine if we're completing argument to `cd`
    // Tokenize up to cursor to infer command context
    std::string upto = buf.substr(0, cursor);
    auto toks = tokenize(upto);
    bool cdContext = (!toks.empty() && toks[0] == "cd");

    // Find token boundaries considering quotes
    bool inQuotes = false;
    size_t tokenStart = 0;
    for (size_t i = 0; i < cursor; ++i) {
        if (buf[i] == '"') inQuotes = !inQuotes;
        if (!inQuotes && (buf[i] == ' ' || buf[i] == '\t')) tokenStart = i + 1;
    }
    bool tokenQuoted = false;
    if (tokenStart < buf.size() && buf[tokenStart] == '"') {
        tokenQuoted = true;
        tokenStart++;
    }

    std::string token = buf.substr(tokenStart, cursor - tokenStart);

    // Path-aware completion: resolve directory, then match base
    std::string base;
    fs::path dir = resolve_for_completion(token, base);
    bool dirsOnly = cdContext; // contextual filter

    auto matches = list_matches_in_dir(dir, base, dirsOnly);
    if (matches.empty()) return;

    // If only one match, insert its tail relative to typed token
    if (matches.size() == 1) {
        const std::string& full = matches[0];
        // add only the part after current base
        std::string tail = full.substr(base.size());
        insert_completion(buf, tokenStart, cursor, tail);
        redraw_line(prompt(), buf, cursor);
        return;
    }

    // Many matches — print in columns
    std::cout << "\n";
    int col = 0;
    for (auto &m : matches) {
        std::cout << m << "\t";
        if (++col % 4 == 0) std::cout << "\n";
    }
    std::cout << "\n";
    redraw_line(prompt(), buf, cursor);
}

// ========== Line editor ==========
static std::string edit_line(const std::string& promptStr,
                             std::vector<std::string>& history,
                             int& histIndex)
{
    std::string buf;
    size_t cursor = 0;

    std::cout << promptStr << std::flush;

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
                redraw_line(promptStr, buf, cursor);
            }
        }
        else if (ch == 9) { // TAB
            tab_complete(buf, cursor);
            redraw_line(promptStr, buf, cursor);
        }
        else if (ch == 224 || ch == 0) { // extended keys
            int code = _getch();
            if (code == 75) { // LEFT
                if (cursor > 0) { --cursor; redraw_line(promptStr, buf, cursor); }
            } else if (code == 77) { // RIGHT
                if (cursor < buf.size()) { ++cursor; redraw_line(promptStr, buf, cursor); }
            } else if (code == 71) { // HOME
                cursor = 0; redraw_line(promptStr, buf, cursor);
            } else if (code == 79) { // END
                cursor = buf.size(); redraw_line(promptStr, buf, cursor);
            } else if (code == 83) { // DEL
                if (cursor < buf.size()) {
                    buf.erase(buf.begin() + cursor);
                    redraw_line(promptStr, buf, cursor);
                }
            } else if (code == 72) { // UP
                if (histIndex > 0) {
                    histIndex--;
                    buf = history[histIndex];
                    cursor = buf.size();
                    redraw_line(promptStr, buf, cursor);
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
                redraw_line(promptStr, buf, cursor);
            }
        }
        else if (ch == 1) { // Ctrl+A
            cursor = 0; redraw_line(promptStr, buf, cursor);
        }
        else if (ch == 5) { // Ctrl+E
            cursor = buf.size(); redraw_line(promptStr, buf, cursor);
        }
        else if (ch == 21) { // Ctrl+U
            buf.erase(0, cursor); cursor = 0; redraw_line(promptStr, buf, cursor);
        }
        else if (ch == 11) { // Ctrl+K
            buf.erase(cursor); redraw_line(promptStr, buf, cursor);
        }
        else if (std::isprint((unsigned char)ch)) {
            buf.insert(buf.begin() + cursor, (char)ch);
            ++cursor;
            redraw_line(promptStr, buf, cursor);
        }
    }
}

// ========== Command Line -> Pipeline Exec with CMD fallback ==========
static bool run_command_line(const std::string& lineRaw) {
    if (lineRaw.empty()) return true;

    // Quick pass: if the user typed explicit CMD metachars (&, &&, ||, parentheses),
    // just hand the whole thing to CMD so behavior matches Windows.
    if (lineRaw.find('&') != std::string::npos ||
        lineRaw.find("||") != std::string::npos ||
        lineRaw.find('(')  != std::string::npos ||
        lineRaw.find(')')  != std::string::npos)
    {
        return run_via_cmd(lineRaw);
    }

    std::string line = expand_all_once(lineRaw);
    auto tokens = tokenize(line);

    // Builtins fast-path (no pipes) handled inside exec_pipeline

    // Execute (pipes + redirs inside)
    // If single-stage spawn fails (e.g., 'dir'), we route whole line to CMD.
    bool ok = exec_pipeline(tokens);
    if (!ok) return run_via_cmd(lineRaw);
    return true;
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
    load_rc();
    std::cout << "Winix Shell v1.9 (TAB path-aware, env vars, ~/.winixrc)\n";

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
