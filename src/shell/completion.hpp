#pragma once
#include <functional>
#include <string>
#include <vector>

#include "aliases.hpp"

// Completion callback signature used by the line editor and main.
// line_prefix is everything in the buffer to the left of the word being completed.
using CompletionFunc = std::function<std::vector<std::string>(const std::string& partial,
                                                              const std::string& line_prefix)>;

// Return completion candidates for a given partial word.
// line_prefix is used for context-aware filtering (e.g. "cd " → dirs only).
std::vector<std::string> completion_matches(const std::string& partial,
                                            const Aliases& aliases,
                                            const std::string& line_prefix = "");
