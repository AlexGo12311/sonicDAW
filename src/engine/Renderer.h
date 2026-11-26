#pragma once
#include "engine/Arrangement.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace sonic::engine {

// Рендер аранжировки в буферы. Используется онлайн (аудиопоток) и оффлайн (баунс).
// Онлайн: сглаживание гейнов включено. Оффлайн: выключено (детерминизм).
class Renderer
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        trackGain_.fill (1.0f);
    }

    void setSmoothing (bool on) noexcept { smoothing_ = on; }

    void render (const Arrangement& arr, float* const* outputs, int numOut,
                 int numSamples, std::int64_t blockStart, int outputOffset) noexcept
    {
        const double sr = sampleRate_;

        bool anySolo = false;
        for (int t = 0; t < arr.numTracks(); ++t)
            anySolo = anySolo || arr.track (t).solo;

        for (int t = 0; t < arr.numTracks(); ++t)
        {
            const auto& track = arr.track (t);
            const bool audible = ! track.mute && (! anySolo || track.solo);
            const float target = audible ? track.gain : 0.0f;

            // постоянный пан: равная мощность, нормировка на единичный центр
            const double angle = ((double) track.pan + 1.0) * kPiOver4;
            const float panL = (float) (std::cos (angle) * kPanNorm);
            const float panR = (float) (std::sin (angle) * kPanNorm);

            for (const auto& clip : track.clips)
            {
                const auto* src = clip.source;
                if (src == nullptr || ! src->isValid())
                    continue;

                const auto clipStart = (std::int64_t) std::llround (clip.startSeconds * sr);
                const auto clipEnd   = clipStart + (std::int64_t) std::llround (clip.lengthSeconds * sr);
                const auto from = std::max (blockStart, clipStart);
                const auto to   = std::min (blockStart + numSamples, clipEnd);
                if (to <= from)
                    continue;

                const int n = (int) (to - from);
                const double ratio = src->sampleRate / sr;
                const double offFrames = clip.offsetSeconds * src->sampleRate;
                const double fadeIn  = std::max (clip.fadeInSeconds,  kMinFade);
                const double fadeOut = std::max (clip.fadeOutSeconds, kMinFade);

                // 1-й проход: гейн дорожки + огибающая фейдов в скретч
                float& g = trackGain_[(size_t) t];
                for (int i = 0; i < n; ++i)
                {
                    if (smoothing_)
                        g += (target - g) * kSmoothing;
                    const double posSec = (double) (from + i - clipStart) / sr;
                    scratch_[(size_t) i] = g * fadeEnvelope (posSec, fadeIn, fadeOut,
                                                             clip.lengthSeconds);
                }

                // 2-й проход: самоэмплы + пан
                for (int ch = 0; ch < numOut; ++ch)
                {
                    auto* out = outputs[ch];
                    if (out == nullptr)
                        continue;

                    const int srcCh = std::min (ch, src->numChannels - 1);
                    const float pan = (numOut < 2) ? 1.0f : (ch == 0 ? panL : panR);

                    for (int i = 0; i < n; ++i)
                    {
                        const double srcFrame = (double) (from + i - clipStart) * ratio + offFrames;
                        out[outputOffset + (int) (from + i - blockStart)] +=
                            src->sampleLinear (srcCh, srcFrame)
                              * clip.gain * scratch_[(size_t) i] * pan;
                    }
                }
            }
        }
    }

private:
    static float fadeEnvelope (double posSec, double fadeIn, double fadeOut,
                               double clipDur) noexcept
    {
        float e = 1.0f;
        if (posSec < fadeIn)
            e = (float) (posSec / fadeIn);
        const double remaining = clipDur - posSec;
        if (remaining < fadeOut)
            e = std::min (e, (float) (remaining / fadeOut));
        return e < 0.0f ? 0.0f : e;
    }

    static constexpr float   kSmoothing = 0.002f;
    static constexpr double  kMinFade   = 0.003;   // 3 мс — антиклик на границах клипа
    static constexpr double  kPiOver4   = 0.7853981633974483;
    static constexpr double  kPanNorm   = 1.4142135623730951; // 1/cos(pi/4)

    double sampleRate_ = 48000.0;
    bool smoothing_ = true;
    std::array<float, Arrangement::kMaxTracks> trackGain_ {};
    std::array<float, 16384> scratch_ {};
};

} // namespace sonic::engine