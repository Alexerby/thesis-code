#include "order_tracker.hpp"
#include "databento/record.hpp"
#include "logging.hpp"
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

void OrderTracker::Add(const db::MboMsg &mbo) {
  OrderMap::iterator it = order_map.find(mbo.order_id);

  if (it == order_map.end()) {
    // New Order: Insert to Order Map & Expiry Queue
    order_map[mbo.order_id] =
        Order{mbo.order_id, static_cast<int64_t>(mbo.size), mbo.price, mbo.side,
              std::chrono::steady_clock::now()};

    expiry_queue_.push_back({mbo.order_id, std::chrono::steady_clock::now()});
  } else {
    // Existing Order: Update size directly (XNAS.ITCH multi-part add)
    it->second.size += mbo.size;
  }
}

void OrderTracker::Fill(const db::MboMsg &mbo) {
  // Accumulate: Add Fill size to PendingVolumeMap
  pending_volume_map_[mbo.order_id] += mbo.size;

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
    order_map.erase(expiry_queue_.front().first);
    expiry_queue_.pop_front();
  }
}
