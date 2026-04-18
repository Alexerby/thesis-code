#include "features/order_tracker.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>

#include "core/logging.hpp"
#include "csv.hpp"
#include "data/book.hpp"
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

double OrderTracker::RollingMedianSize() const {
  if (size_window_.empty()) return 1.0;
  std::vector<uint32_t> sorted(size_window_.begin(), size_window_.end());
  std::sort(sorted.begin(), sorted.end());
  return sorted[sorted.size() / 2];
}

void OrderTracker::Add(const db::MboMsg &mbo) {
  OrderMap::iterator it = order_map.find(mbo.order_id);

  if (it == order_map.end()) {
    double median = RollingMedianSize();
    double rel_size = (median > 0.0) ? (mbo.size / median) : 1.0;

    order_map[mbo.order_id] = Order{
        mbo.order_id,
        static_cast<int64_t>(mbo.size),
        mbo.price,
        mbo.side,
        std::chrono::steady_clock::now(),
        mbo.ts_recv.time_since_epoch().count(),
        mbo.hd.ts_event.time_since_epoch().count(),
        OrderInducedImbalance(mbo),
        0,
        market_.GetVolumeAhead(instrument_id_, mbo.order_id),
        rel_size,
    };

    size_window_.push_back(mbo.size);
    if (static_cast<int>(size_window_.size()) > kSizeWindowN)
      size_window_.pop_front();

    expiry_queue_.push_back({mbo.order_id, std::chrono::steady_clock::now()});
  } else {
    // Existing Order: Update size directly (XNAS.ITCH multi-part add)
    it->second.size += mbo.size;
  }
}

void OrderTracker::Fill(const db::MboMsg &mbo) {
  // Accumulate fill volume on the Order itself (persists across events)
  auto it = order_map.find(mbo.order_id);
  if (it != order_map.end()) it->second.total_filled += mbo.size;

  // Also stage in pending_volume_map_ for same-event reconciliation
  pending_volume_map_[mbo.order_id] += mbo.size;
  // If this is the last message in the event, reconcile immediately
  // I don't think this should ever be the case.
  // This is just me quadrupel-checking.
  if (mbo.flags.IsLast()) {
    Reconcile(mbo);
  }
}
void OrderTracker::Cancel(const db::MboMsg &mbo) {
  if (!mbo.flags.IsLast()) {  // More messages expected for this OrderID
    // Pure Cancel: Reduce OrderMap size directly via OrderID
    OrderMap::iterator it = order_map.find(mbo.order_id);
    if (it != order_map.end()) {
      it->second.size -= mbo.size;
      if (it->second.size <= 0) {
        EmitFeatureRecord(it->second, mbo, CancelType::Pure);
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
      EmitFeatureRecord(it->second, mbo, CancelType::Pure);
    } else {
      EmitFeatureRecord(it->second, mbo, CancelType::Fill);
    }
    order_map.erase(it);
  }
}

void OrderTracker::EmitFeatureRecord(const Order &order, const db::MboMsg &mbo,
                                     CancelType cancel_type) {
  uint64_t ts_cancel = mbo.hd.ts_event.time_since_epoch().count();
  double delta_t = static_cast<double>(ts_cancel - order.ts_event_add);

  // Override using cross-event fill history. staged_vol (pending_volume_map_)
  // only covers fills within the current event; total_filled persists across
  // all events for this order's lifetime.
  CancelType resolved = (order.total_filled > 0) ? CancelType::Fill : CancelType::Pure;

  feature_records_.push_back(
      FeatureRecord{
          delta_t,
          order.induced_imbalance,
          static_cast<double>(order.volume_ahead),
          order.relative_size,
          resolved,
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

double OrderTracker::OrderInducedImbalance(const db::MboMsg &mbo) {
  const int N = 5;
  Side side = (mbo.side == db::Side::Bid) ? Side::Bid : Side::Ask;

  // Pre-add OBI: top-N depth with this order's contribution removed
  auto [V_b_pre, V_a_pre] = market_.GetTopNDepthExcluding(
      instrument_id_, N, mbo.price, mbo.size, side);
  double denom_pre = V_b_pre + V_a_pre;
  if (denom_pre == 0.0) return 0.0;
  double OBI_before = (V_b_pre - V_a_pre) / denom_pre;

  // Post-add OBI: top-N depth including this order
  auto [V_b, V_a] = market_.GetTopNDepth(instrument_id_, N);
  double denom = V_b + V_a;
  if (denom == 0.0) return 0.0;
  double OBI_after = (V_b - V_a) / denom;

  return std::abs(OBI_after - OBI_before);
}
