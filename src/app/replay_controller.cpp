#include "app/replay_controller.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>

#include "data/replay_engine.hpp"

namespace {
// Format a databento UnixNanos timestamp as "HH:MM:SS.mmm ET"
std::string FormatET(databento::UnixNanos ts) {
  using namespace std::chrono;
  auto zt  = zoned_time{"America/New_York", ts};
  auto lt  = zt.get_local_time();
  auto sec = floor<seconds>(lt);
  auto day = floor<days>(lt);
  hh_mm_ss hms{sec - day};
  int ms = (int)duration_cast<milliseconds>(lt - sec).count();
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d ET",
                (int)hms.hours().count(), (int)hms.minutes().count(),
                (int)hms.seconds().count(), ms);
  return buf;
}
}  // namespace

ReplayController::ReplayController(const std::string &data_path,
                                   uint32_t focus_instrument)
    : m_data_path(data_path), m_focus_instrument(focus_instrument) {
  // Initialize engine early to get metadata/symbols
  m_engine = std::make_unique<ReplayEngine>(m_data_path, false);

  try {
    const auto &meta = m_engine->GetMetadata();
    m_file_start_ts = meta.start.time_since_epoch().count();

    const auto &symbol_map = m_engine->GetSymbolMap();
    std::set<std::string> ticker_set;
    for (const auto &entry : symbol_map.Map()) {
      uint32_t inst_id = entry.first.second; // databento-cpp: pair(date, id)
      std::string ticker = *entry.second;
      m_available_instruments[inst_id] = ticker;
      auto& ids = m_ticker_to_ids[ticker];
      if (std::find(ids.begin(), ids.end(), inst_id) == ids.end()) {
        ids.push_back(inst_id);
      }
      ticker_set.insert(ticker);
    }
    m_ticker_list = {ticker_set.begin(), ticker_set.end()};

    // Initialize focus ticker based on requested instrument ID
    if (m_focus_instrument != 0) {
      auto it = m_available_instruments.find(m_focus_instrument);
      if (it != m_available_instruments.end()) {
        SetFocusTicker(it->second);
      }
    }
  } catch (...) {
    // Handle or log error
  }
}

ReplayController::~ReplayController() { Stop(); }

void ReplayController::Start() {
  if (m_running) return;
  m_running = true;
  m_thread = std::make_unique<std::thread>(&ReplayController::ReplayLoop, this);
}

void ReplayController::Stop() {
  m_running = false;
  if (m_thread && m_thread->joinable()) {
    m_thread->join();
  }
}

const databento::Metadata& ReplayController::GetMetadata() const {
  return m_engine->GetMetadata();
}

MarketSnapshot ReplayController::GetLatestSnapshot() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_latest_snapshot;
}

std::vector<SpreadPoint> ReplayController::GetSpreadHistory() {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<SpreadPoint> result(m_spread_history.begin(), m_spread_history.end());

  // Append a synthetic point at current market time so the chart line
  // extends smoothly to "now" even when no new messages have arrived
  if (!result.empty() && m_session_stats.current_ts > 0) {
    double live_ts = static_cast<double>(m_session_stats.current_ts) / 1e9;
    if (live_ts > result.back().ts) {
      SpreadPoint live = result.back();
      live.ts = live_ts;
      live.action = '\0';  // no event marker
      result.push_back(live);
    }
  }
  return result;
}

std::vector<OrderEvent> ReplayController::GetOrderEvents() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return {m_order_events.begin(), m_order_events.end()};
}

void ReplayController::PlayRange(uint64_t start_ts, uint64_t end_ts) {
  constexpr uint64_t kPreBufferNs  = 120ULL * 1'000'000'000ULL;
  constexpr uint64_t kPostBufferNs = 120ULL * 1'000'000'000ULL;
  m_range_highlight_start = start_ts;
  m_range_highlight_end   = end_ts;
  uint64_t seek_to  = (start_ts > kPreBufferNs) ? start_ts - kPreBufferNs : start_ts;
  uint64_t pause_at = end_ts + kPostBufferNs;
  SeekToTime(seek_to);   // clears m_range_end_ts
  m_range_end_ts = pause_at;
}

