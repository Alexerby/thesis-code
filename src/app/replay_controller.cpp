#include "app/replay_controller.hpp"
#include "data/replay_engine.hpp"
#include <chrono>
#include <iostream>

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
  if (m_running)
    return;
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

void ReplayController::SeekToTime(uint64_t target_ts) {
  m_target_ts = target_ts;
  m_is_warping = true;
  m_playback_state = PlaybackState::Playing; // Ensure we are moving
}

SessionStats ReplayController::GetSessionStats() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_session_stats;
}

std::map<uint32_t, std::string> ReplayController::GetAvailableInstruments() const {
    return m_available_instruments;
}

void ReplayController::ReplayLoop() {
  Market market;
  const int MAX_DEPTH = 20;

  // Initial metadata setup
  {
    const auto& meta = m_engine->GetMetadata();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_session_stats.start_ts = meta.start.time_since_epoch().count();
    m_session_stats.end_ts = meta.end.time_since_epoch().count();
  }

  auto callback = [&](const db::MboMsg &mbo) {
    uint64_t current_ts = mbo.ts_recv.time_since_epoch().count();
    uint32_t focus_id = m_focus_instrument.load();
    bool is_focus = (mbo.hd.instrument_id == focus_id);

    // Resolve symbol for current focus
    std::string symbol = "Unknown";
    auto it = m_available_instruments.find(focus_id);
    if (it != m_available_instruments.end()) {
        symbol = it->second;
    }

    // Warp Logic (Fast-forward to target time)
    if (m_is_warping) {
      if (current_ts < m_target_ts) {
        // Throttled UI update during warp so we see the mountain move
        static uint64_t last_warp_ui_update = 0;
        if (is_focus && ++last_warp_ui_update >= 2000) {
          MarketSnapshot snap =
              market.GetSnapshot(focus_id, symbol, MAX_DEPTH);
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
        m_playback_state = PlaybackState::Paused; // Pause when reached
      }
    }

    // Coarse Progress Tracking (for non-focus or low-activity periods)
    static uint64_t last_stats_update = 0;
    if (++last_stats_update >= 10000) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_session_stats.current_ts = current_ts;
      m_latest_snapshot.timestamp = db::ToIso8601(mbo.ts_recv);
      last_stats_update = 0;
    }

    // Wait while paused (and not stepping)
    while (m_running && m_playback_state == PlaybackState::Paused &&
           !m_step_requested) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!m_running)
      return false;

    // Consume step request if present
    bool stepping = m_step_requested.exchange(false);

    if (is_focus) {
      m_msg_count++;
      MarketSnapshot snap =
          market.GetSnapshot(focus_id, symbol, MAX_DEPTH);
      snap.msg_count = m_msg_count;
      snap.timestamp = db::ToIso8601(mbo.ts_recv);

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latest_snapshot = std::move(snap);
        m_session_stats.current_ts = current_ts;
        m_session_stats.msg_count = m_msg_count;
      }

      // Control replay speed (Skip sleep if stepping)
      if (!stepping && m_sleep_micros > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(m_sleep_micros));
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
