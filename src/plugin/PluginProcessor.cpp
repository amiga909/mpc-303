#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace acid3
{
using namespace juce;

Acid3Processor::Acid3Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Acid3", params::createLayout()),
      patterns (presets::initialPatterns())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    p = { get (params::wave), get (params::tune), get (params::cutoff), get (params::reso), get (params::envmod),
          get (params::decay), get (params::accent), get (params::volume), get (params::drift), get (params::seqOn),
          get (params::swing), get (params::accThr), get (params::pattern), get (params::transpose), get (params::bpm) };
    midiScratch.reserve (2048);
}

bool Acid3Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
}

void Acid3Processor::prepareToPlay (double sampleRate, int)
{
    syncParameters();
    engine.prepare (sampleRate);
    appliedPatternVersion = appliedPatternIndex = -1;
    internalPpq = 0.0;
    setLatencySamples (engine.getLatencySamples());
}

void Acid3Processor::syncParameters()
{
    VoiceParams v;
    v.square = p.wave->load() > 0.5f;
    v.tune = p.tune->load();
    v.cutoff = p.cutoff->load();
    v.reso = p.reso->load();
    v.envMod = p.envmod->load();
    v.decay = p.decay->load();
    v.accent = p.accent->load();
    v.volumeDb = p.volume->load() <= -59.9f ? -200.0 : (double) p.volume->load();
    v.drift = p.drift->load() > 0.5f;
    engine.setParams (v);
    engine.setSequencerEnabled (p.seqOn->load() > 0.5f);
    engine.setAccentVelocityThreshold ((int) p.accThr->load());
    auto& seq = engine.getSequencer();
    seq.setSwing (p.swing->load());
    seq.setTranspose ((int) p.transpose->load());

    // pick up pattern edits / selection without ever blocking the audio thread
    const int index = jlimit (0, numPatterns - 1, (int) p.pattern->load() - 1);
    const int version = patternVersion.load();
    if (index != appliedPatternIndex || version != appliedPatternVersion)
    {
        const SpinLock::ScopedTryLockType lock (patternLock);
        if (lock.isLocked())
        {
            seq.setPattern (patterns[(size_t) index]);
            appliedPatternIndex = index;
            appliedPatternVersion = version;
        }
    }
}

void Acid3Processor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    syncParameters();

    const int numSamples = buffer.getNumSamples();

    // ---- transport: host sync, or the internal clock when the host is stopped ----------
    Transport t;
    bool hostIsPlaying = false;
    double hostBpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostIsPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm()) hostBpm = *b;
            if (hostIsPlaying)
            {
                t.playing = true;
                t.bpm = hostBpm > 0.0 ? hostBpm : 120.0;
                t.ppq = pos->getPpqPosition().orFallback (0.0);
            }
        }
    hostPlaying.store (hostIsPlaying);

    const bool internal = ! hostIsPlaying && internalPlaying.load();
    if (internal)
    {
        if (! wasInternalPlaying) internalPpq = 0.0;
        t.playing = true;
        t.bpm = p.bpm->load();
        t.ppq = internalPpq;
        internalPpq += numSamples * t.bpm / (60.0 * getSampleRate());
    }
    wasInternalPlaying = internal;

    // ---- MIDI -> engine events (CCs go straight to parameters) --------------------------
    midiScratch.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int off = jlimit (0, numSamples - 1, meta.samplePosition);
        if (m.isNoteOn())
            midiScratch.push_back ({ off, MidiEvent::Type::NoteOn, m.getNoteNumber(), m.getVelocity(), 0.0 });
        else if (m.isNoteOff())
            midiScratch.push_back ({ off, MidiEvent::Type::NoteOff, m.getNoteNumber(), 0, 0.0 });
        else if (m.isPitchWheel())
            midiScratch.push_back ({ off, MidiEvent::Type::PitchBend, 0, 0, (m.getPitchWheelValue() - 8192) / 8192.0 * 2.0 });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            midiScratch.push_back ({ off, MidiEvent::Type::AllNotesOff, 0, 0, 0.0 });
        else if (m.isController())
        {
            const char* id = nullptr;
            switch (m.getControllerNumber())
            {
                case 74: id = params::cutoff; break;
                case 71: id = params::reso; break;
                case 75: id = params::decay; break;
                case 76: id = params::envmod; break;
                case 77: id = params::accent; break;
                case 7:  id = params::volume; break;
                default: break;
            }
            if (id != nullptr)
                if (auto* param = apvts.getParameter (id))
                    param->setValueNotifyingHost (m.getControllerValue() / 127.0f);
        }
        if (midiScratch.size() >= midiScratch.capacity())
            break;
    }

    // ---- render ----------------------------------------------------------------------------
    auto* left = buffer.getWritePointer (0);
    engine.process (left, numSamples, t, midiScratch.data(), (int) midiScratch.size());
    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, left, numSamples);

    currentStep.store (t.playing && p.seqOn->load() > 0.5f ? engine.getSequencer().getCurrentStep() : -1);
}