ReplayController::RangeHighlight ReplayController::GetRangeHighlight() const {
  uint64_t s = m_range_highlight_start.load();
  uint64_t e = m_range_highlight_end.load();
  if (s == 0 || e == 0) return {};
  return {static_cast<double>(s) / 1e9, static_cast<double>(e) / 1e9, true};
}

void ReplayController::SeekToTime(uint64_t target_ts) {
  m_range_end_ts = 0;  // cancel any active range so warp always pauses on arrival
  uint64_t current;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    current = m_session_stats.current_ts;
  }

  if (current > 0 && target_ts < current) {
    // Backward seek: stop the replay thread, reinit engine, restart
    Stop();
    m_engine = std::make_unique<ReplayEngine>(m_data_path, false);
    m_msg_count = 0;
    m_last_spread_sample_ts = 0;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_spread_history.clear();
      m_order_events.clear();
      m_session_stats.current_ts = 0;
      m_session_stats.msg_count = 0;
    }
    m_target_ts = target_ts;
    m_is_warping = true;
    m_playback_state = PlaybackState::Playing;
    Start();
  } else {
    // Forward seek: fast-forward in current replay
    m_last_spread_sample_ts = 0;
    m_target_ts = target_ts;
    m_is_warping = true;
    m_playback_state = PlaybackState::Playing;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_spread_history.clear();
    m_order_events.clear();
  }
}

SessionStats ReplayController::GetSessionStats() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_session_stats;
}


void ReplayController::ReplayLoop() {
  Market market;
  const int MAX_DEPTH = 20;

  // Delta-T clock state
  uint64_t t_start_market = 0;
  std::chrono::steady_clock::time_point s_start_wall;
  bool clock_initialized = false;

  // Initial metadata setup
  {
    const auto &meta = m_engine->GetMetadata();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_session_stats.start_ts = meta.start.time_since_epoch().count();
    m_session_stats.end_ts = meta.end.time_since_epoch().count();
  }

  auto recalibrate = [&](uint64_t current_ts) {
    t_start_market = current_ts;
    s_start_wall = std::chrono::steady_clock::now();
    clock_initialized = true;
    m_recalibrate = false;
  };

  auto callback = [&](const db::MboMsg &mbo) {
    uint64_t current_ts = mbo.ts_recv.time_since_epoch().count();
    uint32_t inst_id = mbo.hd.instrument_id;

    int ticker_idx = m_focus_ticker_idx.load();
    const std::string &focus_ticker = (ticker_idx >= 0 && ticker_idx < (int)m_ticker_list.size())
        ? m_ticker_list[ticker_idx] : "";
    const auto &ticker_ids = m_ticker_to_ids.at(focus_ticker);

    auto sym_it = m_available_instruments.find(inst_id);
    bool is_focus = (sym_it != m_available_instruments.end() && sym_it->second == focus_ticker);

    // Warp Logic (Fast-forward to target time)
    if (m_is_warping) {
      if (current_ts < m_target_ts) {
        static uint64_t last_warp_ui_update = 0;
        if (is_focus && ++last_warp_ui_update >= 2000) {
          MarketSnapshot snap = market.GetSnapshot(ticker_ids, focus_ticker, MAX_DEPTH);
          snap.timestamp = FormatET(mbo.ts_recv);
          snap.msg_count = m_msg_count;
          {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latest_snapshot = std::move(snap);
            m_session_stats.current_ts = current_ts;
          }
          last_warp_ui_update = 0;
        }
        return true;
      } else {
        m_is_warping = false;
        if (m_range_end_ts == 0)
          m_playback_state = PlaybackState::Paused;
        clock_initialized = false;
      }
    }

    // Coarse progress tracking
    static uint64_t last_stats_update = 0;
    if (++last_stats_update >= 10000) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_session_stats.current_ts = current_ts;
      m_latest_snapshot.timestamp = FormatET(mbo.ts_recv);
      last_stats_update = 0;
    }

    // Wait while paused
    while (m_running && m_playback_state == PlaybackState::Paused) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      clock_initialized = false;  // Force recalibration on resume
    }

    if (!m_running) return false;

    // Delta-T synchronization: skip entirely during range playback (max speed).
    if (m_range_end_ts == 0) {
      if (m_recalibrate || !clock_initialized) {
        recalibrate(current_ts);
      } else {
        float speed = m_speed_multiplier.load();
        if (speed > 0.0f) {
          uint64_t dt_market_ns = current_ts - t_start_market;
          auto scheduled = s_start_wall + std::chrono::nanoseconds(
              static_cast<uint64_t>(dt_market_ns / speed));
          while (m_running &&
                 m_playback_state == PlaybackState::Playing &&
                 std::chrono::steady_clock::now() < scheduled) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            // Interpolate current_ts from wall clock so the chart advances
            // smoothly between message bursts instead of freezing
            auto wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - s_start_wall).count();
            uint64_t interp_ts = t_start_market +
                static_cast<uint64_t>(static_cast<double>(wall_ns) * speed);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_session_stats.current_ts = interp_ts;
          }
        }
      }
    }

    // Auto-pause at range end
    uint64_t range_end = m_range_end_ts.load();
    if (range_end > 0 && current_ts >= range_end) {
      m_playback_state = PlaybackState::Paused;
      m_range_end_ts = 0;
      clock_initialized = false;
    }

    if (is_focus) {
      m_msg_count++;
      MarketSnapshot snap = market.GetSnapshot(ticker_ids, focus_ticker, MAX_DEPTH);
      snap.msg_count = m_msg_count;
      snap.timestamp = FormatET(mbo.ts_recv);

      RecordEvent(mbo, snap);

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_last_snap = m_latest_snapshot;
        m_latest_snapshot = std::move(snap);
        m_session_stats.current_ts = current_ts;
        m_session_stats.msg_count = m_msg_count;
      }
    }
    return true;
  };

  try {
    m_engine->Run(market, callback);
  } catch (const std::exception &e) {
    std::cerr << "Replay Error: " << e.what() << std::endl;
  }
  m_running = false;
}

