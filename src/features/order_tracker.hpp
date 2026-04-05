/**
 * @file order_tracker.hpp
 * @brief Tracks individual order lifetimes and emits per-order feature records.
 *
 * MBO messages are noisy: a single logical order book event is often split
 * across multiple messages. OrderTracker reconstructs the full lifecycle of
 * each order — from placement through cancellation or fill, by maintaining a
 * live map of open orders keyed by order ID.
 *
 * When an order is fully cancelled (pure cancellation, no fills), its lifetime
 * snapshot is used to compute a FeatureRecord x_i and appended to
 * feature_records_. These records are the primary input to the GMM classifier.
 *
 * FeatureRecord is a direct output of the tracking process; it cannot be
 * produced without the order state accumulated during the lifetime of an order.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "data/market.hpp"
#include "databento/record.hpp"

namespace db = databento;

// Supports only XNAS.ITCH for now
enum class FeedType { XNAS_ITCH };

/**
 * @struct FeatureRecord
 * @brief Feature vector x_i for a single order lifecycle.
 *
 * Index mapping for use with GMM::ToEigen:
 *   0 = delta_t  (\Delta t_i, order age in nanoseconds)
 */
struct FeatureRecord {
  double delta_t;            ///< \Delta t_i
  double induced_imbalance;  ///< \Delta \mathcal{I}_i
};

// Represents the tracking of an individual order
struct Order {
  uint64_t order_id;
  int64_t size;   // Remaining quantity
  int64_t price;  // Current price
  db::Side side;  // Ask or Bid
  std::chrono::steady_clock::time_point entry_time;
  uint64_t entry_ts_recv;  // ts_recv at add (nanoseconds)
  uint64_t
      ts_event_add;  // ts_event at add — matching engine clock (nanoseconds)
  double induced_imbalance;  ///< \Delta \mathcal{I}_i
};

/* @class OrderTracker
 * @brief Tracks orders for the duration of their lifetime.
 */
class OrderTracker {
 public:
  using OrderMap = std::unordered_map<uint64_t, Order>;
  using PendingVolumeMap = std::unordered_map<uint64_t, int64_t>;
  using ExpiryQueue =
      std::deque<std::pair<uint64_t, std::chrono::steady_clock::time_point>>;

  explicit OrderTracker(uint32_t instrument_id, FeedType feed_type,
                        Market &market)
      : instrument_id_(instrument_id), feed_type_(feed_type), market_(market) {
    base_dir_ = "features/" + std::to_string(instrument_id_);
  }

  void Router(const db::MboMsg &mbo);
  void DumpOrders(const std::string &filename) const;

  OrderMap order_map{};
  PendingVolumeMap pending_volume_map_{};
  ExpiryQueue expiry_queue_{};

  // Populated on every pure cancellation event
  std::vector<FeatureRecord> feature_records_{};

 private:
  uint32_t instrument_id_;
  FeedType feed_type_;
  std::string base_dir_;
  Market &market_;

  void Add(const db::MboMsg &mbo);
  void Modify(const db::MboMsg &mbo);
  void Fill(const db::MboMsg &mbo);
  void Cancel(const db::MboMsg &mbo);
  void Clear(const db::MboMsg &mbo);
  void PruneZombies();
  void Reconcile(const db::MboMsg &mbo);

  /**
   * @brief Calculates feature: Order-Induced Imbalance.
   *
   * As the OrderTracker is not aware of the order before
   * it's added, we can not directly calculate the
   * difference that this order had on the balance
   * of the order book. To get around this, we calculate
   * the order book imbalance per usual (after the ADD)
   * and backtracks the imbalance level from before by
   * adjusting the volume at db::Side::Bid.
   *
   *
   * @param The Market-By-Order (MBO) message.
   */
  double OrderInducedImbalance(const db::MboMsg &mbo);

  /**
   * @brief Emits a FeatureRecord. We call this on event
   * CANCEL and RECONCILE.
   */
  void EmitFeatureRecord(const Order &order, const db::MboMsg &mbo);
};
