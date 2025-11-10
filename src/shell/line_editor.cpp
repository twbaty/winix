#include "line_editor.hpp"
#include <iostream>

LineEditor::LineEditor(CompletionFunc completer)
    : completer_(std::move(completer)) {}

std::optional<std::string> LineEditor::read_line(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();

    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::nullopt; // EOF / Ctrl+Z
    }
    return line;
}

std::vector<std::string> LineEditor::suggest(const std::string& partial) const {
    if (!completer_) return {};
    return completer_(partial);
}
