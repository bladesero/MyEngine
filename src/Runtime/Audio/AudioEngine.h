#pragma once

#include "Core/EngineMath.h"

#include <cstdint>
#include <memory>
#include <string>

class AudioClipAsset;

struct AudioPlayDesc {
    const AudioClipAsset* clip = nullptr;
    bool loop = false;
    bool spatial = true;
    float volume = 1.0f;
    float pitch = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 100.0f;
    Vec3 position{0.0f, 0.0f, 0.0f};
};

class AudioEngine {
public:
    using SoundID = uint64_t;

    static AudioEngine& Get();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool Init();
    void Shutdown();
    void Update();

    bool IsInitialized() const;
    bool IsSilent() const;
    static float CalculateDistanceAttenuation(float distance, float minDistance, float maxDistance);

    SoundID Play(const AudioPlayDesc& desc);
    void Stop(SoundID id);
    void StopAll();
    bool IsPlaying(SoundID id) const;
    void SetSoundPosition(SoundID id, const Vec3& position);
    void SetSoundVolume(SoundID id, float volume);
    void SetSoundPitch(SoundID id, float pitch);
    void SetListenerTransform(const Vec3& position, const Vec3& forward, const Vec3& up);
    const Vec3& GetListenerPosition() const;
    const Vec3& GetListenerForward() const;
    const Vec3& GetListenerUp() const;

private:
    AudioEngine();
    ~AudioEngine();

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
