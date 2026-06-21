#include "Audio/AudioClipAsset.h"

#include "Core/Logger.h"

#include <miniaudio.h>

void AudioClipAsset::SetMetadata(uint32_t channels, uint32_t sampleRate,
                                 uint64_t frameCount)
{
    m_Channels = channels;
    m_SampleRate = sampleRate;
    m_FrameCount = frameCount;
    m_DurationSeconds = sampleRate == 0
        ? 0.0f
        : static_cast<float>(static_cast<double>(frameCount) /
                             static_cast<double>(sampleRate));
    MarkReady();
}

bool AudioClipAsset::ReloadFrom(const Asset& source)
{
    if (source.GetType() != AssetType::AudioClip) return false;
    const auto& clip = static_cast<const AudioClipAsset&>(source);
    m_Channels = clip.m_Channels;
    m_SampleRate = clip.m_SampleRate;
    m_FrameCount = clip.m_FrameCount;
    m_DurationSeconds = clip.m_DurationSeconds;
    MarkReady();
    return true;
}

std::shared_ptr<AudioClipAsset> LoadAudioClipAssetFromFile(const std::string& path)
{
    ma_decoder decoder;
    const ma_result result = ma_decoder_init_file(path.c_str(), nullptr, &decoder);
    if (result != MA_SUCCESS) {
        Logger::Error("[Audio] Failed to decode audio clip: ", path);
        return {};
    }

    ma_uint64 frameCount = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount) != MA_SUCCESS) {
        frameCount = 0;
    }

    auto clip = std::make_shared<AudioClipAsset>(path);
    clip->SetMetadata(decoder.outputChannels, decoder.outputSampleRate, frameCount);
    ma_decoder_uninit(&decoder);

    Logger::Info("[Audio] Loaded clip '", clip->GetName(), "' channels=",
                 clip->GetChannels(), " rate=", clip->GetSampleRate(),
                 " duration=", clip->GetDurationSeconds());
    return clip;
}
