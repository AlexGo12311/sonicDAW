#include "app/MainComponent.h"

MainComponent::MainComponent()
{
    model_.addTrack();
    model_.addTrack();

    model_.onChanged = [this]
    {
        trackPanel_.refresh();
        refreshTrackBox();
        timeline_.repaint();
    };

    trackPanel_.rebuild();
    addAndMakeVisible (trackPanel_);
    addAndMakeVisible (timeline_);

    timeline_.onZoomChanged = [this] (double pps)
    {
        zoomSlider_.setValue (pps, juce::dontSendNotification);
    };

    addAndMakeVisible (loadButton_);
    loadButton_.onClick = [this] { openFileClicked(); };

    addAndMakeVisible (addTrackButton_);
    addTrackButton_.onClick = [this] { model_.addTrack(); };

    addAndMakeVisible (targetTrackBox_);
    refreshTrackBox();

    addAndMakeVisible (playButton_);
    playButton_.onClick = [this]
    {
        auto& t = engine_.transport();
        if (t.positionSeconds() >= model_.lengthSeconds() - 0.001)
            t.locateSeconds (0.0);
        t.play();
    };

    addAndMakeVisible (stopButton_);
    stopButton_.onClick = [this] { engine_.transport().stop(); };

    addAndMakeVisible (zeroButton_);
    zeroButton_.onClick = [this] { engine_.transport().locateSeconds (0.0); };

    addAndMakeVisible (timeLabel_);
    addAndMakeVisible (fileLabel_);

    snapToggle_.setToggleState (true, juce::dontSendNotification);
    snapToggle_.onClick = [this] { timeline_.setSnapEnabled (snapToggle_.getToggleState()); };
    addAndMakeVisible (snapToggle_);

    toneToggle_.onClick = [this] { engine_.setToneEnabled (toneToggle_.getToggleState()); };
    addAndMakeVisible (toneToggle_);

    gainSlider_.setRange (0.0, 1.0, 0.001);
    gainSlider_.setValue (0.8);
    gainSlider_.onValueChange = [this] { engine_.setMasterGain ((float) gainSlider_.getValue()); };
    addAndMakeVisible (gainSlider_);
    addAndMakeVisible (gainLabel_);

    tempoSlider_.setRange (30.0, 300.0, 1.0);
    tempoSlider_.setValue (120.0);
    tempoSlider_.onValueChange = [this] { model_.setTempo (tempoSlider_.getValue()); };
    addAndMakeVisible (tempoSlider_);
    addAndMakeVisible (tempoLabel_);

    zoomSlider_.setRange (4.0, 800.0, 0.1);
    zoomSlider_.setSkewFactorFromMidPoint (60.0);
    zoomSlider_.setValue (60.0);
    zoomSlider_.onValueChange = [this] { timeline_.setPixelsPerSecond (zoomSlider_.getValue()); };
    addAndMakeVisible (zoomSlider_);
    addAndMakeVisible (zoomLabel_);

    addAndMakeVisible (deviceSelector_);

    if (auto err = deviceManager_.initialise (0, 2, nullptr, true); err.isNotEmpty())
        DBG ("Audio init error: " + err);

    deviceManager_.addAudioCallback (&engine_);

    startTimerHz (15);
    setSize (1120, 780);
}

MainComponent::~MainComponent()
{
    stopTimer();
    model_.onChanged = nullptr;
    deviceManager_.removeAudioCallback (&engine_);
    deviceManager_.closeAudioDevice();
}

void MainComponent::refreshTrackBox()
{
    const int previous = targetTrackBox_.getSelectedId();
    targetTrackBox_.clear (juce::dontSendNotification);

    for (int i = 0; i < model_.numTracks(); ++i)
        targetTrackBox_.addItem (model_.track (i).name, i + 1);

    targetTrackBox_.setSelectedId (previous > 0 && previous <= model_.numTracks() ? previous : 1,
                                   juce::dontSendNotification);
}

void MainComponent::openFileClicked()
{
    chooser_ = std::make_unique<juce::FileChooser> ("Choose an audio file", juce::File{},
                                                    "*.wav;*.aif;*.aiff;*.flac;*.ogg");

    chooser_->launchAsync (juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                           [this] (const juce::FileChooser& fc)
                           {
                               auto f = fc.getResult();
                               if (! f.existsAsFile())
                                   return;

                               const int track = targetTrackBox_.getSelectedId() - 1;
                               juce::String error;

                               // вставляем клип туда, где стоит плейhed
                               if (model_.addClipFromFile (track, f,
                                                           engine_.transport().positionSeconds(),
                                                           error))
                                   fileLabel_.setText ("Loaded: " + f.getFileName(),
                                                       juce::dontSendNotification);
                               else
                                   fileLabel_.setText ("ERROR: " + error,
                                                       juce::dontSendNotification);
                           });
}

juce::String MainComponent::formatTime (double seconds)
{
    if (seconds < 0.0) seconds = 0.0;
    const int totalMs = (int) (seconds * 1000.0);
    return juce::String (totalMs / 60000).paddedLeft ('0', 2)
         + ":" + juce::String ((totalMs / 1000) % 60).paddedLeft ('0', 2)
         + "." + juce::String (totalMs % 1000).paddedLeft ('0', 3);
}

void MainComponent::timerCallback()
{
    timeLabel_.setText (formatTime (engine_.transport().positionSeconds())
                          + " / " + formatTime (model_.lengthSeconds()),
                        juce::dontSendNotification);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (12);

    auto row1 = r.removeFromTop (30);
    loadButton_.setBounds (row1.removeFromLeft (140));
    row1.removeFromLeft (8);
    addTrackButton_.setBounds (row1.removeFromLeft (100));
    row1.removeFromLeft (8);
    targetTrackBox_.setBounds (row1.removeFromLeft (140));
    row1.removeFromLeft (12);
    fileLabel_.setBounds (row1);

    r.removeFromTop (8);
    auto row2 = r.removeFromTop (30);
    playButton_.setBounds (row2.removeFromLeft (80));
    row2.removeFromLeft (6);
    stopButton_.setBounds (row2.removeFromLeft (80));
    row2.removeFromLeft (6);
    zeroButton_.setBounds (row2.removeFromLeft (50));
    row2.removeFromLeft (12);
    timeLabel_.setBounds (row2.removeFromLeft (190));
    snapToggle_.setBounds (row2.removeFromLeft (80));
    toneToggle_.setBounds (row2.removeFromLeft (80));
    gainLabel_.setBounds (row2.removeFromLeft (50));
    gainSlider_.setBounds (row2);

    r.removeFromTop (6);
    auto row3 = r.removeFromTop (26);
    tempoLabel_.setBounds (row3.removeFromLeft (40));
    tempoSlider_.setBounds (row3.removeFromLeft (110));
    row3.removeFromLeft (20);
    zoomLabel_.setBounds (row3.removeFromLeft (45));
    zoomSlider_.setBounds (row3.removeFromLeft (200));

    r.removeFromTop (8);
    deviceSelector_.setBounds (r.removeFromBottom (140));
    r.removeFromBottom (8);

    trackPanel_.setBounds (r.removeFromLeft (150));
    timeline_.setBounds (r);
}