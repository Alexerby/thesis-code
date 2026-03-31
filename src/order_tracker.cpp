#include "order_tracker.hpp"
#include "databento/record.hpp"
#include "logging.hpp"
#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace db = databento;

void OrderTracker::Router(const db::MboMsg &mbo) {
  PruneZombies();
  switch (mbo.action) {
  case db::Action::Clear: {
    Clear(mbo);
    break;
  }
  case db::Action::Add: {
    Add(mbo);
    break;
  }
  case db::Action::Cancel: {
    Cancel(mbo);
    break;
  }
  case db::Action::Modify: {
    Modify(mbo);
    break;
  }
  case db::Action::Fill: {
    Fill(mbo);
    break;
  }
  case db::Action::None: {
    // TODO: Handle flags
    break;
  }
  case db::Action::Trade: {
    // Simply ignore these
    break;
  }
  }
}

/* @brief Handle the routing of Action::Add.
 * @param &mbo The Order ID which is part of the MBO Message.
 */
void OrderTracker::Add(const db::MboMsg &mbo) {
  OrderMap::iterator it = order_map.find(mbo.order_id);

  // If we fail to find an order, insert a new entry to the map
  if (it == order_map.end()) {
    order_map[mbo.order_id] =
        Order{mbo.order_id, static_cast<int64_t>(mbo.size), mbo.price, mbo.side,
              std::chrono::steady_clock::now()};

    // Track for pruning
    expiry_queue_.push_back({mbo.order_id, std::chrono::steady_clock::now()});
  } else {
    // Existing Order -> Update order map record with new volume
    it->second.size += mbo.size;
  }
}

/* @brief Handle the routing of Action::Modify.
 * @param &mbo The Order ID which is part of the MBO Message.
 */
void OrderTracker::Modify(const db::MboMsg &mbo) {

  // There should be no modify messages in XNAS.ITCH
  if (feed_type_ == FeedType::XNAS_ITCH) {
    Logger logger("error.log");
    logger.log(CRITICAL,
               "OrderTracker::Router (Modify) called. Exiting program.");
    exit(1);
  };
}

/* @brief Handle the routing of Action::Fill.
 * @param &mbo The Order ID which is part of the MBO Message.
 */
void OrderTracker::Fill(const db::MboMsg &mbo) {
  // Stage the fill using sequence ID as the key
  staging_map_[mbo.sequence] = mbo;
}

/* @brief Handle the routing of Action::Cancel.
 * @param &mbo The Order ID which is part of the MBO Message.
 */
void OrderTracker::Cancel(const db::MboMsg &mbo) {
  OrderMap::iterator it = order_map.find(mbo.order_id);
  if (it == order_map.end()) {
    Logger logger("error.log");
    logger.log(CRITICAL, "OrderTracker::Cancel - Order ID " +
                             std::to_string(mbo.order_id) +
                             " not found in order_map. Sequence ID: " +
                             std::to_string(mbo.sequence));
    exit(1);
    return;
  }

  // Check if this Cancel is actually a "Fill" reconciliation
  StagingMap::iterator stage_it = staging_map_.find(mbo.sequence);

  if (stage_it != staging_map_.end()) {
    // MATCHED: This is a Fill-induced reduction
    // We use the size from the STAGED Fill, not the cancel msg itself
    it->second.size -= stage_it->second.size;

    // Consume the staging record so it's not used again
    staging_map_.erase(stage_it);
  } else {
    // PURE CANCEL: No matching fill found, reduce by the cancel amount
    it->second.size -= mbo.size;
  }

  // Cleanup: If volume is gone, erase from persistent map
  if (it->second.size <= 0) {
    order_map.erase(it);
  }
}

/* @brief Handle the routing of Action::Cancel.
 * @param &mbo The Order ID which is part of the MBO Message.
 */
void OrderTracker::Clear(const db::MboMsg &mbo) {
  order_map.clear();

  // This should have no effect, this one should always be clear
  // because the Action::Fill was consumed by a consecutive Action::Cancel.
  staging_map_.clear();
  expiry_queue_.clear();

  // Make sure empty
  assert(order_map.empty() && staging_map_.empty() && expiry_queue_.empty());
}

/* @brief Handle the pruning of orders older than current_ts.
 * @param current_ts The current time in nanoseconds.
 *
 * This prunes the double ended queue of orders that are
 * older than 1 minute.
 * If we don't prune the deque we will overflow the heap.
 */
void OrderTracker::PruneZombies() {
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  std::chrono::minutes timeout = std::chrono::minutes(1);

  while (!expiry_queue_.empty() &&
         (now - expiry_queue_.front().second) > timeout) {
    order_map.erase(expiry_queue_.front().first);
    expiry_queue_.pop_front();
  }
}
