#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/HybridPanner.h"

//==============================================================================
/**
    HybridPan - a single-source stereo panner implementing the ICLD / ICTD /
    HYBD panning laws from hybridPanMix.m (Duwal, Arias, Bulla & Chon, AES 2025,
    plus the Wightman & Kistler freqsplit extension).

    Drop one instance per track: it sums the input to mono and pans it to a
    position in [-1, +1] using the selected technique.
*/
class HybridPanAudioProcessor : public juce::AudioProcessor
{
public:
    HybridPanAudioProcessor();
    ~HybridPanAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.005; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    HybridPanner panner;

    // Cached raw parameter pointers.
    std::atomic<float>* panParam        = nullptr;
    std::atomic<float>* modeParam        = nullptr;
    std::atomic<float>* subModeParam     = nullptr;
    std::atomic<float>* crossoverParam   = nullptr;
    std::atomic<float>* maxICTDParam      = nullptr;
    std::atomic<float>* outGainParam      = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridPanAudioProcessor)
};
