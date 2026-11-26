#pragma once
#include <JuceHeader.h>
#include "engine/AudioData.h"
#include "engine/Arrangement.h"
#include "engine/CommandQueue.h"
#include "engine/Oscillator.h"
#include "engine/Transport.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace sonic {

class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine() { arrangement_.prepare(); }

    engine::Transport& transport() noexcept { return transport_; }

    // ================= MESSAGE THREAD =================
    const engine::AudioData* importAudio (std::unique_ptr<engine::AudioData> data)
    {
        if (data == nullptr || ! data->isValid())
            return nullptr;

        const auto* raw = data.get();
        ownedAudio_.push_back (std::move (data));   // живёт до конца сессии
        return raw;
    }

    bool send (const engine::Command& c) noexcept { return queue_.push (c); }

    void setMasterGain (float g) noexcept  { masterGain_.store (g, std::memory_order_relaxed); }
    void setToneEnabled (bool on) noexcept { toneOn_.store (on, std::memory_order_relaxed); }

    // ================= AUDIO THREAD =================
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        deviceSampleRate_ = device->getCurrentSampleRate();
        transport_.prepare (deviceSampleRate_);
        oscillator_.prepare (deviceSampleRate_);
        gain_ = 0.0f;
    }

    void audioDeviceStopped() override
    {
        oscillator_.reset();
        gain_ = 0.0f;
    }

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override
    {
        juce::ignoreUnused (inputChannelData, numInputChannels, context);

        // 1. Применяем правки от UI к своей приватной аранжировке
        engine::Command cmd;
        while (queue_.pop (cmd))
            arrangement_.apply (cmd);

        // 2. Обнуляем шины
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (auto* out = outputChannelData[ch])
                juce::FloatVectorOperations::clear (out, numSamples);

        const auto blockStart = transport_.positionFrames();
        const bool playing = transport_.isPlaying();

        // 3. Рендер аранжировки
        if (playing && deviceSampleRate_ > 0.0)
            renderArrangement (outputChannelData, numOutputChannels, numSamples, blockStart);

        // 4. Тестовый тон
        if (toneOn_.load (std::memory_order_relaxed))
        {
            int offset = 0;
            while (offset < numSamples)
            {
                const int n = juce::jmin (numSamples - offset, kMaxBlockSize);
                for (int i = 0; i < n; ++i)
                    toneBuffer_[(size_t) i] = oscillator_.nextSample() * kToneLevel;

                for (int ch = 0; ch < numOutputChannels; ++ch)
                    if (auto* out = outputChannelData[ch])
                        juce::FloatVectorOperations::add (out + offset, toneBuffer_.data(), n);
                offset += n;
            }
        }

        // 5. Мастер-гейн с рампой
        const float target = masterGain_.load (std::memory_order_relaxed);
        int offset = 0;
        while (offset < numSamples)
        {
            const int n = juce::jmin (numSamples - offset, kMaxBlockSize);
            for (int i = 0; i < n; ++i)
            {
                gain_ += (target - gain_) * kSmoothing;
                gainBuffer_[(size_t) i] = gain_;
            }
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (auto* out = outputChannelData[ch])
                    juce::FloatVectorOperations::multiply (out + offset, gainBuffer_.data(), n);
            offset += n;
        }

        // 6. Плейhed
        if (playing)
        {
            transport_.advance (numSamples);
            const double end = arrangement_.endSeconds();
            if (end <= 0.0
                || (double) (blockStart + numSamples) / deviceSampleRate_ >= end)
                transport_.stop();
        }
    }

private:
    void renderArrangement (float* const* outputs, int numOut, int numSamples,
                            std::int64_t blockStart) noexcept
    {
        const auto blockEnd = blockStart + numSamples;
        const double sr = deviceSampleRate_;

        bool anySolo = false;
        for (int t = 0; t < arrangement_.numTracks(); ++t)
            anySolo = anySolo || arrangement_.track (t).solo;

        for (int t = 0; t < arrangement_.numTracks(); ++t)
        {
            const auto& track = arrangement_.track (t);
            if (track.mute || (anySolo && ! track.solo))
                continue;

            for (const auto& clip : track.clips)
            {
                const auto* src = clip.source;
                if (src == nullptr || ! src->isValid())
                    continue;

                // SAMPLE-ACCURATE: границы клипа вычисляются в сэмплах,
                // пересечение с блоком — точное, без округления до блока.
                const auto clipStart = (std::int64_t) std::llround (clip.startSeconds * sr);
                const auto clipEnd   = clipStart + (std::int64_t) std::llround (clip.lengthSeconds * sr);
                const auto from = std::max (blockStart, clipStart);
                const auto to   = std::min (blockEnd, clipEnd);
                if (to <= from)
                    continue;

                const double ratio = src->sampleRate / sr;
                const double offsetFrames = clip.offsetSeconds * src->sampleRate;
                const float g = clip.gain * track.gain;

                for (int ch = 0; ch < numOut; ++ch)
                {
                    auto* out = outputs[ch];
                    if (out == nullptr)
                        continue;

                    const int srcCh = juce::jmin (ch, src->numChannels - 1);
                    for (auto f = from; f < to; ++f)
                    {
                        const double srcFrame = (double) (f - clipStart) * ratio + offsetFrames;
                        out[(int) (f - blockStart)] += src->sampleLinear (srcCh, srcFrame) * g;
                    }
                }
            }
        }
    }

    static constexpr int   kMaxBlockSize = 8192;
    static constexpr float kSmoothing    = 0.001f;
    static constexpr float kToneLevel    = 0.10f;

    engine::Transport  transport_;
    engine::Oscillator oscillator_;
    engine::Arrangement arrangement_;                        // только audio thread
    engine::CommandQueue<engine::Command, 1024> queue_;

    std::vector<std::unique_ptr<engine::AudioData>> ownedAudio_; // только message thread

    std::atomic<float> masterGain_ { 0.8f };
    std::atomic<bool>  toneOn_     { false };

    double deviceSampleRate_ = 48000.0;
    float  gain_ = 0.0f;

    std::array<float, kMaxBlockSize> toneBuffer_ {};
    std::array<float, kMaxBlockSize> gainBuffer_ {};
};

} // namespace sonic