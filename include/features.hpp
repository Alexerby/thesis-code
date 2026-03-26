#pragma once
#include <databento/record.hpp>

namespace db = databento;

struct Features {
  int64_t order_age_ns{0};
  int64_t tick_imbalance{0};
  double distance_from_mid{0.0};
};

struct EnrichedOrder {
  db::MboMsg base;
  Features features;
};

struct OrderState {
  uint64_t entry_ts;
  int64_t price;
  uint32_t size;
  db::Side side;
};
