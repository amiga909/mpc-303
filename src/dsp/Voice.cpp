#include "Voice.h"
#include <algorithm>

namespace acid3
{
namespace
{
    double msToCoeff (double ms, double rate) { return ms <= 0.0 ? 0.0 : std::exp (-1000.0 / (ms * rate)); }
    double noteToHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
    constexpr double filterInputLevel = 0.5;
}

double Voice::cutoffKnobToHz (double knob)
{
    return cal::cutoffMinHz * std::pow (cal::cutoffMaxHz / cal::cutoffMinHz, std::clamp (knob, 0.0, 1.0));
}

double Voice::decayKnobToMs (double knob)
{
    return cal::megDecayMinMs * std::pow (cal::megDecayMaxMs / cal::megDecayMinMs, std::clamp (knob, 0.0, 1.0));
}

int Voice::designDecimators (double sampleRate, HalfbandDecimator& a, HalfbandDecimator& b)
{
    // Stage A runs at 4x: pass 0..20 kHz, reject what folds into it. Stage B at 2x: steep around fs/2.
    const double passA = 20000.0 / (sampleRate * oversampling);
    a.design (0.5 - 2.0 * passA, 100.0);
    const double passB = std::min (20000.0, 0.4535 * sampleRate) / (2.0 * sampleRate);
    b.design (0.5 - 2.0 * passB, 100.0);
    return (int) std::lround ((a.latencyInInputSamples() / 2.0 + b.latencyInInputSamples()) / 2.0);
}

void Voice::prepare (double sampleRate, uint32_t seed)
{
    sr = sampleRate;
    osr = sampleRate * oversampling;
    rngSeed = seed == 0 ? 0x303u : seed;

    smoothCoeff = msToCoeff (10.0, sr);
    slideCoeff = msToCoeff (cal::slideTauMs, sr);
    driftCoeff = msToCoeff (1000.0 / (2.0 * kPi * cal::driftRateHz), sr);
    vegDecayCoeff = msToCoeff (cal::vegDecayMs, sr);
    declick.setLowpass (cal::vcaDeclickHz, std::sqrt (0.5), sr);
    waveMixStep = 1.0 / (0.005 * sr);

    preHighpass.setCutoff (cal::preFilterHighpassHz, osr);
    ladder.prepare (osr);
    postAllpass.setCutoff (cal::postAllpassHz, sr);
    postHighpass.setCutoff (cal::postHighpassHz, sr);
    postNotch.setNotch (cal::postNotchHz, cal::postNotchBwOct, sr);

    latency = designDecimators (sr, decimA, decimB);

    reset();
}

void Voice::reset()
{
    smoothed = params;
    rng = rngSeed;
    gate = accentOn = false;
    meg = accentCap = veg = ampDeclicked = 0.0;
    driftValue = driftTarget = 0.0;
    driftCounter = 0;
    pitch = targetPitch;
    waveMix = params.square ? 1.0 : 0.0;
    megTauMs = decayKnobToMs (params.decay);
    megCoeff = msToCoeff (megTauMs, sr);
    vegReleaseCoeff = msToCoeff (cal::vegReleaseMs, sr);
    osc.reset();
    preHighpass.reset();
    ladder.reset();
    decimA.reset();
    decimB.reset();
    postAllpass.reset();
    postHighpass.reset();
    postNotch.reset();
    declick.reset();
    lastCutoffHz = cutoffKnobToHz (params.cutoff);
}

void Voice::setAccentState (bool accent)
{
    accentOn = accent;
    // accented notes ignore the Decay knob [O303][CIRC]
    megTauMs = accent ? cal::megAccentDecayMs : decayKnobToMs (smoothed.decay);
    megCoeff = msToCoeff (megTauMs, sr);
    vegReleaseCoeff = msToCoeff (accent ? cal::vegAccentReleaseMs : cal::vegReleaseMs, sr);
}

void Voice::trigger (int note, bool accent)
{
    if (! isActive())
    {
        // idle voice: start from a clean state like the hardware at rest
        osc.reset();
        ladder.reset();
    }
    targetPitch = pitch = (double) note;
    setAccentState (accent);
    meg = 1.0;
    veg = 1.0; // attack is effectively instant; the declicker shapes it (~1 ms)
    gate = true;
}

void Voice::slideTo (int note, bool accent)
{
    targetPitch = (double) note;
    setAccentState (accent);
    gate = true;
}

void Voice::release()
{
    gate = false;
}

double Voice::nextRandom()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (rng / 4294967295.0) * 2.0 - 1.0;
}

