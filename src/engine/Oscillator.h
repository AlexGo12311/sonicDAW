#pragma once
#include <cmath>

namespace sonic::engine {

// Генератор синуса. Реальное время: без new/delete, без блокировок.
class Oscillator
{
public:
    void prepare (double sampleRate) noexcept
    {
        if (sampleRate > 0.0)
            sampleRate_ = sampleRate;
        updateIncrement();
    }

    void setFrequency (float hz) noexcept
    {
        frequency_ = hz > 0.0f ? hz : 0.0f;
        updateIncrement();
    }

    float nextSample() noexcept
    {
        const float s = static_cast<float> (std::sin (phase_));
        phase_ += increment_;
        if (phase_ >= kTwoPi)
            phase_ -= kTwoPi;
        return s;
    }

    void reset() noexcept { phase_ = 0.0; }

private:
    void updateIncrement() noexcept
    {
        increment_ = kTwoPi * static_cast<double> (frequency_) / sampleRate_;
    }

    static constexpr double kTwoPi = 6.283185307179586;
    double sampleRate_ = 48000.0;
    double phase_      = 0.0;
    double increment_  = 0.0;
    float  frequency_  = 440.0f;
};

} // namespace sonic::engine