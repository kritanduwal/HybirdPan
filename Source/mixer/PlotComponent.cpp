#include "PlotComponent.h"
#include "Style.h"

namespace
{
    struct Ticks
    {
        std::vector<float> values;
        float step = 1.0f;
    };

    Ticks linearTicks (float lo, float hi, int target)
    {
        Ticks t;
        const double span = (double) hi - (double) lo;
        if (! (span > 0.0) || target < 1)
            return t;

        const double raw  = span / target;
        const double mag  = std::pow (10.0, std::floor (std::log10 (raw)));
        const double norm = raw / mag;
        const double step = (norm < 1.5 ? 1.0 : norm < 3.5 ? 2.0 : norm < 7.5 ? 5.0 : 10.0) * mag;

        for (double v = std::ceil (lo / step) * step; v <= hi + step * 1.0e-6; v += step)
            t.values.push_back (std::abs (v) < step * 1.0e-6 ? 0.0f : (float) v);

        t.step = (float) step;
        return t;
    }

    juce::String linearTickText (float v, float step)
    {
        if (step >= 1.0f)
            return juce::String (juce::roundToInt (v));

        return juce::String (v, juce::jlimit (1, 4, (int) std::ceil (-std::log10 (step) - 1.0e-6f)));
    }

    std::vector<float> logTicks (float lo, float hi)
    {
        std::vector<float> t;
        for (int e = (int) std::floor (std::log10 (lo)); e <= (int) std::ceil (std::log10 (hi)); ++e)
            for (float m : { 1.0f, 2.0f, 5.0f })
            {
                const float v = m * std::pow (10.0f, (float) e);
                if (v >= lo * 0.999f && v <= hi * 1.001f)
                    t.push_back (v);
            }

        return t;
    }

    juce::String logTickText (float v)
    {
        return v >= 1000.0f ? juce::String (juce::roundToInt (v / 1000.0f)) + "k"
                            : juce::String (juce::roundToInt (v));
    }

    // Rough text width; good enough for laying out legend entries.
    float approxTextWidth (const juce::String& s, float fontHeight)
    {
        return (float) s.length() * fontHeight * 0.56f;
    }
}

