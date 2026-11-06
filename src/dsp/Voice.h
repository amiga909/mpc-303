#pragma once

#include "Calibration.h"
#include "DiodeLadder.h"
#include "Filters.h"
#include "Oscillator.h"
#include <cstdint>

namespace acid3
{
struct VoiceParams
{
    double tune = 0.0;       // semitones, -12..12
    double cutoff = 0.5;     // knob 0..1
    double reso = 0.5;       // knob 0..1
    double envMod = 0.5;     // knob 0..1
    double decay = 0.5;      // knob 0..1
    double accent = 0.5;     // knob 0..1
    double volumeDb = 0.0;
    bool square = false;
    bool drift = true;
    double pitchBend = 0.0;  // semitones
};

/** Internal signals, exposed for tests. */
struct VoiceProbe
{
    double pitch = 0.0;          // current (slid) note number incl. tune
    double meg = 0.0;            // main envelope 0..1
    double accentSweep = 0.0;    // accent capacitor voltage
    double cutoffHz = 0.0;       // instantaneous VCF cutoff
    double amp = 0.0;            // VCA control after declicker
    double megDecayMs = 0.0;     // active MEG time constant
};

/** One monophonic TB-303 voice. render() runs at the host rate, VCO and VCF run at 4x. */
class Voice
{
public:
    static constexpr int oversampling = 4;

    void prepare (double sampleRate, uint32_t seed = 0x303u);
    void reset();
    void setParams (const VoiceParams& p) { params = p; }

    /** New note: retriggers MEG/VEG and jumps the pitch. */
    void trigger (int note, bool accent);
    /** Legato note: pitch glides via the slide RC, nothing is retriggered. */
    void slideTo (int note, bool accent);
    /** Gate low. */
    void release();
    /** Hard stop (transport stop / all notes off). */
    void allNotesOff() { release(); }

    bool isGateOn() const { return gate; }
    bool isActive() const { return gate || veg > 1e-6 || ampDeclicked > 1e-6; }

    float renderSample();
    void render (float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = renderSample();
    }

    int getLatencySamples() const { return latency; }
    const VoiceProbe& probe() const { return probeData; }

    /** Designs the 4x -> 2x -> 1x decimation chain for a host rate; returns latency in host samples. */
    static int designDecimators (double sampleRate, HalfbandDecimator& a, HalfbandDecimator& b);

    // knob mappings (documented in SPEC §7.1)
    static double cutoffKnobToHz (double knob);
    static double decayKnobToMs (double knob);

private:
    void setAccentState (bool accent);
    double nextRandom();

    double sr = 44100.0, osr = 176400.0;
    VoiceParams params;
    VoiceParams smoothed;
    double smoothCoeff = 0.0;

    // pitch
    double targetPitch = 36.0, pitch = 36.0, slideCoeff = 0.0;
    double driftValue = 0.0, driftTarget = 0.0, driftCoeff = 0.0;
    int driftCounter = 0;
    uint32_t rng = 0x303u, rngSeed = 0x303u;

    // envelopes
    bool gate = false, accentOn = false;
    double meg = 0.0, megCoeff = 0.0, megTauMs = 1000.0;
    double accentCap = 0.0;
    double veg = 0.0, vegDecayCoeff = 0.0, vegReleaseCoeff = 0.0;
    double ampDeclicked = 0.0;
    Biquad declick;

    // audio path
    Oscillator osc;
    double waveMix = 0.0, waveMixStep = 0.0;
    OnePole preHighpass;
    DiodeLadder ladder;
    double lastCutoffHz = 1000.0;
    double lastAccTau = -1.0, accCoeff = 0.0;
    double lastVolumeDb = 1e9, volumeGain = 1.0;
    HalfbandDecimator decimA, decimB; // 4x -> 2x -> 1x
    OnePole postAllpass, postHighpass;
    Biquad postNotch;

    int latency = 0;
    VoiceProbe probeData;
};
} // namespace acid3
