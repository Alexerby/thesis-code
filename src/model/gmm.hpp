/**
 * @file gmm.hpp
 * @brief Two-component Gaussian Mixture Model fitted via the EM algorithm.
 *
 * Implements the unsupervised identification strategy from the empirical
 * chapter. The population of cancelled orders is modelled as a mixture of
 * two multivariate Gaussians — an anomalous component and a liquidity-consistent component
 * — whose parameters \theta = {\pi, \mu_1, \Sigma_1, \mu_2, \Sigma_2} are
 * estimated by maximum likelihood via Expectation Maximisation (Dempster 1977).
 */

#pragma once

#include <Eigen/Dense>
#include <vector>

#include "features/order_tracker.hpp"

/**
 * @struct GMMParams
 * @brief Estimated parameters \theta = {\pi, \mu_1, \Sigma_1, \mu_2, \Sigma_2}.
 *
 * Component 1 = anomalous, Component 2 = liquidity-consistent.
 */
struct GMMParams {
  double pi;               ///< Mixing weight for the anomalous component
  Eigen::VectorXd mu1;     ///< Mean of the anomalous component
  Eigen::VectorXd mu2;     ///< Mean of the liquidity-consistent component
  Eigen::MatrixXd sigma1;  ///< Covariance of the anomalous component
  Eigen::MatrixXd sigma2;  ///< Covariance of the liquidity-consistent component
};

/**
 * @struct RestartSummary
 * @brief Per-restart diagnostics from a GMM::Fit() call.
 */
struct RestartSummary {
  int restart;
  double log_likelihood;
  double pi_spoof;
  int iterations;
};

/**
 * @struct GMMResult
 * @brief Output of a single GMM::Fit() call.
 */
struct GMMResult {
  GMMParams params;
  std::vector<double> responsibilities;  ///< r_i \in [0,1] per observation
  double pi_spoof;                       ///< \hat{\pi}_spoof = mean(r_i)
  double log_likelihood;
  int iterations;
  int best_restart;                          ///< Which restart index won
  std::vector<RestartSummary> restarts;      ///< One entry per K-means restart
};

/**
 * @struct FitOptions
 * @brief Tuning parameters for the EM algorithm.
 */
struct FitOptions {
  int max_iter = 300;
  double tol = 1e-6;  ///< Convergence threshold on log-likelihood change
  double reg = 1e-6;  ///< Ridge added to \Sigma diagonal to prevent singularity
  int n_init = 10;    ///< Number of K-means restarts; best log-likelihood is kept

  /// Optional per-observation constraint. If fixed_lc[i] == true,
  /// r_i is forced to 0 in every E-step (observation is known liquidity-consistent).
  /// Must be empty or the same length as the data passed to Fit().
  std::vector<bool> fixed_lc;
};

/**
 * @class GMM
 * @brief Fits a two-component Gaussian Mixture Model via the EM algorithm.
 *
 * @details
 * The model assumes each observation \f$ x_i \f$ is drawn from
 * \f[
 *   p(x_i \mid \theta) = \pi \, \mathcal{N}(x_i \mid \mu_1, \Sigma_1)
 *                      + (1-\pi) \, \mathcal{N}(x_i \mid \mu_2, \Sigma_2),
 * \f]
 * where component 1 is the **anomalous** (fast-cancel) cluster and
 * component 2 is the **liquidity-consistent** cluster.
 * Parameters \f$ \theta = \{\pi, \mu_1, \Sigma_1, \mu_2, \Sigma_2\} \f$ are
 * estimated by maximum likelihood via the EM algorithm (Dempster et al. 1977).
 */
