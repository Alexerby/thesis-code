#pragma once

#include "core/book.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @struct MarketSnapshot
 * @brief A clean, UI-ready snapshot of an instrument's state.
 */
struct MarketSnapshot {
  std::string symbol;
  std::string timestamp;
  uint64_t msg_count = 0;
  double last_price = 0.0;
  double imbalance = 0.0;

  // Depth data
  std::vector<float> bid_volumes;
  std::vector<float> ask_volumes;
  std::vector<float> bid_volumes_cum;
  std::vector<float> ask_volumes_cum;
};

/**
 * @struct MarketState
 * @brief Represents a point-in-time snapshot of the market for a specific
 * symbol.
 */
struct MarketState {
  std::string symbol;    ///< Ticker symbol
  std::string timestamp; ///< ISO-8601 formatted event time
  uint64_t ts_recv;      ///< Nanoseconds since epoch

  std::pair<PriceLevel, PriceLevel> bbo; ///< Best Bid and Best Offer

  std::vector<std::pair<std::string, double>>
      imbalance_levels; ///< Multi-level imbalance signals
  std::vector<std::pair<std::string, double>>
      volume_levels; ///< Price/Volume levels for visualization

  int64_t last_trade_price;    ///< Price of the most recent execution
  uint32_t last_trade_volume;  ///< Size of the most recent execution
  uint64_t total_trade_volume; ///< Cumulative trade volume for the session
};

/**
 * @class Market
 * @brief Manages multiple order books across different instruments and
 * publishers.
 */
class Market {
public:
  struct PublisherBook {
    uint16_t publisher_id;
    Book book;
  };

  Market() = default;

  const Book &GetBook(uint32_t instrument_id, uint16_t publisher_id);
  const std::vector<PublisherBook> &GetBooksByPub(uint32_t instrument_id);

  std::pair<PriceLevel, PriceLevel> Bbo(uint32_t instrument_id,
                                        uint16_t publisher_id);
  std::pair<PriceLevel, PriceLevel> AggregatedBbo(uint32_t instrument_id);

  void Apply(const db::MboMsg &mbo_msg);

  double Imbalance(uint32_t instrument_id, uint16_t publisher_id);
  double AggregatedDeepImbalance(uint32_t instrument_id, std::size_t depth);
  double AggregatedImbalanceVelocity(uint32_t instrument_id, std::size_t depth);
  double AggregatedTotalVolume(uint32_t instrument_id, std::size_t depth);
  double AggregatedSideVolume(uint32_t instrument_id, std::size_t depth,
                              bool is_bid);
  double AggregatedLevelVolume(uint32_t instrument_id, std::size_t depth,
                               bool is_bid);

  double GetPriceAtDepth(uint32_t inst_id, std::size_t depth, bool is_bid);

  TradeExecution GetLastTrade(uint32_t inst_id) const;



  // TODO: These are too similar, should be able to get rid of one of them
  MarketSnapshot GetSnapshot(uint32_t inst_id, const std::string &symbol,
                             std::size_t depth);

  MarketState CaptureState(uint32_t inst_id, const std::vector<size_t> &depths,
                           const std::string &symbol, const std::string &ts,
                           uint64_t ts_nanos);

private:
  std::unordered_map<uint32_t, std::vector<PublisherBook>> books_;
  std::unordered_map<uint32_t, std::unordered_map<std::size_t, double>>
      last_imbalances_;
};
