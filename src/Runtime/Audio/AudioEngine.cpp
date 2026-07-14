#include "Audio/AudioEngine.h"

#include "Audio/AudioClipAsset.h"
#include "Core/Logger.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>

const char* AudioBusName(AudioBus bus)
{
    switch (bus) {
    case AudioBus::Music: return "music";
    case AudioBus::Effects: return "effects";
    case AudioBus::Voice: return "voice";
    case AudioBus::UI: return "ui";
    case AudioBus::Count: break;
    }
    return "effects";
}

bool ParseAudioBus(const std::string& value, AudioBus& bus)
{
    for (uint8_t index = 0; index < static_cast<uint8_t>(AudioBus::Count); ++index) {
        const auto candidate = static_cast<AudioBus>(index);
        if (value == AudioBusName(candidate)) { bus = candidate; return true; }
    }
    return false;
}

struct AudioEngine::Impl {
    struct BusState { float volume = 1.0f; bool muted = false; bool pauseWithGame = true; };
    struct SoundEntry {
        std::unique_ptr<ma_sound> sound;
        float baseVolume = 1.0f;
        AudioBus bus = AudioBus::Effects;
        int priority = 0;
        std::string concurrencyGroup;
        uint64_t sequence = 0;
        bool pausedByGame = false;
    };
    ma_engine engine{};
    bool initialized = false;
    bool silent = true;
    bool paused = false;
    Vec3 listenerPosition = Vec3::Zero();
    Vec3 listenerForward = Vec3::Forward();
    Vec3 listenerUp = Vec3::Up();
    SoundID nextID = 1;
    uint64_t nextSequence = 1;
    float masterVolume = 1.0f;
    std::array<BusState, static_cast<size_t>(AudioBus::Count)> buses{};
    uint32_t maxVoices = 128;
    uint32_t stolenVoices = 0;
    uint32_t rejectedVoices = 0;
    std::unordered_map<SoundID, SoundEntry> sounds;
};

AudioEngine& AudioEngine::Get()
{
    static AudioEngine instance;
    return instance;
}

AudioEngine::AudioEngine() : m_Impl(std::make_unique<Impl>())
{
    m_Impl->buses[static_cast<size_t>(AudioBus::UI)].pauseWithGame = false;
}
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
    m_Impl->paused = false;
}

void AudioEngine::Update()
{
    if (!m_Impl->initialized) return;
    for (auto it = m_Impl->sounds.begin(); it != m_Impl->sounds.end();) {
        ma_sound* sound = it->second.sound.get();
        if (!it->second.pausedByGame && ma_sound_at_end(sound)) {
            ma_sound_uninit(sound);
            it = m_Impl->sounds.erase(it);
        } else {
            ++it;
        }
    }
}

bool AudioEngine::IsInitialized() const { return m_Impl->initialized; }
bool AudioEngine::IsSilent() const { return m_Impl->silent; }

float AudioEngine::CalculateDistanceAttenuation(float distance, float minDistance, float maxDistance)
{
    minDistance = (std::max)(0.01f, minDistance);
    maxDistance = (std::max)(minDistance, maxDistance);
    if (distance <= minDistance) return 1.0f;
    if (distance >= maxDistance || maxDistance <= minDistance) return 0.0f;
    return 1.0f - (distance - minDistance) / (maxDistance - minDistance);
}

