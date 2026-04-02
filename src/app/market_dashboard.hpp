#pragma once

#include "core/market.hpp"

class ReplayController; // Forward declaration

/**
 * @class MarketDashboard
 * @brief Responsible for rendering the market visualization using ImGui.
 */
class MarketDashboard {
public:
  MarketDashboard();
  ~MarketDashboard();

  void Render(const MarketSnapshot &snapshot, ReplayController &controller);
  bool ShouldQuit() const { return m_request_quit; }

private:
  void RenderHeader(const MarketSnapshot &snapshot, ReplayController &controller);
  void RenderPlaybackControls(ReplayController &controller);
  void RenderOrderBookDepth(const MarketSnapshot &snapshot);

  bool m_use_cumulative = true;
  bool m_request_quit = false;
};
