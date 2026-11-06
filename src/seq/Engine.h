#pragma once

#include "../dsp/Voice.h"
#include "Sequencer.h"
#include <array>

namespace acid3
{
struct MidiEvent
{
    enum class Type : uint8_t { NoteOn, NoteOff, PitchBend, AllNotesOff };
    int sampleOffset = 0;
    Type type = Type::NoteOn;
    int note = 0;
    int velocity = 0;
    double bendSemis = 0.0;
};

/** Voice + sequencer + MIDI note handling. JUCE-free; used by the plugin, render CLI and tests. */
class Engine
{
public:
    void prepare (double sampleRate, uint32_t seed = 0x303u)
    {
        sr = sampleRate;
        voice.prepare (sampleRate, seed);
        sequencer.reset();
        heldCount = 0;
    }

    void setParams (const VoiceParams& p) { voiceParams = p; voiceParams.pitchBend = bend; }
    void setSequencerEnabled (bool on) { if (seqEnabled && ! on && sequencerOwnsVoice) { voice.release(); } seqEnabled = on; }
    void setAccentVelocityThreshold (int v) { accentThreshold = v; }
    Sequencer& getSequencer() { return sequencer; }
    Voice& getVoice() { return voice; }
    int getLatencySamples() const { return voice.getLatencySamples(); }

    /** Renders one block. midi must be sorted by sampleOffset. */
    void process (float* out, int numSamples, const Transport& transport, const MidiEvent* midi, int numMidi);

private:
    void handleMidi (const MidiEvent& e);
    void handleVoiceEvent (const VoiceEvent& e);

    double sr = 44100.0;
    Voice voice;
    VoiceParams voiceParams;
    double bend = 0.0;
    Sequencer sequencer;
    EventList seqEvents;
    bool seqEnabled = true;
    bool sequencerOwnsVoice = false;
    int accentThreshold = 100;

    // MIDI note stack, last-note priority
    struct Held { int note; bool accent; };
    std::array<Held, 32> held {};
    int heldCount = 0;
};
} // namespace acid3
