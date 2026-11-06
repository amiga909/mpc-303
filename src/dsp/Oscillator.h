#pragma once

#include "Calibration.h"
#include <cmath>

namespace acid3
{
/** 303 VCO: saw core plus the transistor-derived square. Runs at the oversampled rate.

    Saw: ramp with a slight exponential bend, band-limited with PolyBLEP at the reset.
    Square: the 303 derives it from the saw through a single-transistor shaper, modelled as
    -tanh(G*saw + offset) [O303]. That gives ~47% duty, a hard edge at the saw reset (BLEP'd)
    and a softened edge at the shaper threshold. The tilt/sag comes from the AC coupling
    into the filter (pre-filter highpass in Voice). */
class Oscillator
{
public:
    void reset() { phase = 0.0; }

    /** Advances one sample and returns both waveforms (303 output polarity, ~ +-1). */
    void process (double freqHz, double sampleRate, double& sawOut, double& squareOut, bool needSquare = true)
    {
        const double dt = std::fmin (freqHz / sampleRate, 0.45);
        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;

        const double raw = shapeSaw (phase);
        const double blep = polyBlep (phase, dt);

        // saw steps by -2 at the reset; correction = (height / 2) * blep
        sawOut = -(raw - blep);

        // square steps from shape(+1) to shape(-1) at the reset
        static const double squareJump = shapeSquare (-1.0) - shapeSquare (1.0);
        squareOut = needSquare ? -(shapeSquare (raw) + 0.5 * squareJump * blep) : 0.0;
    }

    double getPhase() const { return phase; }

    static double shapeSaw (double p)
    {
        // ramp -1..+1 with a mild exponential (RC-charge) bend, endpoints fixed
        constexpr double c = cal::sawCurvature;
        if constexpr (c <= 0.0)
            return 2.0 * p - 1.0;
        constexpr double k = 4.0 * c;
        static const double norm = 2.0 / (1.0 - std::exp (-k));
        return norm * (1.0 - std::exp (-k * p)) - 1.0;
    }

    static double shapeSquare (double saw)
    {
        return -std::tanh (cal::squareShaperGain * saw + cal::squareShaperOffset);
    }

private:
    /** Standard 2-point PolyBLEP residual for a downward step of height 2 at phase 0. */
    static double polyBlep (double t, double dt)
    {
        if (t < dt)       { t /= dt;               return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt;    return t * t + t + t + 1.0; }
        return 0.0;
    }

    double phase = 0.0;
};
} // namespace acid3
