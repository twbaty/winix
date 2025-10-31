// Winix Shell v1.12 — Bash++ aliases, env persist, background & chains
// Tom: single-file drop-in. Tested with MinGW-w64 on Windows 10/11.

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

// ---------- Globals ----------
static std::map<std::string, std::string> g_aliases;
static std::unordered_set<std::string> g_session_env_modified; // vars set this session
static bool g_aliases_dirty = false;

// ---------- Helpers ----------
static inline std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e-1]))) --e;
    return s.substr(b, e-b);
}

static std::string home_dir() {
    char* up = nullptr;
    size_t len = 0;
    _dupenv_s(&up, &len, "USERPROFILE");
    std::string h = (up && *up) ? std::string(up) : std::string();
    if (up) free(up);
    if (h.empty()) {
        // Fallback to HOMEDRIVE+HOMEPATH
        char* hd = nullptr; char* hp = nullptr; size_t l1=0,l2=0;
        _dupenv_s(&hd,&l1,"HOMEDRIVE"); _dupenv_s(&hp,&l2,"HOMEPATH");
        if (hd && hp) h = std::string(hd) + std::string(hp);
        if (hd) free(hd); if (hp) free(hp);
    }
    if (h.empty()) h = "."; // last resort
    return h;
}

static fs::path rc_path()        { return fs::path(home_dir()) / ".winixrc"; }
static fs::path aliases_path()   { return fs::path(home_dir()) / ".winix_aliases"; }

// ---------- Env persistence ----------
static void load_rc() {
    std::ifstream in(rc_path());
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        // VAR=VALUE (no export keyword required; we accept both)
        // Accept optional leading "export "
        if (line.rfind("export ", 0) == 0) line = trim(line.substr(7));
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = trim(line.substr(0, pos));
        std::string v = line.substr(pos + 1);
        // allow quoted values
        if (!v.empty() && (v.front()=='"' || v.front()=='\'')) {
            char q = v.front();
            if (v.size() >= 2 && v.back() == q) v = v.substr(1, v.size()-2);
        }
        if (!k.empty()) {
            _putenv_s(k.c_str(), v.c_str());
        }
    }
}

static void persist_env_to_rc() {
    // Write a header and only what we touched this session (least surprise).
    std::ofstream out(rc_path(), std::ios::trunc);
    if (!out) return;
    out << "# ~/.winixrc — persisted variables from Winix (session-modified)\n";
    for (const auto& k : g_session_env_modified) {
        char* val = nullptr; size_t len = 0;
        if (_dupenv_s(&val, &len, k.c_str()) == 0 && val) {
            out << k << "=";
            // write quoted if it includes spaces or special chars
            bool needs_q = std::any_of(std::string(val).begin(), std::string(val).end(),
                [](char c){ return std::isspace(static_cast<unsigned char>(c)) || c=='#' || c=='"' || c=='\''; });
            if (needs_q) {
                out << '"';
                for (char c: std::string(val)) { if (c=='"') out << '\\'; out << c; }
                out << '"';
            } else {
                out << val;
            }
            out << "\n";
            free(val);
        }
    }
}

// ---------- Aliases ----------
static void load_aliases() {
    std::ifstream in(aliases_path());
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0]=='#') continue;
        // expect: alias name='value' or name=value
        if (line.rfind("alias ",0) == 0) line = trim(line.substr(6));
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string name = trim(line.substr(0,pos));
        std::string val  = trim(line.substr(pos+1));
        if (!val.empty() && (val.front()=='"' || val.front()=='\'')) {
            char q = val.front();
            if (val.size()>=2 && val.back()==q) val = val.substr(1, val.size()-2);
        }
        if (!name.empty()) g_aliases[name] = val;
    }
}

static bool save_aliases() {
    std::ofstream out(aliases_path(), std::ios::trunc);
    if (!out) return false;
    out << "# ~/.winix_aliases — saved by Winix\n";
    for (auto& [k,v] : g_aliases) {
        // quote if needed
        bool needs_q = v.find_first_of(" \t\"'|&;<>") != std::string::npos;
        out << "alias " << k << "=";
        if (needs_q) {
            out << '\'';
            for (char c: v) { if (c=='\'') out << "'\\''"; else out << c; }
            out << '\'';
        } else {
            out << v;
        }
        out << "\n";
    }
    g_aliases_dirty = false;
    return true;
}

