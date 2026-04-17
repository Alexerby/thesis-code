/**
 * @file gmm.cpp
 * @brief Implementation of the two-component Gaussian Mixture Model.
 */

#include "model/gmm.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>

using Eigen::VectorXd;
using Eigen::MatrixXd;
using Eigen::LDLT;

double GMM::LogGaussianPdf(const VectorXd &x, const VectorXd &mu,
                           const MatrixXd &sigma_inv, double log_det) {
  // log N(x | \mu, \Sigma) = -1/2 [ n log(2\pi) + log|\Sigma| + (x-\mu)^T
  // \Sigma^{-1} (x-\mu) ]
  int n = static_cast<int>(x.size());  // number of features
  VectorXd x_bar = x - mu;  // (x_i - \mu): deviation from component mean
  double mahal = x_bar.transpose() * sigma_inv *
                 x_bar;  // (x-\mu)^T \Sigma^{-1} (x-\mu): Mahalanobis distance
  return -0.5 * (n * std::log(2.0 * M_PI)  // normalisation constant
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
    const double all[2] = {r.delta_t, r.induced_imbalance};
    VectorXd v(D);
    for (int j = 0; j < D; ++j) {
      v[j] = all[feature_indices[j]];
    }
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

GMMResult GMM::Fit(const std::vector<VectorXd> &data,
                   const FitOptions &opts) const {
  const int N = static_cast<int>(data.size());
  if (N < 2) {
    throw std::runtime_error("GMM::Fit requires at least 2 observations.");
  }
  const int D = static_cast<int>(data[0].size());

  // --- Initialisation ---
  // Sort by first feature and assign the bottom 20% to component 1
  // (the fast-cancel / strategic region).
  std::vector<int> idx(N);
  std::iota(idx.begin(), idx.end(), 0);
  std::sort(idx.begin(), idx.end(),
            [&](int a, int b) { return data[a][0] < data[b][0]; });

  int split = std::max(1, static_cast<int>(0.2 * N));

  GMMParams p;
  p.pi = static_cast<double>(split) / N;
  p.mu1 = VectorXd::Zero(D);
  p.mu2 = VectorXd::Zero(D);
  p.sigma1 = MatrixXd::Identity(D, D);
  p.sigma2 = MatrixXd::Identity(D, D);

  for (int i = 0; i < split; ++i) p.mu1 += data[idx[i]];
  for (int i = split; i < N; ++i) p.mu2 += data[idx[i]];
  p.mu1 /= split;
  p.mu2 /= (N - split);

  for (int i = 0; i < split; ++i) {
    VectorXd d = data[idx[i]] - p.mu1;
    p.sigma1 += d * d.transpose();
  }
  for (int i = split; i < N; ++i) {
    VectorXd d = data[idx[i]] - p.mu2;
    p.sigma2 += d * d.transpose();
  }
  p.sigma1 = p.sigma1 / split + opts.reg * MatrixXd::Identity(D, D);
  p.sigma2 =
      p.sigma2 / (N - split) + opts.reg * MatrixXd::Identity(D, D);

  // --- EM loop ---
  std::vector<double> r(N, 0.0);
  double prev_ll = -std::numeric_limits<double>::infinity();
  int iter = 0;

  for (; iter < opts.max_iter; ++iter) {
    // Precompute \Sigma^{-1} and log|\Sigma| via LDLT decomposition
    LDLT<MatrixXd> ldlt1(p.sigma1);
    LDLT<MatrixXd> ldlt2(p.sigma2);

    if (ldlt1.info() != Eigen::Success || ldlt2.info() != Eigen::Success) {
      std::cerr << "GMM: covariance not positive-definite at iteration " << iter
                << ". Increasing regularisation.\n";
      p.sigma1 += opts.reg * MatrixXd::Identity(D, D);
      p.sigma2 += opts.reg * MatrixXd::Identity(D, D);
      continue;
    }

    MatrixXd s1_inv = ldlt1.solve(MatrixXd::Identity(D, D));
    MatrixXd s2_inv = ldlt2.solve(MatrixXd::Identity(D, D));
    // log|\Sigma| = sum of logs of the diagonal of D in the LDLT factorisation
    double ld1 = ldlt1.vectorD().array().log().sum();
    double ld2 = ldlt2.vectorD().array().log().sum();

    // E-step: compute r_i^{(p)} = P(z_i = strategic | x_i, \theta^{(p)})
    // Eq. (10): r_i = [ \pi^{(p)} f_strategic(x_i) ] /
    //                 [ \pi^{(p)} f_strategic(x_i) + (1 - \pi^{(p)})
    //                 f_reactive(x_i) ]
    // Works in log-space throughout to avoid floating-point underflow;
    // log_p1 and log_p2 are the logs of the two terms in Eq. (10), not the
    // terms themselves.
    for (int i = 0; i < N; ++i) {
      // Known-reactive observations (e.g. fill-cancelled orders) are pinned
      // to r_i = 0, they inform the reactive component but can never be
      // assigned to the strategic component.
      if (!opts.fixed_reactive.empty() && opts.fixed_reactive[i]) {
        r[i] = 0.0;
        continue;
      }
      // log[ \pi^{(p)} * f_strategic(x_i | \theta^{(p)}) ]  (numerator of Eq.
      // 10)
      double log_p1 =
          std::log(p.pi) + LogGaussianPdf(data[i], p.mu1, s1_inv, ld1);
      // log[ (1 - \pi^{(p)}) * f_reactive(x_i | \theta^{(p)}) ]  (second
      // denominator term)
      double log_p2 =
          std::log(1.0 - p.pi) + LogGaussianPdf(data[i], p.mu2, s2_inv, ld2);
      // log-sum-exp: subtract max before exp() to prevent underflow;
      // equivalent to p1 / (p1 + p2) from Eq. (10) without ever materialising
      // p1, p2
      double log_max = std::max(log_p1, log_p2);
      double sum_exp = std::exp(log_p1 - log_max) + std::exp(log_p2 - log_max);
      r[i] = std::exp(log_p1 - log_max) / sum_exp;  ///< r_i^{(p)} \in [0, 1]
    }

    // M-step: update \theta using r_i as soft weights
    double sum_r1 = 0.0, sum_r2 = 0.0;
    for (int i = 0; i < N; ++i) {
      sum_r1 += r[i];
      sum_r2 += (1.0 - r[i]);
    }

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

  // --- Build result ---
  double pi_spoof = 0.0;
  for (double ri : r) pi_spoof += ri;
  pi_spoof /= N;

  return GMMResult{p, r, pi_spoof, prev_ll, iter};
}
