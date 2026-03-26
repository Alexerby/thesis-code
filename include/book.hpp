// Functionality for feature engineering of MBO (L3) orderbook data
#pragma once

#include "databento/enums.hpp"
#include "databento/record.hpp"
#include <cstdint>
#include <databento/record.hpp>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace db = databento;

// PriceLevel helper struct
struct PriceLevel {
  int64_t price{db::kUndefPrice};
  uint32_t size{0};
  uint32_t count{0};

  bool IsEmpty() const { return price == db::kUndefPrice; }
  operator bool() const { return !IsEmpty(); }
};

std::ostream &operator<<(std::ostream &stream, const PriceLevel &level);

class Book {
public:
  Book() = default;

  // Public API
  std::pair<PriceLevel, PriceLevel> Bbo() const;
  PriceLevel GetBidLevel(std::size_t idx = 0) const;
  PriceLevel GetAskLevel(std::size_t idx = 0) const;
  PriceLevel GetBidLevelByPx(int64_t px) const;
  PriceLevel GetAskLevelByPx(int64_t px) const;

  const db::MboMsg &GetOrder(uint64_t order_id);
  uint32_t GetQueuePos(uint64_t order_id);
  std::vector<db::BidAskPair> GetSnapshot(std::size_t level_count = 1) const;

  void Apply(const db::MboMsg &mbo);

private:
  // Internal Types
  using LevelOrders = std::vector<db::MboMsg>;
  struct PriceAndSide {
    int64_t price;
    db::Side side;
  };
  using Orders = std::unordered_map<uint64_t, PriceAndSide>;
  using SideLevels = std::map<int64_t, LevelOrders>;

  // Private Helpers
  static PriceLevel GetPriceLevel(int64_t price, const LevelOrders &level);
  static LevelOrders::iterator GetLevelOrder(LevelOrders &level,
                                             uint64_t order_id);

  void Clear();
  void Add(db::MboMsg mbo);
  void Cancel(db::MboMsg mbo);
  void Modify(db::MboMsg mbo);

  SideLevels &GetSideLevels(db::Side side);
  LevelOrders &GetLevel(db::Side side, int64_t price);
  LevelOrders &GetOrInsertLevel(db::Side side, int64_t price);
  void RemoveLevel(db::Side side, int64_t price);

  // Private Data
  Orders orders_by_id_;
  SideLevels offers_;
  SideLevels bids_;
};
