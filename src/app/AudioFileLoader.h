#pragma once
#include <JuceHeader.h>
#include "engine/AudioData.h"
#include <memory>
#include <vector>

namespace sonic {

// Единственное место, где JUCE касается файлов. engine остаётся переносимым.
class AudioFileLoader
{
public:
    AudioFileLoader() { formatManager_.registerBasicFormats(); }

    // Вызывать из message thread. nullptr = ошибка.
    std::unique_ptr<engine::AudioData> load (const juce::File& file, juce::String& errorMessage)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager_.createReaderFor (file));

        if (reader == nullptr)
        {
            errorMessage = "Unsupported format: " + file.getFileName();
            return nullptr;
        }

        if (reader->lengthInSamples <= 0 || reader->numChannels == 0)
        {
            errorMessage = "File is empty";
            return nullptr;
        }

        // Шаг 2 грузит файл целиком в RAM. Стриминг с диска будет позже.
        if (reader->lengthInSamples > 100000000)
        {
            errorMessage = "File too long for now (limit ~35 min)";
            return nullptr;
        }

        const int numChannels = (int) reader->numChannels;
        const int numFrames   = (int) reader->lengthInSamples;

        auto data = std::make_unique<engine::AudioData>();
        data->sampleRate  = reader->sampleRate;
        data->numChannels = numChannels;
        data->numFrames   = numFrames;
        data->channels.resize ((size_t) numChannels);

        for (auto& c : data->channels)
            c.resize ((size_t) numFrames);

        // zero-copy: AudioBuffer ссылается на нашу память, читатель пишет прямо в неё
        std::vector<float*> pointers ((size_t) numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
            pointers[(size_t) ch] = data->channels[(size_t) ch].data();

        juce::AudioBuffer<float> view (pointers.data(), numChannels, numFrames);

        if (! reader->read (&view, 0, numFrames, 0, true, true))
        {
            errorMessage = "Read failed: " + file.getFileName();
            return nullptr;
        }

        return data;
    }

private:
    juce::AudioFormatManager formatManager_;
};

} // namespace sonic