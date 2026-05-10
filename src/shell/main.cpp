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
#include <utility>
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
    // Fast path: fully quoted token
    if (is_quoted(s)) return s.substr(1, s.size()-2);
    // Handle mixed tokens like "path with spaces"/rest or prefix"quoted"suffix
    if (s.find('"') == std::string::npos && s.find('\'') == std::string::npos)
        return s;
    std::string out;
    bool in_s = false, in_d = false;
    for (char c : s) {
        if (c == '\'' && !in_d) { in_s = !in_s; continue; }
        if (c == '"'  && !in_s) { in_d = !in_d; continue; }
        out += c;
    }
    return out;
}

// --------------------------------------------------
// Glob expansion
// --------------------------------------------------
static bool has_glob(const std::string& s) {
    return s.find('*') != std::string::npos || s.find('?') != std::string::npos;
}

// --------------------------------------------------
// Brace expansion  {a,b,c}  {1..5}  {a..z}
// --------------------------------------------------

// Split brace content on commas at depth 0 (respects nested braces).
static std::vector<std::string> split_brace_items(const std::string& s) {
    std::vector<std::string> items;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if      (c == '{') { depth++; cur.push_back(c); }
        else if (c == '}') { depth--; cur.push_back(c); }
        else if (c == ',' && depth == 0) { items.push_back(cur); cur.clear(); }
        else                               cur.push_back(c);
    }
    items.push_back(cur);
    return items;
}

