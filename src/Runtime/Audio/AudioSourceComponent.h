#pragma once

#include "Audio/AudioClipAsset.h"
#include "Audio/AudioEngine.h"
#include "Scene/Component.h"

class AudioSourceComponent final : public Component {
public:
    const char* GetTypeName() const override { return "AudioSource"; }

    AudioClipHandle GetClip() const { return m_Clip; }
    const std::string& GetClipPath() const { return m_ClipPath; }
    void SetClip(AudioClipHandle clip);
    void SetClipPath(const std::string& path);

    bool GetPlayOnStart() const { return m_PlayOnStart; }
    void SetPlayOnStart(bool value) { m_PlayOnStart = value; }
    bool GetLoop() const { return m_Loop; }
    void SetLoop(bool value) { m_Loop = value; }
    bool GetSpatial() const { return m_Spatial; }
    void SetSpatial(bool value) { m_Spatial = value; }
    float GetVolume() const { return m_Volume; }
    void SetVolume(float value);
    float GetPitch() const { return m_Pitch; }
    void SetPitch(float value);
    float GetMinDistance() const { return m_MinDistance; }
    void SetMinDistance(float value);
    float GetMaxDistance() const { return m_MaxDistance; }
    void SetMaxDistance(float value);

    bool IsPlaying() const;
    bool Play();
    void Stop();

    void OnBeginPlay() override;
    void OnUpdate(float deltaSeconds) override;
    void OnDisable() override;
    void OnEndPlay() override;
    void OnDetach() override;
    void OnAnimationEvent(const AnimationEventData& event) override;

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    Vec3 GetWorldPosition() const;

    AudioClipHandle m_Clip;
    std::string m_ClipPath;
    bool m_PlayOnStart = true;
    bool m_Loop = false;
    bool m_Spatial = true;
    float m_Volume = 1.0f;
    float m_Pitch = 1.0f;
    float m_MinDistance = 1.0f;
    float m_MaxDistance = 100.0f;
    AudioEngine::SoundID m_SoundID = 0;
};
