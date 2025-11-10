#pragma once
#include <string>
#include <unordered_map>
#include <vector>

class AliasManager {
public:
    AliasManager();

    void setAlias(const std::string& name, const std::string& value);
    void removeAlias(const std::string& name);
    bool hasAlias(const std::string& name) const;
    std::string expand(const std::string& input) const;
    std::vector<std::pair<std::string, std::string>> listAliases() const;

private:
    std::unordered_map<std::string, std::string> aliases;
};
