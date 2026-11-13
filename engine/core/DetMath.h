#pragma once

// Deterministic transcendental functions for the simulation.
//
// The C/C++ math libraries (MSVC CRT, glibc, libc++) do not round sin/cos/atan2/exp
// identically, so the same run could diverge between Windows and Linux builds. These
// versions only use +, -, *, / and sqrt in double precision, which IEEE 754 defines
// exactly, plus exact operations (floor, ldexp). Results are therefore bit-identical on
// every compiler that does not contract or reorder floating-point operations (no fast-math,
// FP contraction disabled: see CMakeLists.txt). Accuracy is about 1e-15 in double, far
// below what a float can represent.

#include <cmath>
#include <cstdint>

namespace cosmic::dmath {

namespace detail {
constexpr double kPiOver2Hi = 1.57079632679489655800e+00;  // first 33 bits of pi/2
constexpr double kPiOver2Lo = 6.12323399573676603587e-17;  // pi/2 - kPiOver2Hi
constexpr double kTwoOverPi = 6.36619772367581382433e-01;

/// sin(r) for |r| <= pi/4 (Taylor series, enough terms for double precision).
inline double sinKernel(double r) {
    double r2 = r * r;
    return r * (1.0 + r2 * (-1.0 / 6.0 + r2 * (1.0 / 120.0 + r2 * (-1.0 / 5040.0 + r2 * (1.0 / 362880.0 +
           r2 * (-1.0 / 39916800.0 + r2 * (1.0 / 6227020800.0 + r2 * (-1.0 / 1307674368000.0))))))));
}

/// cos(r) for |r| <= pi/4.
inline double cosKernel(double r) {
    double r2 = r * r;
    return 1.0 + r2 * (-0.5 + r2 * (1.0 / 24.0 + r2 * (-1.0 / 720.0 + r2 * (1.0 / 40320.0 + r2 * (-1.0 / 3628800.0 +
           r2 * (1.0 / 479001600.0 + r2 * (-1.0 / 87178291200.0 + r2 * (1.0 / 20922789888000.0))))))));
}

/// Splits x = k * pi/2 + r with |r| <= pi/4 (Cody-Waite reduction; fine for the angles a
/// game uses, |x| < 1e6).
inline double reduce(double x, int64_t& quadrant) {
    double k = std::floor(x * kTwoOverPi + 0.5);
    quadrant = static_cast<int64_t>(k);
    return (x - k * kPiOver2Hi) - k * kPiOver2Lo;
}

/// atan(x) for 0 <= x <= 1 (argument halving then series).
inline double atanUnit(double x) {
    // atan(x) = 2 * atan(x / (1 + sqrt(1 + x^2))): two halvings bring x below tan(pi/16).
    double y = x / (1.0 + std::sqrt(1.0 + x * x));
    y = y / (1.0 + std::sqrt(1.0 + y * y));
    double y2 = y * y;
    // Series sum_{n} (-1)^n y^(2n+1) / (2n+1), |y| <= 0.199: 12 terms reach 1e-17.
    double term = y, sum = 0.0;
    for (int n = 0; n < 12; ++n) {
        sum += term / static_cast<double>(2 * n + 1);
        term *= -y2;
    }
    return 4.0 * sum;
}
}  // namespace detail

inline double sin(double x) {
    if (!std::isfinite(x)) return x - x;  // NaN
    int64_t q = 0;
    double r = detail::reduce(x, q);
    switch (q & 3) {
        case 0: return detail::sinKernel(r);
        case 1: return detail::cosKernel(r);
        case 2: return -detail::sinKernel(r);
        default: return -detail::cosKernel(r);
    }
}

inline double cos(double x) {
    if (!std::isfinite(x)) return x - x;
    int64_t q = 0;
    double r = detail::reduce(x, q);
    switch (q & 3) {
        case 0: return detail::cosKernel(r);
        case 1: return -detail::sinKernel(r);
        case 2: return -detail::cosKernel(r);
        default: return detail::sinKernel(r);
    }
}

inline double atan2(double y, double x) {
    constexpr double kPi = 3.14159265358979311600e+00;
    if (std::isnan(x) || std::isnan(y)) return x + y;
    if (x == 0.0 && y == 0.0) return std::signbit(x) ? (std::signbit(y) ? -kPi : kPi) : (std::signbit(y) ? -0.0 : 0.0);
    double ax = std::fabs(x), ay = std::fabs(y);
    double a;
    if (ay <= ax) a = detail::atanUnit(ay / ax);
    else a = kPi * 0.5 - detail::atanUnit(ax / ay);
    if (x < 0.0) a = kPi - a;
    return std::signbit(y) ? -a : a;
}

inline double exp(double x) {
    constexpr double kLn2Hi = 6.93147180369123816490e-01;
    constexpr double kLn2Lo = 1.90821492927058770002e-10;
    constexpr double kInvLn2 = 1.44269504088896338700e+00;
    if (std::isnan(x)) return x;
    if (x > 709.0) return HUGE_VAL;
    if (x < -745.0) return 0.0;
    double k = std::floor(x * kInvLn2 + 0.5);
    double r = (x - k * kLn2Hi) - k * kLn2Lo;  // |r| <= ln2 / 2
    // Taylor series of e^r up to r^13 (|r| <= 0.347: error < 1e-17).
    double term = 1.0, sum = 1.0;
    for (int n = 1; n <= 13; ++n) {
        term *= r / static_cast<double>(n);
        sum += term;
    }
    return std::ldexp(sum, static_cast<int>(k));
}

// Float overloads used by the gameplay code.
inline float sin(float x) { return static_cast<float>(sin(static_cast<double>(x))); }
inline float cos(float x) { return static_cast<float>(cos(static_cast<double>(x))); }
inline float atan2(float y, float x) { return static_cast<float>(atan2(static_cast<double>(y), static_cast<double>(x))); }
inline float exp(float x) { return static_cast<float>(exp(static_cast<double>(x))); }

}  // namespace cosmic::dmath
