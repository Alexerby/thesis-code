#include "data/market.hpp"

#include <algorithm>
#include <stdexcept>

#include "data/book.hpp"

const std::vector<Market::PublisherBook> &Market::GetBooksByPub(
    uint32_t instrument_id) {
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

BestBidOffer Market::Bbo(uint32_t instrument_id, uint16_t publisher_id) {
  const auto &book = GetBook(instrument_id, publisher_id);
  return book.Bbo();
}

std::pair<double, double> Market::GetTopNDepth(uint32_t instrument_id, int n) {
  double bid_vol = 0, ask_vol = 0;
  for (auto &pub_book : books_[instrument_id]) {
    auto [b, a] = pub_book.book.GetTopNDepth(n);
    bid_vol += b;
    ask_vol += a;
  }
  return {bid_vol, ask_vol};
}

std::pair<double, double> Market::GetTopNDepthExcluding(uint32_t instrument_id,
                                                         int n, int64_t price,
                                                         uint32_t size, Side side) {
  double bid_vol = 0, ask_vol = 0;
  for (auto &pub_book : books_[instrument_id]) {
    auto [b, a] = pub_book.book.GetTopNDepthExcluding(n, price, size, side);
    bid_vol += b;
    ask_vol += a;
  }
  return {bid_vol, ask_vol};
}

uint32_t Market::GetVolumeAhead(uint32_t instrument_id, uint64_t order_id) {
  for (auto &pub_book : books_[instrument_id]) {
    try {
      return pub_book.book.GetVolumeAhead(order_id);
    } catch (const std::invalid_argument &) {
      // Order not in this publisher's book — try the next one
    }
  }
  return 0;  // TOB order or order not yet tracked
}

BestBidOffer Market::AggregatedBbo(uint32_t instrument_id) {
  PriceLevel agg_bid;
  PriceLevel agg_ask;

  for (const auto &pub_book : GetBooksByPub(instrument_id)) {
    const auto bbo = pub_book.book.Bbo();
    const auto &bid = bbo.first;
    const auto &ask = bbo.second;

    if (bid) {
      if (agg_bid.IsEmpty() || bid.price > agg_bid.price) {
        agg_bid = bid;
      } else if (bid.price == agg_bid.price) {
        agg_bid.count += bid.count;
        agg_bid.size += bid.size;
      }
    }

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

double Market::AggregatedTotalVolume(uint32_t instrument_id,
                                     std::size_t depth) {
  double total_vol = 0;

  for (const auto &pub_book : GetBooksByPub(instrument_id)) {
    for (std::size_t i = 0; i < depth; ++i) {
      total_vol += static_cast<double>(pub_book.book.GetBidLevel(i).size);
      total_vol += static_cast<double>(pub_book.book.GetAskLevel(i).size);
    }
  }

  return total_vol;
}

double Market::AggregatedSideVolume(uint32_t instrument_id, std::size_t depth,
                                    bool is_bid) {
  double side_vol = 0;

  for (const auto &pub_book : GetBooksByPub(instrument_id)) {
    for (std::size_t i = 0; i < depth; ++i) {
      if (is_bid) {
        side_vol += static_cast<double>(pub_book.book.GetBidLevel(i).size);
      } else {
        side_vol += static_cast<double>(pub_book.book.GetAskLevel(i).size);
      }
    }
  }

  return side_vol;
}

double Market::AggregatedLevelVolume(uint32_t instrument_id, std::size_t depth,
                                     bool is_bid) {
  double level_vol = 0;
  if (depth == 0) return 0.0;

  for (const auto &pub_book : GetBooksByPub(instrument_id)) {
    if (is_bid) {
      level_vol +=
          static_cast<double>(pub_book.book.GetBidLevel(depth - 1).size);
    } else {
      level_vol +=
          static_cast<double>(pub_book.book.GetAskLevel(depth - 1).size);
    }
  }

  return level_vol;
}

PriceLevel Market::AggregatedBidLevel(uint32_t instrument_id, std::size_t depth_idx) {
  PriceLevel result;
  for (const auto &pub_book : GetBooksByPub(instrument_id)) {
    PriceLevel level = pub_book.book.GetBidLevel(depth_idx);
    if (!level.IsEmpty()) {
      if (result.IsEmpty() || level.price > result.price) {
        result = level;
      } else if (level.price == result.price) {
        result.size += level.size;
        result.count += level.count;
      }
    }
  }
  return result;
}

PriceLevel Market::AggregatedAskLevel(uint32_t instrument_id, std::size_t depth_idx) {
  PriceLevel result;
  for (const auto &pub_book : GetBooksByPub(instrument_id)) {
    PriceLevel level = pub_book.book.GetAskLevel(depth_idx);
    if (!level.IsEmpty()) {
      if (result.IsEmpty() || level.price < result.price) {
        result = level;
      } else if (level.price == result.price) {
        result.size += level.size;
        result.count += level.count;
      }
    }
  }
  return result;
}

double Market::Imbalance(uint32_t instrument_id, uint16_t publisher_id) {
  const auto &book = GetBook(instrument_id, publisher_id);
  return book.CalculateImbalance();
}

double Market::AggregatedImbalance(uint32_t instrument_id, std::size_t depth) {
  double total_bid_sz = 0;
  double total_ask_sz = 0;

  for (const auto &pub_book : GetBooksByPub(instrument_id)) {
    for (std::size_t i = 0; i < depth; ++i) {
      total_bid_sz += static_cast<double>(pub_book.book.GetBidLevel(i).size);
      total_ask_sz += static_cast<double>(pub_book.book.GetAskLevel(i).size);
    }
  }

  double total_vol = total_bid_sz + total_ask_sz;
  return (total_vol == 0) ? 0.0 : (total_bid_sz - total_ask_sz) / total_vol;
}

void Market::Apply(const db::MboMsg &mbo_msg) {
  auto &instrument_books = books_[mbo_msg.hd.instrument_id];

  auto it =
      std::find_if(instrument_books.begin(), instrument_books.end(),
                   [&mbo_msg](const PublisherBook &pub_book) {
                     return pub_book.publisher_id == mbo_msg.hd.publisher_id;
                   });

  if (it == instrument_books.end()) {
    instrument_books.emplace_back(
        PublisherBook{mbo_msg.hd.publisher_id, Book{}});
    it = std::prev(instrument_books.end());
  }

  it->book.Apply(mbo_msg);
}

MarketSnapshot Market::GetSnapshot(uint32_t inst_id, const std::string &symbol,
                                   std::size_t depth) {
  MarketSnapshot snap;
  snap.symbol = symbol;
  snap.imbalance = AggregatedImbalance(inst_id, 5);

  TradeExecution last_trade = GetLastTrade(inst_id);
  snap.last_price = static_cast<double>(last_trade.price) / 1e9;

  auto bbo = AggregatedBbo(inst_id);
  snap.best_bid = bbo.first.IsEmpty() ? 0.0 : static_cast<double>(bbo.first.price) / 1e9;
  snap.best_ask = bbo.second.IsEmpty() ? 0.0 : static_cast<double>(bbo.second.price) / 1e9;

  snap.bid_volumes.resize(depth);
  snap.ask_volumes.resize(depth);
  snap.bid_volumes_cum.resize(depth);
  snap.ask_volumes_cum.resize(depth);
  snap.bid_prices.resize(depth, 0.0);
  snap.ask_prices.resize(depth, 0.0);
  snap.bid_counts.resize(depth, 0);
  snap.ask_counts.resize(depth, 0);

  for (std::size_t d = 1; d <= depth; ++d) {
    snap.bid_volumes[d - 1] =
        static_cast<float>(AggregatedLevelVolume(inst_id, d, true));
    snap.ask_volumes[d - 1] =
        static_cast<float>(AggregatedLevelVolume(inst_id, d, false));
    snap.bid_volumes_cum[d - 1] =
        static_cast<float>(AggregatedSideVolume(inst_id, d, true));
    snap.ask_volumes_cum[d - 1] =
        static_cast<float>(AggregatedSideVolume(inst_id, d, false));

    PriceLevel bid_lvl = AggregatedBidLevel(inst_id, d - 1);
    PriceLevel ask_lvl = AggregatedAskLevel(inst_id, d - 1);
    snap.bid_prices[d - 1] = bid_lvl.IsEmpty() ? 0.0 : static_cast<double>(bid_lvl.price) / 1e9;
    snap.ask_prices[d - 1] = ask_lvl.IsEmpty() ? 0.0 : static_cast<double>(ask_lvl.price) / 1e9;
    snap.bid_counts[d - 1] = bid_lvl.count;
    snap.ask_counts[d - 1] = ask_lvl.count;
  }

  return snap;
}

TradeExecution Market::GetLastTrade(uint32_t instrument_id) const {
  TradeExecution latest_exec{};

  auto it = books_.find(instrument_id);
  if (it != books_.end()) {
    for (const auto &pub_book : it->second) {
      const auto &current_exec = pub_book.book.GetLastExecution();
      if (current_exec.ts_recv > latest_exec.ts_recv) {
        latest_exec = current_exec;
      }
    }
  }
  return latest_exec;
}
