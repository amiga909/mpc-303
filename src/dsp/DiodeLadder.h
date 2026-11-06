#pragma once

#include "Calibration.h"
#include "Filters.h"
#include <cmath>
#include <utility>

namespace acid3
{
/** TB-303 diode ladder VCF.

    Linear core [KVR]: four coupled (unbuffered) RC stages, bottom capacitor half the others,
        dy1/dt = w * 2 * (u  - y1 + y2)
        dy2/dt = w *     (y1 - 2*y2 + y3)
        dy3/dt = w *     (y2 - 2*y3 + y4)
        dy4/dt = w *     (y3 - 2*y4)
    The system matrix is negative definite (stable for every w) and has unity DC gain.
    Resonance feeds y4 back through a highpass (coupling cap, [O303] 150 Hz); a linear loop gain
    of ~17 is needed for self-oscillation.

    Nonlinearity: the differential input pair, u = tanh(d * (x - k * hp(y4))) / d.

    Discretisation: zero-delay-feedback TPT (trapezoidal) for all five states. The tridiagonal
    stage system is solved exactly every sample, and the single nonlinear loop equation in u
    with Newton-Raphson (monotonic residual, so it always converges). */
class DiodeLadder
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        feedbackHpG = std::tan (kPi * cal::feedbackHighpassHz / sampleRate);
        cutoffToArg = kPi / (resonanceFrequency() * sampleRate);
        lastReso = -1.0;
        reset();
    }

    void reset()
    {
        for (auto& v : s) v = 0.0;
        hpState = 0.0;
        uPrev = 0.0;
    }

    /** cutoffHz: the resonance frequency (where the ladder's phase reaches -180 deg). For this
        topology the -3 dB point of the k = 0 response lies ~9.5x lower, and the slope from 2x to
        4x the cutoff is ~18.7 dB/oct - the "18 dB" 303 character. reso: 0..1 knob position. */
    void setParameters (double cutoffHz, double reso)
    {
        setCutoff (cutoffHz);
        setResonance (reso);
    }

    /** Per (oversampled) sample: one tan. */
    void setCutoff (double cutoffHz)
    {
        const double fc = std::fmin (cutoffHz, cal::maxCutoffHz);
        g = std::tan (std::fmin (fc * cutoffToArg, 1.45));
    }

    /** Recomputed only when reso changes. */
    void setResonance (double reso)
    {
        if (std::fabs (reso - lastReso) < 1e-15) return;
        lastReso = reso;
        const double skewed = (1.0 - std::exp (-cal::resonanceSkew * reso)) / (1.0 - std::exp (-cal::resonanceSkew));
        k = cal::ladderResonanceMax * skewed;
        outGain = 1.0 + skewed; // [KVR] loudness compensation of the resonant ladder
    }

    double process (double x)
    {
        // --- solve tridiagonal stage system for y = A + B*u (Thomas algorithm, two RHS) ---
        const double diag = 1.0 + 2.0 * g;
        double c[4], dA[4], dB[4];
        // row 0: (1+2g) y1 - 2g y2 = s1 + 2g u
        c[0] = -2.0 * g / diag;
        dA[0] = s[0] / diag;
        dB[0] = 2.0 * g / diag;
        for (int i = 1; i < 4; ++i)
        {
            const double m = diag + g * c[i - 1]; // sub-diagonal is -g
            c[i] = (i < 3 ? -g : 0.0) / m;
            dA[i] = (s[i] + g * dA[i - 1]) / m;
            dB[i] = (g * dB[i - 1]) / m;
        }
        // back substitution gives y4 directly (last row)
        const double y4A = dA[3], y4B = dB[3];

        // --- feedback highpass (TPT one-pole lowpass state hpState) ---
        const double gh = feedbackHpG;
        const double f0 = (y4A - hpState) / (1.0 + gh);
        const double f1 = y4B / (1.0 + gh);

        // --- Newton on u = tanh(d (x - k f)) / d,  f = f0 + f1 u ---
        constexpr double d = cal::ladderDrive;
        double u = uPrev;
        for (int it = 0; it < 4; ++it)
        {
            const double t = std::tanh (d * (x - k * (f0 + f1 * u)));
            const double r = u - t / d;
            const double dr = 1.0 + k * f1 * (1.0 - t * t);
            const double step = r / dr;
            u -= step;
            if (std::fabs (step) < 1e-10) break;
        }
        uPrev = u;

        // --- final stage values and state update ---
        double y[4];
        y[3] = y4A + y4B * u;
        for (int i = 2; i >= 0; --i)
            y[i] = (dA[i] + dB[i] * u) - c[i] * y[i + 1];

        // integrator inputs v_i = y_i - s_i, new state s_i = 2 y_i - s_i
        for (int i = 0; i < 4; ++i)
            s[i] = 2.0 * y[i] - s[i];

        const double lp = (gh * y[3] + hpState) / (1.0 + gh);
        hpState = 2.0 * lp - hpState;

        if (! std::isfinite (y[3]))
        {
            reset();
            return 0.0;
        }
        return y[3] * outGain;
    }

    /** Probe for tests: the instantaneous feedback gain k. */
    double getK() const { return k; }

    /** Frequency (rad/s, at w = 1) where the linear ladder's phase is -180 deg, computed once. */
    static double resonanceFrequency()
    {
        static const double w180 = []
        {
            double lo = 1e-2, hi = 10.0;
            for (int i = 0; i < 200; ++i)
            {
                const double mid = std::sqrt (lo * hi);
                (linearResponse (mid).second < 0.0 ? lo : hi) = mid;
            }
            return std::sqrt (lo * hi);
        }();
        return w180;
    }

    /** Linear loop gain at which the ideal ladder self-oscillates (~17 for the 303 topology). */
    static double criticalResonance()
    {
        const auto h = linearResponse (resonanceFrequency());
        return 1.0 / std::hypot (h.first, h.second);
    }

    static double linearMagnitude (double w) { const auto h = linearResponse (w); return std::hypot (h.first, h.second); }

    /** H(jw) of the continuous linear ladder (k = 0, w = 1) as {re, im}. */
    static std::pair<double, double> linearResponse (double w)
    {
        // complex Thomas solve of (s I - A) y = b, s = j w
        using C = std::pair<double, double>;
        auto mul = [] (C a, C b) { return C { a.first * b.first - a.second * b.second, a.first * b.second + a.second * b.first }; };
        auto div = [] (C a, C b) { const double n = b.first * b.first + b.second * b.second;
                                   return C { (a.first * b.first + a.second * b.second) / n, (a.second * b.first - a.first * b.second) / n }; };
        auto sub = [] (C a, C b) { return C { a.first - b.first, a.second - b.second }; };
        // rows: [s+2, -2], [-1, s+2, -1], [-1, s+2, -1], [-1, s+2]; rhs [2,0,0,0]
        const C sd { 2.0, w };
        C cp[4], dp[4];
        cp[0] = div (C { -2.0, 0.0 }, sd);
        dp[0] = div (C { 2.0, 0.0 }, sd);
        for (int i = 1; i < 4; ++i)
        {
            const C m = sub (sd, mul (C { -1.0, 0.0 }, cp[i - 1]));
            cp[i] = div (C { -1.0, 0.0 }, m);
            dp[i] = div (sub (C { 0.0, 0.0 }, mul (C { -1.0, 0.0 }, dp[i - 1])), m);
        }
        return dp[3];
    }

private:
    double sampleRate = 44100.0;
    double g = 0.1, k = 0.0, outGain = 1.0;
    double cutoffToArg = 0.0, lastReso = -1.0;
    double s[4] {};
    double hpState = 0.0, uPrev = 0.0;
    double feedbackHpG = 0.0;
};
} // namespace acid3
