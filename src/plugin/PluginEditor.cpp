#include "PluginEditor.h"
#include "Presets.h"

namespace acid3
{
using namespace juce;

namespace colours
{
    const Colour panel     { 0xffc9ccd0 };   // brushed silver
    const Colour panelDark { 0xffa9adb3 };
    const Colour ink       { 0xff1d1f22 };
    const Colour knob      { 0xff26282b };
    const Colour accentRed { 0xffd8432f };
    const Colour lcd       { 0xff20262b };
    const Colour lcdOn     { 0xffffa12b };
    const Colour slideBlue { 0xff3f8fd6 };
}

// ======================================================================================
Acid3LookAndFeel::Acid3LookAndFeel()
{
    setColour (Label::textColourId, colours::ink);
    setColour (Slider::textBoxTextColourId, colours::ink);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (TextButton::buttonColourId, colours::panelDark);
    setColour (TextButton::buttonOnColourId, colours::accentRed);
    setColour (TextButton::textColourOffId, colours::ink);
    setColour (TextButton::textColourOnId, Colours::white);
    setColour (ComboBox::backgroundColourId, colours::lcd);
    setColour (ComboBox::textColourId, colours::lcdOn);
    setColour (ComboBox::outlineColourId, colours::ink);
    setColour (ComboBox::arrowColourId, colours::lcdOn);
    setColour (PopupMenu::backgroundColourId, colours::lcd);
    setColour (PopupMenu::textColourId, colours::lcdOn);
}

void Acid3LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider&)
{
    const auto bounds = Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);

    // scale ticks
    g.setColour (colours::ink.withAlpha (0.7f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        const auto p1 = c.getPointOnCircumference (radius, a), p2 = c.getPointOnCircumference (radius * 0.86f, a);
        g.drawLine ({ p1, p2 }, i % 5 == 0 ? 1.6f : 0.9f);
    }
    // knob body
    const float r = radius * 0.74f;
    g.setGradientFill (ColourGradient (colours::knob.brighter (0.35f), c.x - r, c.y - r, colours::knob.darker (0.4f), c.x + r, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.2f);
    // pointer
    g.setColour (Colours::white);
    g.drawLine ({ c.getPointOnCircumference (r * 0.25f, angle), c.getPointOnCircumference (r * 0.92f, angle) }, 2.4f);
}

void Acid3LookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    auto col = b.getToggleState() ? findColour (TextButton::buttonOnColourId) : base;
    if (down) col = col.darker (0.2f);
    else if (over) col = col.brighter (0.1f);
    g.setColour (col);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (colours::ink.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

// ======================================================================================
namespace
{
    const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C'" };
    const char* extraNames[] = { "OCT", "ACC", "SLIDE", "GATE" };
}

Rectangle<float> StepGrid::cell (int col, int row) const
{
    const auto area = getLocalBounds().toFloat().withTrimmedLeft (labelWidth);
    const float cw = area.getWidth() / Pattern::maxSteps;
    const float noteH = (area.getHeight() * 0.68f) / noteRows;
    const float extraH = (area.getHeight() * 0.32f) / extraRows;
    const float yy = row < noteRows ? row * noteH : noteRows * noteH + (row - noteRows) * extraH;
    return { area.getX() + col * cw, yy, cw, row < noteRows ? noteH : extraH };
}

bool StepGrid::hit (Point<float> pt, int& col, int& row) const
{
    for (row = 0; row < noteRows + extraRows; ++row)
        for (col = 0; col < Pattern::maxSteps; ++col)
            if (cell (col, row).contains (pt))
                return true;
    return false;
}

void StepGrid::paint (Graphics& g)
{
    g.setColour (colours::lcd);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    g.setFont (FontOptions (11.0f));
    for (int row = 0; row < noteRows + extraRows; ++row)
    {
        const auto c0 = cell (0, row);
        g.setColour (colours::lcdOn.withAlpha (0.7f));
        g.drawText (row < noteRows ? noteNames[noteRows - 1 - row] : extraNames[row - noteRows],
                    Rectangle<float> (4.0f, c0.getY(), labelWidth - 8.0f, c0.getHeight()), Justification::centredRight);
    }

    for (int col = 0; col < Pattern::maxSteps; ++col)
    {
        const bool active = col < pattern.length;
        const auto& s = pattern.steps[(size_t) col];
        for (int row = 0; row < noteRows + extraRows; ++row)
        {
            auto r = cell (col, row).reduced (1.0f);
            const int semis = noteRows - 1 - row;
            const bool black = row < noteRows && String (noteNames[semis]).contains ("#");
            g.setColour (Colour (black ? 0xff181c20 : 0xff2a3137).withAlpha (active ? 1.0f : 0.35f));
            g.fillRect (r);

            if (! active) continue;
            if (row < noteRows && s.gate != Gate::Rest && s.note == semis)
            {
                g.setColour (s.gate == Gate::Tie ? colours::lcdOn.withAlpha (0.45f) : colours::lcdOn);
                g.fillRoundedRectangle (r.reduced (2.0f), 2.0f);
            }
            else if (row >= noteRows)
            {
                String text;
                Colour tint = colours::lcdOn;
                switch (row - noteRows)
                {
                    case 0: text = s.octave < 0 ? "DN" : s.octave > 0 ? "UP" : "-"; break;
                    case 1: text = s.accent ? "A" : ""; tint = colours::accentRed; break;
                    case 2: text = s.slide ? "S" : ""; tint = colours::slideBlue; break;
                    case 3: text = s.gate == Gate::Note ? "N" : s.gate == Gate::Tie ? "T" : "R"; break;
                }
                if ((row - noteRows == 1 && s.accent) || (row - noteRows == 2 && s.slide))
                {
                    g.setColour (tint);
                    g.fillRoundedRectangle (r.reduced (2.0f), 2.0f);
                    g.setColour (Colours::white);
                }
                else
                    g.setColour (colours::lcdOn.withAlpha (0.8f));
                g.drawText (text, r, Justification::centred);
            }
        }
        if (col == playhead)
        {
            g.setColour (Colours::white.withAlpha (0.18f));
            g.fillRect (cell (col, 0).getUnion (cell (col, noteRows + extraRows - 1)));
        }
    }
    // step numbers on beat boundaries
    g.setColour (colours::lcdOn.withAlpha (0.25f));
    for (int col = 4; col < Pattern::maxSteps; col += 4)
    {
        const float x = cell (col, 0).getX();
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
    }
}

void StepGrid::edit (int col, int row, bool fromDrag)
{
    if (col >= pattern.length) return;
    auto& s = pattern.steps[(size_t) col];
    if (row < noteRows)
    {
        const int semis = noteRows - 1 - row;
        if (! fromDrag && s.gate != Gate::Rest && s.note == semis)
            s.gate = Gate::Rest; // click on the lit note toggles it off
        else
        {
            s.note = semis;
            if (s.gate == Gate::Rest) s.gate = Gate::Note;
        }
    }
    else if (fromDrag)
        return;
    else
        switch (row - noteRows)
        {
            case 0: s.octave = s.octave == 0 ? 1 : s.octave == 1 ? -1 : 0; break;
            case 1: s.accent = ! s.accent; break;
            case 2: s.slide = ! s.slide; break;
            case 3: s.gate = s.gate == Gate::Note ? Gate::Tie : s.gate == Gate::Tie ? Gate::Rest : Gate::Note; break;
        }
    repaint();
    if (onEdit) onEdit (pattern);
}

void StepGrid::mouseDown (const MouseEvent& e)
{
    int col, row;
    lastDragCol = -1;
    if (hit (e.position, col, row))
    {
        edit (col, row, false);
        lastDragCol = col;
    }
}

void StepGrid::mouseDrag (const MouseEvent& e)
{
    int col, row;
    if (hit (e.position, col, row) && row < noteRows && col != lastDragCol)
    {
        edit (col, row, true);
        lastDragCol = col;
    }
}

// ======================================================================================
Acid3Editor::Acid3Editor (Acid3Processor& p) : AudioProcessorEditor (p), acid (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    addKnob (tune, params::tune, "TUNING");
    addKnob (cutoff, params::cutoff, "CUT OFF FREQ");
    addKnob (reso, params::reso, "RESONANCE");
    addKnob (envmod, params::envmod, "ENV MOD");
    addKnob (decay, params::decay, "DECAY");
    addKnob (accent, params::accent, "ACCENT");
    addKnob (volume, params::volume, "VOLUME");
    addKnob (swing, params::swing, "SWING", true);

    waveButton.setClickingTogglesState (true);
    waveAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, params::wave, waveButton);
    waveButton.onStateChange = [this] { waveButton.setButtonText (waveButton.getToggleState() ? "SQUARE" : "SAW"); };
    waveButton.setButtonText (waveButton.getToggleState() ? "SQUARE" : "SAW");
    content.addAndMakeVisible (waveButton);

    for (auto* b : { &seqButton, &driftButton })
    {
        b->setClickingTogglesState (true);
        content.addAndMakeVisible (*b);
    }
    seqAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, params::seqOn, seqButton);
    driftAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, params::drift, driftButton);

    playButton.setClickingTogglesState (true);
    playButton.setToggleState (p.isInternalPlaying(), dontSendNotification);
    playButton.onClick = [this] { acid.setInternalPlaying (playButton.getToggleState()); };
    content.addAndMakeVisible (playButton);

    for (auto* s : { &bpmSlider, &transposeSlider })
    {
        s->setSliderStyle (Slider::IncDecButtons);
        s->setTextBoxStyle (Slider::TextBoxLeft, false, 52, 22);
        s->setColour (Slider::textBoxBackgroundColourId, colours::lcd);
        s->setColour (Slider::textBoxTextColourId, colours::lcdOn);
        content.addAndMakeVisible (*s);
    }
    bpmAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, params::bpm, bpmSlider);
    transposeAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, params::transpose, transposeSlider);
    bpmSlider.setNumDecimalPlacesToDisplay (1);
    bpmLabel.setText ("TEMPO", dontSendNotification);
    transposeLabel.setText ("TRANSPOSE", dontSendNotification);
    for (auto* l : { &bpmLabel, &transposeLabel })
    {
        l->setFont (FontOptions (11.0f, Font::bold));
        l->setJustificationType (Justification::centred);
        content.addAndMakeVisible (*l);
    }

    for (int i = 0; i < numPatterns; ++i)
        patternBox.addItem ("PATTERN " + String (i + 1) + (i < (int) presets::patternNames().size() ? String (" - ") + presets::patternNames()[(size_t) i] : String()), i + 1);
    patternAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, params::pattern, patternBox);
    content.addAndMakeVisible (patternBox);

    for (int i = 0; i < p.getNumPrograms(); ++i)
        presetBox.addItem (p.getProgramName (i), i + 1);
    presetBox.setTextWhenNothingSelected ("SOUND PRESET");
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) acid.setCurrentProgram (presetBox.getSelectedId() - 1); };
    content.addAndMakeVisible (presetBox);

    randomButton.onClick = [this]
    {
        editPattern ([this] (Pattern& pat)
        {
            static const int scale[] = { 0, 0, 0, 3, 5, 7, 10, 12 };
            for (int i = 0; i < pat.length; ++i)
            {
                auto& st = pat.steps[(size_t) i];
                st.note = scale[random.nextInt (8)];
                const int r = random.nextInt (10);
                st.octave = r < 2 ? -1 : r > 8 ? 1 : 0;
                st.accent = random.nextInt (4) == 0;
                st.slide = random.nextInt (4) == 0;
                const int gr = random.nextInt (10);
                st.gate = gr < 7 ? Gate::Note : gr < 8 ? Gate::Tie : Gate::Rest;
            }
        });
    };
    clearButton.onClick = [this] { editPattern ([] (Pattern& pat) { for (auto& s : pat.steps) s = Step { 0, 0, false, false, Gate::Rest }; }); };
    shiftLeft.onClick = [this] { editPattern ([] (Pattern& pat) { std::rotate (pat.steps.begin(), pat.steps.begin() + 1, pat.steps.begin() + pat.length); }); };
    shiftRight.onClick = [this] { editPattern ([] (Pattern& pat) { std::rotate (pat.steps.begin(), pat.steps.begin() + pat.length - 1, pat.steps.begin() + pat.length); }); };
    for (auto* b : { &randomButton, &clearButton, &shiftLeft, &shiftRight })
        content.addAndMakeVisible (*b);

    grid.onEdit = [this] (const Pattern& pat) { acid.setPattern (acid.getSelectedPatternIndex(), pat); };
    content.addAndMakeVisible (grid);
    loadSelectedPattern();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    content.setBounds (0, 0, baseWidth, baseHeight);
    layoutContent();
    setSize (baseWidth, baseHeight);
    startTimerHz (30);
}