// ---- patterns ----------------------------------------------------------------------------
Pattern Acid3Processor::getPattern (int index) const
{
    const SpinLock::ScopedLockType lock (patternLock);
    return patterns[(size_t) jlimit (0, numPatterns - 1, index)];
}

void Acid3Processor::setPattern (int index, const Pattern& pat)
{
    {
        const SpinLock::ScopedLockType lock (patternLock);
        patterns[(size_t) jlimit (0, numPatterns - 1, index)] = pat;
    }
    ++patternVersion;
}

int Acid3Processor::getSelectedPatternIndex() const
{
    return jlimit (0, numPatterns - 1, (int) p.pattern->load() - 1);
}

// ---- programs ----------------------------------------------------------------------------
int Acid3Processor::getNumPrograms() { return (int) presets::sounds().size(); }

const String Acid3Processor::getProgramName (int index)
{
    return isPositiveAndBelow (index, getNumPrograms()) ? String (presets::sounds()[(size_t) index].name) : String();
}

void Acid3Processor::setCurrentProgram (int index)
{
    if (! isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    const auto& s = presets::sounds()[(size_t) index];
    auto set = [this] (const char* id, float v) { if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
    set (params::cutoff, s.cutoff);
    set (params::reso, s.reso);
    set (params::envmod, s.envmod);
    set (params::decay, s.decay);
    set (params::accent, s.accent);
    set (params::wave, s.square ? 1.0f : 0.0f);
}

// ---- state -------------------------------------------------------------------------------
static constexpr int stateVersion = 1;

ValueTree Acid3Processor::patternsToTree() const
{
    ValueTree tree ("Patterns");
    const SpinLock::ScopedLockType lock (patternLock);
    for (int i = 0; i < numPatterns; ++i)
    {
        ValueTree pt ("Pattern");
        pt.setProperty ("index", i, nullptr);
        pt.setProperty ("steps", String (patterns[(size_t) i].toString()), nullptr);
        tree.appendChild (pt, nullptr);
    }
    return tree;
}

void Acid3Processor::patternsFromTree (const ValueTree& tree)
{
    for (const auto& pt : tree)
    {
        const int index = pt.getProperty ("index", -1);
        Pattern pat;
        if (isPositiveAndBelow (index, numPatterns) && Pattern::fromString (pt.getProperty ("steps").toString().toStdString(), pat))
            setPattern (index, pat);
    }
}

void Acid3Processor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("version", stateVersion, nullptr);
    state.removeChild (state.getChildWithName ("Patterns"), nullptr);
    state.appendChild (patternsToTree(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Acid3Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType())) return;
        patternsFromTree (state.getChildWithName ("Patterns"));
        state.removeChild (state.getChildWithName ("Patterns"), nullptr);
        apvts.replaceState (state);
    }
}

AudioProcessorEditor* Acid3Processor::createEditor() { return new Acid3Editor (*this); }
} // namespace acid3

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new acid3::Acid3Processor(); }