// ---------- Variable expansion ----------
static std::string expand_percent_vars(std::string s) {
    // %VAR% style
    std::string out;
    out.reserve(s.size());
    for (size_t i=0; i<s.size(); ) {
        if (s[i]=='%') {
            size_t j = s.find('%', i+1);
            if (j != std::string::npos) {
                std::string key = s.substr(i+1, j-(i+1));
                char* v = nullptr; size_t len=0;
                if (!key.empty() && _dupenv_s(&v,&len,key.c_str())==0 && v) {
                    out += v; free(v);
                }
                i = j+1;
                continue;
            }
        }
        out.push_back(s[i++]);
    }
    return out;
}

static std::string expand_dollar_vars(const std::string& in) {
    // $VAR style (letters, digits, underscore)
    std::string out; out.reserve(in.size());
    for (size_t i=0;i<in.size();) {
        if (in[i]=='$') {
            size_t j = i+1;
            if (j<in.size() && (std::isalpha((unsigned char)in[j]) || in[j]=='_')) {
                while (j<in.size() && (std::isalnum((unsigned char)in[j]) || in[j]=='_')) ++j;
                std::string key = in.substr(i+1, j-(i+1));
                char* v=nullptr; size_t len=0;
                if (_dupenv_s(&v,&len,key.c_str())==0 && v) { out += v; free(v); }
                i = j;
                continue;
            }
        }
        out.push_back(in[i++]);
    }
    return out;
}

static std::string expand_vars_all(const std::string& in) {
    return expand_dollar_vars(expand_percent_vars(in));
}

// ---------- Tokenization ----------
enum class TokType { WORD, OP_AND, OP_OR, OP_PIPE, OP_BG, OP_SEMI, REDIR_IN, REDIR_OUT, REDIR_APP };

struct Token {
    TokType type;
    std::string text;
};

static std::vector<Token> tokenize(const std::string& line) {
    std::vector<Token> toks;
    std::string cur;
    enum State { NORM, SQ, DQ } st = NORM;

    auto flush = [&](){
        if (!cur.empty()) {
            toks.push_back({TokType::WORD, cur});
            cur.clear();
        }
    };

    for (size_t i=0;i<line.size();++i) {
        char c = line[i];
        if (st==NORM) {
            if (std::isspace((unsigned char)c)) { flush(); continue; }
            if (c=='"') { st=DQ; continue; }
            if (c=='\''){ st=SQ; continue; }

            // two-char ops
            if (c=='&' && i+1<line.size() && line[i+1]=='&') { flush(); toks.push_back({TokType::OP_AND,"&&"}); ++i; continue; }
            if (c=='|' && i+1<line.size() && line[i+1]=='|') { flush(); toks.push_back({TokType::OP_OR,"||"}); ++i; continue; }
            if (c=='>' && i+1<line.size() && line[i+1]=='>'){ flush(); toks.push_back({TokType::REDIR_APP,">>"}); ++i; continue; }

            // single-char ops
            if (c=='|') { flush(); toks.push_back({TokType::OP_PIPE,"|"}); continue; }
            if (c=='&') { flush(); toks.push_back({TokType::OP_BG,"&"}); continue; }
            if (c==';') { flush(); toks.push_back({TokType::OP_SEMI,";"}); continue; }
            if (c=='<') { flush(); toks.push_back({TokType::REDIR_IN,"<"}); continue; }
            if (c=='>') { flush(); toks.push_back({TokType::REDIR_OUT,">"}); continue; }

            if (c=='\\' && i+1<line.size()) { cur.push_back(line[++i]); continue; }
            cur.push_back(c);
        } else if (st==DQ) {
            if (c=='"') { st=NORM; continue; }
            if (c=='\\' && i+1<line.size()) { cur.push_back(line[++i]); continue; }
            cur.push_back(c);
        } else { // SQ
            if (c=='\'') { st=NORM; continue; }
            cur.push_back(c);
        }
    }
    flush();
    return toks;
}

