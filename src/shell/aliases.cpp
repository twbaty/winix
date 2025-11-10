#include "aliases.hpp"

AliasManager::AliasManager() {}

void AliasManager::setAlias(const std::string& name, const std::string& value) {
    aliases[name] = value;
}

void AliasManager::removeAlias(const std::string& name) {
    aliases.erase(name);
}

bool AliasManager::hasAlias(const std::string& name) const {
    return aliases.find(name) != aliases.end();
}

std::string AliasManager::expand(const std::string& input) const {
    auto it = aliases.find(input);
    return (it != aliases.end()) ? it->second : input;
}

std::vector<std::pair<std::string, std::string>> AliasManager::listAliases() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& kv : aliases) out.push_back(kv);
    return out;
}