void ReplayController::RecordEvent(const db::MboMsg &mbo,
                                   const MarketSnapshot &snap) {
  uint64_t current_ts = mbo.ts_recv.time_since_epoch().count();

  // Buffer state to identify if a Cancel is part of a Trade/Fill event
  if (current_ts != m_current_event.ts_recv) {
    m_current_event.ts_recv = current_ts;
    m_current_event.has_fill = false;
    m_current_event.has_trade = false;
  }

  if (mbo.action == db::Action::Fill) m_current_event.has_fill = true;
  if (mbo.action == db::Action::Trade) m_current_event.has_trade = true;

  bool high_signal = false;
  char act = '?';

  switch (mbo.action) {
    case db::Action::Add:
      act = 'A';
      high_signal = true;
      break;
    case db::Action::Fill:
    case db::Action::Trade:
      act = (mbo.action == db::Action::Fill) ? 'F' : 'T';
      high_signal = true;
      break;
    case db::Action::Cancel:
      // PURE CANCEL: Only if no Fill/Trade in this sequence
      if (!m_current_event.has_fill && !m_current_event.has_trade) {
        act = 'C';
        high_signal = true;
      }
      break;
    case db::Action::Clear:
      act = 'X';
      high_signal = true;
      break;
    default:
      break;
  }

  if (high_signal) {
    char side = '-';
    if (mbo.side == db::Side::Bid)
      side = 'B';
    else if (mbo.side == db::Side::Ask)
      side = 'S';

    uint64_t ts_ns = mbo.ts_recv.time_since_epoch().count();
    double ts_s = static_cast<double>(ts_ns) / 1e9;

    std::lock_guard<std::mutex> lock(m_mutex);

    SpreadPoint pt{
        ts_s,
        snap.best_bid,
        snap.best_ask,
        m_last_snap.best_bid,
        m_last_snap.best_ask,
        act,
        side,
        static_cast<double>(mbo.price) / 1e9,
        mbo.size,
    };
    m_spread_history.push_back(pt);
    if (m_spread_history.size() > kMaxSpreadHistory) {
      m_spread_history.pop_front();
    }

    OrderEvent ev{
        ts_ns, ts_s, act, side, static_cast<double>(mbo.price) / 1e9, mbo.size,
    };
    m_order_events.push_back(ev);
    if (m_order_events.size() > kMaxOrderEvents) {
      m_order_events.pop_front();
    }
  }
}