Acid3Editor::~Acid3Editor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Acid3Editor::addKnob (Knob& k, const char* paramId, const String& text, bool small)
{
    k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 16);
    k.slider.setPopupDisplayEnabled (false, false, nullptr);
    k.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (acid.apvts, paramId, k.slider);
    k.label.setText (text, dontSendNotification);
    k.label.setFont (FontOptions (small ? 11.0f : 12.0f, Font::bold));
    k.label.setJustificationType (Justification::centred);
    content.addAndMakeVisible (k.slider);
    content.addAndMakeVisible (k.label);
}

void Acid3Editor::editPattern (std::function<void (Pattern&)> fn)
{
    auto pat = grid.getPattern();
    fn (pat);
    grid.setPattern (pat);
    acid.setPattern (acid.getSelectedPatternIndex(), pat);
}

void Acid3Editor::loadSelectedPattern()
{
    shownPattern = acid.getSelectedPatternIndex();
    grid.setPattern (acid.getPattern (shownPattern));
}

void Acid3Editor::timerCallback()
{
    if (acid.getSelectedPatternIndex() != shownPattern)
        loadSelectedPattern();
    grid.setPlayhead (acid.getCurrentStep());
    if (playButton.getToggleState() != acid.isInternalPlaying())
        playButton.setToggleState (acid.isInternalPlaying(), dontSendNotification);
}

