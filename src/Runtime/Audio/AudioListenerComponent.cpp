#include "Audio/AudioListenerComponent.h"

#include "Audio/AudioEngine.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

void AudioListenerComponent::OnLateUpdate(float)
{
    Actor* owner = GetOwner();
    if (!owner || !m_Primary || !owner->IsActive()) return;
    Scene* scene = owner->GetScene();
    if (scene) {
        bool earlierPrimary = false;
        scene->ForEach([&](Actor& actor) {
            if (earlierPrimary || actor.GetID() >= owner->GetID()) return;
            auto* listener = actor.GetComponent<AudioListenerComponent>();
            earlierPrimary = actor.IsActive() && listener && listener->IsEnabled() && listener->IsPrimary();
        });
        if (earlierPrimary) return;
    }
    const Mat4 world = owner->GetWorldMatrix();
    AudioEngine::Get().SetListenerTransform(owner->GetWorldPosition(),
        world.TransformDir(Vec3::Forward()).Normalized(),
        world.TransformDir(Vec3::Up()).Normalized());
}

void AudioListenerComponent::Serialize(nlohmann::json& data) const { data["primary"] = m_Primary; }
void AudioListenerComponent::Deserialize(const nlohmann::json& data) { m_Primary = data.value("primary", true); }
