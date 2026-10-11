#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
/** A compact, flat rotary look. */
class HybridLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HybridLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;
};

//==============================================================================
/** Small visual: a pan indicator plus a band-allocation strip showing which
    cue (ICTD / ICLD) is active where, given the current mode. */
class PanVisualiser : public juce::Component,
                      private juce::Timer
{
public:
    explicit PanVisualiser (HybridPanAudioProcessor& p);
    ~PanVisualiser() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    HybridPanAudioProcessor& processor;
    float lastPan = 0.0f;
    int   lastMode = -1, lastSub = -1;
    float lastXover = -1.0f;
};

//==============================================================================
class HybridPanAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit HybridPanAudioProcessorEditor (HybridPanAudioProcessor&);
    ~HybridPanAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void parameterChanged (const juce::String& id, float newValue) override;
    void refreshEnablement();

    HybridPanAudioProcessor& processor;
    HybridLookAndFeel lnf;

    juce::Slider panSlider, crossoverSlider, maxICTDSlider, outGainSlider;
    juce::ComboBox modeBox, subModeBox;
    juce::Label panLabel, crossoverLabel, maxICTDLabel, outGainLabel;
    juce::Label modeLabel, subModeLabel, titleLabel, hintLabel;

    PanVisualiser visualiser;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BoxAtt    = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAtt> panAtt, crossoverAtt, maxICTDAtt, outGainAtt;
    std::unique_ptr<BoxAtt>    modeAtt, subModeAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridPanAudioProcessorEditor)
};
