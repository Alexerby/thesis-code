#include "data/book.hpp"

#include <algorithm>
#include <cstdint>

#include "core/logging.hpp"
#include "databento/pretty.hpp"
#include "databento/record.hpp"

std::pair<PriceLevel, PriceLevel> Book::Bbo() const {
  return {GetBidLevel(), GetAskLevel()};
}

PriceLevel Book::GetBidLevel(std::size_t idx) const {
  if (bids_.size() > idx) {
    auto level_it = bids_.rbegin();
    std::advance(level_it, idx);
    return GetPriceLevel(level_it->first, level_it->second);
  }
  return PriceLevel{};
}

PriceLevel Book::GetAskLevel(std::size_t idx) const {
  if (offers_.size() > idx) {
    auto level_it = offers_.begin();
    std::advance(level_it, idx);
    return GetPriceLevel(level_it->first, level_it->second);
  }
  return PriceLevel{};
}

PriceLevel Book::GetBidLevelByPx(int64_t px) const {
  auto level_it = bids_.find(px);
  if (level_it == bids_.end()) {
    throw std::invalid_argument{"No bid level at " +
                                db::pretty::PxToString(px)};
  }
  return GetPriceLevel(px, level_it->second);
}

PriceLevel Book::GetAskLevelByPx(int64_t px) const {
  auto level_it = offers_.find(px);
  if (level_it == offers_.end()) {
    throw std::invalid_argument{"No ask level at " +
                                db::pretty::PxToString(px)};
  }
  return GetPriceLevel(px, level_it->second);
}

const db::MboMsg &Book::GetOrder(uint64_t order_id) {
  auto order_it = orders_by_id_.find(order_id);
  if (order_it == orders_by_id_.end()) {
    throw std::invalid_argument{"No order with ID " + std::to_string(order_id)};
  }
  auto &level = GetLevel(order_it->second.side, order_it->second.price);
  return *GetLevelOrder(level, order_id);
}

uint32_t Book::GetQueuePos(uint64_t order_id) {
  auto order_it = orders_by_id_.find(order_id);
  if (order_it == orders_by_id_.end()) {
    throw std::invalid_argument{"No order with ID " + std::to_string(order_id)};
  }
  const auto &level_it = GetLevel(order_it->second.side, order_it->second.price);
  uint32_t prior_size = 0;
  for (const auto &order : level_it) {
    if (order.order_id == order_id) break;
    prior_size += order.size;
  }
  return prior_size;
}

std::vector<db::BidAskPair> Book::GetSnapshot(std::size_t level_count) const {
  std::vector<db::BidAskPair> res;
  for (size_t i = 0; i < level_count; ++i) {
    db::BidAskPair ba_pair{db::kUndefPrice, db::kUndefPrice, 0, 0, 0, 0};
    auto bid = GetBidLevel(i);
    if (bid) {
      ba_pair.bid_px = bid.price;
      ba_pair.bid_sz = bid.size;
      ba_pair.bid_ct = bid.count;
    }
    auto ask = GetAskLevel(i);
    if (ask) {
      ba_pair.ask_px = ask.price;
      ba_pair.ask_sz = ask.size;
      ba_pair.ask_ct = ask.count;
    }
    res.emplace_back(ba_pair);
  }
  return res;
}

void Book::Apply(const db::MboMsg &mbo) {
  switch (mbo.action) {
    case db::Action::Clear: {
      Clear();
      break;
    }
    case db::Action::Add: {
      Add(mbo);
      break;
    }
    case db::Action::Cancel: {
      Cancel(mbo);
      break;
    }
    case db::Action::Modify: {
      Modify(mbo);

      // For me to identify if Action::Modify has been called.
      // This should not be the case and therefore marked as CRITICAL.
      Logger logger("action_modification.log");
      logger.log(CRITICAL, "Book::Apply | Case Action::Modify called.");
      break;
    }
    case db::Action::Fill: {
      Fill(mbo);
      break;
    }
    case db::Action::Trade: {
      Trade(mbo);
      break;
    }
    default:
      break;
  }
}

double Book::CalculateImbalance() const {
  auto bid = GetBidLevel();
  auto ask = GetAskLevel();

  double bid_sz = static_cast<double>(bid.size);
  double ask_sz = static_cast<double>(ask.size);

  double total_vol = bid_sz + ask_sz;
  if (total_vol == 0) return 0.0;

  return (bid_sz - ask_sz) / total_vol;
}

double Book::CalculateDeepImbalance(std::size_t depth) const {
  double total_bid_sz = 0;
  double total_ask_sz = 0;

  for (std::size_t i = 0; i < depth; ++i) {
    total_bid_sz += static_cast<double>(GetBidLevel(i).size);
    total_ask_sz += static_cast<double>(GetAskLevel(i).size);
  }

  double total_vol = total_bid_sz + total_ask_sz;
  return (total_vol == 0) ? 0.5 : (total_bid_sz / total_vol);
}

PriceLevel Book::GetPriceLevel(int64_t price, const LevelOrders &level) {
  PriceLevel res{price};
  for (const auto &order : level) {
    if (!order.flags.IsTob()) ++res.count;
    res.size += order.size;
  }
  return res;
}

