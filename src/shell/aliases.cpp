#include "aliases.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

// simple trim
static std::string trim(std::string s) {
    auto issp = [](unsigned char c){ return std::isspace(c); };
    while (!s.empty() && issp((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && issp((unsigned char)s.back()))  s.pop_back();
    return s;
}

// Strip a single layer of matching surrounding quotes (" or ')
static std::string unquote(const std::string& s) {
    if (s.size() >= 2 &&
        ((s.front() == '"'  && s.back() == '"') ||
         (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

void Aliases::set(const std::string& name, const std::string& value) {
    if (name.empty()) return;
    data_[name] = value;
}

bool Aliases::remove(const std::string& name) {
    return data_.erase(name) > 0;
}

std::optional<std::string> Aliases::get(const std::string& name) const {
    auto it = data_.find(name);
    if (it == data_.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> Aliases::names() const {
    std::vector<std::string> out;
    out.reserve(data_.size());
    for (auto& kv : data_) out.push_back(kv.first);
    return out;
}

// Very simple "key=value" persistence (no escaping).
bool Aliases::load(const std::string& file_path) {
    data_.clear();
    std::ifstream in(file_path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        // Accept both "name=value" and bash-style "alias name=value"
        if (line.size() > 6 &&
            (line[0]=='a'||line[0]=='A') && (line[1]=='l'||line[1]=='L') &&
            (line[2]=='i'||line[2]=='I') && (line[3]=='a'||line[3]=='A') &&
            (line[4]=='s'||line[4]=='S') && line[5]==' ')
            line = trim(line.substr(6));

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = unquote(trim(line.substr(0, eq)));
        std::string val = unquote(trim(line.substr(eq + 1)));
        if (!key.empty()) data_[key] = val;
    }
    return true;
}

bool Aliases::save(const std::string& file_path) const {
    std::ofstream out(file_path, std::ios::trunc);
    if (!out) return false;
    for (auto& kv : data_) {
        out << kv.first << "=" << kv.second << "\n";
    }
    return true;
}
