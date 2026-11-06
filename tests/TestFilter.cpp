#include "TestHelpers.h"
#include "dsp/DiodeLadder.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <random>

using namespace acid3;
using namespace testutil;

namespace
{
/** Small-signal gain of the ladder at freq (linear region). */
double ladderGain (double cutoff, double reso, double freq, double sr)
{
    DiodeLadder f;
    f.prepare (sr);
    f.setParameters (cutoff, reso);
    const double amp = 1e-3;
    const int settle = (int) (sr * 0.3), len = (int) (sr * 0.2);
    std::vector<double> y;
    y.reserve ((size_t) len);
    for (int i = 0; i < settle + len; ++i)
    {
        const double out = f.process (amp * std::sin (2.0 * pi * freq * i / sr));
        if (i >= settle) y.push_back (out);
    }
    return toneMagnitude (y, freq, sr) / amp;
}
} // namespace

TEST_CASE ("Ladder: 303 topology needs loop gain ~17 to oscillate", "[filter]")
{
    CHECK (std::fabs (DiodeLadder::criticalResonance() - 17.0) < 0.2);
    CHECK (cal::ladderResonanceMax < DiodeLadder::criticalResonance());
}

TEST_CASE ("Ladder: resonant peak sits at the requested cutoff", "[filter]")
{
    const double sr = 176400.0;
    for (double fc : { 300.0, 1000.0, 3000.0 })
    {
        double best = 0.0, bestF = 0.0;
        for (double f = 0.7 * fc; f < 1.4 * fc; f *= 1.005)
        {
            const double g = ladderGain (fc, 0.9, f, sr);
            if (g > best) { best = g; bestF = f; }
        }
        INFO ("fc " << fc << " peak at " << bestF);
        CHECK (std::fabs (bestF / fc - 1.0) < 0.05);
    }
}

TEST_CASE ("Ladder: slope above cutoff is ~18 dB/oct, not a 24 dB Moog", "[filter]")
{
    const double sr = 176400.0, fc = 500.0;
    const double g2 = ladderGain (fc, 0.0, 2.0 * fc, sr);
    const double g4 = ladderGain (fc, 0.0, 4.0 * fc, sr);
    const double slope = db (g2) - db (g4);
    INFO ("slope from 2*fc to 4*fc: " << slope << " dB/oct");
    CHECK (slope > 15.0);
    CHECK (slope < 21.0);
}

TEST_CASE ("Ladder: resonance peak grows with reso, bass drops", "[filter]")
{
    const double sr = 176400.0, fc = 1000.0;
    double prevPeak = 0.0;
    for (double r : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        double peak = 0.0;
        for (double f = 500.0; f < 2000.0; f *= 1.01)
            peak = std::fmax (peak, ladderGain (fc, r, f, sr));
        INFO ("reso " << r << " peak dB " << db (peak));
        CHECK (peak > prevPeak);
        prevPeak = peak;
    }
    const double bass0 = ladderGain (fc, 0.0, 50.0, sr);
    const double bass1 = ladderGain (fc, 1.0, 50.0, sr);
    INFO ("50 Hz gain reso0 " << db (bass0) << " reso1 " << db (bass1));
    CHECK (db (bass0) - db (bass1) > 6.0);
}

TEST_CASE ("Ladder: rings long at full resonance but no runaway", "[filter]")
{
    const double sr = 176400.0;
    DiodeLadder f;
    f.prepare (sr);
    f.setParameters (800.0, 1.0);
    double maxLate = 0.0, maxAll = 0.0;
    for (int i = 0; i < (int) (sr * 0.5); ++i)
    {
        const double y = f.process (i == 0 ? 1.0 : 0.0);
        maxAll = std::fmax (maxAll, std::fabs (y));
        if (i > sr * 0.05 && i < sr * 0.06) maxLate = std::fmax (maxLate, std::fabs (y));
    }
    INFO ("peak " << maxAll << " level at 50 ms " << maxLate);
    CHECK (maxLate > 1e-3 * maxAll); // still ringing after 50 ms
    CHECK (maxAll < 4.0);
}

TEST_CASE ("Ladder: stability fuzz", "[filter][fuzz]")
{
    std::mt19937 rng (1234);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    for (double sr : { 88200.0, 176400.0, 384000.0, 768000.0 })
    {
        DiodeLadder f;
        f.prepare (sr);
        double worst = 0.0;
        bool finite = true;
        for (int block = 0; block < 2500; ++block)
        {
            const double fc = 20.0 * std::pow (1000.0, u (rng));
            const double r = u (rng);
            const double amp = 4.0 * u (rng);
            for (int i = 0; i < 64; ++i)
            {
                f.setParameters (fc * (1.0 + 0.5 * std::sin (i * 0.3)), r);
                const double y = f.process (amp * (u (rng) * 2.0 - 1.0));
                finite = finite && std::isfinite (y);
                worst = std::fmax (worst, std::fabs (y));
            }
        }
        INFO ("sr " << sr << " worst |y| " << worst);
        CHECK (finite);
        CHECK (worst < 4.0);
    }
}
