#pragma once

#include "Pattern.h"
#include <array>

namespace acid3
{
struct Transport
{
    bool playing = false;
    double ppq = 0.0;      // position (quarter notes) at the first sample of the block
    double bpm = 120.0;
};

struct VoiceEvent
{
    enum class Type : uint8_t { Trigger, Slide, Release };
    int sampleOffset = 0;
    Type type = Type::Trigger;
    int note = 0;
    bool accent = false;
};

/** Fixed-capacity event list, no allocation on the audio thread. */
struct EventList
{
    static constexpr int capacity = 512;
    std::array<VoiceEvent, capacity> events {};
    int size = 0;

    void clear() { size = 0; }
    void add (const VoiceEvent& e) { if (size < capacity) events[(size_t) size++] = e; }
    /** Stable sort by sample offset (insertion sort - lists are tiny). */
    void sort();
};

/** 16th-note step sequencer, scheduled in the PPQ domain so it follows host tempo changes,
    loops and jumps with sample accuracy. Step n starts at ppq n/4 (+ swing on odd steps). */
class Sequencer
{
public:
    static constexpr double stepPpq = 0.25;

    void setPattern (const Pattern& p) { pattern = p; }
    const Pattern& getPattern() const { return pattern; }
    void setSwing (double amount01) { swing = amount01; } // 0..0.75
    void setTranspose (int semis) { transpose = semis; }
    void setGateFraction (double f) { gateFraction = f; }

    /** Generates voice events for one block. */
    void process (const Transport& t, int numSamples, double sampleRate, EventList& out);

    /** Index of the step that most recently started (for the GUI playhead), -1 if stopped. */
    int getCurrentStep() const { return currentStep; }
    void reset();

private:
    double stepStartPpq (long long n) const;
    void startStep (long long n, int offset, EventList& out);

    Pattern pattern;
    double swing = 0.0;
    int transpose = 0;
    double gateFraction = 0.5;

    bool wasPlaying = false;
    double expectedPpq = 0.0;
    long long lastStepStarted = -1000000;
    bool noteSounding = false;
    bool slideIntoNext = false;
    double releasePpq = -1.0; // < 0: none pending
    int currentStep = -1;
};
} // namespace acid3
