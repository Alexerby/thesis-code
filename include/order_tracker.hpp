/**
 * @file order_tracker.hpp
 * @brief Implements the state of individual orders.
 *
 * MBOMessages are very noisy and a single action
 * in the order book is often represented by multiple
 * messages. In order to track the lifetime of orders
 * this module consumes these order messages and constructs
 * the `alive' orders as a single std::unordered_map<uint64_t, Order>.
 * This gives a picture of the `state' (\mathcal{S})) of the order book
 * in the Event-State-Space.
 */

#pragma once

#include "databento/record.hpp"
#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>

namespace db = databento;

// Supports only XNAS.ITCH for now
enum class FeedType { XNAS_ITCH };

// Represents the tracking of an individual order
struct Order {
  uint64_t order_id;
  int64_t size;  // Remaining quantity
  int64_t price; // Current price
  db::Side side; // Ask or Bid
  std::chrono::steady_clock::time_point entry_time;
  uint64_t entry_ts_recv; // Message timestamp in nanos
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

  explicit OrderTracker(uint32_t instrument_id, FeedType feed_type)
      : instrument_id_(instrument_id), feed_type_(feed_type) {
    base_dir_ = "features/" + std::to_string(instrument_id_);
  }

  /* @brief Routes the incoming order message to order map.
   * @param mbo The incoming MBO message.
   */
  void Router(const db::MboMsg &mbo);

  /**
   * @brief Exports the current state of the order map to a CSV file.
   * @param filename The name of the file to export to (saved in "features/" dir).
   */
  void DumpOrders(const std::string &filename) const;

  /**
   * @brief Logs the details of a single event for an order to its own CSV file.
   * @param order_id The ID of the order being logged.
   * @param mbo The message triggering the log.
   * @param action A string description of the action (e.g., "ADD", "FILL").
   * @param size_after The size of the order after the action.
   */
  void LogLifecycle(uint64_t order_id, const db::MboMsg &mbo,
                   const std::string &action, int64_t size_after) const;

  // Persistent Map (Key: OrderID)
  OrderMap order_map{};

  // Pending Volume Map (Key: Order ID)
  PendingVolumeMap pending_volume_map_{};

  // TTL Tracker pair<order_id, ts>
  ExpiryQueue expiry_queue_{};

private:
  // Data for which instrument and feed we are tracking
  uint32_t instrument_id_;
  FeedType feed_type_;
  std::string base_dir_;

  void Add(const db::MboMsg &mbo);
  void Modify(const db::MboMsg &mbo);
  void Fill(const db::MboMsg &mbo);
  void Cancel(const db::MboMsg &mbo);
  void Clear(const db::MboMsg &mbo);
  void PruneZombies();
  
  // Internal reconciliation logic for both Fills and Cancels
  void Reconcile(const db::MboMsg &mbo);
};
