#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "databento/record.hpp"
#include "features/order_tracker.hpp"

namespace db = databento;

namespace {
db::MboMsg create_mock_mbo(uint64_t order_id, int64_t price, uint32_t size,
                           db::Action action, uint8_t flags_repr = 128,
                           uint32_t instrument_id = 1234) {
  db::MboMsg mbo{};
  mbo.hd.rtype = db::RType::Mbo;
  mbo.hd.instrument_id = instrument_id;

  mbo.order_id = order_id;
  mbo.price = price;
  mbo.size = size;
  mbo.action = action;
  mbo.side = db::Side::Bid;
  mbo.flags = db::FlagSet{flags_repr};
  mbo.ts_recv = db::UnixNanos{std::chrono::nanoseconds{1773820800000000000}};
  return mbo;
}
}  // namespace

TEST_CASE("OrderTracker Clear Logic", "[order_tracker]") {
  Market market;
  OrderTracker tracker(1234, FeedType::XNAS_ITCH, market);

  // Add five orders and route them
  for (int i = 1; i <= 5; ++i) {
    tracker.Router(create_mock_mbo(i, 100, 1'000, db::Action::Add));
  }
  REQUIRE(tracker.order_map.size() == 5);

  // Real Clear messages always carry flag=8 (F_BAD_TS_RECV), order_id=0,
  // size=0, price=INT64_MAX. We intentionally do not filter on this flag —
  // the Clear semantic is correct regardless of the bad timestamp.
  tracker.Router(
      create_mock_mbo(0, std::numeric_limits<int64_t>::max(), 0,
                      db::Action::Clear, /*flags=*/8));

  REQUIRE(tracker.order_map.empty());
  REQUIRE(tracker.pending_volume_map_.empty());
  REQUIRE(tracker.expiry_queue_.empty());
}

TEST_CASE("OrderTracker ignores wrong instrument", "[order_tracker]") {
  Market market;
  OrderTracker tracker(1234, FeedType::XNAS_ITCH, market);

  // Message for a different instrument should be silently dropped
  tracker.Router(
      create_mock_mbo(1, 100, 500, db::Action::Add, 128, /*instrument_id=*/9999));

  REQUIRE(tracker.order_map.empty());
}

TEST_CASE("OrderTracker Add Logic", "[order_tracker]") {
  Market market;
  OrderTracker tracker(1234, FeedType::XNAS_ITCH, market);

  SECTION("Adding a new order") {
    db::MboMsg add_msg =
        create_mock_mbo(1166, 312100000000, 100, db::Action::Add, 128);
    tracker.Router(add_msg);

    REQUIRE(tracker.order_map.count(1166) == 1);
    REQUIRE(tracker.order_map[1166].size == 100);
    REQUIRE(tracker.order_map[1166].price == 312100000000);
  }
}

TEST_CASE("OrderTracker Zombie Pruning", "[order_tracker]") {
  Market market;
  OrderTracker tracker(1234, FeedType::XNAS_ITCH, market);

  SECTION("Pruning expired orders") {
    // Add an order and create a partial fill
    db::MboMsg add_msg =
        create_mock_mbo(1166, 232000000000, 2540, db::Action::Add, 128);
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
    CHECK(tracker.expiry_queue_.size() ==
          1);  // Only the new trigger_msg order should remain
    REQUIRE(tracker.expiry_queue_.front().first == 9999);
  }
}

TEST_CASE("OrderTracker CSV Dump", "[order_tracker][slow]") {
  Market market;
  OrderTracker tracker(1234, FeedType::XNAS_ITCH, market);

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

TEST_CASE("CancelType classification", "[order_tracker]") {
  Market market;
  OrderTracker tracker(1234, FeedType::XNAS_ITCH, market);

  SECTION("Pure cancel emits CancelType::Pure") {
    tracker.Router(create_mock_mbo(1, 100, 1000, db::Action::Add));
    tracker.Router(
        create_mock_mbo(1, 100, 1000, db::Action::Cancel, 128));  // IsLast

    REQUIRE(tracker.feature_records_.size() == 1);
    REQUIRE(tracker.feature_records_[0].cancel_type == CancelType::Pure);
  }

  SECTION("Fill + cancel in same event emits CancelType::Fill") {
    tracker.Router(create_mock_mbo(1, 100, 1000, db::Action::Add));
    tracker.Router(
        create_mock_mbo(1, 100, 1000, db::Action::Fill, 0));  // not last
    tracker.Router(create_mock_mbo(1, 100, 0, db::Action::Cancel,
                                   128));  // IsLast -> Reconcile

    REQUIRE(tracker.feature_records_.size() == 1);
    REQUIRE(tracker.feature_records_[0].cancel_type == CancelType::Fill);
  }

  SECTION("Fill in event 1, cancel in event 2 emits CancelType::Fill") {
    tracker.Router(create_mock_mbo(1, 100, 1000, db::Action::Add));

    // Event 1: partial fill (400), order survives with 600 remaining
    tracker.Router(
        create_mock_mbo(1, 100, 400, db::Action::Fill, 0));  // not last
    tracker.Router(
        create_mock_mbo(1, 100, 0, db::Action::Cancel,
                        128));  // IsLast, size not exhausted -> no emit

    // Event 2: pure-looking cancel finishes the order
    tracker.Router(create_mock_mbo(1, 100, 600, db::Action::Cancel, 128));

    REQUIRE(tracker.feature_records_.size() == 1);
    // total_filled > 0 → Fill despite no staged_vol in event 2
    REQUIRE(tracker.feature_records_[0].cancel_type == CancelType::Fill);
  }
}
