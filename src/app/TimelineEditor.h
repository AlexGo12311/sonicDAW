#pragma once
#include <JuceHeader.h>
#include "app/ProjectModel.h"
#include <cmath>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace sonic {

class TimelineEditor : public juce::Component,
                       private juce::Timer,
                       private juce::ScrollBar::Listener
{
public:
    static constexpr int kRulerHeight = 26;
    static constexpr int kTrackHeight = 76;

    TimelineEditor (ProjectModel& m, AudioEngine& e) : model_ (m), engine_ (e)
    {
        setWantsKeyboardFocus (true);
        scrollbar_ = std::make_unique<juce::ScrollBar> (false);
        addAndMakeVisible (*scrollbar_);
        scrollbar_->addListener (this);
        startTimerHz (30);
    }

    ~TimelineEditor() override { stopTimer(); scrollbar_->removeListener (this); }

    std::function<void (double)> onZoomChanged;

    void setPixelsPerSecond (double pps)
    {
        pixelsPerSecond_ = juce::jlimit (4.0, 800.0, pps);
        updateScrollBar();
        repaint();
    }

    double pixelsPerSecond() const noexcept { return pixelsPerSecond_; }
    int selectedTrack() const noexcept { return selectedTrack_; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff202225));

        // дорожки
        for (int i = 0; i < model_.numTracks(); ++i)
        {
            juce::Rectangle<int> lane (0, trackTop (i), getWidth(), kTrackHeight);
            g.setColour (i % 2 == 0 ? juce::Colour (0xff24262a) : juce::Colour (0xff272a2e));
            g.fillRect (lane);
            g.setColour (juce::Colour (0xff3a3d42));
            g.drawHorizontalLine (lane.getBottom() - 1, 0.0f, (float) getWidth());

            for (const auto& clip : model_.track (i).clips)
                drawClip (g, clip, i);
        }

        drawGrid (g);

        // плейhed
        const int x = (int) std::llround (secondsToX (engine_.transport().positionSeconds()));
        if (x >= 0 && x <= getWidth())
        {
            g.setColour (juce::Colours::orange);
            g.drawVerticalLine (x, 0.0f, (float) getHeight());
        }
    }

    void resized() override
    {
        scrollbar_->setBounds (0, getHeight() - 12, getWidth(), 12);
        updateScrollBar();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();

        if (e.y < kRulerHeight)
        {
            seeking_ = true;
            engine_.transport().locateSeconds (juce::jmax (0.0, xToSeconds (e.x)));
            repaint();
            return;
        }

        const int ti = trackAtY (e.y);
        if (ti < 0)
            return;

        if (auto* clip = hitTestClip (ti, e.x))
        {
            selectedTrack_ = ti;
            selectedClip_  = clip->id;
            dragTrack_     = ti;
            dragging_      = true;
            grabOffset_    = xToSeconds (e.x) - clip->startSeconds;
        }
        else
        {
            selectedClip_ = 0;
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (seeking_)
        {
            engine_.transport().locateSeconds (juce::jmax (0.0, xToSeconds (e.x)));
            repaint();
            return;
        }

        if (! dragging_)
            return;

        double newStart = xToSeconds (e.x) - grabOffset_;
        if (snapEnabled_)
            newStart = snapToBeat (newStart);
        newStart = juce::jmax (0.0, newStart);

        int target = trackAtY (e.y);
        if (target < 0)
            target = dragTrack_;

        model_.moveClip (dragTrack_, selectedClip_, target, newStart);
        dragTrack_     = target;
        selectedTrack_ = target;
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging_ = false;
        seeking_  = false;
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            const double anchor = xToSeconds (e.x);
            setPixelsPerSecond (pixelsPerSecond_ * (1.0 + w.deltaY * 2.0));
            viewStart_ = juce::jmax (0.0, anchor - (double) e.x / pixelsPerSecond_);
            if (onZoomChanged) onZoomChanged (pixelsPerSecond_);
            updateScrollBar();
            repaint();
        }
        else
        {
            const double delta = (w.deltaX != 0.0f ? w.deltaX : w.deltaY);
            viewStart_ = juce::jmax (0.0, viewStart_ - delta * 120.0 / pixelsPerSecond_);
            updateScrollBar();
            repaint();
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            if (selectedClip_ != 0)
            {
                model_.removeClip (selectedTrack_, selectedClip_);
                peaks_.erase (selectedClip_);
                selectedClip_ = 0;
                repaint();
            }
            return true;
        }

        if (key == juce::KeyPress::spaceKey)
        {
            auto& t = engine_.transport();
            if (t.isPlaying())
                t.stop();
            else
            {
                if (t.positionSeconds() >= model_.lengthSeconds() - 0.001)
                    t.locateSeconds (0.0);
                t.play();
            }
            return true;
        }

        return false;
    }

    void setSnapEnabled (bool on) { snapEnabled_ = on; }

