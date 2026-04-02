#include "app/market_dashboard.hpp"
#include "app/replay_controller.hpp"
#include "imgui.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace { // make anonymous
constexpr uint64_t NANOS_1S = 1'000'000'000ULL;
constexpr uint64_t NANOS_10S = 10'000'000'000ULL;
constexpr uint64_t NANOS_1M = 60'000'000'000ULL;

constexpr float HEADER_HEIGHT = 50.0f;
constexpr float CONTROLS_HEIGHT = 80.0f;
constexpr float ORDERBOOK_DEPTH_HEIGHT = 550.0f;


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
  if (ss.fail())
    return 0;

  // reference_ts is unix nanos. We need the date part from it.
  std::time_t ref_secs = reference_ts / NANOS_1S;
  std::tm *ref_tm = std::gmtime(&ref_secs);

  ref_tm->tm_hour = t.tm_hour;
  ref_tm->tm_min = t.tm_min;
  ref_tm->tm_sec = t.tm_sec;

  return static_cast<uint64_t>(timegm(ref_tm)) * NANOS_1S;
}
} // namespace

MarketDashboard::MarketDashboard() {}
MarketDashboard::~MarketDashboard() {}

void MarketDashboard::Render(const MarketSnapshot &snapshot,
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

  // Volume section
  RenderOrderBookDepth(snapshot);

  // Pop window from stack
  ImGui::End();
}

void MarketDashboard::RenderPlaybackControls(ReplayController &controller) {
  ImGui::BeginChild("Controls", ImVec2(0, CONTROLS_HEIGHT), true);
  SessionStats stats = controller.GetSessionStats();
  PlaybackState state = controller.GetPlaybackState();

  // Play/Pause/Step
  if (state == PlaybackState::Paused) {
    if (ImGui::Button(" Play "))
      controller.SetPlaybackState(PlaybackState::Playing);
    ImGui::SameLine();
    if (ImGui::Button(" Step > "))
      controller.RequestStep();
  } else {
    if (ImGui::Button(" Pause "))
      controller.SetPlaybackState(PlaybackState::Paused);
  }

  // Skips
  ImGui::SameLine(0, 20);
  if (ImGui::Button("+1s"))
    controller.SeekToTime(stats.current_ts + NANOS_1S);
  ImGui::SameLine();
  if (ImGui::Button("+10s"))
    controller.SeekToTime(stats.current_ts + NANOS_10S);
  ImGui::SameLine();
  if (ImGui::Button("+1m"))
    controller.SeekToTime(stats.current_ts + NANOS_1M);

  // Jump to Time
  ImGui::SameLine(0, 40);
  static char jump_time[16] = "14:30:00";
  ImGui::SetNextItemWidth(100);
  ImGui::InputText("##JumpTime", jump_time, IM_ARRAYSIZE(jump_time));
  ImGui::SameLine();
  if (ImGui::Button("Jump to Time")) {
    uint64_t target = TimeStringToNanos(jump_time, stats.start_ts);
    if (target > 0)
      controller.SeekToTime(target);
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

void MarketDashboard::RenderHeader(const MarketSnapshot &snapshot,
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
      if (is_selected)
        ImGui::SetItemDefaultFocus();
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

void MarketDashboard::RenderOrderBookDepth(const MarketSnapshot &snapshot) {
  ImGui::BeginChild("Order book depth", ImVec2(0, ORDERBOOK_DEPTH_HEIGHT), true);
  const std::vector<float> &left_data =
      m_use_cumulative ? snapshot.bid_volumes_cum : snapshot.bid_volumes;
  const std::vector<float> &right_data =
      m_use_cumulative ? snapshot.ask_volumes_cum : snapshot.ask_volumes;

  if (left_data.empty() || right_data.empty()) {
    ImGui::Text("Waiting for market depth data... (Press Play or Jump)");
  } else {


  float max_vol = 0.1f;
  for (float v : left_data)
    if (v > max_vol)
      max_vol = v;
  for (float v : right_data)
    if (v > max_vol)
      max_vol = v;

  float available_width = ImGui::GetContentRegionAvail().x;
  float center_width = CHART_CENTER_WIDTH;
  float plot_width = (available_width - center_width) * 0.5f;
  float plot_height = ImGui::GetContentRegionAvail().y - 45.0f;

  std::vector<float> rev_bids = left_data;
  std::reverse(rev_bids.begin(), rev_bids.end());

  ImGui::Checkbox("Cumulative Volume (Mountain)", &m_use_cumulative);
  ImGui::BeginGroup();
  TextCentered("BIDS (Liquidity)", plot_width);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.0f, 0.5f, 0.0f, 1.0f));
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
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
  ImGui::PlotHistogram("##Asks", right_data.data(), (int)right_data.size(), 0,
                       nullptr, 0.0f, max_vol * 1.1f,
                       ImVec2(plot_width, plot_height));
  ImGui::PopStyleColor();
  ImGui::EndGroup();
  }
  ImGui::EndChild();
}
