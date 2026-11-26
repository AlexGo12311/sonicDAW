#include "app/MainComponent.h"

MainComponent::MainComponent()
{
    playButton_.setClickingTogglesState (true);
    playButton_.onClick = [this] { engine_.setPlaying (playButton_.getToggleState()); };
    addAndMakeVisible (playButton_);

    gainSlider_.setRange (0.0, 0.5, 0.001);
    gainSlider_.setValue (0.15);
    gainSlider_.onValueChange = [this] { engine_.setLevel ((float) gainSlider_.getValue()); };
    addAndMakeVisible (gainSlider_);
    addAndMakeVisible (gainLabel_);

    freqSlider_.setRange (50.0, 2000.0, 0.1);
    freqSlider_.setSkewFactorFromMidPoint (440.0);
    freqSlider_.setValue (440.0);
    freqSlider_.onValueChange = [this] { engine_.setFrequency ((float) freqSlider_.getValue()); };
    addAndMakeVisible (freqSlider_);
    addAndMakeVisible (freqLabel_);

    addAndMakeVisible (deviceSelector_);

    // 0 входов, 2 выхода. Ошибку инициализации пока просто логируем.
    if (auto err = deviceManager_.initialise (0, 2, nullptr, true); err.isNotEmpty())
        DBG ("Audio init error: " + err);

    deviceManager_.addAudioCallback (&engine_);

    setSize (560, 520);
}

MainComponent::~MainComponent()
{
    deviceManager_.removeAudioCallback (&engine_);   // ОБЯЗАТЕЛЬНО до разрушения engine_
    deviceManager_.closeAudioDevice();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (16);

    playButton_.setBounds (r.removeFromTop (32).removeFromLeft (100));
    r.removeFromTop (12);

    auto gainRow = r.removeFromTop (32);
    gainLabel_.setBounds (gainRow.removeFromLeft (80));
    gainSlider_.setBounds (gainRow);

    auto freqRow = r.removeFromTop (32);
    freqLabel_.setBounds (freqRow.removeFromLeft (80));
    freqSlider_.setBounds (freqRow);

    r.removeFromTop (16);
    deviceSelector_.setBounds (r);
}