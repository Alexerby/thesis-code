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
 *   0 = delta_t          (\Delta t_i,   order age in nanoseconds)
 *   1 = delta_imbalance  (\Delta I_i,   imbalance change over lifetime)
 *   2 = size_ratio       (size relative to best-level depth)
 *   3 = queue_pos        (queue position at placement)
 *   4 = dist_touch       (distance from same-side best price)
 *   5 = cancel_rate      (local cancel rate in +-500ms window)
 */
struct FeatureRecord {
  double delta_t;          ///< \Delta t_i
  double delta_imbalance;  ///< \Delta I_i
  double size_ratio;
  double queue_pos;
  double dist_touch;
  double cancel_rate;
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
  double imbalance_at_add;      // Book imbalance when order was placed
  double dist_to_touch_at_add;  // Distance from best same-side price at add
                                // (raw price units)
  int64_t size_at_add;        // Original order size at placement
  uint32_t queue_pos_at_add;  // Queue position at placement (TODO: update to
                              // cancel-time)
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

  /* @brief Routes the incoming order message to order map.
   * @param mbo The incoming MBO message.
   */
  void Router(const db::MboMsg &mbo);

  /**
   * @brief Exports the current state of the order map to a CSV file.
   * @param filename The name of the file to export to (saved in "features/"
   * dir).
   */
  void DumpOrders(const std::string &filename) const;

  // Persistent Map (Key: OrderID)
  OrderMap order_map{};

  // Pending Volume Map (Key: Order ID)
  PendingVolumeMap pending_volume_map_{};

  // TTL Tracker pair<order_id, ts>
  ExpiryQueue expiry_queue_{};

  // Populated on every pure cancellation event
  std::vector<FeatureRecord> feature_records_{};

 private:
  // Data for which instrument and feed we are tracking
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

  // Internal reconciliation logic for both Fills and Cancels
  void Reconcile(const db::MboMsg &mbo);

  // Builds and appends a FeatureRecord for a completed cancellation
  void EmitFeatureRecord(const Order &order, const db::MboMsg &mbo);
};
