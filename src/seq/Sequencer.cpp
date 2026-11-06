#include "Sequencer.h"
#include <cmath>

namespace acid3
{
void EventList::sort()
{
    for (int i = 1; i < size; ++i)
    {
        const auto e = events[(size_t) i];
        int j = i - 1;
        while (j >= 0 && events[(size_t) j].sampleOffset > e.sampleOffset)
        {
            events[(size_t) j + 1] = events[(size_t) j];
            --j;
        }
        events[(size_t) j + 1] = e;
    }
}

void Sequencer::reset()
{
    wasPlaying = false;
    lastStepStarted = -1000000;
    noteSounding = slideIntoNext = false;
    releasePpq = -1.0;
    currentStep = -1;
}

double Sequencer::stepStartPpq (long long n) const
{
    // swing delays the off-beat 16ths by up to 3/8 of a step at 75 %
    const bool offBeat = (n & 1) != 0;
    return (double) n * stepPpq + (offBeat ? swing * 0.5 * stepPpq : 0.0);
}

void Sequencer::startStep (long long n, int offset, EventList& out)
{
    const int len = pattern.length;
    const int idx = (int) (((n % len) + len) % len);
    const Step& cur = pattern.step (idx);
    const Step& next = pattern.step (idx + 1);
    currentStep = idx;
    lastStepStarted = n;

    if (cur.gate == Gate::Rest)
    {
        if (noteSounding)
            out.add ({ offset, VoiceEvent::Type::Release, 0, false });
        noteSounding = slideIntoNext = false;
        releasePpq = -1.0;
        return;
    }

    if (cur.gate == Gate::Note)
    {
        const int note = sequencerRootNote + transpose + cur.note + 12 * cur.octave;
        const bool slide = slideIntoNext && noteSounding;
        out.add ({ offset, slide ? VoiceEvent::Type::Slide : VoiceEvent::Type::Trigger, note, cur.accent });
        noteSounding = true;
    }
    // Tie: the gate simply stays open (only meaningful if a note is sounding)

    if (! noteSounding)
        return;

    // 303 gate logic: slide on this step holds the gate into a following note; a following
    // tie always holds it. Otherwise the gate closes after gateFraction of the step.
    const bool nextHolds = next.gate == Gate::Tie || (cur.slide && next.gate == Gate::Note);
    slideIntoNext = cur.slide && next.gate == Gate::Note;
    releasePpq = nextHolds ? -1.0 : stepStartPpq (n) + gateFraction * stepPpq;
}

void Sequencer::process (const Transport& t, int numSamples, double sampleRate, EventList& out)
{
    if (! t.playing)
    {
        if (wasPlaying && noteSounding)
            out.add ({ 0, VoiceEvent::Type::Release, 0, false });
        if (wasPlaying)
            reset();
        return;
    }

    const double ppqPerSample = t.bpm / (60.0 * sampleRate);
    const double p0 = t.ppq;
    const double p1 = p0 + numSamples * ppqPerSample;

    // transport jump / loop wrap / start: drop scheduled state
    if (! wasPlaying || std::fabs (p0 - expectedPpq) > 1e-6 + ppqPerSample)
    {
        if (wasPlaying && noteSounding)
            out.add ({ 0, VoiceEvent::Type::Release, 0, false });
        noteSounding = slideIntoNext = false;
        releasePpq = -1.0;
        // steps that started before the block are skipped (playback started mid-step)
        lastStepStarted = (long long) std::floor (p0 / stepPpq) - 2;
    }
    wasPlaying = true;
    expectedPpq = p1;

    // Block membership is decided in the sample domain so rounding in the host's ppq can never
    // put an event into two blocks or none: an event at x samples maps to ceil(x - eps).
    auto offsetOf = [&] (double ppq) { return (long long) std::ceil ((ppq - p0) / ppqPerSample - 1e-6); };
    auto inBlock = [&] (double ppq) { const auto o = offsetOf (ppq); return o >= 0 && o < numSamples; };

    const long long firstN = (long long) std::floor (p0 / stepPpq) - 1;
    const long long lastN = (long long) std::floor (p1 / stepPpq) + 1;
    for (long long n = firstN; n <= lastN; ++n)
    {
        if (n <= lastStepStarted)
            continue;
        const double ps = stepStartPpq (n);
        if (! inBlock (ps))
            continue;

        // a pending release that falls before this step start comes first
        if (releasePpq >= 0.0 && offsetOf (releasePpq) < offsetOf (ps))
        {
            out.add ({ (int) std::max (0LL, offsetOf (releasePpq)), VoiceEvent::Type::Release, 0, false });
            noteSounding = false;
            releasePpq = -1.0;
        }
        startStep (n, (int) offsetOf (ps), out);
    }

    if (releasePpq >= 0.0 && offsetOf (releasePpq) < numSamples)
    {
        out.add ({ (int) std::max (0LL, offsetOf (releasePpq)), VoiceEvent::Type::Release, 0, false });
        noteSounding = slideIntoNext = false;
        releasePpq = -1.0;
    }
}
} // namespace acid3
