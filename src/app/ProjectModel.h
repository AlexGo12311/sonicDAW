#pragma once
#include <JuceHeader.h>
#include "app/AudioEngine.h"
#include "app/AudioFileLoader.h"
#include "engine/Arrangement.h"
#include <array>
#include <functional>
#include <vector>

namespace sonic {

// Копия аранжировки для UI. Синхронизируется с движком через команды.
struct UiClip
{
    int id = 0;
    const engine::AudioData* source = nullptr;
    double startSeconds = 0.0, lengthSeconds = 0.0, offsetSeconds = 0.0;
    float gain = 1.0f;
    juce::String name;
    juce::Colour colour { 0xff4fc3f7 };
};

struct UiTrack
{
    juce::String name;
    float gain = 1.0f;
    bool mute = false, solo = false;
    std::vector<UiClip> clips;
};

class ProjectModel
{
public:
    explicit ProjectModel (AudioEngine& e) : engine_ (e) {}

    std::function<void()> onChanged;

    int numTracks() const noexcept { return (int) tracks_.size(); }
    const UiTrack& track (int i) const { return tracks_[(size_t) i]; }
    double tempoBpm() const noexcept { return tempo_; }

    void setTempo (double bpm) { tempo_ = juce::jlimit (30.0, 300.0, bpm); notify(); }

    double lengthSeconds() const noexcept
    {
        double end = 0.0;
        for (const auto& t : tracks_)
            for (const auto& c : t.clips)
                end = juce::jmax (end, c.startSeconds + c.lengthSeconds);
        return end;
    }

    void addTrack()
    {
        if (numTracks() >= engine::Arrangement::kMaxTracks)
            return;

        engine::Command c;
        c.type = engine::CommandType::addTrack;
        if (! engine_.send (c))
            return;

        UiTrack t;
        t.name = "Track " + juce::String (numTracks() + 1);
        tracks_.push_back (std::move (t));
        notify();
    }

    bool addClipFromFile (int trackIndex, const juce::File& file, double startSeconds,
                          juce::String& error)
    {
        auto data = loader_.load (file, error);
        if (data == nullptr)
            return false;

        const auto* raw = engine_.importAudio (std::move (data));
        if (raw == nullptr)
        {
            error = "Import failed";
            return false;
        }

        return addClip (trackIndex, raw, startSeconds, file.getFileName());
    }

    bool addClip (int trackIndex, const engine::AudioData* src, double startSeconds,
                  const juce::String& name)
    {
        if (! validTrack (trackIndex) || src == nullptr)
            return false;

        engine::Command c;
        c.type = engine::CommandType::addClip;
        c.trackIndex    = trackIndex;
        c.clipId        = nextClipId_;
        c.source        = src;
        c.startSeconds  = juce::jmax (0.0, startSeconds);
        c.lengthSeconds = src->durationSeconds();

        if (! engine_.send (c))
            return false;

        UiClip clip;
        clip.id            = nextClipId_++;
        clip.source        = src;
        clip.startSeconds  = c.startSeconds;
        clip.lengthSeconds = c.lengthSeconds;
        clip.name          = name;
        clip.colour        = palette_[(size_t) (clip.id % (int) palette_.size())];
        tracks_[(size_t) trackIndex].clips.push_back (clip);
        notify();
        return true;
    }

    void moveClip (int trackIndex, int clipId, int targetTrack, double newStart)
    {
        auto* clip = find (trackIndex, clipId);
        if (clip == nullptr || ! validTrack (targetTrack))
            return;

        newStart = juce::jmax (0.0, newStart);

        engine::Command c;
        c.type             = engine::CommandType::moveClip;
        c.trackIndex       = trackIndex;
        c.targetTrackIndex = targetTrack;
        c.clipId           = clipId;
        c.startSeconds     = newStart;
        if (! engine_.send (c))
            return;

        if (targetTrack == trackIndex)
        {
            clip->startSeconds = newStart;
        }
        else
        {
            auto copy = *clip;
            copy.startSeconds = newStart;
            tracks_[(size_t) targetTrack].clips.push_back (copy);
            removeLocal (trackIndex, clipId);
        }
        notify();
    }

    void removeClip (int trackIndex, int clipId)
    {
        if (find (trackIndex, clipId) == nullptr)
            return;

        engine::Command c;
        c.type = engine::CommandType::removeClip;
        c.trackIndex = trackIndex;
        c.clipId = clipId;
        if (! engine_.send (c))
            return;

        removeLocal (trackIndex, clipId);
        notify();
    }

    void setTrackGain (int i, float g)
    {
        if (! validTrack (i)) return;
        engine::Command c;
        c.type = engine::CommandType::setTrackGain;
        c.trackIndex = i; c.value = g;
        if (engine_.send (c)) { tracks_[(size_t) i].gain = g; notify(); }
    }

    void setTrackMute (int i, bool on)
    {
        if (! validTrack (i)) return;
        engine::Command c;
        c.type = engine::CommandType::setTrackMute;
        c.trackIndex = i; c.flag = on;
        if (engine_.send (c)) { tracks_[(size_t) i].mute = on; notify(); }
    }

    void setTrackSolo (int i, bool on)
    {
        if (! validTrack (i)) return;
        engine::Command c;
        c.type = engine::CommandType::setTrackSolo;
        c.trackIndex = i; c.flag = on;
        if (engine_.send (c)) { tracks_[(size_t) i].solo = on; notify(); }
    }

    UiClip* find (int trackIndex, int clipId) noexcept
    {
        if (! validTrack (trackIndex)) return nullptr;
        for (auto& c : tracks_[(size_t) trackIndex].clips)
            if (c.id == clipId)
                return &c;
        return nullptr;
    }

private:
    bool validTrack (int i) const noexcept { return i >= 0 && i < numTracks(); }
    void notify() { if (onChanged) onChanged(); }

    void removeLocal (int trackIndex, int clipId)
    {
        auto& v = tracks_[(size_t) trackIndex].clips;
        v.erase (std::remove_if (v.begin(), v.end(),
                                 [clipId] (const UiClip& c) { return c.id == clipId; }),
                 v.end());
    }

    AudioEngine& engine_;
    AudioFileLoader loader_;
    std::vector<UiTrack> tracks_;
    int nextClipId_ = 1;
    double tempo_ = 120.0;

    const std::array<juce::Colour, 6> palette_ {
        juce::Colour (0xff4fc3f7), juce::Colour (0xff81c784), juce::Colour (0xffffb74d),
        juce::Colour (0xffba68c8), juce::Colour (0xffe57373), juce::Colour (0xff4db6ac)
    };
};

} // namespace sonic