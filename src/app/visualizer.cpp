/**
 * @file visualizer.cpp
 * @brief Feature distribution plots for exploratory analysis.
 */

#include "app/visualizer.hpp"

#include <matplot/matplot.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

#include "core/constants.hpp"

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

  auto h = matplot::hist(values, n_bins);
  // color_array is {alpha, r, g, b}. face_color() does not set
  // manual_face_color_, so the axis color cycle (blue) overrides at render
  // time unless we set it explicitly here.
  h->face_color({1.0f, 0.45f, 0.45f, 0.45f});  // solid mid-grey
  h->manual_face_color(true);

  // APA: no embedded title (caption belongs in the document)
  matplot::xlabel(xlabel);
  matplot::ylabel("Frequency");
  matplot::box(false);  // L-shaped axes only; no top/right spine

  f->save(path);
  std::cout << "  Saved [" << title << "]: " << path << "\n";
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
std::vector<double> Clip(const std::vector<double> &values, double lo,
                         double hi) {
  std::vector<double> out;
  out.reserve(values.size());
  for (double v : values)
    if (v >= lo && v <= hi) out.push_back(v);
  return out;
}

}  // namespace

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
  auto us_to_s = [](double us) { return us * 1e-6; };
  std::cout << std::fixed << std::setprecision(3) << "InspectOrderAge: " << N
            << " records\n"
            << "  p1    = " << Percentile(sorted_us, 0.010) * 1e-3 << " ms\n"
            << "  p5    = " << Percentile(sorted_us, 0.050) * 1e-3 << " ms\n"
            << "  p10   = " << Percentile(sorted_us, 0.10) * 1e-3 << " ms\n"
            << "  p25   = " << Percentile(sorted_us, 0.25) * 1e-3 << " ms\n"
            << "  p50   = " << Percentile(sorted_us, 0.50) * 1e-3 << " ms\n"
            << "  p75   = " << Percentile(sorted_us, 0.75) * 1e-3 << " ms\n"
            << "  p90   = " << us_to_s(Percentile(sorted_us, 0.90)) << " s\n"
            << "  p95   = " << us_to_s(Percentile(sorted_us, 0.95)) << " s\n"
            << "  p99   = " << us_to_s(Percentile(sorted_us, 0.99)) << " s\n"
            << "  p99.9 = " << us_to_s(Percentile(sorted_us, 0.999)) << " s\n"
            << "  max   = " << us_to_s(sorted_us.back()) << " s\n"
            << std::defaultfloat;

  // Raw: clip at p75
  {
    double p75 = Percentile(sorted_us, 0.75);
    auto clipped = Clip(dt_us, 0.0, p75);
    std::cout << "  raw plot window: 0 - " << p75 << " us  ("
              << (100.0 * clipped.size() / N) << "% of records)\n";
    SaveHistogram(clipped, "Order Age - raw", "\\Delta t_i  (\\mus)",
                  output_dir + "/order_age_raw.png");
  }

  // ln(Δt_i): motivates the two-component GMM by revealing bimodality.
  // Floor at 1 μs: sub-microsecond lifetimes produce negative ln values and
  // are below the meaningful timestamp resolution of ITCH data.
  {
    std::vector<double> ln_us;
    ln_us.reserve(N);
    int n_dropped = 0;
    for (int i = 0; i < N; ++i) {
      if (dt_us[i] >= 1.0)
        ln_us.push_back(std::log(dt_us[i]));
      else
        ++n_dropped;
    }
    if (n_dropped > 0)
      std::cout << "  ln plot: dropped " << n_dropped
                << " sub-microsecond observations\n";

    SaveHistogram(ln_us, "Order Age - natural log", "ln(\\Delta t_i)  [\\mus]",
                  output_dir + "/order_age_ln.png");
  }

  std::cout << "InspectOrderAge: 2 plots saved to " << output_dir << "/\n";
}

void RunVisualizer(const std::vector<FeatureRecord> &records,
                   const std::string &base_dir) {
  if (records.empty()) {
    std::cerr << "RunVisualizer: no records to plot.\n";
    return;
  }

  std::cout << "RunVisualizer: " << records.size() << " records -> " << base_dir
            << "/\n";

  InspectOrderAge(records, base_dir + "/order_age");
}
