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
  void RenderOrderBookDepth(const MarketSnapshot &snapshot);

  bool m_use_cumulative = true;
  bool m_request_quit = false;
};
