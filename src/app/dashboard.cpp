#include "app/dashboard.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "app/replay_controller.hpp"
#include "imgui.h"
#include "implot.h"

namespace {  // make anonymous
constexpr uint64_t NANOS_1S = 1'000'000'000ULL;
constexpr uint64_t NANOS_10S = 10'000'000'000ULL;
constexpr uint64_t NANOS_1M = 60'000'000'000ULL;

constexpr float HEADER_HEIGHT = 50.0f;
constexpr float CONTROLS_HEIGHT = 80.0f;
constexpr float SPREAD_GRAPH_HEIGHT = 480.0f;
constexpr float ORDERBOOK_DEPTH_HEIGHT = 270.0f;

constexpr float CHART_CENTER_WIDTH = 120.0f;

void TextCentered(const char *text, float width) {
  float textWidth = ImGui::CalcTextSize(text).x;
  if (width > textWidth)
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - textWidth) * 0.5f);
  ImGui::Text("%s", text);
}

// Convert HH:MM:SS to nanoseconds offset from start of day
uint64_t TimeStringToNanos(const std::string &time_str, uint64_t reference_ts) {
  std::tm t = {};
  std::istringstream ss(time_str);
  ss >> std::get_time(&t, "%H:%M:%S");
  if (ss.fail()) return 0;

  // reference_ts is unix nanos. We need the date part from it.
  std::time_t ref_secs = reference_ts / NANOS_1S;
  std::tm *ref_tm = std::gmtime(&ref_secs);

  ref_tm->tm_hour = t.tm_hour;
  ref_tm->tm_min = t.tm_min;
  ref_tm->tm_sec = t.tm_sec;

  return static_cast<uint64_t>(timegm(ref_tm)) * NANOS_1S;
}
}  // namespace

Dashboard::Dashboard() {}
Dashboard::~Dashboard() {}

