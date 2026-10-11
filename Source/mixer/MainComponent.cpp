#include "MainComponent.h"
#include "Style.h"

using namespace MixerStyle;

namespace
{
    const char* const audioWildcard = "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg";

    bool savePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream stream (file);
        return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    void styleValueSlider (juce::Slider& s, double min, double max, double step, double value, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
        s.setRange (min, max, step);
        s.setValue (value, juce::dontSendNotification);
        s.setTextValueSuffix (suffix);
    }

    void styleCaption (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (font (12.0f));
        l.setColour (juce::Label::textColourId, textDim);
    }
}

//==============================================================================
MixerLookAndFeel::MixerLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, panel.brighter (0.12f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, panel.brighter (0.2f));
    setColour (juce::ComboBox::arrowColourId, ictd);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ictd.withAlpha (0.35f));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::Slider::trackColourId, ictd);
    setColour (juce::Slider::backgroundColourId, bg.brighter (0.12f));
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, bg);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedComponent::backgroundColourId, bg);
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::tabTextColourId, textDim);
    setColour (juce::TabbedButtonBar::frontTextColourId, text);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
}

//==============================================================================
PositionPanel::PositionPanel (const juce::String& name, double defaultPan) : positionName (name)
{
    nameLabel.setText (name, juce::dontSendNotification);
    nameLabel.setFont (font (14.0f, true));
    addAndMakeVisible (nameLabel);

    styleCaption (panLabel, "Pan");
    addAndMakeVisible (panLabel);

    styleValueSlider (panSlider, -1.0, 1.0, 0.01, defaultPan, {});
    addAndMakeVisible (panSlider);

    fileList.setFont (font (12.0f));
    fileList.setColour (juce::Label::textColourId, textDim);
    fileList.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (fileList);

    addButton.onClick   = [this] { if (onAddClicked) onAddClicked(); };
    clearButton.onClick = [this] { files.clear(); refreshFileList(); };
    addAndMakeVisible (addButton);
    addAndMakeVisible (clearButton);

    refreshFileList();
}

void PositionPanel::addFiles (const juce::Array<juce::File>& newFiles)
{
    for (auto& f : newFiles)
        files.addIfNotAlreadyThere (f);

    refreshFileList();
}

void PositionPanel::refreshFileList()
{
    juce::StringArray names;
    for (auto& f : files)
        names.add (f.getFileName());

    fileList.setText (names.isEmpty() ? "No files" : names.joinIntoString ("\n"), juce::dontSendNotification);
    clearButton.setEnabled (! files.isEmpty());
}

void PositionPanel::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void PositionPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);

    auto top = r.removeFromTop (24);
    clearButton.setBounds (top.removeFromRight (56));
    top.removeFromRight (6);
    addButton.setBounds (top.removeFromRight (92));
    nameLabel.setBounds (top);

    r.removeFromTop (4);
    auto panRow = r.removeFromTop (22);
    panLabel.setBounds (panRow.removeFromLeft (34));
    panSlider.setBounds (panRow);

    r.removeFromTop (2);
    fileList.setBounds (r);
}

//==============================================================================
/** Runs the load / render / write / analyse job off the message thread. */
class MainComponent::RenderThread : public juce::Thread
{
public:
    RenderThread (MainComponent& o, RenderRequest r)
        : juce::Thread ("Hybrid Pan render"), owner (&o), request (std::move (r)) {}

    ~RenderThread() override { stopThread (-1); }

    void run() override
    {
        auto outcome = runRender (request);

        juce::MessageManager::callAsync ([safeOwner = owner, outcome = std::move (outcome), dir = request.outputDirectory]
        {
            if (safeOwner != nullptr)
                safeOwner->renderFinished (outcome, dir);
        });
    }

private:
    juce::Component::SafePointer<MainComponent> owner;
    RenderRequest request;
};

