#pragma once

#include "Parameters.h"
#include "seq/Engine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace acid3
{
class Acid3Processor : public juce::AudioProcessor
{
public:
    Acid3Processor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Acid3"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- GUI <-> audio ------------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

    /** Thread-safe pattern access for the editor (message thread). */
    Pattern getPattern (int index) const;
    void setPattern (int index, const Pattern& p);
    int getSelectedPatternIndex() const;

    /** Internal transport (standalone, or plugin while the host is stopped). */
    void setInternalPlaying (bool shouldPlay) { internalPlaying.store (shouldPlay); }
    bool isInternalPlaying() const { return internalPlaying.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }
    int getCurrentStep() const { return currentStep.load(); }

private:
    void syncParameters();
    juce::ValueTree patternsToTree() const;
    void patternsFromTree (const juce::ValueTree&);

    Engine engine;
    std::array<Pattern, numPatterns> patterns;
    mutable juce::SpinLock patternLock;
    std::atomic<int> patternVersion { 0 };
    int appliedPatternVersion = -1, appliedPatternIndex = -1;

    std::atomic<bool> internalPlaying { false }, hostPlaying { false };
    std::atomic<int> currentStep { -1 };
    double internalPpq = 0.0;
    bool wasInternalPlaying = false;

    std::vector<MidiEvent> midiScratch;
    int currentProgram = 0;

    struct ParamPtrs
    {
        std::atomic<float>* wave, *tune, *cutoff, *reso, *envmod, *decay, *accent, *volume,
                           *drift, *seqOn, *swing, *accThr, *pattern, *transpose, *bpm;
    } p {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Acid3Processor)
};
} // namespace acid3