Book::LevelOrders::iterator Book::GetLevelOrder(LevelOrders &level,
                                                uint64_t order_id) {
  return std::find_if(
      level.begin(), level.end(),
      [order_id](const db::MboMsg &o) { return o.order_id == order_id; });
}

void Book::Clear() {
  orders_by_id_.clear();
  offers_.clear();
  bids_.clear();
}

void Book::Add(const db::MboMsg &mbo) {
  Side side = ConvertSide(mbo.side);
  if (mbo.flags.IsTob()) {
    SideLevels &levels = GetSideLevels(side);
    levels.clear();
    if (mbo.price != db::kUndefPrice) levels[mbo.price] = {mbo};
  } else {
    LevelOrders &level = GetOrInsertLevel(side, mbo.price);
    level.emplace_back(mbo);
    orders_by_id_.emplace(mbo.order_id, PriceAndSide{mbo.price, side});
  }
}

void Book::Cancel(const db::MboMsg &mbo) {
  Side side = ConvertSide(mbo.side);
  auto &side_levels = GetSideLevels(side);
  if (side_levels.find(mbo.price) == side_levels.end()) return;

  LevelOrders &level = side_levels[mbo.price];
  auto it = GetLevelOrder(level, mbo.order_id);

  if (it == level.end()) return;

  if (it->size <= mbo.size) {
    orders_by_id_.erase(mbo.order_id);
    level.erase(it);
    if (level.empty()) RemoveLevel(side, mbo.price);
  } else {
    it->size -= mbo.size;
  }
}

void Book::Fill(const db::MboMsg &mbo) {
  Side side = ConvertSide(mbo.side);
  auto &side_levels = GetSideLevels(side);
  if (side_levels.find(mbo.price) == side_levels.end()) return;

  LevelOrders &level = side_levels[mbo.price];
  auto it = GetLevelOrder(level, mbo.order_id);

  if (it == level.end()) return;

  if (it->size <= mbo.size) {
    orders_by_id_.erase(mbo.order_id);
    level.erase(it);
    if (level.empty()) RemoveLevel(side, mbo.price);
  } else {
    it->size -= mbo.size;
  }
}

void Book::Trade(const db::MboMsg &mbo) {
  if (mbo.price != db::kUndefPrice) {
    last_execution_.price = mbo.price;
    last_execution_.volume = mbo.size;
    last_execution_.side = ConvertSide(mbo.side);
    last_execution_.ts_recv = mbo.ts_recv.time_since_epoch().count();

    this->total_trade_volume_ += mbo.size;
  }
}

void Book::Modify(const db::MboMsg &mbo) {
  auto it = orders_by_id_.find(mbo.order_id);
  Side side = ConvertSide(mbo.side);
  if (it == orders_by_id_.end()) {
    Add(mbo);
    return;
  }

  auto prev_price = it->second.price;
  auto &side_levels = GetSideLevels(side);
  if (side_levels.find(prev_price) == side_levels.end()) return;

  LevelOrders &prev_lvl = side_levels[prev_price];
  auto order_it = GetLevelOrder(prev_lvl, mbo.order_id);

  if (order_it == prev_lvl.end()) {
    orders_by_id_.erase(it);
    Add(mbo);
    return;
  }

  if (prev_price != mbo.price) {
    it->second.price = mbo.price;
    prev_lvl.erase(order_it);
    if (prev_lvl.empty()) RemoveLevel(side, prev_price);
    GetOrInsertLevel(side, mbo.price).emplace_back(mbo);
  } else if (order_it->size < mbo.size) {
    prev_lvl.erase(order_it);
    prev_lvl.emplace_back(mbo);
  } else {
    order_it->size = mbo.size;
  }
}

Book::SideLevels &Book::GetSideLevels(Side side) {
  return (side == Side::Ask) ? offers_ : bids_;
}

Book::LevelOrders &Book::GetLevel(Side side, int64_t price) {
  auto &levels = GetSideLevels(side);
  auto it = levels.find(price);
  if (it == levels.end()) throw std::invalid_argument("Level not found");
  return it->second;
}

Book::LevelOrders &Book::GetOrInsertLevel(Side side, int64_t price) {
  return GetSideLevels(side)[price];
}

void Book::RemoveLevel(Side side, int64_t price) {
  GetSideLevels(side).erase(price);
}

uint32_t Book::GetVolumeAhead(uint64_t order_id) {

  // Find order
  auto order_it = orders_by_id_.find(order_id);
  if (order_it == orders_by_id_.end()) {
    throw std::invalid_argument{"No order with ID " + std::to_string(order_id)};
  }

  uint32_t volume_ahead = 0;
  int64_t price = order_it->second.price;

  // Iterator up until upper bound, then add upp cumulative sum
  // of order up until that point. 
  if (order_it->second.side == Side::Bid) {
    auto it = bids_.upper_bound(price);
    while (it != bids_.end()) {
      for (const auto &order : it->second)
        volume_ahead += order.size;
      ++it;
    }
  } else {
    auto end = offers_.lower_bound(price);
    for (auto it = offers_.begin(); it != end; ++it) {
      for (const auto &order : it->second)
        volume_ahead += order.size;
    }
  }

  // Handle orders at same price level as order_id
  volume_ahead += GetQueuePos(order_id);

  return volume_ahead;
}
