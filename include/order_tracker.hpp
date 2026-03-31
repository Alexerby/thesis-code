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
};

/* @class OrderTracker
 * @brief Tracks orders for the duration of their lifetime.
 */
class OrderTracker {
public:
  using OrderMap = std::unordered_map<uint64_t, Order>;
  using StagingMap = std::unordered_map<uint64_t, db::MboMsg>;
  using ExpiryQueue =
      std::deque<std::pair<uint64_t, std::chrono::steady_clock::time_point>>;

  explicit OrderTracker(uint32_t instrument_id, FeedType feed_type)
      : instrument_id_(instrument_id), feed_type_(feed_type) {}

  /* @brief Routes the incoming order message to order map.
   * @param mbo The incoming MBO message.
   */
  void Router(const db::MboMsg &mbo);

  // Persistent Map (Key: OrderID)
  OrderMap order_map{};

private:
  // Data for which instrument and feed we are tracking
  uint32_t instrument_id_;
  FeedType feed_type_;

  // Staging Map (Key: Sequence ID)
  StagingMap staging_map_{};

  // TTL Tracker pair<order_id, ts>
  ExpiryQueue expiry_queue_{};

  void Add(const db::MboMsg &mbo);
  void Modify(const db::MboMsg &mbo);
  void Fill(const db::MboMsg &mbo);
  void Cancel(const db::MboMsg &mbo);
  void Clear(const db::MboMsg &mbo);
  void PruneZombies();
};
