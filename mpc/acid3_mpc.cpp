// Acid3 on MPC OS: the JUCE-free DSP core (src/dsp, src/seq) behind the mpc-vst-plugins engine interface
// (mpc-vst-plugins/wrapper/engine.h). 44.1 kHz, int16 stereo, render() of 1..128 frames (SAMPLE_ACCURATE).
// The sequencer follows MPC's song position (HAS_SONG_POS); MIDI notes from pads, keys and clips play the voice.
#include "seq/Engine.h"
#include "plugin/Presets.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "engine.h"
}

namespace
{
using namespace acid3;

constexpr double kRate = 44100.0;
constexpr int kMaxFrames = 128;
constexpr int kMaxMidi = 256;

// Parameter keys, in params.json order (the VST index). Append only.
enum Key { kCutoff, kReso, kEnvMod, kDecay, kAccent, kWave, kTune, kVolume, kDrift, kSeq, kPattern, kTranspose, kSwing, kAccThr, kNumKeys };
const char* const keyNames[kNumKeys] = { "cutoff", "reso", "envmod", "decay", "accent", "wave", "tune", "volume", "drift", "seq",
                                         "pattern", "transpose", "swing", "accthr" };
const double defaults[kNumKeys] = { 0.5, 0.5, 0.5, 0.5, 0.5, 0, 0, 0, 1, 0, 0, 0, 0, 100 };

// MIDI CCs that move a knob (same map as the desktop plugin). CC 20-35 belong to the wrapper's Q-Link map.
struct CcMap { int cc; Key key; double lo, hi; };
const CcMap ccMap[] = { { 74, kCutoff, 0, 1 }, { 71, kReso, 0, 1 }, { 75, kDecay, 0, 1 }, { 76, kEnvMod, 0, 1 },
                        { 77, kAccent, 0, 1 }, { 7, kVolume, -60, 6 } };

struct Instance
{
    Engine engine;
    std::array<Pattern, numPatterns> patterns = presets::initialPatterns();
    double v[kNumKeys];
    bool dirty = true;
    int appliedPattern = -1;

    // transport, from song_pos() once per host block
    bool hostPlaying = false;
    double blockPpq = -1.0, bpm = 0.0;
    long framesIntoBlock = 0;

    MidiEvent midi[kMaxMidi];
    int numMidi = 0;

