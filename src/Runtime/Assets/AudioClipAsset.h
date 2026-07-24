#pragma once

// Audio asset data belongs to Assets; Runtime/Audio owns device playback.

#include "Assets/Asset.h"

#include <cstdint>
#include <memory>
#include <string>

class AudioClipAsset final : public Asset {
public:
    explicit AudioClipAsset(const std::string& path) : Asset(AssetType::AudioClip, path) {}

    uint32_t GetChannels() const { return m_Channels; }
    uint32_t GetSampleRate() const { return m_SampleRate; }
    uint64_t GetFrameCount() const { return m_FrameCount; }
    float GetDurationSeconds() const { return m_DurationSeconds; }

    void SetMetadata(uint32_t channels, uint32_t sampleRate, uint64_t frameCount);
    void MarkReady() { SetState(AssetState::Ready); }
    bool ReloadFrom(const Asset& source) override;

private:
    uint32_t m_Channels = 0;
    uint32_t m_SampleRate = 0;
    uint64_t m_FrameCount = 0;
    float m_DurationSeconds = 0.0f;
};

using AudioClipHandle = AssetHandle<AudioClipAsset>;

std::shared_ptr<AudioClipAsset> LoadAudioClipAssetFromFile(const std::string& path);