//==============================================================================
MainComponent::MainComponent()
{
    setLookAndFeel (&lnf);

    titleLabel.setText ("HYBRID PAN MIXER", juce::dontSendNotification);
    titleLabel.setFont (font (20.0f, true));
    addAndMakeVisible (titleLabel);

    hintLabel.setText ("Left / centre / right sources  ->  ICLD, ICTD and HYBD mixes", juce::dontSendNotification);
    hintLabel.setFont (font (11.0f));
    hintLabel.setColour (juce::Label::textColourId, textDim);
    hintLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (hintLabel);

    const std::pair<const char*, double> stage[] = { { "LEFT", -1.0 }, { "CENTER", 0.0 }, { "RIGHT", 1.0 } };
    for (int i = 0; i < 3; ++i)
    {
        auto* p = positions.add (new PositionPanel (stage[i].first, stage[i].second));
        p->onAddClicked = [this, i] { chooseFilesFor (i); };
        addAndMakeVisible (p);
    }

    styleCaption (modeLabel, "HYBD mode");
    styleCaption (crossoverLabel, "Crossover");
    styleCaption (maxICTDLabel, "Max ICTD");
    addAndMakeVisible (modeLabel);
    addAndMakeVisible (crossoverLabel);
    addAndMakeVisible (maxICTDLabel);

    modeBox.addItemList ({ "Freqsplit", "Broadband" }, 1);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.onChange = [this] { refreshEnablement(); };
    addAndMakeVisible (modeBox);

    styleValueSlider (crossoverSlider, 100.0, 2000.0, 1.0, 700.0, " Hz");
    styleValueSlider (maxICTDSlider, 0.01, 2.0, 0.01, 0.3, " ms");
    addAndMakeVisible (crossoverSlider);
    addAndMakeVisible (maxICTDSlider);

    outputLabel.setFont (font (11.0f));
    outputLabel.setColour (juce::Label::textColourId, textDim);
    outputLabel.setJustificationType (juce::Justification::topLeft);
    outputLabel.setText ("Output: next to the first audio file", juce::dontSendNotification);
    addAndMakeVisible (outputLabel);

    outputButton.onClick = [this] { chooseOutputDirectory(); };
    addAndMakeVisible (outputButton);

    renderButton.setColour (juce::TextButton::buttonColourId, hybd.darker (0.3f));
    renderButton.onClick = [this] { render(); };
    addAndMakeVisible (renderButton);

    statusLabel.setFont (font (11.5f));
    statusLabel.setColour (juce::Label::textColourId, textDim);
    statusLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (statusLabel);

    report.setMultiLine (true);
    report.setReadOnly (true);
    report.setScrollbarsShown (true);
    report.setFont (monoFont (13.0f));
    report.setText ("Add audio files for the left, centre and right positions, then press \"Render mixes\".\n\n"
                    "Three stereo WAVs are written - output_HYBD.wav, output_ICLD.wav and output_ICTD.wav -\n"
                    "loudness-matched for A/B/C listening, plus the two analysis plots as PNGs.");

    tabs.setTabBarDepth (30);
    tabs.addTab ("Report",       bg, &report,           false);
    tabs.addTab ("Mix analysis", bg, &analysisFigure,   false);
    tabs.addTab ("Comparison",   bg, &comparisonFigure, false);
    addAndMakeVisible (tabs);

    lastBrowsedDirectory = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    refreshEnablement();
    setSize (1320, 780);
}

MainComponent::~MainComponent()
{
    renderThread = nullptr;
    setLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::handleCommandLine (const juce::StringArray& args)
{
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    int position = -1;
    bool anyFiles = false;

    for (int i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];

        if      (a == "--left")   position = 0;
        else if (a == "--center" || a == "--centre") position = 1;
        else if (a == "--right")  position = 2;
        else if (a == "--exit")   quitWhenRendered = true;
        else if (a == "--out" && i + 1 < args.size())
        {
            setOutputDirectory (cwd.getChildFile (args[++i].unquoted()));
            position = -1;
        }
        else if (a == "--mode" && i + 1 < args.size())
        {
            modeBox.setSelectedId (args[++i].equalsIgnoreCase ("broadband") ? 2 : 1, juce::dontSendNotification);
            position = -1;
        }
        else if (position >= 0)
        {
            positions[position]->addFiles ({ cwd.getChildFile (a.unquoted()) });
            anyFiles = true;
        }
    }

    refreshEnablement();

    if (anyFiles)
        render();
}

void MainComponent::chooseFilesFor (int position)
{
    chooser = std::make_unique<juce::FileChooser> ("Select audio files for the "
                                                       + positions[position]->getPositionName() + " position",
                                                   lastBrowsedDirectory, audioWildcard);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, position] (const juce::FileChooser& fc)
                          {
                              const auto results = fc.getResults();
                              if (results.isEmpty())
                                  return;

                              lastBrowsedDirectory = results[0].getParentDirectory();
                              positions[position]->addFiles (results);
                          });
}

