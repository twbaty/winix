// Winix Shell v1.10 — Native Command Logic (&&, ||, & background)
// Based on v1.9: retains path-aware completion, env vars, ~/.winixrc
// New in v1.10:
//  - Native parsing/evaluation of &&, ||, and & (background) — no cmd.exe fallback
//  - Background jobs registry + `jobs` builtin (basic)
//  - Proper pipeline exit status propagation (last stage)

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
#include <map>

namespace fs = std::filesystem;

// Track env keys we set/export to persist into ~/.winixrc
static std::unordered_set<std::string> g_modified_env;

struct Job {
    int id;
    std::vector<PROCESS_INFORMATION> procs;
};
static std::vector<Job> g_jobs;
static int g_next_job_id = 1;

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
        auto ltrim = [](std::string& s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch){return !std::isspace(ch);})); };
        auto rtrim = [](std::string& s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch){return !std::isspace(ch);}).base(), s.end()); };
        ltrim(k); rtrim(k);
        if (!k.empty()) { _putenv_s(k.c_str(), v.c_str()); }
    }
}

static void persist_env_to_rc() {
    if (g_modified_env.empty()) return;
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
            auto ltrim = [](std::string& s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch){return !std::isspace(ch);})); };
            auto rtrim = [](std::string& s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch){return !std::isspace(ch);}).base(), s.end()); };
            ltrim(k); rtrim(k);
            mapkv[k] = v;
        }
    }
    for (auto& k : g_modified_env) { mapkv[k] = getenv_str(k.c_str()); }
    std::ofstream out(rc_path(), std::ios::trunc);
    out << "# ~/.winixrc — persisted variables from Winix
";
    for (auto& kv : mapkv) out << kv.first << "=" << kv.second << "
";
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
    return "[1;32m[Winix][0m " + fs::current_path().string() + " > ";
}

static void redraw_line(const std::string& promptStr, const std::string& buf, size_t cursor) {
    std::cout << "
[K" << promptStr << buf;
    size_t target = promptStr.size() + cursor;
    size_t current = promptStr.size() + buf.size();
    if (current > target) { std::cout << std::string(current - target, ''); }
    std::cout.flush();
}

// ========== Utility ==========
static bool is_exe_path(const std::string& s) {
    return s.find('\') != std::string::npos || s.find('/') != std::string::npos;
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
        bool needQuote = a.find_first_of(" 	\"&|<>^") != std::string::npos;
        if (i) oss << ' ';
        if (!needQuote) { oss << a; continue; }
        oss << '"';
        for (char c : a) { if (c == '"') oss << '\'; oss << c; }
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
                i = j+1; continue;
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
    if (in[0] == '~') { std::string rest = in.substr(1); return home_dir() + rest; }
    return in;
}

static std::string expand_all_once(const std::string& in) {
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
        if (!inQuotes && (c=='&' || c=='|')) {
            // capture && and || specially; keep single & and | too
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            if (i+1 < line.size() && line[i+1]==c) {
                out.emplace_back(std::string(2,c));
                ++i;
            } else {
                out.emplace_back(std::string(1,c));
            }
            continue;
        }
        if (c == '\' && i+1 < line.size()) { cur.push_back(line[++i]); continue; }
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
           std::tolower((unsigned char)pat[p]) == std::tolower((unsigned char)name[n]))) { ++n; ++p; }
        else if (p < pat.size() && pat[p] == '*') { star = p++; ss = n; }
        else if (star != std::string::npos) { p = star + 1; n = ++ss; }
        else return false;
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

static std::vector<std::string> expand_globs_one(const std::string& token) {
    if (token.find('*') == std::string::npos && token.find('?') == std::string::npos) return { token };
    fs::path p(token);
    fs::path dir = p.has_parent_path() ? p.parent_path() : fs::current_path();
    std::string pat = p.filename().string();
    std::vector<std::string> matches;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        std::string name = e.path().filename().string();
        if (match_wild(name, pat)) matches.push_back((dir / name).string());
    }
    if (matches.empty()) return { token };
    std::sort(matches.begin(), matches.end());
    return matches;
}

static std::vector<std::string> expand_globs(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    for (auto& a : args) { auto v = expand_globs_one(a); out.insert(out.end(), v.begin(), v.end()); }
    return out;
}

// ========== PATH search ==========
static std::string find_on_path(const std::string& cmd) {
    if (cmd.empty()) return cmd;
    if (is_exe_path(cmd)) {
        std::string p = cmd; if (!ends_with_casei(p, ".exe")) p += ".exe";
        if (fs::exists(p)) return p; return cmd;
    }
    char* envp = std::getenv("PATH");
    std::string path = envp ? envp : "";
    std::vector<std::string> dirs;
    std::stringstream ss(path);
    std::string item;
    while (std::getline(ss, item, ';')) if (!item.empty()) dirs.push_back(item);

    auto try_file = [&](const std::string& base)->std::string{ if (fs::exists(base)) return base; return {}; };

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
struct Redir { std::string inPath; std::string outPath; bool append = false; };

static void split_redirs(std::vector<std::string>& tokens, Redir& r) {
    for (size_t i=0;i<tokens.size();) {
        if (tokens[i] == "<" && i+1 < tokens.size()) { r.inPath = tokens[i+1]; tokens.erase(tokens.begin()+i, tokens.begin()+i+2); continue; }
        else if (tokens[i] == ">>" && i+1 < tokens.size()) { r.outPath = tokens[i+1]; r.append = true; tokens.erase(tokens.begin()+i, tokens.begin()+i+2); continue; }
        else if (tokens[i] == ">" && i+1 < tokens.size()) { r.outPath = tokens[i+1]; r.append = false; tokens.erase(tokens.begin()+i, tokens.begin()+i+2); continue; }
        ++i;
    }
}

static bool spawn_proc(const std::vector<std::string>& argv, HANDLE hIn, HANDLE hOut, PROCESS_INFORMATION& pi) {
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
    buf.push_back('
