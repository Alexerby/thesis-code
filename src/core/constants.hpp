#pragma once
#include <string>

namespace constants {

constexpr double NANOS_PER_SECOND = 1'000'000'000.0;
constexpr double NANOS_PER_MINUTE = 60'000'000'000.0;

// Unit conversions from nanoseconds
constexpr double NS_TO_US = 1e-3;            ///< nanoseconds -> microseconds
constexpr double NS_TO_MS = 1e-6;            ///< nanoseconds -> milliseconds
constexpr double NS_TO_S  = 1e-9;            ///< nanoseconds -> seconds

// Time thresholds in microseconds (for order-age windowing)
constexpr double US_PER_MS  =      1'000.0;  ///<   1 millisecond
constexpr double US_PER_S   =  1'000'000.0;  ///<   1 second
constexpr double US_PER_MIN = 60'000'000.0;  ///<   1 minute

} // namespace constants
