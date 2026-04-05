#include "features/order_tracker.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <unordered_map>

#include "core/logging.hpp"
#include "csv.hpp"
#include "databento/record.hpp"

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
  std::cout << "Dumped " << order_map.size() << " orders to features/"
            << filename << std::endl;
}

void OrderTracker::Add(const db::MboMsg &mbo) {
  OrderMap::iterator it = order_map.find(mbo.order_id);

  if (it == order_map.end()) {
    // Snapshot book state at add time (market.Apply() has already run)
    double imbalance = market_.AggregatedDeepImbalance(instrument_id_, 1);
    auto bbo = market_.AggregatedBbo(instrument_id_);
    PriceLevel best_bid = bbo.first;
    PriceLevel best_ask = bbo.second;

    double dist_to_touch = 0.0;
    if (mbo.side == db::Side::Bid && !best_bid.IsEmpty()) {
      dist_to_touch = static_cast<double>(best_bid.price - mbo.price);
    } else if (mbo.side == db::Side::Ask && !best_ask.IsEmpty()) {
      dist_to_touch = static_cast<double>(mbo.price - best_ask.price);
    }

    // New Order: Insert to Order Map & Expiry Queue
    order_map[mbo.order_id] = Order{
        mbo.order_id,
        static_cast<int64_t>(mbo.size),
        mbo.price,
        mbo.side,
        std::chrono::steady_clock::now(),
        mbo.ts_recv.time_since_epoch().count(),
        mbo.hd.ts_event.time_since_epoch().count(),
        imbalance,
        dist_to_touch,
        static_cast<int64_t>(mbo.size),
        0  // queue_pos_at_add: TODO: requires pre-apply book snapshot
    };

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
        EmitFeatureRecord(it->second, mbo);
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
    if (staged_vol == 0) {
      // Pure cancellation — no fill component, emit feature record
      EmitFeatureRecord(it->second, mbo);
    }
    order_map.erase(it);
  }
}

void OrderTracker::EmitFeatureRecord(const Order &order,
                                     const db::MboMsg &mbo) {
  uint64_t ts_cancel = mbo.hd.ts_event.time_since_epoch().count();
  double delta_t = static_cast<double>(ts_cancel - order.ts_event_add);

  double imbalance_at_cancel =
      market_.AggregatedDeepImbalance(instrument_id_, 1);
  double delta_imbalance = imbalance_at_cancel - order.imbalance_at_add;

  auto bbo = market_.AggregatedBbo(instrument_id_);
  PriceLevel best_bid = bbo.first;
  PriceLevel best_ask = bbo.second;

  double dist_touch = 0.0;
  if (order.side == db::Side::Bid && !best_bid.IsEmpty()) {
    dist_touch = static_cast<double>(best_bid.price - order.price);
  } else if (order.side == db::Side::Ask && !best_ask.IsEmpty()) {
    dist_touch = static_cast<double>(order.price - best_ask.price);
  }

  double size_ratio = 0.0;
  if (order.side == db::Side::Bid && !best_bid.IsEmpty() && best_bid.size > 0) {
    size_ratio = static_cast<double>(order.size_at_add) / best_bid.size;
  } else if (order.side == db::Side::Ask && !best_ask.IsEmpty() &&
             best_ask.size > 0) {
    size_ratio = static_cast<double>(order.size_at_add) / best_ask.size;
  }

  feature_records_.push_back(FeatureRecord{
      delta_t, delta_imbalance, size_ratio,
      static_cast<double>(order.queue_pos_at_add), dist_touch,
      0.0  // cancel_rate: TODO: requires \pm 500ms rolling window
  });
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
      order_map.erase(it);
    }
    expiry_queue_.pop_front();
  }
}
