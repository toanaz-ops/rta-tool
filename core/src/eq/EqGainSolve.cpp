// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/eq/EqGainSolve.h"

#include "rta/eq/BiquadDesign.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rta::eq {

namespace {

/// A dense NxN Cholesky factorisation (A = L L^T) and the forward/backward
/// substitution solve, for the SPD systems this file only ever builds
/// (S^T W S + lambda*I -- ridge-regularised, so SPD by construction as long
/// as at least one weight is positive, EqGainSolve.h's own throw guarantees
/// N <= 16). No external linear-algebra dependency exists anywhere in
/// core/ (ButterworthDesign.h's own doc comment: closed form, no runtime
/// scipy) -- this is the smallest thing that solves a dense SPD system in
/// double, not a general-purpose library substitute.
class Cholesky {
public:
    explicit Cholesky(const std::vector<std::vector<double>>& a) : n_(a.size()), l_(n_, std::vector<double>(n_, 0.0)) {
        for (std::size_t i = 0; i < n_; ++i) {
            for (std::size_t j = 0; j <= i; ++j) {
                double sum = a[i][j];
                for (std::size_t k = 0; k < j; ++k) sum -= l_[i][k] * l_[j][k];
                if (i == j) {
                    if (sum <= 0.0) {
                        throw std::invalid_argument(
                            "solveGains: S^T W S + lambda*I is not positive definite -- "
                            "check that at least one bin has a positive weight");
                    }
                    l_[i][j] = std::sqrt(sum);
                } else {
                    l_[i][j] = sum / l_[j][j];
                }
            }
        }
    }

    [[nodiscard]] std::vector<double> solve(const std::vector<double>& b) const {
        std::vector<double> y(n_, 0.0);
        for (std::size_t i = 0; i < n_; ++i) {
            double sum = b[i];
            for (std::size_t k = 0; k < i; ++k) sum -= l_[i][k] * y[k];
            y[i] = sum / l_[i][i];
        }
        std::vector<double> x(n_, 0.0);
        for (std::size_t ii = 0; ii < n_; ++ii) {
            const std::size_t i = n_ - 1 - ii;
            double sum = y[i];
            for (std::size_t k = i + 1; k < n_; ++k) sum -= l_[k][i] * x[k];
            x[i] = sum / l_[i][i];
        }
        return x;
    }

private:
    std::size_t n_;
    std::vector<std::vector<double>> l_;
};

std::vector<double> matVec(const std::vector<std::vector<double>>& a, const std::vector<double>& v) {
    const std::size_t n = a.size();
    std::vector<double> out(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < n; ++j) sum += a[i][j] * v[j];
        out[i] = sum;
    }
    return out;
}

double norm(const std::vector<double>& v) {
    double sum = 0.0;
    for (double x : v) sum += x * x;
    return std::sqrt(sum);
}

void normalizeInPlace(std::vector<double>& v) {
    const double n = norm(v);
    if (n > 0.0) {
        for (double& x : v) x /= n;
    }
}

/// Largest eigenvalue of the SPD matrix `a`, by plain power iteration --
/// cheap and exact enough for a condition-number DIAGNOSTIC on an N<=16
/// matrix (the golden carries it so a reader can judge collinearity, not so
/// a downstream test asserts a tight digit of it).
double largestEigenvalue(const std::vector<std::vector<double>>& a) {
    const std::size_t n = a.size();
    std::vector<double> v(n, 1.0);
    normalizeInPlace(v);
    double eigenvalue = 0.0;
    for (int iter = 0; iter < 200; ++iter) {
        const auto av = matVec(a, v);
        const double newEigenvalue = norm(av);
        if (newEigenvalue <= 0.0) return 0.0;
        v = av;
        normalizeInPlace(v);
        if (std::abs(newEigenvalue - eigenvalue) <= 1e-13 * std::max(newEigenvalue, 1.0)) {
            eigenvalue = newEigenvalue;
            break;
        }
        eigenvalue = newEigenvalue;
    }
    return eigenvalue;
}

