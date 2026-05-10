#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Same signature as declared in completion.hpp, duplicated here to keep this header standalone.
// line_prefix is everything in the buffer to the left of the word being completed.
using CompletionFunc = std::function<std::vector<std::string>(const std::string& partial,
                                                              const std::string& line_prefix)>;

class LineEditor {
public:
    explicit LineEditor(CompletionFunc completer = nullptr,
                        const std::vector<std::string>* history = nullptr);

    // Prints prompt, reads a line using raw console input.
    // Returns std::nullopt on EOF (Ctrl+Z / Ctrl+D on empty line).
    std::optional<std::string> read_line(const std::string& prompt);

    std::vector<std::string> suggest(const std::string& partial) const;

private:
    CompletionFunc completer_;
    const std::vector<std::string>* history_ = nullptr;
};
