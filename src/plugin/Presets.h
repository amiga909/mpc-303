#pragma once

#include "seq/Pattern.h"
#include <array>

namespace acid3::presets
{
struct Sound
{
    const char* name;
    float cutoff, reso, envmod, decay, accent;
    bool square;
};

/** Factory sound presets (SPEC §7.3). */
const std::array<Sound, 8>& sounds();

/** Factory patterns. Original lines written for this project, not transcriptions. */
const std::array<const char*, 8>& patternTexts();
const std::array<const char*, 8>& patternNames();

/** The 16 initial pattern slots: 8 factory patterns + 8 empty (all rests). */
std::array<Pattern, numPatterns> initialPatterns();
} // namespace acid3::presets
