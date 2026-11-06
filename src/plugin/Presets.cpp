#include "Presets.h"

namespace acid3::presets
{
const std::array<Sound, 8>& sounds()
{
    static const std::array<Sound, 8> s { {
        //  name               cutoff reso  envmod decay accent square
        { "Init",              0.50f, 0.50f, 0.50f, 0.50f, 0.50f, false },
        { "Squelch Saw",       0.25f, 0.85f, 0.70f, 0.35f, 0.80f, false },
        { "Rubber Bass",       0.15f, 0.40f, 0.35f, 0.55f, 0.40f, false },
        { "Square Acid",       0.30f, 0.80f, 0.65f, 0.40f, 0.75f, true  },
        { "Screamer",          0.45f, 0.97f, 0.85f, 0.25f, 1.00f, false },
        { "Deep Pluck",        0.05f, 0.20f, 0.55f, 0.10f, 0.30f, true  },
        { "Wah Line",          0.35f, 0.75f, 0.90f, 0.80f, 0.60f, false },
        { "Closed Tick",       0.00f, 0.90f, 0.15f, 0.00f, 0.90f, false },
    } };
    return s;
}

// token format: see Pattern.cpp  (n=note t=tie r=rest, 0..12 semitones, -/+ octave, a=accent s=slide)
const std::array<const char*, 8>& patternTexts()
{
    static const std::array<const char*, 8> p { {
        "n0- n0 n12a n0- n0s n3 n0- n7a n0- n0 n10s n12a n0- n3+ n0-s n3",
        "n0- r0 n0-a n0 n0-s n0+ n0- n3s n5 n0- r0 n7a n0- n10s n7 n0a",
        "n0 n0 n12s n10 n7as n5 n3 n0 n0- n0 n3s n5 n0a n0+ n10s n7",
        "n5- n5 n5-a n8s n10 n5- n5 n12a n5- r0 n5s n3 n0 n5-a n5 t0",
        "n0- n12a n0- n0 n12as n10 n0- n7 n0- n12a n0- n3s n5 n0- n7a n10s",
        "n0 t0 n3 n0s n7 t0 n0a r0 n0 t0 n10s n12 n0a t0 n7s n5",
        "n0-a n0- n0 n0-s n12 n0- n0-a n3 n0-s n5 n0- n0-a n7s n0 n0- n10",
        "n7- n7 n7+as n5 n7- n3s n2 n0 n7-a n7 n10s n12 n7- n7a n5s n3",
    } };
    return p;
}

const std::array<const char*, 8>& patternNames()
{
    static const std::array<const char*, 8> n { {
        "Warehouse", "Stutter", "Climber", "Minor Drift", "Octave Jumps", "Tied Up", "Root Pump", "Seventh Heaven"
    } };
    return n;
}

std::array<Pattern, numPatterns> initialPatterns()
{
    std::array<Pattern, numPatterns> out {};
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (i < patternTexts().size())
            Pattern::fromString (patternTexts()[i], out[i]);
        else
            for (auto& s : out[i].steps) s.gate = Gate::Rest;
    }
    return out;
}
} // namespace acid3::presets
