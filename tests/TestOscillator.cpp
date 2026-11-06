#include "TestHelpers.h"
#include "dsp/Oscillator.h"
#include "dsp/Voice.h"
#include <catch2/catch_test_macros.hpp>

using namespace acid3;
using namespace testutil;

namespace
{
double noteHz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }

/** Measured frequency of the oscillator via interpolated reset times of the saw. */
double measureOscFrequency (double freq, double sr, double seconds)
{
    Oscillator o;
    double prev = 0.0, first = -1.0, last = -1.0;
    int count = 0;
    for (int i = 0; i < (int) (sr * seconds); ++i)
    {
        double saw, sq;
        o.process (freq, sr, saw, sq);
        // output saw is a falling ramp: use its clean downward zero crossing
        if (i > 0 && prev > 0.0 && saw <= 0.0 && prev - saw < 0.5)
        {
            const double t = (i - 1) + prev / (prev - saw);
            if (first < 0.0) first = t; else ++count;
            last = t;
        }
        prev = saw;
    }
    return count * sr / (last - first);
}
} // namespace

TEST_CASE ("Oscillator: pitch accurate within 1 cent over MIDI 24..84", "[osc]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int note = 24; note <= 84; note += 5)
        {
            const double f = noteHz (note);
            const double osr = sr * Voice::oversampling;
            const double measured = measureOscFrequency (f, osr, 3.0);
            const double cents = 1200.0 * std::log2 (measured / f);
            INFO ("sr " << sr << " note " << note << " cents " << cents);
            CHECK (std::fabs (cents) < 1.0);
        }
}

TEST_CASE ("Square: ~47% duty and not a clean pulse (even harmonics)", "[osc]")
{
    const double sr = 192000.0, f = 93.75; // f = sr / 2048 -> exact bins for a 2^16 FFT
    Oscillator o;
    int high = 0;
    const int n = 1 << 16;
    std::vector<double> buf ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        double saw, sq;
        o.process (f, sr, saw, sq);
        buf[(size_t) i] = sq;
        if (sq > 0.0) ++high;
    }
    const double duty = std::fmin ((double) high / n, 1.0 - (double) high / n);
    INFO ("duty " << duty);
    CHECK (duty > 0.45);
    CHECK (duty < 0.50);

    const auto mag = spectrum (buf);
    const size_t bin1 = (size_t) std::lround (f * n / sr);
    auto peak = [&] (size_t b) { double m = 0; for (size_t k = b - 2; k <= b + 2; ++k) m = std::fmax (m, mag[k]); return m; };
    const double h2 = db (peak (2 * bin1) / peak (bin1));
    INFO ("2nd harmonic " << h2 << " dB re fundamental");
    CHECK (h2 > -40.0);
}

TEST_CASE ("Oscillator + decimation: aliasing at C5 below -80 dB", "[osc]")
{
    for (double sr : { 44100.0, 48000.0 })
        for (bool square : { false, true })
        {
            HalfbandDecimator a, b;
            Voice::designDecimators (sr, a, b);
            Oscillator o;
            const double f0 = noteHz (72), osr = sr * Voice::oversampling;
            const int n = 1 << 15;
            std::vector<double> buf ((size_t) n);
            for (int i = -4096; i < n; ++i)
            {
                double x[4];
                for (auto& v : x) { double saw, sq; o.process (f0, osr, saw, sq); v = square ? sq : saw; }
                const double y = b.process (a.process (x[0], x[1]), a.process (x[2], x[3]));
                if (i >= 0) buf[(size_t) i] = y;
            }
            const auto mag = spectrum (buf);
            const double binHz = sr / n;
            double fund = 0.0, worst = 0.0, worstF = 0.0;
            for (size_t bin = 1; bin < mag.size(); ++bin)
            {
                const double f = bin * binHz;
                if (f > 20000.0) break;
                const double h = f / f0;
                const double distHz = std::fabs (h - std::round (h)) * f0;
                if (std::round (h) == 1.0 && distHz < 4 * binHz) fund = std::fmax (fund, mag[bin]);
                else if (distHz > 6 * binHz && mag[bin] > worst) { worst = mag[bin]; worstF = f; }
            }
            INFO ("sr " << sr << (square ? " square" : " saw") << " worst alias " << db (worst / fund) << " dB at " << worstF << " Hz");
            CHECK (db (worst / fund) < -80.0);
        }
}
