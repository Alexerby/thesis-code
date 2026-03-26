#include "orderbook_processor.hpp"
#include "databento/dbn_file_store.hpp"
#include "databento/record.hpp"

OrderBookProcessor::OrderBookProcessor() {
}

// Transforms raw DBN records into EnrichedOrders
std::vector<EnrichedOrder>
OrderBookProcessor::ProcessRecords(db::DbnFileStore &store) {
    std::vector<EnrichedOrder> orders;

    const db::Record *record;
    uint64_t count = 0; // local count

    while ((record = store.NextRecord()) != nullptr) {
        if (record->Header().rtype == databento::RType::Mbo) {
            const auto &mbo = record->Get<databento::MboMsg>();

            // Update internal book state here
            // Calculate Tick Imbalance and Distance from Mid

            // Enrich the message
            orders.push_back(extend_order_msg(mbo));

            count++;
        }
    }
    m_total_obs = count;
    return orders;
}

EnrichedOrder OrderBookProcessor::extend_order_msg(const db::MboMsg &mbo) {
    Features f;

    // Calculate features based on internal book state
    // f.order_age_ns = ...
    // f.tick_imbalance = ...
    // f.distance_from_mid = ...

    return {mbo, f};
}
