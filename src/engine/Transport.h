#pragma once
#include <atomic>
#include <cstdint>

namespace sonic::engine {

// Плейhead хранится в СЭМПЛАХ устройства, не в секундах и не в double-батрах.
// Это фундамент sample-accurate секвенсора.
class Transport
{
public:
    // ---- любой поток ----
    void prepare (double sampleRate) noexcept
    {
        if (sampleRate > 0.0)
            sampleRate_.store (sampleRate, std::memory_order_relaxed);
    }

    void play() noexcept { playing_.store (true, std::memory_order_relaxed); }
    void stop() noexcept { playing_.store (false, std::memory_order_relaxed); }

    void locateSeconds (double seconds) noexcept
    {
        const auto frames = (std::int64_t) (seconds * sampleRate());
        position_.store (frames < 0 ? 0 : frames, std::memory_order_relaxed);
    }

    bool isPlaying() const noexcept { return playing_.load (std::memory_order_relaxed); }
    std::int64_t positionFrames() const noexcept { return position_.load (std::memory_order_relaxed); }
    double sampleRate() const noexcept { return sampleRate_.load (std::memory_order_relaxed); }
    double positionSeconds() const noexcept { return (double) positionFrames() / sampleRate(); }

    // ---- только аудиопоток ----
    void advance (int numSamples) noexcept
    {
        position_.fetch_add (numSamples, std::memory_order_relaxed);
    }

private:
    std::atomic<bool>          playing_  { false };
    std::atomic<std::int64_t>  position_ { 0 };
    std::atomic<double>        sampleRate_ { 48000.0 };
};

} // namespace sonic::engine