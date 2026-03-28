#pragma once

#include "book.hpp" // PriceLevel
#include <chrono>
#include <string>
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
  uint64_t total_trade_volume;
};

class Visualizer {
public:
  // Fixed constructor syntax and member initialization
  Visualizer(
      const std::vector<std::size_t> &depths,
      std::chrono::milliseconds refresh_rate = std::chrono::milliseconds(100));

  void Render(const MarketState &state);
  void SetTimeDomain(uint64_t start_ts, uint64_t end_ts);

private:
  // Layout helpers
  std::string GetHeader(const MarketState &state);
  std::string GetMetricBar(const std::string &label, double value);
  std::vector<std::string> GetOrderbookLines(const MarketState &state);
  std::vector<std::string> GetPriceChartLines();
  std::string GetBBO(const std::pair<PriceLevel, PriceLevel> &bbo);

  // Depths to be handled by the visualizer logic
  std::vector<std::size_t> m_depths;

  // History for the chart
  struct PricePoint {
    uint64_t ts_nanos;
    double bid;
    double ask;
    double last;
  };
  std::vector<PricePoint> m_price_history;
  uint64_t m_start_ts = 0;
  uint64_t m_end_ts = 0;

  // Throttling and sampling
  std::chrono::steady_clock::time_point m_last_render;
  uint64_t m_msg_counter = 0;
  const uint64_t HISTORY_SAMPLE_RATE = 1000; // Sample every 1000 messages

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
