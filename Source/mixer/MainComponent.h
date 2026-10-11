#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MixSession.h"
#include "PlotComponent.h"

//==============================================================================
class MixerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MixerLookAndFeel();
};

//==============================================================================
/** One stage position (LEFT / CENTER / RIGHT): its audio files and pan value. */
class PositionPanel : public juce::Component
{
public:
    PositionPanel (const juce::String& name, double defaultPan);

    void addFiles (const juce::Array<juce::File>&);
    const juce::Array<juce::File>& getFiles() const noexcept { return files; }
    double getPan() const                                    { return panSlider.getValue(); }
    const juce::String& getPositionName() const noexcept     { return positionName; }

    std::function<void()> onAddClicked;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshFileList();

    juce::String positionName;
    juce::Array<juce::File> files;

    juce::Label nameLabel, panLabel, fileList;
    juce::Slider panSlider;
    juce::TextButton addButton { "Add files..." }, clearButton { "Clear" };
};

//==============================================================================
/**
    Hybrid Pan Mixer: pick audio files for the left, centre and right positions
    and render HYBD / ICLD / ICTD mixes of them, with the analysis plots from
    Arias_HW4.m and compareOutputs.m.
*/
class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    /** --left / --center / --right <files...>, --out <dir>, --mode freqsplit|broadband, --exit.
        If any files are given the mixes are rendered straight away. */
    void handleCommandLine (const juce::StringArray& args);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class RenderThread;

    void chooseFilesFor (int position);
    void chooseOutputDirectory();
    void setOutputDirectory (const juce::File&);
    void refreshEnablement();
    void render();
    void renderFinished (const RenderOutcome&, const juce::File& outputDirectory);

    MixerLookAndFeel lnf;

    juce::Label titleLabel, hintLabel;
    juce::OwnedArray<PositionPanel> positions;

    juce::Label modeLabel, crossoverLabel, maxICTDLabel, outputLabel, statusLabel;
    juce::ComboBox modeBox;
    juce::Slider crossoverSlider, maxICTDSlider;
    juce::TextButton outputButton { "Output folder..." }, renderButton { "Render mixes" };

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TextEditor report;
    FigureComponent analysisFigure, comparisonFigure;

    juce::File outputDirectory, lastBrowsedDirectory;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<RenderThread> renderThread;
    bool quitWhenRendered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
