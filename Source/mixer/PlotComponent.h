#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Plain data describing one set of axes, filled in by MixAnalysis.

struct PlotSeries
{
    enum class Style { line, dashed, dots };

    juce::String name;
    juce::Colour colour;
    std::vector<float> x, y;
    Style style = Style::line;
    float thickness = 1.3f;
};

struct PlotMarker
{
    float x = 0.0f, y = 0.0f;
    juce::Colour colour;
    juce::String text;
    bool square = false;
};

struct PlotVLine
{
    float x = 0.0f;
    juce::String label;
};

struct PlotData
{
    juce::String title, xLabel, yLabel;
    bool logX = false;
    bool square = false;                 // keep the axes square (goniometer)
    juce::Range<float> xRange, yRange;   // empty = fit to the data
    std::vector<PlotSeries> series;
    std::vector<PlotMarker> markers;
    std::vector<PlotVLine>  vLines;
};

/** A figure is a titled grid of plots; cells may span several columns. */
struct FigureData
{
    struct Cell
    {
        PlotData plot;
        int column = 0, row = 0, columnSpan = 1;
    };

    juce::String title;
    int columns = 1, rows = 1;
    std::vector<Cell> cells;
};

//==============================================================================
class PlotComponent : public juce::Component
{
public:
    explicit PlotComponent (PlotData d) : data (std::move (d)) {}
    void paint (juce::Graphics&) override;

private:
    PlotData data;
};

//==============================================================================
class FigureComponent : public juce::Component
{
public:
    void setData (const FigureData&);
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Renders a figure off-screen at a fixed size (used for the PNG export). */
    static juce::Image renderToImage (const FigureData&, int width, int height);

private:
    FigureData data;
    juce::OwnedArray<PlotComponent> plots;
};
