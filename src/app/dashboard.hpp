#pragma once

#include "data/market.hpp"

class ReplayController;  // Forward declaration

/**
 * @class Dashboard
 * @brief Responsible for rendering the market visualization using ImGui.
 */
class Dashboard {
 public:
  Dashboard();
  ~Dashboard();

  void Render(const MarketSnapshot &snapshot, ReplayController &controller);
  bool ShouldQuit() const { return m_request_quit; }

 private:
  void RenderHeader(const MarketSnapshot &snapshot,
                    ReplayController &controller);
  void RenderPlaybackControls(ReplayController &controller);
  void RenderSpreadGraph(ReplayController &controller, float width);
  void RenderOrderEventList(ReplayController &controller, float width);
  void RenderOrderBookDepth(const MarketSnapshot &snapshot);

  bool m_use_cumulative = true;
  bool m_request_quit = false;
  bool m_spread_follow = true;  // Keep time-axis locked to latest data
  float m_y_range = 10.0f;      // Manual Y-axis range in dollars

  // Visual toggles

  bool m_show_fills = true;
  bool m_show_cancels = false;
  bool m_show_adds = false;
  };

