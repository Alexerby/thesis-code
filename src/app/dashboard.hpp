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
  void RenderSpreadGraph(ReplayController &controller, float width, float height);
  void RenderOrderEventList(ReplayController &controller, float width, float height);
  void RenderOrderBookDepth(const MarketSnapshot &snapshot, float width, float height);
  bool m_use_cumulative = true;
  bool m_request_quit = false;
  bool m_spread_follow = true;  // Keep time-axis locked to latest data
  float m_y_range = 0.05f;      // Manual Y-axis range in dollars
  float m_window_secs = 120.0f; // Rolling chart window in seconds

  // Visual toggles

  bool m_show_fills = true;
  bool m_show_cancels = false;
  bool m_show_adds = false;

  char m_jump_time[32] = "2023-05-30 09:30:00";
  char m_range_start[32] = "2023-05-30 09:30:00";
  char m_range_end[32] = "2023-05-30 09:35:00";
};

