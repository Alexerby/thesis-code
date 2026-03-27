#pragma once
#include <string>

namespace constants {
// 1e9 = 1 second
// 60 * 1e9 = 1 minute
constexpr double NANOS_PER_SECOND = 1'000'000'000.0;
constexpr double NANOS_PER_MINUTE = 60'000'000'000.0;

// Colors
const std::string RED = "\033[1;31m";
const std::string GRN = "\033[1;32m";
const std::string YEL = "\033[1;33m";
const std::string CYN = "\033[1;36m";
const std::string RESET = "\033[0m";

// Clear screen
const std::string CLEAR = "\033[2J\033[1;1H";

} // namespace constants
