#pragma once
#include <JuceHeader.h>
#include "engine/AudioData.h"
#include "engine/Arrangement.h"
#include "engine/CommandQueue.h"
#include "engine/Oscillator.h"
#include "engine/Renderer.h"
#include "engine/Transport.h"
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
        ownedAudio_.push_back (std::move (data));
        return raw;
    }

    bool send (const engine::Command& c) noexcept { return queue_.push (c); }

    void setMasterGain (float g) noexcept  { masterGain_.store (g, std::memory_order_relaxed); }
    float masterGain() const noexcept      { return masterGain_.load (std::memory_order_relaxed); }
    void setToneEnabled (bool on) noexcept { toneOn_.store (on, std::memory_order_relaxed); }

    // ================= AUDIO THREAD =================
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        deviceSampleRate_ = device->getCurrentSampleRate();
        transport_.prepare (deviceSampleRate_);
        renderer_.prepare (deviceSampleRate_);
        renderer_.setSmoothing (true);
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

        engine::Command cmd;
        while (queue_.pop (cmd))
            arrangement_.apply (cmd);

        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (auto* out = outputChannelData[ch])
                juce::FloatVectorOperations::clear (out, numSamples);

        const auto blockStart = transport_.positionFrames();
        const bool playing = transport_.isPlaying();
        const double sr = deviceSampleRate_;

        // параметры лупа
        bool loopValid = false;
        std::int64_t loopStartF = 0, loopEndF = 0, loopSpan = 0;
        if (transport_.isLoopEnabled() && sr > 0.0)
        {
            loopStartF = (std::int64_t) std::llround (transport_.loopStartSeconds() * sr);
            loopEndF   = (std::int64_t) std::llround (transport_.loopEndSeconds() * sr);
            loopSpan   = loopEndF - loopStartF;
            loopValid  = loopSpan > 0;
        }

        std::int64_t newPos = blockStart + numSamples;

        if (playing && sr > 0.0)
        {
            std::int64_t pos = blockStart;
            int done = 0;

            // Рендерим кусками: при пересечении границы лупа перематываем
            // посреди блока — это и есть sample-accurate луп.
            while (done < numSamples)
            {
                int n = numSamples - done;

                if (loopValid)
                {
                    if (pos >= loopEndF)
                        pos = loopStartF + ((pos - loopStartF) % loopSpan);
                    n = (int) juce::jmin ((std::int64_t) n, loopEndF - pos);
                }

                renderer_.render (arrangement_, outputChannelData, numOutputChannels,
                                  n, pos, done);
                pos  += n;
                done += n;
            }

            newPos = pos;
        }

        // тестовый тон
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

        // мастер-гейн с рампой
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

        if (playing)
        {
            transport_.setPositionFrames (newPos);

            if (! loopValid)
            {
                const double end = arrangement_.endSeconds();
                if (end <= 0.0 || (double) newPos / sr >= end)
                    transport_.stop();
            }
        }
    }

private:
    static constexpr int   kMaxBlockSize = 8192;
    static constexpr float kSmoothing    = 0.001f;
    static constexpr float kToneLevel    = 0.10f;

    engine::Transport  transport_;
    engine::Oscillator oscillator_;
    engine::Renderer   renderer_;
    engine::Arrangement arrangement_;
    engine::CommandQueue<engine::Command, 1024> queue_;

    std::vector<std::unique_ptr<engine::AudioData>> ownedAudio_;

    std::atomic<float> masterGain_ { 0.8f };
    std::atomic<bool>  toneOn_     { false };

    double deviceSampleRate_ = 48000.0;
    float  gain_ = 0.0f;

    std::array<float, kMaxBlockSize> toneBuffer_ {};
    std::array<float, kMaxBlockSize> gainBuffer_ {};
};

} // namespace sonic