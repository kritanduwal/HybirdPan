#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg{ 0xff1b1f24 };
    const juce::Colour kPanel{ 0xff242a31 };
    const juce::Colour kAccent{ 0xff4fb3ff };   // ICTD / blue
    const juce::Colour kAccent2{ 0xff9b7bff };   // HYBD accent
    const juce::Colour kLevel{ 0xffb0b8c0 };   // ICLD / grey
    const juce::Colour kText{ 0xffdfe6ee };
    const juce::Colour kTextDim{ 0xff8b95a1 };

    // JUCE 8 deprecates the plain Font(height) constructors in favour of
    // FontOptions. This helper stays warning-free on 8.x while still
    // compiling against 7.x.
    juce::Font makeFont(float height, bool bold = false)
    {
#if defined (JUCE_VERSION) && JUCE_VERSION >= 0x080000
        auto opts = juce::FontOptions(height);
        return juce::Font(bold ? opts.withStyle("Bold") : opts);
#else
        return juce::Font(height, bold ? juce::Font::bold : juce::Font::plain);
#endif
    }
}

//==============================================================================
HybridLookAndFeel::HybridLookAndFeel()
{
    setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    setColour(juce::Slider::rotarySliderOutlineColourId, kPanel.brighter(0.15f));
    setColour(juce::Slider::thumbColourId, kText);
    setColour(juce::ComboBox::backgroundColourId, kPanel);
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::ComboBox::outlineColourId, kPanel.brighter(0.2f));
    setColour(juce::ComboBox::arrowColourId, kAccent);
    setColour(juce::PopupMenu::backgroundColourId, kPanel);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.35f));
    setColour(juce::PopupMenu::textColourId, kText);
    setColour(juce::Label::textColourId, kText);
}

void HybridLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
    float sliderPos, float startAngle, float endAngle,
    juce::Slider& s)
{
    auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(6.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto centre = bounds.getCentre();
    auto angle = startAngle + sliderPos * (endAngle - startAngle);
    const float thickness = radius * 0.28f;

    // Track
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius - thickness * 0.5f, radius - thickness * 0.5f,
        0.0f, startAngle, endAngle, true);
    g.setColour(findColour(juce::Slider::rotarySliderOutlineColourId));
    g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    // Value arc (drawn from the 12-o'clock centre for bipolar pan feel)
    const bool bipolar = (s.getMinimum() < 0.0 && s.getMaximum() > 0.0);
    juce::Path value;
    float fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    value.addCentredArc(centre.x, centre.y, radius - thickness * 0.5f, radius - thickness * 0.5f,
        0.0f, fromAngle, angle, true);
    g.setColour(findColour(juce::Slider::rotarySliderFillColourId));
    g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    // Pointer
    juce::Path p;
    auto pl = radius - thickness;
    p.addRoundedRectangle(-1.5f, -radius + thickness * 0.4f, 3.0f, pl * 0.55f, 1.5f);
    p.applyTransform(juce::AffineTransform::rotation(angle).translated(centre));
    g.setColour(kText);
    g.fillPath(p);
}

//==============================================================================
PanVisualiser::PanVisualiser(HybridPanAudioProcessor& p) : processor(p)
{
    startTimerHz(30);
}

PanVisualiser::~PanVisualiser() { stopTimer(); }

void PanVisualiser::timerCallback()
{
    const float pan = processor.apvts.getRawParameterValue("pan")->load();
    const int   mode = (int)processor.apvts.getRawParameterValue("mode")->load();
    const int   sub = (int)processor.apvts.getRawParameterValue("submode")->load();
    const float xover = processor.apvts.getRawParameterValue("crossover")->load();

    if (std::abs(pan - lastPan) > 1.0e-4f || mode != lastMode
        || sub != lastSub || std::abs(xover - lastXover) > 1.0e-3f)
    {
        lastPan = pan; lastMode = mode; lastSub = sub; lastXover = xover;
        repaint();
    }
}

