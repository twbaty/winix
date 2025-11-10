#include "completion.hpp"
#include <filesystem>

std::vector<std::string> completion_matches(const std::string& input,
                                            const Aliases& aliases)
{
    std::vector<std::string> out;

    // Alias completion (first token)
    auto pos = input.find(' ');
    std::string first = (pos == std::string::npos) ? input : input.substr(0,pos);

    for (auto& [name, val] : aliases.map) {
        if (name.rfind(first, 0) == 0)
            out.push_back(name);
    }

    // Filesystem completion
    namespace fs = std::filesystem;
    fs::path prefix = input;
    fs::path dir = prefix.parent_path();
    std::string partial = prefix.filename().string();

    if (dir.empty()) dir = fs::current_path();

    if (fs::exists(dir)) {
        for (auto& entry : fs::directory_iterator(dir)) {
            auto name = entry.path().filename().string();
            if (name.rfind(partial, 0) == 0)
                out.push_back((dir / name).string());
        }
    }

    return out;
}
