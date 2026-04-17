/**
 * @file gmm.cpp
 * @brief Implementation of the two-component Gaussian Mixture Model.
 */

#include "model/gmm.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>

using Eigen::VectorXd;
using Eigen::MatrixXd;
using Eigen::LDLT;

double GMM::LogGaussianPdf(const VectorXd &x, const VectorXd &mu,
                           const MatrixXd &sigma_inv, double log_det) {
  // log N(x | \mu, \Sigma) = -1/2 [ D log(2\pi) + log|\Sigma| + (x-\mu)^T
  // \Sigma^{-1} (x-\mu) ]
  int D = static_cast<int>(x.size());  // number of features
  VectorXd x_bar = x - mu;  // (x_i - \mu): deviation from component mean
  double mahal = x_bar.transpose() * sigma_inv *
                 x_bar;  // (x-\mu)^T \Sigma^{-1} (x-\mu): Mahalanobis distance
  return -0.5 * (D * std::log(2.0 * M_PI)  // normalisation constant
                 + log_det                 // log|\Sigma|: precomputed from LDLT
                 + mahal);                 // Mahalanobis distance
}

double GMM::LogLikelihood(const std::vector<VectorXd> &data,
                          const GMMParams &p, const MatrixXd &s1_inv,
                          double ld1, const MatrixXd &s2_inv,
                          double ld2) {
  double ll = 0.0;
  for (const auto &x : data) {
    double log_p1 = std::log(p.pi) + LogGaussianPdf(x, p.mu1, s1_inv, ld1);
    double log_p2 = std::log(1.0 - p.pi) + LogGaussianPdf(x, p.mu2, s2_inv, ld2);
    // log-sum-exp for numerical stability
    double log_max = std::max(log_p1, log_p2);
    ll += log_max +
          std::log(std::exp(log_p1 - log_max) + std::exp(log_p2 - log_max));
  }
  return ll;
}

std::vector<VectorXd> GMM::ToEigen(
    const std::vector<FeatureRecord> &records,
    const std::vector<int> &feature_indices) {
  std::vector<VectorXd> out;
  out.reserve(records.size());

  int D = static_cast<int>(feature_indices.size());
  for (const auto &r : records) {
    VectorXd v(D);
    for (int j = 0; j < D; ++j)
      v[j] = kFeatures[feature_indices[j]].extract(r);
    out.push_back(std::move(v));
  }
  return out;
}

std::pair<VectorXd, VectorXd> GMM::Standardize(
    std::vector<VectorXd> &data) {
  if (data.empty()) {
    throw std::runtime_error("GMM::Standardize: empty dataset.");
  }
  int D = static_cast<int>(data[0].size());
  int N = static_cast<int>(data.size());

  VectorXd mean = VectorXd::Zero(D);
  for (const auto &x : data) mean += x;
  mean /= N;

  VectorXd var = VectorXd::Zero(D);
  for (const auto &x : data) {
    VectorXd diff = x - mean;
    var += diff.cwiseProduct(diff);
  }
  var /= N;

  VectorXd std_dev = var.cwiseSqrt();
  // Guard against zero-variance features
  for (int j = 0; j < D; ++j) {
    if (std_dev[j] < 1e-12) std_dev[j] = 1.0;
  }

  for (auto &x : data) {
    x = (x - mean).cwiseQuotient(std_dev);
  }

  return {mean, std_dev};
}

GMMParams GMM::KMeansInit(const std::vector<VectorXd> &data, int seed,
                          double reg) const {
  const int N = static_cast<int>(data.size());
  const int D = static_cast<int>(data[0].size());

  std::mt19937 rng(static_cast<uint32_t>(seed));
  std::uniform_int_distribution<int> dist(0, N - 1);

  VectorXd c1 = data[dist(rng)];
  VectorXd c2 = data[dist(rng)];

  std::vector<int> assignments(N, 0);
  for (int iter = 0; iter < 100; ++iter) {
    for (int i = 0; i < N; ++i)
      assignments[i] = ((data[i] - c1).squaredNorm() <=
                        (data[i] - c2).squaredNorm()) ? 0 : 1;

    VectorXd new_c1 = VectorXd::Zero(D), new_c2 = VectorXd::Zero(D);
    int n1 = 0, n2 = 0;
    for (int i = 0; i < N; ++i) {
      if (assignments[i] == 0) { new_c1 += data[i]; ++n1; }
      else                     { new_c2 += data[i]; ++n2; }
    }
    if (n1 == 0 || n2 == 0) break;
    new_c1 /= n1; new_c2 /= n2;
    if ((new_c1 - c1).norm() < 1e-8 && (new_c2 - c2).norm() < 1e-8) break;
    c1 = new_c1; c2 = new_c2;
  }

  int n1 = 0, n2 = 0;
  for (int a : assignments) a == 0 ? ++n1 : ++n2;
  if (n1 == 0) n1 = 1;
  if (n2 == 0) n2 = 1;

  GMMParams p;
  p.pi     = static_cast<double>(n1) / N;
  p.mu1    = c1;
  p.mu2    = c2;
  p.sigma1 = reg * MatrixXd::Identity(D, D);
  p.sigma2 = reg * MatrixXd::Identity(D, D);
  for (int i = 0; i < N; ++i) {
    if (assignments[i] == 0) { VectorXd d = data[i] - c1; p.sigma1 += d * d.transpose(); }
    else                     { VectorXd d = data[i] - c2; p.sigma2 += d * d.transpose(); }
  }
  p.sigma1 /= n1;
  p.sigma2 /= n2;
  return p;
}

