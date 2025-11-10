#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Same signature as declared in completion.hpp, duplicated here to keep this header standalone.
// If both headers are included, the type is identical.
using CompletionFunc = std::function<std::vector<std::string>(const std::string& partial)>;

// Minimal line editor wrapper. For now it delegates to std::getline,
// but keeps a pluggable completion hook so we can extend to real TAB handling later.
class LineEditor {
public:
    explicit LineEditor(CompletionFunc completer = nullptr);

    // Prints prompt, reads a line. Returns std::nullopt on EOF.
    std::optional<std::string> read_line(const std::string& prompt);

    // For now, a helper to dump suggestions if caller wants to preview them.
    std::vector<std::string> suggest(const std::string& partial) const;

private:
    CompletionFunc completer_;
};
