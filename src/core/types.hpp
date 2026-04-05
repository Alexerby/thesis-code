#pragma once

#include <cstdint>
#include <iostream>
#include <limits>

/**
 * @enum Side
 * @brief Represents the side of the market (Bid or Ask).
 */
enum class Side : uint8_t { None = 0, Bid = 1, Ask = 2 };

/**
 * @brief Undefined price constant.
 */
static constexpr int64_t kUndefPrice = std::numeric_limits<int64_t>::max();

/**
 * @struct TradeExecution
 * @brief Captures the specifics of a completed trade event.
 */
struct TradeExecution {
  int64_t price{0};
  uint32_t volume{0};
  Side side{Side::None};
  uint64_t ts_recv{0};  // Unix nanoseconds

  bool IsValid() const { return price > 0 && price != kUndefPrice; }
};

/**
 * @struct PriceLevel
 * @brief Represents an aggregated view of a single price point in the book.
 */
struct PriceLevel {
  int64_t price{kUndefPrice};
  uint32_t size{0};
  uint32_t count{0};

  bool IsEmpty() const { return price == kUndefPrice; }
  operator bool() const { return !IsEmpty(); }
};

inline std::ostream &operator<<(std::ostream &stream, const PriceLevel &level) {
  if (level.IsEmpty()) {
    stream << "EMPTY";
  } else {
    stream << level.price << " @ " << level.size << " (" << level.count << ")";
  }
  return stream;
}
