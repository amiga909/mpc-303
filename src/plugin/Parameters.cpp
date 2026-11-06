#include "Parameters.h"
#include "dsp/Voice.h"

namespace acid3::params
{
using namespace juce;

namespace
{
    String percent (float v, int) { return String (juce::roundToInt (v * 100.0f)) + " %"; }
}

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto attrs = [] { return AudioParameterFloatAttributes(); };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { wave, 1 }, "Waveform", StringArray { "Saw", "Square" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tune, 1 }, "Tuning", NormalisableRange<float> (-12.0f, 12.0f), 0.0f,
        attrs().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " st"; })));

    // Cutoff: knob position is linear in CV; CV -> Hz is exponential, 314 Hz .. 2.39 kHz [O303]
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { cutoff, 1 }, "Cut Off Freq", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        attrs().withStringFromValueFunction ([] (float v, int)
        {
            const double hz = Voice::cutoffKnobToHz (v);
            return hz >= 1000.0 ? String (hz / 1000.0, 2) + " kHz" : String (juce::roundToInt (hz)) + " Hz";
        })));

    // Resonance: the skew towards the top (1 - e^{-3r}) is applied inside the ladder [O303]
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { reso, 1 }, "Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        attrs().withStringFromValueFunction (percent)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { envmod, 1 }, "Env Mod", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        attrs().withStringFromValueFunction (percent)));

    // Decay: exponential 200 ms .. 2 s (MEG time constant) [O303]
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { decay, 1 }, "Decay", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        attrs().withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (Voice::decayKnobToMs (v))) + " ms"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { accent, 1 }, "Accent", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        attrs().withStringFromValueFunction (percent)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { volume, 1 }, "Volume", NormalisableRange<float> (-60.0f, 6.0f, 0.0f, 2.5f), 0.0f,
        attrs().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return v <= -59.9f ? String ("-inf dB") : String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { drift, 1 }, "Analog Drift", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { seqOn, 1 }, "Sequencer", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { swing, 1 }, "Swing", NormalisableRange<float> (0.0f, 0.75f), 0.0f,
        attrs().withStringFromValueFunction (percent)));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { accThr, 1 }, "Accent Velocity", 1, 127, 100));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { pattern, 1 }, "Pattern", 1, 16, 1));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { transpose, 1 }, "Transpose", -12, 12, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bpm, 1 }, "Internal Tempo", NormalisableRange<float> (40.0f, 300.0f, 0.1f), 130.0f,
        attrs().withLabel ("BPM")));
    return layout;
}
} // namespace acid3::params