/// Smallest eigenvalue, by inverse power iteration -- reuses the SAME
/// Cholesky factor the gain solve already computed (A^-1 * v via one
/// forward/backward substitution per step), so this costs no second
/// factorisation.
double smallestEigenvalue(const Cholesky& factorization, std::size_t n) {
    std::vector<double> v(n, 1.0);
    normalizeInPlace(v);
    double eigenvalueOfInverse = 0.0;
    for (int iter = 0; iter < 200; ++iter) {
        const auto solved = factorization.solve(v);
        const double newEigenvalueOfInverse = norm(solved);
        if (newEigenvalueOfInverse <= 0.0) break;
        v = solved;
        normalizeInPlace(v);
        if (std::abs(newEigenvalueOfInverse - eigenvalueOfInverse) <=
            1e-13 * std::max(newEigenvalueOfInverse, 1.0)) {
            eigenvalueOfInverse = newEigenvalueOfInverse;
            break;
        }
        eigenvalueOfInverse = newEigenvalueOfInverse;
    }
    return (eigenvalueOfInverse > 0.0) ? 1.0 / eigenvalueOfInverse : 0.0;
}

void validate(std::span<const FilterSpec> placed, const EqInput& input) {
    if (placed.empty()) {
        throw std::invalid_argument("solveGains: placed must not be empty");
    }
    if (placed.size() > 16) {
        throw std::invalid_argument("solveGains: placed.size() must not exceed 16 (record Sec.3)");
    }
    const std::size_t m = input.hz.size();
    if (input.residualDb.size() != m || input.coherence.size() != m ||
        input.trusted.size() != m || input.excluded.size() != m) {
        throw std::invalid_argument(
            "solveGains: hz, residualDb, coherence, trusted and excluded must be the same length");
    }
    if (input.sampleRate <= 0.0) {
        throw std::invalid_argument("solveGains: sampleRate must be positive");
    }
    if (input.gCapDb <= 0.1) {
        throw std::invalid_argument(
            "solveGains: gCapDb must exceed 0.1 dB (lambda's own denominator)");
    }
}

}  // namespace

GainSolve solveGains(std::span<const FilterSpec> placed, const EqInput& input) {
    validate(placed, input);

    const std::size_t m = input.hz.size();
    const std::size_t n = placed.size();

    // Weights: gamma^2/f on trusted, non-excluded, f>0 bins (record Sec.3).
    std::vector<double> weight(m, 0.0);
    for (std::size_t k = 0; k < m; ++k) {
        if (input.trusted[k] && !input.excluded[k] && input.hz[k] > 0.0f) {
            weight[k] = static_cast<double>(input.coherence[k]) / static_cast<double>(input.hz[k]);
        }
    }

    // S: M x N, s_i(f_k) = responseDb({type_i, fc_i, Q_i, 1 dB}, fs, f_k)
    // (EQ-R5 -- Wave 0's designBiquad/responseDb, exact, never re-derived).
    std::vector<std::vector<double>> s(m, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
        const FilterSpec unitGain{ placed[i].type, placed[i].fcHz, placed[i].q, 1.0 };
        for (std::size_t k = 0; k < m; ++k) {
            s[k][i] = responseDb(unitGain, input.sampleRate, static_cast<double>(input.hz[k]));
        }
    }

    // A = S^T W S, rhs = S^T W r.
    std::vector<std::vector<double>> a(n, std::vector<double>(n, 0.0));
    std::vector<double> rhs(n, 0.0);
    for (std::size_t k = 0; k < m; ++k) {
        if (weight[k] <= 0.0) continue;
        const double w = weight[k];
        const double r = static_cast<double>(input.residualDb[k]);
        for (std::size_t i = 0; i < n; ++i) {
            rhs[i] += w * s[k][i] * r;
            for (std::size_t j = 0; j < n; ++j) a[i][j] += w * s[k][i] * s[k][j];
        }
    }

    // lambda, derived from the smallest placed filter's own weighted column
    // norm (record Sec.3: shrinkage at the cap is exactly 0.1 dB for the
    // filter ridge would shrink MOST).
    double minDiagonal = a[0][0];
    for (std::size_t i = 1; i < n; ++i) minDiagonal = std::min(minDiagonal, a[i][i]);
    const double lambda = 0.1 * minDiagonal / (input.gCapDb - 0.1);

    std::vector<std::vector<double>> regularized = a;
    for (std::size_t i = 0; i < n; ++i) regularized[i][i] += lambda;

    const Cholesky factorization(regularized);
    const auto gains = factorization.solve(rhs);

    GainSolve result;
    result.gainsDb = gains;
    const double largest = largestEigenvalue(regularized);
    const double smallest = smallestEigenvalue(factorization, n);
    result.conditionNumber = (smallest > 0.0) ? (largest / smallest) : std::numeric_limits<double>::infinity();
    return result;
}

}  // namespace rta::eq
