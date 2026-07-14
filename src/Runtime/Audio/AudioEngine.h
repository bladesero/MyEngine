#pragma once

#include "Core/EngineMath.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class AudioClipAsset;

enum class AudioBus : uint8_t { Music, Effects, Voice, UI, Count };

const char* AudioBusName(AudioBus bus);
bool ParseAudioBus(const std::string& value, AudioBus& bus);

struct AudioDiagnostics {
    uint32_t activeVoices = 0;
    uint32_t pausedVoices = 0;
    uint32_t stolenVoices = 0;
    uint32_t rejectedVoices = 0;
    uint32_t maxVoices = 128;
    uint32_t voicesByBus[static_cast<size_t>(AudioBus::Count)]{};
};

struct AudioVoiceCandidate {
    uint64_t id = 0;
    int priority = 0;
    uint64_t sequence = 0;
    std::string concurrencyGroup;
};

struct AudioVoiceAdmission {
    bool accepted = true;
    uint64_t victimID = 0;
};

struct AudioPlayDesc {
    const AudioClipAsset* clip = nullptr;
    bool loop = false;
    bool spatial = true;
    float volume = 1.0f;
    float pitch = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 100.0f;
    Vec3 position{0.0f, 0.0f, 0.0f};
    AudioBus bus = AudioBus::Effects;
    int priority = 0;
    std::string concurrencyGroup;
    uint32_t maxInstances = 0;
    bool stream = false;
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
    void SetPaused(bool paused);
    bool IsPaused() const;
    void SetMasterVolume(float volume);
    float GetMasterVolume() const;
    void SetBusVolume(AudioBus bus, float volume);
    float GetBusVolume(AudioBus bus) const;
    void SetBusMuted(AudioBus bus, bool muted);
    bool IsBusMuted(AudioBus bus) const;
    void SetBusPauseWithGame(AudioBus bus, bool enabled);
    bool GetBusPauseWithGame(AudioBus bus) const;
    void SetMaxVoices(uint32_t maxVoices);
    AudioDiagnostics GetDiagnostics() const;
    static AudioVoiceAdmission EvaluateVoiceAdmission(
        const std::vector<AudioVoiceCandidate>& active, uint32_t maxVoices,
        const std::string& concurrencyGroup, uint32_t maxInstances,
        int newPriority);
    bool IsPlaying(SoundID id) const;
    void SetSoundPosition(SoundID id, const Vec3& position);
    void SetSoundBus(SoundID id, AudioBus bus);
    void SetSoundVolume(SoundID id, float volume);
    void FadeSoundVolume(SoundID id, float targetVolume, uint32_t milliseconds);
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
