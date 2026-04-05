#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "data/market.hpp"

enum class PlaybackState { Playing, Paused };

struct SessionStats {
  uint64_t start_ts = 0;
  uint64_t end_ts = 0;
  uint64_t current_ts = 0;
  uint64_t msg_count = 0;
};

class ReplayEngine;

/**
 * @class ReplayController
 * @brief Manages the background replay thread and current market state.
 */
class ReplayController {
 public:
  ReplayController(const std::string &data_path, uint32_t focus_instrument);
  ~ReplayController();

  void Start();
  void Stop();

  // Playback Control
  void SetPlaybackState(PlaybackState state) { m_playback_state = state; }
  PlaybackState GetPlaybackState() const { return m_playback_state; }
  void RequestStep() { m_step_requested = true; }
  void SetSpeed(int sleep_micros) { m_sleep_micros = sleep_micros; }
  int GetSpeed() const { return m_sleep_micros; }

  // Navigation
  void SeekToTime(uint64_t target_ts);
  SessionStats GetSessionStats();

  // Instrument Management
  void SetFocusInstrument(uint32_t instrument_id) {
    m_focus_instrument = instrument_id;
  }
  uint32_t GetFocusInstrument() const { return m_focus_instrument; }
  std::map<uint32_t, std::string> GetAvailableInstruments() const;

  MarketSnapshot GetLatestSnapshot();

 private:
  void ReplayLoop();

  std::string m_data_path;
  std::unique_ptr<ReplayEngine> m_engine;
  std::map<uint32_t, std::string> m_available_instruments;
  std::atomic<uint32_t> m_focus_instrument;
  std::atomic<bool> m_running{false};
  std::atomic<PlaybackState> m_playback_state{PlaybackState::Paused};
  std::atomic<bool> m_step_requested{false};
  std::atomic<int> m_sleep_micros{100};

  // Navigation state
  std::atomic<uint64_t> m_target_ts{0};
  std::atomic<bool> m_is_warping{false};

  std::unique_ptr<std::thread> m_thread;

  std::mutex m_mutex;
  MarketSnapshot m_latest_snapshot;
  SessionStats m_session_stats;
  uint64_t m_msg_count = 0;
};
