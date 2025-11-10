#pragma once
#include <string>
#include <vector>
#include <functional>

using CompletionFunc = std::function<std::vector<std::string>(const std::string&)>;

std::string read_line_with_completion(const std::string& prompt, CompletionFunc complete);
