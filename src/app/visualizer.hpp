/**
 * @file visualizer.hpp
 * @brief Feature distribution plots for exploratory analysis.
 *
 * Generates histograms of the raw feature distributions so that
 * modelling decisions (log transforms, feature selection) can be
 * made from evidence rather than assumption.
 */

#pragma once

#include <string>
#include <vector>

#include "features/order_tracker.hpp"

/**
 * @brief Detailed inspection of the order-age feature (\Delta t_i).
 *
 * Generates two plots, each saved as a PNG:
 *  1. Raw microseconds, clipped at the 99th percentile, shows the bulk
 *     distribution without the heavy tail crushing the x-axis.
 *  2. ln(\Delta t_i [us]), motivates the two-component GMM by revealing
 *     the bimodal structure that is invisible on the raw scale.
 *
 * All x-axes are expressed in microseconds.
 *
 * @param records    Feature records collected from the order tracker.
 * @param output_dir Directory to write plots into (created if absent).
 */
void InspectOrderAge(
    const std::vector<FeatureRecord> &records,
    const std::string &output_dir = "features/descriptives/order_age");

/**
 * @brief Detailed inspection of feature: Order-Induced Imbalance (\delta
 * \mathcal I_i).
 *
 * Generates plots.
 *
 * @param records    Feature records collected from the order tracker.
 * @param output_dir Directory to write plots into (created if absent).
 */

void InspectOrderInducedImbalance(
    const std::vector<FeatureRecord> &records,
    const std::string &output_dir =
        "features/descriptives/order_induced_imbalance");

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
