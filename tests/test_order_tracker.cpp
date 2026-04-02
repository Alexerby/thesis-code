#include <catch2/catch_test_macros.hpp>
#include "databento/record.hpp"
#include "features/order_tracker.hpp"
#include <chrono>

namespace db = databento;

namespace {
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
} // namespace

TEST_CASE("OrderTracker Add Logic", "[order_tracker]") {
  OrderTracker tracker(7152, FeedType::XNAS_ITCH);

  SECTION("Adding a new order") {
    db::MboMsg add_msg = create_mock_mbo(1166, 312100000000, 100, db::Action::Add, 128);
    tracker.Router(add_msg);

    REQUIRE(tracker.order_map.count(1166) == 1);
    REQUIRE(tracker.order_map[1166].size == 100);
    REQUIRE(tracker.order_map[1166].price == 312100000000);
  }
}

TEST_CASE("OrderTracker Zombie Pruning", "[order_tracker]") {
  OrderTracker tracker(7152, FeedType::XNAS_ITCH);

  SECTION("Pruning expired orders") {
    // Add an order and create a partial fill
    db::MboMsg add_msg = create_mock_mbo(1166, 232000000000, 2540, db::Action::Add, 128);
    tracker.Router(add_msg);

    db::MboMsg fill_msg = create_mock_mbo(1166, 12, 5, db::Action::Fill, 0);
    tracker.Router(fill_msg);

    REQUIRE(tracker.order_map.size() == 1);
    REQUIRE(tracker.pending_volume_map_.size() == 1);
    REQUIRE(tracker.expiry_queue_.size() == 1);

    // Simulate "time travel": Make the order entry 2 minutes old
    // (Pruning threshold is 1 minute)
    tracker.expiry_queue_.front().second -= std::chrono::minutes(2);

    // Trigger another Router call to invoke PruneZombies()
    db::MboMsg trigger_msg = create_mock_mbo(9999, 0, 0, db::Action::Add, 128);
    tracker.Router(trigger_msg);

    // Verify that the order 1166 was pruned
    CHECK(tracker.order_map.count(1166) == 0);
    CHECK(tracker.expiry_queue_.size() == 1); // Only the new trigger_msg order should remain
    REQUIRE(tracker.expiry_queue_.front().first == 9999);
  }
}

TEST_CASE("OrderTracker CSV Dump", "[order_tracker][slow]") {
  OrderTracker tracker(7152, FeedType::XNAS_ITCH);

  SECTION("Dumping orders to CSV") {
    tracker.Router(create_mock_mbo(1001, 100000, 50, db::Action::Add));
    tracker.Router(create_mock_mbo(1002, 100100, 100, db::Action::Add));
    tracker.Router(create_mock_mbo(1003, 99900, 25, db::Action::Add));

    // Multi-part add (Modify logic in tracker adds size if already exists)
    tracker.Router(create_mock_mbo(1001, 100000, 25, db::Action::Add)); 

    // This should not throw
    REQUIRE_NOTHROW(tracker.DumpOrders("test_orders.csv"));
  }
}
