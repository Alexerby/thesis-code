/**
 * @file gmm.hpp
 * @brief Two-component Gaussian Mixture Model fitted via the EM algorithm.
 *
 * Implements the unsupervised identification strategy from the empirical
 * chapter. The population of cancelled orders is modelled as a mixture of
 * two multivariate Gaussians — a strategic component and a reactive component
 * — whose parameters \theta = {\pi, \mu_1, \Sigma_1, \mu_2, \Sigma_2} are
 * estimated by maximum likelihood via Expectation Maximisation (Dempster 1977).
 */

// TODO: Initialization is currently not using K-Means.

#pragma once

#include <Eigen/Dense>
#include <vector>

/**
 * @struct FeatureRecord
 * @brief Feature vector x_i for a single order lifecycle.
 *
 * Index mapping for use with GMM::ToEigen:
 *   0 = delta_t          (\Delta t_i,   order age in nanoseconds)
 *   1 = delta_imbalance  (\Delta I_i,   imbalance change over lifetime)
 *   2 = size_ratio       (size relative to best-level depth)
 *   3 = queue_pos        (queue position at placement)
 *   4 = dist_touch       (distance from same-side best price)
 *   5 = cancel_rate      (local cancel rate in +-500ms window)
 */
struct FeatureRecord {
  double delta_t;         ///< \Delta t_i
  double delta_imbalance; ///< \Delta I_i
  double size_ratio;
  double queue_pos;
  double dist_touch;
  double cancel_rate;
};

/**
 * @struct GMMParams
 * @brief Estimated parameters \theta = {\pi, \mu_1, \Sigma_1, \mu_2, \Sigma_2}.
 *
 * Component 1 = strategic, Component 2 = reactive.
 */
struct GMMParams {
  double pi;              ///< Mixing weight for the strategic component
  Eigen::VectorXd mu1;    ///< Mean of the strategic component
  Eigen::VectorXd mu2;    ///< Mean of the reactive component
  Eigen::MatrixXd sigma1; ///< Covariance of the strategic component
  Eigen::MatrixXd sigma2; ///< Covariance of the reactive component
};

/**
 * @struct GMMResult
 * @brief Output of a single GMM::Fit() call.
 */
struct GMMResult {
  GMMParams params;
  std::vector<double> responsibilities; ///< r_i \in [0,1] per observation
  double pi_spoof;                      ///< \hat{\pi}_spoof = mean(r_i)
  double log_likelihood;
  int iterations;
};

/**
 * @struct FitOptions
 * @brief Tuning parameters for the EM algorithm.
 */
struct FitOptions {
  int max_iter = 300;
  double tol = 1e-6; ///< Convergence threshold on log-likelihood change
  double reg = 1e-6; ///< Ridge added to \Sigma diagonal to prevent singularity
};

/**
 * @class GMM
 * @brief Fits a two-component Gaussian Mixture Model via the EM algorithm.
 */
class GMM {
public:
  /**
   * @brief Runs the EM algorithm on a set of D-dimensional observations.
   *
   * Initialises by sorting on the first feature and assigning the bottom 20%
   * to component 1 (the fast-cancel / strategic region).
   *
   * @param data  N observations, each a D-dimensional Eigen vector.
   * @param opts  Tuning parameters (max iterations, tolerance, regularisation).
   * @return      GMMResult containing fitted parameters and responsibilities.
   */
  GMMResult Fit(const std::vector<Eigen::VectorXd> &data,
                const FitOptions &opts = FitOptions{}) const;

  /**
   * @brief Extracts selected feature indices from a FeatureRecord vector.
   *
   * @param records         Source feature records.
   * @param feature_indices Indices of features to include (e.g. {0, 1, 4}).
   * @return                Vector of Eigen vectors ready for Fit().
   */
  static std::vector<Eigen::VectorXd>
  ToEigen(const std::vector<FeatureRecord> &records,
          const std::vector<int> &feature_indices);

  /**
   * @brief Z-score standardises data in-place (zero mean, unit variance).
   *
   * Should be called on the output of ToEigen() before Fit() to prevent
   * features with large absolute values from dominating the covariance.
   *
   * @param data  Data to standardise (modified in-place).
   * @return      Pair of {mean, std_dev} vectors for each feature dimension,
   *              which can be used to back-transform the fitted means if
   * needed.
   */
  static std::pair<Eigen::VectorXd, Eigen::VectorXd>
  Standardize(std::vector<Eigen::VectorXd> &data);

  /// Human-readable names for each FeatureRecord field, indexed 0-5.
  static constexpr const char *kFeatureNames[6] = {
      "delta_t",   "delta_imbalance", "size_ratio",
      "queue_pos", "dist_touch",      "cancel_rate"};

private:
  /**
   * @brief Evaluates log N(x | \mu, \Sigma) using a precomputed inverse and
   *        log-determinant.
   */
  static double LogGaussianPdf(const Eigen::VectorXd &x,
                               const Eigen::VectorXd &mu,
                               const Eigen::MatrixXd &sigma_inv,
                               double log_det);

  /**
   * @brief Evaluates the observed-data log-likelihood log L(\theta) using
   *        precomputed inverses and log-determinants for both components.
   */
  static double LogLikelihood(const std::vector<Eigen::VectorXd> &data,
                              const GMMParams &p, const Eigen::MatrixXd &s1_inv,
                              double ld1, const Eigen::MatrixXd &s2_inv,
                              double ld2);
};
