#include "TestHelpers.h"
#include "seq/Engine.h"
#include <catch2/catch_test_macros.hpp>
#include <random>

using namespace acid3;

namespace
{
VoiceParams defaults()
{
    VoiceParams p;
    p.drift = false;
    return p;
}

/** Time (ms) until the MEG falls to 1/e after a trigger. */
double megTimeConstantMs (double decayKnob, bool accent, double sr = 48000.0)
{
    Voice v;
    auto p = defaults();
    p.decay = decayKnob;
    v.setParams (p);
    v.prepare (sr);
    v.trigger (36, accent);
    for (int i = 1; i < (int) (sr * 10); ++i)
    {
        v.renderSample();
        if (v.probe().meg < std::exp (-1.0))
            return 1000.0 * i / sr;
    }
    return -1.0;
}
} // namespace

TEST_CASE ("MEG: decay knob maps to 200 ms .. 2 s, accent forces short decay", "[voice]")
{
    for (double knob : { 0.0, 0.5, 1.0 })
    {
        const double expected = Voice::decayKnobToMs (knob);
        const double measured = megTimeConstantMs (knob, false);
        INFO ("knob " << knob << " expected " << expected << " measured " << measured);
        CHECK (std::fabs (measured / expected - 1.0) < 0.1);

        const double acc = megTimeConstantMs (knob, true);
        INFO ("accented: " << acc);
        CHECK (std::fabs (acc / cal::megAccentDecayMs - 1.0) < 0.1);
    }
    CHECK (std::fabs (Voice::decayKnobToMs (0.0) - 200.0) < 1e-9);
    CHECK (std::fabs (Voice::decayKnobToMs (1.0) - 2000.0) < 1e-9);
}

TEST_CASE ("Slide: constant-time RC glide, no envelope retrigger", "[voice]")
{
    const double sr = 48000.0;
    for (int interval : { 1, 12 })
    {
        Voice v;
        v.setParams (defaults());
        v.prepare (sr);
        v.trigger (36, false);
        for (int i = 0; i < (int) (0.1 * sr); ++i) v.renderSample();
        const double megBefore = v.probe().meg;
        v.slideTo (36 + interval, false);
        double t95 = -1.0, maxMeg = 0.0;
        for (int i = 1; i < (int) (0.5 * sr); ++i)
        {
            v.renderSample();
            maxMeg = std::fmax (maxMeg, v.probe().meg);
            if (t95 < 0.0 && v.probe().pitch - 36.0 >= 0.95 * interval)
                t95 = 1000.0 * i / sr;
        }
        INFO ("interval " << interval << " t95 " << t95 << " ms");
        CHECK (t95 > 45.0);
        CHECK (t95 < 75.0);
        CHECK (maxMeg <= megBefore); // MEG not retriggered
        CHECK (v.isGateOn());
    }
}

TEST_CASE ("Accent sweep: consecutive accents stack at high resonance only", "[voice]")
{
    const double sr = 48000.0;
    const int stepSamples = (int) std::lround (sr * 60.0 / 130.0 / 4.0);
    auto peaks = [&] (double reso)
    {
        Voice v;
        auto p = defaults();
        p.reso = reso;
        p.accent = 1.0;
        v.setParams (p);
        v.prepare (sr);
        std::vector<double> result;
        for (int note = 0; note < 4; ++note)
        {
            v.trigger (36, true);
            double peak = 0.0;
            for (int i = 0; i < stepSamples; ++i)
            {
                if (i == stepSamples / 2) v.release();
                v.renderSample();
                peak = std::fmax (peak, v.probe().accentSweep);
            }
            result.push_back (peak);
        }
        return result;
    };

    const auto hi = peaks (0.9);
    INFO ("high reso peaks " << hi[0] << " " << hi[1] << " " << hi[2] << " " << hi[3]);
    CHECK (hi[1] > hi[0]);
    CHECK (hi[2] > hi[1]);
    CHECK (hi[3] > hi[2]);

    const auto lo = peaks (0.0);
    INFO ("low reso peaks " << lo[0] << " " << lo[3]);
    CHECK (lo[3] / lo[0] < 1.05);
}

TEST_CASE ("Voice: accent is louder than a normal note", "[voice]")
{
    const double sr = 48000.0;
    auto peakOf = [&] (bool accent)
    {
        Voice v;
        auto p = defaults();
        p.accent = 0.8;
        v.setParams (p);
        v.prepare (sr);
        v.trigger (36, accent);
        double peak = 0.0;
        for (int i = 0; i < (int) (0.1 * sr); ++i) peak = std::fmax (peak, std::fabs (v.renderSample()));
        return peak;
    };
    const double ratio = testutil::db (peakOf (true) / peakOf (false));
    INFO ("accent boost " << ratio << " dB");
    CHECK (ratio > 4.0);
    CHECK (ratio < 16.0);
}

TEST_CASE ("Voice: stability fuzz across sample rates and automation", "[voice][fuzz]")
{
    std::mt19937 rng (42);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        Voice v;
        v.prepare (sr);
        bool finite = true;
        double worst = 0.0;
        for (int block = 0; block < 800; ++block)
        {
            VoiceParams p;
            p.cutoff = u (rng); p.reso = u (rng); p.envMod = u (rng); p.decay = u (rng);
            p.accent = u (rng); p.square = u (rng) > 0.5; p.tune = 24.0 * u (rng) - 12.0;
            p.volumeDb = 6.0 * u (rng);
            v.setParams (p);
            const double r = u (rng);
            if (r < 0.3) v.trigger (24 + (int) (60 * u (rng)), u (rng) > 0.5);
            else if (r < 0.5) v.slideTo (24 + (int) (60 * u (rng)), u (rng) > 0.5);
            else if (r < 0.7) v.release();
            for (int i = 0; i < 64; ++i)
            {
                const float y = v.renderSample();
                finite = finite && std::isfinite (y);
                worst = std::fmax (worst, std::fabs ((double) y));
            }
        }
        INFO ("sr " << sr << " worst " << worst);
        CHECK (finite);
        CHECK (worst < 4.0);
    }
}

TEST_CASE ("Engine: identical seeds give bit-identical output (drift on)", "[voice]")
{
    auto render = [] (uint32_t seed)
    {
        Engine e;
        VoiceParams p;
        p.drift = true;
        e.setParams (p);
        e.prepare (48000.0, seed);
        Pattern pat;
        Pattern::fromString ("n0 n12as n3 n7s n5 r0 n0a t0", pat);
        e.getSequencer().setPattern (pat);
        std::vector<float> out (48000);
        Transport t; t.playing = true; t.bpm = 128.0;
        for (int pos = 0; pos < 48000; pos += 500)
        {
            t.ppq = pos / 48000.0 * t.bpm / 60.0;
            e.process (out.data() + pos, 500, t, nullptr, 0);
        }
        return out;
    };
    const auto a = render (7), b = render (7), c = render (8);
    CHECK (a == b);
    CHECK (a != c);
}
