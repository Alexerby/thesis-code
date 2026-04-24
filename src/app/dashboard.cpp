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

constexpr float HEADER_HEIGHT = 70.0f;
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

// Convert YYYY-MM-DD HH:MM:SS[.ffffff] (Eastern Time) to UTC nanoseconds since epoch.
uint64_t TimeStringToNanos(const std::string &time_str, uint64_t reference_ts) {
  int y = 0, month = 0, d = 0, h = 0, m = 0, s = 0;
  bool has_date = false;

  if (std::sscanf(time_str.c_str(), "%d-%d-%d %d:%d:%d", &y, &month, &d, &h, &m,
                  &s) == 6) {
    has_date = true;
  } else if (std::sscanf(time_str.c_str(), "%d:%d:%d", &h, &m, &s) != 3) {
    return 0;
  }

  uint64_t frac_ns = 0;
  auto dot = time_str.find('.');
  if (dot != std::string::npos) {
    std::string frac = time_str.substr(dot + 1);
    frac.resize(9, '0');
    try {
      frac_ns = static_cast<uint64_t>(std::stoull(frac));
    } catch (...) {
    }
  }

  sys_time<nanoseconds> target_utc;
  if (has_date) {
    local_days ld{year{y} / month / d};
    local_time<seconds> target_local{ld + hours(h) + minutes(m) + seconds(s)};
    target_utc = zoned_time{"America/New_York", target_local}.get_sys_time();
  } else {
    auto ref_day_utc =
        floor<days>(system_clock::time_point{nanoseconds(reference_ts)});
    local_time<seconds> target_local{local_days{ref_day_utc.time_since_epoch()} +
                                     hours(h) + minutes(m) + seconds(s)};
    target_utc = zoned_time{"America/New_York", target_local}.get_sys_time();
  }

  return static_cast<uint64_t>(
             duration_cast<nanoseconds>(target_utc.time_since_epoch()).count()) +
         frac_ns;
}

// Decompose a double epoch-seconds value into ET wall-clock components.
struct ETTime {
  int y, mon, d, h, m, s, ms;
};
static ETTime ToET(double epoch_seconds) {
  using namespace std::chrono;
  auto tp = system_clock::time_point{duration_cast<system_clock::duration>(
      nanoseconds(static_cast<int64_t>(epoch_seconds * 1e9)))};
  auto zt = zoned_time{"America/New_York", tp};
  auto lt = zt.get_local_time();
  auto day_tp = floor<days>(lt);
  year_month_day ymd{day_tp};
  hh_mm_ss hms{lt - day_tp};
  int ms = (int)duration_cast<milliseconds>(lt - floor<seconds>(lt)).count();
  return {int(ymd.year()), int(unsigned(ymd.month())), int(unsigned(ymd.day())),
          (int)hms.hours().count(), (int)hms.minutes().count(),
          (int)hms.seconds().count(), ms};
}

static int TimeAxisFormatter(double value, char *buff, int size, void *) {
  auto t = ToET(value);
  return std::snprintf(buff, size, "%04d-%02d-%02d\n%02d:%02d:%02d", t.y, t.mon,
                       t.d, t.h, t.m, t.s);
}
}  // namespace

