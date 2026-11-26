#pragma once
#include <JuceHeader.h>
#include "app/ProjectModel.h"
#include <memory>
#include <vector>

namespace sonic {

class TrackControlsPanel : public juce::Component
{
public:
    // Должны совпадать с TimelineEditor
    static constexpr int kRulerHeight = 26;
    static constexpr int kTrackHeight = 76;

    explicit TrackControlsPanel (ProjectModel& m) : model_ (m) {}

    void refresh()
    {
        if ((int) rows_.size() != model_.numTracks())
            rebuild();
    }

    void rebuild()
    {
        rows_.clear();

        for (int i = 0; i < model_.numTracks(); ++i)
        {
            auto row = std::make_unique<Row>();
            Row* raw = row.get();
            const auto& t = model_.track (i);

            raw->name.setText (t.name, juce::dontSendNotification);
            raw->name.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

            raw->mute.setButtonText ("M");
            raw->mute.setClickingTogglesState (true);
            raw->mute.setToggleState (t.mute, juce::dontSendNotification);
            raw->mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe57373));
            raw->mute.onClick = [this, i, raw] { model_.setTrackMute (i, raw->mute.getToggleState()); };

            raw->solo.setButtonText ("S");
            raw->solo.setClickingTogglesState (true);
            raw->solo.setToggleState (t.solo, juce::dontSendNotification);
            raw->solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffb74d));
            raw->solo.onClick = [this, i, raw] { model_.setTrackSolo (i, raw->solo.getToggleState()); };

            raw->gain.setRange (0.0, 1.5, 0.01);
            raw->gain.setValue (t.gain, juce::dontSendNotification);
            raw->gain.onValueChange = [this, i, raw]
            {
                model_.setTrackGain (i, (float) raw->gain.getValue());
            };

            addAndMakeVisible (raw->name);
            addAndMakeVisible (raw->mute);
            addAndMakeVisible (raw->solo);
            addAndMakeVisible (raw->gain);

            rows_.push_back (std::move (row));
        }

        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1c1e21));
        g.setColour (juce::Colour (0xff3a3d42));
        for (int i = 0; i < (int) rows_.size(); ++i)
            g.drawHorizontalLine (kRulerHeight + (i + 1) * kTrackHeight - 1,
                                  0.0f, (float) getWidth());
    }

    void resized() override
    {
        for (int i = 0; i < (int) rows_.size(); ++i)
        {
            juce::Rectangle<int> row (0, kRulerHeight + i * kTrackHeight, getWidth(), kTrackHeight);
            row.reduce (6, 4);

            rows_[(size_t) i]->name.setBounds (row.removeFromTop (18));
            auto buttons = row.removeFromTop (24);
            rows_[(size_t) i]->mute.setBounds (buttons.removeFromLeft (28));
            buttons.removeFromLeft (4);
            rows_[(size_t) i]->solo.setBounds (buttons.removeFromLeft (28));
            row.removeFromTop (2);
            rows_[(size_t) i]->gain.setBounds (row.removeFromTop (22));
        }
    }

private:
    struct Row
    {
        juce::Label name;
        juce::TextButton mute, solo;
        juce::Slider gain { juce::Slider::LinearBar, juce::Slider::NoTextBox };
    };

    ProjectModel& model_;
    std::vector<std::unique_ptr<Row>> rows_;
};

} // namespace sonic