    unsigned changedMask = 0;   // keys moved by MIDI CC, reported through mpc_engine_changed()
    float mono[kMaxFrames];
};

int keyIndex (const char* k)
{
    for (int i = 0; i < kNumKeys; ++i)
        if (! std::strcmp (k, keyNames[i])) return i;
    return -1;
}

void applyParams (Instance& s)
{
    VoiceParams p;
    p.cutoff = s.v[kCutoff];
    p.reso = s.v[kReso];
    p.envMod = s.v[kEnvMod];
    p.decay = s.v[kDecay];
    p.accent = s.v[kAccent];
    p.square = s.v[kWave] > 0.5;
    p.tune = s.v[kTune];
    p.volumeDb = s.v[kVolume];
    p.drift = s.v[kDrift] > 0.5;
    s.engine.setParams (p);

    s.engine.setSequencerEnabled (s.v[kSeq] > 0.5);
    s.engine.setAccentVelocityThreshold ((int) std::lround (s.v[kAccThr]));
    auto& seq = s.engine.getSequencer();
    seq.setSwing (s.v[kSwing] / 100.0);
    seq.setTranspose ((int) std::lround (s.v[kTranspose]));
    const int pat = (int) std::lround (s.v[kPattern]);
    if (pat != s.appliedPattern && pat >= 0 && pat < numPatterns)
    {
        seq.setPattern (s.patterns[(size_t) pat]);
        s.appliedPattern = pat;
    }
    s.dirty = false;
}

void setValue (Instance& s, int k, double x)
{
    if (k == kVolume) x = x < -60 ? -60 : x > 6 ? 6 : x;
    s.v[k] = x;
    s.dirty = true;
}

void queueMidi (Instance& s, const MidiEvent& e)
{
    if (s.numMidi < kMaxMidi) s.midi[s.numMidi++] = e;
}

// ---- mpc_engine_t ----------------------------------------------------------------------------------------------------

void* create (const char*)
{
    auto* s = new Instance();
    for (int i = 0; i < kNumKeys; ++i) s->v[i] = defaults[i];
    s->engine.prepare (kRate);
    applyParams (*s);
    return s;
}

void destroy (void* inst) { delete static_cast<Instance*> (inst); }

void midi (void* inst, const uint8_t* m, int len)
{
    auto& s = *static_cast<Instance*> (inst);
    if (len < 1) return;
    const int status = m[0] & 0xf0;
    const int d1 = len > 1 ? m[1] & 0x7f : 0, d2 = len > 2 ? m[2] & 0x7f : 0;
    switch (status)
    {
        case 0x90:
            queueMidi (s, { 0, d2 > 0 ? MidiEvent::Type::NoteOn : MidiEvent::Type::NoteOff, d1, d2, 0.0 });
            break;
        case 0x80:
            queueMidi (s, { 0, MidiEvent::Type::NoteOff, d1, 0, 0.0 });
            break;
        case 0xe0:
            queueMidi (s, { 0, MidiEvent::Type::PitchBend, 0, 0, (((d2 << 7) | d1) - 8192) / 8192.0 * 2.0 });
            break;
        case 0xb0:
            if (d1 == 120 || d1 == 123)
            {
                queueMidi (s, { 0, MidiEvent::Type::AllNotesOff, 0, 0, 0.0 });
                break;
            }
            for (const auto& c : ccMap)
                if (c.cc == d1)
                {
                    setValue (s, c.key, c.lo + (c.hi - c.lo) * d2 / 127.0);
                    s.changedMask |= 1u << c.key;
                }
            break;
        default: break;
    }
}

// "state": every value as "key=value;" (patterns are factory-only on MPC, so the pattern index is enough)
int writeState (const Instance& s, char* buf, int n)
{
    int used = 0;
    for (int i = 0; i < kNumKeys && used < n; ++i)
        used += std::snprintf (buf + used, (size_t) (n - used), "%s=%.6g;", keyNames[i], s.v[i]);
    return used < n ? used : 0;
}

void readState (Instance& s, const char* text)
{
    char key[32];
    const char* p = text;
    while (*p)
    {
        const char* eq = std::strchr (p, '=');
        if (eq == nullptr) break;
        const size_t kl = (size_t) (eq - p) < sizeof key - 1 ? (size_t) (eq - p) : sizeof key - 1;
        std::memcpy (key, p, kl);
        key[kl] = 0;
        const int k = keyIndex (key);
        if (k >= 0) setValue (s, k, std::atof (eq + 1));
        const char* semi = std::strchr (eq, ';');
        if (semi == nullptr) break;
        p = semi + 1;
    }
}

void set_param (void* inst, const char* key, const char* val)
{
    auto& s = *static_cast<Instance*> (inst);
    if (! std::strcmp (key, "state")) { readState (s, val); return; }
    const int k = keyIndex (key);
    if (k >= 0) setValue (s, k, std::atof (val));
}

int get_param (void* inst, const char* key, char* buf, int n)
{
    auto& s = *static_cast<Instance*> (inst);
    if (! std::strcmp (key, "state")) return writeState (s, buf, n);

    // value text for the screen
    const size_t kl = std::strlen (key);
    static const char suffix[] = "_display";
    if (kl > sizeof suffix - 1 && ! std::strcmp (key + kl - (sizeof suffix - 1), suffix))
    {
        char base[32];
        const size_t bl = kl - (sizeof suffix - 1);
        if (bl >= sizeof base) return 0;
        std::memcpy (base, key, bl);
        base[bl] = 0;
        const int k = keyIndex (base);
        const double x = k >= 0 ? s.v[k] : 0.0;
        switch (k)
        {
            case kCutoff:
            {
                const double hz = Voice::cutoffKnobToHz (x);
                return hz >= 1000.0 ? std::snprintf (buf, (size_t) n, "%.2f kHz", hz / 1000.0)
                                    : std::snprintf (buf, (size_t) n, "%d Hz", (int) std::lround (hz));
            }
            case kDecay: return std::snprintf (buf, (size_t) n, "%d ms", (int) std::lround (Voice::decayKnobToMs (x)));
            case kReso: case kEnvMod: case kAccent:
                return std::snprintf (buf, (size_t) n, "%d %%", (int) std::lround (x * 100.0));
            case kVolume: return x <= -59.9 ? std::snprintf (buf, (size_t) n, "-inf dB") : std::snprintf (buf, (size_t) n, "%.1f dB", x);
            case kTune: return std::snprintf (buf, (size_t) n, "%+.2f st", x);
            default: return 0;   // the wrapper's own text
        }
    }

    const int k = keyIndex (key);
    if (k < 0) return 0;
    return std::snprintf (buf, (size_t) n, "%.6g", s.v[k]);
}

void render (void* inst, int16_t* out, int frames)
{
    auto& s = *static_cast<Instance*> (inst);
    if (frames > kMaxFrames) frames = kMaxFrames;
    if (s.dirty) applyParams (s);

    Transport t;
    if (s.hostPlaying && s.blockPpq >= 0.0 && s.bpm > 0.0)
    {
        t.playing = true;
        t.bpm = s.bpm;
        t.ppq = s.blockPpq + s.framesIntoBlock * s.bpm / (60.0 * kRate);
    }
    s.framesIntoBlock += frames;

    s.engine.process (s.mono, frames, t, s.midi, s.numMidi);
    s.numMidi = 0;

    for (int i = 0; i < frames; ++i)
    {
        float x = s.mono[i] * 32767.0f;
        x = x > 32767.0f ? 32767.0f : x < -32768.0f ? -32768.0f : x;
        const auto smp = (int16_t) std::lrint (x);
        out[2 * i] = smp;
        out[2 * i + 1] = smp;
    }
}

void song_pos (void* inst, double ppq, double bpm, int playing, int)
{
    auto& s = *static_cast<Instance*> (inst);
    s.hostPlaying = playing != 0;
    s.blockPpq = ppq;
    s.bpm = bpm;
    s.framesIntoBlock = 0;
}

const mpc_engine_t engineApi = { create, destroy, midi, set_param, get_param, render, nullptr, song_pos };
} // namespace

extern "C" const mpc_engine_t* mpc_engine (void) { return &engineApi; }

// CC-moved knobs, so the MPC screen follows (wrapper/engine.h)
extern "C" int mpc_engine_changed (void* inst, char* key, int len)
{
    auto& s = *static_cast<Instance*> (inst);
    for (int k = 0; k < kNumKeys; ++k)
        if (s.changedMask & (1u << k))
        {
            s.changedMask &= ~(1u << k);
            std::snprintf (key, (size_t) len, "%s", keyNames[k]);
            return 1;
        }
    return 0;
}
