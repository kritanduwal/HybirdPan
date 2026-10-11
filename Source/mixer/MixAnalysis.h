#pragma once

#include "HybridPanMix.h"
#include "PlotComponent.h"

//==============================================================================
/**
    Port of the analysis halves of Arias_HW4.m and compareOutputs.m.

    analysis   - Arias_HW4.m's four panels: L/R waveform around the loudest
                 moment, L/R magnitude spectrum, inter-channel cross-correlation
                 (the peak lag is the effective ITD) and side-signal spectrum.
    comparison - compareOutputs.m's panels: a goniometer per mix, magnitude-
                 squared coherence, inter-channel phase and side/mid ratio.
    summary    - compareOutputs.m's summary-statistics table.
*/
struct MixAnalysis
{
    struct Output
    {
        FigureData analysis, comparison;
        juce::String summary;
    };

    static Output run (const HybridPanMix::Result& mixes, double fs, int numTracks);
};
