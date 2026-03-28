#pragma once

#include "book.hpp"
#include "visualizer.hpp"
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @class Market
 * @brief Manages multiple order books across different instruments and
 * publishers.
 *
 * The Market class acts as the central aggregator for all market data. it
 * routes incoming MBO messages to venue-specific books and provides methods for
 * generating consolidated "Global" features like Aggregated BBO and Deep
 * Imbalance.
 */
class Market {
public:
  struct PublisherBook {
    uint16_t publisher_id;
    Book book;
  };

  Market() = default;

  /**
   * @brief Retrieves all publisher-specific books for a given instrument.
   * @param instrument_id The unique identifier for the financial instrument.
   * @return A constant reference to the vector of PublisherBook objects.
   */
  const std::vector<PublisherBook> &GetBooksByPub(uint32_t instrument_id);

  /**
   * @brief Retrieves a specific order book for an instrument-publisher pair.
   * @param instrument_id The unique identifier for the instrument.
   * @param publisher_id The unique identifier for the venue/publisher.
   * @return A constant reference to the specific Book.
   * @throws std::invalid_argument If no book exists for the given publisher.
   */
  const Book &GetBook(uint32_t instrument_id, uint16_t publisher_id);

  /**
   * @brief Returns the Best-Bid-Offer (BBO) for a specific publisher.
   * @param instrument_id The unique identifier for the instrument.
   * @param publisher_id The unique identifier for the venue.
   * @return A pair of PriceLevel objects representing the top-of-book.
   */
  std::pair<PriceLevel, PriceLevel> Bbo(uint32_t instrument_id,
                                        uint16_t publisher_id);

  /**
   * @brief Calculates top-of-execution book imbalance for a specific venue.
   * @param instrument_id Unique instrument identifier.
   * @param publisher_id Unique venue identifier.
   * @return Normalized ratio [-1.0, 1.0].
   */
  double Imbalance(uint32_t instrument_id, uint16_t publisher_id);

  /**
   * @brief Aggregates the Best-Bid-Offer across all active publishers.
   * @details Creates a consolidated view of the top-of-book by selecting the
   * highest bid and lowest ask across all venues. Sums sizes for equal prices.
   * @param instrument_id The unique identifier for the instrument.
   * @return A pair of PriceLevel objects representing the consolidated BBO.
   */
  std::pair<PriceLevel, PriceLevel> AggregatedBbo(uint32_t instrument_id);

  /**
   * @brief Routes an incoming MBO message to its corresponding publisher's
   * book.
   * @param mbo_msg The raw Databento MBO message.
   */
  void Apply(const db::MboMsg &mbo_msg);

  /**
   * @brief Calculates global imbalance across all venues up to a specific
   * depth.
   * @param instrument_id Unique instrument identifier.
   * @param depth Number of price levels to include.
   * @return Normalized ratio [-1.0, 1.0]. 0.0 is neutral.
   */
  double AggregatedDeepImbalance(uint32_t instrument_id, std::size_t depth);

  /**
   * @brief Tracks the change in global imbalance over time (Imbalance
   * Velocity).
   * @param instrument_id Unique instrument identifier.
   * @param depth Number of price levels to aggregate.
   * @return The first-order difference in imbalance ratio.
   */
  double AggregatedImbalanceVelocity(uint32_t instrument_id, std::size_t depth);

  /**
   * @brief Calculates total liquidity across all publishers up to a specific
   * depth.
   * @param instrument_id Unique instrument identifier.
   * @param depth Number of price levels to include in the sum.
   * @return Total volume (sum of sizes) across both sides and all venues.
   */
  double AggregatedTotalVolume(uint32_t instrument_id, std::size_t depth);

  /**
   * @brief Calculates total liquidity for one side of the book across all
   * venues.
   * @param instrument_id Unique instrument identifier.
   * @param depth Number of price levels to include.
   * @param is_bid True to aggregate bid side, false for ask side.
   * @return Total volume for the specified side.
   */
  double AggregatedSideVolume(uint32_t instrument_id, std::size_t depth,
                              bool is_bid);

  /**
   * @brief Identifies the best available price at a specific depth across all
   * venues.
   * @param inst_id Unique instrument identifier.
   * @param depth The 1-based depth level.
   * @param is_bid True to search bid side, false for ask side.
   * @return The best price at that depth (scaled by 1e-9).
   */
  double GetPriceAtDepth(uint32_t inst_id, std::size_t depth, bool is_bid);

  /**
   * @brief Iterate through all Books for an instrument id and find the
   * execution price of the last ts_recv.
   * @param inst_id Unique instrument identifier.
   * @return { Price, Time }
   */
  TradeExecution GetLastTrade(uint32_t inst_id) const;

  /**
   * @brief Generates a synchronized snapshot of the entire market state.
   * @param inst_id Unique instrument identifier.
   * @param depths Vector of depth levels to include in the snapshot.
   * @param symbol Human-readable ticker symbol.
   * @param ts ISO-8601 formatted event timestamp.
   * @return A complete MarketState object.
   */
  MarketState CaptureState(uint32_t inst_id, const std::vector<size_t> &depths,
                           const std::string &symbol, const std::string &ts,
                           uint64_t ts_nanos);

private:
  // Instrument ID -> List of PublisherBooks (Venues)
  std::unordered_map<uint32_t, std::vector<PublisherBook>> books_;

  // Tracks the previous imbalance state to calculate velocity/delta signals.
  std::unordered_map<uint32_t, std::unordered_map<std::size_t, double>>
      last_imbalances_;
};