void Dashboard::Render(const MarketSnapshot &snapshot,
                       ReplayController &controller) {
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

  // Push window to the stack
  ImGui::Begin("Market Visualizer", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

  // ALl the appending to the window (when on stack) happening here
  RenderHeader(snapshot, controller);

  ImGui::Spacing();

  // Controls section
  RenderPlaybackControls(controller);

  // Middle row: spread graph + order event list side by side
  {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float event_w = 290.0f;
    const float graph_w = avail - event_w - ImGui::GetStyle().ItemSpacing.x;
    RenderSpreadGraph(controller, graph_w);
    ImGui::SameLine();
    RenderOrderEventList(controller, event_w);
  }

  // Order book depth (full width)
  RenderOrderBookDepth(snapshot);

  // Pop window from stack
  ImGui::End();
}

void Dashboard::RenderPlaybackControls(ReplayController &controller) {
  ImGui::BeginChild("Controls", ImVec2(0, CONTROLS_HEIGHT), true);
  SessionStats stats = controller.GetSessionStats();
  PlaybackState state = controller.GetPlaybackState();

  // Play/Pause/Step
  if (state == PlaybackState::Paused) {
    if (ImGui::Button(" Play "))
      controller.SetPlaybackState(PlaybackState::Playing);
    ImGui::SameLine();
    if (ImGui::Button(" Step > ")) controller.RequestStep();
  } else {
    if (ImGui::Button(" Pause "))
      controller.SetPlaybackState(PlaybackState::Paused);
  }

  // Skips
  ImGui::SameLine(0, 20);
  if (ImGui::Button("+1s")) controller.SeekToTime(stats.current_ts + NANOS_1S);
  ImGui::SameLine();
  if (ImGui::Button("+10s"))
    controller.SeekToTime(stats.current_ts + NANOS_10S);
  ImGui::SameLine();
  if (ImGui::Button("+1m")) controller.SeekToTime(stats.current_ts + NANOS_1M);

  // Jump to Time
  ImGui::SameLine(0, 40);
  static char jump_time[16] = "14:30:00";
  ImGui::SetNextItemWidth(100);
  ImGui::InputText("##JumpTime", jump_time, IM_ARRAYSIZE(jump_time));
  ImGui::SameLine();
  if (ImGui::Button("Jump to Time")) {
    uint64_t target = TimeStringToNanos(jump_time, stats.start_ts);
    if (target > 0) controller.SeekToTime(target);
  }

  // Delay Slider
  ImGui::SameLine(0, 40);
  ImGui::SetNextItemWidth(150);
  int speed = controller.GetSpeed();
  if (ImGui::SliderInt("Delay (us)", &speed, 0, 10000))
    controller.SetSpeed(speed);

  // Timeline
  float progress = 0.0f;
  if (stats.end_ts > stats.start_ts) {
    progress = static_cast<float>(stats.current_ts - stats.start_ts) /
               static_cast<float>(stats.end_ts - stats.start_ts);
  }
  ImGui::Spacing();
  ImGui::ProgressBar(progress, ImVec2(-1, 15), "");
  ImGui::EndChild();
}

void Dashboard::RenderHeader(const MarketSnapshot &snapshot,
                             ReplayController &controller) {
  ImGui::BeginChild("Header", ImVec2(0, HEADER_HEIGHT), true);
  ImGui::Columns(4, "HeaderColumns", false);
  ImGui::SetColumnWidth(0, 250);
  ImGui::SetColumnWidth(1, ImGui::GetWindowWidth() - 550);
  ImGui::SetColumnWidth(2, 200);
  ImGui::SetColumnWidth(3, 100);

  auto instruments = controller.GetAvailableInstruments();
  uint32_t current_id = controller.GetFocusInstrument();

  std::string current_label = "Select Symbol";
  if (instruments.count(current_id)) {
    current_label =
        instruments[current_id] + " (" + std::to_string(current_id) + ")";
  }

  ImGui::SetNextItemWidth(200);
  if (ImGui::BeginCombo("##SymbolSelector", current_label.c_str())) {
    for (const auto &[id, sym] : instruments) {
      bool is_selected = (id == current_id);
      std::string label = sym + " (" + std::to_string(id) + ")";
      if (ImGui::Selectable(label.c_str(), is_selected)) {
        controller.SetFocusInstrument(id);
      }
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::NextColumn();

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
  TextCentered(snapshot.timestamp.c_str(), ImGui::GetColumnWidth());
  ImGui::PopStyleColor();
  ImGui::NextColumn();

  ImGui::Text("Msgs: %lu", snapshot.msg_count);
  ImGui::NextColumn();

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
  if (ImGui::Button(" Quit ", ImVec2(-1, 0))) {
    m_request_quit = true;
  }
  ImGui::PopStyleColor();

  ImGui::Columns(1);
  ImGui::EndChild();
}

void Dashboard::RenderSpreadGraph(ReplayController &controller, float width) {
  ImGui::BeginChild("SpreadGraph", ImVec2(width, SPREAD_GRAPH_HEIGHT), true);

  auto history = controller.GetSpreadHistory();

  if (history.empty()) {
    ImGui::TextDisabled("Press Play to begin...");
    ImGui::EndChild();
    return;
  }

  // Rolling window: show last 30 seconds of market time
  constexpr double kWindowSecs = 30.0;
  const double t_latest = history.back().ts;
  const double t_min_window = t_latest - kWindowSecs;

  // Filter to only points in (or just before) the window for tight y-autofit
  std::vector<double> times, bids, asks;
  std::vector<double> fill_ts, fill_px;
  std::vector<double> cancel_ts, cancel_px;
  std::vector<double> add_ts, add_px;

  times.reserve(history.size());
  bids.reserve(history.size());
  asks.reserve(history.size());

  for (const auto &pt : history) {
    if (pt.ts < t_min_window - 1.0) continue;

    if (pt.bid > 0.0 && pt.ask > 0.0) {
      times.push_back(pt.ts);
      bids.push_back(pt.bid);
      asks.push_back(pt.ask);
    }

    // Filter events for markers
    if (pt.action == 'F' || pt.action == 'T') {
      fill_ts.push_back(pt.ts);
      // If it was a bid fill, it should be at the OLD bid
      // If it was an ask fill, it should be at the OLD ask
      fill_px.push_back(pt.side == 'B' ? pt.pre_bid : pt.pre_ask);
    } else if (pt.action == 'C') {
      cancel_ts.push_back(pt.ts);
      cancel_px.push_back(pt.side == 'B' ? pt.pre_bid : pt.pre_ask);
    } else if (pt.action == 'A') {
      add_ts.push_back(pt.ts);
      add_px.push_back(pt.side == 'B' ? pt.bid : pt.ask); // Add is on NEW state
    }
  }

  SessionStats stats = controller.GetSessionStats();
  double current_ts_s = static_cast<double>(stats.current_ts) / 1e9;

  // Calculate local price bounds for framing (ignore 0.0)
  double min_p = 1e18, max_p = -1e18;
  for (double b : bids) if (b > 0) min_p = std::min(min_p, b);
  for (double a : asks) if (a > 0) max_p = std::max(max_p, a);

  // Also check event prices to ensure they fit
  for (double p : fill_px) if (p > 0) { min_p = std::min(min_p, p); max_p = std::max(max_p, p); }
  for (double p : cancel_px) if (p > 0) { min_p = std::min(min_p, p); max_p = std::max(max_p, p); }
  for (double p : add_px) if (p > 0) { min_p = std::min(min_p, p); max_p = std::max(max_p, p); }

  if (min_p > max_p) { min_p = 0; max_p = 100; } // Fallback

  // Visual Controls
  ImGui::Checkbox("Follow", &m_spread_follow);
  if (!m_spread_follow) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("Y-Range ($)", &m_y_range, 0.1f, 100.0f, "%.1f");
  }
  ImGui::SameLine();
  ImGui::Checkbox("Fills", &m_show_fills);
  ImGui::SameLine();
  ImGui::Checkbox("Cancels", &m_show_cancels);
  ImGui::SameLine();
  ImGui::Checkbox("Adds", &m_show_adds);

  const ImGuiCond x_cond = m_spread_follow ? ImGuiCond_Always : ImGuiCond_Once;
  const ImGuiCond y_cond = m_spread_follow ? ImGuiCond_Always : ImGuiCond_Once;

  ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(6, 6));
  const float plot_h = SPREAD_GRAPH_HEIGHT - 50.0f;

  if (ImPlot::BeginPlot("##SpreadChart", ImVec2(-1, plot_h), ImPlotFlags_NoMenus)) {
    ImPlot::SetupAxes("Time", "Price (USD)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    ImPlot::SetupAxisLimits(ImAxis_X1, t_min_window, t_latest + 0.5, x_cond);

    if (m_spread_follow) {
      double padding = (max_p - min_p) * 0.1;
      if (padding < 0.01) padding = 0.5;
      ImPlot::SetupAxisLimits(ImAxis_Y1, min_p - padding, max_p + padding, ImGuiCond_Always);
    } else if (!times.empty()) {
      double mid = (bids.back() + asks.back()) * 0.5;
      ImPlot::SetupAxisLimits(ImAxis_Y1, mid - m_y_range * 0.5, mid + m_y_range * 0.5, y_cond);
    }

    const int n = static_cast<int>(times.size());

    // Shading: Red above Ask, Green below Bid
    if (n > 0) {
      ImPlotRect limits = ImPlot::GetPlotLimits();
      double plot_min = limits.Y.Min;
      double plot_max = limits.Y.Max;

      std::vector<double> y_top(n, plot_max);
      std::vector<double> y_bottom(n, plot_min);

      ImPlot::SetNextFillStyle(ImVec4(0.9f, 0.25f, 0.25f, 0.15f));
      ImPlot::PlotShaded("##AskShade", times.data(), asks.data(), y_top.data(), n);

      ImPlot::SetNextFillStyle(ImVec4(0.15f, 0.85f, 0.3f, 0.15f));
      ImPlot::PlotShaded("##BidShade", times.data(), bids.data(), y_bottom.data(), n);
    }

    // Bid (green)
    ImPlot::SetNextLineStyle(ImVec4(0.15f, 0.85f, 0.3f, 1.0f), 1.5f);
    ImPlot::PlotLine("Bid", times.data(), bids.data(), n);

    // Ask (red)
    ImPlot::SetNextLineStyle(ImVec4(0.9f, 0.25f, 0.25f, 1.0f), 1.5f);
    ImPlot::PlotLine("Ask", times.data(), asks.data(), n);

    // Event markers
    if (m_show_fills && !fill_ts.empty()) {
      ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 4.0f,
                                 ImVec4(1.0f, 0.88f, 0.1f, 1.0f), 1.0f,
                                 ImVec4(1.0f, 0.88f, 0.1f, 1.0f));
      ImPlot::PlotScatter("Fills", fill_ts.data(), fill_px.data(),
                          (int)fill_ts.size());
    }
    if (m_show_cancels && !cancel_ts.empty()) {
      ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 3.0f,
                                 ImVec4(0.9f, 0.2f, 0.2f, 0.8f), 1.0f,
                                 ImVec4(0.9f, 0.2f, 0.2f, 0.4f));
      ImPlot::PlotScatter("Cancels", cancel_ts.data(), cancel_px.data(),
                          (int)cancel_ts.size());
    }
    if (m_show_adds && !add_ts.empty()) {
      ImPlot::SetNextMarkerStyle(ImPlotMarker_Up, 3.0f,
                                 ImVec4(0.2f, 0.9f, 0.2f, 0.8f), 1.0f,
                                 ImVec4(0.2f, 0.9f, 0.2f, 0.4f));
      ImPlot::PlotScatter("Adds", add_ts.data(), add_px.data(),
                          (int)add_ts.size());
    }

    // Current-position cursor
    if (current_ts_s > 0.0) {
      double pos[] = {current_ts_s};
      ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 1.0f, 0.55f), 1.5f);
      ImPlot::PlotInfLines("##cursor", pos, 1);
    }

    ImPlot::EndPlot();
  }

  ImPlot::PopStyleVar();
  ImGui::EndChild();
}

