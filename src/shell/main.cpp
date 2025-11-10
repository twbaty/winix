// Winix Shell (Monolithic Stable Edition)
// No tab completion. No external modules. Fully self-contained.

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

namespace fs = std::filesystem;

// --------------------------------------
// Utility helpers
// --------------------------------------
static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static bool is_quoted(const std::string& s) {
    return s.size() >= 2 &&
           ((s.front() == '"' && s.back() == '"') ||
            (s.front() == '\'' && s.back() == '\''));
}

static std::string unquote(const std::string& s) {
    if (is_quoted(s)) return s.substr(1, s.size() - 2);
    return s;
}

static std::string getenv_win(const std::string& name) {
    DWORD need = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
    if (!need) return {};
    std::string out(need, '\0');
    GetEnvironmentVariableA(name.c_str(), out.data(), need);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

// --------------------------------------
// Config paths for persistence
// --------------------------------------
struct Paths {
    std::string history_file;
    std::string aliases_file;
};

static std::string user_home() {
    auto h = getenv_win("USERPROFILE");
    if (!h.empty()) return h;
    return fs::current_path().string();
}

static Paths make_paths() {
    fs::path base = user_home();
    Paths p;
    p.history_file = (base / ".winix_history.txt").string();
    p.aliases_file = (base / ".winix_aliases").string();
    return p;
}

// --------------------------------------
// History
// --------------------------------------
struct History {
    std::vector<std::string> items;
    size_t max = 100;

    void load(const std::string& file) {
        items.clear();
        std::ifstream in(file);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (!line.empty()) items.push_back(line);
        }
        if (items.size() > max)
            items.erase(items.begin(), items.begin() + (items.size() - max));
    }

    void save(const std::string& file) const {
        std::ofstream out(file, std::ios::trunc);
        for (auto& s : items) out << s << "\n";
    }

    void add(const std::string& s) {
        std::string t = trim(s);
        if (t.empty()) return;
        items.erase(std::remove(items.begin(), items.end(), t), items.end());
        items.push_back(t);
        if (items.size() > max)
            items.erase(items.begin());
    }

    void print() const {
        int i = 1;
        for (auto& s : items)
            std::cout << i++ << "  " << s << "\n";
    }

    void clear() {
        items.clear();
    }
};

// --------------------------------------
// Aliases
// --------------------------------------
struct Aliases {
    std::map<std::string, std::string> map;

    void load(const std::string& file) {
        map.clear();
        std::ifstream in(file);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty()) continue;
            if (line[0] == '#') continue;

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            auto k = trim(line.substr(0, eq));
            auto v = trim(line.substr(eq + 1));
            v = unquote(v);
            if (!k.empty() && !v.empty())
                map[k] = v;
        }
    }

    void save(const std::string& file) const {
        std::ofstream out(file, std::ios::trunc);
        for (auto& kv : map)
            out << kv.first << "=" << kv.second << "\n";
    }

    void print() const {
        for (auto& kv : map)
            std::cout << "alias " << kv.first << "=\"" << kv.second << "\"\n";
    }

    void set(const std::string& k, const std::string& v) {
        map[k] = v;
    }

    bool remove(const std::string& k) {
        return map.erase(k) > 0;
    }

    std::optional<std::string> get(const std::string& k) const {
        auto it = map.find(k);
        if (it == map.end()) return std::nullopt;
        return it->second;
    }
};

