#pragma once
#include <vector>
#include <string>
#include "../shell/aliases.hpp"

std::vector<std::string> completion_matches(const std::string& input,
                                            const Aliases& aliases);