class GMM {
 public:
  /**
   * @brief Runs the EM algorithm on \f$ N \f$ observations of dimension
   *        \f$ D \f$.
   *
   * @details
   * **Initialisation** — observations are sorted on the first feature;
   * the bottom 20 % are assigned to component 1 (anomalous).
   *
   * **E-step** — compute the posterior responsibility of component 1 for
   * each observation:
   * \f[
   *   r_i = \frac{\pi \, \mathcal{N}(x_i \mid \mu_1, \Sigma_1)}
   *              {\pi \, \mathcal{N}(x_i \mid \mu_1, \Sigma_1)
   *               + (1-\pi) \, \mathcal{N}(x_i \mid \mu_2, \Sigma_2)}.
   * \f]
   * If `opts.fixed_lc[i]` is true the responsibility is clamped to
   * \f$ r_i = 0 \f$.
   *
   * **M-step** — update parameters using the soft counts
   * \f$ N_k = \sum_i r_i^{(k)} \f$:
   * \f[
   *   \hat\pi = \frac{N_1}{N}, \quad
   *   \hat\mu_k = \frac{1}{N_k}\sum_i r_i^{(k)} x_i, \quad
   *   \hat\Sigma_k = \frac{1}{N_k}\sum_i r_i^{(k)}(x_i-\hat\mu_k)(x_i-\hat\mu_k)^\top + \lambda I,
   * \f]
   * where \f$ \lambda \f$ = `opts.reg` is a ridge term preventing singularity.
   *
   * Iteration stops when \f$ |\ell^{(t)} - \ell^{(t-1)}| < \texttt{tol} \f$
   * or `max_iter` is reached.
   *
   * @param data  \f$ N \f$ observations, each a \f$ n \f$-dimensional vector.
   * @param opts  Tuning parameters (iterations, tolerance, regularisation).
   * @return      GMMResult with fitted \f$\theta\f$, responsibilities, and
   *              \f$\hat\pi_{\text{spoof}} = \frac{1}{N}\sum_i r_i\f$.
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
  static std::vector<Eigen::VectorXd> ToEigen(
      const std::vector<FeatureRecord> &records,
      const std::vector<int> &feature_indices);

  /**
   * @brief Z-score standardises data in-place to zero mean and unit variance.
   *
   * @details
   * For each feature dimension \f$ d \f$:
   * \f[
   *   x_i^{(d)} \leftarrow \frac{x_i^{(d)} - \bar{x}^{(d)}}{\sigma^{(d)}},
   * \f]
   * where \f$\bar x^{(d)}\f$ and \f$\sigma^{(d)}\f$ are the sample mean and
   * standard deviation over all \f$N\f$ observations.
   * This prevents features with large absolute values from dominating the
   * covariance structure.
   *
   * @param data  Data to standardise (modified in-place).
   * @return      \f$(\bar x,\, \sigma)\f$ vectors so fitted means can be
   *              back-transformed via \f$\hat\mu_k^{(d)} \cdot \sigma^{(d)}
   *              + \bar x^{(d)}\f$ if needed.
   */
  static std::pair<Eigen::VectorXd, Eigen::VectorXd> Standardize(
      std::vector<Eigen::VectorXd> &data);

 private:
  /**
   * @brief Evaluates \f$\log \mathcal{N}(x \mid \mu, \Sigma)\f$ using a
   *        precomputed inverse and log-determinant.
   *
   * @details
   * \f[
   *   \log \mathcal{N}(x \mid \mu, \Sigma)
   *     = -\frac{1}{2}\bigl[n\log 2\pi
   *       + \log|\Sigma|
   *       + (x-\mu)^\top \Sigma^{-1}(x-\mu)\bigr].
   * \f]
   * \f$\Sigma^{-1}\f$ and \f$\log|\Sigma|\f$ are passed in pre-computed
   * (via LDLT decomposition) so the M-step can reuse them across all \f$N\f$
   * observations without redundant factorisations.
   *
   * @param x        Observation vector \f$x \in \mathbb{R}^n\f$.
   * @param mu       Component mean \f$\mu\f$.
   * @param sigma_inv Precomputed \f$\Sigma^{-1}\f$.
   * @param log_det  Precomputed \f$\log|\Sigma|\f$.
   * @return         Scalar log-density.
   */
  static double LogGaussianPdf(const Eigen::VectorXd &x,
                               const Eigen::VectorXd &mu,
                               const Eigen::MatrixXd &sigma_inv,
                               double log_det);

  /**
   * @brief Evaluates the observed-data log-likelihood
   *        \f$\ell(\theta) = \sum_i \log p(x_i \mid \theta)\f$.
   *
   * @details
   * \f[
   *   \ell(\theta) = \sum_{i=1}^{N}
   *     \log\!\Bigl[
   *       \pi \, \mathcal{N}(x_i \mid \mu_1, \Sigma_1)
   *       + (1-\pi) \, \mathcal{N}(x_i \mid \mu_2, \Sigma_2)
   *     \Bigr].
   * \f]
   * The log-sum-exp is evaluated directly (no further numerical stabilisation
   * is applied beyond the precomputed LDLT inverses).
   *
   * @param data  Observations.
   * @param p     Current parameter estimates.
   * @param s1_inv Precomputed \f$\Sigma_1^{-1}\f$.
   * @param ld1   Precomputed \f$\log|\Sigma_1|\f$.
   * @param s2_inv Precomputed \f$\Sigma_2^{-1}\f$.
   * @param ld2   Precomputed \f$\log|\Sigma_2|\f$.
   * @return      Scalar \f$\ell(\theta)\f$.
   */
  static double LogLikelihood(const std::vector<Eigen::VectorXd> &data,
                              const GMMParams &p, const Eigen::MatrixXd &s1_inv,
                              double ld1, const Eigen::MatrixXd &s2_inv,
                              double ld2);

  /**
   * @brief Initialises GMM parameters via K-means with a given random seed.
   *
   * @details
   * Runs Lloyd's K-means algorithm (K=2) for up to 100 iterations using two
   * randomly chosen data points as starting centroids. The resulting hard
   * cluster assignments are used to compute \f$\pi\f$, \f$\mu_k\f$, and
   * \f$\Sigma_k\f$ for the subsequent EM run.
   *
   * @param data  Standardised observations.
   * @param seed  RNG seed — each restart passes a different value.
   * @param reg   Ridge regularisation applied to initial covariances.
   * @return      Initialised GMMParams ready for the EM loop.
   */
  GMMParams KMeansInit(const std::vector<Eigen::VectorXd> &data, int seed,
                       double reg) const;
};
