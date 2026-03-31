#include "databento/record.hpp"
#include "order_tracker.hpp"
#include <cassert>
#include <iostream>

namespace db = databento;

db::MboMsg create_mock_mbo(uint64_t order_id, int64_t price, uint32_t size,
                           db::Action action, uint8_t flags_repr = 128) {
  db::MboMsg mbo{};
  mbo.hd.rtype = db::RType::Mbo;
  mbo.hd.instrument_id = 7152;

  mbo.order_id = order_id;
  mbo.price = price;
  mbo.size = size;
  mbo.action = action;
  mbo.side = db::Side::Bid;
  mbo.flags = db::FlagSet{flags_repr};
  mbo.ts_recv = db::UnixNanos{std::chrono::nanoseconds{1773820800000000000}};
  return mbo;
}

void test_add_logic() {
  std::cout << "Running realistic test_add_logic..." << std::endl;

  OrderTracker tracker(7152, FeedType::XNAS_ITCH);

  db::MboMsg add_msg =
      create_mock_mbo(1166, 312100000000, 100, db::Action::Add, 128);
  tracker.Router(add_msg);

  assert(tracker.order_map.count(1166) == 1);
  assert(tracker.order_map[1166].size == 100);

  std::cout << "test_add_logic passed!" << std::endl;
}

int main() {
  test_add_logic();
  std::cout << "\nAll tests completed successfully!" << std::endl;
  return 0;
}
