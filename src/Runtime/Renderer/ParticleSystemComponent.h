#pragma once

#include "API/RuntimeApi.h"

#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/ParticleAsset.h"
#include "Scene/Component.h"

#include <random>
#include <vector>

class Camera;

class MYENGINE_RUNTIME_API ParticleSystemComponent final : public Component {
public:
    const char* GetTypeName() const override { return "ParticleSystem"; }
    void OnBeginPlay() override;
    void OnUpdate(float deltaSeconds) override;
    void OnEndPlay() override;
    void OnAnimationEvent(const AnimationEventData& event) override;
    bool Play();
    void Stop(bool clear = false);
    void Emit(uint32_t count);
    bool IsPlaying() const { return m_Playing; }
    size_t GetAliveCount() const { return m_Particles.size(); }
    ParticleEmitterSettings& GetSettings() { return m_Settings; }
    const ParticleEmitterSettings& GetSettings() const { return m_Settings; }
    MeshAsset* BuildBillboardMesh(const Camera& camera);
    MaterialAsset* GetMaterial() const { return m_Material.Get(); }
    void SetMaterial(MaterialHandle material) { m_Material = std::move(material); }
    void SetAssetPath(const std::string& path);
    const std::string& GetAssetPath() const { return m_AssetPath; }
    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    struct Particle {
        Vec3 position;
        Vec3 velocity;
        float age = 0.0f;
        float lifetime = 1.0f;
        float rotation = 0.0f;
    };
    void EnsureResources();
    ParticleEmitterSettings m_Settings;
    std::string m_AssetPath;
    std::vector<Particle> m_Particles;
    std::shared_ptr<MeshAsset> m_RenderMesh;
    MaterialHandle m_Material;
    std::mt19937 m_Random{0x4D59454Eu};
    float m_EmissionAccumulator = 0.0f;
    float m_EmitterAge = 0.0f;
    bool m_Playing = false;
};
