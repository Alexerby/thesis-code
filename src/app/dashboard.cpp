#include "app/dashboard.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "app/replay_controller.hpp"
#include "imgui.h"
#include "implot.h"

namespace {  // make anonymous
using namespace std::chrono;

constexpr uint64_t NANOS_1S = 1'000'000'000ULL;

constexpr float HEADER_HEIGHT = 50.0f;
constexpr float CONTROLS_HEIGHT = 110.0f;
constexpr float SPREAD_GRAPH_HEIGHT = 480.0f;
constexpr float ORDERBOOK_DEPTH_HEIGHT = 160.0f;

constexpr float CHART_CENTER_WIDTH = 120.0f;

void TextCentered(const char *text, float width) {
  float textWidth = ImGui::CalcTextSize(text).x;
  if (width > textWidth)
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - textWidth) * 0.5f);
  ImGui::Text("%s", text);
}

// Convert HH:MM:SS[.ffffff] (Eastern Time) to UTC nanoseconds since epoch.
// The date is inferred from reference_ts (also UTC nanoseconds).
uint64_t TimeStringToNanos(const std::string &time_str, uint64_t reference_ts) {
  int h = 0, m = 0, s = 0;
  if (std::sscanf(time_str.c_str(), "%d:%d:%d", &h, &m, &s) != 3) return 0;

  uint64_t frac_ns = 0;
  auto dot = time_str.find('.');
  if (dot != std::string::npos) {
    std::string frac = time_str.substr(dot + 1);
    frac.resize(9, '0');
    frac_ns = static_cast<uint64_t>(std::stoull(frac));
  }

  // Use the UTC calendar date from reference_ts as the trading day.
  // For US equities, market hours (13:30–20:00 UTC) are within one UTC day,
  // so the UTC date always matches the ET trading date.
  auto ref_day_utc = floor<days>(system_clock::time_point{nanoseconds(reference_ts)});
  local_time<seconds> target_local{
      local_days{ref_day_utc.time_since_epoch()} + hours(h) + minutes(m) + seconds(s)};
  auto target_utc = zoned_time{"America/New_York", target_local}.get_sys_time();

  return static_cast<uint64_t>(
      duration_cast<nanoseconds>(target_utc.time_since_epoch()).count()) + frac_ns;
}

// Decompose a double epoch-seconds value into ET wall-clock components.
struct ETTime { int h, m, s, ms; };
static ETTime ToET(double epoch_seconds) {
  using namespace std::chrono;
  auto tp = system_clock::time_point{
      duration_cast<system_clock::duration>(duration<double>(epoch_seconds))};
  auto zt  = zoned_time{"America/New_York", tp};
  auto lt  = zt.get_local_time();
  auto sec = floor<seconds>(lt);
  auto day = floor<days>(lt);
  hh_mm_ss hms{sec - day};
  int ms = (int)duration_cast<milliseconds>(lt - sec).count();
  return {(int)hms.hours().count(), (int)hms.minutes().count(),
          (int)hms.seconds().count(), ms};
}

