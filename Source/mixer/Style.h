#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Shared palette / fonts for the mixer app (same dark theme as the plugin editor). */
namespace MixerStyle
{
    const juce::Colour bg      { 0xff1b1f24 };
    const juce::Colour panel   { 0xff242a31 };
    const juce::Colour ictd    { 0xff4fb3ff };   // ICTD / blue
    const juce::Colour hybd    { 0xff9b7bff };   // HYBD / purple
    const juce::Colour icld    { 0xffb0b8c0 };   // ICLD / grey
    const juce::Colour hybdLow { 0xffe58fd0 };   // HYBD low-band trace
    const juce::Colour left    { 0xff4fb3ff };
    const juce::Colour right   { 0xffff8a6b };
    const juce::Colour text    { 0xffdfe6ee };
    const juce::Colour textDim { 0xff8b95a1 };

    inline juce::Font font (float height, bool bold = false)
    {
        auto opts = juce::FontOptions (height);
        return juce::Font (bold ? opts.withStyle ("Bold") : opts);
    }

    inline juce::Font monoFont (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
    }
}