AudioEngine::SoundID AudioEngine::Play(const AudioPlayDesc& desc)
{
    if (!m_Impl->initialized || !desc.clip || !desc.clip->IsReady()) return 0;
    const AudioBus resolvedBus = desc.bus == AudioBus::Count ? AudioBus::Effects : desc.bus;

    std::vector<AudioVoiceCandidate> active;
    active.reserve(m_Impl->sounds.size());
    for (const auto& pair : m_Impl->sounds)
        active.push_back({pair.first, pair.second.priority, pair.second.sequence,
                          pair.second.concurrencyGroup});
    const AudioVoiceAdmission admission = EvaluateVoiceAdmission(
        active, m_Impl->maxVoices, desc.concurrencyGroup, desc.maxInstances, desc.priority);
    if (!admission.accepted) {
        ++m_Impl->rejectedVoices;
        return 0;
    }
    if (admission.victimID != 0) {
        const auto victim = m_Impl->sounds.find(admission.victimID);
        ma_sound_stop(victim->second.sound.get());
        ma_sound_uninit(victim->second.sound.get());
        m_Impl->sounds.erase(victim);
        ++m_Impl->stolenVoices;
    }

    auto sound = std::make_unique<ma_sound>();
    ma_uint32 flags = desc.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    const ma_result result = ma_sound_init_from_file(
        &m_Impl->engine, desc.clip->GetPath().c_str(), flags, nullptr, nullptr, sound.get());
    if (result != MA_SUCCESS) {
        Logger::Warn("[Audio] Failed to create sound: ", desc.clip->GetPath());
        return 0;
    }

    ma_sound_set_looping(sound.get(), desc.loop ? MA_TRUE : MA_FALSE);
    const size_t busIndex = static_cast<size_t>(resolvedBus);
    const auto& bus = m_Impl->buses[busIndex];
    const float effectiveVolume = (std::max)(0.0f, desc.volume) * m_Impl->masterVolume *
        bus.volume * (bus.muted ? 0.0f : 1.0f);
    ma_sound_set_volume(sound.get(), effectiveVolume);
    ma_sound_set_pitch(sound.get(), (std::max)(0.01f, desc.pitch));
    ma_sound_set_spatialization_enabled(sound.get(), desc.spatial ? MA_TRUE : MA_FALSE);
    ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_linear);
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
    Impl::SoundEntry entry;
    entry.sound = std::move(sound);
    entry.baseVolume = (std::max)(0.0f, desc.volume);
    entry.bus = resolvedBus;
    entry.priority = desc.priority;
    entry.concurrencyGroup = desc.concurrencyGroup;
    entry.sequence = m_Impl->nextSequence++;
    entry.pausedByGame = m_Impl->paused && bus.pauseWithGame;
    if (entry.pausedByGame) ma_sound_stop(entry.sound.get());
    m_Impl->sounds.emplace(id, std::move(entry));
    return id;
}

void AudioEngine::Stop(SoundID id)
{
    const auto it = m_Impl->sounds.find(id);
    if (it == m_Impl->sounds.end()) return;
    ma_sound_stop(it->second.sound.get());
    ma_sound_uninit(it->second.sound.get());
    m_Impl->sounds.erase(it);
}

void AudioEngine::StopAll()
{
    for (auto& entry : m_Impl->sounds) {
        ma_sound_stop(entry.second.sound.get());
        ma_sound_uninit(entry.second.sound.get());
    }
    m_Impl->sounds.clear();
}

void AudioEngine::SetPaused(bool paused)
{
    if (m_Impl->paused == paused) return;
    m_Impl->paused = paused;
    for (auto& pair : m_Impl->sounds) {
        auto& entry = pair.second;
        if (!m_Impl->buses[static_cast<size_t>(entry.bus)].pauseWithGame) continue;
        if (paused) {
            if (ma_sound_is_playing(entry.sound.get())) {
                ma_sound_stop(entry.sound.get());
                entry.pausedByGame = true;
            }
        } else if (entry.pausedByGame) {
            if (!ma_sound_at_end(entry.sound.get())) ma_sound_start(entry.sound.get());
            entry.pausedByGame = false;
        }
    }
}

bool AudioEngine::IsPaused() const { return m_Impl->paused; }

void AudioEngine::SetMasterVolume(float volume)
{
    m_Impl->masterVolume = std::clamp(volume, 0.0f, 1.0f);
    for (auto& pair : m_Impl->sounds) {
        auto& entry = pair.second;
        const auto& bus = m_Impl->buses[static_cast<size_t>(entry.bus)];
        ma_sound_set_volume(entry.sound.get(), entry.baseVolume * m_Impl->masterVolume *
            bus.volume * (bus.muted ? 0.0f : 1.0f));
    }
}

float AudioEngine::GetMasterVolume() const { return m_Impl->masterVolume; }

void AudioEngine::SetBusVolume(AudioBus bus, float volume)
{
    if (bus == AudioBus::Count) return;
    auto& state = m_Impl->buses[static_cast<size_t>(bus)];
    state.volume = std::clamp(volume, 0.0f, 1.0f);
    for (auto& pair : m_Impl->sounds) {
        auto& entry = pair.second;
        if (entry.bus != bus) continue;
        ma_sound_set_volume(entry.sound.get(), entry.baseVolume * m_Impl->masterVolume *
            state.volume * (state.muted ? 0.0f : 1.0f));
    }
}