GMMResult GMM::Fit(const std::vector<VectorXd> &data,
                   const FitOptions &opts) const {
  const int N = static_cast<int>(data.size());
  if (N < 2) {
    throw std::runtime_error("GMM::Fit requires at least 2 observations.");
  }
  const int D = static_cast<int>(data[0].size());

  GMMResult best;
  best.log_likelihood = -std::numeric_limits<double>::infinity();
  best.best_restart = 0;

  for (int init = 0; init < opts.n_init; ++init) {
    GMMParams p = KMeansInit(data, init, opts.reg);

    // --- EM loop ---
    std::vector<double> r(N, 0.0);
    double prev_ll = -std::numeric_limits<double>::infinity();
    int iter = 0;

    for (; iter < opts.max_iter; ++iter) {
      // Precompute \Sigma^{-1} and log|\Sigma| via LDLT decomposition
      LDLT<MatrixXd> ldlt1(p.sigma1);
      LDLT<MatrixXd> ldlt2(p.sigma2);

      if (ldlt1.info() != Eigen::Success || ldlt2.info() != Eigen::Success) {
        p.sigma1 += opts.reg * MatrixXd::Identity(D, D);
        p.sigma2 += opts.reg * MatrixXd::Identity(D, D);
        continue;
      }

      MatrixXd s1_inv = ldlt1.solve(MatrixXd::Identity(D, D));
      MatrixXd s2_inv = ldlt2.solve(MatrixXd::Identity(D, D));
      // log|\Sigma| = sum of logs of the diagonal of D in the LDLT factorisation
      double ld1 = ldlt1.vectorD().array().log().sum();
      double ld2 = ldlt2.vectorD().array().log().sum();

      // E-step: compute r_i^{(p)} = P(z_i = anomalous | x_i, \theta^{(p)})
      // Eq. (10): r_i = [ \pi^{(p)} f_anomalous(x_i) ] /
      //                 [ \pi^{(p)} f_anomalous(x_i) + (1 - \pi^{(p)}) f_lc(x_i) ]
      // Works in log-space to avoid floating-point underflow.
      for (int i = 0; i < N; ++i) {
        // Known liquidity-consistent observations are pinned to r_i = 0.
        if (!opts.fixed_lc.empty() && opts.fixed_lc[i]) {
          r[i] = 0.0;
          continue;
        }
        double log_p1 =
            std::log(p.pi) + LogGaussianPdf(data[i], p.mu1, s1_inv, ld1);
        double log_p2 =
            std::log(1.0 - p.pi) + LogGaussianPdf(data[i], p.mu2, s2_inv, ld2);
        double log_max = std::max(log_p1, log_p2);
        double sum_exp = std::exp(log_p1 - log_max) + std::exp(log_p2 - log_max);
        r[i] = std::exp(log_p1 - log_max) / sum_exp;
      }

      // M-step: update \theta using r_i as soft weights
      double sum_r1 = 0.0, sum_r2 = 0.0;
      for (int i = 0; i < N; ++i) { sum_r1 += r[i]; sum_r2 += (1.0 - r[i]); }

      p.pi = std::clamp(sum_r1 / N, 1e-6, 1.0 - 1e-6);

      VectorXd mu1_new = VectorXd::Zero(D);
      VectorXd mu2_new = VectorXd::Zero(D);
      for (int i = 0; i < N; ++i) {
        mu1_new += r[i] * data[i];
        mu2_new += (1.0 - r[i]) * data[i];
      }
      p.mu1 = mu1_new / sum_r1;
      p.mu2 = mu2_new / sum_r2;

      MatrixXd s1_new = opts.reg * MatrixXd::Identity(D, D);
      MatrixXd s2_new = opts.reg * MatrixXd::Identity(D, D);
      for (int i = 0; i < N; ++i) {
        VectorXd d1 = data[i] - p.mu1;
        VectorXd d2 = data[i] - p.mu2;
        s1_new += r[i] * (d1 * d1.transpose());
        s2_new += (1.0 - r[i]) * (d2 * d2.transpose());
      }
      p.sigma1 = s1_new / sum_r1;
      p.sigma2 = s2_new / sum_r2;

      // Convergence check: |log L(\theta^{(p+1)}) - log L(\theta^{(p)})| < tol
      LDLT<MatrixXd> c1(p.sigma1), c2(p.sigma2);
      MatrixXd cs1_inv = c1.solve(MatrixXd::Identity(D, D));
      MatrixXd cs2_inv = c2.solve(MatrixXd::Identity(D, D));
      double cld1 = c1.vectorD().array().log().sum();
      double cld2 = c2.vectorD().array().log().sum();

      double ll = LogLikelihood(data, p, cs1_inv, cld1, cs2_inv, cld2);
      if (std::abs(ll - prev_ll) < opts.tol) {
        prev_ll = ll;
        ++iter;
        break;
      }
      prev_ll = ll;
    }

    // Label consistency: ensure component 1 = anomalous (lower delta_t mean).
    // K-means may arbitrarily assign the fast-cancel cluster to either component;
    // swap so the output labelling is always consistent across restarts.
    if (p.mu1[0] > p.mu2[0]) {
      std::swap(p.mu1, p.mu2);
      std::swap(p.sigma1, p.sigma2);
      p.pi = 1.0 - p.pi;
      for (double &ri : r) ri = 1.0 - ri;
    }

    double pi_spoof = 0.0;
    for (double ri : r) pi_spoof += ri;
    pi_spoof /= N;

    best.restarts.push_back({init, prev_ll, pi_spoof, iter});

    if (prev_ll > best.log_likelihood) {
      best.params        = p;
      best.responsibilities = r;
      best.pi_spoof      = pi_spoof;
      best.log_likelihood = prev_ll;
      best.iterations    = iter;
      best.best_restart  = init;
    }
  }
  return best;
}
