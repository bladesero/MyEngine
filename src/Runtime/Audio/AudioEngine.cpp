#include "Audio/AudioEngine.h"

#include "Audio/AudioClipAsset.h"
#include "Core/Logger.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <memory>
#include <unordered_map>

struct AudioEngine::Impl {
    ma_engine engine{};
    bool initialized = false;
    bool silent = true;
    SoundID nextID = 1;
    std::unordered_map<SoundID, std::unique_ptr<ma_sound>> sounds;
};

AudioEngine& AudioEngine::Get()
{
    static AudioEngine instance;
    return instance;
}

AudioEngine::AudioEngine() : m_Impl(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { Shutdown(); }

bool AudioEngine::Init()
{
    if (m_Impl->initialized) return true;
    const ma_result result = ma_engine_init(nullptr, &m_Impl->engine);
    if (result != MA_SUCCESS) {
        m_Impl->initialized = false;
        m_Impl->silent = true;
        Logger::Warn("[Audio] Device initialization failed; running in silent mode");
        return false;
    }
    m_Impl->initialized = true;
    m_Impl->silent = false;
    Logger::Info("[Audio] Initialized miniaudio engine");
    return true;
}

void AudioEngine::Shutdown()
{
    StopAll();
    if (m_Impl->initialized) {
        ma_engine_uninit(&m_Impl->engine);
        m_Impl->initialized = false;
    }
    m_Impl->silent = true;
}

void AudioEngine::Update()
{
    if (!m_Impl->initialized) return;
    for (auto it = m_Impl->sounds.begin(); it != m_Impl->sounds.end();) {
        ma_sound* sound = it->second.get();
        if (ma_sound_at_end(sound)) {
            ma_sound_uninit(sound);
            it = m_Impl->sounds.erase(it);
        } else {
            ++it;
        }
    }
}

bool AudioEngine::IsInitialized() const { return m_Impl->initialized; }
bool AudioEngine::IsSilent() const { return m_Impl->silent; }

AudioEngine::SoundID AudioEngine::Play(const AudioPlayDesc& desc)
{
    if (!m_Impl->initialized || !desc.clip || !desc.clip->IsReady()) return 0;

    auto sound = std::make_unique<ma_sound>();
    ma_uint32 flags = MA_SOUND_FLAG_DECODE;
    const ma_result result = ma_sound_init_from_file(
        &m_Impl->engine, desc.clip->GetPath().c_str(), flags, nullptr, nullptr, sound.get());
    if (result != MA_SUCCESS) {
        Logger::Warn("[Audio] Failed to create sound: ", desc.clip->GetPath());
        return 0;
    }

    ma_sound_set_looping(sound.get(), desc.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(sound.get(), (std::max)(0.0f, desc.volume));
    ma_sound_set_pitch(sound.get(), (std::max)(0.01f, desc.pitch));
    ma_sound_set_spatialization_enabled(sound.get(), desc.spatial ? MA_TRUE : MA_FALSE);
    ma_sound_set_position(sound.get(), desc.position.x, desc.position.y, desc.position.z);
    ma_sound_set_min_distance(sound.get(), (std::max)(0.01f, desc.minDistance));
    ma_sound_set_max_distance(sound.get(), (std::max)(desc.minDistance, desc.maxDistance));

    if (ma_sound_start(sound.get()) != MA_SUCCESS) {
        ma_sound_uninit(sound.get());
        Logger::Warn("[Audio] Failed to start sound: ", desc.clip->GetPath());
        return 0;
    }

    const SoundID id = m_Impl->nextID++;
    if (m_Impl->nextID == 0) m_Impl->nextID = 1;
    m_Impl->sounds.emplace(id, std::move(sound));
    return id;
}

void AudioEngine::Stop(SoundID id)
{
    const auto it = m_Impl->sounds.find(id);
    if (it == m_Impl->sounds.end()) return;
    ma_sound_stop(it->second.get());
    ma_sound_uninit(it->second.get());
    m_Impl->sounds.erase(it);
}

void AudioEngine::StopAll()
{
    for (auto& entry : m_Impl->sounds) {
        ma_sound_stop(entry.second.get());
        ma_sound_uninit(entry.second.get());
    }
    m_Impl->sounds.clear();
}

bool AudioEngine::IsPlaying(SoundID id) const
{
    const auto it = m_Impl->sounds.find(id);
    return it != m_Impl->sounds.end() && ma_sound_is_playing(it->second.get());
}

void AudioEngine::SetSoundPosition(SoundID id, const Vec3& position)
{
    const auto it = m_Impl->sounds.find(id);
    if (it != m_Impl->sounds.end())
        ma_sound_set_position(it->second.get(), position.x, position.y, position.z);
}

void AudioEngine::SetSoundVolume(SoundID id, float volume)
{
    const auto it = m_Impl->sounds.find(id);
    if (it != m_Impl->sounds.end()) ma_sound_set_volume(it->second.get(), (std::max)(0.0f, volume));
}

void AudioEngine::SetSoundPitch(SoundID id, float pitch)
{
    const auto it = m_Impl->sounds.find(id);
    if (it != m_Impl->sounds.end()) ma_sound_set_pitch(it->second.get(), (std::max)(0.01f, pitch));
}

void AudioEngine::SetListenerTransform(const Vec3& position, const Vec3& forward, const Vec3& up)
{
    if (!m_Impl->initialized) return;
    ma_engine_listener_set_position(&m_Impl->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_Impl->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_Impl->engine, 0, up.x, up.y, up.z);
}
