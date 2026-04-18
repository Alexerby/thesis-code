#include "app/replay_controller.hpp"

#include <chrono>
#include <iostream>

#include "data/replay_engine.hpp"

ReplayController::ReplayController(const std::string &data_path,
                                   uint32_t focus_instrument)
    : m_data_path(data_path), m_focus_instrument(focus_instrument) {
  // Initialize engine early to get metadata/symbols
  m_engine = std::make_unique<ReplayEngine>(m_data_path, false);

  try {
    const auto &symbol_map = m_engine->GetSymbolMap();
    for (const auto &entry : symbol_map.Map()) {
      m_available_instruments[entry.first.second] = *entry.second;
    }

    if (m_focus_instrument == 0 && !m_available_instruments.empty()) {
      m_focus_instrument = m_available_instruments.begin()->first;
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

MarketSnapshot ReplayController::GetLatestSnapshot() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_latest_snapshot;
}

std::vector<SpreadPoint> ReplayController::GetSpreadHistory() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return {m_spread_history.begin(), m_spread_history.end()};
}

std::vector<OrderEvent> ReplayController::GetOrderEvents() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return {m_order_events.begin(), m_order_events.end()};
}

void ReplayController::SeekToTime(uint64_t target_ts) {
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

std::map<uint32_t, std::string> ReplayController::GetAvailableInstruments()
    const {
  return m_available_instruments;
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
    uint32_t focus_id = m_focus_instrument.load();
    bool is_focus = (mbo.hd.instrument_id == focus_id);

    std::string symbol = "Unknown";
    auto it = m_available_instruments.find(focus_id);
    if (it != m_available_instruments.end()) symbol = it->second;

    // Warp Logic (Fast-forward to target time)
    if (m_is_warping) {
      if (current_ts < m_target_ts) {
        static uint64_t last_warp_ui_update = 0;
        if (is_focus && ++last_warp_ui_update >= 2000) {
          MarketSnapshot snap = market.GetSnapshot(focus_id, symbol, MAX_DEPTH);
          snap.timestamp = db::ToIso8601(mbo.ts_recv);
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
        m_playback_state = PlaybackState::Paused;
        clock_initialized = false;
      }
    }

    // Coarse progress tracking
    static uint64_t last_stats_update = 0;
    if (++last_stats_update >= 10000) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_session_stats.current_ts = current_ts;
      m_latest_snapshot.timestamp = db::ToIso8601(mbo.ts_recv);
      last_stats_update = 0;
    }

    // Wait while paused
    while (m_running && m_playback_state == PlaybackState::Paused &&
           !m_step_requested) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      clock_initialized = false;  // Force recalibration on resume
    }

    if (!m_running) return false;

    bool stepping = m_step_requested.exchange(false);

    // Delta-T synchronization: process all messages whose market time has
    // elapsed on the wall clock; sleep only when we are ahead of real time.
    if (!stepping) {
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
          }
        }
      }
    }

    if (is_focus) {
      m_msg_count++;
      MarketSnapshot snap = market.GetSnapshot(focus_id, symbol, MAX_DEPTH);
      snap.msg_count = m_msg_count;
      snap.timestamp = db::ToIso8601(mbo.ts_recv);

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
