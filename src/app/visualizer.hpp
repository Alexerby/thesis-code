/**
 * @file visualizer.hpp
 * @brief Feature distribution plots for exploratory analysis.
 *
 * Generates histograms of the raw feature distributions so that
 * modelling decisions (log transforms, feature selection) can be
 * made from evidence rather than assumption.
 */

#pragma once

#include "model/gmm.hpp"
#include <string>
#include <vector>

/**
 * @brief Detailed inspection of the order-age feature (\Delta t_i).
 *
 * Generates four plots, each saved as a PNG:
 *  1. Raw microseconds, clipped at the 99th percentile (bulk view).
 *  2. Raw microseconds, clipped at the 99.9th percentile (tail view).
 *  3. log10(\Delta t_i [us]) — standard HFT analysis scale.
 *  4. log1p(\Delta t_i [us]) — the transform used inside the GMM.
 *
 * All x-axes are expressed in microseconds.
 *
 * @param records    Feature records collected from the order tracker.
 * @param output_dir Directory to write plots into (created if absent).
 */
void InspectOrderAge(const std::vector<FeatureRecord> &records,
                     const std::string &output_dir = "features/descriptives/order_age");

/**
 * @brief Runs the full descriptive analysis suite across all features.
 *
 * Dispatches to one InspectXxx() function per feature, each of which
 * writes its plots into its own subdirectory under @p base_dir.
 *
 * @param records   Feature records collected from the order tracker.
 * @param base_dir  Root directory; one subdir per feature is created inside.
 */
void RunVisualizer(const std::vector<FeatureRecord> &records,
                   const std::string &base_dir = "features/descriptives");
