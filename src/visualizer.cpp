#include "visualizer.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

Visualizer::Visualizer(const std::vector<std::size_t> &depths,
                       std::chrono::milliseconds refresh_rate)
    : m_depths(depths), m_refresh_rate(refresh_rate) {}

void Visualizer::DrawHeader(const MarketState &state) {
  std::cout << CLEAR;
  std::cout << YEL << ">> " << state.symbol << " | " << state.timestamp << RESET
            << "\n";
}

void Visualizer::DrawMetricBar(const std::string &label, double value) {
    const int bar_width = 20;
    const int mid = bar_width / 2; // Position 10
    
    // Clamp value to [-1, 1] for safety
    double clamped = std::max(-1.0, std::min(1.0, value));
    
    // Calculate how many characters to fill (0 to 10)
    int fill_amount = static_cast<int>(std::abs(clamped) * mid);
    
    std::string bar = std::string(bar_width, '.');
    std::string color = RESET;

    if (clamped > 0.01) { // Positive: Fill right side
        color = GRN;
        for (int i = 0; i < fill_amount; ++i) {
            bar[mid + i] = '|';
        }
    } else if (clamped < -0.01) { // Negative: Fill left side
        color = RED;
        for (int i = 0; i < fill_amount; ++i) {
            bar[mid - 1 - i] = '|';
        }
    } else {
        // Neutral: Just a center marker
        bar[mid] = ':';
    }

    std::cout << std::left << std::setw(15) << label << " [" 
              << color << bar << RESET << "] " 
              << std::fixed << std::setprecision(4) << value << "\n";
}

void Visualizer::DrawBBO(const std::pair<PriceLevel, PriceLevel> &bbo) {
  const auto &bid = bbo.first;
  const auto &ask = bbo.second;

  std::cout << "\n";
  if (ask.size > 0) {
    std::cout << RED << "  ASK  " << RESET << std::setw(8) << ask.size << " @ "
              << std::fixed << std::setprecision(4) << (ask.price / 1e9)
              << "\n";
  }
  if (bid.size > 0) {
    std::cout << GRN << "  BID  " << RESET << std::setw(8) << bid.size << " @ "
              << std::fixed << std::setprecision(4) << (bid.price / 1e9)
              << "\n";
  }
}

void Visualizer::Render(const MarketState &state) {
  // Draw the top info
  DrawHeader(state);

  // Loop through the pre-calculated metrics in MarketState
  for (const auto &metric : state.imbalance_levels) {
    DrawMetricBar(metric.first, metric.second);
  }

  // Draw the spread/BBO area
  DrawBBO(state.bbo);

  std::cout << std::string(60, '-') << std::endl;

  // Use the configured refresh rate
  std::this_thread::sleep_for(m_refresh_rate);
}