void Dashboard::RenderOrderEventList(ReplayController &controller,
                                     float width) {
  ImGui::BeginChild("OrderEvents", ImVec2(width, SPREAD_GRAPH_HEIGHT), true);
  ImGui::Text("Recent Order Events");
  ImGui::Separator();

  auto events = controller.GetOrderEvents();
  if (events.empty()) {
    ImGui::TextDisabled("No events captured yet.");
    ImGui::EndChild();
    return;
  }

  static ImGuiTableFlags flags =
      ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
      ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
      ImGuiTableFlags_Hideable;

  if (ImGui::BeginTable("EventTable", 4, flags, ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    ImGui::TableSetupColumn("Act", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 45.0f);
    ImGui::TableHeadersRow();

    // Show events latest-first
    for (int i = static_cast<int>(events.size()) - 1; i >= 0; --i) {
      const auto &e = events[i];
      ImGui::TableNextRow();

      // Time (HH:MM:SS)
      ImGui::TableSetColumnIndex(0);
      std::time_t secs = static_cast<std::time_t>(e.ts);
      std::tm *tm_info = std::gmtime(&secs);
      if (tm_info) {
        ImGui::Text("%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min,
                    tm_info->tm_sec);
      } else {
        ImGui::Text("??:??:??");
      }

      // Action / Side
      ImGui::TableSetColumnIndex(1);
      ImVec4 color = ImVec4(1, 1, 1, 1);
      if (e.side == 'B')
        color = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);
      else if (e.side == 'S')
        color = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);

      char act_buf[8];
      std::snprintf(act_buf, sizeof(act_buf), "%c/%c", e.action, e.side);
      ImGui::TextColored(color, "%s", act_buf);

      // Price
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%.2f", e.price);

      // Size
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%u", e.size);
    }
    ImGui::EndTable();
  }

  ImGui::EndChild();
}

