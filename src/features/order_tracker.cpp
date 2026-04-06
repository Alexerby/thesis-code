#include "features/order_tracker.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <unordered_map>

#include "core/logging.hpp"
#include "csv.hpp"
#include "data/book.hpp"
#include "databento/record.hpp"

#include <cstdlib>

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
    order_map[mbo.order_id] = Order{
        mbo.order_id,
        static_cast<int64_t>(mbo.size),
        mbo.price,
        mbo.side,
        std::chrono::steady_clock::now(),
        mbo.ts_recv.time_since_epoch().count(),
        mbo.hd.ts_event.time_since_epoch().count(),
        OrderInducedImbalance(mbo),
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
  // As this function is only called on CANCEL and RECONCILE events,
  // the matching engines timestamp here is the timestamp of the cancel
  // Therefore making delta the tracked diff between ADD and CANCEL.

  // Add feature: 'Order Age'
  uint64_t ts_cancel = mbo.hd.ts_event.time_since_epoch().count();
  double delta_t = static_cast<double>(ts_cancel - order.ts_event_add);

  // Add feature: 'Order-Induced Imbalance'
  feature_records_.push_back(FeatureRecord{delta_t, order.induced_imbalance});
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

double OrderTracker::OrderInducedImbalance(const db::MboMsg &mbo) {
  BestBidOffer bbo = market_.AggregatedBbo(instrument_id_);
  PriceLevel best_bid = bbo.first;
  PriceLevel best_ask = bbo.second;

  if (best_bid.IsEmpty() || best_ask.IsEmpty()) return 0.0;

  double V_b = best_bid.size;
  double V_a = best_ask.size;

  if (V_b + V_a == 0.0) return 0.0;

  // I_after: current (post-Add) BBO imbalance
  double I_after = (V_b - V_a) / (V_b + V_a);

  // Reconstruct pre-Add volumes by removing this order's contribution.
  // Only valid when the order sits at the current best on its side.
  // Orders that created a new best level or sit behind the touch
  // are treated as having no measurable BBO impact.
  double V_b_pre = V_b;
  double V_a_pre = V_a;

  if (mbo.side == db::Side::Bid && mbo.price == best_bid.price)
    V_b_pre = V_b - mbo.size;
  else if (mbo.side == db::Side::Ask && mbo.price == best_ask.price)
    V_a_pre = V_a - mbo.size;
  else
    return 0.0;  // no BBO impact

  double denom_pre = V_b_pre + V_a_pre;
  if (denom_pre == 0.0) return 0.0;
  double I_before = (V_b_pre - V_a_pre) / denom_pre;

  return abs(I_before - I_after);
}
