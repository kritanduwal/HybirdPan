#pragma once

#include <juce_core/juce_core.h>
#include "HybridPanMix.h"
#include "PlotComponent.h"

//==============================================================================
/**
    The non-UI half of Arias_HW4.m: load the audio files, bring them to a common
    sample rate, render the three mixes, write them as 24-bit WAVs and run the
    analysis. Safe to call from a background thread.
*/
struct RenderRequest
{
    struct Input
    {
        juce::File file;
        double pan = 0.0;
        juce::String position;   // "LEFT" / "CENTER" / "RIGHT", for the report
    };

    std::vector<Input> inputs;
    HybridPanMix::Options options;
    juce::File outputDirectory;
};

struct RenderOutcome
{
    bool ok = false;
    juce::String report;
    FigureData analysis, comparison;
};

RenderOutcome runRender (const RenderRequest&);
