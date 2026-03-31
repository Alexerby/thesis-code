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
  OrderTracker tracker(7152, FeedType::XNAS_ITCH);

  db::MboMsg add_msg =
      create_mock_mbo(1166, 312100000000, 100, db::Action::Add, 128);
  tracker.Router(add_msg);

  assert(tracker.order_map.count(1166) == 1);
  assert(tracker.order_map[1166].size == 100);

  std::cout << "test_add_logic passed!" << std::endl;
}

void test_zombie_pruning_logic() {
  OrderTracker tracker(7152, FeedType::XNAS_ITCH);

  // Add an order and create a partial fill which is realistic
  db::MboMsg add_msg =
      create_mock_mbo(1166, 232000000000, 2540, db::Action::Add, 128);
  tracker.Router(add_msg);

  db::MboMsg fill_msg = create_mock_mbo(1166, 12, 5, db::Action::Fill, 0);
  tracker.Router(fill_msg);

  // Ensure all the data structures contain information
  assert(tracker.order_map.size() == 1);
  assert(tracker.pending_volume_map_.size() == 1);
  assert(tracker.expiry_queue_.size() == 1);

  // Simulate "time travel": Make the order entry 2 minutes old
  // (Pruning threshold is 1 minute)
  tracker.expiry_queue_.front().second -= std::chrono::minutes(2);

  // Trigger another Router call (action types doesnt matter)
  // to invoke PruneZombies()
  db::MboMsg trigger_msg = create_mock_mbo(9999, 0, 0, db::Action::Add, 128);
  tracker.Router(trigger_msg);

  // Verify that the order 1166 was pruned
  assert(tracker.order_map.count(1166) == 0);
  assert(tracker.expiry_queue_.size() ==
         1); // Only the new trigger_msg order should remain
  assert(tracker.expiry_queue_.front().first == 9999);

  std::cout << "test_zombie_pruning_logic passed!" << std::endl;
}

int main() {
  test_add_logic();
  test_zombie_pruning_logic();
  std::cout << "\nAll tests completed successfully!" << std::endl;
  return 0;
}
