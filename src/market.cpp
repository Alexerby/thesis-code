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

// -------------------------------------------
// Feature Engineering
// -------------------------------------------

/**
* Calculates the orderbook imbalance for a specific publisher (venue).
* @param instrument_id The unique identifier for the instrument (ISIN/Symbol).
* @param publisher_id  The specific data source or venue (e.g., CME, NYSE).
* @return A ratio [0.0, 1.0] where > 0.5 indicates buy-side pressure on this venue.
* */
double Market::Imbalance(uint32_t instrument_id, uint16_t publisher_id) {
  const auto &book = GetBook(instrument_id, publisher_id);
  return book.CalculateImbalance();
}

/**
 * Calculates the consolidated order book imbalance for an instrument.
 * * Provides a "Global" view by aggregating liquidity across all venues 
 * (CME, ICE, etc.). Effectively measures the total market-wide buying 
 * versus selling pressure for a single ISIN.
 * * @param instrument_id The unique identifier for the financial instrument (ISIN/Symbol).
 * @return A ratio [0.0, 1.0] where 1.0 represents a market entirely dominated by 
 * bids (buy-side) across all active publishers.
 */
double Market::AggregatedImbalance(uint32_t instrument_id) {
  auto bbo = AggregatedBbo(instrument_id);

  double bid_sz = static_cast<double>(bbo.first.size);
  double ask_sz = static_cast<double>(bbo.second.size);

  if (bid_sz + ask_sz == 0)
    return 0.5;

  return bid_sz / (bid_sz + ask_sz);
}

/**
 * Calculates the aggregated order book imbalance across multiple depth levels and venues.
 * * Provides a "Global Deep Book" feature by summing the total bid and ask volume 
 * for the top N price levels across all active publishers. Aggregating multiple levels 
 * reduces noise from top-of-book "flickering" and helps detect layering-based 
 * spoofing strategies.
 * * @param instrument_id The unique identifier for the financial instrument.
 * @param depth The number of price levels (ticks) to aggregate from each side of the book.
 * @return A normalized ratio [0.0, 1.0]. A value of 0.5 indicates a balanced deep book; 
 * values nearing 1.0 indicate heavy buy-side concentration across the specified depth.
 */
double Market::AggregatedDeepImbalance(uint32_t instrument_id, std::size_t depth) {
    double total_bid_sz = 0;
    double total_ask_sz = 0;

    for (const auto& pub_book : GetBooksByPub(instrument_id)) {
        for (std::size_t i = 0; i < depth; ++i) {
            total_bid_sz += static_cast<double>(pub_book.book.GetBidLevel(i).size);
            total_ask_sz += static_cast<double>(pub_book.book.GetAskLevel(i).size);
        }
    }

    double total_vol = total_bid_sz + total_ask_sz;
    return (total_vol == 0) ? 0.0 : (total_bid_sz - total_ask_sz) / total_vol;
}

/**
 * Calculates the aggregated velocity (delta) in the order book imbalance 
 * between the current event and the previous state. 
 *
 * Serves as a high-frequency feature to detect "Balance Shocks"—sudden shifts 
 * in book pressure caused by aggressive layering, large cancellations, or 
 * sweeping the top of the book (Sweep-to-Fill order).
 *
 * @param instrument_id Unique identifier for the product (e.g., ES M6).
 * @param depth Number of price levels to aggregate.
 * @return The first-order difference ΔI ∈ [-2, 1.0] representing the 
 * direction and magnitude of the book shock.
 */
double Market::AggregatedImbalanceVelocity(uint32_t instrument_id, std::size_t depth) {

    // Calculate current imbalance
    double current_imb = AggregatedDeepImbalance(instrument_id, depth);

    // Lookup and update previous imbalance
    double prev_imb = current_imb;
    if (last_imbalances_[instrument_id].count(depth)) {
        prev_imb = last_imbalances_[instrument_id][depth];
    }

    // Calculate delta
    double vel = current_imb - prev_imb;
    last_imbalances_[instrument_id][depth] = current_imb;

    return vel;
}

// -------------------------------------------

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