//==============================================================================
void PlotComponent::paint (juce::Graphics& g)
{
    using namespace MixerStyle;

    auto bounds = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto area = bounds.reduced (8.0f, 6.0f);

    g.setColour (text);
    g.setFont (font (12.5f, true));
    g.drawText (data.title, area.removeFromTop (18.0f), juce::Justification::centred, true);

    // Legend sits in its own row above the axes so it never hides the data.
    auto legendRow = area.removeFromTop (16.0f);
    {
        const float fh = 11.0f;
        float total = 0.0f;
        for (auto& s : data.series)
            if (s.name.isNotEmpty())
                total += 22.0f + approxTextWidth (s.name, fh) + 12.0f;

        float x = legendRow.getCentreX() - total * 0.5f;
        g.setFont (font (fh));

        for (auto& s : data.series)
        {
            if (s.name.isEmpty())
                continue;

            const float w = approxTextWidth (s.name, fh);
            g.setColour (s.colour);
            g.fillRect (x, legendRow.getCentreY() - 1.5f, 16.0f, 3.0f);
            g.setColour (textDim);
            g.drawText (s.name, juce::Rectangle<float> (x + 22.0f, legendRow.getY(), w + 10.0f, legendRow.getHeight()),
                        juce::Justification::centredLeft, false);
            x += 22.0f + w + 12.0f;
        }
    }

    auto xLabelRow  = area.removeFromBottom (15.0f);
    auto xTickRow   = area.removeFromBottom (14.0f);
    auto yLabelCol  = area.removeFromLeft (14.0f);
    auto yTickCol   = area.removeFromLeft (38.0f);
    area.removeFromRight (6.0f);
    area.removeFromTop (4.0f);

    auto plot = area;
    if (data.square)
    {
        const float side = juce::jmin (plot.getWidth(), plot.getHeight());
        plot = plot.withSizeKeepingCentre (side, side);
    }

    if (plot.getWidth() < 10.0f || plot.getHeight() < 10.0f)
        return;

    xLabelRow = xLabelRow.withX (plot.getX()).withWidth (plot.getWidth());
    xTickRow  = xTickRow .withX (plot.getX()).withWidth (plot.getWidth());

    //==============================================================================
    // Axis ranges
    float x0 = data.xRange.getStart(), x1 = data.xRange.getEnd();
    if (data.xRange.isEmpty())
    {
        x0 = std::numeric_limits<float>::max();
        x1 = std::numeric_limits<float>::lowest();
        for (auto& s : data.series)
            for (auto v : s.x)
                if (std::isfinite (v) && (! data.logX || v > 0.0f))
                {
                    x0 = juce::jmin (x0, v);
                    x1 = juce::jmax (x1, v);
                }
    }

    float y0 = data.yRange.getStart(), y1 = data.yRange.getEnd();
    if (data.yRange.isEmpty())
    {
        y0 = std::numeric_limits<float>::max();
        y1 = std::numeric_limits<float>::lowest();
        for (auto& s : data.series)
            for (size_t i = 0; i < s.y.size(); ++i)
                if (std::isfinite (s.y[i]) && s.x[i] >= x0 && s.x[i] <= x1)
                {
                    y0 = juce::jmin (y0, s.y[i]);
                    y1 = juce::jmax (y1, s.y[i]);
                }

        const float pad = (y1 - y0) * 0.06f;
        y0 -= pad;
        y1 += pad;
    }

    if (! (x1 > x0)) { x0 -= 1.0f; x1 = x0 + 2.0f; }
    if (! (y1 > y0)) { y0 -= 1.0f; y1 = y0 + 2.0f; }
    if (data.logX)   x0 = juce::jmax (x0, 1.0e-6f);

    auto xToPx = [&] (float v)
    {
        const float t = data.logX ? std::log (v / x0) / std::log (x1 / x0) : (v - x0) / (x1 - x0);
        return plot.getX() + t * plot.getWidth();
    };

    auto yToPx = [&] (float v)
    {
        const float py = plot.getBottom() - (v - y0) / (y1 - y0) * plot.getHeight();
        return juce::jlimit (plot.getY() - 2000.0f, plot.getBottom() + 2000.0f, py);
    };

    //==============================================================================
    // Frame, grid and tick labels
    g.setColour (bg);
    g.fillRect (plot);

    g.setFont (font (10.0f));
    const auto grid = panel.brighter (0.18f);

    if (data.logX)
    {
        for (auto v : logTicks (x0, x1))
        {
            const float px = xToPx (v);
            g.setColour (grid);
            g.fillRect (px, plot.getY(), 1.0f, plot.getHeight());
            g.setColour (textDim);
            g.drawText (logTickText (v), juce::Rectangle<float> (px - 20.0f, xTickRow.getY(), 40.0f, xTickRow.getHeight()),
                        juce::Justification::centred, false);
        }
    }
    else
    {
        const auto ticks = linearTicks (x0, x1, juce::jmax (2, (int) (plot.getWidth() / 70.0f)));
        for (auto v : ticks.values)
        {
            const float px = xToPx (v);
            g.setColour (grid);
            g.fillRect (px, plot.getY(), 1.0f, plot.getHeight());
            g.setColour (textDim);
            g.drawText (linearTickText (v, ticks.step),
                        juce::Rectangle<float> (px - 25.0f, xTickRow.getY(), 50.0f, xTickRow.getHeight()),
                        juce::Justification::centred, false);
        }
    }

    {
        const auto ticks = linearTicks (y0, y1, juce::jmax (2, (int) (plot.getHeight() / 38.0f)));
        for (auto v : ticks.values)
        {
            const float py = yToPx (v);
            g.setColour (grid);
            g.fillRect (plot.getX(), py, plot.getWidth(), 1.0f);
            g.setColour (textDim);
            g.drawText (linearTickText (v, ticks.step),
                        juce::Rectangle<float> (plot.getX() - 42.0f, py - 7.0f, 38.0f, 14.0f),
                        juce::Justification::centredRight, false);
        }
    }

    juce::ignoreUnused (yTickCol);

    g.setColour (textDim);
    g.setFont (font (10.5f));
    g.drawText (data.xLabel, xLabelRow, juce::Justification::centred, true);

    {
        juce::Graphics::ScopedSaveState state (g);
        const auto c = juce::Point<float> (yLabelCol.getCentreX(), plot.getCentreY());
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, c.x, c.y));
        g.drawText (data.yLabel, juce::Rectangle<float> (300.0f, 14.0f).withCentre (c),
                    juce::Justification::centred, true);
    }

    //==============================================================================
    // Data
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plot.toNearestInt());

        for (auto& v : data.vLines)
        {
            const float px = xToPx (v.x);
            g.setColour (text.withAlpha (0.6f));
            for (float y = plot.getY(); y < plot.getBottom(); y += 8.0f)
                g.fillRect (px - 0.5f, y, 1.0f, 4.0f);

            g.setFont (font (10.0f));
            g.drawText (v.label, juce::Rectangle<float> (px + 4.0f, plot.getY() + 2.0f, 120.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }

        for (auto& s : data.series)
        {
            g.setColour (s.colour);
            const size_t n = juce::jmin (s.x.size(), s.y.size());

            if (s.style == PlotSeries::Style::dots)
            {
                for (size_t i = 0; i < n; ++i)
                    if (std::isfinite (s.x[i]) && std::isfinite (s.y[i]))
                        g.fillRect (xToPx (s.x[i]) - 0.75f, yToPx (s.y[i]) - 0.75f, 1.5f, 1.5f);
                continue;
            }

            // Collapse all points that land in the same pixel column into a
            // min/max segment, so 65k-point spectra stay cheap to draw.
            juce::Path path;
            bool started = false;
            int column = std::numeric_limits<int>::min(), count = 0;
            float firstX = 0.0f, firstY = 0.0f, lastY = 0.0f, minY = 0.0f, maxY = 0.0f;

            auto flush = [&]
            {
                if (count == 0)
                    return;

                const float px = count == 1 ? firstX : (float) column + 0.5f;
                if (started) path.lineTo (px, firstY);
                else         path.startNewSubPath (px, firstY);
                started = true;

                if (count > 1)
                {
                    path.lineTo (px, minY);
                    path.lineTo (px, maxY);
                    path.lineTo (px, lastY);
                }

                count = 0;
            };

            for (size_t i = 0; i < n; ++i)
            {
                if (! std::isfinite (s.x[i]) || ! std::isfinite (s.y[i]) || (data.logX && s.x[i] <= 0.0f))
                    continue;

                const float px = xToPx (s.x[i]);
                const float py = yToPx (s.y[i]);
                if (! std::isfinite (px))
                    continue;

                const int c = (int) std::floor (px);
                if (c != column)
                {
                    flush();
                    column = c;
                    firstX = px;
                    firstY = minY = maxY = py;
                }

                minY = juce::jmin (minY, py);
                maxY = juce::jmax (maxY, py);
                lastY = py;
                ++count;
            }
            flush();

            juce::PathStrokeType stroke (s.thickness, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);
            if (s.style == PlotSeries::Style::dashed)
            {
                const float dashes[] = { 6.0f, 4.0f };
                juce::Path dashed;
                stroke.createDashedStroke (dashed, path, dashes, 2);
                g.fillPath (dashed);
            }
            else
            {
                g.strokePath (path, stroke);
            }
        }

        g.setFont (font (10.0f, true));
        for (auto& m : data.markers)
        {
            const auto c = juce::Point<float> (xToPx (m.x), yToPx (m.y));
            const auto box = juce::Rectangle<float> (9.0f, 9.0f).withCentre (c);
            g.setColour (m.colour);
            if (m.square) g.drawRect (box, 2.0f);
            else          g.drawEllipse (box, 2.0f);

            g.drawText (m.text, juce::Rectangle<float> (c.x + 7.0f, c.y - 16.0f, 170.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    g.setColour (panel.brighter (0.3f));
    g.drawRect (plot, 1.0f);
}

//==============================================================================
void FigureComponent::setData (const FigureData& d)
{
    data = d;
    plots.clear();

    for (auto& cell : data.cells)
        addAndMakeVisible (plots.add (new PlotComponent (cell.plot)));

    resized();
    repaint();
}

void FigureComponent::paint (juce::Graphics& g)
{
    using namespace MixerStyle;
    g.fillAll (bg);

    if (data.cells.empty())
    {
        g.setColour (textDim);
        g.setFont (font (14.0f));
        g.drawText ("Render the mixes to see this analysis.", getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setColour (text);
    g.setFont (font (15.0f, true));
    g.drawText (data.title, getLocalBounds().removeFromTop (30), juce::Justification::centred, true);
}

void FigureComponent::resized()
{
    auto area = getLocalBounds().reduced (4);
    area.removeFromTop (26);

    const float cw = (float) area.getWidth()  / (float) juce::jmax (1, data.columns);
    const float ch = (float) area.getHeight() / (float) juce::jmax (1, data.rows);

    for (int i = 0; i < plots.size(); ++i)
    {
        const auto& cell = data.cells[(size_t) i];
        plots[i]->setBounds (juce::Rectangle<float> ((float) area.getX() + cw * (float) cell.column,
                                                     (float) area.getY() + ch * (float) cell.row,
                                                     cw * (float) cell.columnSpan, ch).toNearestInt());
    }
}

juce::Image FigureComponent::renderToImage (const FigureData& d, int width, int height)
{
    FigureComponent figure;
    figure.setSize (width, height);
    figure.setData (d);
    return figure.createComponentSnapshot (figure.getLocalBounds());
}
