#include <JuceHeader.h>
#include "app/MainComponent.h"

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow()
        : DocumentWindow ("SonicDAW 0.0.1",
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                              .findColour (juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
        setResizable (true, true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class SonicDawApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "SonicDAW"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override { window_ = std::make_unique<MainWindow>(); }
    void shutdown() override                      { window_.reset(); }
    void systemRequestedQuit() override           { quit(); }

private:
    std::unique_ptr<MainWindow> window_;
};

START_JUCE_APPLICATION (SonicDawApplication)