float AudioEngine::GetBusVolume(AudioBus bus) const
{
    return bus == AudioBus::Count ? 0.0f : m_Impl->buses[static_cast<size_t>(bus)].volume;
}

void AudioEngine::SetBusMuted(AudioBus bus, bool muted)
{
    if (bus == AudioBus::Count) return;
    auto& state = m_Impl->buses[static_cast<size_t>(bus)];
    state.muted = muted;
    SetBusVolume(bus, state.volume);
}

bool AudioEngine::IsBusMuted(AudioBus bus) const
{
    return bus != AudioBus::Count && m_Impl->buses[static_cast<size_t>(bus)].muted;
}

void AudioEngine::SetBusPauseWithGame(AudioBus bus, bool enabled)
{
    if (bus == AudioBus::Count) return;
    auto& state = m_Impl->buses[static_cast<size_t>(bus)];
    if (state.pauseWithGame == enabled) return;
    state.pauseWithGame = enabled;
    if (!m_Impl->paused) return;
    for (auto& pair : m_Impl->sounds) {
        auto& entry = pair.second;
        if (entry.bus != bus) continue;
        if (enabled && ma_sound_is_playing(entry.sound.get())) {
            ma_sound_stop(entry.sound.get());
            entry.pausedByGame = true;
        } else if (!enabled && entry.pausedByGame) {
            if (!ma_sound_at_end(entry.sound.get())) ma_sound_start(entry.sound.get());
            entry.pausedByGame = false;
        }
    }
}

bool AudioEngine::GetBusPauseWithGame(AudioBus bus) const
{
    return bus != AudioBus::Count &&
        m_Impl->buses[static_cast<size_t>(bus)].pauseWithGame;
}

void AudioEngine::SetMaxVoices(uint32_t maxVoices)
{
    m_Impl->maxVoices = std::clamp(maxVoices, 1u, 1024u);
    while (m_Impl->sounds.size() > m_Impl->maxVoices) {
        auto victim = m_Impl->sounds.end();
        for (auto it = m_Impl->sounds.begin(); it != m_Impl->sounds.end(); ++it) {
            if (victim == m_Impl->sounds.end() ||
                it->second.priority < victim->second.priority ||
                (it->second.priority == victim->second.priority &&
                 it->second.sequence < victim->second.sequence)) victim = it;
        }
        ma_sound_stop(victim->second.sound.get());
        ma_sound_uninit(victim->second.sound.get());
        m_Impl->sounds.erase(victim);
        ++m_Impl->stolenVoices;
    }
}

AudioDiagnostics AudioEngine::GetDiagnostics() const
{
    AudioDiagnostics diagnostics;
    diagnostics.maxVoices = m_Impl->maxVoices;
    diagnostics.stolenVoices = m_Impl->stolenVoices;
    diagnostics.rejectedVoices = m_Impl->rejectedVoices;
    diagnostics.activeVoices = static_cast<uint32_t>(m_Impl->sounds.size());
    for (const auto& pair : m_Impl->sounds) {
        const auto& entry = pair.second;
        ++diagnostics.voicesByBus[static_cast<size_t>(entry.bus)];
        if (entry.pausedByGame) ++diagnostics.pausedVoices;
    }
    return diagnostics;
}

AudioVoiceAdmission AudioEngine::EvaluateVoiceAdmission(
    const std::vector<AudioVoiceCandidate>& active, uint32_t maxVoices,
    const std::string& concurrencyGroup, uint32_t maxInstances, int newPriority)
{
    const AudioVoiceCandidate* victim = nullptr;
    auto prefer = [&victim](const AudioVoiceCandidate& candidate) {
        if (!victim || candidate.priority < victim->priority ||
            (candidate.priority == victim->priority && candidate.sequence < victim->sequence))
            victim = &candidate;
    };

    if (maxInstances > 0 && !concurrencyGroup.empty()) {
        uint32_t count = 0;
        for (const auto& candidate : active) {
            if (candidate.concurrencyGroup != concurrencyGroup) continue;
            ++count;
            prefer(candidate);
        }
        if (count < maxInstances) victim = nullptr;
    }
    if (!victim && active.size() >= (std::max)(1u, maxVoices))
        for (const auto& candidate : active) prefer(candidate);
    if (!victim) return {};
    if (victim->priority > newPriority) return {false, 0};
    return {true, victim->id};
}

