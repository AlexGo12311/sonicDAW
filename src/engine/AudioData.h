#pragma once
#include <vector>
#include <cstddef>

namespace sonic::engine {

// Планарное хранение: channels[ch][frame]. Только чтение из аудиопотока.
struct AudioData
{
    double sampleRate = 0.0;
    int numChannels   = 0;
    int numFrames     = 0;
    std::vector<std::vector<float>> channels;

    bool isValid() const noexcept
    {
        return sampleRate > 0.0 && numChannels > 0 && numFrames > 0;
    }

    double durationSeconds() const noexcept
    {
        return sampleRate > 0.0 ? (double) numFrames / sampleRate : 0.0;
    }

    // Чтение с линейной интерполяцией по дробной позиции.
    // Нужно для файла с другой частотой дискретизации. RT-safe: без аллокаций.
    float sampleLinear (int channel, double frame) const noexcept
    {
        if (channel < 0 || channel >= numChannels)
            return 0.0f;
        if (frame < 0.0 || frame >= (double) numFrames)
            return 0.0f;

        const auto& c = channels[(size_t) channel];
        const auto i0 = (size_t) frame;
        const auto i1 = (i0 + 1 < c.size()) ? i0 + 1 : i0;
        const float frac = (float) (frame - (double) i0);
        return c[i0] + (c[i1] - c[i0]) * frac;
    }
};

} // namespace sonic::engine