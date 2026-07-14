#pragma once
#include "Assets/Asset.h"
#include "Core/EngineMath.h"
#include <nlohmann/json.hpp>
struct ParticleEmitterSettings {
    uint32_t maxParticles = 256;
    float rate = 20.0f;
    uint32_t burst = 0;
    float lifetime = 1.5f;
    float startSpeed = 1.0f;
    float startSize = 0.2f;
    float endSize = 0.0f;
    Vec3 startColor = Vec3::One();
    Vec3 endColor = Vec3::One();
    float startAlpha = 1.0f;
    float endAlpha = 0.0f;
    bool loop = true;
    bool playOnStart = true;
};
class ParticleAsset final : public Asset {
public:
    explicit ParticleAsset(const std::string& path) : Asset(AssetType::Particle, path) {}
    ParticleEmitterSettings& GetSettings() { return m_Settings; }
    const ParticleEmitterSettings& GetSettings() const { return m_Settings; }
    void MarkReady() { SetState(AssetState::Ready); }

private:
    ParticleEmitterSettings m_Settings;
};
using ParticleAssetHandle = AssetHandle<ParticleAsset>;
std::shared_ptr<ParticleAsset> LoadParticleAssetFromFile(const std::string& path);
bool SaveParticleAssetToFile(const ParticleAsset& asset, const std::string& path);
