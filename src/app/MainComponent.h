#pragma once
#include <JuceHeader.h>
#include "app/AudioEngine.h"

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    sonic::AudioEngine engine_;
    juce::AudioDeviceManager deviceManager_;

    juce::AudioDeviceSelectorComponent deviceSelector_ {
        deviceManager_, 0, 0, 2, 2, false, false, true, false
    };

    juce::ToggleButton playButton_ { "Play" };
    juce::Label  gainLabel_ { {}, "Gain" };
    juce::Slider gainSlider_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label  freqLabel_ { {}, "Frequency" };
    juce::Slider freqSlider_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};