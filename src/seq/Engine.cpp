#include "Engine.h"
#include <algorithm>

namespace acid3
{
void Engine::handleVoiceEvent (const VoiceEvent& e)
{
    switch (e.type)
    {
        case VoiceEvent::Type::Trigger: voice.trigger (e.note, e.accent); sequencerOwnsVoice = true; break;
        case VoiceEvent::Type::Slide:   voice.slideTo (e.note, e.accent); sequencerOwnsVoice = true; break;
        case VoiceEvent::Type::Release: if (heldCount == 0) voice.release(); break;
    }
}

void Engine::handleMidi (const MidiEvent& e)
{
    switch (e.type)
    {
        case MidiEvent::Type::NoteOn:
        {
            if (e.velocity == 0)
                return handleMidi ({ e.sampleOffset, MidiEvent::Type::NoteOff, e.note, 0, 0.0 });
            const bool accent = e.velocity >= accentThreshold;
            // remove a duplicate of this note, then push on top
            for (int i = 0; i < heldCount; ++i)
                if (held[(size_t) i].note == e.note)
                {
                    for (int j = i; j < heldCount - 1; ++j) held[(size_t) j] = held[(size_t) j + 1];
                    --heldCount;
                    break;
                }
            const bool legato = heldCount > 0 && voice.isGateOn();
            if (heldCount < (int) held.size())
                held[(size_t) heldCount++] = { e.note, accent };
            sequencerOwnsVoice = false;
            if (legato) voice.slideTo (e.note, accent);
            else        voice.trigger (e.note, accent);
            break;
        }
        case MidiEvent::Type::NoteOff:
        {
            int found = -1;
            for (int i = 0; i < heldCount; ++i)
                if (held[(size_t) i].note == e.note) { found = i; break; }
            if (found < 0) return;
            const bool wasTop = found == heldCount - 1;
            for (int j = found; j < heldCount - 1; ++j) held[(size_t) j] = held[(size_t) j + 1];
            --heldCount;
            if (heldCount == 0)
                voice.release();
            else if (wasTop) // return to the previous held note with slide
                voice.slideTo (held[(size_t) heldCount - 1].note, held[(size_t) heldCount - 1].accent);
            break;
        }
        case MidiEvent::Type::PitchBend:
            bend = voiceParams.pitchBend = e.bendSemis;
            break;
        case MidiEvent::Type::AllNotesOff:
            heldCount = 0;
            voice.release();
            break;
    }
}

void Engine::process (float* out, int numSamples, const Transport& transport, const MidiEvent* midi, int numMidi)
{
    seqEvents.clear();
    if (seqEnabled)
        sequencer.process (transport, numSamples, sr, seqEvents);
    seqEvents.sort();

    int si = 0, mi = 0, pos = 0;
    while (pos < numSamples)
    {
        while (mi < numMidi && midi[mi].sampleOffset <= pos)
            handleMidi (midi[mi++]);
        while (si < seqEvents.size && seqEvents.events[(size_t) si].sampleOffset <= pos)
            handleVoiceEvent (seqEvents.events[(size_t) si++]);

        int end = numSamples;
        if (mi < numMidi) end = std::min (end, midi[mi].sampleOffset);
        if (si < seqEvents.size) end = std::min (end, seqEvents.events[(size_t) si].sampleOffset);
        if (end <= pos) end = pos + 1;

        voice.setParams (voiceParams);
        voice.render (out + pos, end - pos);
        pos = end;
    }
}
} // namespace acid3