void MainComponent::chooseOutputDirectory()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose where to write the mixes",
                                                   outputDirectory != juce::File() ? outputDirectory : lastBrowsedDirectory);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult() != juce::File())
                                  setOutputDirectory (fc.getResult());
                          });
}

void MainComponent::setOutputDirectory (const juce::File& dir)
{
    outputDirectory = dir;
    outputLabel.setText ("Output: " + dir.getFullPathName(), juce::dontSendNotification);
}

void MainComponent::refreshEnablement()
{
    const bool freqsplit = modeBox.getSelectedId() == 1;
    crossoverSlider.setEnabled (freqsplit);
    crossoverLabel.setEnabled (freqsplit);
}

//==============================================================================
void MainComponent::render()
{
    if (renderThread != nullptr && renderThread->isThreadRunning())
        return;

    RenderRequest request;
    for (auto* p : positions)
        for (auto& f : p->getFiles())
            request.inputs.push_back ({ f, p->getPan(), p->getPositionName() });

    if (request.inputs.empty())
    {
        statusLabel.setText ("Add at least one audio file first.", juce::dontSendNotification);
        return;
    }

    if (outputDirectory == juce::File())
        setOutputDirectory (request.inputs.front().file.getParentDirectory());

    request.outputDirectory     = outputDirectory;
    request.options.mode        = modeBox.getSelectedId() == 2 ? HybridPanMix::Mode::Broadband
                                                               : HybridPanMix::Mode::Freqsplit;
    request.options.crossoverHz = crossoverSlider.getValue();
    request.options.maxICTDms   = maxICTDSlider.getValue();

    renderButton.setEnabled (false);
    statusLabel.setText ("Rendering...", juce::dontSendNotification);

    renderThread = std::make_unique<RenderThread> (*this, std::move (request));
    renderThread->startThread();
}

void MainComponent::renderFinished (const RenderOutcome& outcome, const juce::File& dir)
{
    auto reportText = outcome.report;

    analysisFigure.setData (outcome.analysis);
    comparisonFigure.setData (outcome.comparison);

    if (outcome.ok)
    {
        // Same file names the MATLAB scripts write.
        const bool saved = savePng (FigureComponent::renderToImage (outcome.analysis, 1800, 1200),
                                    dir.getChildFile ("output_analysis.png"))
                        && savePng (FigureComponent::renderToImage (outcome.comparison, 1950, 1350),
                                    dir.getChildFile ("comparison_analysis.png"));

        reportText << "\n\n" << (saved ? "Wrote output_analysis.png and comparison_analysis.png to:\n  "
                                       : "ERROR: could not write the analysis plots to:\n  ")
                   << dir.getFullPathName()
                   << "\n\nDone. Play the three WAV files for A/B/C comparison.";

        // The MATLAB prints this to the console; keep a copy next to the mixes.
        dir.getChildFile ("output_report.txt").replaceWithText (reportText);

        statusLabel.setText ("Done. Mixes written to " + dir.getFullPathName(), juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText ("Rendering failed - see the report.", juce::dontSendNotification);
    }

    report.setText (reportText);
    renderButton.setEnabled (true);

    if (quitWhenRendered)
        juce::JUCEApplication::quit();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    auto header = getLocalBounds().removeFromTop (48).toFloat();
    g.setColour (panel);
    g.fillRect (header);
    g.setColour (ictd);
    g.fillRect (header.removeFromBottom (2.0f));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop (48).reduced (16, 0);
    titleLabel.setBounds (header.removeFromLeft (240));
    hintLabel.setBounds (header);

    r.reduce (12, 12);

    auto side = r.removeFromLeft (320);
    r.removeFromLeft (12);
    tabs.setBounds (r);

    for (auto* p : positions)
    {
        p->setBounds (side.removeFromTop (128));
        side.removeFromTop (8);
    }

    auto row = [&side] (juce::Label& label, juce::Component& control)
    {
        auto area = side.removeFromTop (26);
        label.setBounds (area.removeFromLeft (78));
        control.setBounds (area);
        side.removeFromTop (4);
    };

    side.removeFromTop (4);
    row (modeLabel, modeBox);
    row (crossoverLabel, crossoverSlider);
    row (maxICTDLabel, maxICTDSlider);

    side.removeFromTop (6);
    outputButton.setBounds (side.removeFromTop (26).removeFromLeft (130));
    side.removeFromTop (4);
    outputLabel.setBounds (side.removeFromTop (44));

    renderButton.setBounds (side.removeFromTop (38));
    side.removeFromTop (6);
    statusLabel.setBounds (side);
}
