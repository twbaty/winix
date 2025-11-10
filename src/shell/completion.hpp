#pragma once
#include <functional>
#include <string>
#include <vector>

#include "aliases.hpp"

// Completion callback signature used by the line editor and main.
using CompletionFunc = std::function<std::vector<std::string>(const std::string& partial)>;

// Return completion candidates for a given partial word,
// using a small set of builtins + provided aliases.
std::vector<std::string> completion_matches(const std::string& partial,
                                            const Aliases& aliases);
