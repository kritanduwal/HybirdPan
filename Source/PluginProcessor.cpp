#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
HybridPanAudioProcessor::HybridPanAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    panParam       = apvts.getRawParameterValue ("pan");
    modeParam      = apvts.getRawParameterValue ("mode");
    subModeParam   = apvts.getRawParameterValue ("submode");
    crossoverParam = apvts.getRawParameterValue ("crossover");
    maxICTDParam   = apvts.getRawParameterValue ("maxictd");
    outGainParam   = apvts.getRawParameterValue ("outgain");
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
HybridPanAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "pan", 1 }, "Pan",
        NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                if (std::abs (v) < 0.005f) return String ("C");
                int pct = roundToInt (std::abs (v) * 100.0f);
                return String (pct) + (v < 0 ? " L" : " R");
            })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "mode", 1 }, "Mode",
        StringArray { "HYBD", "ICLD", "ICTD" }, 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "submode", 1 }, "HYBD Mode",
        StringArray { "Freqsplit", "Broadband" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "crossover", 1 }, "Crossover",
        NormalisableRange<float> (100.0f, 2000.0f, 1.0f, 0.4f), 700.0f,
        AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "maxictd", 1 }, "Max ICTD",
        NormalisableRange<float> (0.0f, 2.0f, 0.001f), 0.3f,
        AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 3) + " ms"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "outgain", 1 }, "Output",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    return layout;
}

//==============================================================================
void HybridPanAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    panner.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (1);   // constant 1-sample latency from the causal Lagrange delay
}

bool HybridPanAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Output must be stereo. Input may be mono or stereo (we sum to mono).
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
void HybridPanAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numSamples = buffer.getNumSamples();

    // Push per-block parameters into the DSP core.
    panner.setMode        (static_cast<HybridPanner::Mode>    ((int) *modeParam));
    panner.setSubMode     (static_cast<HybridPanner::SubMode> ((int) *subModeParam));
    panner.setPan         (*panParam);
    panner.setCrossover   (*crossoverParam);
    panner.setMaxICTDms   (*maxICTDParam);
    panner.setOutputGainDb (*outGainParam);

    const float* inL = buffer.getReadPointer (0);
    const float* inR = (numIn > 1) ? buffer.getReadPointer (1) : nullptr;

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);   // output is guaranteed stereo

    for (int n = 0; n < numSamples; ++n)
    {
        // Sum input to a mono source (matches the mono-track model in the MATLAB).
        const float src = (inR != nullptr) ? 0.5f * (inL[n] + inR[n]) : inL[n];

        float l, r;
        panner.processSample (src, l, r);
        outL[n] = l;
        outR[n] = r;
    }
}

//==============================================================================
juce::AudioProcessorEditor* HybridPanAudioProcessor::createEditor()
{
    return new HybridPanAudioProcessorEditor (*this);
}

void HybridPanAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HybridPanAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HybridPanAudioProcessor();
}
