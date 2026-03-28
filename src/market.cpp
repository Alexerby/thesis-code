#include "market.hpp"
#include "databento/datetime.hpp"

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

double Market::GetPriceAtDepth(uint32_t inst_id, std::size_t depth,
                               bool is_bid) {
  auto &instr_books = books_[inst_id];
  if (instr_books.empty())
    return 0.0;

  int64_t global_price = is_bid ? 0 : std::numeric_limits<int64_t>::max();

  for (const auto &pub_book : instr_books) {
    const auto level = is_bid ? pub_book.book.GetBidLevel(depth - 1)
                              : pub_book.book.GetAskLevel(depth - 1);

    if (level.price == 0 || level.price == std::numeric_limits<int64_t>::max())
      continue;

    if (is_bid) {
      if (level.price > global_price)
        global_price = level.price;
    } else {
      if (level.price < global_price)
        global_price = level.price;
    }
  }

  if (global_price == 0 || global_price == std::numeric_limits<int64_t>::max())
    return 0.0;

  return static_cast<double>(global_price) / 1e9;
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

double Market::Imbalance(uint32_t instrument_id, uint16_t publisher_id) {
  const auto &book = GetBook(instrument_id, publisher_id);
  return book.CalculateImbalance();
}

double Market::AggregatedDeepImbalance(uint32_t instrument_id,
                                       std::size_t depth) {
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

double Market::AggregatedImbalanceVelocity(uint32_t instrument_id,
                                           std::size_t depth) {
  double current_imb = AggregatedDeepImbalance(instrument_id, depth);

  double prev_imb = current_imb;
  if (last_imbalances_[instrument_id].count(depth)) {
    prev_imb = last_imbalances_[instrument_id][depth];
  }

  double vel = current_imb - prev_imb;
  last_imbalances_[instrument_id][depth] = current_imb;

  return vel;
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

MarketState Market::CaptureState(uint32_t inst_id,
                                 const std::vector<size_t> &depths,
                                 const std::string &symbol,
                                 const std::string &ts,
                                 uint64_t ts_nanos) {
  MarketState state;
  state.symbol = symbol;
  state.timestamp = ts;
  state.ts_recv = ts_nanos;
  state.bbo = AggregatedBbo(inst_id);

  state.last_trade_price = db::kUndefPrice;
  state.total_trade_volume = 0;
  db::UnixNanos latest_ts{};

  if (books_.count(inst_id)) {
    for (const auto &pub_book : books_.at(inst_id)) {
      state.total_trade_volume += pub_book.book.GetTotalTradeVolume();

      // publishers
      const auto &exec = pub_book.book.GetLastExecution();
      if (exec.ts_recv > latest_ts && exec.price != db::kUndefPrice) {
        state.last_trade_price = exec.price;
        latest_ts = exec.ts_recv;
      }
    }
  }

  for (auto d : depths) {
    state.imbalance_levels.push_back({"L" + std::to_string(d) + " imbalance",
                                      AggregatedDeepImbalance(inst_id, d)});
    state.imbalance_levels.push_back({"L" + std::to_string(d) + " velocity",
                                      AggregatedImbalanceVelocity(inst_id, d)});

    state.volume_levels.push_back({"L" + std::to_string(d) + "_BidVol",
                                   AggregatedSideVolume(inst_id, d, true)});
    state.volume_levels.push_back({"L" + std::to_string(d) + "_AskVol",
                                   AggregatedSideVolume(inst_id, d, false)});

    state.volume_levels.push_back(
        {"B_P" + std::to_string(d), GetPriceAtDepth(inst_id, d, true)});
    state.volume_levels.push_back(
        {"A_P" + std::to_string(d), GetPriceAtDepth(inst_id, d, false)});
  }
  return state;
}

TradeExecution Market::GetLastTrade(uint32_t inst_id) const {
  TradeExecution latest_exec{};

  auto it = books_.find(inst_id);
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
