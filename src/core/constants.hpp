#pragma once
#include <string>

namespace constants {

constexpr double NANOS_PER_SECOND = 1'000'000'000.0;
constexpr double NANOS_PER_MINUTE = 60'000'000'000.0;

// Unit conversions from nanoseconds
constexpr double NS_TO_US = 1e-3;  ///< nanoseconds -> microseconds
constexpr double NS_TO_MS = 1e-6;  ///< nanoseconds -> milliseconds
constexpr double NS_TO_S = 1e-9;   ///< nanoseconds -> seconds

// Databento fixed-point price scale: raw_price / PRICE_SCALE = USD
constexpr int64_t PRICE_SCALE = 1'000'000'000;
// Minimum price increment for XNAS.ITCH equities priced above $1 (one cent)
constexpr int64_t XNAS_TICK_SIZE = 10'000'000;

// Time thresholds in microseconds (for order-age windowing)
constexpr double US_PER_MS = 1'000.0;        ///<   1 millisecond
constexpr double US_PER_S = 1'000'000.0;     ///<   1 second
constexpr double US_PER_MIN = 60'000'000.0;  ///<   1 minute

}  // namespace constants
