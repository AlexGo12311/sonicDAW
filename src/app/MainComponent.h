#pragma once
#include <JuceHeader.h>
#include "app/AudioEngine.h"
#include "app/ProjectModel.h"
#include "app/TimelineEditor.h"
#include "app/TrackControlsPanel.h"
#include <memory>

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void openFileClicked();
    void refreshTrackBox();
    static juce::String formatTime (double seconds);

    sonic::AudioEngine        engine_;
    sonic::ProjectModel       model_      { engine_ };
    sonic::TrackControlsPanel trackPanel_ { model_ };
    sonic::TimelineEditor     timeline_   { model_, engine_ };

    juce::AudioDeviceManager deviceManager_;
    juce::AudioDeviceSelectorComponent deviceSelector_ {
        deviceManager_, 0, 0, 2, 2, false, false, true, false
    };

    juce::TextButton loadButton_     { "Load audio..." };
    juce::TextButton addTrackButton_ { "+ Track" };
    juce::ComboBox   targetTrackBox_;

    juce::TextButton playButton_ { "Play" };
    juce::TextButton stopButton_ { "Stop" };
    juce::TextButton zeroButton_ { "|<" };

    juce::Label fileLabel_ { {}, "Load a file onto a track" };
    juce::Label timeLabel_ { {}, "00:00.000" };

    juce::ToggleButton snapToggle_ { "Snap" };
    juce::ToggleButton toneToggle_ { "Tone" };

    juce::Label  gainLabel_  { {}, "Master" };
    juce::Slider gainSlider_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label  tempoLabel_ { {}, "BPM" };
    juce::Slider tempoSlider_ { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label  zoomLabel_ { {}, "Zoom" };
    juce::Slider zoomSlider_ { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};