void Acid3Editor::paint (Graphics& g) { g.fillAll (colours::panel); }

void Acid3Editor::paintContent (Graphics& g)
{
    auto r = content.getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (colours::panel.brighter (0.15f), 0, 0, colours::panel.darker (0.08f), 0, r.getHeight(), false));
    g.fillAll();
    // top strip
    g.setColour (colours::ink);
    g.fillRect (0.0f, 0.0f, r.getWidth(), 34.0f);
    g.setColour (Colours::white);
    g.setFont (FontOptions (20.0f, Font::bold));
    g.drawText ("ACID3", 16, 0, 200, 34, Justification::centredLeft);
    g.setFont (FontOptions (11.0f));
    g.setColour (Colours::white.withAlpha (0.6f));
    g.drawText ("BASS LINE SYNTHESIZER", 100, 0, 300, 34, Justification::centredLeft);
    // section frames
    g.setColour (colours::ink.withAlpha (0.35f));
    g.drawRoundedRectangle (10.0f, 44.0f, r.getWidth() - 20.0f, 176.0f, 6.0f, 1.0f);
    g.setColour (colours::accentRed);
    g.fillRect (10.0f, 226.0f, r.getWidth() - 20.0f, 2.0f);
}

void Acid3Editor::layoutContent()
{
    // knobs in the original's left-to-right order, then waveform + volume
    const int top = 52, knobW = 104, knobH = 132;
    int x = 18;
    for (auto* k : { &tune, &cutoff, &reso, &envmod, &decay, &accent })
    {
        k->label.setBounds (x, top, knobW, 18);
        k->slider.setBounds (x, top + 18, knobW, knobH);
        x += knobW + 6;
    }
    waveButton.setBounds (x + 6, top + 50, 92, 32);
    x += 108;
    volume.label.setBounds (x, top, knobW, 18);
    volume.slider.setBounds (x, top + 18, knobW, knobH);
    x += knobW;
    swing.label.setBounds (x, top, 70, 18);
    swing.slider.setBounds (x, top + 18, 70, 90);
    driftButton.setBounds (x + 4, top + 118, 62, 24);

    // transport / pattern row
    int y = 236;
    playButton.setBounds (14, y, 72, 28);
    seqButton.setBounds (92, y, 56, 28);
    bpmLabel.setBounds (152, y - 2, 60, 14);
    bpmSlider.setBounds (152, y + 10, 120, 22);
    transposeLabel.setBounds (280, y - 2, 104, 14);
    transposeSlider.setBounds (280, y + 10, 104, 22);
    patternBox.setBounds (392, y, 220, 28);
    presetBox.setBounds (618, y, 130, 28);
    shiftLeft.setBounds (754, y, 28, 28);
    shiftRight.setBounds (784, y, 28, 28);
    randomButton.setBounds (818, y, 70, 28);
    clearButton.setBounds (892, y, 56, 28);

    grid.setBounds (12, 274, baseWidth - 24, baseHeight - 284);
}

void Acid3Editor::resized()
{
    const float scale = (float) getWidth() / (float) baseWidth;
    content.setTransform (AffineTransform::scale (scale));
}
} // namespace acid3
