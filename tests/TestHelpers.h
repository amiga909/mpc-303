#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace testutil
{
constexpr double pi = 3.14159265358979323846;

/** Magnitude (linear) of the component at freq, using a single-bin DFT with a Hann window. */
inline double toneMagnitude (const std::vector<double>& x, double freq, double sr)
{
    std::complex<double> acc = 0.0;
    double wsum = 0.0;
    const size_t n = x.size();
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) (n - 1));
        acc += w * x[i] * std::polar (1.0, -2.0 * pi * freq * (double) i / sr);
        wsum += w;
    }
    return 2.0 * std::abs (acc) / wsum;
}

inline double db (double lin) { return 20.0 * std::log10 (std::fmax (lin, 1e-30)); }

/** Simple radix-2 FFT magnitude spectrum (Blackman-Harris window), length must be pow2. */
inline std::vector<double> spectrum (const std::vector<double>& in)
{
    const size_t n = in.size();
    std::vector<std::complex<double>> a (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = 2.0 * pi * (double) i / (double) (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        a[i] = in[i] * w;
    }
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto wl = std::polar (1.0, -2.0 * pi / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w = 1.0;
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    std::vector<double> mag (n / 2);
    for (size_t i = 0; i < n / 2; ++i) mag[i] = std::abs (a[i]);
    return mag;
}
} // namespace testutil
