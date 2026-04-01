/**
 * @file visualizer.hpp
 * @brief TUI-based visualization engine for market data.
 */

#pragma once

#include <databento/dbn.hpp>
#include <chrono>
#include <string>
#include <unordered_map>
#include <utility> // std::pair
#include <vector>
#include "market.hpp"


/**
 * @class Visualizer
 * @brief High-performance terminal-based dashboard for real-time market monitoring.
 */
class Visualizer {
public:
  /**
   * @brief Constructs the visualizer with specific configuration.
   * @param depths Vector of depth levels to visualize (e.g., {1, 2, 3}).
   * @param refresh_rate Minimum interval between screen renders.
   */
  Visualizer(
      const std::vector<std::size_t> &depths,
      std::chrono::milliseconds refresh_rate = std::chrono::milliseconds(100));

  /**
   * @brief Records a new market state update. Triggers a render if the refresh rate is met.
   * @param state The newly captured market state.
   * @param meta Metadata for time domain context.
   */
  void RecordAction(const MarketState &state, const databento::Metadata &meta);

  /**
   * @brief Sets the primary symbol to be focused in the main dashboard panels.
   */
  void SetFocus(const std::string &symbol) { m_focused_symbol = symbol; }

  /**
   * @brief Renders a complete, non-rolling price history chart for a symbol.
   * @param symbol Ticker to visualize.
   */
  void DrawFullHistory(const std::string &symbol);

private:
  void SetTimeDomain(uint64_t start_ts, uint64_t end_ts);

  void UpdateHistory(const MarketState &state);
  void UpdateOverview(const MarketState &state);
  void UpdateDynamicDomain(uint64_t current_ts);
  int GetVisibleLength(const std::string &str);

  std::string GetHeader(const std::string &symbol, const std::string &timestamp);
  std::string GetMetricBar(const std::string &label, double value);
  std::pair<double, double> GetPriceRange(const std::string &symbol, uint64_t start, uint64_t end);

  std::string GetBBO(const std::pair<PriceLevel, PriceLevel> &bbo);
  std::vector<std::string> GetMarketOverviewLines();
  std::vector<std::string> GetDashboardLines(const MarketState &state);
  std::vector<std::string> GetOrderbookLines(const MarketState &state);
  std::vector<std::string> GetPriceChartLines(const std::string &symbol, uint64_t start, uint64_t end);

  void DrawSplitPanel(const std::vector<std::string> &left,
                      const std::vector<std::string> &right);
  void DrawPriceHistory(const std::string &symbol, uint64_t start, uint64_t end);

  std::vector<std::size_t> m_depths;
  std::string m_focused_symbol;

  struct PricePoint {
    uint64_t ts_nanos;
    double bid;
    double ask;
    double last;
  };

  std::unordered_map<std::string, std::vector<PricePoint>> m_price_histories;
  std::unordered_map<std::string, MarketState> m_latest_states;

  uint64_t m_start_ts = 0;
  uint64_t m_end_ts = 0;
  uint64_t m_global_start = 0;
  uint64_t m_global_end = 0;
  bool m_domain_set = false;

  std::chrono::steady_clock::time_point m_last_render;
  uint64_t m_msg_counter = 0;
  const uint64_t HISTORY_SAMPLE_RATE = 100;

  const std::string RED = "\033[1;31m";
  const std::string GRN = "\033[1;32m";
  const std::string YEL = "\033[1;33m";
  const std::string CYN = "\033[1;36m";
  const std::string BOLD = "\033[1m";
  const std::string RESET = "\033[0m";
  const std::string CLEAR = "\033[2J\033[1;1H";

  std::chrono::milliseconds m_refresh_rate;
};
