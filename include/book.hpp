// Functionality for feature engineering of MBO (L3) orderbook data
#pragma once

#include "databento/datetime.hpp"
#include "databento/enums.hpp"
#include "databento/record.hpp"
#include <cstddef>
#include <cstdint>
#include <databento/record.hpp>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace db = databento;

/**
 * @struct TradeExecution
 * @brief Captures the specifics of a completed trade event.
 */
struct TradeExecution {
  int64_t price{0};
  uint32_t volume{0};
  db::Side side{db::Side::None}; // Aggressor side

  // Timestamp (Nanoseconds)
  // TODO: This is not the execution time, but rather
  // Databento capture time. Let be for now.
  db::UnixNanos ts_recv;

  bool IsValid() const { return price > 0; }
};

/**
 * @struct PriceLevel
 * @brief Represents an aggregated view of a single price point in the book.
 *
 * In MBO data, a price level is composed of multiple individual orders.
 * This struct provides the total volume and order count for that price.
 */
struct PriceLevel {
  int64_t price{db::kUndefPrice};
  uint32_t size{0};
  uint32_t count{0};

  bool IsEmpty() const { return price == db::kUndefPrice; }
  operator bool() const { return !IsEmpty(); }
};

std::ostream &operator<<(std::ostream &stream, const PriceLevel &level);

/**
 * @class Book
 * @brief Represents a Market-By-Order (L3) limit order book for a single
 * instrument.
 *
 * This class maintains the full state of the order book by tracking individual
 * orders by their ID. it supports structural updates (Add, Cancel, Modify,
 * Fill) and provides high-performance access to depth levels and feature
 * calculations.
 */
class Book {
public:
  Book() = default;

  /**
   * @brief Returns the best bid and best offer (Top of Book).
   * @return A pair of PriceLevel objects; check .IsEmpty() if no liquidity
   * exists.
   */
  std::pair<PriceLevel, PriceLevel> Bbo() const;

  /**
   * @brief Retrieves the price level at a specific depth index.
   * @param idx 0-based index (0 is best price, 1 is next level, etc.).
   * @return PriceLevel at that rank.
   */
  PriceLevel GetBidLevel(std::size_t idx = 0) const;
  PriceLevel GetAskLevel(std::size_t idx = 0) const;

  /**
   * @brief Retrieves the volume and count for an exact price point.
   * @param px The price to look up.
   * @return The PriceLevel at that price.
   * @throws std::invalid_argument if no liquidity exists at that price.
   */
  PriceLevel GetBidLevelByPx(int64_t px) const;
  PriceLevel GetAskLevelByPx(int64_t px) const;

  /**
   * @brief Accesses a specific resting order by its unique ID.
   * @param order_id The exchange-assigned order identifier.
   * @return Constant reference to the raw MboMsg for that order.
   */
  const db::MboMsg &GetOrder(uint64_t order_id);

  /**
   * @brief Calculates the quantity of volume ahead of a specific order.
   * @param order_id The exchange-assigned order identifier.
   * @return The total size of orders preceding this one at the same price.
   */
  uint32_t GetQueuePos(uint64_t order_id);

  /**
   * @brief Generates a flat vector of price-level pairs up to N depth.
   * @param level_count Number of levels to include from each side.
   * @return A vector of BidAskPair objects.
   */
  std::vector<db::BidAskPair> GetSnapshot(std::size_t level_count = 1) const;

  /**
   * @brief Processes an incoming MBO message and updates the book's state.
   * @param mbo The raw Databento MBO message.
   */
  void Apply(const db::MboMsg &mbo);

  /**
   * @brief Calculates the standard Top-of-Book Imbalance (OBI).
   * @details Formula: (BidSize - AskSize) / (BidSize + AskSize)
   * @return Normalized ratio [-1.0, 1.0].
   */
  double CalculateImbalance() const;

  /**
   * @brief Calculates imbalance across multiple price levels for smoother
   * signals.
   * @param depth Number of levels to aggregate.
   * @return Normalized ratio [0.0, 1.0] where 0.5 is neutral.
   */
  double CalculateDeepImbalance(std::size_t depth) const;

  /** @name Getters */
  ///@{
  uint64_t GetTotalTradeVolume() const { return total_trade_volume_; }
  ///@}

  // Trade
  const TradeExecution &GetLastExecution() const { return last_execution_; }

private:
  using LevelOrders = std::vector<db::MboMsg>;
  struct PriceAndSide {
    int64_t price;
    db::Side side;
  };
  using Orders = std::unordered_map<uint64_t, PriceAndSide>;
  using SideLevels = std::map<int64_t, LevelOrders>;

  // Static Utility
  static PriceLevel GetPriceLevel(int64_t price, const LevelOrders &level);
  static LevelOrders::iterator GetLevelOrder(LevelOrders &level,
                                             uint64_t order_id);

  // Structural Updates
  void Add(const db::MboMsg &mbo);
  void Cancel(const db::MboMsg &mbo);
  void Modify(const db::MboMsg &mbo);
  void Clear();
  void Fill(const db::MboMsg &mbo);

  // Information Tracking
  void Trade(const db::MboMsg &mbo);

  // Side Navigation
  SideLevels &GetSideLevels(db::Side side);
  LevelOrders &GetLevel(db::Side side, int64_t price);
  LevelOrders &GetOrInsertLevel(db::Side side, int64_t price);
  void RemoveLevel(db::Side side, int64_t price);

  // State
  Orders orders_by_id_;
  SideLevels offers_;
  SideLevels bids_;

  // Trade
  TradeExecution last_execution_{};

  // Total
  uint64_t total_trade_volume_{0};
};
