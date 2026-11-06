#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace acid3
{
class Acid3LookAndFeel : public juce::LookAndFeel_V4
{
public:
    Acid3LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
};

/** 16-step grid: 13 note rows (C..C), then octave, accent, slide and gate rows. */
class StepGrid : public juce::Component
{
public:
    std::function<void (const Pattern&)> onEdit;

    void setPattern (const Pattern& p) { pattern = p; repaint(); }
    const Pattern& getPattern() const { return pattern; }
    void setPlayhead (int step) { if (step != playhead) { playhead = step; repaint(); } }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    static constexpr int noteRows = 13, extraRows = 4;

private:
    juce::Rectangle<float> cell (int col, int row) const;
    bool hit (juce::Point<float>, int& col, int& row) const;
    void edit (int col, int row, bool fromDrag);

    Pattern pattern;
    int playhead = -1;
    int lastDragCol = -1;
    float labelWidth = 54.0f;
};

class Acid3Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Acid3Editor (Acid3Processor&);
    ~Acid3Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseWidth = 960, baseHeight = 600;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    class Content : public juce::Component
    {
    public:
        explicit Content (Acid3Editor& e) : editor (e) {}
        void paint (juce::Graphics& g) override { editor.paintContent (g); }
    private:
        Acid3Editor& editor;
    };

    void timerCallback() override;
    void paintContent (juce::Graphics&);
    void layoutContent();
    void addKnob (Knob&, const char* paramId, const juce::String& text, bool small = false);
    void editPattern (std::function<void (Pattern&)> fn);
    void loadSelectedPattern();

    Acid3Processor& acid;
    Acid3LookAndFeel lnf;
    Content content { *this };

    Knob tune, cutoff, reso, envmod, decay, accent, volume, swing;
    juce::TextButton waveButton { "SAW" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> waveAttachment;

    juce::TextButton playButton { "PLAY" }, seqButton { "SEQ" }, driftButton { "DRIFT" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> seqAttachment, driftAttachment;
    juce::Slider bpmSlider, transposeSlider;
    juce::Label bpmLabel, transposeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAttachment, transposeAttachment;
    juce::ComboBox patternBox, presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> patternAttachment;
    juce::TextButton randomButton { "RANDOM" }, clearButton { "CLEAR" }, shiftLeft { "<" }, shiftRight { ">" };

    StepGrid grid;
    int shownPattern = -1;
    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Acid3Editor)
};
} // namespace acid3
