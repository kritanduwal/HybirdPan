#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"
#include "Style.h"

//==============================================================================
class HybridPanMixerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Hybrid Pan Mixer"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
        mainWindow->content->handleCommandLine (getCommandLineParameterArray());
    }

    void shutdown() override            { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, MixerStyle::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content = new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (900, 700, 4000, 3000);

            // Don't open larger than the screen (matters on scaled laptop displays).
            auto size = getLocalBounds();
            if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                size = size.constrainedWithin (display->userArea.reduced (20, 30).withZeroOrigin());

            centreWithSize (size.getWidth(), size.getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        MainComponent* content = nullptr;
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (HybridPanMixerApplication)
