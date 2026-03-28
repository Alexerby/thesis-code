#pragma once

#include "book.hpp" // PriceLevel
#include "metadata.hpp"
#include <chrono>
#include <string>
#include <unordered_map>
#include <utility> // std::pair
#include <vector>

struct MarketState {
  std::string symbol;
  std::string timestamp;
  uint64_t ts_recv;

  // BBO
  std::pair<PriceLevel, PriceLevel> bbo;

  // Vector of imbalance levels
  std::vector<std::pair<std::string, double>> imbalance_levels;

  // Vector of volume levels
  std::vector<std::pair<std::string, double>> volume_levels;

  // Informational data
  int64_t last_trade_price;
  uint32_t last_trade_volume;
  uint64_t total_trade_volume;
};

class Visualizer {
public:
  // Fixed constructor syntax and member initialization
  Visualizer(
      const std::vector<std::size_t> &depths,
      std::chrono::milliseconds refresh_rate = std::chrono::milliseconds(100));

  void RecordAction(const MarketState &state, const MetadataSummary &meta);

private:
  void SetTimeDomain(uint64_t start_ts, uint64_t end_ts);

  // Logic helpers
  void UpdateHistory(const MarketState &state);
  void UpdateOverview(const MarketState &state);
  int GetVisibleLength(const std::string &str);

  // Layout and Widget helpers
  std::string GetHeader(const MarketState &state);
  std::string GetMetricBar(const std::string &label, double value);
  std::string GetBBO(const std::pair<PriceLevel, PriceLevel> &bbo);
  std::vector<std::string> GetMarketOverviewLines();
  std::vector<std::string> GetDashboardLines(const MarketState &state);
  std::vector<std::string> GetOrderbookLines(const MarketState &state);
  std::vector<std::string> GetPriceChartLines(const std::string &symbol);

  void DrawSplitPanel(const std::vector<std::string> &left,
                      const std::vector<std::string> &right);
  void DrawPriceHistory(const std::string &symbol);

  // Depths to be handled by the visualizer logic
  std::vector<std::size_t> m_depths;

  // History for the chart
  struct PricePoint {
    uint64_t ts_nanos;
    double bid;
    double ask;
    double last;
    uint32_t volume;
  };

  std::unordered_map<std::string, std::vector<PricePoint>> m_price_histories;
  std::unordered_map<std::string, MarketState> m_latest_states;
  std::unordered_map<std::string, uint64_t> m_last_total_volumes;
  std::unordered_map<std::string, uint32_t> m_accumulated_volumes;

  uint64_t m_start_ts = 0;
  uint64_t m_end_ts = 0;
  bool m_domain_set = false;

  // Throttling and sampling
  std::chrono::steady_clock::time_point m_last_render;
  uint64_t m_msg_counter = 0;
  const uint64_t HISTORY_SAMPLE_RATE = 100; // Sample every 100 messages

  // Colors
  const std::string RED = "\033[1;31m";
  const std::string GRN = "\033[1;32m";
  const std::string YEL = "\033[1;33m";
  const std::string CYN = "\033[1;36m";
  const std::string RESET = "\033[0m";

  // Clear screen
  const std::string CLEAR = "\033[2J\033[1;1H";

  // Refresh rate
  std::chrono::milliseconds m_refresh_rate;
};
