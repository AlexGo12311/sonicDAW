#pragma once
#include <JuceHeader.h>
#include "app/AudioEngine.h"
#include "app/AudioFileLoader.h"
#include "engine/Arrangement.h"
#include <array>
#include <functional>
#include <vector>

namespace sonic {

struct UiClip
{
    int id = 0;
    const engine::AudioData* source = nullptr;
    double startSeconds = 0.0, lengthSeconds = 0.0, offsetSeconds = 0.0;
    double fadeInSeconds = 0.0, fadeOutSeconds = 0.0;
    float gain = 1.0f;
    juce::String name;
    juce::Colour colour { 0xff4fc3f7 };
};

struct UiTrack
{
    juce::String name;
    float gain = 1.0f;
    float pan  = 0.0f;
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

    // ================= дорожки =================
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

    void setTrackGain (int i, float g)
    {
        if (! validTrack (i)) return;
        engine::Command c;
        c.type = engine::CommandType::setTrackGain;
        c.trackIndex = i; c.value = g;
        if (engine_.send (c)) { tracks_[(size_t) i].gain = g; notify(); }
    }

    void setTrackPan (int i, float p)
    {
        if (! validTrack (i)) return;
        engine::Command c;
        c.type = engine::CommandType::setTrackPan;
        c.trackIndex = i; c.value = juce::jlimit (-1.0f, 1.0f, p);
        if (engine_.send (c)) { tracks_[(size_t) i].pan = c.value; notify(); }
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

    // ================= клипы =================
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
        c.type = engine::CommandType::moveClip;
        c.trackIndex = trackIndex; c.targetTrackIndex = targetTrack;
        c.clipId = clipId; c.startSeconds = newStart;
        if (! engine_.send (c))
            return;

        if (targetTrack == trackIndex)
            clip->startSeconds = newStart;
        else
        {
            auto copy = *clip;
            copy.startSeconds = newStart;
            tracks_[(size_t) targetTrack].clips.push_back (copy);
            removeLocal (trackIndex, clipId);
        }
        notify();
    }

    void reshapeClip (int trackIndex, int clipId, double newStart, double newLength,
                      double newOffset)
    {
        auto* clip = find (trackIndex, clipId);
        if (clip == nullptr)
            return;

        newStart  = juce::jmax (0.0, newStart);
        newLength = juce::jmax (0.02, newLength);
        newOffset = juce::jmax (0.0, newOffset);

        engine::Command c;
        c.type = engine::CommandType::setClipShape;
        c.trackIndex = trackIndex; c.clipId = clipId;
        c.startSeconds = newStart; c.lengthSeconds = newLength; c.offsetSeconds = newOffset;
        if (! engine_.send (c))
            return;

        clip->startSeconds  = newStart;
        clip->lengthSeconds = newLength;
        clip->offsetSeconds = newOffset;
        notify();
    }

    void setClipFades (int trackIndex, int clipId, double fadeIn, double fadeOut)
    {
        auto* clip = find (trackIndex, clipId);
        if (clip == nullptr)
            return;

        engine::Command c;
        c.type = engine::CommandType::setClipFades;
        c.trackIndex = trackIndex; c.clipId = clipId;
        c.startSeconds  = juce::jmax (0.0, fadeIn);
        c.lengthSeconds = juce::jmax (0.0, fadeOut);
        if (! engine_.send (c))
            return;

        clip->fadeInSeconds  = c.startSeconds;
        clip->fadeOutSeconds = c.lengthSeconds;
        notify();
    }

