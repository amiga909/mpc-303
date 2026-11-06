// acid3-render: offline renderer for verification (SPEC §9.1)
//   acid3-render --pattern "n0- n0-as n12 ..." | --pattern-file f.txt
//                [--bpm 130] [--bars 4] [--sr 48000] [--seed 1] [--no-drift]
//                [--params "cutoff=0.4,reso=0.8,envmod=0.7,decay=0.3,accent=0.8,wave=saw,tune=0,volume=0"]
//                --out out.wav
#include "../../src/seq/Engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace acid3;

static bool writeWav (const std::string& path, const std::vector<float>& data, int sampleRate)
{
    std::ofstream f (path, std::ios::binary);
    if (! f) return false;
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t bytes = (uint32_t) (data.size() * 4);
    f.write ("RIFF", 4); u32 (36 + bytes); f.write ("WAVE", 4);
    f.write ("fmt ", 4); u32 (16); u16 (3); u16 (1); u32 ((uint32_t) sampleRate); u32 ((uint32_t) sampleRate * 4); u16 (4); u16 (32);
    f.write ("data", 4); u32 (bytes);
    f.write ((const char*) data.data(), bytes);
    return (bool) f;
}

static bool applyParam (VoiceParams& p, const std::string& key, const std::string& val)
{
    if (key == "wave") { p.square = (val == "square" || val == "sq"); return true; }
    const double v = std::stod (val);
    if (key == "cutoff") p.cutoff = v;
    else if (key == "reso") p.reso = v;
    else if (key == "envmod") p.envMod = v;
    else if (key == "decay") p.decay = v;
    else if (key == "accent") p.accent = v;
    else if (key == "tune") p.tune = v;
    else if (key == "volume") p.volumeDb = v;
    else return false;
    return true;
}

int main (int argc, char** argv)
{
    std::string patternText, out;
    double bpm = 130.0, bars = 4.0;
    int sr = 48000;
    uint32_t seed = 0x303u;
    bool drift = true;
    VoiceParams params;
    params.cutoff = 0.4; params.reso = 0.7; params.envMod = 0.6; params.decay = 0.4; params.accent = 0.7;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string { if (i + 1 >= argc) { std::cerr << "missing value for " << a << "\n"; std::exit (2); } return argv[++i]; };
        if (a == "--pattern") patternText = next();
        else if (a == "--pattern-file") { std::ifstream f (next()); std::stringstream ss; ss << f.rdbuf(); patternText = ss.str(); }
        else if (a == "--bpm") bpm = std::stod (next());
        else if (a == "--bars") bars = std::stod (next());
        else if (a == "--sr") sr = std::stoi (next());
        else if (a == "--seed") seed = (uint32_t) std::stoul (next());
        else if (a == "--no-drift") drift = false;
        else if (a == "--out") out = next();
        else if (a == "--params")
        {
            std::stringstream ss (next());
            std::string kv;
            while (std::getline (ss, kv, ','))
            {
                const auto eq = kv.find ('=');
                if (eq == std::string::npos || ! applyParam (params, kv.substr (0, eq), kv.substr (eq + 1)))
                { std::cerr << "bad param: " << kv << "\n"; return 2; }
            }
        }
        else { std::cerr << "unknown arg " << a << "\n"; return 2; }
    }
    if (out.empty() || patternText.empty()) { std::cerr << "need --out and --pattern/--pattern-file\n"; return 2; }

    Pattern pattern;
    if (! Pattern::fromString (patternText, pattern)) { std::cerr << "bad pattern\n"; return 2; }
    params.drift = drift;

    Engine engine;
    engine.setParams (params);
    engine.prepare (sr, seed);
    engine.getSequencer().setPattern (pattern);

    const int block = 256;
    const long total = (long) (bars * 4.0 * 60.0 / bpm * sr) + sr / 2; // + tail
    const long seqEnd = (long) (bars * 4.0 * 60.0 / bpm * sr);
    std::vector<float> audio ((size_t) total, 0.0f);
    Transport t;
    t.bpm = bpm;
    for (long pos = 0; pos < total; pos += block)
    {
        const int n = (int) std::min<long> (block, total - pos);
        t.playing = pos < seqEnd;
        t.ppq = (double) pos / sr * bpm / 60.0;
        engine.process (audio.data() + pos, n, t, nullptr, 0);
    }
    // compensate latency so onsets line up with the grid
    const int lat = engine.getLatencySamples();
    audio.erase (audio.begin(), audio.begin() + std::min<long> (lat, (long) audio.size()));

    float peak = 0.0f;
    for (float s : audio) peak = std::max (peak, std::fabs (s));
    if (! writeWav (out, audio, sr)) { std::cerr << "cannot write " << out << "\n"; return 1; }
    std::printf ("wrote %s  (%.2f s, peak %.3f, latency %d)\n", out.c_str(), audio.size() / (double) sr, peak, lat);
    return 0;
}
