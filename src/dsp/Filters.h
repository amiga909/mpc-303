#pragma once

#include <cmath>
#include <vector>

namespace acid3
{
constexpr double kPi = 3.14159265358979323846;

/** TPT one-pole: lowpass, highpass and allpass outputs. */
class OnePole
{
public:
    void setCutoff (double hz, double sampleRate)
    {
        g = std::tan (kPi * std::fmin (hz, 0.49 * sampleRate) / sampleRate);
        G = g / (1.0 + g);
    }
    void reset() { s = 0.0; }

    double lowpass (double x)
    {
        const double v = (x - s) * G;
        const double y = v + s;
        s = y + v;
        return y;
    }
    double highpass (double x) { return x - lowpass (x); }
    double allpass (double x)  { const double lp = lowpass (x); return 2.0 * lp - x; }

private:
    double g = 0.0, G = 0.0, s = 0.0;
};

/** RBJ biquad (transposed direct form II). */
class Biquad
{
public:
    void setNotch (double hz, double bwOctaves, double sampleRate)
    {
        const double w0 = 2.0 * kPi * hz / sampleRate;
        const double alpha = std::sin (w0) * std::sinh (std::log (2.0) / 2.0 * bwOctaves * w0 / std::sin (w0));
        const double cw = std::cos (w0);
        set (1.0, -2.0 * cw, 1.0, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }
    void setLowpass (double hz, double q, double sampleRate)
    {
        const double w0 = 2.0 * kPi * hz / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double cw = std::cos (w0);
        set ((1.0 - cw) / 2.0, 1.0 - cw, (1.0 - cw) / 2.0, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }
    void reset() { z1 = z2 = 0.0; }

    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

/** Linear-phase Kaiser-windowed halfband FIR decimator (2:1). */
class HalfbandDecimator
{
public:
    /** transition: normalised width of the transition band (fraction of the input rate). */
    void design (double transition, double attenuationDb)
    {
        const double beta = attenuationDb > 50.0 ? 0.1102 * (attenuationDb - 8.7)
                                                 : 0.5842 * std::pow (attenuationDb - 21.0, 0.4) + 0.07886 * (attenuationDb - 21.0);
        int n = (int) std::ceil ((attenuationDb - 8.0) / (2.285 * 2.0 * kPi * transition));
        n = ((n / 4) * 4) + 3; // length 4k+3 -> centre tap odd, proper halfband
        taps.assign ((size_t) n, 0.0);
        const int c = n / 2;
        for (int i = 0; i < n; ++i)
        {
            const int m = i - c;
            const double sinc = m == 0 ? 0.5 : std::sin (kPi * m * 0.5) / (kPi * m);
            const double r = (double) m / c;
            taps[(size_t) i] = sinc * bessel0 (beta * std::sqrt (std::fmax (0.0, 1.0 - r * r))) / bessel0 (beta);
        }
        double sum = 0.0;
        for (auto t : taps) sum += t;
        for (auto& t : taps) t /= sum;
        history.assign (taps.size() * 2, 0.0);
        pos = 0;
    }

    int latencyInInputSamples() const { return (int) taps.size() / 2; }
    void reset() { std::fill (history.begin(), history.end(), 0.0); pos = 0; }

    /** Push two input samples, returns one output sample. */
    double process (double x0, double x1)
    {
        push (x0);
        push (x1);
        const size_t n = taps.size();
        // halfband: every other tap (except centre) is zero; symmetric, so pair tap i with n-1-i
        const double* h = history.data() + pos;
        const double* t = taps.data();
        double acc = t[n / 2] * h[n / 2];
        for (size_t i = 0; i < n / 2; i += 2)
            acc += t[i] * (h[i] + h[n - 1 - i]);
        return acc;
    }

private:
    void push (double x)
    {
        pos = (pos == 0 ? taps.size() : pos) - 1;
        history[pos] = x;
        history[pos + taps.size()] = x;
    }
    static double bessel0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-12 * sum) break;
        }
        return sum;
    }

    std::vector<double> taps, history;
    size_t pos = 0;
};
} // namespace acid3
