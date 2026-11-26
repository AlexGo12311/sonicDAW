#pragma once
#include <JuceHeader.h>
#include "app/ProjectModel.h"
#include "engine/Renderer.h"
#include <memory>

namespace sonic {

class OfflineRenderer
{
public:
    // Полный оффлайн-просчёт аранжировки в 16-битный WAV, 48 кГц, стерео.
    static bool renderToFile (const ProjectModel& model, const AudioEngine& engine,
                              const juce::File& file, juce::String& error)
    {
        constexpr double kSampleRate = 48000.0;
        constexpr double kMaxLengthSeconds = 1800.0;

        const double length = model.lengthSeconds();
        if (length <= 0.0)
        {
            error = "Nothing to bounce";
            return false;
        }
        if (length > kMaxLengthSeconds)
        {
            error = "Project too long to bounce in RAM";
            return false;
        }

        auto arrangement = model.makeOfflineArrangement();

        engine::Renderer renderer;
        renderer.prepare (kSampleRate);
        renderer.setSmoothing (false);

        const int numChannels = 2;
        const auto totalFrames = (std::int64_t) std::llround ((length + 0.05) * kSampleRate);

        juce::AudioBuffer<float> buffer (numChannels, (int) totalFrames);
        buffer.clear();

        constexpr int kBlockSize = 4096;
        std::int64_t done = 0;

        while (done < totalFrames)
        {
            const int n = (int) juce::jmin ((std::int64_t) kBlockSize, totalFrames - done);
            float* ptrs[2] = { buffer.getWritePointer (0) + done,
                               buffer.getWritePointer (1) + done };

            renderer.render (arrangement, ptrs, numChannels, n, done, 0);
            done += n;
        }

        buffer.applyGain (engine.masterGain());

        juce::WavAudioFormat wavFormat;
        auto stream = file.createOutputStream();
        if (stream == nullptr)
        {
            error = "Cannot open file for writing";
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wavFormat.createWriterFor (stream.get(), kSampleRate, numChannels, 16, {}, 0));

        if (writer == nullptr)
        {
            error = "Cannot create WAV writer";
            return false;
        }

        stream.release(); // writer теперь владеет потоком

        if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        {
            error = "Write failed";
            return false;
        }

        writer->flush();
        return true;
    }
};

} // namespace sonic