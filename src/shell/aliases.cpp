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
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
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
