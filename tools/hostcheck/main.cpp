// acid3-hostcheck: loads the built VST3 through JUCE's VST3 hosting code and exercises it
// the way a DAW would (SPEC §9.4 smoke test; pluginval remains the reference validator).
//   acid3-hostcheck path/to/Acid3.vst3
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <chrono>
#include <iostream>

using namespace juce;

namespace
{
int failures = 0;
void check (bool ok, const String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (! ok) ++failures;
}

/** Renders numBlocks with optional host transport; returns peak, flags non-finite output. */
struct Playhead : public AudioPlayHead
{
    double ppq = 0.0, bpm = 128.0;
    bool playing = true;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

float render (AudioPluginInstance& p, double sr, int block, int numBlocks, bool withMidi, Playhead* ph, bool& finite)
{
    AudioBuffer<float> buf (2, block);
    MidiBuffer midi;
    float peak = 0.0f;
    for (int b = 0; b < numBlocks; ++b)
    {
        midi.clear();
        if (withMidi && b % 8 == 0) midi.addEvent (MidiMessage::noteOn (1, 36 + (b / 8) % 12, (uint8) (b % 16 == 0 ? 127 : 80)), 0);
        if (withMidi && b % 8 == 4) midi.addEvent (MidiMessage::noteOff (1, 36 + (b / 8) % 12), jmax (0, block - 1));
        if (withMidi && b % 32 == 7) midi.addEvent (MidiMessage::controllerEvent (1, 74, (b * 7) % 128), 0);
        buf.clear();
        p.processBlock (buf, midi);
        if (ph != nullptr) ph->ppq += block * ph->bpm / (60.0 * sr);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float s = buf.getSample (ch, i);
                finite = finite && std::isfinite (s);
                peak = jmax (peak, std::abs (s));
            }
    }
    return peak;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: acid3-hostcheck Acid3.vst3\n"; return 2; }
    ScopedJuceInitialiser_GUI init;

    AudioPluginFormatManager fm;
    addDefaultFormatsToManager (fm);
    OwnedArray<PluginDescription> types;
    VST3PluginFormat vst3;
    vst3.findAllTypesForFile (types, String (argv[1]));
    check (types.size() == 1, "VST3 scanned, one plugin found");
    if (types.isEmpty()) return 1;
    const auto& desc = *types[0];
    std::cout << "  plugin: " << desc.name << " by " << desc.manufacturerName << ", instrument=" << (int) desc.isInstrument << "\n";
    check (desc.isInstrument, "registered as instrument");

    String err;
    auto plugin = fm.createPluginInstance (desc, 48000.0, 512, err);
    check (plugin != nullptr, "instance created " + err);
    if (plugin == nullptr) return 1;
    std::cout << "  parameters: " << plugin->getParameters().size() << ", programs: " << plugin->getNumPrograms()
              << ", latency: " << plugin->getLatencySamples() << "\n";
    check (plugin->getParameters().size() >= 15, "exposes all parameters");
    check (plugin->acceptsMidi(), "accepts MIDI");

    // ---- sample rates / block sizes / MIDI ---------------------------------------------
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int block : { 1, 64, 480, 4096 })
        {
            plugin->releaseResources();
            plugin->setPlayConfigDetails (0, 2, sr, block);
            plugin->prepareToPlay (sr, block);
            bool finite = true;
            const int blocks = jmax (8, (int) (sr * 0.5 / block));
            const float peak = render (*plugin, sr, block, blocks, true, nullptr, finite);
            if (sr == 48000.0 && block == 64)
                check (plugin->getLatencySamples() > 0, "reports oversampling latency (" + String (plugin->getLatencySamples()) + " samples)");
            check (finite && peak > 1e-4f && peak < 4.0f,
                   "sr " + String (sr) + " block " + String (block) + " MIDI render, peak " + String (peak, 3));
        }