// Recursively expand one token; returns the list of expanded words.
static std::vector<std::string> brace_expand_token(const std::string& tok) {
    // Find first unquoted '{' (skip content inside ' or " that shell_tokens kept)
    size_t open = std::string::npos;
    { bool qs = false, qd = false;
      for (size_t i = 0; i < tok.size(); ++i) {
          char c = tok[i];
          if      (c == '\'' && !qd) qs = !qs;
          else if (c == '"'  && !qs) qd = !qd;
          else if (!qs && !qd && c == '{') { open = i; break; }
      }
    }
    if (open == std::string::npos) return {tok};

    // Find matching '}'
    size_t close = open + 1;
    { int depth = 1;
      while (close < tok.size() && depth > 0) {
          if      (tok[close] == '{') depth++;
          else if (tok[close] == '}') { if (--depth == 0) break; }
          ++close;
      }
      if (depth != 0) return {tok}; // unmatched — pass through literally
    }

    std::string prefix = tok.substr(0, open);
    std::string inner  = tok.substr(open + 1, close - (open + 1));
    std::string suffix = tok.substr(close + 1);

    // Sequence form  {from..to}  or  {from..to..step}
    size_t dotdot = inner.find("..");
    if (dotdot != std::string::npos) {
        std::string from_s = inner.substr(0, dotdot);
        std::string rest_s = inner.substr(dotdot + 2);
        std::string to_s, step_s;
        size_t dd2 = rest_s.find("..");
        if (dd2 != std::string::npos) { to_s = rest_s.substr(0, dd2); step_s = rest_s.substr(dd2 + 2); }
        else                            to_s = rest_s;

        // Numeric sequence
        auto is_int = [](const std::string& s) {
            if (s.empty()) return false;
            size_t i = (s[0] == '-') ? 1 : 0;
            return i < s.size() && std::all_of(s.begin()+i, s.end(),
                       [](unsigned char c){ return std::isdigit(c); });
        };
        if (is_int(from_s) && is_int(to_s)) {
            int fv = std::stoi(from_s), tv = std::stoi(to_s);
            int sv = step_s.empty() ? 1 : std::abs(std::stoi(step_s));
            if (sv == 0) sv = 1;
            bool pad = (from_s.size() > 1 && from_s[0] == '0') ||
                       (to_s.size()   > 1 && to_s[0]   == '0');
            int width = pad ? (int)std::max(from_s.size(), to_s.size()) : 0;
            std::vector<std::string> res;
            int v = fv;
            while ((fv <= tv) ? (v <= tv) : (v >= tv)) {
                std::string ns = std::to_string(std::abs(v));
                if (width > (int)ns.size()) ns = std::string(width - ns.size(), '0') + ns;
                if (v < 0) ns = "-" + ns;
                for (auto& r : brace_expand_token(prefix + ns + suffix)) res.push_back(r);
                v += (fv <= tv) ? sv : -sv;
            }
            return res;
        }

        // Character sequence {a..z}
        if (from_s.size() == 1 && to_s.size() == 1 &&
            std::isalpha((unsigned char)from_s[0]) &&
            std::isalpha((unsigned char)to_s[0])) {
            int sv = step_s.empty() ? 1 : std::abs(std::stoi(step_s));
            if (sv == 0) sv = 1;
            char fc = from_s[0], tc = to_s[0];
            std::vector<std::string> res;
            int v = fc;
            while ((fc <= tc) ? (v <= tc) : (v >= tc)) {
                for (auto& r : brace_expand_token(prefix + (char)v + suffix)) res.push_back(r);
                v += (fc <= tc) ? sv : -sv;
            }
            return res;
        }
        // Not a valid sequence — fall through to comma check
    }

    // Comma list  {a,b,c}
    auto items = split_brace_items(inner);
    if (items.size() <= 1) return {tok}; // no commas — literal

    std::vector<std::string> res;
    for (auto& item : items)
        for (auto& r : brace_expand_token(prefix + item + suffix))
            res.push_back(r);
    return res;
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
// Unquoted tokens: brace-expanded first, then glob-expanded.
static std::vector<std::string> glob_expand(const std::vector<std::string>& tokens) {
    std::vector<std::string> out;
    out.reserve(tokens.size());
    for (const auto& tok : tokens) {
        if (is_quoted(tok)) { out.push_back(unquote(tok)); continue; }
        // Brace expansion may produce multiple words; glob-expand each
        for (auto& be : brace_expand_token(tok)) {
            std::string actual = unquote(be); // strip any shell quotes (e.g. --flag="value")
            if (!has_glob(actual)) { out.push_back(actual); continue; }
            auto matches = glob_one(actual);
            if (matches.empty()) out.push_back(actual);
            else for (auto& m : matches) out.push_back(m);
        }
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
    std::string man_dir;        // structured man pages (bin_dir/man/)
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
    p.man_dir = (exe / "man").string();

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
static std::map<std::string, std::vector<std::string>> g_arrays; // shell arrays
static std::vector<std::string> g_positional; // $1...$N positional params (0-indexed)
static std::map<std::string, std::vector<std::string>> g_functions; // user-defined functions

// local VAR scope stack — one frame per active function call.
// Each frame maps variable name -> saved outer value (nullopt = didn't exist).
struct LocalFrame {
    std::map<std::string, std::optional<std::string>> saved;
};
static std::vector<LocalFrame> g_local_stack;

// Exit code set by builtins that need to return non-zero (e.g. getopts, test).
// Reset to 0 at the start of each handle_builtin call.
static int g_builtin_exit = 0;

// trap EXIT — command to run when the top-level script exits.
static std::string g_trap_exit;
// Context pointers for running the EXIT trap (set when a script is invoked).
static const Paths* g_trap_paths   = nullptr;
static Aliases*     g_trap_aliases = nullptr;

// Process substitution temp-file tracking.
struct ProcSubOut { std::string path; std::string cmd; };
static std::vector<std::string> g_proc_sub_in;   // <(cmd) temp files (delete after cmd)
static std::vector<ProcSubOut>  g_proc_sub_out;  // >(cmd) entries (feed + delete after cmd)

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

// Expand process substitutions <(cmd) and >(cmd) in a command line.
// Replaces each with a temp file path; registers files in g_proc_sub_in / g_proc_sub_out.
// Must be called before expand_vars on actual command lines (not variable assignments).
static std::string expand_proc_subs(const std::string& line) {
    std::string out;
    bool in_s = false, in_d = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\'' && !in_d) { in_s = !in_s; out.push_back(c); continue; }
        if (c == '"'  && !in_s) { in_d = !in_d; out.push_back(c); continue; }
        if (!in_s && !in_d) {
            bool is_in  = (c == '<' && i+1 < line.size() && line[i+1] == '(');
            bool is_out = (c == '>' && i+1 < line.size() && line[i+1] == '(');
            if (is_in || is_out) {
                // Find matching close paren (depth-aware, quote-aware)
                int depth = 1;
                size_t j = i + 2;
                bool ns = false, nd = false;
                while (j < line.size() && depth > 0) {
                    char nc = line[j];
                    if      (nc == '\'' && !nd) ns = !ns;
                    else if (nc == '"'  && !ns) nd = !nd;
                    else if (!ns && !nd) {
                        if      (nc == '(') depth++;
                        else if (nc == ')') { if (--depth == 0) break; }
                    }
                    ++j;
                }
                std::string subcmd = line.substr(i + 2, j - (i + 2));

                char tmpdir[MAX_PATH], tmpfile[MAX_PATH];
                GetTempPathA(MAX_PATH, tmpdir);

                if (is_in) {
                    GetTempFileNameA(tmpdir, "psi", 0, tmpfile);
                    // Run subcmd and capture output to temp file
                    FILE* fp = _popen(subcmd.c_str(), "r");
                    if (fp) {
                        if (FILE* tf = fopen(tmpfile, "wb")) {
                            char buf[4096]; size_t n;
                            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
                                fwrite(buf, 1, n, tf);
                            fclose(tf);
                        }
                        _pclose(fp);
                    }
                    g_proc_sub_in.push_back(tmpfile);
                } else {
                    GetTempFileNameA(tmpdir, "pso", 0, tmpfile);
                    // Touch temp file; parent command will write to it
                    if (FILE* tf = fopen(tmpfile, "wb")) fclose(tf);
                    g_proc_sub_out.push_back({tmpfile, subcmd});
                }
                out += tmpfile;
                i = j; // advance past ')'
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

// Split whitespace-separated words respecting single/double quotes (for array literals).
static std::vector<std::string> parse_array_words(const std::string& s) {
    std::vector<std::string> result;
    std::string cur;
    bool in_s = false, in_d = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\'' && !in_d) { in_s = !in_s; continue; }
        if (c == '"'  && !in_s) { in_d = !in_d; continue; }
        if (!in_s && !in_d && std::isspace((unsigned char)c)) {
            if (!cur.empty()) { result.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) result.push_back(cur);
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
                        std::string home = user_home();
                        // If home contains spaces and we're not inside quotes,
                        // wrap it so shell_tokens doesn't split on the space.
                        if (home.find(' ') != std::string::npos && !in_d && !in_s)
                            out += '"' + home + '"';
                        else
                            out += home;
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
                    std::string val = getenv_win(line.substr(i+1, j-(i+1)));
                    if (!in_d && (val.find(';') != std::string::npos ||
                                  val.find(' ') != std::string::npos))
                        out += '"' + val + '"';
                    else
                        out += val;
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
            // $@ — positional params; inside double quotes each element is a
            //      separate word (close/reopen quote around each boundary so
            //      downstream shell_tokens splits them correctly).
            if (c == '$' && i+1 < line.size() && line[i+1] == '@') {
                if (in_d) {
                    for (size_t k = 0; k < g_positional.size(); ++k) {
                        if (k) out += "\" \""; // boundary: close quote, space, open quote
                        out += g_positional[k];
                    }
                } else {
                    for (size_t k = 0; k < g_positional.size(); ++k) {
                        if (k) out += ' ';
                        out += g_positional[k];
                    }
                }
                i++;
                continue;
            }
            // $* — all positional params joined as a single string with IFS (space)
            if (c == '$' && i+1 < line.size() && line[i+1] == '*') {
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

                    // ${#VAR} — string length / ${#arr[@]} — array count
                    if (!inner.empty() && inner[0] == '#') {
                        std::string name = inner.substr(1);
                        size_t lb = name.find('[');
                        if (lb != std::string::npos) {
                            std::string arrname = name.substr(0, lb);
                            size_t rb = name.find(']', lb);
                            std::string idx = (rb != std::string::npos) ? name.substr(lb+1, rb-lb-1) : "";
                            auto it = g_arrays.find(arrname);
                            if (idx == "@" || idx == "*") {
                                out += std::to_string(it != g_arrays.end() ? it->second.size() : 0);
                            } else {
                                if (it != g_arrays.end()) {
                                    int n = (int)strtol(idx.c_str(), nullptr, 10);
                                    if (n >= 0 && n < (int)it->second.size())
                                        out += std::to_string(it->second[(size_t)n].size());
                                }
                            }
                        } else {
                            auto it = g_shell_vars.find(name);
                            std::string val = (it != g_shell_vars.end()) ? it->second : getenv_win(name);
                            out += std::to_string(val.size());
                        }
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

                    // ${arr[N]}, ${arr[@]}, ${arr[*]} — array subscript
                    {
                        size_t lb = inner.find('[');
                        if (lb != std::string::npos) {
                            std::string arrname = inner.substr(0, lb);
                            size_t rb = inner.find(']', lb);
                            std::string idx = (rb != std::string::npos) ? inner.substr(lb+1, rb-lb-1) : "";
                            auto it = g_arrays.find(arrname);
                            if (it != g_arrays.end()) {
                                if (idx == "@" || idx == "*") {
                                    for (size_t k = 0; k < it->second.size(); ++k) {
                                        if (k) out += ' ';
                                        out += it->second[k];
                                    }
                                } else {
                                    int n = (int)strtol(idx.c_str(), nullptr, 10);
                                    if (n >= 0 && n < (int)it->second.size())
                                        out += it->second[(size_t)n];
                                }
                            }
                            continue;
                        }
                    }

                    // Plain ${VAR}
                    auto it = g_shell_vars.find(inner);
                    {
                        std::string val = (it != g_shell_vars.end()) ? it->second : getenv_win(inner);
                        if (!in_d && (val.find(';') != std::string::npos ||
                                      val.find(' ') != std::string::npos))
                            out += '"' + val + '"';
                        else
                            out += val;
                    }
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
                    std::string val = (it != g_shell_vars.end()) ? it->second : getenv_win(name);
                    if (!in_d && (val.find(';') != std::string::npos ||
                                  val.find(' ') != std::string::npos))
                        out += '"' + val + '"';
                    else
                        out += val;
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
    { DWORD _m; if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &_m))
          FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE)); }
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
    // Flush stale console input events that the child may have left behind.
    // Some runtimes (e.g. GHC/Haskell used by pandoc) inject synthetic events
    // into the console input buffer, which would cause the shell to re-execute
    // the last command.  Only flush when stdin is a real console (not a pipe).
    { DWORD _m; if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &_m))
          FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE)); }
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

    // Fallback: try system PATH directly — spawn_direct avoids cmd.exe overhead
    {
        char buf[MAX_PATH] = {};
        if (SearchPathA(NULL, (cmd + ".exe").c_str(), NULL, MAX_PATH, buf, NULL) > 0) {
            code = spawn_direct(std::string(buf), rest_args, !bg, h_in, h_out, h_err, out_hproc, out_pid);
            close_redirs();
            return code;
        }
    }

    // Last resort: cmd.exe /C — handles .bat, .cmd, and anything else Windows knows about
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
    int depth = 0;
    if (kw == "if" || kw == "for" || kw == "while" || kw == "case" || kw == "select") depth++;
    else if (kw == "fi" || kw == "done" || kw == "esac") depth--;
    else if (kw == "{") depth++;
    else if (kw == "}") depth--;
    else if (t.back() == '{') depth++;  // "name() {" or "function foo {"
    // Scan remaining tokens for closing keywords so one-liners (for..done, if..fi, etc.)
    // don't incorrectly enter continuation mode.
    for (size_t i = 1; i < toks.size() && depth > 0; i++) {
        if (toks[i] == "done" || toks[i] == "fi" || toks[i] == "esac" || toks[i] == "}") depth--;
    }
    return depth;
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
        {"man",     "<cmd>",           "show command help (--help via less)"},
        {"which",   "<cmd>",           "locate a command"},
        {"basename","<path>",          "filename portion of path"},
        {"dirname", "<path>",          "directory portion of path"},
        {"true",    "",                "exit 0"},
        {"false",   "",                "exit 1"},
    }) row(c);

    section("SHELL SCRIPTING");
    std::cout << "    " << DIM
              << "if COND; then ...  conditional (elif/else/fi)\n"
              << "    for VAR in LIST    iterate over words (done)\n"
              << "    while COND         loop while condition is true (done)\n"
              << "    select VAR in LIST interactive numbered menu (done)\n"
              << "    case WORD in       pattern matching (esac)\n"
              << "    arr=(a b c)        array assignment\n"
              << "    ${arr[@]}          expand all array elements\n"
              << "    ${#arr[@]}         array element count\n"
              << RST;

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
    g_builtin_exit = 0;
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

    // ver / ver --version
    if (match("ver") || match("ver --version") || match("ver -v")) {
        std::cout << "Winix Shell " << WINIX_VERSION << "\n";
        return true;
    }

    // man CMD — display structured man page (or fall back to CMD --help) via less
    if (starts("man ") || match("man")) {
        std::string cmd = starts("man ") ? trim(line.substr(4)) : std::string();
        if (cmd.empty()) {
            std::cerr << "Usage: man <command>\n";
            return true;
        }

        std::string page_text;

        // 1. Look for structured page in man_dir/<cmd>
        fs::path man_page = fs::path(paths.man_dir) / cmd;
        if (fs::exists(man_page)) {
            std::ifstream mf(man_page);
            if (mf) {
                std::string line2;
                while (std::getline(mf, line2)) { page_text += line2; page_text += '\n'; }
            }
        }

        // 2. Fall back to CMD --help
        if (page_text.empty()) {
            fs::path exe_path = fs::path(paths.bin_dir) / (cmd + ".exe");
            std::string invoke = fs::exists(exe_path)
                ? ("\"" + exe_path.string() + "\"") : cmd;
            std::string help_cmd = invoke + " --help 2>&1";
            FILE* fp = _popen(help_cmd.c_str(), "r");
            if (!fp) { std::cerr << "man: cannot run " << cmd << "\n"; return true; }
            char buf[4096];
            while (fgets(buf, sizeof(buf), fp)) page_text += buf;
            _pclose(fp);
        }

        if (page_text.empty()) {
            std::cerr << "man: no manual entry for '" << cmd << "'\n";
            return true;
        }

        fs::path less_exe = fs::path(paths.bin_dir) / "less.exe";
        std::string less_cmd = "\"" + less_exe.string() + "\"";
        FILE* lp = _popen(less_cmd.c_str(), "w");
        if (lp) { fputs(page_text.c_str(), lp); _pclose(lp); }
        else     { std::cout << page_text; }
        return true;
    }

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

    // vars — list shell-local variables and arrays
    if (match("vars")) {
        for (auto& kv : g_shell_vars)
            std::cout << kv.first << "=" << kv.second << "\n";
        for (auto& kv : g_arrays) {
            std::cout << kv.first << "=(";
            for (size_t i = 0; i < kv.second.size(); ++i) {
                if (i) std::cout << ' ';
                std::cout << kv.second[i];
            }
            std::cout << ")\n";
        }
        return true;
    }

    // unset VAR — remove shell variable or array
    if (starts("unset ")) {
        auto name = trim(line.substr(6));
        g_shell_vars.erase(name);
        g_arrays.erase(name);
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

    // getopts OPTSTRING VAR [ARGS...]
    // POSIX option parser for shell scripts.
    // Sets VAR to the next option char; sets OPTARG for options requiring an arg.
    // Returns 0 while options remain, 1 when done.
    // OPTIND tracks current position (1-based); _GETOPTS_SUB tracks pos within arg.
    if (starts("getopts ")) {
        auto toks = shell_tokens(line);
        if (toks.size() < 3) {
            std::cerr << "getopts: usage: getopts OPTSTRING VAR [ARGS...]\n";
            g_builtin_exit = 2; return true;
        }
        std::string optstring = toks[1];
        std::string varname   = toks[2];

        // Explicit args override positional params
        std::vector<std::string> args;
        if (toks.size() > 3) {
            for (size_t i = 3; i < toks.size(); ++i) args.push_back(toks[i]);
        } else {
            args = g_positional;
        }

        bool silent = (!optstring.empty() && optstring[0] == ':');
        if (silent) optstring = optstring.substr(1);

        // OPTIND is 1-based index into args; _GETOPTS_SUB is offset within current arg
        int optind = 1, sub = 0;
        { auto it = g_shell_vars.find("OPTIND");
          if (it != g_shell_vars.end() && !it->second.empty())
              optind = std::stoi(it->second); }
        { auto it = g_shell_vars.find("_GETOPTS_SUB");
          if (it != g_shell_vars.end() && !it->second.empty())
              sub = std::stoi(it->second); }

        // Skip to next arg if sub == 0 (fresh start on new arg)
        // args is 0-indexed; optind is 1-based
        while (optind <= (int)args.size()) {
            const std::string& arg = args[optind - 1];

            // "--" ends option processing
            if (arg == "--") { optind++; break; }

            // Non-option arg or empty arg ends option processing
            if (arg.empty() || arg[0] != '-' || arg == "-") break;

            // sub == 0 means start of a new -xyz arg; begin at char 1 (past '-')
            if (sub == 0) sub = 1;

            if (sub >= (int)arg.size()) { // exhausted this arg; move on
                optind++; sub = 0; continue;
            }

            char opt = arg[sub++];

            // Look up option in optstring
            size_t pos = optstring.find(opt);
            if (pos == std::string::npos) {
                // Unknown option
                if (!silent)
                    std::cerr << "winix: illegal option -- " << opt << "\n";
                g_shell_vars[varname] = "?";
                if (silent) g_shell_vars["OPTARG"] = std::string(1, opt);
                else        g_shell_vars.erase("OPTARG");
            } else {
                g_shell_vars[varname] = std::string(1, opt);
                bool needs_arg = (pos + 1 < optstring.size() && optstring[pos + 1] == ':');
                if (needs_arg) {
                    std::string optarg;
                    if (sub < (int)arg.size()) {
                        // Rest of current arg is the option argument
                        optarg = arg.substr(sub);
                        sub = (int)arg.size();
                    } else {
                        // Next arg is the option argument
                        optind++; sub = 0;
                        if (optind <= (int)args.size()) {
                            optarg = args[optind - 1];
                            optind++; // advance past the consumed optarg
                        } else {
                            if (silent) {
                                g_shell_vars[varname] = ":";
                                g_shell_vars["OPTARG"] = std::string(1, opt);
                            } else {
                                std::cerr << "winix: option requires an argument -- " << opt << "\n";
                                g_shell_vars[varname] = "?";
                                g_shell_vars.erase("OPTARG");
                            }
                            g_shell_vars["OPTIND"] = std::to_string(optind);
                            g_shell_vars["_GETOPTS_SUB"] = "0";
                            g_builtin_exit = 1; return true;
                        }
                    }
                    g_shell_vars["OPTARG"] = optarg;
                } else {
                    g_shell_vars.erase("OPTARG");
                }
            }

            // Advance to next arg if we've consumed all chars in this one
            if (sub >= (int)arg.size()) { optind++; sub = 0; }
            g_shell_vars["OPTIND"] = std::to_string(optind);
            g_shell_vars["_GETOPTS_SUB"] = std::to_string(sub);
            g_builtin_exit = 0; return true; // found an option → return 0
        }

        // No more options
        g_shell_vars[varname] = "?";
        g_shell_vars["OPTIND"] = std::to_string(optind);
        g_shell_vars["_GETOPTS_SUB"] = "0";
        g_builtin_exit = 1; return true;
    }

    // trap [CMD] SIGNAL  — register a command to run on a signal/event.
    // Only EXIT is supported; SIGINT/SIGTERM etc. are Windows-incompatible.
    if (starts("trap ") || match("trap")) {
        auto toks = shell_tokens(line);
        if (toks.size() == 1) {
            // trap — list registered traps
            if (!g_trap_exit.empty())
                std::cout << "trap -- " << g_trap_exit << " EXIT\n";
            return true;
        }
        if (toks.size() == 2) {
            // trap - SIGNAL  or  trap CMD (with no signal = EXIT implied? non-standard)
            // treat as: trap - EXIT  if only one arg and it's a signal name
            std::string sig = toks[1];
            if (sig == "EXIT" || sig == "0") { g_trap_exit.clear(); return true; }
            // else: single-arg form not supported — warn
            std::cerr << "trap: usage: trap CMD EXIT | trap - EXIT | trap\n";
            return true;
        }
        // trap CMD SIGNAL [SIGNAL...]
        std::string cmd = toks[1];
        bool is_clear = (cmd == "-");
        for (size_t ti = 2; ti < toks.size(); ++ti) {
            std::string sig = toks[ti];
            if (sig == "EXIT" || sig == "0") {
                g_trap_exit = is_clear ? "" : cmd;
            } else {
                std::cerr << "trap: " << sig << ": unsupported signal (only EXIT supported)\n";
            }
        }
        return true;
    }

    // printf FORMAT [ARG...]
    if (starts("printf ") || match("printf")) {
        auto toks = shell_tokens(line);
        if (toks.size() >= 2 && (toks[1] == "--help")) {
            std::cout << "Usage: printf FORMAT [ARG...]\n";
            std::cout << "Format and print data.\n";
            std::cout << "Supports: \\n \\t \\r \\a \\b \\f \\v \\\\ \\0NNN\n";
            std::cout << "          %s %d %i %u %o %x %X %f %e %E %g %G %c %b %%\n";
            return true;
        }
        if (toks.size() < 2) {
            std::cerr << "printf: missing format string\n";
            g_builtin_exit = 1;
            return true;
        }

        // Print string with escape expansion (used by %b specifier)
        auto print_escaped = [](const char* s) {
            for (; *s; s++) {
                if (*s != '\\') { putchar(*s); continue; }
                s++;
                switch (*s) {
                    case 'n':  putchar('\n'); break;
                    case 't':  putchar('\t'); break;
                    case 'r':  putchar('\r'); break;
                    case 'a':  putchar('\a'); break;
                    case 'b':  putchar('\b'); break;
                    case 'f':  putchar('\f'); break;
                    case 'v':  putchar('\v'); break;
                    case '\\': putchar('\\'); break;
                    case '0': {
                        unsigned int val = 0;
                        for (int i = 0; i < 3 && s[1] >= '0' && s[1] <= '7'; i++)
                            val = val * 8 + (*++s - '0');
                        putchar((char)val);
                        break;
                    }
                    case '\0': putchar('\\'); s--; break;
                    default:   putchar('\\'); putchar(*s); break;
                }
            }
        };

        // Unquote all tokens (shell_tokens keeps quotes in the strings)
        std::string fmt = unquote(toks[1]);
        std::vector<std::string> args;
        for (size_t ti = 2; ti < toks.size(); ++ti)
            args.push_back(unquote(toks[ti]));
        size_t argi = 0;

        // Repeat format until all args consumed (GNU behavior).
        // Stop early if a pass consumed no args (no format specs → only one pass).
        do {
            size_t prev_argi = argi;
            for (const char* p = fmt.c_str(); *p; p++) {
                if (*p == '\\') {
                    p++;
                    switch (*p) {
                        case 'n':  putchar('\n'); break;
                        case 't':  putchar('\t'); break;
                        case 'r':  putchar('\r'); break;
                        case 'a':  putchar('\a'); break;
                        case 'b':  putchar('\b'); break;
                        case 'f':  putchar('\f'); break;
                        case 'v':  putchar('\v'); break;
                        case '\\': putchar('\\'); break;
                        case '0': {
                            unsigned int val = 0;
                            for (int i = 0; i < 3 && p[1] >= '0' && p[1] <= '7'; i++)
                                val = val * 8 + (*++p - '0');
                            putchar((char)val);
                            break;
                        }
                        case '\0': putchar('\\'); p--; break;
                        default:   putchar('\\'); putchar(*p); break;
                    }
                } else if (*p == '%') {
                    p++;
                    if (*p == '%') { putchar('%'); continue; }

                    char spec[64] = "%";
                    int  si = 1;
                    // Flags
                    while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#') {
                        if (si < 60) spec[si++] = *p;
                        p++;
                    }
                    // Width
                    while (*p >= '0' && *p <= '9') {
                        if (si < 60) spec[si++] = *p;
                        p++;
                    }
                    // Precision
                    if (*p == '.') {
                        if (si < 60) spec[si++] = *p++;
                        while (*p >= '0' && *p <= '9') {
                            if (si < 60) spec[si++] = *p;
                            p++;
                        }
                    }
                    char conv = *p;
                    if (si < 62) { spec[si++] = conv; spec[si] = '\0'; }

                    const char* arg = (argi < args.size()) ? args[argi++].c_str() : "";

                    switch (conv) {
                        case 's':
                            printf(spec, arg); // NOLINT
                            break;
                        case 'd': case 'i':
                            printf(spec, (int)strtol(arg, nullptr, 0)); // NOLINT
                            break;
                        case 'u':
                            printf(spec, (unsigned int)strtoul(arg, nullptr, 0)); // NOLINT
                            break;
                        case 'o':
                            printf(spec, (unsigned int)strtoul(arg, nullptr, 0)); // NOLINT
                            break;
                        case 'x': case 'X':
                            printf(spec, (unsigned int)strtoul(arg, nullptr, 0)); // NOLINT
                            break;
                        case 'f': case 'e': case 'E': case 'g': case 'G':
                            printf(spec, strtod(arg, nullptr)); // NOLINT
                            break;
                        case 'c':
                            putchar(arg[0]);
                            break;
                        case 'b':
                            print_escaped(arg);
                            break;
                        default:
                            putchar('%');
                            putchar(conv);
                            break;
                    }
                } else {
                    putchar(*p);
                }
            }
            if (argi == prev_argi) break; // no args consumed in this pass — stop
        } while (argi < args.size());

        fflush(stdout);
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

    // VAR=value / VAR=(a b c) / VAR[N]=value assignment
    {
        size_t eq = line.find('=');
        if (eq != std::string::npos && eq > 0) {
            // arr[N]=value — individual element assignment
            size_t lb = line.find('[');
            if (lb != std::string::npos && lb < eq) {
                size_t rb = line.find(']', lb);
                if (rb != std::string::npos && rb == eq - 1) {
                    std::string arrname = line.substr(0, lb);
                    bool valid = !arrname.empty() && !std::isdigit((unsigned char)arrname[0]);
                    for (char ch : arrname) if (!std::isalnum((unsigned char)ch) && ch != '_') { valid = false; break; }
                    if (valid && arrname.find(' ') == std::string::npos) {
                        std::string idxstr = line.substr(lb+1, rb-lb-1);
                        int n = (int)strtol(idxstr.c_str(), nullptr, 10);
                        std::string val = expand_vars(line.substr(eq + 1), last_exit);
                        if (n >= 0) {
                            auto& arr = g_arrays[arrname];
                            if ((size_t)n >= arr.size()) arr.resize((size_t)n + 1);
                            arr[(size_t)n] = val;
                        }
                        return 0;
                    }
                }
            }
            std::string name = line.substr(0, eq);
            bool valid = !std::isdigit((unsigned char)name[0]);
            for (char ch : name)
                if (!std::isalnum((unsigned char)ch) && ch != '_') { valid = false; break; }
            if (valid && name.find(' ') == std::string::npos) {
                std::string val = line.substr(eq + 1);
                if (!val.empty() && val.front() == '(') {
                    // Array assignment: NAME=(a b c)
                    size_t close = val.rfind(')');
                    std::string content = (close != std::string::npos) ? val.substr(1, close - 1) : val.substr(1);
                    g_arrays[name] = parse_array_words(expand_vars(content, last_exit));
                } else {
                    g_shell_vars[name] = val;
                }
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
            g_positional.clear();
            for (size_t pi = 1; pi < toks.size(); ++pi)
                g_positional.push_back(unquote(toks[pi]));
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
        return g_builtin_exit;

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
                        g_positional.clear();
                        for (size_t pi = 1; pi < toks.size(); ++pi)
                            g_positional.push_back(unquote(toks[pi]));

                        // Set trap context for EXIT trap (save/restore for nested scripts)
                        const Paths* old_trap_paths   = g_trap_paths;
                        Aliases*     old_trap_aliases = g_trap_aliases;
                        g_trap_paths   = &paths;
                        g_trap_aliases = &aliases;

                        ScriptState ss;
                        int rc = script_exec_lines(slines, paths, aliases, hist, cfg, last_exit, ss);
                        if (ss.do_return) rc = ss.return_val;

                        // Fire EXIT trap if set
                        if (!g_trap_exit.empty()) {
                            std::string tcmd = std::exchange(g_trap_exit, "");
                            run_command_line(tcmd, paths, aliases, nullptr, nullptr, rc);
                        }

                        // Restore trap context
                        g_trap_paths   = old_trap_paths;
                        g_trap_aliases = old_trap_aliases;
                        g_positional   = old_pos;
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

// Execute and clean up any pending process-substitution temp files.
// For >(cmd): feeds the temp file into cmd, then deletes it.
// For <(cmd): just deletes the temp file.
static void flush_proc_subs(const Paths& paths, Aliases& aliases, int last_exit) {
    for (auto& ps : g_proc_sub_out) {
        std::string feed = ps.cmd + " < \"" + ps.path + "\"";
        run_command_line(feed, paths, aliases, nullptr, nullptr, last_exit);
        DeleteFileA(ps.path.c_str());
    }
    g_proc_sub_out.clear();
    for (auto& p : g_proc_sub_in)
        DeleteFileA(p.c_str());
    g_proc_sub_in.clear();
}

// Run EXIT trap (if set) then call std::exit.
// Used by the 'exit' builtin so the trap fires even on explicit exit.
static void run_exit_trap_and_exit(int code) {
    if (!g_trap_exit.empty() && g_trap_paths && g_trap_aliases) {
        std::string cmd = std::exchange(g_trap_exit, ""); // clear first to prevent re-entry
        run_command_line(cmd, *g_trap_paths, *g_trap_aliases, nullptr, nullptr, code);
    }
    std::exit(code);
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
            run_exit_trap_and_exit(code);
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
            size_t do_idx = toks.size();
            for (size_t j = 2; j < toks.size(); ++j) {
                if (toks[j] == "in") { in_list = true; continue; }
                if (toks[j] == "do") { do_idx = j; break; }
                if (toks[j] == ";")  continue;
                if (in_list) {
                    std::string tok = toks[j];
                    if (!tok.empty() && tok.back() == ';') tok.pop_back();
                    if (tok.empty() || tok == "do") continue;
                    std::string expanded = expand_vars(tok, last_exit);
                    // Quote-aware word-split: handles "$@" → multiple quoted words,
                    // plain expansion → whitespace split, "quoted val" → single word.
                    for (auto& w : shell_tokens(expanded))
                        items.push_back(unquote(w));
                }
            }

            size_t body_start = i + 1;
            if (body_start < lines.size() && trim(lines[body_start]) == "do")
                body_start++;

            std::vector<std::string> body;
            size_t done_idx = collect_until_closed(lines, body_start, body);

            // One-liner: body follows "do" on the same line before "done"
            if (body.empty() && do_idx < toks.size()) {
                size_t do_pos = l.find("; do ");
                if (do_pos == std::string::npos) do_pos = l.find(" do ");
                if (do_pos != std::string::npos) {
                    size_t off = do_pos + (l[do_pos] == ';' ? 5 : 4);
                    std::string body_text = trim(l.substr(off));
                    size_t dp = body_text.rfind("; done");
                    if (dp == std::string::npos) dp = body_text.rfind(" done");
                    if (dp != std::string::npos) body_text = trim(body_text.substr(0, dp));
                    if (!body_text.empty()) body.push_back(body_text);
                }
            }

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

        // select VAR in item1 item2 ...; do ... done
        if (toks[0] == "select") {
            std::string var = (toks.size() >= 2) ? toks[1] : "";
            std::vector<std::string> items;
            bool in_list = false;
            for (size_t j = 2; j < toks.size(); ++j) {
                if (toks[j] == "in")                   { in_list = true; continue; }
                if (toks[j] == "do" || toks[j] == ";") continue;
                if (in_list) {
                    std::string expanded = expand_vars(toks[j], last_exit);
                    std::istringstream wss(expanded);
                    std::string word;
                    while (wss >> word) items.push_back(word);
                }
            }

            size_t body_start = i + 1;
            if (body_start < lines.size() && trim(lines[body_start]) == "do")
                body_start++;
            std::vector<std::string> body;
            size_t done_idx = collect_until_closed(lines, body_start, body);

            // PS3 is the select prompt (bash convention); default "#? "
            auto ps3_it = g_shell_vars.find("PS3");
            std::string ps3 = (ps3_it != g_shell_vars.end()) ? ps3_it->second : "#? ";

            for (;;) {
                for (size_t k = 0; k < items.size(); ++k)
                    std::cerr << (k + 1) << ") " << items[k] << "\n";
                std::cerr << ps3 << std::flush;
                std::string reply;
                if (!std::getline(std::cin, reply)) break; // EOF
                g_shell_vars["REPLY"] = reply;
                char* endp;
                long n = strtol(reply.c_str(), &endp, 10);
                if (endp > reply.c_str() && n >= 1 && (size_t)n <= items.size())
                    g_shell_vars[var] = items[(size_t)(n - 1)];
                else
                    g_shell_vars[var] = "";
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
                std::string cp_ps = expand_proc_subs(cmd_part);
                std::string ex = expand_vars(expand_aliases_once(cp_ps, aliases), last_exit);
                last_exit = run_command_line(ex, paths, aliases, hist, cfg, last_exit);
                DeleteFileA(tmpfile);
                flush_proc_subs(paths, aliases, last_exit);
                continue;
            }
        }

        // Regular command
        std::string l_ps = expand_proc_subs(l);
        std::string expanded = expand_vars(expand_aliases_once(l_ps, aliases), last_exit);
        last_exit = run_command_line(expanded, paths, aliases, hist, cfg, last_exit);
        flush_proc_subs(paths, aliases, last_exit);
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

        // Set trap context so EXIT trap can fire
        g_trap_paths   = &paths2;
        g_trap_aliases = &aliases2;

        ScriptState ss;
        int rc2 = script_exec_lines(slines, paths2, aliases2, nullptr, &cfg2, 0, ss);
        if (ss.do_return) rc2 = ss.return_val;

        // Fire EXIT trap if set
        if (!g_trap_exit.empty()) {
            std::string tcmd = std::exchange(g_trap_exit, "");
            run_command_line(tcmd, paths2, aliases2, nullptr, nullptr, rc2);
        }

        return rc2;
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
        [&](const std::string& partial, const std::string& line_prefix){ return completion_matches(partial, aliases, line_prefix); },
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
                    if (!cont.has_value() || editor.last_ctrl_c()) break;
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
            auto toks0 = shell_tokens(original);
            bool is_compound = !toks0.empty() &&
                (toks0[0] == "if" || toks0[0] == "for" || toks0[0] == "while" ||
                 toks0[0] == "case" || toks0[0] == "select");
            if (depth > 0) {
                std::vector<std::string> block_lines;
                block_lines.push_back(original);
                while (depth > 0) {
                    auto cont = editor.read_line("> ");
                    if (!cont.has_value() || editor.last_ctrl_c()) break;
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
            // One-liner: compound command that opened and closed on the same line
            if (depth == 0 && is_compound) {
                hist.add(original);
                hist.save(paths.history_file);
                ScriptState ss;
                last_exit = script_exec_lines({original}, paths, aliases,
                                               &hist, &cfg, last_exit, ss);
                continue;
            }
        }

        auto expanded = expand_vars(
                            expand_aliases_once(original, aliases), last_exit);

        // VAR=value / VAR=(a b c) / VAR[N]=value — shell variable assignment
        {
            size_t eq = expanded.find('=');
            bool is_assign = false;
            if (eq != std::string::npos && eq > 0) {
                // arr[N]=value — individual element assignment
                size_t lb = expanded.find('[');
                if (lb != std::string::npos && lb < eq) {
                    size_t rb = expanded.find(']', lb);
                    if (rb != std::string::npos && rb == eq - 1) {
                        std::string arrname = expanded.substr(0, lb);
                        bool valid = !arrname.empty() && !std::isdigit((unsigned char)arrname[0]);
                        for (char ch : arrname) if (!std::isalnum((unsigned char)ch) && ch != '_') { valid = false; break; }
                        if (valid && arrname.find(' ') == std::string::npos) {
                            std::string idxstr = expanded.substr(lb+1, rb-lb-1);
                            int n = (int)strtol(idxstr.c_str(), nullptr, 10);
                            if (n >= 0) {
                                auto& arr = g_arrays[arrname];
                                if ((size_t)n >= arr.size()) arr.resize((size_t)n + 1);
                                arr[(size_t)n] = expanded.substr(eq + 1);
                            }
                            last_exit = 0;
                            is_assign = true;
                        }
                    }
                }
                if (!is_assign) {
                    std::string name = expanded.substr(0, eq);
                    bool valid = !std::isdigit((unsigned char)name[0]);
                    for (char ch : name)
                        if (!std::isalnum((unsigned char)ch) && ch != '_') { valid = false; break; }
                    // Only treat as assignment if there's no space before '=' (no command name)
                    if (valid && name.find(' ') == std::string::npos) {
                        std::string val = expanded.substr(eq + 1);
                        if (!val.empty() && val.front() == '(') {
                            // Array assignment: NAME=(a b c)
                            size_t close = val.rfind(')');
                            std::string content = (close != std::string::npos) ? val.substr(1, close - 1) : val.substr(1);
                            g_arrays[name] = parse_array_words(content);
                        } else {
                            g_shell_vars[name] = val;
                        }
                        last_exit = 0;
                        is_assign = true;
                    }
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
            last_exit = g_builtin_exit;
            if (ll != "history") {
                hist.add(original);
                hist.save(paths.history_file);
            }
            continue;
        }

        // Process substitution: <(cmd) / >(cmd) → temp file paths
        expanded = expand_proc_subs(expanded);

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
        flush_proc_subs(paths, aliases, last_exit);

        hist.add(original);
        hist.save(paths.history_file);
    }

    return 0;
}
