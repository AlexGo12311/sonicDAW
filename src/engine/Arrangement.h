#pragma once
#include "engine/AudioData.h"
#include <vector>
#include <algorithm>

namespace sonic::engine {

// Время в СЕКУНДАХ: смена sample rate устройства не двигает аранжировку.
struct Clip
{
    const AudioData* source = nullptr;   // не владеет
    int    id            = 0;
    double startSeconds  = 0.0;
    double lengthSeconds = 0.0;
    double offsetSeconds = 0.0;          // с какого места файла играем
    float  gain          = 1.0f;
};

struct Track
{
    std::vector<Clip> clips;
    float gain = 1.0f;
    bool  mute = false;
    bool  solo = false;
};

enum class CommandType : int
{
    addTrack, addClip, removeClip, moveClip, setTrackGain, setTrackMute, setTrackSolo
};

struct Command
{
    CommandType type = CommandType::addTrack;
    int trackIndex       = 0;
    int targetTrackIndex = 0;
    int clipId           = 0;
    const AudioData* source = nullptr;
    double startSeconds  = 0.0;
    double lengthSeconds = 0.0;
    double offsetSeconds = 0.0;
    float value = 0.0f;
    bool  flag  = false;
};

// ПРИНАДЛЕЖИТ АУДИОПОТОКУ. UI сюда не заходит никогда.
class Arrangement
{
public:
    static constexpr int kMaxTracks         = 16;
    static constexpr int kMaxClipsPerTrack  = 512;

    // Вызывать до регистрации аудио-callback.
    void prepare()
    {
        tracks_.resize ((size_t) kMaxTracks);
        for (auto& t : tracks_)
            t.clips.reserve ((size_t) kMaxClipsPerTrack);   // дальше аллокаций не будет
        activeTracks_ = 0;
    }

    void apply (const Command& c) noexcept
    {
        switch (c.type)
        {
            case CommandType::addTrack:
                if (activeTracks_ < kMaxTracks)
                    ++activeTracks_;
                break;

            case CommandType::addClip:
                if (auto* t = mutableTrack (c.trackIndex))
                    if (c.source != nullptr && t->clips.size() < (size_t) kMaxClipsPerTrack)
                        t->clips.push_back (Clip { c.source, c.clipId, c.startSeconds,
                                                   c.lengthSeconds, c.offsetSeconds, 1.0f });
                break;

            case CommandType::removeClip:
                if (auto* t = mutableTrack (c.trackIndex))
                    for (auto it = t->clips.begin(); it != t->clips.end(); ++it)
                        if (it->id == c.clipId) { t->clips.erase (it); break; }
                break;

            case CommandType::moveClip:
                if (auto* t = mutableTrack (c.trackIndex))
                    for (auto it = t->clips.begin(); it != t->clips.end(); ++it)
                        if (it->id == c.clipId)
                        {
                            if (c.targetTrackIndex == c.trackIndex)
                            {
                                it->startSeconds = c.startSeconds;
                            }
                            else if (auto* dst = mutableTrack (c.targetTrackIndex))
                            {
                                if (dst->clips.size() < (size_t) kMaxClipsPerTrack)
                                {
                                    auto moved = *it;
                                    moved.startSeconds = c.startSeconds;
                                    dst->clips.push_back (moved);
                                    t->clips.erase (it);
                                }
                            }
                            break;
                        }
                break;

            case CommandType::setTrackGain:
                if (auto* t = mutableTrack (c.trackIndex)) t->gain = c.value;
                break;

            case CommandType::setTrackMute:
                if (auto* t = mutableTrack (c.trackIndex)) t->mute = c.flag;
                break;

            case CommandType::setTrackSolo:
                if (auto* t = mutableTrack (c.trackIndex)) t->solo = c.flag;
                break;
        }
    }

    int numTracks() const noexcept { return activeTracks_; }
    const Track& track (int i) const noexcept { return tracks_[(size_t) i]; }

    double endSeconds() const noexcept
    {
        double end = 0.0;
        for (int i = 0; i < activeTracks_; ++i)
            for (const auto& c : tracks_[(size_t) i].clips)
                end = std::max (end, c.startSeconds + c.lengthSeconds);
        return end;
    }

private:
    Track* mutableTrack (int i) noexcept
    {
        return (i >= 0 && i < activeTracks_) ? &tracks_[(size_t) i] : nullptr;
    }

    std::vector<Track> tracks_;
    int activeTracks_ = 0;
};

} // namespace sonic::engine