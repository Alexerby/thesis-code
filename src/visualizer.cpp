#include "visualizer.hpp"
#include "databento/constants.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <vector>

Visualizer::Visualizer(const std::vector<std::size_t> &depths,
                       std::chrono::milliseconds refresh_rate)
    : m_depths(depths), m_refresh_rate(refresh_rate),
      m_last_render(std::chrono::steady_clock::now()) {}

void Visualizer::SetTimeDomain(uint64_t start_ts, uint64_t end_ts) {
  m_start_ts = start_ts;
  m_end_ts = end_ts;
}

void Visualizer::UpdateHistory(const MarketState &state) {
  const std::string &sym = state.symbol;
  
  // 1. Accumulate volume delta every single message
  uint64_t current_total = state.total_trade_volume;
  bool trade_occurred = false;
  if (m_last_total_volumes.find(sym) != m_last_total_volumes.end()) {
      uint64_t last_total = m_last_total_volumes[sym];
      if (current_total > last_total) {
          m_accumulated_volumes[sym] += static_cast<uint32_t>(current_total - last_total);
          trade_occurred = true;
      }
  }
  m_last_total_volumes[sym] = current_total;

  // 2. Sample logic
  double bid_px = (state.bbo.first.price > 0 &&
                   state.bbo.first.price != databento::kUndefPrice)
                      ? state.bbo.first.price / 1e9
                      : 0;
  double ask_px = (state.bbo.second.price > 0 &&
                   state.bbo.second.price != databento::kUndefPrice)
                      ? state.bbo.second.price / 1e9
                      : 0;
  double last_px = (trade_occurred && state.last_trade_price > 0 &&
                    state.last_trade_price != databento::kUndefPrice)
                       ? state.last_trade_price / 1e9
                       : 0;

  auto &history = m_price_histories[sym];
  bool price_changed = history.empty() ||
                       history.back().bid != bid_px ||
                       history.back().ask != ask_px;

  m_msg_counter++;
  if (price_changed || trade_occurred || m_msg_counter % HISTORY_SAMPLE_RATE == 0) {
    if (bid_px > 0 || ask_px > 0 || last_px > 0) {
      uint32_t vol_to_push = m_accumulated_volumes[sym];
      history.push_back({state.ts_recv, bid_px, ask_px, last_px, vol_to_push});
      m_accumulated_volumes[sym] = 0; // Reset after pushing
    }
  }
}

void Visualizer::UpdateOverview(const MarketState &state) {
  m_latest_states[state.symbol] = state;
}

int Visualizer::GetVisibleLength(const std::string &str) {
  int visible_len = 0;
  bool in_ansi = false;
  for (char c : str) {
    if (c == '\033') in_ansi = true;
    else if (in_ansi && c == 'm') in_ansi = false;
    else if (!in_ansi) visible_len++;
  }
  return visible_len;
}

std::string Visualizer::GetHeader(const MarketState &state) {
  std::stringstream ss;
  ss << CLEAR << YEL << ">> " << state.symbol << " | " << state.timestamp
     << RESET << "\n";
  return ss.str();
}

std::string Visualizer::GetMetricBar(const std::string &label, double value) {
  const int bar_width = 16;
  const int mid = bar_width / 2;
  double clamped = std::max(-1.0, std::min(1.0, value));
  int fill_amount = static_cast<int>(std::abs(clamped) * mid);
  std::string bar = std::string(bar_width, '.');
  std::string color = RESET;
  if (clamped > 0.01) {
    color = GRN;
    for (int i = 0; i < fill_amount; ++i) bar[mid + i] = '|';
  } else if (clamped < -0.01) {
    color = RED;
    for (int i = 0; i < fill_amount; ++i) bar[mid - 1 - i] = '|';
  } else {
    bar[mid] = ':';
  }
  std::stringstream ss;
  ss << std::left << std::setw(13) << label << " [" << color << bar << RESET
     << "] " << std::fixed << std::setprecision(3) << std::setw(6) << value;
  return ss.str();
}

std::string Visualizer::GetBBO(const std::pair<PriceLevel, PriceLevel> &bbo) {
  std::stringstream ss;
  const auto &bid = bbo.first;
  const auto &ask = bbo.second;
  ss << "  " << GRN << "BID: " << RESET << std::fixed << std::setprecision(4)
     << (bid.price / 1e9) << " (" << std::setw(5) << bid.size << ")\n"
     << "  " << RED << "ASK: " << RESET << std::fixed << std::setprecision(4)
     << (ask.price / 1e9) << " (" << std::setw(5) << ask.size << ")";
  return ss.str();
}

