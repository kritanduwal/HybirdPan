#pragma once

#include <juce_dsp/juce_dsp.h>
#include "FractionalDelay.h"

//==============================================================================
/**
    Real-time port of the per-track panning law in hybridPanMix.m.

    hybridPanMix.m renders a whole multi-track mix three ways (ICLD-only,
    ICTD-only, HYBD) and LUFS-matches them for offline A/B/C listening. As a
    plugin we operate on a single source instead (one instance per DAW track,
    which is exactly how the MATLAB treats each mono track), and we emit ONE of
    the three techniques at a time, selected by the Mode parameter. The offline
    LUFS matching / peak safety is a cross-mix normalisation that has no
    real-time analogue, so it is replaced by a manual output-gain control.

    Modes
      ICLD  - constant-power (equal-power, ~3 dB) level pan. Standard DAW pan.
      ICTD  - equal level both channels, contralateral channel delayed
              (sub-sample Lagrange-3 fractional delay).
      HYBD  - hybrid. Two sub-modes:
                Broadband - ICLD + ICTD applied across the full band
                            (matches Duwal, Arias, Bulla & Chon, AES 2025).
                Freqsplit - ICTD below the crossover, ICLD above it
                            (Wightman & Kistler duplex-theory extension).

    All modes carry a constant 1-sample latency (see FractionalDelay) so that
    switching modes never changes host plugin-delay compensation.
*/
class HybridPanner
{
public:
    enum class Mode    { HYBD = 0, ICLD, ICTD };
    enum class SubMode { Freqsplit = 0, Broadband };

    HybridPanner() = default;

    //==============================================================================
    void prepare (double newSampleRate, int /*maxBlockSize*/)
    {
        sampleRate = newSampleRate;

        // Ring buffers sized for the largest ICTD we allow (absolute 5 ms
        // ceiling) plus the +1 reference offset, at this sample rate.
        const int maxDelay = (int) std::ceil (0.005 * sampleRate) + 2;
        delaySrc .prepare (maxDelay);
        delayLow .prepare (maxDelay);
        delayHigh.prepare (maxDelay);

        juce::dsp::ProcessSpec spec { sampleRate, 512u, 1u };
        lowpass .prepare (spec);
        highpass.prepare (spec);
        updateCrossover (crossoverHz, true);

        panSmoothed    .reset (sampleRate, 0.02);   // 20 ms
        gainSmoothed   .reset (sampleRate, 0.02);
        panSmoothed    .setCurrentAndTargetValue (pan);
        gainSmoothed   .setCurrentAndTargetValue (outputGainLin);

        reset();
    }

    void reset()
    {
        delaySrc.reset(); delayLow.reset(); delayHigh.reset();
        lowpass.reset();  highpass.reset();
    }

    //==============================================================================
    // Parameter setters (call once per block from processBlock).
    void setMode      (Mode m)       noexcept { mode = m; }
    void setSubMode   (SubMode s)    noexcept { subMode = s; }
    void setPan       (float p)      noexcept { pan = juce::jlimit (-1.0f, 1.0f, p);
                                                panSmoothed.setTargetValue (pan); }
    void setMaxICTDms (float ms)     noexcept { maxICTDsamples = ms * 1.0e-3f * (float) sampleRate; }
    void setOutputGainDb (float dB)  noexcept { outputGainLin = juce::Decibels::decibelsToGain (dB);
                                                gainSmoothed.setTargetValue (outputGainLin); }

    void setCrossover (float hz) noexcept
    {
        if (std::abs (hz - crossoverHz) > 1.0e-3f)
            updateCrossover (hz, false);
    }

    float getCrossover() const noexcept { return crossoverHz; }
    Mode  getMode()      const noexcept { return mode; }

    //==============================================================================
    /** Process one mono input sample into a stereo (L,R) pair. */
    inline void processSample (float src, float& outL, float& outR) noexcept
    {
        const float p = panSmoothed.getNextValue();

        // --- ICLD: constant-power pan law (identical mapping to the MATLAB) ---
        const float theta = (p + 1.0f) * (juce::MathConstants<float>::pi * 0.25f);
        const float gL = std::cos (theta);
        const float gR = std::sin (theta);

        // --- ICTD: contralateral delay, magnitude linear in |pan| ---
        const float Drel = std::abs (p) * maxICTDsamples;   // relative delay (samples)
        const bool  leftDelayed = (p > 0.0f);               // source right -> delay L
        const float invSqrt2 = 0.70710678f;

        // Crossover bands (always computed so mode switches are click-free).
        const float xLow  = lowpass .processSample (src);
        const float xHigh = highpass.processSample (src);

        delaySrc .push (src);
        delayLow .push (xLow);
        delayHigh.push (xHigh);

        // Reference reads are a plain 1-sample delay (the uniform latency);
        // delayed reads add the relative ICTD on top.
        const float srcRef = delaySrc.read (1.0f);
        const float lowRef = delayLow.read (1.0f);
        const float highRef = delayHigh.read (1.0f);

        float L = 0.0f, R = 0.0f;

        switch (mode)
        {
            case Mode::ICLD:
                L = gL * srcRef;
                R = gR * srcRef;
                break;

            case Mode::ICTD:
            {
                const float srcDel = delaySrc.read (1.0f + Drel);
                if (leftDelayed) { L = invSqrt2 * srcDel; R = invSqrt2 * srcRef; }
                else             { L = invSqrt2 * srcRef; R = invSqrt2 * srcDel; }
                break;
            }

            case Mode::HYBD:
            default:
                if (subMode == SubMode::Broadband)
                {
                    const float srcDel = delaySrc.read (1.0f + Drel);
                    if (leftDelayed) { L = gL * srcDel; R = gR * srcRef; }
                    else             { L = gL * srcRef; R = gR * srcDel; }
                }
                else // Freqsplit: ICTD in lows, ICLD in highs
                {
                    const float lowDel = delayLow.read (1.0f + Drel);
                    float lowL, lowR;
                    if (leftDelayed) { lowL = invSqrt2 * lowDel; lowR = invSqrt2 * lowRef; }
                    else             { lowL = invSqrt2 * lowRef; lowR = invSqrt2 * lowDel; }

                    const float highL = gL * highRef;   // high band uses ICLD only
                    const float highR = gR * highRef;

                    L = lowL + highL;
                    R = lowR + highR;
                }
                break;
        }

        const float g = gainSmoothed.getNextValue();
        outL = L * g;
        outR = R * g;
    }

private:
    void updateCrossover (float hz, bool force)
    {
        crossoverHz = juce::jlimit (20.0f, (float) (sampleRate * 0.45), hz);
        juce::ignoreUnused (force);
        // 2nd-order Butterworth LP/HP == MATLAB butter(2, fc/(fs/2), ...).
        // (Q defaults to 1/sqrt(2), i.e. Butterworth.)
        lowpass .coefficients =
            juce::dsp::IIR::Coefficients<float>::makeLowPass  (sampleRate, crossoverHz);
        highpass.coefficients =
            juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, crossoverHz);
    }

    double sampleRate = 44100.0;

    Mode    mode    = Mode::HYBD;
    SubMode subMode = SubMode::Freqsplit;

    float pan            = 0.0f;
    float maxICTDsamples = 0.3f * 1.0e-3f * 44100.0f;
    float crossoverHz    = 700.0f;
    float outputGainLin  = 1.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> panSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmoothed;

    juce::dsp::IIR::Filter<float> lowpass, highpass;

    FractionalDelay delaySrc, delayLow, delayHigh;
};
