#include "Audio/AudioSourceComponent.h"

#include "Assets/AssetManager.h"
#include "Scene/Actor.h"

#include <algorithm>

void AudioSourceComponent::SetClip(AudioClipHandle clip)
{
    m_Clip = std::move(clip);
    m_ClipPath = m_Clip.Get()
        ? AssetManager::Get().MakeProjectRelativePath(m_Clip->GetPath())
        : std::string{};
}

void AudioSourceComponent::SetClipPath(const std::string& path)
{
    m_ClipPath = path;
    m_Clip = path.empty() ? AudioClipHandle{} : AssetManager::Get().Load<AudioClipAsset>(path);
}

void AudioSourceComponent::SetVolume(float value)
{
    m_Volume = std::max(0.0f, value);
    if (m_SoundID) AudioEngine::Get().SetSoundVolume(m_SoundID, m_Volume);
}

void AudioSourceComponent::SetPitch(float value)
{
    m_Pitch = std::max(0.01f, value);
    if (m_SoundID) AudioEngine::Get().SetSoundPitch(m_SoundID, m_Pitch);
}

void AudioSourceComponent::FadeVolume(float value, uint32_t milliseconds)
{
    m_Volume = std::max(0.0f, value);
    if (m_SoundID) AudioEngine::Get().FadeSoundVolume(m_SoundID, m_Volume, milliseconds);
}

void AudioSourceComponent::SetMinDistance(float value)
{
    m_MinDistance = std::max(0.01f, value);
    m_MaxDistance = std::max(m_MinDistance, m_MaxDistance);
}

void AudioSourceComponent::SetMaxDistance(float value)
{
    m_MaxDistance = std::max(m_MinDistance, value);
}

void AudioSourceComponent::SetBus(AudioBus value)
{
    m_Bus = value == AudioBus::Count ? AudioBus::Effects : value;
    if (m_SoundID) AudioEngine::Get().SetSoundBus(m_SoundID, m_Bus);
}

void AudioSourceComponent::SetPriority(int value)
{
    m_Priority = std::clamp(value, -100, 100);
}

void AudioSourceComponent::SetMaxInstances(uint32_t value)
{
    m_MaxInstances = std::min(value, 1024u);
}

bool AudioSourceComponent::IsPlaying() const
{
    return m_SoundID != 0 && AudioEngine::Get().IsPlaying(m_SoundID);
}

bool AudioSourceComponent::Play()
{
    Stop();
    if (!m_Clip.IsValid() && !m_ClipPath.empty())
        m_Clip = AssetManager::Get().Load<AudioClipAsset>(m_ClipPath);
    if (!m_Clip.IsValid()) return false;

    AudioPlayDesc desc;
    desc.clip = m_Clip.Get();
    desc.loop = m_Loop;
    desc.spatial = m_Spatial;
    desc.volume = m_Volume;
    desc.pitch = m_Pitch;
    desc.minDistance = m_MinDistance;
    desc.maxDistance = m_MaxDistance;
    desc.position = GetWorldPosition();
    desc.bus = m_Bus;
    desc.priority = m_Priority;
    desc.concurrencyGroup = m_ConcurrencyGroup;
    desc.maxInstances = m_MaxInstances;
    desc.stream = m_Streaming;
    m_SoundID = AudioEngine::Get().Play(desc);
    return m_SoundID != 0;
}

void AudioSourceComponent::Stop()
{
    if (m_SoundID) {
        AudioEngine::Get().Stop(m_SoundID);
        m_SoundID = 0;
    }
}

void AudioSourceComponent::OnBeginPlay()
{
    if (m_PlayOnStart) Play();
}

void AudioSourceComponent::OnUpdate(float deltaSeconds)
{
    (void)deltaSeconds;
    if (m_SoundID && !AudioEngine::Get().IsPlaying(m_SoundID)) {
        m_SoundID = 0;
        return;
    }
    if (m_SoundID && m_Spatial) {
        AudioEngine::Get().SetSoundPosition(m_SoundID, GetWorldPosition());
    }
}

void AudioSourceComponent::OnDisable() { Stop(); }
void AudioSourceComponent::OnEndPlay() { Stop(); }

void AudioSourceComponent::OnAnimationEvent(const AnimationEventData& event)
{
    if (event.name == "Audio.Play") Play();
    else if (event.name == "Audio.Stop") Stop();
}
void AudioSourceComponent::OnDetach() { Stop(); }

void AudioSourceComponent::Serialize(nlohmann::json& data) const
{
    Component::Serialize(data);
    if (!m_ClipPath.empty()) data["clip"] = m_ClipPath;
    else if (m_Clip.Get()) data["clip"] = AssetManager::Get().MakeProjectRelativePath(m_Clip->GetPath());
    data["playOnStart"] = m_PlayOnStart;
    data["loop"] = m_Loop;
    data["volume"] = m_Volume;
    data["pitch"] = m_Pitch;
    data["spatial"] = m_Spatial;
    data["minDistance"] = m_MinDistance;
    data["maxDistance"] = m_MaxDistance;
    data["bus"] = AudioBusName(m_Bus);
    data["priority"] = m_Priority;
    data["concurrencyGroup"] = m_ConcurrencyGroup;
    data["maxInstances"] = m_MaxInstances;
    data["streaming"] = m_Streaming;
}

void AudioSourceComponent::Deserialize(const nlohmann::json& data)
{
    Component::Deserialize(data);
    if (auto it = data.find("clip"); it != data.end() && it->is_string()) {
        SetClipPath(it->get<std::string>());
    }
    m_PlayOnStart = data.value("playOnStart", m_PlayOnStart);
    m_Loop = data.value("loop", m_Loop);
    m_Volume = std::max(0.0f, data.value("volume", m_Volume));
    m_Pitch = std::max(0.01f, data.value("pitch", m_Pitch));
    m_Spatial = data.value("spatial", m_Spatial);
    m_MinDistance = std::max(0.01f, data.value("minDistance", m_MinDistance));
    m_MaxDistance = std::max(m_MinDistance, data.value("maxDistance", m_MaxDistance));
    if (const auto it = data.find("bus"); it != data.end() && it->is_string()) {
        AudioBus parsed = AudioBus::Effects;
        if (ParseAudioBus(it->get<std::string>(), parsed)) m_Bus = parsed;
    }
    m_Priority = std::clamp(data.value("priority", m_Priority), -100, 100);
    m_ConcurrencyGroup = data.value("concurrencyGroup", m_ConcurrencyGroup);
    const int64_t maxInstances = data.value("maxInstances", static_cast<int64_t>(m_MaxInstances));
    m_MaxInstances = static_cast<uint32_t>(std::clamp<int64_t>(maxInstances, 0, 1024));
    m_Streaming = data.value("streaming", m_Streaming);
}

Vec3 AudioSourceComponent::GetWorldPosition() const
{
    const Actor* owner = GetOwner();
    return owner ? owner->GetWorldPosition() : Vec3{0.0f, 0.0f, 0.0f};
}
