#include "app/gmm_visualizer.hpp"

#include <matplot/matplot.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

#include "core/constants.hpp"

namespace fs = std::filesystem;

namespace {

double NormalPdf(double x, double mu, double sigma) {
  const double z = (x - mu) / sigma;
  return std::exp(-0.5 * z * z) / (sigma * std::sqrt(2.0 * M_PI));
}

std::vector<double> Linspace(double lo, double hi, int n) {
  std::vector<double> v(n);
  for (int i = 0; i < n; ++i)
    v[i] = lo + (hi - lo) * i / (n - 1);
  return v;
}

double Percentile(std::vector<double> sorted, double p) {
  if (sorted.empty()) return 0.0;
  std::sort(sorted.begin(), sorted.end());
  size_t idx = static_cast<size_t>(p * (sorted.size() - 1));
  return sorted[std::min(idx, sorted.size() - 1)];
}

// ── Plot 1: Histogram of responsibilities ────────────────────────────────────
// A U-shape (mass near 0 and 1, little in the middle) means the model found
// a clean separation. A bell shape means the two components overlap heavily.
void PlotResponsibilityHistogram(const std::vector<double>& r,
                                 const std::string& path) {
  auto f = matplot::figure(true);
  f->size(800, 500);
  auto h = matplot::hist(r, 60);
  h->face_color({1.0f, 0.55f, 0.35f, 0.75f});
  h->manual_face_color(true);
  matplot::xlabel("r_i  —  Pr(anomalous | x_i)");
  matplot::ylabel("Count");
  matplot::xlim({0.0, 1.0});
  matplot::box(false);
  f->save(path);
  std::cout << "  Saved: " << path << "\n";
}

// ── Plot 2: r_i split by cancel type ─────────────────────────────────────────
// Fill-cancelled orders are known-legitimate: they should cluster near r_i = 0.
// If they don't, the semi-supervised anchor isn't constraining the model.
void PlotResponsibilityByCancelType(const std::vector<FeatureRecord>& records,
                                    const std::vector<double>& r,
                                    const std::string& path) {
  std::vector<double> r_fill, r_pure;
  for (size_t i = 0; i < records.size(); ++i) {
    (records[i].cancel_type == CancelType::Fill ? r_fill : r_pure).push_back(r[i]);
  }

  auto f = matplot::figure(true);
  f->size(900, 500);
  bool drew = false;

  if (!r_fill.empty()) {
    auto h = matplot::hist(r_fill, 50);
    h->face_color({0.7f, 0.2f, 0.45f, 0.85f});
    h->manual_face_color(true);
    matplot::hold(true);
    drew = true;
  }
  if (!r_pure.empty()) {
    auto h = matplot::hist(r_pure, 50);
    h->face_color({0.7f, 0.85f, 0.3f, 0.3f});
    h->manual_face_color(true);
    drew = true;
  }
  if (drew) matplot::hold(false);

  matplot::xlabel("r_i  —  Pr(anomalous | x_i)");
  matplot::ylabel("Count");
  matplot::xlim({0.0, 1.0});
  matplot::legend({"Fill-cancelled (known legitimate)", "Pure-cancelled"});
  matplot::box(false);
  f->save(path);
  std::cout << "  Saved: " << path << "\n";
}

// ── Plot 3: Empirical density + weighted Gaussian overlays per feature ────────
// Both components are drawn in standardised units so the overlay is exact.
// Overlap between the two Gaussians shows how much each feature discriminates.
void PlotComponentDensity(const std::vector<double>& z,
                          double mu1, double sigma1,
                          double mu2, double sigma2,
                          double pi,
                          const char* feature_name,
                          const std::string& path) {
  auto f = matplot::figure(true);
  f->size(850, 520);

  auto h = matplot::hist(z, 80);
  h->normalization(matplot::histogram::normalization::pdf);
  h->face_color({1.0f, 0.72f, 0.72f, 0.72f});
  h->manual_face_color(true);

  double lo = Percentile(z, 0.005) - 0.3;
  double hi = Percentile(z, 0.995) + 0.3;
  auto grid = Linspace(lo, hi, 500);

  std::vector<double> c1(500), c2(500), mix(500);
  for (int k = 0; k < 500; ++k) {
    c1[k]  = pi * NormalPdf(grid[k], mu1, sigma1);
    c2[k]  = (1.0 - pi) * NormalPdf(grid[k], mu2, sigma2);
    mix[k] = c1[k] + c2[k];
  }

  matplot::hold(true);
  matplot::plot(grid, c1,  "r-")->line_width(2.0f);
  matplot::plot(grid, c2,  "b-")->line_width(2.0f);
  matplot::plot(grid, mix, "k--")->line_width(1.5f);
  matplot::hold(false);

  matplot::xlim({lo, hi});
  matplot::xlabel(std::string(feature_name) + "  (standardised)");
  matplot::ylabel("Density");
  matplot::legend({"C1: anomalous", "C2: liquidity-consistent", "Mixture"});
  matplot::box(false);
  f->save(path);
  std::cout << "  Saved: " << path << "\n";
}

// ── Plot 4: Scatter for one feature pair, coloured by hard assignment ─────────
void PlotFeaturePair(const std::vector<double>& xi,
                     const std::vector<double>& yi,
                     const std::vector<double>& r,
                     const char* x_name,
                     const char* y_name,
                     const std::string& path) {
  std::vector<double> x1, y1, x2, y2;
  for (size_t k = 0; k < r.size(); ++k) {
    if (r[k] > 0.5) { x1.push_back(xi[k]); y1.push_back(yi[k]); }
    else             { x2.push_back(xi[k]); y2.push_back(yi[k]); }
  }

  auto f = matplot::figure(true);
  f->size(700, 620);

  if (!x1.empty()) {
    auto s = matplot::scatter(x1, y1);
    s->marker_size(3);
    s->color({0.85f, 0.2f, 0.2f});
    matplot::hold(true);
  }
  if (!x2.empty()) {
    auto s = matplot::scatter(x2, y2);
    s->marker_size(3);
    s->color({0.2f, 0.4f, 0.85f});
  }
  matplot::hold(false);

  matplot::xlabel(std::string(x_name) + " (std.)");
  matplot::ylabel(std::string(y_name) + " (std.)");
  matplot::legend({"C1: anomalous", "C2: liquidity-consistent"});
  matplot::box(false);
  f->save(path);
  std::cout << "  Saved: " << path << "\n";
}

// ── Plot 5: Log-likelihood per EM restart ────────────────────────────────────
// A wide spread indicates a rough likelihood landscape (many local optima).
// The best restart is the rightmost high bar.
void PlotRestartLikelihoods(const GMMResult& result, const std::string& path) {
  std::vector<double> idx, lls;
  for (const auto& rs : result.restarts) {
    idx.push_back(static_cast<double>(rs.restart + 1));
    lls.push_back(rs.log_likelihood);
  }

  auto f = matplot::figure(true);
  f->size(800, 420);
  matplot::bar(idx, lls);
  matplot::xlabel("Restart");
  matplot::ylabel("Log-likelihood");
  matplot::box(false);
  f->save(path);
  std::cout << "  Saved: " << path << "\n";
}

// ── Plot 6: Mean r_i by 30-min intraday bin (Eastern Time) ───────────────────
// Spoofing tends to peak near open (9:30) and close (15:30-16:00) ET.
// A flat line means the model is picking up a structural pattern, not episodes.
void PlotIntradayPi(const std::vector<FeatureRecord>& records,
                    const std::vector<double>& r,
                    const std::string& path) {
  const int n_bins = 32;             // 30-min bins from 09:00–17:00 ET
  const double DAY_START = 9.0;      // 09:00 ET
  const double BIN_W = 0.5;          // hours
  const double ET_OFFSET = -4.0;     // EDT (UTC-4); change to -5 for EST

  std::vector<double> bin_sum(n_bins, 0.0);
  std::vector<int>    bin_n(n_bins, 0);

  for (size_t i = 0; i < records.size(); ++i) {
    if (records[i].ts_recv == 0) continue;
    double hour_et = static_cast<double>(
        (records[i].ts_recv / 1'000'000'000ULL) % 86400) / 3600.0 + ET_OFFSET;
    if (hour_et < 0) hour_et += 24.0;
    int b = static_cast<int>((hour_et - DAY_START) / BIN_W);
    if (b >= 0 && b < n_bins) { bin_sum[b] += r[i]; bin_n[b]++; }
  }

  std::vector<double> hours, pi_hat;
  for (int b = 0; b < n_bins; ++b) {
    if (bin_n[b] >= 5) {
      hours.push_back(DAY_START + (b + 0.5) * BIN_W);
      pi_hat.push_back(bin_sum[b] / bin_n[b]);
    }
  }

  if (hours.empty()) {
    std::cerr << "  PlotIntradayPi: no data (ts_recv not populated?)\n";
    return;
  }

  auto f = matplot::figure(true);
  f->size(900, 420);
  matplot::bar(hours, pi_hat);
  matplot::xlabel("Hour (Eastern Time)");
  matplot::ylabel("Mean r_i  (anomaly probability)");
  matplot::xlim({9.0, 17.0});
  matplot::box(false);
  f->save(path);
  std::cout << "  Saved: " << path << "\n";
}

}  // namespace

void RunGMMVisualizer(const std::vector<FeatureRecord>& records,
                      const GMMResult& result,
                      const std::vector<int>& feature_indices,
                      const Eigen::VectorXd& data_mean,
                      const Eigen::VectorXd& data_std,
                      const std::string& base_dir) {
  if (records.empty() || result.responsibilities.empty()) {
    std::cerr << "RunGMMVisualizer: no data.\n";
    return;
  }

  const int D = static_cast<int>(feature_indices.size());
  const auto& r = result.responsibilities;

  fs::create_directories(base_dir + "/densities");
  fs::create_directories(base_dir + "/pairs");

  std::cout << "RunGMMVisualizer: " << records.size() << " records -> "
            << base_dir << "/\n";

  // 1. Responsibility histogram
  PlotResponsibilityHistogram(r, base_dir + "/responsibility_histogram.png");

  // 2. Responsibilities split by cancel type
  PlotResponsibilityByCancelType(records, r,
      base_dir + "/responsibility_by_cancel_type.png");

  // Precompute z-scores for all used features (needed for plots 3 and 4)
  std::vector<std::vector<double>> z(D, std::vector<double>(records.size()));
  for (int d = 0; d < D; ++d) {
    int fi = feature_indices[d];
    double mu  = data_mean[d];
    double sig = (data_std[d] > 0.0) ? data_std[d] : 1.0;
    for (size_t i = 0; i < records.size(); ++i)
      z[d][i] = (kFeatures[fi].extract(records[i]) - mu) / sig;
  }

  // 3. Component density overlays
  double pi = result.params.pi;
  for (int d = 0; d < D; ++d) {
    int fi = feature_indices[d];
    double mu1    = result.params.mu1[d];
    double mu2    = result.params.mu2[d];
    double sigma1 = std::sqrt(std::max(result.params.sigma1(d, d), 1e-9));
    double sigma2 = std::sqrt(std::max(result.params.sigma2(d, d), 1e-9));
    std::string path = base_dir + "/densities/" +
                       std::string(kFeatures[fi].name) + ".png";
    PlotComponentDensity(z[d], mu1, sigma1, mu2, sigma2, pi,
                         kFeatures[fi].name, path);
  }

  // 4. Feature pair scatter plots
  for (int i = 0; i < D; ++i) {
    for (int j = i + 1; j < D; ++j) {
      std::string pair_name = std::string(kFeatures[feature_indices[i]].name) +
                              "_vs_" +
                              std::string(kFeatures[feature_indices[j]].name);
      PlotFeaturePair(z[i], z[j], r,
                      kFeatures[feature_indices[i]].name,
                      kFeatures[feature_indices[j]].name,
                      base_dir + "/pairs/" + pair_name + ".png");
    }
  }

  // 5. Restart log-likelihoods
  if (!result.restarts.empty())
    PlotRestartLikelihoods(result, base_dir + "/restart_likelihoods.png");

  // 6. Intraday pi_hat
  PlotIntradayPi(records, r, base_dir + "/intraday_pi.png");

  std::cout << "RunGMMVisualizer: done.\n";
}