Dashboard::Dashboard() {
  std::snprintf(m_jump_time, sizeof(m_jump_time), "2023-05-30 09:30:00");
  std::snprintf(m_range_start, sizeof(m_range_start), "2023-05-30 09:30:00");
  std::snprintf(m_range_end, sizeof(m_range_end), "2023-05-30 09:35:00");
}
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

  if (!m_times_initialized && stats.start_ts > 0) {
    auto t0 = ToET(static_cast<double>(stats.start_ts) / 1e9);
    auto t1 = ToET(static_cast<double>(stats.end_ts)   / 1e9);
    std::snprintf(m_jump_time,   sizeof(m_jump_time),
                  "%04d-%02d-%02d %02d:%02d:%02d", t0.y, t0.mon, t0.d, t0.h, t0.m, t0.s);
    std::snprintf(m_range_start, sizeof(m_range_start),
                  "%04d-%02d-%02d %02d:%02d:%02d", t0.y, t0.mon, t0.d, t0.h, t0.m, t0.s);
    std::snprintf(m_range_end,   sizeof(m_range_end),
                  "%04d-%02d-%02d %02d:%02d:%02d", t1.y, t1.mon, t1.d, t1.h, t1.m, t1.s);
    m_times_initialized = true;
  }

  if (state == PlaybackState::Paused) {
    if (ImGui::Button(" Play "))
      controller.SetPlaybackState(PlaybackState::Playing);
    ImGui::SameLine();
  } else {
    if (ImGui::Button(" Pause "))
      controller.SetPlaybackState(PlaybackState::Paused);
  }

  // Jump to Time
  ImGui::SameLine(0, 20);
  ImGui::SetNextItemWidth(170);
  ImGui::InputText("##JumpTime", m_jump_time, IM_ARRAYSIZE(m_jump_time));
  ImGui::SameLine();
  if (ImGui::Button("Jump to Time")) {
    uint64_t anchor = stats.current_ts ? stats.current_ts : stats.start_ts;
    uint64_t target = TimeStringToNanos(m_jump_time, anchor);
    if (target > 0) controller.SeekToTime(target);
  }

  // Speed Multiplier
  ImGui::SameLine(0, 40);
  ImGui::SetNextItemWidth(120);
  float speed = controller.GetSpeedMultiplier();
  if (ImGui::SliderFloat("Speed", &speed, 0.1f, 20.0f, "%.1fx"))
    controller.SetSpeedMultiplier(speed);

  // Range playback
  ImGui::SameLine(0, 20);
  ImGui::SetNextItemWidth(170);
  ImGui::InputText("##RangeStart", m_range_start, IM_ARRAYSIZE(m_range_start));
  ImGui::SameLine(0, 4);
  ImGui::TextDisabled("→");
  ImGui::SameLine(0, 4);
  ImGui::SetNextItemWidth(170);
  ImGui::InputText("##RangeEnd", m_range_end, IM_ARRAYSIZE(m_range_end));
  ImGui::SameLine(0, 10);
  ImGui::SameLine(0, 8);
  ImGui::SetNextItemWidth(60);
  ImGui::InputInt("##Pad", &m_range_context_secs, 0, 0);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Context seconds on each side of the range");
  ImGui::SameLine(0, 4);
  ImGui::TextDisabled("s");
  ImGui::SameLine(0, 10);
  if (ImGui::Button("Play Range")) {
    uint64_t anchor = stats.current_ts ? stats.current_ts : stats.start_ts;
    uint64_t t0 = TimeStringToNanos(m_range_start, anchor);
    uint64_t t1 = TimeStringToNanos(m_range_end,   anchor);
    if (t0 > 0 && t1 > t0) {
      const uint64_t context_ns = static_cast<uint64_t>(m_range_context_secs) * 1'000'000'000ULL;
      controller.PlayRange(t0, t1, context_ns);
      // Pin the chart to [t0 - pad, t1 + pad] — view does not scroll
      m_reset_x_min   = static_cast<double>(t0) / 1e9 - m_range_context_secs;
      m_reset_x_max   = static_cast<double>(t1) / 1e9 + m_range_context_secs;
      m_reset_view    = true;
      m_spread_follow = false;
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
  ImGui::SetColumnWidth(0, 270);
  ImGui::SetColumnWidth(1, ImGui::GetWindowWidth() - 570);
  ImGui::SetColumnWidth(2, 200);
  ImGui::SetColumnWidth(3, 100);

  const auto &tickers = controller.GetAvailableTickers();
  std::string focus = controller.GetFocusTicker();
  if (focus.empty() && !tickers.empty()) focus = tickers[0];

  ImGui::SetNextItemWidth(150);
  if (ImGui::BeginCombo("##TickerSelector", focus.c_str())) {
    for (const auto &t : tickers) {
      bool is_selected = (t == focus);
      if (ImGui::Selectable(t.c_str(), is_selected))
        controller.SetFocusTicker(t);
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  const auto &meta = controller.GetMetadata();
  auto t0 = ToET(static_cast<double>(meta.start.time_since_epoch().count()) / 1e9);
  auto t1 = ToET(static_cast<double>(meta.end.time_since_epoch().count()) / 1e9);

  ImGui::Text("Start: %04d-%02d-%02d %02d:%02d:%02d ET", t0.y, t0.mon, t0.d, t0.h, t0.m, t0.s);
  ImGui::Text("End:   %04d-%02d-%02d %02d:%02d:%02d ET", t1.y, t1.mon, t1.d, t1.h, t1.m, t1.s);

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

  const double t_latest = history.back().ts;

  // Data load window: in follow mode use rolling window; in free mode load from
  // last-known view start so the visible range is always fully populated.
  double t_min_load;
  if (m_spread_follow) {
    t_min_load = t_latest - m_window_secs - 1.0;
  } else if (m_view_x_min > 0.0) {
    t_min_load = m_view_x_min - 1.0;
  } else {
    t_min_load = history.front().ts - 1.0;
  }

  std::vector<double> times, bids, asks;
  std::vector<double> fill_ts, fill_px;
  std::vector<double> cancel_ts, cancel_px;
  std::vector<double> add_ts, add_px;
  std::vector<double> trade_buy_ts, trade_buy_px;
  std::vector<double> trade_sell_ts, trade_sell_px;

  times.reserve(4096);
  bids.reserve(4096);
  asks.reserve(4096);

  auto it_start = std::lower_bound(history.begin(), history.end(), t_min_load,
                                   [](const SpreadPoint &pt, double val) { return pt.ts < val; });
  for (auto it = it_start; it != history.end(); ++it) {
    const auto &pt = *it;
    if (pt.bid > 0.0 && pt.ask > 0.0) {
      times.push_back(pt.ts);
      bids.push_back(pt.bid);
      asks.push_back(pt.ask);
    }
    if      (pt.action == 'F') { fill_ts.push_back(pt.ts);   fill_px.push_back(pt.side == 'B' ? pt.pre_bid : pt.pre_ask); }
    else if (pt.action == 'C') { cancel_ts.push_back(pt.ts); cancel_px.push_back(pt.side == 'B' ? pt.pre_bid : pt.pre_ask); }
    else if (pt.action == 'A') { add_ts.push_back(pt.ts);    add_px.push_back(pt.side == 'B' ? pt.bid : pt.ask); }
    else if (pt.action == 'T') {
      double px = pt.side == 'B' ? pt.pre_bid : pt.pre_ask;
      if      (pt.side == 'S') { trade_buy_ts.push_back(pt.ts);  trade_buy_px.push_back(px); }
      else if (pt.side == 'B') { trade_sell_ts.push_back(pt.ts); trade_sell_px.push_back(px); }
    }
  }

  SessionStats stats = controller.GetSessionStats();
  const double current_ts_s    = static_cast<double>(stats.current_ts) / 1e9;
  const double session_start_s = static_cast<double>(stats.start_ts)   / 1e9;
  const double session_end_s   = static_cast<double>(stats.end_ts)     / 1e9;

  double min_p = 1e18, max_p = -1e18;
  for (double b : bids) if (b > 0) min_p = std::min(min_p, b);
  for (double a : asks) if (a > 0) max_p = std::max(max_p, a);
  if (min_p > max_p) { min_p = 0; max_p = 100; }

  // ── Controls row ────────────────────────────────────────────────────────
  ImGui::Checkbox("Follow", &m_spread_follow);
  ImGui::SameLine(0, 16);
  ImGui::Checkbox("Events", &m_show_order_events);
  ImGui::SameLine();
  ImGui::Checkbox("Trades", &m_show_trades);
  ImGui::SameLine(0, 20);

  // Helper: set a one-shot view reset
  auto zoom_to = [&](double x0, double x1) {
    m_reset_x_min   = x0;
    m_reset_x_max   = x1;
    m_reset_view    = true;
    m_spread_follow = false;
  };

  if (ImGui::Button("5s"))  { m_window_secs =   5.f; m_spread_follow = true; } ImGui::SameLine();
  if (ImGui::Button("30s")) { m_window_secs =  30.f; m_spread_follow = true; } ImGui::SameLine();
  if (ImGui::Button("2m"))  { m_window_secs = 120.f; m_spread_follow = true; } ImGui::SameLine();
  if (ImGui::Button("10m")) { m_window_secs = 600.f; m_spread_follow = true; } ImGui::SameLine();
  if (ImGui::Button("Full") && session_end_s > session_start_s)
    zoom_to(session_start_s, session_end_s);

  // LOD hint (uses previous-frame visible_secs so it's one frame delayed — imperceptible)
  if (m_visible_secs > 601.0 && (m_show_order_events || m_show_trades)) {
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("(zoom < 5m for event markers)");
  }

  ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(6, 6));
  const float plot_h = height - 60.0f;

  // NoInputs disables ImPlot's built-in pan/zoom/select so LMB is fully
  // owned by the custom rubber-band selection below.
  if (ImPlot::BeginPlot("##SpreadChart", ImVec2(-1, plot_h), ImPlotFlags_NoMenus | ImPlotFlags_NoInputs)) {
    ImPlot::SetupAxes("Time", "Price (USD)");
    ImPlot::SetupAxisFormat(ImAxis_X1, TimeAxisFormatter);

    // X-axis limits
    if (m_reset_view) {
      ImPlot::SetupAxisLimits(ImAxis_X1, m_reset_x_min, m_reset_x_max, ImGuiCond_Always);
      m_reset_view = false;
    } else if (m_spread_follow) {
      ImPlot::SetupAxisLimits(ImAxis_X1, t_latest - m_window_secs, t_latest + 0.5, ImGuiCond_Always);
    }
    // else: free mode — ImPlot retains its own limits from last frame

    // Y-axis: always auto-fit to visible BBO data
    if (min_p < max_p) {
      double pad = std::max((max_p - min_p) * 0.1, max_p * 0.0005);
      ImPlot::SetupAxisLimits(ImAxis_Y1, min_p - pad, max_p + pad,
                              m_spread_follow ? ImGuiCond_Always : ImGuiCond_Once);
    }

    // Read current limits — used for LOD, shading, and rubber-band drawing
    const ImPlotRect limits = ImPlot::GetPlotLimits();
    m_view_x_min   = limits.X.Min;
    m_view_x_max   = limits.X.Max;
    m_visible_secs = limits.X.Max - limits.X.Min;
    const double plot_min = limits.Y.Min;
    const double plot_max = limits.Y.Max;
    const int    n        = static_cast<int>(times.size());

    // Spread shading
    if (n > 0) {
      std::vector<double> y_top(n, plot_max), y_bot(n, plot_min);
      ImPlot::SetNextFillStyle(ImVec4(0.9f, 0.25f, 0.25f, 0.15f));
      ImPlot::PlotShaded("##AskShade", times.data(), asks.data(), y_top.data(), n);
      ImPlot::SetNextFillStyle(ImVec4(0.15f, 0.85f, 0.3f, 0.15f));
      ImPlot::PlotShaded("##BidShade", times.data(), bids.data(), y_bot.data(), n);
    }

    // Range highlight
    auto hl = controller.GetRangeHighlight();
    if (hl.active) {
      double xs[] = {hl.start_s, hl.end_s}, ry0[] = {plot_min, plot_min}, ry1[] = {plot_max, plot_max};
      ImPlot::SetNextFillStyle(ImVec4(1.0f, 0.85f, 0.0f, 0.10f));
      ImPlot::PlotShaded("##RangeHL", xs, ry0, ry1, 2);
      ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.85f, 0.0f, 0.60f), 1.5f);
      ImPlot::PlotInfLines("##RangeStart", &hl.start_s, 1);
      ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.85f, 0.0f, 0.60f), 1.5f);
      ImPlot::PlotInfLines("##RangeEnd", &hl.end_s, 1);
    }

    // BBO lines — always rendered at any zoom level
    ImPlot::SetNextLineStyle(ImVec4(0.15f, 0.85f, 0.3f, 1.0f), 1.5f);
    ImPlot::PlotLine("Bid", times.data(), bids.data(), n);
    ImPlot::SetNextLineStyle(ImVec4(0.9f, 0.25f, 0.25f, 1.0f), 1.5f);
    ImPlot::PlotLine("Ask", times.data(), asks.data(), n);

    // LOD: suppress scatter markers when the window is too wide to be useful
    if (m_visible_secs <= 601.0) {
      if (m_show_order_events && !fill_ts.empty()) {
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 4.0f, ImVec4(0.85f, 0.3f, 0.9f, 1.0f), 1.0f, ImVec4(0.85f, 0.3f, 0.9f, 1.0f));
        ImPlot::PlotScatter("Fills", fill_ts.data(), fill_px.data(), (int)fill_ts.size());
      }
      if (m_show_order_events && !cancel_ts.empty()) {
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 3.0f, ImVec4(0.9f, 0.4f, 0.75f, 0.8f), 1.0f, ImVec4(0.9f, 0.4f, 0.75f, 0.4f));
        ImPlot::PlotScatter("Cancels", cancel_ts.data(), cancel_px.data(), (int)cancel_ts.size());
      }
      if (m_show_order_events && !add_ts.empty()) {
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Up, 3.0f, ImVec4(0.7f, 0.2f, 0.9f, 0.8f), 1.0f, ImVec4(0.7f, 0.2f, 0.9f, 0.4f));
        ImPlot::PlotScatter("Adds", add_ts.data(), add_px.data(), (int)add_ts.size());
      }
      if (m_show_trades && !trade_buy_ts.empty()) {
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, ImVec4(0.15f, 0.85f, 0.3f, 1.0f), 1.0f, ImVec4(0.15f, 0.85f, 0.3f, 0.6f));
        ImPlot::PlotScatter("Trade (buy)", trade_buy_ts.data(), trade_buy_px.data(), (int)trade_buy_ts.size());
      }
      if (m_show_trades && !trade_sell_ts.empty()) {
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, ImVec4(0.9f, 0.25f, 0.25f, 1.0f), 1.0f, ImVec4(0.9f, 0.25f, 0.25f, 0.6f));
        ImPlot::PlotScatter("Trade (sell)", trade_sell_ts.data(), trade_sell_px.data(), (int)trade_sell_ts.size());
      }
    }

    // Playback cursor
    if (current_ts_s > 0.0) {
      double pos[] = {current_ts_s};
      ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 1.0f, 0.55f), 1.5f);
      ImPlot::PlotInfLines("##cursor", pos, 1);
    }

    // ── Rubber-band time selection (LMB drag) ───────────────────────────
    // Start: click inside the plot
    if (!m_is_selecting && ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      m_is_selecting    = true;
      m_select_anchor_s = ImPlot::GetPlotMousePos().x;
      m_select_cursor_s = m_select_anchor_s;
    }
    // Continue: update cursor while held (even if mouse leaves plot)
    if (m_is_selecting) {
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (ImPlot::IsPlotHovered())
          m_select_cursor_s = ImPlot::GetPlotMousePos().x;

        double sx0 = std::min(m_select_anchor_s, m_select_cursor_s);
        double sx1 = std::max(m_select_anchor_s, m_select_cursor_s);
        double sy0[] = {plot_min, plot_min}, sy1[] = {plot_max, plot_max}, sxs[] = {sx0, sx1};
        ImPlot::SetNextFillStyle(ImVec4(0.45f, 0.65f, 1.0f, 0.18f));
        ImPlot::PlotShaded("##Sel", sxs, sy0, sy1, 2);
        ImPlot::SetNextLineStyle(ImVec4(0.55f, 0.75f, 1.0f, 0.9f), 1.2f);
        ImPlot::PlotInfLines("##SelL", &sx0, 1);
        ImPlot::SetNextLineStyle(ImVec4(0.55f, 0.75f, 1.0f, 0.9f), 1.2f);
        ImPlot::PlotInfLines("##SelR", &sx1, 1);
      } else {
        // Released — apply if wide enough to be intentional (> 0.5 s)
        if (std::abs(m_select_cursor_s - m_select_anchor_s) > 0.5)
          zoom_to(std::min(m_select_anchor_s, m_select_cursor_s),
                  std::max(m_select_anchor_s, m_select_cursor_s));
        m_is_selecting = false;
      }
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