std::vector<std::string> Visualizer::GetMarketOverviewLines() {
  std::vector<std::string> lines;
  lines.push_back(CYN + "--- Market Overview ---" + RESET);
  std::map<std::string, MarketState> sorted_states(m_latest_states.begin(), m_latest_states.end());
  for (const auto &pair : sorted_states) {
    const auto &s = pair.second;
    std::stringstream ss;
    ss << std::left << std::setw(6) << s.symbol << " | " 
       << GRN << std::fixed << std::setprecision(2) << std::setw(8) << (s.bbo.first.price / 1e9) << RESET << " x "
       << RED << std::fixed << std::setprecision(2) << std::setw(8) << (s.bbo.second.price / 1e9) << RESET;
    lines.push_back(ss.str());
  }
  return lines;
}

std::vector<std::string> Visualizer::GetDashboardLines(const MarketState &state) {
  std::vector<std::string> left;
  left.push_back(CYN + "--- Focused: " + state.symbol + " ---" + RESET);
  std::stringstream ss_bbo(GetBBO(state.bbo));
  std::string bbo_line;
  while (std::getline(ss_bbo, bbo_line)) left.push_back(bbo_line);
  left.push_back("");
  left.push_back(CYN + "--- Market Pressure ---" + RESET);
  for (const auto &metric : state.imbalance_levels) {
    left.push_back(GetMetricBar(metric.first, metric.second));
  }
  return left;
}

std::vector<std::string>
Visualizer::GetOrderbookLines(const MarketState &state) {
  std::vector<std::string> lines;
  lines.push_back(YEL + "  Bid Vol     |     Price     |   Ask Vol" + RESET);
  double max_side_vol = 1.0;
  for (const auto &v : state.volume_levels) {
    if (v.first.find("Vol") != std::string::npos && v.second > max_side_vol)
      max_side_vol = v.second;
  }
  const int max_chars = 12;
  for (size_t d : m_depths) {
    double b_vol = 0, a_vol = 0, b_px = 0, a_px = 0;
    for (const auto &v : state.volume_levels) {
      if (v.first == "L" + std::to_string(d) + "_BidVol") b_vol = v.second;
      if (v.first == "L" + std::to_string(d) + "_AskVol") a_vol = v.second;
      if (v.first == "B_P" + std::to_string(d)) b_px = v.second;
      if (v.first == "A_P" + std::to_string(d)) a_px = v.second;
    }
    int b_fill = static_cast<int>((b_vol / max_side_vol) * max_chars);
    int a_fill = static_cast<int>((a_vol / max_side_vol) * max_chars);
    std::string b_bar = std::string(b_fill, '#') + std::string(max_chars - b_fill, ' ');
    std::string a_bar = std::string(max_chars - a_fill, ' ') + std::string(a_fill, '#');
    std::stringstream ss;
    ss << GRN << std::setw(max_chars) << b_bar << RESET << " | " << std::fixed
       << std::setprecision(2) << std::right << std::setw(7) << b_px << " : "
       << std::left << std::setw(7) << a_px << " | " << RED
       << std::setw(max_chars) << a_bar << RESET;
    lines.push_back(ss.str());
  }
  return lines;
}