static int TimeAxisFormatter(double value, char *buff, int size, void *) {
  auto t = ToET(value);
  return std::snprintf(buff, size, "%02d:%02d:%02d.%03d", t.h, t.m, t.s, t.ms);
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

  // Bottom section: spread graph (Main) + sidebar (Side-by-side)
  {
    const float avail_x = ImGui::GetContentRegionAvail().x;
    const float avail_y = ImGui::GetContentRegionAvail().y;
    const float sidebar_w = 400.0f;
    const float graph_w = avail_x - sidebar_w - ImGui::GetStyle().ItemSpacing.x;
    
    // Main Area: Spread Graph
    RenderSpreadGraph(controller, graph_w, avail_y);
    
    ImGui::SameLine();
    
    // Sidebar Area: Events (Top) and Book (Bottom)
    ImGui::BeginGroup();
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float sidebar_h_top = avail_y * 0.65f;
    const float sidebar_h_bottom = avail_y - sidebar_h_top - spacing;
    
    RenderOrderEventList(controller, sidebar_w, sidebar_h_top);
    RenderOrderBookDepth(snapshot, sidebar_w, sidebar_h_bottom);
    ImGui::EndGroup();
  }

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

  // Jump to Time
  ImGui::SameLine(0, 20);
  static char jump_time[24] = "14:30:00.000000";
  ImGui::SetNextItemWidth(140);
  ImGui::InputText("##JumpTime", jump_time, IM_ARRAYSIZE(jump_time));
  ImGui::SameLine();
  if (ImGui::Button("Jump to Time")) {
    uint64_t target = TimeStringToNanos(jump_time, stats.start_ts);
    if (target > 0) controller.SeekToTime(target);
  }

  // Speed Multiplier
  ImGui::SameLine(0, 40);
  ImGui::SetNextItemWidth(150);
  float speed = controller.GetSpeedMultiplier();
  if (ImGui::SliderFloat("Speed", &speed, 0.1f, 10.0f, "%.1fx"))
    controller.SetSpeedMultiplier(speed);

  // Range playback
  static char range_start[24] = "14:26:10.000000";
  static char range_end[24]   = "14:28:10.000000";
  ImGui::SetNextItemWidth(140);
  ImGui::InputText("##RangeStart", range_start, IM_ARRAYSIZE(range_start));
  ImGui::SameLine(0, 4);
  ImGui::TextDisabled("→");
  ImGui::SameLine(0, 4);
  ImGui::SetNextItemWidth(140);
  ImGui::InputText("##RangeEnd", range_end, IM_ARRAYSIZE(range_end));
  ImGui::SameLine(0, 10);
  if (ImGui::Button("Play Range")) {
    uint64_t t0 = TimeStringToNanos(range_start, stats.start_ts);
    uint64_t t1 = TimeStringToNanos(range_end,   stats.start_ts);
    if (t0 > 0 && t1 > t0) {
      controller.PlayRange(t0, t1);
      float range_secs = static_cast<float>((t1 - t0) / 1e9);
      m_window_secs  = 120.0f + range_secs + 120.0f + 10.0f;
      m_spread_follow = true;
    }
  }

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

void Dashboard::RenderSpreadGraph(ReplayController &controller, float width, float height) {
  ImGui::BeginChild("SpreadGraph", ImVec2(width, height), true);

  auto history = controller.GetSpreadHistory();

  if (history.empty()) {
    ImGui::TextDisabled("Press Play to begin...");
    ImGui::EndChild();
    return;
  }

  // Rolling window: show last 30 seconds of market time
  const double t_latest = history.back().ts;
  const double t_min_window = t_latest - m_window_secs;

  // Filter to only points in (or just before) the window for tight y-autofit
  std::vector<double> times, bids, asks;
  std::vector<double> fill_ts, fill_px;
  std::vector<double> cancel_ts, cancel_px;
  std::vector<double> add_ts, add_px;

  times.reserve(history.size());
  bids.reserve(history.size());
  asks.reserve(history.size());

  // Efficient Binary Search for the window start: O(log N) instead of O(N)
  auto it_start = std::lower_bound(history.begin(), history.end(), t_min_window - 1.0,
                                   [](const SpreadPoint &pt, double val) { return pt.ts < val; });

  for (auto it = it_start; it != history.end(); ++it) {
    const auto &pt = *it;

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

  // Y bounds driven only by BBO lines — event markers can be at deep prices
  // (e.g. $5.00 asks) that would blow out the scale if included here.
  double min_p = 1e18, max_p = -1e18;
  for (double b : bids) if (b > 0) min_p = std::min(min_p, b);
  for (double a : asks) if (a > 0) max_p = std::max(max_p, a);

  if (min_p > max_p) { min_p = 0; max_p = 100; } // Fallback

  // Visual Controls
  ImGui::Checkbox("Follow", &m_spread_follow);
  if (!m_spread_follow) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
  }
  ImGui::SameLine(0, 20);
  ImGui::SetNextItemWidth(120);

  ImGui::SliderFloat("Window (s)", &m_window_secs, 10.0f, 600.0f, "%.0fs");
  ImGui::SameLine();
  ImGui::Checkbox("Fills", &m_show_fills);
  ImGui::SameLine();
  ImGui::Checkbox("Cancels", &m_show_cancels);
  ImGui::SameLine();
  ImGui::Checkbox("Adds", &m_show_adds);

  const ImGuiCond x_cond = m_spread_follow ? ImGuiCond_Always : ImGuiCond_Once;
  const ImGuiCond y_cond = m_spread_follow ? ImGuiCond_Always : ImGuiCond_Once;

  ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(6, 6));
  const float plot_h = height - 50.0f;

  if (ImPlot::BeginPlot("##SpreadChart", ImVec2(-1, plot_h), ImPlotFlags_NoMenus)) {
    ImPlot::SetupAxes("Time", "Price (USD)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisFormat(ImAxis_X1, TimeAxisFormatter);
    ImPlot::SetupAxisLimits(ImAxis_X1, t_min_window, t_latest + 0.5, x_cond);

    if (m_spread_follow) {
      double padding = (max_p - min_p) * 0.1;
      if (padding < 1e-6) padding = (max_p > 0 ? max_p : 1.0) * 0.001;
      ImPlot::SetupAxisLimits(ImAxis_Y1, min_p - padding, max_p + padding, ImGuiCond_Always);
    } else if (!times.empty()) {
      double mid = (bids.back() + asks.back()) * 0.5;
      ImPlot::SetupAxisLimits(ImAxis_Y1, mid - m_y_range * 0.5, mid + m_y_range * 0.5, y_cond);
    }

    const int n = static_cast<int>(times.size());

    // Shading: Red above Ask, Green below Bid
    ImPlotRect limits = ImPlot::GetPlotLimits();
    double plot_min = limits.Y.Min;
    double plot_max = limits.Y.Max;

    if (n > 0) {
      std::vector<double> y_top(n, plot_max);
      std::vector<double> y_bottom(n, plot_min);

      ImPlot::SetNextFillStyle(ImVec4(0.9f, 0.25f, 0.25f, 0.15f));
      ImPlot::PlotShaded("##AskShade", times.data(), asks.data(), y_top.data(), n);

      ImPlot::SetNextFillStyle(ImVec4(0.15f, 0.85f, 0.3f, 0.15f));
      ImPlot::PlotShaded("##BidShade", times.data(), bids.data(), y_bottom.data(), n);
    }

    // Range highlight band
    auto hl = controller.GetRangeHighlight();
    if (hl.active) {
      double xs[]    = {hl.start_s, hl.end_s};
      double y_bot[] = {plot_min, plot_min};
      double y_top[] = {plot_max, plot_max};
      ImPlot::SetNextFillStyle(ImVec4(1.0f, 0.85f, 0.0f, 0.10f));
      ImPlot::PlotShaded("##RangeHL", xs, y_bot, y_top, 2);
      ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.85f, 0.0f, 0.60f), 1.5f);
      ImPlot::PlotInfLines("##RangeStart", &hl.start_s, 1);
      ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.85f, 0.0f, 0.60f), 1.5f);
      ImPlot::PlotInfLines("##RangeEnd", &hl.end_s, 1);
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
                                     float width, float height) {
  ImGui::BeginChild("OrderEvents", ImVec2(width, height), true);
  ImGui::Text("Recent Order Events");
  ImGui::Separator();

  auto events = controller.GetOrderEvents();
  if (events.empty()) {
    ImGui::TextDisabled("No events captured yet.");
    ImGui::EndChild();
    return;
  }

  // Filter toggles: A=Add, C=Cancel, F=Fill, T=Trade, X=Clear
  static bool show_add    = true;
  static bool show_cancel = true;
  static bool show_fill   = true;
  static bool show_trade  = true;
  static bool show_clear  = false;

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, 2));
  ImGui::Checkbox("A", &show_add);    ImGui::SameLine();
  ImGui::Checkbox("C", &show_cancel); ImGui::SameLine();
  ImGui::Checkbox("F", &show_fill);   ImGui::SameLine();
  ImGui::Checkbox("T", &show_trade);  ImGui::SameLine();
  ImGui::Checkbox("X", &show_clear);
  ImGui::PopStyleVar();
  ImGui::Separator();

  // Build filtered index list (reverse order: latest first)
  std::vector<int> filtered;
  filtered.reserve(events.size());
  for (int i = static_cast<int>(events.size()) - 1; i >= 0; --i) {
    char a = events[i].action;
    if (a == 'A' && !show_add)    continue;
    if (a == 'C' && !show_cancel) continue;
    if ((a == 'F') && !show_fill) continue;
    if (a == 'T' && !show_trade)  continue;
    if (a == 'X' && !show_clear)  continue;
    filtered.push_back(i);
  }

  static ImGuiTableFlags flags =
      ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
      ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
      ImGuiTableFlags_Hideable;

  if (ImGui::BeginTable("EventTable", 4, flags, ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed,   115.0f);
    ImGui::TableSetupColumn("Act",   ImGuiTableColumnFlags_WidthFixed,    40.0f);
    ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Size",  ImGuiTableColumnFlags_WidthFixed,    55.0f);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filtered.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const auto &e = events[filtered[i]];
        ImGui::TableNextRow();

        // Time (HH:MM:SS.ffffff ET)
        ImGui::TableSetColumnIndex(0);
        {
          using namespace std::chrono;
          auto tp  = system_clock::time_point{
              duration_cast<system_clock::duration>(duration<double>(e.ts))};
          auto zt  = zoned_time{"America/New_York", tp};
          auto lt  = zt.get_local_time();
          auto sec = floor<seconds>(lt);
          auto day = floor<days>(lt);
          hh_mm_ss hms{sec - day};
          int us = (int)duration_cast<microseconds>(lt - sec).count();
          ImGui::Text("%02d:%02d:%02d.%06d",
                      (int)hms.hours().count(), (int)hms.minutes().count(),
                      (int)hms.seconds().count(), us);
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
        ImGui::Text("%.4f", e.price);

        // Size
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%u", e.size);
      }
    }
    ImGui::EndTable();
  }

  ImGui::EndChild();
}

