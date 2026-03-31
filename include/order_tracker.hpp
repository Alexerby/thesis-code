/* This module sets up the functionality for tracking
 * orders over their lifetime.
 * As of now functinality is only implemented for the
 * XNAS.ITCH feed.
 */


// TODO: 
// - Create instance method for
//  *  

#include "databento/enums.hpp"
#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>

namespace db = databento;

// Represents the tracking of an individual order
struct Order {
  db::Side side;
  int64_t price;
  uint32_t volume;
};

/* @class OrderTracker
 * @brief Tracks orders for the duration of their lifetime.
 */
class OrderTracker {
public:
  void upsert(uint64_t id, const Order &order);

private:
  // Persistent Map (Key: OrderID)
  std::unordered_map<uint64_t, Order> orderBook;

  // Staging Map (Key: Sequence ID)
  std::unordered_map<uint64_t, Order> stagingArea;

  // TTL Tracker pair<order_id, ts>
  std::deque<std::pair<uint64_t, std::chrono::steady_clock::time_point>>
      expiryQueue;

  void prune_zombies() {
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::minutes(1);

    while (!expiryQueue.empty() &&
           (now - expiryQueue.front().second) > timeout) {
      orderBook.erase(expiryQueue.front().first);
      expiryQueue.pop_front();
    }
  }
};
