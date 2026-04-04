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

// ---------------------------------------------------------------------------
// Private static helpers
// ---------------------------------------------------------------------------

/**
 * Evaluates log N(x | \mu, \Sigma) given precomputed \Sigma^{-1} and
 * log|\Sigma|. Using log-space avoids underflow for high-dimensional data.
 */
double GMM::LogGaussianPdf(const Eigen::VectorXd &x,
                            const Eigen::VectorXd &mu,
                            const Eigen::MatrixXd &sigma_inv,
                            double log_det) {
  int D = static_cast<int>(x.size());
  Eigen::VectorXd diff = x - mu;
  double mahal = diff.transpose() * sigma_inv * diff;
  return -0.5 * (D * std::log(2.0 * M_PI) + log_det + mahal);
}

/**
 * Evaluates the observed-data log-likelihood
 *   log L(\theta) = \sum_i log[ \pi f_1(x_i) + (1-\pi) f_2(x_i) ]
 * using log-sum-exp to avoid numerical underflow.
 */
double GMM::LogLikelihood(const std::vector<Eigen::VectorXd> &data,
                           const GMMParams &p,
                           const Eigen::MatrixXd &s1_inv, double ld1,
                           const Eigen::MatrixXd &s2_inv, double ld2) {
  double ll = 0.0;
  for (const auto &x : data) {
    double log_p1 = std::log(p.pi) + LogGaussianPdf(x, p.mu1, s1_inv, ld1);
    double log_p2 = std::log(1.0 - p.pi) + LogGaussianPdf(x, p.mu2, s2_inv, ld2);
    // log-sum-exp for numerical stability
    double log_max = std::max(log_p1, log_p2);
    ll += log_max + std::log(std::exp(log_p1 - log_max) +
                             std::exp(log_p2 - log_max));
  }
  return ll;
}

// ---------------------------------------------------------------------------
// ToEigen
// ---------------------------------------------------------------------------

std::vector<Eigen::VectorXd>
GMM::ToEigen(const std::vector<FeatureRecord> &records,
             const std::vector<int> &feature_indices) {
  std::vector<Eigen::VectorXd> out;
  out.reserve(records.size());

  int D = static_cast<int>(feature_indices.size());
  for (const auto &r : records) {
    const double all[6] = {r.delta_t,  r.delta_imbalance, r.size_ratio,
                           r.queue_pos, r.dist_touch,       r.cancel_rate};
    Eigen::VectorXd v(D);
    for (int j = 0; j < D; ++j) {
      v[j] = all[feature_indices[j]];
    }
    out.push_back(std::move(v));
  }
  return out;
}

// ---------------------------------------------------------------------------
// Fit
// ---------------------------------------------------------------------------

