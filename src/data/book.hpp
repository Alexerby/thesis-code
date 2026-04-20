// Functionality for feature engineering of MBO (L3) orderbook data
#pragma once

#include <cstddef>
#include <cstdint>
#include <databento/datetime.hpp>
#include <databento/enums.hpp>
#include <databento/record.hpp>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/types.hpp"

namespace db = databento;

using BestBidOffer = std::pair<PriceLevel, PriceLevel>;

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
  BestBidOffer Bbo() const;

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
   * @brief Calculates total volume between this order and the BBO.
   *
   * @details
   * For a bid order at price P, "ahead" means closer to the best bid, i.e.
   * higher prices. For an ask order at price P, "ahead" means lower prices.
   *
   * The result is the sum of two parts:
   *   1. All volume at price levels strictly between P and the BBO
   *      (cross-level sum).
   *   2. Volume at price level P that arrived before this order
   *      (intra-level queue position via GetQueuePos).
   *
   * Example for a bid order at price 100:
   * @code
   *   price 103 → [X, Y]      ← BBO
   *   price 102 → [Z]         ┐
   *   price 101 → [A, B]      ┘ summed in part 1
   *   price 100 → [C, D, YOU] ← C and D summed in part 2
   *   price  99 → [E]         ← ignored (behind your order)
   * @endcode
   *
   * @param order_id The exchange-assigned order identifier.
   * @return Total volume ahead of this order (feature: `volume_ahead` in FeatureRecord).
   */
  uint32_t GetVolumeAhead(uint64_t order_id);

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

  /**
   * @brief Returns total bid and ask volume summed over the top @p n price
   *        levels on each side.
   */
  std::pair<double, double> GetTopNDepth(int n) const;

  /**
   * @brief Same as GetTopNDepth but subtracts one order's contribution from
   *        its side, giving the pre-add depth state.  If the order's price is
   *        not among the top @p n levels its contribution is zero and the
   *        result is identical to GetTopNDepth.
   */
  std::pair<double, double> GetTopNDepthExcluding(int n, int64_t order_price,
                                                   uint32_t order_size,
                                                   Side order_side) const;

  /** @name Getters */
  ///@{
  uint64_t GetTotalTradeVolume() const { return total_trade_volume_; }
  ///@}

  // To make last_execution_ public to class Market
  const TradeExecution &GetLastExecution() const { return last_execution_; }

 private:
  using LevelOrders = std::vector<db::MboMsg>;
  struct PriceAndSide {
    int64_t price;
    Side side;
  };
  using Orders = std::unordered_map<uint64_t, PriceAndSide>;
  using SideLevels = std::map<int64_t, LevelOrders>;

  // Static Utility
  static PriceLevel GetPriceLevel(int64_t price, const LevelOrders &level);
  static LevelOrders::iterator GetLevelOrder(LevelOrders &level,
                                             uint64_t order_id);
  static Side ConvertSide(db::Side side) {
    if (side == db::Side::Bid) return Side::Bid;
    if (side == db::Side::Ask) return Side::Ask;
    return Side::None;
  }

  // Structural Updates
  void Add(const db::MboMsg &mbo);
  void Cancel(const db::MboMsg &mbo);
  void Modify(const db::MboMsg &mbo);
  void Clear();
  void Fill(const db::MboMsg &mbo);

  // Information Tracking
  void Trade(const db::MboMsg &mbo);

  // Side Navigation
  SideLevels &GetSideLevels(Side side);
  LevelOrders &GetLevel(Side side, int64_t price);
  LevelOrders &GetOrInsertLevel(Side side, int64_t price);
  void RemoveLevel(Side side, int64_t price);

  // State
  Orders orders_by_id_;
  SideLevels offers_;
  SideLevels bids_;

  // Trade
  TradeExecution last_execution_{};

  // Total
  uint64_t total_trade_volume_{0};
};