private:
    double secondsToX (double s) const noexcept { return (s - viewStart_) * pixelsPerSecond_; }
    double xToSeconds (double x) const noexcept { return viewStart_ + x / pixelsPerSecond_; }
    int trackTop (int i) const noexcept { return kRulerHeight + i * kTrackHeight; }

    int trackAtY (int y) const noexcept
    {
        if (y < kRulerHeight) return -1;
        const int i = (y - kRulerHeight) / kTrackHeight;
        return (i < model_.numTracks()) ? i : -1;
    }

    double snapToBeat (double s) const noexcept
    {
        const double beat = 60.0 / model_.tempoBpm();
        return std::round (s / beat) * beat;
    }

    UiClip* hitTestClip (int trackIndex, int x)
    {
        auto& clips = const_cast<std::vector<UiClip>&> (model_.track (trackIndex).clips);
        const double sec = xToSeconds (x);
        for (auto& c : clips)
            if (sec >= c.startSeconds && sec < c.startSeconds + c.lengthSeconds)
                return &c;
        return nullptr;
    }

    void drawGrid (juce::Graphics& g)
    {
        const double beat = 60.0 / model_.tempoBpm();
        const double bar  = beat * 4.0;

        g.setColour (juce::Colour (0xff191b1e));
        g.fillRect (0, 0, getWidth(), kRulerHeight);

        const int firstBar = (int) std::floor (viewStart_ / bar);
        for (int b = firstBar; ; ++b)
        {
            const double x = secondsToX (b * bar);
            if (x > getWidth()) break;
            if (x < -1.0) continue;

            g.setColour (juce::Colour (0xff44474d));
            g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
            g.setColour (juce::Colours::grey);
            g.drawText (juce::String (b + 1), (int) x + 3, 2, 40, kRulerHeight - 4,
                        juce::Justification::left);

            if (beat * pixelsPerSecond_ > 14.0)
                for (int k = 1; k < 4; ++k)
                {
                    const double bx = secondsToX (b * bar + k * beat);
                    if (bx >= 0.0 && bx <= getWidth())
                    {
                        g.setColour (juce::Colour (0xff2f3237));
                        g.drawVerticalLine ((int) bx, (float) kRulerHeight, (float) getHeight());
                    }
                }
        }

        g.setColour (juce::Colour (0xff44474d));
        g.drawHorizontalLine (kRulerHeight - 1, 0.0f, (float) getWidth());
    }

    void drawClip (juce::Graphics& g, const UiClip& clip, int trackIndex)
    {
        const double x0 = secondsToX (clip.startSeconds);
        const double x1 = secondsToX (clip.startSeconds + clip.lengthSeconds);
        if (x1 < 0.0 || x0 > (double) getWidth())
            return;

        juce::Rectangle<int> r ((int) x0, trackTop (trackIndex) + 2,
                                juce::jmax (2, (int) (x1 - x0)), kTrackHeight - 6);

        const bool selected = (clip.id == selectedClip_ && trackIndex == selectedTrack_);

        g.setColour (clip.colour.withAlpha (selected ? 0.95f : 0.70f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);

        const auto& peaks = peaksFor (clip);
        const float mid   = (float) r.getCentreY() + 6.0f;
        const float halfH = (float) r.getHeight() * 0.36f;

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        for (int x = r.getX(); x < r.getRight(); x += 2)
        {
            const double frac = (double) (x - r.getX()) / (double) juce::jmax (1, r.getWidth());
            const int bucket = juce::jlimit (0, kPeakBuckets - 1, (int) (frac * kPeakBuckets));
            const float amp = peaks[(size_t) bucket] * halfH;
            g.drawVerticalLine (x, mid - amp, mid + amp);
        }

        g.setColour (selected ? juce::Colours::white : clip.colour.darker (0.6f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, selected ? 2.0f : 1.0f);

        g.setColour (juce::Colours::black);
        g.drawText (clip.name, r.reduced (4, 2).removeFromTop (14),
                    juce::Justification::left, true);
    }

    const std::vector<float>& peaksFor (const UiClip& clip)
    {
        auto it = peaks_.find (clip.id);
        if (it != peaks_.end())
            return it->second;

        std::vector<float> peaks ((size_t) kPeakBuckets, 0.0f);

        if (clip.source != nullptr && clip.source->isValid())
        {
            const auto& src = *clip.source;
            const double fpb = (double) src.numFrames / kPeakBuckets;

            for (int b = 0; b < kPeakBuckets; ++b)
            {
                const int begin = (int) ((double) b * fpb);
                const int end   = juce::jmin (src.numFrames, (int) ((double) (b + 1) * fpb));
                float peak = 0.0f;
                for (int ch = 0; ch < src.numChannels; ++ch)
                {
                    const auto& c = src.channels[(size_t) ch];
                    for (int i = begin; i < end; ++i)
                        peak = juce::jmax (peak, std::abs (c[(size_t) i]));
                }
                peaks[(size_t) b] = peak;
            }
        }

        return peaks_.emplace (clip.id, std::move (peaks)).first->second;
    }

    void updateScrollBar()
    {
        const double viewDur = (double) juce::jmax (1, getWidth()) / pixelsPerSecond_;
        const double total   = juce::jmax (model_.lengthSeconds() + 2.0, viewDur);
        scrollbar_->setRangeLimits (0.0, total);
        scrollbar_->setCurrentRange (viewStart_, viewDur);
    }

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override
    {
        if (std::abs (newRangeStart - viewStart_) > 1e-9)
        {
            viewStart_ = juce::jmax (0.0, newRangeStart);
            repaint();
        }
    }

    void timerCallback() override
    {
        const double pos = engine_.transport().positionSeconds();
        const double viewDur = (double) juce::jmax (1, getWidth()) / pixelsPerSecond_;

        // авто-прокрутка за плейhedом
        if (engine_.transport().isPlaying() && (pos < viewStart_ || pos > viewStart_ + viewDur * 0.95))
        {
            viewStart_ = juce::jmax (0.0, pos - viewDur * 0.1);
            updateScrollBar();
        }

        repaint();
    }

    static constexpr int kPeakBuckets = 512;

    ProjectModel& model_;
    AudioEngine&  engine_;
    std::unique_ptr<juce::ScrollBar> scrollbar_;

    double pixelsPerSecond_ = 60.0;
    double viewStart_ = 0.0;

    int selectedClip_ = 0, selectedTrack_ = 0, dragTrack_ = 0;
    bool dragging_ = false, seeking_ = false, snapEnabled_ = true;
    double grabOffset_ = 0.0;

    std::unordered_map<int, std::vector<float>> peaks_;
};

} // namespace sonic