// ---------- Alias expansion (Bash-style, first word only, with reparse) ----------
static std::string expand_alias_line_once(const std::string& line, bool& changed) {
    // Find first non-space token (simple scan, not a full tokenization yet)
    size_t i = 0;
    while (i<line.size() && std::isspace((unsigned char)line[i])) ++i;
    if (i>=line.size()) { changed=false; return line; }

    // If line starts with a comment, do nothing
    if (line[i]=='#') { changed=false; return line; }

    // Capture first token considering quotes/backslashes simply
    size_t start = i;
    bool in_s=false, in_d=false;
    for (; i<line.size(); ++i) {
        char c = line[i];
        if (in_s) { if (c=='\'') in_s=false; continue; }
        if (in_d) { if (c=='"') in_d=false; else if (c=='\\') ++i; continue; }
        if (c=='\'') { in_s=true; continue; }
        if (c=='"') { in_d=true; continue; }
        if (std::isspace((unsigned char)c) || c=='|' || c=='&' || c==';' || c=='<' || c=='>')
            break;
    }
    std::string first = line.substr(start, i-start);

    auto it = g_aliases.find(first);
    if (it == g_aliases.end()) { changed=false; return line; }

    // Replace first token with alias value and rejoin
    std::string expanded = line.substr(0, start) + it->second + line.substr(i);
    changed = true;
    return expanded;
}

static std::vector<Token> alias_expand_then_tokenize(const std::string& original) {
    std::string line = original;
    bool changed = false;
    int depth = 0;
    do {
        changed = false;
        line = expand_alias_line_once(line, changed);
        if (changed) ++depth;
    } while (changed && depth < 5); // recursion guard

    // Now tokenize the final line
    return tokenize(line);
}

// ---------- Process execution ----------
struct Redir {
    std::string in, out, app;
};

static void apply_redirs(const Redir& r, STARTUPINFOA& si, HANDLE& hIn, HANDLE& hOut, std::vector<HANDLE>& to_close) {
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    if (!r.in.empty()) {
        hIn = CreateFileA(r.in.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hIn != INVALID_HANDLE_VALUE) { si.hStdInput = hIn; to_close.push_back(hIn); }
    }
    if (!r.out.empty() || !r.app.empty()) {
        DWORD disp = r.app.empty() ? CREATE_ALWAYS : OPEN_ALWAYS;
        const std::string& path = r.app.empty() ? r.out : r.app;
        hOut = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hOut != INVALID_HANDLE_VALUE) {
            if (!r.app.empty()) SetFilePointer(hOut, 0, nullptr, FILE_END);
            si.hStdOutput = hOut; si.hStdError = hOut; to_close.push_back(hOut);
        }
    }
}

static DWORD spawn_proc(const std::string& cmdline, bool background, const Redir& r) {
    // Build command line for CreateProcessA
    std::string cl = "cmd /C " + cmdline;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    HANDLE hIn = INVALID_HANDLE_VALUE, hOut = INVALID_HANDLE_VALUE;
    std::vector<HANDLE> to_close;

    apply_redirs(r, si, hIn, hOut, to_close);

    // Inherit handles so redirs work, but do not create another window.
    DWORD flags = CREATE_NO_WINDOW;

    BOOL ok = CreateProcessA(
        nullptr,
        cl.data(), // modifiable
        nullptr, nullptr,
        TRUE, // inherit handles for redirs
        flags,
        nullptr,
        nullptr,
        &si, &pi
    );

    for (HANDLE h : to_close) { if (h && h!=INVALID_HANDLE_VALUE) CloseHandle(h); }

    if (!ok) {
        std::cerr << "Error: failed to start: " << cmdline << "\n";
        return static_cast<DWORD>(-1);
    }

    if (background) {
        std::cout << "[background] PID " << pi.dwProcessId << " started\n";
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    } else {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return code;
    }
}

// ---------- Builtins ----------
static void builtin_alias_list() {
    for (auto& [k,v] : g_aliases) {
        bool needs_q = v.find_first_of(" \t\"'|&;<>") != std::string::npos;
        std::cout << "alias " << k << "=";
        if (needs_q) {
            std::cout << '\'';
            for (char c: v) { if (c=='\'') std::cout << "'\\''"; else std::cout << c; }
            std::cout << '\'';
        } else {
            std::cout << v;
        }
        std::cout << "\n";
    }
}

static bool is_assignment(const std::string& w) {
    auto eq = w.find('=');
    if (eq == std::string::npos || eq==0) return false;
    // left must be a bare name
    for (size_t i=0;i<eq;i++) {
        char c = w[i];
        if (!(std::isalnum((unsigned char)c) || c=='_')) return false;
    }
    return true;
}

static std::string prompt() {
    try {
        return "\033[1;32m[Winix]\033[0m " + fs::current_path().string() + " > ";
    } catch (...) {
        return "\033[1;32m[Winix]\033[0m > ";
    }
}

// ---------- Parse & run ----------
struct Cmd {
    std::vector<std::string> argv;
    Redir redir;
    bool background = false;
};

