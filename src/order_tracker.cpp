#include "order_tracker.hpp"
#include "databento/record.hpp"
#include "logging.hpp"
#include "csv.hpp"
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <cassert>

namespace db = databento;

void OrderTracker::Router(const db::MboMsg &mbo) {
  // Only process messages for the requested instrument
  if (mbo.hd.instrument_id != instrument_id_) {
    return;
  }

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
  case db::Action::Fill: {
    Fill(mbo);
    break;
  }
  case db::Action::Modify:
  case db::Action::Trade:
  case db::Action::None: {
    // These actions are ignored or not expected in 
    // XNAS.ITCH for order tracking
    break;
  }
  }
}

void OrderTracker::DumpOrders(const std::string &filename) const {
  CSVWriter writer;
  if (!writer.Open(filename)) {
    std::cerr << "Failed to open CSV for dumping: " << filename << std::endl;
    return;
  }

  // Header
  writer.Write("order_id");
  writer.Write("size");
  writer.Write("price");
  writer.Write("side");
  writer.Write("entry_ts_recv", true);

  for (const auto &[id, order] : order_map) {
    writer.Write(order.order_id);
    writer.Write(order.size);
    writer.Write(order.price);

    // Convert Side to character for Excel compatibility
    char side_char = 'N';
    if (order.side == db::Side::Bid)
      side_char = 'B';
    else if (order.side == db::Side::Ask)
      side_char = 'A';

    writer.Write(side_char);
    writer.Write(order.entry_ts_recv, true);
  }

  writer.Flush();
  std::cout << "Dumped " << order_map.size() << " orders to features/" << filename << std::endl;
}

void OrderTracker::LogLifecycle(uint64_t order_id, const db::MboMsg &mbo,
                               const std::string &action,
                               int64_t size_after) const {
  CSVWriter writer;
  std::string filename = std::to_string(order_id) + ".csv";

  bool is_new = !writer.Exists(filename, base_dir_);
  if (!writer.Open(filename, base_dir_, true)) {
    return;
  }

  if (is_new) {
    writer.Write("ts_recv");
    writer.Write("action");
    writer.Write("price");
    writer.Write("size_delta");
    writer.Write("remaining_size", true);
  }

  writer.Write(mbo.ts_recv.time_since_epoch().count());
  writer.Write(action);
  writer.Write(mbo.price);
  writer.Write(mbo.size);
  writer.Write(size_after, true);
  writer.Flush();
}

void OrderTracker::Add(const db::MboMsg &mbo) {
  OrderMap::iterator it = order_map.find(mbo.order_id);

  if (it == order_map.end()) {
    // New Order: Insert to Order Map & Expiry Queue
    order_map[mbo.order_id] =
        Order{mbo.order_id, static_cast<int64_t>(mbo.size), mbo.price, mbo.side,
              std::chrono::steady_clock::now(),
              mbo.ts_recv.time_since_epoch().count()};

    expiry_queue_.push_back({mbo.order_id, std::chrono::steady_clock::now()});
    LogLifecycle(mbo.order_id, mbo, "ADD", mbo.size);
  } else {
    // Existing Order: Update size directly (XNAS.ITCH multi-part add)
    it->second.size += mbo.size;
    LogLifecycle(mbo.order_id, mbo, "ADD_PART", it->second.size);
  }
}

void OrderTracker::Fill(const db::MboMsg &mbo) {
  // Accumulate: Add Fill size to PendingVolumeMap
  pending_volume_map_[mbo.order_id] += mbo.size;
  LogLifecycle(mbo.order_id, mbo, "FILL_STAGED", -1);
  // If this is the last message in the event, reconcile immediately
  // I don't think this should ever be the case.
  // This is just me quadrupel-checking.
  if (mbo.flags.IsLast()) {
    Reconcile(mbo);
  }
}
void OrderTracker::Cancel(const db::MboMsg &mbo) {
  if (!mbo.flags.IsLast()) {
    // Pure Cancel: Reduce OrderMap size directly via OrderID
    OrderMap::iterator it = order_map.find(mbo.order_id);
    if (it != order_map.end()) {
      it->second.size -= mbo.size;
      LogLifecycle(mbo.order_id, mbo, "CANCEL_PART", it->second.size);
      if (it->second.size <= 0) {
        order_map.erase(it);
      }
    }
  } else {
    // Last message in event: Perform reconciliation
    Reconcile(mbo);
  }
}

void OrderTracker::Reconcile(const db::MboMsg &mbo) {
  OrderMap::iterator it = order_map.find(mbo.order_id);
  if (it == order_map.end()) {
    return;
  }

  // Check Staged: Pending Vol for OrderID?
  int64_t staged_vol = 0;
  auto pending_it = pending_volume_map_.find(mbo.order_id);
  if (pending_it != pending_volume_map_.end()) {
    staged_vol = pending_it->second;
    // Clear Staged: Erase from PendingVolumeMap
    pending_volume_map_.erase(pending_it);
  }

  // Reconcile: OrderMap.Vol -= Staged + Msg.Size
  it->second.size -= (staged_vol + static_cast<int64_t>(mbo.size));
  LogLifecycle(mbo.order_id, mbo, "RECONCILE", it->second.size);

  // VolCheck: Erase if gone, otherwise update state
  if (it->second.size <= 0) {
    order_map.erase(it);
  }
}

void OrderTracker::Clear(const db::MboMsg &mbo) {
  order_map.clear();
  pending_volume_map_.clear();
  expiry_queue_.clear();
}

void OrderTracker::PruneZombies() {
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  std::chrono::minutes timeout = std::chrono::minutes(1);

  while (!expiry_queue_.empty() &&
         (now - expiry_queue_.front().second) > timeout) {
    // Only erase from order_map if it's the exact same order entry
    // (In case order_id was reused, though unlikely in 1 min)
    uint64_t order_id = expiry_queue_.front().first;
    auto it = order_map.find(order_id);
    if (it != order_map.end()) {
      // Mock a message for pruning log
      db::MboMsg mock_mbo{};
      mock_mbo.order_id = order_id;
      mock_mbo.ts_recv = db::UnixNanos{std::chrono::nanoseconds{0}};
      LogLifecycle(order_id, mock_mbo, "PRUNED", 0);
      order_map.erase(it);
    }
    expiry_queue_.pop_front();
  }
}
