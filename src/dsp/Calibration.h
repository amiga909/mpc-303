#pragma once

// All [cal] constants of the TB-303 model live here (see SPEC.md §4).
// Source tags:
//   [O303]  measured values published in Open303 (Robin Schmidt, MIT license)
//   [KVR]   303 diode-ladder topology from the "mystran & kunn" KVR thread
//   [CIRC]  derived from the 303 circuit description (Devil Fish docs / service notes)
//   [EST]   engineering estimate, NOT verified against a real unit
// Nothing here is calibrated against reference recordings yet (see CALIBRATION_LOG.md).

namespace acid3::cal
{
// ---- VCO -----------------------------------------------------------------------------
constexpr double sawCurvature        = 0.03;     // [EST] exponential bend of the ramp (0 = linear)
constexpr double squareShaperGain    = 70.0;     // [O303] dB2amp(36.9): transistor shaper gain
constexpr double squareShaperOffset  = 4.37;     // [O303] shaper bias -> ~47% duty, soft falling edge
constexpr double driftCents          = 3.0;      // [EST] max analog pitch wander
constexpr double driftRateHz         = 0.15;     // [EST]

// ---- Signal-path filters ---------------------------------------------------------------
constexpr double preFilterHighpassHz = 44.486;   // [O303] coupling into the VCF (gives square tilt)
constexpr double feedbackHighpassHz  = 150.0;    // [O303][KVR] HPF in resonance loop (bass loss at high reso)
constexpr double postAllpassHz       = 14.008;   // [O303]
constexpr double postHighpassHz      = 24.167;   // [O303]
constexpr double postNotchHz         = 7.5164;   // [O303] very wide low-frequency band-reject
constexpr double postNotchBwOct      = 4.7;      // [O303]

// ---- VCF (diode ladder) ---------------------------------------------------------------
constexpr double cutoffMinHz         = 313.8152786059267;  // [O303] knob fully CCW
constexpr double cutoffMaxHz         = 2394.411986817546;  // [O303] knob fully CW
constexpr double ladderResonanceMax  = 16.0;     // [KVR] linear self-osc threshold ~17; stock unit stays just below
constexpr double resonanceSkew       = 3.0;      // [O303] k = kmax * (1-e^{-3r})/(1-e^{-3})
constexpr double ladderDrive         = 1.6;      // [EST] input stage tanh drive (squelch / grit)
constexpr double maxCutoffHz         = 18000.0;  // safety clamp

// Env-mod mapping measured on a real unit [O303]: cutoff_inst = cutoff * 2^(scaler*(env - offset))
constexpr double envModOffsetF = 0.048292930943553;
constexpr double envModOffsetC = 0.294391201442418;
constexpr double envModScalerLoF = 3.773996325111173;
constexpr double envModScalerLoC = 0.736965594166206;
constexpr double envModScalerHiF = 4.194548788411135;
constexpr double envModScalerHiC = 0.864344900642434;

// ---- MEG (filter envelope) -------------------------------------------------------------
constexpr double megDecayMinMs       = 200.0;    // [O303] time constant (1/e), knob CCW
constexpr double megDecayMaxMs       = 2000.0;   // [O303] knob CW
constexpr double megAccentDecayMs    = 200.0;    // [O303][CIRC] accented notes ignore the Decay knob

// ---- Accent ------------------------------------------------------------------------------
// Accent sweep capacitor: RC time constant depends on Resonance (dual-gang pot) [CIRC]
constexpr double accentSweepTauMinMs = 5.0;      // [EST] reso = 0 -> follows MEG closely
constexpr double accentSweepTauMaxMs = 100.0;    // [EST] reso = 1 -> smoothed, consecutive accents stack
constexpr double accentCutoffOctaves = 1.6;      // [EST] accent sweep depth at Accent = max
constexpr double vcaMegAmount        = 0.45;     // [O303] MEG bleed into VCA
constexpr double vcaAccentAmount     = 4.0;      // [O303] extra VCA level from accent (x MEG)

// ---- VEG / VCA ---------------------------------------------------------------------------
constexpr double vegDecayMs          = 1230.0;   // [O303] time constant while gate high
constexpr double vegReleaseMs        = 0.5;      // [O303] normal note release
constexpr double vegAccentReleaseMs  = 50.0;     // [O303] accented note release
constexpr double vcaDeclickHz        = 200.0;    // [O303] 2-pole LP on VCA CV
constexpr double vcaSaturation       = 0.15;     // [EST] soft saturation amount of VCA
constexpr double outputGain          = 0.8;      // [EST] accents peak around -3 dBFS at default settings

// ---- Slide / gate ----------------------------------------------------------------------
constexpr double slideTauMs          = 20.0;     // [CIRC][EST] RC on pitch CV: 95% in ~60 ms
constexpr double gateFraction        = 0.5;      // [O303] gate = half a 16th step
} // namespace acid3::cal
