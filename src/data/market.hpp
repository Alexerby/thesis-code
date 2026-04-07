#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/book.hpp"

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

  void Apply(const db::MboMsg &mbo_msg);

  MarketSnapshot GetSnapshot(uint32_t inst_id, const std::string &symbol,
                             std::size_t depth);

  BestBidOffer AggregatedBbo(uint32_t instrument_id);

 private:
  std::unordered_map<uint32_t, std::vector<PublisherBook>> books_;
  std::unordered_map<uint32_t, std::unordered_map<std::size_t, double>>
      last_imbalances_;

  const Book &GetBook(uint32_t instrument_id, uint16_t publisher_id);
  const std::vector<PublisherBook> &GetBooksByPub(uint32_t instrument_id);

  BestBidOffer Bbo(uint32_t instrument_id, uint16_t publisher_id);
  double Imbalance(uint32_t instrument_id, uint16_t publisher_id);

  /**
   * @brief Calculates the imbalance up to 'depth'.
   *
   * @param instrument_id The instrument ID provided by the venue.
   * @return Imbalance \in [-1, 1]
   */
  double AggregatedImbalance(uint32_t instrument_id, std::size_t depth);
  double AggregatedTotalVolume(uint32_t instrument_id, std::size_t depth);
  double AggregatedSideVolume(uint32_t instrument_id, std::size_t depth,
                              bool is_bid);
  double AggregatedLevelVolume(uint32_t instrument_id, std::size_t depth,
                               bool is_bid);

  double GetPriceAtDepth(uint32_t inst_id, std::size_t depth, bool is_bid);
  TradeExecution GetLastTrade(uint32_t inst_id) const;
};