// --------------------------------------
// Tokenizer
// --------------------------------------
static std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool in_s=false, in_d=false;

    for (char c : s) {
        if (c=='\'' && !in_d){ in_s=!in_s; cur.push_back(c); continue; }
        if (c=='"'  && !in_s){ in_d=!in_d; cur.push_back(c); continue; }

        if (!in_s && !in_d && std::isspace((unsigned char)c)) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// --------------------------------------
// Alias expansion
// --------------------------------------
static std::string expand_aliases(const std::string& line, const Aliases& aliases) {
    auto tok = tokenize(line);
    if (tok.empty()) return line;

    auto ali = aliases.get(tok[0]);
    if (!ali) return line;

    std::string out = *ali;
    for (size_t i = 1; i < tok.size(); ++i) {
        out.push_back(' ');
        out += tok[i];
    }
    return out;
}

// --------------------------------------
// Variable expansion ($VAR or %VAR%)
// --------------------------------------
static std::string expand_vars(const std::string& line) {
    std::string out;
    bool in_s=false, in_d=false;

    for (size_t i=0;i<line.size();++i){
        char c=line[i];

        if (c=='\'' && !in_d){ in_s=!in_s; out.push_back(c); continue; }
        if (c=='"'  && !in_s){ in_d=!in_d; out.push_back(c); continue; }

        if (!in_s) {
            if (c=='%') {
                size_t j=i+1;
                while (j<line.size() && line[j] != '%') j++;
                if (j<line.size()) {
                    auto name=line.substr(i+1, j-(i+1));
                    out += getenv_win(name);
                    i=j;
                    continue;
                }
            }
            if (c=='$') {
                size_t j=i+1;
                while (j<line.size() &&
                       (std::isalnum((unsigned char)line[j]) || line[j]=='_')) j++;
                auto name=line.substr(i+1, j-(i+1));
                out += getenv_win(name);
                i=j-1;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

// --------------------------------------
// Command execution
// --------------------------------------
static DWORD run_cmd(const std::string& cmd, bool wait=true) {
    std::string full = "cmd.exe /C " + cmd;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(
        NULL, (LPSTR)full.c_str(),
        NULL, NULL, TRUE, 0,
        NULL, NULL, &si, &pi
    );

    if (!ok) return GetLastError();

    if (!wait) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code=0;
    GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static DWORD run_builtin_cd(const std::vector<std::string>& tok) {
    if (tok.size()==1) {
        std::error_code ec;
        fs::current_path(user_home(), ec);
        if (ec) std::cerr << "cd: " << ec.message() << "\n";
        return 0;
    }
    std::string target = unquote(tok[1]);
    std::error_code ec;
    fs::current_path(target, ec);
    if (ec) {
        std::cerr << "cd: " << target << ": " << ec.message() << "\n";
        return 1;
    }
    return 0;
}

// --------------------------------------
// Builtins
// --------------------------------------
static bool handle_builtin(
    const std::string& line,
    Aliases& aliases,
    const Paths& paths,
    History& hist)
{
    std::string lower = to_lower(line);

    // history
    if (lower=="history") {
        hist.print();
        return true;
    }
    if (lower=="history -c") {
        hist.clear();
        hist.save(paths.history_file);
        return true;
    }

    // set NAME=VALUE
    if (lower.rfind("set ",0)==0) {
        auto rest = trim(line.substr(4));
        auto eq = rest.find('=');
        if (eq==std::string::npos) {
            std::cerr << "Usage: set NAME=VALUE\n";
            return true;
        }
        auto name = trim(rest.substr(0,eq));
        auto val  = trim(rest.substr(eq+1));
        val = unquote(val);
        SetEnvironmentVariableA(name.c_str(), val.c_str());
        return true;
    }

    // alias
    if (lower=="alias") {
        aliases.print();
        return true;
    }

    // unalias NAME
    if (lower.rfind("unalias ",0)==0) {
        auto k = trim(line.substr(8));
        if (aliases.remove(k))
            aliases.save(paths.aliases_file);
        else
            std::cerr << "unalias: " << k << " not found\n";
        return true;
    }

    // alias foo="bar"
    if (lower.rfind("alias ",0)==0) {
        auto rest = trim(line.substr(6));
        auto eq = rest.find('=');
        if (eq==std::string::npos) {
            std::cerr << "Usage: alias name=\"cmd\"\n";
            return true;
        }
        auto k = trim(rest.substr(0,eq));
        auto v = unquote(trim(rest.substr(eq+1)));
        aliases.set(k,v);
        aliases.save(paths.aliases_file);
        return true;
    }

    return false;
}

// --------------------------------------
// Main loop
// --------------------------------------
int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "Winix Shell — Stable Edition\n";

    Paths paths = make_paths();

    History hist;
    hist.load(paths.history_file);

    Aliases aliases;
    aliases.load(paths.aliases_file);

    while (true) {
        std::cout << fs::current_path().string() << " > ";
        std::string line;
        if (!std::getline(std::cin, line)) break;

        auto orig = trim(line);
        if (orig.empty()) continue;

        auto lower = to_lower(orig);
        if (lower=="exit" || lower=="quit") break;

        // alias expansion
        std::string ali_exp = expand_aliases(orig, aliases);

        // var expansion
        std::string exp = expand_vars(ali_exp);

        // builtins
        if (handle_builtin(exp, aliases, paths, hist)) {
            hist.add(orig);
            hist.save(paths.history_file);
            continue;
        }

        // cd
        auto tok = tokenize(exp);
        if (!tok.empty() && to_lower(tok[0])=="cd") {
            run_builtin_cd(tok);
            hist.add(orig);
            hist.save(paths.history_file);
            continue;
        }

        // external
        run_cmd(exp);
        hist.add(orig);
        hist.save(paths.history_file);
    }

    return 0;
}
