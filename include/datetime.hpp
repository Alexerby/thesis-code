#pragma once

#include <chrono>
#include <cstdint>
#include <databento/datetime.hpp>
#include <date/date.h>
#include <string>

/**
 * @namespace db
 * @brief Alias for the databento namespace to maintain brevity in utility
 * functions.
 */
namespace db = databento;

/**
 * @namespace utils
 * @brief General purpose utility functions for time conversion and market data
 * arithmetic.
 */
namespace utils {

/**
 * @brief Converts a standard system clock time_point to a human-readable UTC
 * string.
 * * @param tp The std::chrono::system_clock::time_point to format.
 * @return A string in the format "YYYY-MM-DD HH:MM:SS".
 */
inline std::string epoch_to_str(std::chrono::system_clock::time_point tp) {
  return date::format("%F %T", tp);
}

/**
 * @brief Converts Databento's high-precision UnixNanos to a human-readable UTC
 * string.
 * * This overload handles the specific duration type used by the Databento API.
 * * @param tp The db::UnixNanos time point from a market message.
 * @return A string in the format "YYYY-MM-DD HH:MM:SS".
 */
inline std::string epoch_to_str(db::UnixNanos tp) {
  return epoch_to_str(
      std::chrono::time_point_cast<std::chrono::system_clock::duration>(tp));
}

/**
 * @brief Converts a raw nanosecond integer to a human-readable UTC string.
 * * Useful for logging legacy timestamps or raw offsets from the Unix epoch.
 * * @param nanos The number of nanoseconds since 1970-01-01 00:00:00 UTC.
 * @return A string in the format "YYYY-MM-DD HH:MM:SS".
 */
inline std::string epoch_to_str(uint64_t nanos) {
  std::chrono::nanoseconds duration{nanos};
  std::chrono::system_clock::time_point tp{
      std::chrono::duration_cast<std::chrono::system_clock::duration>(
          duration)};
  return epoch_to_str(tp);
}

/**
 * @brief Calculates the elapsed time between two market events in milliseconds.
 * * Essential for analyzing the lifecycle of a spoofing order or the speed of
 * market impact.
 * * @param start The earlier timestamp (e.g., an Order Add).
 * @param end The later timestamp (e.g., a Trade or Cancel).
 * @return The duration in milliseconds as a double.
 */
inline double time_since_ms(db::UnixNanos start, db::UnixNanos end) {
  auto duration = end - start;
  return std::chrono::duration<double, std::milli>(duration).count();
}

/**
 * @brief Converts a nanosecond duration to minutes.
 * * @param nanos Raw nanosecond count.
 * @return The equivalent duration in fractional minutes.
 */
inline double to_minutes(uint64_t nanos) {
  // 60,000,000,000 nanoseconds in a minute
  return static_cast<double>(nanos) / 60000000000.0;
}

} // namespace utils