std::vector<std::string> Visualizer::GetPriceChartLines(const std::string &symbol) {
  std::vector<std::string> lines;
  auto it = m_price_histories.find(symbol);
  if (it == m_price_histories.end() || it->second.empty()) {
    lines.push_back("[Gathering price data...]");
    return lines;
  }
  const auto &history = it->second;
  const int height = 14;
  const int width = 85;
  double min_p = 1e18, max_p = -1e18;
  struct PixelData {
    double last_p = 0;
    double last_bid = 0;
    double last_ask = 0;
    uint64_t total_vol = 0;
    bool has_data = false;
  };
  std::vector<PixelData> pixels(width);
  uint64_t total_nanos = m_end_ts - m_start_ts;
  if (total_nanos == 0) total_nanos = 1;
  for (const auto &pt : history) {
    if (pt.ts_nanos < m_start_ts) continue;
    int x = static_cast<int>((static_cast<double>(pt.ts_nanos - m_start_ts) / total_nanos) * (width - 1));
    x = std::max(0, std::min(width - 1, x));
    if (pt.bid > 0) { min_p = std::min(min_p, pt.bid); max_p = std::max(max_p, pt.bid); }
    if (pt.ask > 0) { min_p = std::min(min_p, pt.ask); max_p = std::max(max_p, pt.ask); }
    if (pt.last > 0) { min_p = std::min(min_p, pt.last); max_p = std::max(max_p, pt.last); }
    pixels[x].total_vol += pt.volume;
    if (pt.last > 0) pixels[x].last_p = pt.last;
    if (pt.bid > 0) pixels[x].last_bid = pt.bid;
    if (pt.ask > 0) pixels[x].last_ask = pt.ask;
    pixels[x].has_data = true;
  }
  uint64_t max_pixel_vol = 1;
  for (const auto &p : pixels) if (p.total_vol > max_pixel_vol) max_pixel_vol = p.total_vol;
  if (max_p <= min_p) { min_p -= 1.0; max_p += 1.0; }
  double range = max_p - min_p;
  min_p -= range * 0.05; max_p += range * 0.05; range = max_p - min_p;
  struct Pixel { char ch = ' '; const std::string *color = nullptr; };
  std::vector<std::vector<Pixel>> grid(height, std::vector<Pixel>(width));
  auto map_y = [&](double p) {
    if (p <= 0) return -1;
    int y = static_cast<int>((1.0 - (p - min_p) / range) * (height - 1));
    return std::max(0, std::min(height - 1, y));
  };
  for (int x = 0; x < width; ++x) {
    if (!pixels[x].has_data) continue;
    if (pixels[x].total_vol > 0) {
      int vol_h = static_cast<int>((static_cast<double>(pixels[x].total_vol) / max_pixel_vol) * (height / 3.0));
      if (vol_h == 0 && pixels[x].total_vol > 0) vol_h = 1;
      for (int i = 0; i < vol_h; ++i) grid[height - 1 - i][x] = {'|', &RESET};
    }
    int y_bid = map_y(pixels[x].last_bid);
    int y_ask = map_y(pixels[x].last_ask);
    int y_last = map_y(pixels[x].last_p);
    if (y_bid != -1) grid[y_bid][x] = {'.', &CYN};
    if (y_ask != -1) grid[y_ask][x] = {'.', &RED};
    if (y_last != -1) grid[y_last][x] = {'*', &YEL};
  }
  for (int y = 0; y < height; ++y) {
    std::stringstream ss;
    double price = max_p - (static_cast<double>(y) / (height - 1)) * range;
    ss << std::fixed << std::setprecision(4) << std::setw(10) << price << " | ";
    for (int x = 0; x < width; ++x) {
      if (grid[y][x].color) ss << *grid[y][x].color << grid[y][x].ch << RESET;
      else ss << ' ';
    }
    lines.push_back(ss.str());
  }
  lines.push_back(std::string(11, ' ') + "+" + std::string(width, '-'));
  auto format_time = [](uint64_t nanos) {
    time_t secs = nanos / 1000000000;
    struct tm *tm_info = gmtime(&secs);
    char buffer[10];
    strftime(buffer, 10, "%H:%M:%S", tm_info);
    return std::string(buffer);
  };
  const int num_ticks = 5;
  std::string timeline = std::string(11, ' ');
  int last_pos = 0;
  for (int i = 0; i < num_ticks; ++i) {
    double fraction = static_cast<double>(i) / (num_ticks - 1);
    uint64_t ts = m_start_ts + static_cast<uint64_t>(fraction * total_nanos);
    std::string label = format_time(ts);
    int target_pos = static_cast<int>(fraction * (width - 1));
    int spaces_needed = target_pos - last_pos;
    if (i == 0) timeline += label;
    else {
      int adj_spaces = spaces_needed - (label.length() / 2);
      if (adj_spaces > 0) timeline += std::string(adj_spaces, ' ') + label;
    }
    last_pos = target_pos + (label.length() / 2);
  }
  lines.push_back(timeline);
  return lines;
}

void Visualizer::DrawSplitPanel(const std::vector<std::string> &left,
                                const std::vector<std::string> &right) {
  size_t max_l = std::max(left.size(), right.size());
  for (size_t i = 0; i < max_l; ++i) {
    std::string l_str = i < left.size() ? left[i] : "";
    std::string r_str = i < right.size() ? right[i] : "";
    std::cout << l_str;
    int padding = 45 - GetVisibleLength(l_str);
    if (padding < 0) padding = 1;
    std::cout << std::string(padding, ' ') << " |  " << r_str << "\n";
  }
}

void Visualizer::DrawPriceHistory(const std::string &symbol) {
  std::cout << "\n" << YEL << "--- Price History: " << symbol << " ---" << RESET << "\n";
  for (const auto &line : GetPriceChartLines(symbol)) {
    std::cout << line << "\n";
  }
}

void Visualizer::RecordAction(const MarketState &state, const MetadataSummary &meta) {
  if (!m_domain_set) {
    SetTimeDomain(meta.start_ts, meta.end_ts);
    m_domain_set = true;
  }
  UpdateHistory(state);
  UpdateOverview(state);
  auto now = std::chrono::steady_clock::now();
  if (now - m_last_render < m_refresh_rate) return;
  m_last_render = now;
  std::cout << GetHeader(state);
  auto overview = GetMarketOverviewLines();
  for (const auto &line : overview) std::cout << line << "\n";
  std::cout << "\n";
  DrawSplitPanel(GetDashboardLines(state), GetOrderbookLines(state));
  DrawPriceHistory(state.symbol);
  std::cout << std::string(105, '=') << std::endl;
}