bool AudioEngine::IsPlaying(SoundID id) const
{
    const auto it = m_Impl->sounds.find(id);
    return it != m_Impl->sounds.end() &&
        (it->second.pausedByGame || ma_sound_is_playing(it->second.sound.get()));
}

void AudioEngine::SetSoundPosition(SoundID id, const Vec3& position)
{
    const auto it = m_Impl->sounds.find(id);
    if (it != m_Impl->sounds.end())
        ma_sound_set_position(it->second.sound.get(), position.x, position.y, position.z);
}

void AudioEngine::SetSoundBus(SoundID id, AudioBus bus)
{
    if (bus == AudioBus::Count) return;
    const auto it = m_Impl->sounds.find(id);
    if (it == m_Impl->sounds.end()) return;
    auto& entry = it->second;
    const bool wasPausedByGame = entry.pausedByGame;
    entry.bus = bus;
    const auto& state = m_Impl->buses[static_cast<size_t>(bus)];
    ma_sound_set_volume(entry.sound.get(), entry.baseVolume * m_Impl->masterVolume *
        state.volume * (state.muted ? 0.0f : 1.0f));
    if (!m_Impl->paused) return;
    if (state.pauseWithGame && !wasPausedByGame && ma_sound_is_playing(entry.sound.get())) {
        ma_sound_stop(entry.sound.get());
        entry.pausedByGame = true;
    } else if (!state.pauseWithGame && wasPausedByGame) {
        if (!ma_sound_at_end(entry.sound.get())) ma_sound_start(entry.sound.get());
        entry.pausedByGame = false;
    }
}

void AudioEngine::SetSoundVolume(SoundID id, float volume)
{
    const auto it = m_Impl->sounds.find(id);
    if (it != m_Impl->sounds.end()) {
        it->second.baseVolume = (std::max)(0.0f, volume);
        const auto& bus = m_Impl->buses[static_cast<size_t>(it->second.bus)];
        ma_sound_set_volume(it->second.sound.get(), it->second.baseVolume *
            m_Impl->masterVolume * bus.volume * (bus.muted ? 0.0f : 1.0f));
    }
}

void AudioEngine::SetSoundPitch(SoundID id, float pitch)
{
    const auto it = m_Impl->sounds.find(id);
    if (it != m_Impl->sounds.end()) ma_sound_set_pitch(it->second.sound.get(), (std::max)(0.01f, pitch));
}

void AudioEngine::FadeSoundVolume(SoundID id, float targetVolume, uint32_t milliseconds)
{
    const auto it = m_Impl->sounds.find(id);
    if (it == m_Impl->sounds.end()) return;
    auto& entry = it->second;
    entry.baseVolume = (std::max)(0.0f, targetVolume);
    const auto& bus = m_Impl->buses[static_cast<size_t>(entry.bus)];
    const float effectiveTarget = entry.baseVolume * m_Impl->masterVolume * bus.volume *
        (bus.muted ? 0.0f : 1.0f);
    ma_sound_set_fade_in_milliseconds(entry.sound.get(), -1.0f, effectiveTarget, milliseconds);
}

void AudioEngine::SetListenerTransform(const Vec3& position, const Vec3& forward, const Vec3& up)
{
    m_Impl->listenerPosition = position;
    m_Impl->listenerForward = forward.LengthSq() > 1e-6f ? forward.Normalized() : Vec3::Forward();
    m_Impl->listenerUp = up.LengthSq() > 1e-6f ? up.Normalized() : Vec3::Up();
    if (!m_Impl->initialized) return;
    ma_engine_listener_set_position(&m_Impl->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_Impl->engine, 0, m_Impl->listenerForward.x, m_Impl->listenerForward.y, m_Impl->listenerForward.z);
    ma_engine_listener_set_world_up(&m_Impl->engine, 0, m_Impl->listenerUp.x, m_Impl->listenerUp.y, m_Impl->listenerUp.z);
}

const Vec3& AudioEngine::GetListenerPosition() const { return m_Impl->listenerPosition; }
const Vec3& AudioEngine::GetListenerForward() const { return m_Impl->listenerForward; }
const Vec3& AudioEngine::GetListenerUp() const { return m_Impl->listenerUp; }
