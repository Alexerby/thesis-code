// Functionality for feature engineering of MBO (L3) orderbook data

#include "databento/datetime.hpp"
#include "databento/dbn_file_store.hpp"
#include <cstdint>
#include <databento/record.hpp>
#include <unordered_map>
#include <vector>

namespace db = databento;

// ------------------------------------------------------------------
// Logic for extending individual orders
// ------------------------------------------------------------------

// The features we will append to the L3 Orderbook
struct Features {

    // The order age measured in nanoseconds
    int64_t order_age_ns;

    // The impact that this specific order had on the
    // balance of the orderbook
    int64_t tick_imbalance;

    // The distance from the mid price that this order was placed
    double distance_from_mid;
};

// Extend/enrich the db::MboMsg (order message) with custom features
struct EnrichedOrder {
    db::MboMsg base;
    Features features;
};

// The state of an individual order
struct OrderState {
    db::UnixNanos entry_ts;
    db::UnixNanos last_mod_ts;
    int64_t price;
    uint32_t size;
    db::Side side;
};

using EnrichedOrderBook = std::vector<EnrichedOrder>;

// ------------------------------------------------------------------
// Processor Class
// ------------------------------------------------------------------

class OrderBookProcessor {
  public:
    OrderBookProcessor();

    std::vector<EnrichedOrder> ProcessRecords(db::DbnFileStore &store);

  private:
    // Internal state tracking
    // Price -> Aggregate volumes
    std::map<int64_t, uint32_t> m_bids;
    std::map<int64_t, uint32_t> m_asks;

    // Track individual order lifecycle for "Order Age" calculations
    std::unordered_map<uint64_t, OrderState> m_active_orders;

    // Updates internal maps based on Add, Cancel, Fill or Clear action
    void update_book_state(const db::MboMsg &mbo);

    // Calculates features and returns the enriched struct
    EnrichedOrder extend_order_msg(const db::MboMsg &mbo);

    // Helper for mid-price
    double calculate_mid() const;
};