float Voice::renderSample()
{
    // ---- control-rate section (host rate) ----------------------------------------------
    auto smooth = [this] (double& s, double t) { s = t + smoothCoeff * (s - t); };
    smooth (smoothed.tune, params.tune);
    smooth (smoothed.cutoff, params.cutoff);
    smooth (smoothed.reso, params.reso);
    smooth (smoothed.envMod, params.envMod);
    smooth (smoothed.accent, params.accent);
    smooth (smoothed.volumeDb, params.volumeDb);
    smooth (smoothed.pitchBend, params.pitchBend);
    smoothed.decay = params.decay;
    if (! accentOn)
    {
        const double tau = decayKnobToMs (smoothed.decay);
        if (tau != megTauMs) { megTauMs = tau; megCoeff = msToCoeff (tau, sr); }
    }

    // slide: RC lag on the pitch CV (constant time, not constant rate)
    pitch = targetPitch + slideCoeff * (pitch - targetPitch);

    // analog drift: smoothed random walk, deterministic per seed
    if (params.drift)
    {
        if (--driftCounter <= 0)
        {
            driftTarget = nextRandom() * cal::driftCents / 100.0;
            driftCounter = (int) (sr / cal::driftRateHz);
        }
        driftValue = driftTarget + driftCoeff * (driftValue - driftTarget);
    }
    else
        driftValue = 0.0;

    const double note = pitch + smoothed.tune + smoothed.pitchBend + driftValue;
    const double freq = noteToHz (note);

    // MEG: exponential discharge
    meg *= megCoeff;

    // accent sweep capacitor, RC set by the resonance pot's second gang [CIRC]
    const double accTau = cal::accentSweepTauMinMs + (cal::accentSweepTauMaxMs - cal::accentSweepTauMinMs) * smoothed.reso;
    if (accTau != lastAccTau) { lastAccTau = accTau; accCoeff = msToCoeff (accTau, sr); }
    const double accIn = accentOn ? meg : 0.0;
    accentCap = accIn + accCoeff * (accentCap - accIn);

    // cutoff CV: measured env-mod scaler/offset [O303]; env mod also pulls the base cutoff down
    const double c = std::clamp (smoothed.cutoff, 0.0, 1.0);
    const double e = std::clamp (smoothed.envMod, 0.0, 1.0);
    const double scaler = (1.0 - c) * (cal::envModScalerLoF * e + cal::envModScalerLoC)
                        + c * (cal::envModScalerHiF * e + cal::envModScalerHiC);
    const double offset = cal::envModOffsetF * c + cal::envModOffsetC;
    const double accentGain = accentOn ? smoothed.accent : 0.0;
    const double octaves = scaler * (meg - offset) + cal::accentCutoffOctaves * smoothed.accent * accentCap;
    const double cutoffHz = std::clamp (cutoffKnobToHz (c) * std::pow (2.0, octaves), 10.0, cal::maxCutoffHz);

    // VEG + VCA control
    if (gate)
        veg *= vegDecayCoeff;
    else
        veg *= vegReleaseCoeff;
    double ampCv = veg;
    if (gate)
        ampCv += (cal::vcaMegAmount + cal::vcaAccentAmount * accentGain) * meg;
    ampDeclicked = declick.process (ampCv);

    // waveform switch crossfade
    const double waveTarget = params.square ? 1.0 : 0.0;
    if (waveMix < waveTarget) waveMix = std::min (waveTarget, waveMix + waveMixStep);
    else if (waveMix > waveTarget) waveMix = std::max (waveTarget, waveMix - waveMixStep);

    ladder.setResonance (smoothed.reso);

    // ---- oversampled audio section ------------------------------------------------------
    // cutoff glides geometrically from the last sample's value to this one's across the sub-samples
    const double cutoffStep = std::pow (cutoffHz / lastCutoffHz, 1.0 / oversampling);
    double subCutoff = lastCutoffHz;
    const bool needSquare = waveMix > 0.0;
    double os[oversampling];
    for (int i = 0; i < oversampling; ++i)
    {
        subCutoff *= cutoffStep;
        ladder.setCutoff (subCutoff);
        double saw, sq;
        osc.process (freq, osr, saw, sq, needSquare);
        double x = saw + (sq - saw) * waveMix;
        x = preHighpass.highpass (x * filterInputLevel);
        os[i] = ladder.process (x);
    }
    lastCutoffHz = cutoffHz;
    const double y2a = decimA.process (os[0], os[1]);
    const double y2b = decimA.process (os[2], os[3]);
    double y = decimB.process (y2a, y2b);

    // ---- post filter chain, VCA, output --------------------------------------------------
    y = postAllpass.allpass (y);
    y = postHighpass.highpass (y);
    y = postNotch.process (y);
    y *= ampDeclicked;
    y = std::tanh (y * cal::vcaSaturation) / cal::vcaSaturation; // gentle VCA saturation
    if (smoothed.volumeDb != lastVolumeDb) { lastVolumeDb = smoothed.volumeDb; volumeGain = cal::outputGain * std::pow (10.0, lastVolumeDb / 20.0); }
    y *= volumeGain;

    probeData.pitch = note;
    probeData.meg = meg;
    probeData.accentSweep = accentCap;
    probeData.cutoffHz = cutoffHz;
    probeData.amp = ampDeclicked;
    probeData.megDecayMs = megTauMs;

    return std::isfinite (y) ? (float) y : 0.0f;
}
} // namespace acid3
