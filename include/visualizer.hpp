#pragma once

#include "book.hpp" // PriceLevel
#include <chrono>
#include <string>
#include <utility> // std::pair
#include <vector>

struct MarketState {
  std::string symbol;
  std::string timestamp;

  // Store pairs: { "Label", value }
  std::vector<std::pair<std::string, double>> imbalance_levels;
  std::pair<PriceLevel, PriceLevel> bbo;
};

class Visualizer {
public:
  // Fixed constructor syntax and member initialization
  Visualizer(
      const std::vector<std::size_t> &depths,
      std::chrono::milliseconds refresh_rate = std::chrono::milliseconds(20));

  void Render(const MarketState &state);

private:
  void DrawHeader(const MarketState &state);
  void DrawMetricBar(const std::string &label, double value);
  void DrawBBO(const std::pair<PriceLevel, PriceLevel> &bbo);

  // Depths to be handled by the visualizer logic
  std::vector<std::size_t> m_depths;

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