GMMResult GMM::Fit(const std::vector<Eigen::VectorXd> &data,
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
  p.pi     = static_cast<double>(split) / N;
  p.mu1    = Eigen::VectorXd::Zero(D);
  p.mu2    = Eigen::VectorXd::Zero(D);
  p.sigma1 = Eigen::MatrixXd::Identity(D, D);
  p.sigma2 = Eigen::MatrixXd::Identity(D, D);

  for (int i = 0; i < split; ++i) p.mu1 += data[idx[i]];
  for (int i = split; i < N; ++i) p.mu2 += data[idx[i]];
  p.mu1 /= split;
  p.mu2 /= (N - split);

  for (int i = 0; i < split; ++i) {
    Eigen::VectorXd d = data[idx[i]] - p.mu1;
    p.sigma1 += d * d.transpose();
  }
  for (int i = split; i < N; ++i) {
    Eigen::VectorXd d = data[idx[i]] - p.mu2;
    p.sigma2 += d * d.transpose();
  }
  p.sigma1 = p.sigma1 / split + opts.reg * Eigen::MatrixXd::Identity(D, D);
  p.sigma2 = p.sigma2 / (N - split) + opts.reg * Eigen::MatrixXd::Identity(D, D);

  // --- EM loop ---
  std::vector<double> r(N, 0.0);
  double prev_ll = -std::numeric_limits<double>::infinity();
  int iter = 0;

  for (; iter < opts.max_iter; ++iter) {
    // Precompute \Sigma^{-1} and log|\Sigma| via LDLT decomposition
    Eigen::LDLT<Eigen::MatrixXd> ldlt1(p.sigma1);
    Eigen::LDLT<Eigen::MatrixXd> ldlt2(p.sigma2);

    if (ldlt1.info() != Eigen::Success || ldlt2.info() != Eigen::Success) {
      std::cerr << "GMM: covariance not positive-definite at iteration "
                << iter << ". Increasing regularisation.\n";
      p.sigma1 += opts.reg * Eigen::MatrixXd::Identity(D, D);
      p.sigma2 += opts.reg * Eigen::MatrixXd::Identity(D, D);
      continue;
    }

    Eigen::MatrixXd s1_inv = ldlt1.solve(Eigen::MatrixXd::Identity(D, D));
    Eigen::MatrixXd s2_inv = ldlt2.solve(Eigen::MatrixXd::Identity(D, D));
    // log|\Sigma| = sum of logs of the diagonal of D in the LDLT factorisation
    double ld1 = ldlt1.vectorD().array().log().sum();
    double ld2 = ldlt2.vectorD().array().log().sum();

    // E-step: r_i = P(strategic | x_i, \theta^{(p)})
    for (int i = 0; i < N; ++i) {
      double log_p1 = std::log(p.pi) +
                      LogGaussianPdf(data[i], p.mu1, s1_inv, ld1);
      double log_p2 = std::log(1.0 - p.pi) +
                      LogGaussianPdf(data[i], p.mu2, s2_inv, ld2);
      double log_max = std::max(log_p1, log_p2);
      double sum_exp = std::exp(log_p1 - log_max) + std::exp(log_p2 - log_max);
      r[i] = std::exp(log_p1 - log_max) / sum_exp;
    }

    // M-step: update \theta using r_i as soft weights
    double sum_r1 = 0.0, sum_r2 = 0.0;
    for (int i = 0; i < N; ++i) {
      sum_r1 += r[i];
      sum_r2 += (1.0 - r[i]);
    }

    p.pi = std::clamp(sum_r1 / N, 1e-6, 1.0 - 1e-6);

    Eigen::VectorXd mu1_new = Eigen::VectorXd::Zero(D);
    Eigen::VectorXd mu2_new = Eigen::VectorXd::Zero(D);
    for (int i = 0; i < N; ++i) {
      mu1_new += r[i]          * data[i];
      mu2_new += (1.0 - r[i]) * data[i];
    }
    p.mu1 = mu1_new / sum_r1;
    p.mu2 = mu2_new / sum_r2;

    Eigen::MatrixXd s1_new = opts.reg * Eigen::MatrixXd::Identity(D, D);
    Eigen::MatrixXd s2_new = opts.reg * Eigen::MatrixXd::Identity(D, D);
    for (int i = 0; i < N; ++i) {
      Eigen::VectorXd d1 = data[i] - p.mu1;
      Eigen::VectorXd d2 = data[i] - p.mu2;
      s1_new += r[i]          * (d1 * d1.transpose());
      s2_new += (1.0 - r[i]) * (d2 * d2.transpose());
    }
    p.sigma1 = s1_new / sum_r1;
    p.sigma2 = s2_new / sum_r2;

    // Convergence check: |log L(\theta^{(p+1)}) - log L(\theta^{(p)})| < tol
    Eigen::LDLT<Eigen::MatrixXd> c1(p.sigma1), c2(p.sigma2);
    Eigen::MatrixXd cs1_inv = c1.solve(Eigen::MatrixXd::Identity(D, D));
    Eigen::MatrixXd cs2_inv = c2.solve(Eigen::MatrixXd::Identity(D, D));
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
