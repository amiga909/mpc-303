#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace acid3
{
enum class Gate : uint8_t { Note, Tie, Rest };

struct Step
{
    int note = 0;        // 0..12 semitones above the root (C..C)
    int octave = 0;      // -1, 0, +1
    bool accent = false;
    bool slide = false;  // slide from this step into the next one
    Gate gate = Gate::Note;

    bool operator== (const Step& o) const
    {
        return note == o.note && octave == o.octave && accent == o.accent && slide == o.slide && gate == o.gate;
    }
};

struct Pattern
{
    static constexpr int maxSteps = 16;
    std::array<Step, maxSteps> steps {};
    int length = 16;

    bool operator== (const Pattern& o) const { return steps == o.steps && length == o.length; }

    const Step& step (int i) const { return steps[(size_t) (((i % length) + length) % length)]; }

    /** Compact text form: one token per step, e.g. "C4as" = note C, octave 0... see toString(). */
    std::string toString() const;
    static bool fromString (const std::string& text, Pattern& out);
};

constexpr int numPatterns = 16;
constexpr int sequencerRootNote = 36; // C2 (~65 Hz) - the 303's typical register
} // namespace acid3