    // ---- host-synced sequencer ----------------------------------------------------------
    {
        Playhead ph;
        plugin->setPlayHead (&ph);
        plugin->releaseResources();
        plugin->setPlayConfigDetails (0, 2, 48000.0, 256);
        plugin->prepareToPlay (48000.0, 256);
        bool finite = true;
        const float peak = render (*plugin, 48000.0, 256, 48000 * 2 / 256, false, &ph, finite);
        check (finite && peak > 1e-3f, "sequencer plays from host transport, peak " + String (peak, 3));
        ph.playing = false;
        render (*plugin, 48000.0, 256, 200, false, &ph, finite); // let release tails die
        const float silent = render (*plugin, 48000.0, 256, 10, false, &ph, finite);
        check (silent < 1e-3f, "silent after host stop (" + String (silent, 6) + ")");
        plugin->setPlayHead (nullptr);
    }

    // ---- state round trip --------------------------------------------------------------
    {
        // JUCE's VST3 wrapper adds 16x128 "MIDI CC" proxy parameters; they are not plugin state
        // ... nor are the wrapper's Bypass / Program entries (Program loads a preset over the others)
        auto isOwn = [] (AudioProcessorParameter* prm)
        {
            const auto n = prm->getName (64);
            return ! n.startsWith ("MIDI CC") && n != "Bypass" && n != "Program";
        };
        auto& ps = plugin->getParameters();
        Random rnd (7);
        for (auto* prm : ps)
            if (isOwn (prm))
            {
                const int steps = prm->getNumSteps();
                float v = rnd.nextFloat();
                if (prm->isDiscrete() && steps > 1) v = std::round (v * (float) (steps - 1)) / (float) (steps - 1);
                prm->setValueNotifyingHost (v);
            }
        MemoryBlock state;
        plugin->getStateInformation (state);

        auto other = fm.createPluginInstance (desc, 48000.0, 512, err);
        other->setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        int own = 0;
        for (int i = 0; i < ps.size(); ++i)
            if (isOwn (ps[i]))
            {
                ++own;
                // int parameters show up as continuous on the host side but the plugin snaps them
                // to whole values, so allow half of the coarsest step (Pattern: 1/15)
                const bool eq = std::abs (other->getParameters()[i]->getValue() - ps[i]->getValue()) < 0.034f;
                if (! eq) std::cout << "    mismatch: " << ps[i]->getName (64) << " " << ps[i]->getValue() << " vs " << other->getParameters()[i]->getValue() << " steps " << ps[i]->getNumSteps() << "\n";
                same = same && eq;
            }
        check (same, "parameter state round trip (" + String (own) + " plugin params)");

        MemoryBlock again;
        other->getStateInformation (again);
        check (again == state, "state blob identical after reload (includes patterns)");
    }

    // ---- CPU ---------------------------------------------------------------------------------
    {
        Playhead ph;
        plugin->setPlayHead (&ph);
        plugin->releaseResources();
        plugin->setPlayConfigDetails (0, 2, 48000.0, 128);
        plugin->prepareToPlay (48000.0, 128);
        bool finite = true;
        const int blocks = 48000 * 20 / 128; // 20 s of audio
        const auto t0 = std::chrono::steady_clock::now();
        render (*plugin, 48000.0, 128, blocks, false, &ph, finite);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double load = 100.0 * secs / 20.0;
        std::cout << "  CPU: " << load << " % of one core (48 kHz, 128 block, 4x oversampling)\n";
        check (load < 3.0, "CPU budget < 3 %");
        plugin->setPlayHead (nullptr);
    }

    // ---- editor --------------------------------------------------------------------------------
    {
        std::unique_ptr<AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
        check (ed != nullptr && ed->getWidth() > 0, "editor opens");
        ed.reset();
    }

    plugin->releaseResources();
    std::cout << (failures == 0 ? "ALL CHECKS PASSED\n" : "CHECKS FAILED: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
