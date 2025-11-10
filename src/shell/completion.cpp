#include "completion.hpp"
#include <algorithm>
#include <cctype>

static const std::vector<std::string>& builtin_cmds() {
    static const std::vector<std::string> v = {
        "cd", "set", "alias", "unalias", "history",
        "exit", "quit"
    };
    return v;
}

static bool starts_with_ci(const std::string& s, const std::string& prefix) {
    if (prefix.size() > s.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        unsigned char a = (unsigned char)std::tolower(s[i]);
        unsigned char b = (unsigned char)std::tolower(prefix[i]);
        if (a != b) return false;
    }
    return true;
}

std::vector<std::string> completion_matches(const std::string& partial,
                                            const Aliases& aliases)
{
    std::vector<std::string> out;

    // Builtins
    for (auto& b : builtin_cmds()) {
        if (partial.empty() || starts_with_ci(b, partial))
            out.push_back(b);
    }

    // Alias names
    for (auto& name : aliases.names()) {
        if (partial.empty() || starts_with_ci(name, partial))
            out.push_back(name);
    }

    // De-dup, keep stable order
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
