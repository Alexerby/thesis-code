#include <catch2/catch_test_macros.hpp>

#include "data/book.hpp"
#include "databento/record.hpp"

namespace db = databento;

namespace {
db::MboMsg make_add(uint64_t order_id, int64_t price, uint32_t size,
                    db::Side side) {
  db::MboMsg mbo{};
  mbo.hd.rtype = db::RType::Mbo;
  mbo.order_id = order_id;
  mbo.price = price;
  mbo.size = size;
  mbo.action = db::Action::Add;
  mbo.side = side;
  mbo.flags = db::FlagSet{0};  // no TOB, no IsLast — regular resting order
  mbo.ts_recv = db::UnixNanos{std::chrono::nanoseconds{1773820800000000000}};
  return mbo;
}
}  // namespace

TEST_CASE("Book::GetVolumeAhead — bid side", "[book]") {
  Book book;

  SECTION("Order at BBO with nothing ahead returns 0") {
    book.Apply(make_add(1, 100, 500, db::Side::Bid));
    REQUIRE(book.GetVolumeAhead(1) == 0);
  }

  SECTION("Cross-level volume at better prices is summed") {
    // Order under test sits at 100; orders at 101 and 102 are between it and BBO
    book.Apply(make_add(1, 102, 300, db::Side::Bid));
    book.Apply(make_add(2, 101, 200, db::Side::Bid));
    book.Apply(make_add(3, 100, 500, db::Side::Bid));  // order under test

    REQUIRE(book.GetVolumeAhead(3) == 500);  // 300 + 200
  }

  SECTION("Intra-level queue position is included") {
    // Two orders arrive at the same price before the order under test
    book.Apply(make_add(1, 100, 100, db::Side::Bid));
    book.Apply(make_add(2, 100, 150, db::Side::Bid));
    book.Apply(make_add(3, 100, 200, db::Side::Bid));  // order under test

    REQUIRE(book.GetVolumeAhead(3) == 250);  // 100 + 150
  }

  SECTION("Cross-level and intra-level are combined") {
    // Mirrors the doxygen example:
    // price 103 → [order 1 (200), order 2 (100)]   BBO
    // price 102 → [order 3 (150)]
    // price 101 → [order 4 (300), order 5 (50)]
    // price 100 → [order 6 (400), order 7 (75), ORDER_UNDER_TEST (500)]
    book.Apply(make_add(1, 103, 200, db::Side::Bid));
    book.Apply(make_add(2, 103, 100, db::Side::Bid));
    book.Apply(make_add(3, 102, 150, db::Side::Bid));
    book.Apply(make_add(4, 101, 300, db::Side::Bid));
    book.Apply(make_add(5, 101,  50, db::Side::Bid));
    book.Apply(make_add(6, 100, 400, db::Side::Bid));
    book.Apply(make_add(7, 100,  75, db::Side::Bid));
    book.Apply(make_add(8, 100, 500, db::Side::Bid));  // order under test

    // Cross-level: 200+100 + 150 + 300+50 = 800
    // Intra-level: 400+75 = 475
    REQUIRE(book.GetVolumeAhead(8) == 1275);
  }
}

TEST_CASE("Book::GetVolumeAhead — ask side", "[book]") {
  Book book;

  SECTION("Order at BBO with nothing ahead returns 0") {
    book.Apply(make_add(1, 100, 500, db::Side::Ask));
    REQUIRE(book.GetVolumeAhead(1) == 0);
  }

  SECTION("Cross-level volume at better (lower) prices is summed") {
    // For asks, better prices are lower — orders at 98 and 99 are between
    // order under test (at 100) and the BBO
    book.Apply(make_add(1, 98, 300, db::Side::Ask));
    book.Apply(make_add(2, 99, 200, db::Side::Ask));
    book.Apply(make_add(3, 100, 500, db::Side::Ask));  // order under test

    REQUIRE(book.GetVolumeAhead(3) == 500);  // 300 + 200
  }
}