static void split_redirs(std::vector<Token>& toks, Redir& r) {
    std::vector<Token> out;
    for (size_t i=0;i<toks.size();++i) {
        if ((toks[i].type==TokType::REDIR_IN || toks[i].type==TokType::REDIR_OUT || toks[i].type==TokType::REDIR_APP) && i+1<toks.size() && toks[i+1].type==TokType::WORD) {
            if (toks[i].type==TokType::REDIR_IN)  r.in  = toks[i+1].text;
            if (toks[i].type==TokType::REDIR_OUT) r.out = toks[i+1].text;
            if (toks[i].type==TokType::REDIR_APP) r.app = toks[i+1].text;
            ++i; // consume filename
        } else {
            out.push_back(toks[i]);
        }
    }
    toks.swap(out);
}

static DWORD run_simple_command(std::vector<std::string> argv, const Redir& r, bool background);

static DWORD run_line_tokens(const std::vector<Token>& toks_in) {
    // Handle chains: ;, &&, || and background markers & on a per-command basis (no pipelines here).
    size_t i = 0;
    DWORD last = 0;
    while (i < toks_in.size()) {
        // Collect tokens until a chain operator (not background)
        std::vector<Token> seg;
        TokType chain = TokType::OP_SEMI; // default: independent
        while (i < toks_in.size()) {
            if (toks_in[i].type==TokType::OP_AND || toks_in[i].type==TokType::OP_OR || toks_in[i].type==TokType::OP_SEMI) {
                chain = toks_in[i].type;
                ++i;
                break;
            }
            seg.push_back(toks_in[i++]);
        }

        if (seg.empty()) continue;

        // Extract background marker at end (single & treated as background)
        bool background = false;
        if (!seg.empty() && seg.back().type==TokType::OP_BG) {
            background = true;
            seg.pop_back();
        }

        // Split redirs
        Redir r{};
        std::vector<Token> seg2 = seg;
        split_redirs(seg2, r);

        // Build argv
        std::vector<std::string> argv;
        bool has_pipe = false;
        for (auto& t : seg2) {
            if (t.type == TokType::WORD) argv.push_back(t.text);
            else if (t.type == TokType::OP_PIPE) { has_pipe = true; break; }
        }
        if (has_pipe) {
            std::cerr << "Pipelines '|' not implemented yet.\n";
            last = 1;
        } else if (!argv.empty()) {
            // Execute respecting && and ||
            bool should_run = true;
            if (chain == TokType::OP_AND && last != 0) should_run = false;
            if (chain == TokType::OP_OR  && last == 0) should_run = false;

            if (should_run) last = run_simple_command(argv, r, background);
        }

        // If a failing && chain should skip until next separator, it’s already handled by should_run.
    }
    return last;
}

