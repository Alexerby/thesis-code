#pragma once

#include <Eigen/Dense>
#include <string>
#include <vector>

#include "features/order_tracker.hpp"
#include "model/gmm.hpp"

/**
 * @brief Generates diagnostic plots for a fitted GMM.
 *
 * Produces six plots saved as PNGs under base_dir:
 *   responsibility_histogram.png      — distribution of r_i; U-shape = good separation
 *   responsibility_by_cancel_type.png — r_i split by fill vs pure cancel (primary validation)
 *   densities/{feature}.png           — empirical histogram + weighted Gaussian overlays per feature
 *   pairs/{feat_i}_vs_{feat_j}.png    — scatter coloured by hard assignment for each feature pair
 *   restart_likelihoods.png           — log-likelihood per EM restart
 *   intraday_pi.png                   — mean r_i binned by 30-min market-time interval (ET)
 *
 * @param records         The (possibly subsampled) records passed to GMM::Fit().
 * @param result          Output of GMM::Fit().
 * @param feature_indices Feature indices used (must match the order in data_mean/data_std).
 * @param data_mean       Per-feature mean from GMM::Standardize().
 * @param data_std        Per-feature std  from GMM::Standardize().
 * @param base_dir        Root output directory (created if absent).
 */
void RunGMMVisualizer(const std::vector<FeatureRecord>& records,
                      const GMMResult& result,
                      const std::vector<int>& feature_indices,
                      const Eigen::VectorXd& data_mean,
                      const Eigen::VectorXd& data_std,
                      const std::string& base_dir = "features/gmm_plots");
