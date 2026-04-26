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
  void RenderVolumeChart(ReplayController &controller, float width, float height);
  void RenderOrderEventList(ReplayController &controller, float width, float height);
  void RenderOrderBookDepth(const MarketSnapshot &snapshot, float width, float height);
  bool  m_use_cumulative    = true;
  bool  m_request_quit      = false;
  bool  m_spread_follow     = true;
  float m_window_secs       = 120.0f;

  bool m_show_order_events = true;
  bool m_show_trades       = true;

  // Seconds of context shown on each side of a Play Range view
  int m_range_context_secs = 300;

  // Time field auto-init
  bool m_times_initialized = false;
  char m_jump_time[32]    = "2023-05-30 09:30:00";
  char m_range_start[32]  = "2023-05-30 09:30:00";
  char m_range_end[32]    = "2023-05-30 09:35:00";

  // Programmatic view reset (set by preset buttons / rubber-band)
  bool   m_reset_view  = false;
  double m_reset_x_min = 0.0;
  double m_reset_x_max = 0.0;

  // Rubber-band selection state
  bool   m_is_selecting    = false;
  double m_select_anchor_s = 0.0;
  double m_select_cursor_s = 0.0;

  // Last-frame X limits — used for data filtering in free-zoom mode
  double m_view_x_min    = 0.0;
  double m_view_x_max    = 0.0;
  double m_visible_secs  = 0.0;  // updated each frame inside the plot

  // Y-axis manual override (applied via Y-axis hover + scroll)
  bool   m_reset_y     = false;
  double m_reset_y_min = 0.0;
  double m_reset_y_max = 0.0;
  double m_view_y_min  = 0.0;
  double m_view_y_max  = 0.0;
};

