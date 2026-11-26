#pragma once
#include <JuceHeader.h>
#include "engine/Oscillator.h"
#include <array>
#include <atomic>
#include <algorithm>

namespace sonic {

class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    // Вызывается из UI-потока
    void setPlaying   (bool shouldPlay)  noexcept { playing_.store (shouldPlay, std::memory_order_relaxed); }
    void setLevel     (float linearGain) noexcept { level_.store (linearGain, std::memory_order_relaxed); }
    void setFrequency (float hz)         noexcept { frequency_.store (hz, std::memory_order_relaxed); }

    // ---- Аудиопоток (вызывает драйвер, ~каждые 5-10 мс) ----
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        oscillator_.prepare (device->getCurrentSampleRate());
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

        const float target = playing_.load (std::memory_order_relaxed)
                               ? level_.load (std::memory_order_relaxed)
                               : 0.0f;

        oscillator_.setFrequency (frequency_.load (std::memory_order_relaxed));

        int offset = 0;
        while (offset < numSamples)
        {
            const int n = juce::jmin (numSamples - offset, kMaxBlockSize);

            // считаем моно один раз, с плавным гейном (иначе будут щелчки)
            for (int i = 0; i < n; ++i)
            {
                gain_ += (target - gain_) * kSmoothing;
                mono_[(size_t) i] = oscillator_.nextSample() * gain_;
            }

            // раздаём по всем выходным каналам
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (auto* out = outputChannelData[ch])
                    std::copy_n (mono_.begin(), n, out + offset);

            offset += n;
        }
    }

private:
    static constexpr int   kMaxBlockSize = 8192;
    static constexpr float kSmoothing    = 0.001f;   // ~20 мс рампа

    engine::Oscillator oscillator_;
    std::atomic<bool>  playing_   { false };
    std::atomic<float> level_     { 0.15f };
    std::atomic<float> frequency_ { 440.0f };

    float gain_ = 0.0f;                        // только аудиопоток
    std::array<float, kMaxBlockSize> mono_ {}; // заранее выделено, не в callback
};

} // namespace sonic