    // Разрезать клип по позиции плейхеда
    void splitClip (int trackIndex, int clipId, double positionSeconds)
    {
        auto* clip = find (trackIndex, clipId);
        if (clip == nullptr)
            return;

        if (positionSeconds <= clip->startSeconds + 0.01
            || positionSeconds >= clip->startSeconds + clip->lengthSeconds - 0.01)
            return;

        const double splitLocal = positionSeconds - clip->startSeconds;

        // правая часть — новый клип
        engine::Command add;
        add.type = engine::CommandType::addClip;
        add.trackIndex    = trackIndex;
        add.clipId        = nextClipId_;
        add.source        = clip->source;
        add.startSeconds  = positionSeconds;
        add.lengthSeconds = clip->lengthSeconds - splitLocal;
        add.offsetSeconds = clip->offsetSeconds + splitLocal;
        if (! engine_.send (add))
            return;

        // левая часть — укорачиваем оригинал
        engine::Command shape;
        shape.type = engine::CommandType::setClipShape;
        shape.trackIndex = trackIndex; shape.clipId = clipId;
        shape.startSeconds  = clip->startSeconds;
        shape.lengthSeconds = splitLocal;
        shape.offsetSeconds = clip->offsetSeconds;
        if (! engine_.send (shape))
            return;

        UiClip right = *clip;
        right.id            = nextClipId_++;
        right.startSeconds  = positionSeconds;
        right.lengthSeconds = clip->lengthSeconds - splitLocal;
        right.offsetSeconds = clip->offsetSeconds + splitLocal;

        clip->lengthSeconds = splitLocal;
        tracks_[(size_t) trackIndex].clips.push_back (right);
        notify();
    }

    void removeClip (int trackIndex, int clipId)
    {
        if (find (trackIndex, clipId) == nullptr)
            return;
        engine::Command c;
        c.type = engine::CommandType::removeClip;
        c.trackIndex = trackIndex; c.clipId = clipId;
        if (! engine_.send (c))
            return;
        removeLocal (trackIndex, clipId);
        notify();
    }

    UiClip* find (int trackIndex, int clipId) noexcept
    {
        if (! validTrack (trackIndex))
            return nullptr;
        for (auto& c : tracks_[(size_t) trackIndex].clips)
            if (c.id == clipId)
                return &c;
        return nullptr;
    }

    // ================= луп =================
    void setLoopRange (double a, double b)
    {
        loopStartSeconds_ = juce::jmin (a, b);
        loopEndSeconds_   = juce::jmax (a, b);
        engine_.transport().setLoopRange (a, b);
        notify();
    }

    void setLoopEnabled (bool on)
    {
        loopEnabled_ = on;
        engine_.transport().setLoopEnabled (on);
        notify();
    }

    void clearLoop()
    {
        setLoopRange (0.0, 0.0);
        setLoopEnabled (false);
    }

    bool isLoopEnabled()     const noexcept { return loopEnabled_; }
    double loopStartSeconds() const noexcept { return loopStartSeconds_; }
    double loopEndSeconds()   const noexcept { return loopEndSeconds_; }

    // ================= оффлайн-слепок для баунса =================
    engine::Arrangement makeOfflineArrangement() const
    {
        engine::Arrangement arr;
        arr.prepare();

        for (int t = 0; t < numTracks(); ++t)
        {
            engine::Command addTrack;
            addTrack.type = engine::CommandType::addTrack;
            arr.apply (addTrack);

            const auto& tr = tracks_[(size_t) t];
            arr.setupTrack (t, tr.gain, tr.pan, tr.mute, tr.solo);

            for (const auto& clip : tr.clips)
            {
                engine::Command c;
                c.type = engine::CommandType::addClip;
                c.trackIndex    = t;
                c.clipId        = clip.id;
                c.source        = clip.source;
                c.startSeconds  = clip.startSeconds;
                c.lengthSeconds = clip.lengthSeconds;
                c.offsetSeconds = clip.offsetSeconds;
                arr.apply (c);
                arr.setupClip (t, clip.id, clip.fadeInSeconds, clip.fadeOutSeconds);
            }
        }

        return arr;
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

    bool loopEnabled_ = false;
    double loopStartSeconds_ = 0.0, loopEndSeconds_ = 0.0;

    const std::array<juce::Colour, 6> palette_ {
        juce::Colour (0xff4fc3f7), juce::Colour (0xff81c784), juce::Colour (0xffffb74d),
        juce::Colour (0xffba68c8), juce::Colour (0xffe57373), juce::Colour (0xff4db6ac)
    };
};

} // namespace sonic