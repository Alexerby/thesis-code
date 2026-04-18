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
 * @enum CancelType
 * @brief Whether the order lifecycle ended in a pure cancellation or a fill.
 *
 * Pure: order was withdrawn before any execution (candidate for spoofing).
 * Fill: order was fully or partially filled before the cancel message.
 */
enum class CancelType : uint8_t { Pure = 0, Fill = 1 };

/**
 * @struct FeatureRecord
 * @brief Feature vector x_i for a single order lifecycle.
 */
struct FeatureRecord {
  double delta_t;            ///< \Delta t_i
  double induced_imbalance;  ///< \Delta \mathcal{I}_i
  double volume_ahead;       ///< Total volume between order and BBO at add-time
  double relative_size;      ///< order size / rolling median size (last 500 adds)
  CancelType cancel_type;    ///< Pure cancellation or fill-induced cancel
};

// ---------------------------------------------------------------------------
// Feature registry
// To add a new feature:
//   1. Add a field to FeatureRecord above.
//   2. Add an entry here (name + extractor lambda).
//   3. Compute it in OrderTracker::Add() and store it on the Order struct,
//      then copy it to FeatureRecord in OrderTracker::EmitFeatureRecord().
// ---------------------------------------------------------------------------
using FeatureExtractor = double (*)(const FeatureRecord &);

struct FeatureDef {
  const char *name;
  FeatureExtractor extract;
};

inline const FeatureDef kFeatures[] = {
    {"delta_t",
     [](const FeatureRecord &r) { return r.delta_t; }},
    {"induced_imbalance",
     [](const FeatureRecord &r) { return r.induced_imbalance; }},
    {"volume_ahead",
     [](const FeatureRecord &r) { return r.volume_ahead; }},
    {"relative_size",
     [](const FeatureRecord &r) { return r.relative_size; }},
};

// Represents the tracking of an individual order
struct Order {
  uint64_t order_id;
  int64_t size;          // Remaining quantity
  int64_t price;         // Current price
  db::Side side;         // Ask or Bid
  std::chrono::steady_clock::time_point entry_time;
  uint64_t entry_ts_recv;  // ts_recv at add (nanoseconds)
  uint64_t ts_event_add;   // ts_event at add — matching engine clock (nanoseconds)
  double induced_imbalance;  ///< \Delta \mathcal{I}_i
  int64_t total_filled{0};   // Cumulative filled volume across all events
  uint32_t volume_ahead;
  double relative_size;      ///< size / rolling median size at add-time
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

  // Handlers for all teh event types
  void Add(const db::MboMsg &mbo);
  void Modify(const db::MboMsg &mbo);
  void Fill(const db::MboMsg &mbo);
  void Cancel(const db::MboMsg &mbo);
  void Clear(const db::MboMsg &mbo);
  void PruneZombies();

  /**
   * @brief Reconciles the final state of an order at the end of a multi-message
   * event.
   *
   * In XNAS.ITCH, a single logical order book event (e.g. a partial fill
   * followed by a cancel of the remainder) is split across several MBO messages
   * that share a common event boundary. Only the last message in the sequence
   * carries @c flags.IsLast() == true. Reconcile is called on that final
   * message to settle the order's net position.
   *
   * The reconciliation logic is:
   *   1. Drain any volume that was accumulated in @c pending_volume_map_ by
   *      preceding Fill messages in the same event (@c staged_vol).
   *   2. Subtract @c staged_vol + @c mbo.size from the order's remaining size.
   *   3. If the order is fully consumed:
   *      - @c staged_vol == 0: the event contained only cancel messages
   *        → pure cancellation, emits CancelType::Pure.
   *      - @c staged_vol  > 0: at least one Fill preceded the final cancel
   *        → the order was partially or fully executed before withdrawal,
   *        emits CancelType::Fill.
   *   4. If residual size remains the order stays live in @c order_map.
   *
   * @param mbo The last MBO message in the event (flags.IsLast() == true).
   */
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

  /// Returns the median of size_window_, or 1.0 if the window is empty.
  double RollingMedianSize() const;

  /**
   * @brief Emits a FeatureRecord. We call this on event
   * CANCEL and RECONCILE.
   */
  void EmitFeatureRecord(const Order &order, const db::MboMsg &mbo,
                         CancelType cancel_type);

  static constexpr int kSizeWindowN = 500;
  std::deque<uint32_t> size_window_;
};
