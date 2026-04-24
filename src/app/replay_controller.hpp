#pragma once

#include <algorithm>
#include <atomic>
#include <databento/dbn_file_store.hpp>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "data/market.hpp"

struct SpreadPoint {
  double ts;      // Unix seconds
  double bid;     // Best bid price
  double ask;     // Best ask price
  double pre_bid; // BBO state BEFORE the event
  double pre_ask; // BBO state BEFORE the event
  char action;    // 'A', 'C', 'F', 'T'
  char side;      // 'B', 'S'
  double price;   // Price of the event itself
  uint32_t size;  // Size of the event
};

struct OrderEvent {
  uint64_t ts_ns;  // Nanosecond timestamp for seeking
  double ts;       // Unix seconds for display
  char action;     // 'A'=Add, 'C'=Cancel, 'F'=Fill, 'X'=Clear
  char side;       // 'B'=Bid, 'S'=Ask, '-'=None
  double price;
  uint32_t size;
};

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
  void SetSpeedMultiplier(float s) { m_speed_multiplier = s; m_recalibrate = true; }
  float GetSpeedMultiplier() const { return m_speed_multiplier; }

  // Navigation
  void SeekToTime(uint64_t target_ts);
  void PlayRange(uint64_t start_ts, uint64_t end_ts,
                 uint64_t context_ns = 120ULL * 1'000'000'000ULL);
  SessionStats GetSessionStats();

  // Ticker Management (one logical ticker may span multiple instrument IDs across days)
  const std::vector<std::string>& GetAvailableTickers() const { return m_ticker_list; }
  std::string GetFocusTicker() const {
    int idx = m_focus_ticker_idx.load();
    if (idx >= 0 && idx < (int)m_ticker_list.size()) return m_ticker_list[idx];
    return "";
  }
  void SetFocusTicker(const std::string &ticker) {
    auto it = std::find(m_ticker_list.begin(), m_ticker_list.end(), ticker);
    if (it != m_ticker_list.end())
      m_focus_ticker_idx = (int)(it - m_ticker_list.begin());
  }

  uint64_t GetFileStartTs() const { return m_file_start_ts; }
  const databento::Metadata& GetMetadata() const;
  MarketSnapshot GetLatestSnapshot();
  std::vector<SpreadPoint> GetSpreadHistory();
  std::vector<OrderEvent> GetOrderEvents();

  struct RangeHighlight {
    double start_s = 0.0;
    double end_s   = 0.0;
    bool   active  = false;
  };
  RangeHighlight GetRangeHighlight() const;

 private:
  void ReplayLoop();
  void RecordEvent(const db::MboMsg &mbo, const MarketSnapshot &snap);


  uint64_t m_file_start_ts = 0;
  uint32_t m_focus_instrument = 0;
  std::string m_data_path;
  std::unique_ptr<ReplayEngine> m_engine;
  std::map<uint32_t, std::string> m_available_instruments;  // id → ticker
  std::map<std::string, std::vector<uint32_t>> m_ticker_to_ids; // ticker → [ids]
  std::vector<std::string> m_ticker_list;                   // sorted unique tickers
  std::atomic<int> m_focus_ticker_idx{0};
  std::atomic<bool> m_running{false};
  std::atomic<PlaybackState> m_playback_state{PlaybackState::Paused};
  std::atomic<float> m_speed_multiplier{1.0f};
  std::atomic<bool> m_recalibrate{false};

  // Navigation state
  std::atomic<uint64_t> m_target_ts{0};
  std::atomic<bool> m_is_warping{false};
  std::atomic<uint64_t> m_range_end_ts{0};
  std::atomic<uint64_t> m_range_highlight_start{0};
  std::atomic<uint64_t> m_range_highlight_end{0};

  std::unique_ptr<std::thread> m_thread;

  std::mutex m_mutex;
  MarketSnapshot m_latest_snapshot;
  MarketSnapshot m_last_snap; // State BEFORE current message
  SessionStats m_session_stats;
  uint64_t m_msg_count = 0;

  static constexpr size_t kMaxSpreadHistory = 1000000;
  std::deque<SpreadPoint> m_spread_history;
  std::atomic<uint64_t> m_last_spread_sample_ts{0};

  static constexpr size_t kMaxOrderEvents = 100000;
  std::deque<OrderEvent> m_order_events;

  // Event buffering for high-signal reconciliation
  struct {
    uint64_t ts_recv = 0;
    bool has_fill = false;
    bool has_trade = false;
  } m_current_event;
};
