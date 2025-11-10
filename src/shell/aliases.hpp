#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

class Aliases {
public:
    // Set or update an alias
    void set(const std::string& name, const std::string& value);

    // Remove alias; returns true if removed
    bool remove(const std::string& name);

    // Retrieve alias value
    std::optional<std::string> get(const std::string& name) const;

    // All alias names (for completion)
    std::vector<std::string> names() const;

    // Persistence
    bool load(const std::string& file_path);
    bool save(const std::string& file_path) const;

private:
    std::map<std::string, std::string> data_;
};