void Dashboard::RenderOrderBookDepth(const MarketSnapshot &snapshot) {
  ImGui::BeginChild("Order book depth", ImVec2(0, ORDERBOOK_DEPTH_HEIGHT),
                    true);
  const std::vector<float> &left_data =
      m_use_cumulative ? snapshot.bid_volumes_cum : snapshot.bid_volumes;
  const std::vector<float> &right_data =
      m_use_cumulative ? snapshot.ask_volumes_cum : snapshot.ask_volumes;

  if (left_data.empty() || right_data.empty()) {
    ImGui::Text("Waiting for market depth data... (Press Play or Jump)");
  } else {
    float max_vol = 0.1f;
    for (float v : left_data)
      if (v > max_vol) max_vol = v;
    for (float v : right_data)
      if (v > max_vol) max_vol = v;

    float available_width = ImGui::GetContentRegionAvail().x;
    float center_width = CHART_CENTER_WIDTH;
    float plot_width = (available_width - center_width) * 0.5f;
    float plot_height = ImGui::GetContentRegionAvail().y - 45.0f;

    std::vector<float> rev_bids = left_data;
    std::reverse(rev_bids.begin(), rev_bids.end());

    ImGui::Checkbox("Cumulative Volume (Mountain)", &m_use_cumulative);
    ImGui::BeginGroup();
    TextCentered("BIDS (Liquidity)", plot_width);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                          ImVec4(0.0f, 0.5f, 0.0f, 1.0f));
    ImGui::PlotHistogram("##Bids", rev_bids.data(), (int)rev_bids.size(), 0,
                         nullptr, 0.0f, max_vol * 1.1f,
                         ImVec2(plot_width, plot_height));
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(center_width, plot_height * 0.4f));
    TextCentered("Last Price", center_width);
    ImGui::SetWindowFontScale(1.5f);
    if (snapshot.last_price > 0)
      TextCentered(std::to_string(snapshot.last_price).substr(0, 10).c_str(),
                   center_width);
    else
      TextCentered("N/A", center_width);
    ImGui::SetWindowFontScale(1.0f);

    ImGui::Dummy(ImVec2(center_width, 20));
    TextCentered("Imbalance", center_width);
    if (snapshot.imbalance > 0.05f)
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
    else if (snapshot.imbalance < -0.05f)
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
    else
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));

    TextCentered(std::to_string(snapshot.imbalance).substr(0, 6).c_str(),
                 center_width);
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    TextCentered("ASKS (Liquidity)", plot_width);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                          ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
    ImGui::PlotHistogram("##Asks", right_data.data(), (int)right_data.size(), 0,
                         nullptr, 0.0f, max_vol * 1.1f,
                         ImVec2(plot_width, plot_height));
    ImGui::PopStyleColor();
    ImGui::EndGroup();
  }
  ImGui::EndChild();
}
