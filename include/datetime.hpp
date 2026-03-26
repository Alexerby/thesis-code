#pragma once
#include "constants.hpp"
#include <chrono>
#include <cstdint>
#include <date/date.h>
#include <string>
#include <sys/types.h>

namespace utils {

// Convert UnixEpoch timestamp to human-readable string
inline std::string epoch_to_str(uint64_t nanos) {
  std::chrono::nanoseconds ns{nanos};
  auto tp = std::chrono::system_clock::time_point(
      std::chrono::duration_cast<std::chrono::system_clock::duration>(ns));
  return date::format("%F %T", tp);
}

inline double to_minutes(uint64_t nanos) {
  return static_cast<double>(nanos) / constants::NANOS_PER_MINUTE;
};

} // namespace utils