void PanVisualiser::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(kPanel);
    g.fillRoundedRectangle(b, 6.0f);

    auto strip = b.removeFromBottom(b.getHeight() * 0.42f).reduced(10.0f, 6.0f);
    auto stage = b.reduced(10.0f, 4.0f);

    // --- Pan stage (top): a dot between L and R ---
    g.setColour(kTextDim);
    g.setFont(makeFont(11.0f));
    g.drawText("L", stage.removeFromLeft(14.0f), juce::Justification::centred);
    g.drawText("R", stage.removeFromRight(14.0f), juce::Justification::centred);

    auto line = stage.withHeight(2.0f).withY(stage.getCentreY());
    g.setColour(kPanel.brighter(0.25f));
    g.fillRect(line);
    // centre tick
    g.setColour(kTextDim.withAlpha(0.5f));
    g.fillRect(juce::Rectangle<float>(stage.getCentreX() - 0.5f, stage.getY(), 1.0f, stage.getHeight()));

    const float px = juce::jmap(lastPan, -1.0f, 1.0f, stage.getX(), stage.getRight());
    const auto dotColour = (lastMode == 2) ? kAccent : (lastMode == 1) ? kLevel : kAccent2;
    g.setColour(dotColour);
    g.fillEllipse(px - 6.0f, stage.getCentreY() - 6.0f, 12.0f, 12.0f);

    // --- Band-allocation strip (bottom): which cue is active per frequency ---
    // ICLD-only -> level across whole band. ICTD-only -> time across whole band.
    // HYBD broadband -> both across whole band. HYBD freqsplit -> time below
    // xover, level above.
    const float fLo = 20.0f, fHi = 20000.0f;
    auto freqToX = [&](float f)
        {
            float t = std::log(f / fLo) / std::log(fHi / fLo);
            return juce::jmap(t, 0.0f, 1.0f, strip.getX(), strip.getRight());
        };

    auto drawBand = [&](juce::Rectangle<float> r, juce::Colour c, const juce::String& txt)
        {
            g.setColour(c.withAlpha(0.30f));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(c);
            g.drawRoundedRectangle(r, 3.0f, 1.0f);
            g.setColour(kText);
            g.setFont(makeFont(10.0f, true));
            g.drawText(txt, r, juce::Justification::centred);
        };

    if (lastMode == 1) // ICLD
        drawBand(strip, kLevel, "ICLD (level) - full band");
    else if (lastMode == 2) // ICTD
        drawBand(strip, kAccent, "ICTD (time) - full band");
    else // HYBD
    {
        if (lastSub == 1) // broadband
            drawBand(strip, kAccent2, "ICTD + ICLD - full band");
        else // freqsplit
        {
            float xx = freqToX(lastXover);
            auto lo = strip.withRight(xx).reduced(0.0f, 0.0f);
            auto hi = strip.withLeft(xx);
            drawBand(lo.reduced(0.0f, 0.0f), kAccent, "ICTD");
            drawBand(hi, kLevel, "ICLD");
            g.setColour(kText);
            g.setFont(makeFont(9.0f));
            g.drawText(juce::String(juce::roundToInt(lastXover)) + " Hz",
                juce::Rectangle<float>(xx - 24.0f, strip.getY() - 12.0f, 48.0f, 11.0f),
                juce::Justification::centred);
        }
    }
}

//==============================================================================
static void styleRotary(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 18);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxTextColourId, kText);
}

HybridPanAudioProcessorEditor::HybridPanAudioProcessorEditor(HybridPanAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), visualiser(p)
{
    setLookAndFeel(&lnf);

    auto setupLabel = [this](juce::Label& l, const juce::String& t)
        {
            l.setText(t, juce::dontSendNotification);
            l.setJustificationType(juce::Justification::centred);
            l.setColour(juce::Label::textColourId, kTextDim);
            l.setFont(makeFont(12.0f));
            addAndMakeVisible(l);
        };

    styleRotary(panSlider);        addAndMakeVisible(panSlider);
    styleRotary(crossoverSlider);  addAndMakeVisible(crossoverSlider);
    styleRotary(maxICTDSlider);    addAndMakeVisible(maxICTDSlider);
    styleRotary(outGainSlider);    addAndMakeVisible(outGainSlider);

    modeBox.addItemList({ "HYBD", "ICLD", "ICTD" }, 1);
    subModeBox.addItemList({ "Freqsplit", "Broadband" }, 1);
    addAndMakeVisible(modeBox);
    addAndMakeVisible(subModeBox);

    setupLabel(panLabel, "PAN");
    setupLabel(crossoverLabel, "CROSSOVER");
    setupLabel(maxICTDLabel, "MAX ICTD");
    setupLabel(outGainLabel, "OUTPUT");
    setupLabel(modeLabel, "MODE");
    setupLabel(subModeLabel, "HYBD SUB-MODE");

    titleLabel.setText("HYBRID PAN", juce::dontSendNotification);
    titleLabel.setFont(makeFont(20.0f, true));
    titleLabel.setColour(juce::Label::textColourId, kText);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    hintLabel.setText("ICLD level  -  ICTD time  -  HYBD both", juce::dontSendNotification);
    hintLabel.setFont(makeFont(11.0f));
    hintLabel.setColour(juce::Label::textColourId, kTextDim);
    hintLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(hintLabel);

    addAndMakeVisible(visualiser);

    panAtt = std::make_unique<SliderAtt>(processor.apvts, "pan", panSlider);
    crossoverAtt = std::make_unique<SliderAtt>(processor.apvts, "crossover", crossoverSlider);
    maxICTDAtt = std::make_unique<SliderAtt>(processor.apvts, "maxictd", maxICTDSlider);
    outGainAtt = std::make_unique<SliderAtt>(processor.apvts, "outgain", outGainSlider);
    modeAtt = std::make_unique<BoxAtt>(processor.apvts, "mode", modeBox);
    subModeAtt = std::make_unique<BoxAtt>(processor.apvts, "submode", subModeBox);

    processor.apvts.addParameterListener("mode", this);
    processor.apvts.addParameterListener("submode", this);

    refreshEnablement();
    setSize(520, 420);
}