void Dashboard::RenderOrderBookDepth(const MarketSnapshot &snapshot, float width, float height) {
  ImGui::BeginChild("Order book depth", ImVec2(width, height), true);

  if (snapshot.bid_volumes.empty() || snapshot.ask_volumes.empty()) {
    ImGui::TextDisabled("Waiting for depth data...");
    ImGui::EndChild();
    return;
  }

  // Stats header
  ImGui::Columns(2, "BookStats", false);
  ImGui::Text("Last Px");
  ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.4f", snapshot.last_price);
  ImGui::NextColumn();
  ImGui::Checkbox("Cumulative", &m_use_cumulative);
  ImGui::Columns(1);

  ImVec4 imb_color = ImVec4(1, 1, 1, 1);
  if (snapshot.imbalance > 0.1f) imb_color = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);
  else if (snapshot.imbalance < -0.1f) imb_color = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
  ImGui::Text("Imbalance:"); ImGui::SameLine();
  ImGui::TextColored(imb_color, "%.3f", snapshot.imbalance);
  ImGui::Separator();

  // LOB ladder table: Orders | Size | Price | Price | Size | Orders
  static ImGuiTableFlags tflags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("LOBTable", 6, tflags, ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Ord##B",  ImGuiTableColumnFlags_WidthFixed, 35.0f);
    ImGui::TableSetupColumn("Size##B", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Bid",     ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Ask",     ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Size##A", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Ord##A",  ImGuiTableColumnFlags_WidthFixed, 35.0f);
    ImGui::TableHeadersRow();

    const int n_levels = 5;
    const auto &bid_vol = m_use_cumulative ? snapshot.bid_volumes_cum : snapshot.bid_volumes;
    const auto &ask_vol = m_use_cumulative ? snapshot.ask_volumes_cum : snapshot.ask_volumes;

    // Ask levels (worst to best, so L5 first)
    for (int i = n_levels - 1; i >= 0; --i) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("-");
      ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("-");
      ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("-");

      ImGui::TableSetColumnIndex(3);
      if (i < (int)snapshot.ask_prices.size() && snapshot.ask_prices[i] > 0.0)
        ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "%.4f", snapshot.ask_prices[i]);
      else
        ImGui::TextDisabled("---");

      ImGui::TableSetColumnIndex(4);
      if (i < (int)ask_vol.size())
        ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "%.0f", ask_vol[i]);
      else
        ImGui::TextDisabled("-");

      ImGui::TableSetColumnIndex(5);
      if (i < (int)snapshot.ask_counts.size() && snapshot.ask_counts[i] > 0)
        ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "%u", snapshot.ask_counts[i]);
      else
        ImGui::TextDisabled("-");
    }

    // Separator row
    ImGui::TableNextRow();
    for (int c = 0; c < 6; ++c) { ImGui::TableSetColumnIndex(c); ImGui::Separator(); }

    // Bid levels (best to worst, L1 first)
    for (int i = 0; i < n_levels; ++i) {
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      if (i < (int)snapshot.bid_counts.size() && snapshot.bid_counts[i] > 0)
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%u", snapshot.bid_counts[i]);
      else
        ImGui::TextDisabled("-");

      ImGui::TableSetColumnIndex(1);
      if (i < (int)bid_vol.size())
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%.0f", bid_vol[i]);
      else
        ImGui::TextDisabled("-");

      ImGui::TableSetColumnIndex(2);
      if (i < (int)snapshot.bid_prices.size() && snapshot.bid_prices[i] > 0.0)
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%.4f", snapshot.bid_prices[i]);
      else
        ImGui::TextDisabled("---");

      ImGui::TableSetColumnIndex(3); ImGui::TextDisabled("-");
      ImGui::TableSetColumnIndex(4); ImGui::TextDisabled("-");
      ImGui::TableSetColumnIndex(5); ImGui::TextDisabled("-");
    }

    ImGui::EndTable();
  }

  ImGui::EndChild();
}
