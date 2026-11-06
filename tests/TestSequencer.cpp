#include "plugin/Presets.h"
#include "seq/Engine.h"
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <random>

using namespace acid3;

namespace
{
struct AbsEvent { long pos; VoiceEvent::Type type; int note; bool accent; };

/** Runs the sequencer with random block sizes over a tempo map; returns events in absolute samples. */
std::vector<AbsEvent> run (const Pattern& pat, double sr, long total, std::function<double (long)> bpmAt,
                           uint32_t seed = 1, double swing = 0.0)
{
    Sequencer s;
    s.setPattern (pat);
    s.setSwing (swing);
    std::mt19937 rng (seed);
    std::uniform_int_distribution<int> bs (1, 4096);
    std::vector<AbsEvent> out;
    EventList ev;
    double ppq = 0.0;
    for (long pos = 0; pos < total;)
    {
        const int n = (int) std::min<long> (bs (rng), total - pos);
        Transport t; t.playing = true; t.ppq = ppq; t.bpm = bpmAt (pos);
        ev.clear();
        s.process (t, n, sr, ev);
        ev.sort();
        for (int i = 0; i < ev.size; ++i)
        {
            const auto& e = ev.events[(size_t) i];
            out.push_back ({ pos + e.sampleOffset, e.type, e.note, e.accent });
        }
        ppq += n * t.bpm / (60.0 * sr);
        pos += n;
    }
    return out;
}

Pattern pat (const char* text) { Pattern p; REQUIRE (Pattern::fromString (text, p)); return p; }
} // namespace

TEST_CASE ("Pattern: text round trip", "[seq]")
{
    const auto p = pat ("n0- n12+as t0 r0 n7s n3a");
    Pattern q;
    REQUIRE (Pattern::fromString (p.toString(), q));
    CHECK (p == q);
    CHECK (q.length == 6);
    CHECK (q.steps[1].note == 12);
    CHECK (q.steps[1].octave == 1);
    CHECK (q.steps[1].accent);
    CHECK (q.steps[1].slide);
    CHECK (q.steps[2].gate == Gate::Tie);
}

TEST_CASE ("Sequencer: sample-accurate onsets with random block sizes", "[seq]")
{
    const double sr = 48000.0;
    const auto ev = run (pat ("n0 n1 n2 n3"), sr, 48000 * 4, [] (long) { return 120.0; });
    // 120 bpm -> 16th = 6000 samples, gate 50% -> release at +3000
    long expected = 0;
    int triggers = 0;
    for (const auto& e : ev)
    {
        if (e.type == VoiceEvent::Type::Trigger)
        {
            CHECK (e.pos == expected);
            CHECK (e.note == sequencerRootNote + triggers % 4);
            expected += 6000;
            ++triggers;
        }
        else if (e.type == VoiceEvent::Type::Release)
            CHECK (e.pos == expected - 6000 + 3000);
    }
    CHECK (triggers == 32);
}

TEST_CASE ("Sequencer: follows tempo changes", "[seq]")
{
    const double sr = 48000.0;
    // 120 bpm for 1 s (8 steps), then 60 bpm: steps every 12000 samples
    const auto ev = run (pat ("n0"), sr, 48000 * 3, [] (long pos) { return pos < 48000 ? 120.0 : 60.0; });
    std::vector<long> on;
    for (const auto& e : ev) if (e.type == VoiceEvent::Type::Trigger) on.push_back (e.pos);
    REQUIRE (on.size() >= 12);
    CHECK (on[7] == 42000);
    // the block straddling the tempo change uses its start tempo; check steps after it
    for (size_t i = 1; i < on.size(); ++i)
        if (on[i - 1] > 48000 + 4096)
            CHECK (std::labs ((on[i] - on[i - 1]) - 12000) <= 1);
}

TEST_CASE ("Sequencer: slide holds gate and emits Slide, tie holds gate", "[seq]")
{
    const double sr = 48000.0;
    const auto ev = run (pat ("n0s n5 n7 t0 r0 r0 r0 r0"), sr, 6000 * 8, [] (long) { return 120.0; });
    REQUIRE (ev.size() >= 4);
    CHECK ((ev[0].type == VoiceEvent::Type::Trigger && ev[0].pos == 0));
    CHECK ((ev[1].type == VoiceEvent::Type::Slide && ev[1].pos == 6000 && ev[1].note == sequencerRootNote + 5));
    CHECK ((ev[2].type == VoiceEvent::Type::Release && ev[2].pos == 9000));
    CHECK ((ev[3].type == VoiceEvent::Type::Trigger && ev[3].pos == 12000));
    // n7 followed by tie: gate held through step 3, released half way into the tie step
    CHECK ((ev[4].type == VoiceEvent::Type::Release && ev[4].pos == 18000 + 3000));
}

TEST_CASE ("Sequencer: loop wrap restarts at step 0", "[seq]")
{
    Sequencer s;
    s.setPattern (pat ("n0 n1 n2 n3 n4 n5 n6 n7"));
    EventList ev;
    const double sr = 48000.0;
    Transport t; t.playing = true; t.bpm = 120.0;
    // play ppq 0..1 (4 steps), then host loops back to 0
    for (int pos = 0; pos < 24000; pos += 1000)
    {
        t.ppq = pos * 2.0 / sr;
        s.process (t, 1000, sr, ev);
    }
    ev.clear();
    t.ppq = 0.0;
    s.process (t, 1000, sr, ev);
    ev.sort();
    bool sawStep0 = false;
    for (int i = 0; i < ev.size; ++i)
        if (ev.events[(size_t) i].type == VoiceEvent::Type::Trigger && ev.events[(size_t) i].note == sequencerRootNote)
            sawStep0 = ev.events[(size_t) i].sampleOffset == 0;
    CHECK (sawStep0);
}

TEST_CASE ("Sequencer: swing delays off-beat steps only", "[seq]")
{
    const double sr = 48000.0;
    const auto ev = run (pat ("n0 n1"), sr, 24000, [] (long) { return 120.0; }, 3, 0.5);
    std::vector<long> on;
    for (const auto& e : ev) if (e.type == VoiceEvent::Type::Trigger) on.push_back (e.pos);
    REQUIRE (on.size() == 4);
    CHECK (on[0] == 0);
    CHECK (on[1] == 6000 + 1500);
    CHECK (on[2] == 12000);
}

TEST_CASE ("Factory patterns all parse and use 16 steps", "[seq]")
{
    for (const char* text : presets::patternTexts())
    {
        Pattern p;
        INFO (text);
        CHECK (Pattern::fromString (text, p));
        CHECK (p.length == 16);
    }
}