HybridPanAudioProcessorEditor::~HybridPanAudioProcessorEditor()
{
    processor.apvts.removeParameterListener("mode", this);
    processor.apvts.removeParameterListener("submode", this);
    setLookAndFeel(nullptr);
}

void HybridPanAudioProcessorEditor::parameterChanged(const juce::String&, float)
{
    // Called on the message thread's parameter dispatch; keep it light.
    juce::MessageManager::callAsync([this] { refreshEnablement(); });
}

void HybridPanAudioProcessorEditor::refreshEnablement()
{
    const int mode = (int)processor.apvts.getRawParameterValue("mode")->load();
    const bool isHybd = (mode == 0);
    const int sub = (int)processor.apvts.getRawParameterValue("submode")->load();
    const bool freqsplit = isHybd && (sub == 0);

    subModeBox.setEnabled(isHybd);
    subModeLabel.setEnabled(isHybd);
    // Crossover only matters for HYBD freqsplit.
    crossoverSlider.setEnabled(freqsplit);
    crossoverLabel.setEnabled(freqsplit);
    // Max ICTD is irrelevant for pure ICLD.
    const bool usesTime = (mode != 1);
    maxICTDSlider.setEnabled(usesTime);
    maxICTDLabel.setEnabled(usesTime);
}

//==============================================================================
void HybridPanAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    auto header = getLocalBounds().removeFromTop(48).toFloat();
    g.setColour(kPanel);
    g.fillRect(header);
    g.setColour(kAccent);
    g.fillRect(header.removeFromBottom(2.0f));
}

void HybridPanAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop(48).reduced(16, 0);
    titleLabel.setBounds(header.removeFromLeft(200));
    hintLabel.setBounds(header);

    r.reduce(16, 12);

    // Visualiser strip
    visualiser.setBounds(r.removeFromTop(90));
    r.removeFromTop(12);

    // Mode row (two combo boxes)
    auto modeRow = r.removeFromTop(58);
    auto mCol = modeRow.removeFromLeft(modeRow.getWidth() / 2).reduced(6, 0);
    modeLabel.setBounds(mCol.removeFromTop(16));
    modeBox.setBounds(mCol.removeFromTop(28));
    auto sCol = modeRow.reduced(6, 0);
    subModeLabel.setBounds(sCol.removeFromTop(16));
    subModeBox.setBounds(sCol.removeFromTop(28));

    r.removeFromTop(10);

    // Knob row (4 rotaries)
    auto knobRow = r.removeFromTop(150);
    auto kw = knobRow.getWidth() / 4;

    auto placeKnob = [](juce::Rectangle<int> area, juce::Label& lbl, juce::Slider& sl)
        {
            lbl.setBounds(area.removeFromTop(16));
            sl.setBounds(area.reduced(4));
        };

    placeKnob(knobRow.removeFromLeft(kw), panLabel, panSlider);
    placeKnob(knobRow.removeFromLeft(kw), maxICTDLabel, maxICTDSlider);
    placeKnob(knobRow.removeFromLeft(kw), crossoverLabel, crossoverSlider);
    placeKnob(knobRow.removeFromLeft(kw), outGainLabel, outGainSlider);
}