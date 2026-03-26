#pragma once

#include "book.hpp"
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

class Market {
public:
  struct PublisherBook {
    uint16_t publisher_id;
    Book book;
  };

  Market() = default;

  // Returns all venue-specific books for a given instrument
  const std::vector<PublisherBook> &GetBooksByPub(uint32_t instrument_id);

  // Returns a specific book for a unique instrument/publisher combo
  const Book &GetBook(uint32_t instrument_id, uint16_t publisher_id);

  // BBO for a specific publisher
  std::pair<PriceLevel, PriceLevel> Bbo(uint32_t instrument_id,
                                        uint16_t publisher_id);

  // Aggregated BBO across all publishers (The "Consolidated" Book)
  std::pair<PriceLevel, PriceLevel> AggregatedBbo(uint32_t instrument_id);

  // Routes the incoming MBO message to the correct internal Book
  void Apply(const db::MboMsg &mbo_msg);

private:
  // Instrument ID -> List of PublisherBooks
  std::unordered_map<uint32_t, std::vector<PublisherBook>> books_;
};
