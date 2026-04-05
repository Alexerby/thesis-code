/**
 * @file visualizer.cpp
 * @brief Feature distribution plots for exploratory analysis.
 */

#include "app/visualizer.hpp"
#include "core/constants.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <matplot/matplot.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

/**
 * @brief Saves a single histogram of @p values to @p path.
 *
 * @param values   Raw data to plot.
 * @param title    Plot title.
 * @param xlabel   X-axis label.
 * @param path     Output file path (PNG).
 * @param n_bins   Number of histogram bins.
 */
void SaveHistogram(const std::vector<double> &values, const std::string &title,
                   const std::string &xlabel, const std::string &path,
                   int n_bins = 100) {
  auto f = matplot::figure(true);
  f->size(900, 500);

  matplot::hist(values, n_bins);
  matplot::title(title);
  matplot::xlabel(xlabel);
  matplot::ylabel("Count");

  f->save(path);
  std::cout << "  Saved: " << path << "\n";
}

/**
 * @brief Returns the value at the p-th percentile of a sorted vector.
 *
 * @param sorted  Data sorted in ascending order.
 * @param p       Percentile in [0, 1].
 */
double Percentile(const std::vector<double> &sorted, double p) {
  if (sorted.empty()) return 0.0;
  const std::size_t idx =
      static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
  return sorted[std::min(idx, sorted.size() - 1)];
}

/**
 * @brief Clips @p values to [lo, hi] and returns the filtered result.
 */
std::vector<double> Clip(const std::vector<double> &values,
                         double lo, double hi) {
  std::vector<double> out;
  out.reserve(values.size());
  for (double v : values)
    if (v >= lo && v <= hi) out.push_back(v);
  return out;
}

} // namespace

void InspectOrderAge(const std::vector<FeatureRecord> &records,
                     const std::string &output_dir) {
  if (records.empty()) {
    std::cerr << "InspectOrderAge: no records.\n";
    return;
  }

  fs::create_directories(output_dir);

  const int N = static_cast<int>(records.size());

  // Convert delta_t from nanoseconds to microseconds
  std::vector<double> dt_us(N);
  for (int i = 0; i < N; ++i)
    dt_us[i] = records[i].delta_t * constants::NS_TO_US;

  auto sorted_us = dt_us;
  std::sort(sorted_us.begin(), sorted_us.end());

  // --- Percentile summary ---
  // Convert representative percentiles to the most readable unit at each scale
  auto us_to_ms = [](double us) { return us * 1e-3; };
  auto us_to_s  = [](double us) { return us * 1e-6; };
  std::cout << std::fixed << std::setprecision(3)
            << "InspectOrderAge: " << N << " records\n"
            << "  p1    = " << Percentile(sorted_us, 0.010) * 1e-3 << " ms\n"
            << "  p5    = " << Percentile(sorted_us, 0.050) * 1e-3 << " ms\n"
            << "  p10   = " << Percentile(sorted_us, 0.10)  * 1e-3 << " ms\n"
            << "  p25   = " << Percentile(sorted_us, 0.25)  * 1e-3 << " ms\n"
            << "  p50   = " << Percentile(sorted_us, 0.50)  * 1e-3 << " ms\n"
            << "  p75   = " << Percentile(sorted_us, 0.75)  * 1e-3 << " ms\n"
            << "  p90   = " << us_to_s(Percentile(sorted_us, 0.90))  << " s\n"
            << "  p95   = " << us_to_s(Percentile(sorted_us, 0.95))  << " s\n"
            << "  p99   = " << us_to_s(Percentile(sorted_us, 0.99))  << " s\n"
            << "  p99.9 = " << us_to_s(Percentile(sorted_us, 0.999)) << " s\n"
            << "  max   = " << us_to_s(sorted_us.back())              << " s\n"
            << std::defaultfloat;

  // --- Raw telescoping views (no log transform) ---

  // Sub-millisecond: show in microseconds
  {
    auto sub_ms = Clip(dt_us, 0.0, constants::US_PER_MS);
    std::cout << "  sub-1ms fraction: "
              << (100.0 * sub_ms.size() / N) << "%\n";
    SaveHistogram(sub_ms,
                  "Order Age - sub-millisecond (raw)",
                  "\\Delta t_i  (microseconds,  window: 0 - 1 ms)",
                  output_dir + "/order_age_raw_sub1ms.png");
  }

  // Sub-second: show in milliseconds
  {
    std::vector<double> dt_ms(N);
    for (int i = 0; i < N; ++i) dt_ms[i] = dt_us[i] * 1e-3;
    auto sub_s = Clip(dt_ms, 0.0, 1000.0); // 0 - 1000 ms
    std::cout << "  sub-1s  fraction: "
              << (100.0 * sub_s.size() / N) << "%\n";
    SaveHistogram(sub_s,
                  "Order Age - sub-second (raw)",
                  "\\Delta t_i  (milliseconds,  window: 0 - 1 s)",
                  output_dir + "/order_age_raw_sub1s.png");
  }

  // Sub-minute: show in seconds
  {
    std::vector<double> dt_s(N);
    for (int i = 0; i < N; ++i) dt_s[i] = dt_us[i] * 1e-6;
    auto sub_min = Clip(dt_s, 0.0, 60.0); // 0 - 60 s
    std::cout << "  sub-1min fraction: "
              << (100.0 * sub_min.size() / N) << "%\n";
    SaveHistogram(sub_min,
                  "Order Age - sub-minute (raw)",
                  "\\Delta t_i  (seconds,  window: 0 - 60 s)",
                  output_dir + "/order_age_raw_sub1min.png");
  }

  // --- Log-transformed views ---

  // log10(us) - standard HFT analysis scale
  {
    std::vector<double> log10_us(N);
    for (int i = 0; i < N; ++i)
      log10_us[i] = std::log10(dt_us[i] + 1.0);

    SaveHistogram(log10_us,
                  "Order Age - log10 scale",
                  "log10(\\Delta t_i + 1)  [log-microseconds]",
                  output_dir + "/order_age_log10.png");
  }

  // log1p(us) - transform used inside GMM
  {
    std::vector<double> log1p_us(N);
    for (int i = 0; i < N; ++i)
      log1p_us[i] = std::log1p(dt_us[i]);

    SaveHistogram(log1p_us,
                  "Order Age - log1p scale (GMM input)",
                  "log(1 + \\Delta t_i)  [log-microseconds]",
                  output_dir + "/order_age_log1p.png");
  }

  std::cout << "InspectOrderAge: 5 plots saved to " << output_dir << "/\n";
}

void RunVisualizer(const std::vector<FeatureRecord> &records,
                   const std::string &base_dir) {
  if (records.empty()) {
    std::cerr << "RunVisualizer: no records to plot.\n";
    return;
  }

  std::cout << "RunVisualizer: " << records.size() << " records -> "
            << base_dir << "/\n";

  InspectOrderAge(records, base_dir + "/order_age");
}
