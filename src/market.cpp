#include "market.hpp"
#include <algorithm>
#include <stdexcept>

const std::vector<Market::PublisherBook> &
Market::GetBooksByPub(uint32_t instrument_id) {
  return books_[instrument_id];
}

const Book &Market::GetBook(uint32_t instrument_id, uint16_t publisher_id) {
  const std::vector<PublisherBook> &instrument_books =
      GetBooksByPub(instrument_id);

  auto it = std::find_if(instrument_books.begin(), instrument_books.end(),
                         [publisher_id](const PublisherBook &pub_book) {
                           return pub_book.publisher_id == publisher_id;
                         });

  if (it == instrument_books.end()) {
    throw std::invalid_argument{"No book for publisher ID " +
                                std::to_string(publisher_id)};
  }
  return it->book;
}

std::pair<PriceLevel, PriceLevel> Market::Bbo(uint32_t instrument_id,
                                              uint16_t publisher_id) {
  const auto &book = GetBook(instrument_id, publisher_id);
  return book.Bbo();
}

std::pair<PriceLevel, PriceLevel>
Market::AggregatedBbo(uint32_t instrument_id) {
  PriceLevel agg_bid;
  PriceLevel agg_ask;

  for (const auto &pub_book : GetBooksByPub(instrument_id)) {
    const auto bbo = pub_book.book.Bbo();
    const auto &bid = bbo.first;
    const auto &ask = bbo.second;

    // Aggregate Bids: Keep highest price, sum size if prices are equal
    if (bid) {
      if (agg_bid.IsEmpty() || bid.price > agg_bid.price) {
        agg_bid = bid;
      } else if (bid.price == agg_bid.price) {
        agg_bid.count += bid.count;
        agg_bid.size += bid.size;
      }
    }

    // Aggregate Asks: Keep lowest price, sum size if prices are equal
    if (ask) {
      if (agg_ask.IsEmpty() || ask.price < agg_ask.price) {
        agg_ask = ask;
      } else if (ask.price == agg_ask.price) {
        agg_ask.count += ask.count;
        agg_ask.size += ask.size;
      }
    }
  }
  return {agg_bid, agg_ask};
}

void Market::Apply(const db::MboMsg &mbo_msg) {

  // Map all books to instrument IDs
  auto &instrument_books = books_[mbo_msg.hd.instrument_id];

  // Within the instrument search for publisher_id
  auto it =
      std::find_if(instrument_books.begin(), instrument_books.end(),
                   [&mbo_msg](const PublisherBook &pub_book) {
                     return pub_book.publisher_id == mbo_msg.hd.publisher_id;
                   });

  if (it == instrument_books.end()) {
    // If we haven't seen this publisher before, create a new book for them
    instrument_books.emplace_back(
        PublisherBook{mbo_msg.hd.publisher_id, Book{}});
    it = std::prev(instrument_books.end());
  }

  // Pass the message down to the specific Book for state update
  it->book.Apply(mbo_msg);
}