static DWORD run_simple_command(std::vector<std::string> argv, const Redir& r, bool background) {
    // Builtins and var assignments
    // 1) var=val [var=val ...] command ...
    // If only assignments: set env then return 0.
    size_t first_cmd_idx = 0;
    while (first_cmd_idx < argv.size() && is_assignment(argv[first_cmd_idx])) {
        std::string asg = argv[first_cmd_idx];
        auto eq = asg.find('=');
        std::string k = asg.substr(0,eq);
        std::string v = asg.substr(eq+1);
        _putenv_s(k.c_str(), v.c_str());
        g_session_env_modified.insert(k);
        ++first_cmd_idx;
    }
    if (first_cmd_idx >= argv.size()) return 0;

    // Expand variables in args (post alias, pre-exec), except the first if builtin needs literal
    for (size_t i=first_cmd_idx; i<argv.size(); ++i) {
        argv[i] = expand_vars_all(argv[i]);
    }

    const std::string& cmd = argv[first_cmd_idx];

    // Builtin: exit/quit
    if (cmd == "exit" || cmd == "quit") {
        // Persist state then exit process entirely.
        persist_env_to_rc();
        if (g_aliases_dirty) save_aliases();
        ExitProcess(0);
    }

    // Builtin: cd
    if (cmd == "cd") {
        std::string target = (first_cmd_idx+1 < argv.size()) ? argv[first_cmd_idx+1] : home_dir();
        std::error_code ec;
        fs::current_path(target, ec);
        if (ec) std::cerr << "cd: " << ec.message() << "\n";
        return ec ? 1 : 0;
    }

    // Builtin: echo
    if (cmd == "echo") {
        for (size_t i=first_cmd_idx+1;i<argv.size();++i) {
            if (i>first_cmd_idx+1) std::cout << " ";
            std::cout << argv[i];
        }
        std::cout << "\n";
        return 0;
    }

    // Builtin: set (Bash uses assignments; we accept 'set VAR=val' convenience)
    if (cmd == "set") {
        bool any = false;
        for (size_t i=first_cmd_idx+1;i<argv.size();++i) {
            if (is_assignment(argv[i])) {
                auto eq = argv[i].find('=');
                std::string k = argv[i].substr(0,eq);
                std::string v = argv[i].substr(eq+1);
                _putenv_s(k.c_str(), v.c_str());
                g_session_env_modified.insert(k);
                any = true;
            }
        }
        if (!any) {
            // print env
            // Note: Windows has no portable getenv iteration; skip to not spam.
            std::cout << "(set VAR=VALUE ...)\n";
        }
        return 0;
    }

    // Builtin: export VAR[=VALUE]
    if (cmd == "export") {
        bool ok = true;
        for (size_t i=first_cmd_idx+1;i<argv.size();++i) {
            if (is_assignment(argv[i])) {
                auto eq = argv[i].find('=');
                std::string k = argv[i].substr(0,eq);
                std::string v = argv[i].substr(eq+1);
                _putenv_s(k.c_str(), v.c_str());
                g_session_env_modified.insert(k);
            } else {
                // just mark existing var as modified so it persists
                g_session_env_modified.insert(argv[i]);
            }
        }
        return ok ? 0 : 1;
    }

    // Builtins: alias / unalias / savealiases
    if (cmd == "alias") {
        if (first_cmd_idx+1 >= argv.size()) {
            builtin_alias_list();
            return 0;
        }
        // alias NAME=VALUE
        for (size_t i=first_cmd_idx+1; i<argv.size(); ++i) {
            auto spec = argv[i];
            auto eq = spec.find('=');
            if (eq == std::string::npos) {
                // show single alias
                auto it = g_aliases.find(spec);
                if (it != g_aliases.end()) {
                    std::cout << "alias " << it->first << "='" << it->second << "'\n";
                } else {
                    std::cerr << "alias: " << spec << " not found\n";
                }
                continue;
            }
            std::string name = spec.substr(0, eq);
            std::string val  = spec.substr(eq+1);
            // strip optional quotes already expanded away
            if (!val.empty() && ((val.front()=='"' && val.back()=='"') || (val.front()=='\'' && val.back()=='\''))) {
                val = val.substr(1, val.size()-2);
            }
            if (!name.empty()) {
                g_aliases[name] = val;
                g_aliases_dirty = true;
            }
        }
        return 0;
    }

    if (cmd == "unalias") {
        if (first_cmd_idx+1 >= argv.size()) {
            std::cerr << "unalias: name required\n";
            return 1;
        }
        for (size_t i=first_cmd_idx+1; i<argv.size(); ++i) {
            g_aliases.erase(argv[i]);
        }
        g_aliases_dirty = true;
        return 0;
    }

    if (cmd == "savealiases") {
        if (!save_aliases()) {
            std::cerr << "savealiases: failed\n";
            return 1;
        }
        std::cout << "aliases saved\n";
        return 0;
    }

// External — automatically wrap bare commands with "cmd /C"
std::ostringstream oss;
for (size_t i = first_cmd_idx; i < argv.size(); ++i) {
    const std::string& a = argv[i];
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
if (!cmdline.empty() && cmdline.find('\\') == std::string::npos && cmdline.find('/') == std::string::npos) {
    // no explicit path, likely a shell builtin
    cmdline = "cmd /C " + cmdline;
}

return spawn_proc(cmdline, background, r);
}

// ---------- REPL ----------
int main() {
    // Load persisted env and aliases
    load_rc();
    load_aliases();

    std::cout << "Winix Shell v1.12 — Bash++ Aliases & Env Persist\n";

    std::string line;
    while (true) {
        std::cout << prompt();
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;

        // Expand aliases (first word) with recursion guard + re-tokenize
        std::vector<Token> toks = alias_expand_then_tokenize(line);

        // After tokenization, expand variables per WORD tokens (keeps ops intact)
        for (auto& t : toks) {
            if (t.type == TokType::WORD) t.text = expand_vars_all(t.text);
        }

        run_line_tokens(toks);
    }

    // On EOF/close: persist session env and aliases (if changed)
    persist_env_to_rc();
    if (g_aliases_dirty) save_aliases();
    return 0;
}
