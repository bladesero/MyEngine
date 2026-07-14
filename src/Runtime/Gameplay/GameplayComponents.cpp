#include "Gameplay/GameplayComponents.h"
#include "Core/RuntimeAccessibility.h"

#include "Physics/PhysicsWorld.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Renderer/PostProcessComponent.h"

#include <algorithm>

namespace {
nlohmann::json VecToJson(const Vec3& value) {
    return nlohmann::json::array({value.x, value.y, value.z});
}
Vec3 JsonToVec(const nlohmann::json& value, const Vec3& fallback) {
    return value.is_array() && value.size() == 3
               ? Vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>())
               : fallback;
}
} // namespace

void HealthComponent::OnBeginPlay() {
    if (m_ResetOnPlay)
        m_Health = m_MaxHealth;
}
void HealthComponent::SetMaxHealth(float value, bool preserveRatio) {
    const float ratio = m_MaxHealth > 0.0f ? m_Health / m_MaxHealth : 1.0f;
    m_MaxHealth = std::max(0.01f, value);
    m_Health = std::clamp(preserveRatio ? m_MaxHealth * ratio : m_Health, 0.0f, m_MaxHealth);
}
void HealthComponent::SetHealth(float value) {
    m_Health = std::clamp(value, 0.0f, m_MaxHealth);
}
bool HealthComponent::ApplyDamage(const DamageEvent& event) {
    if (event.amount <= 0.0f || IsDead())
        return false;
    m_LastDamage = event;
    SetHealth(m_Health - event.amount);
    return true;
}
bool HealthComponent::Heal(float amount) {
    if (amount <= 0.0f || IsDead())
        return false;
    SetHealth(m_Health + amount);
    return true;
}
void HealthComponent::Serialize(nlohmann::json& data) const {
    data = {{"maxHealth", m_MaxHealth}, {"health", m_Health}, {"resetOnPlay", m_ResetOnPlay}};
}
void HealthComponent::Deserialize(const nlohmann::json& data) {
    SetMaxHealth(data.value("maxHealth", 100.0f));
    SetHealth(data.value("health", m_MaxHealth));
    m_ResetOnPlay = data.value("resetOnPlay", true);
}

void HurtboxComponent::SetDamageMultiplier(float value) {
    m_DamageMultiplier = std::max(0.0f, value);
}
void HurtboxComponent::Serialize(nlohmann::json& data) const {
    data = {{"team", m_Team}, {"damageMultiplier", m_DamageMultiplier}};
}
void HurtboxComponent::Deserialize(const nlohmann::json& data) {
    m_Team = data.value("team", 0u);
    SetDamageMultiplier(data.value("damageMultiplier", 1.0f));
}

void HitboxComponent::BeginAttack(float damage) {
    if (damage >= 0.0f)
        SetDamage(damage);
    m_HitActors.clear();
    m_AttackActive = true;
}
void HitboxComponent::EndAttack() {
    m_AttackActive = false;
    m_HitActors.clear();
}
void HitboxComponent::OnAnimationEvent(const AnimationEventData& event) {
    if (event.name == "Hitbox.Begin") {
        float damage = -1.0f;
        try {
            if (!event.payload.empty())
                damage = std::stof(event.payload);
        } catch (...) {}
        BeginAttack(damage);
    } else if (event.name == "Hitbox.End")
        EndAttack();
}
void HitboxComponent::SetDamage(float value) {
    m_Damage = std::max(0.0f, value);
}
void HitboxComponent::SetRadius(float value) {
    m_Radius = std::max(0.01f, value);
}
void HitboxComponent::OnFixedUpdate(float) {
    Actor* owner = GetOwner();
    Scene* scene = owner ? owner->GetScene() : nullptr;
    if (!m_AttackActive || !scene || !owner)
        return;
    std::vector<ActorHandle> overlaps;
    const Vec3 center = owner->GetWorldMatrix().TransformPoint(m_Offset);
    scene->GetPhysicsWorld().OverlapSphere(*scene, center, m_Radius, m_LayerMask, overlaps);
    for (ActorHandle handle : overlaps) {
        Actor* target = scene->TryGetActor(handle);
        if (!target || target == owner || m_HitActors.count(handle.ToUInt64()))
            continue;
        auto* hurtbox = target->GetComponent<HurtboxComponent>();
        auto* health = target->GetComponent<HealthComponent>();
        if (!hurtbox || !health || hurtbox->GetTeam() == m_Team)
            continue;
        DamageEvent event{owner->GetHandle(), handle, m_Damage * hurtbox->GetDamageMultiplier(), center,
                          (target->GetWorldPosition() - owner->GetWorldPosition()).Normalized()};
        if (health->ApplyDamage(event))
            m_HitActors.insert(handle.ToUInt64());
    }
}
void HitboxComponent::Serialize(nlohmann::json& data) const {
    data = {{"damage", m_Damage},
            {"radius", m_Radius},
            {"offset", VecToJson(m_Offset)},
            {"team", m_Team},
            {"layerMask", m_LayerMask}};
}
void HitboxComponent::Deserialize(const nlohmann::json& data) {
    SetDamage(data.value("damage", 10.0f));
    SetRadius(data.value("radius", 0.75f));
    if (data.contains("offset"))
        m_Offset = JsonToVec(data["offset"], m_Offset);
    m_Team = data.value("team", 0u);
    m_LayerMask = data.value("layerMask", 0xffffffffu);
    EndAttack();
}

void InteractionComponent::SetRange(float value) {
    m_Range = std::max(0.0f, value);
}
bool InteractionComponent::Interact(ActorHandle instigator) {
    Actor* owner = GetOwner();
    Scene* scene = owner ? owner->GetScene() : nullptr;
    Actor* actor = scene ? scene->TryGetActor(instigator) : nullptr;
    if (!CanInteract() || !owner || !actor ||
        (owner->GetWorldPosition() - actor->GetWorldPosition()).Length() > m_Range)
        return false;
    m_LastInstigator = instigator;
    if (m_SingleUse)
        m_Consumed = true;
    return true;
}
void InteractionComponent::Serialize(nlohmann::json& data) const {
    data = {{"kind", static_cast<int>(m_Kind)}, {"prompt", m_Prompt},    {"range", m_Range}, {"singleUse", m_SingleUse},
            {"destroyOnUse", m_DestroyOnUse},   {"consumed", m_Consumed}};
}
void InteractionComponent::Deserialize(const nlohmann::json& data) {
    m_Kind = static_cast<InteractionKind>(std::clamp(data.value("kind", 0), 0, 4));
    m_Prompt = data.value("prompt", std::string("Interact"));
    SetRange(data.value("range", 2.0f));
    m_SingleUse = data.value("singleUse", false);
    m_DestroyOnUse = data.value("destroyOnUse", false);
    m_Consumed = data.value("consumed", false);
}

void GameplayFeedbackComponent::Shake(float amplitude, float duration) {
    m_ShakeAmplitude = std::max(m_ShakeAmplitude, std::max(0.0f, amplitude));
    m_ShakeRemaining = std::max(m_ShakeRemaining, std::max(0.0f, duration));
}
void GameplayFeedbackComponent::Flash(float intensity, float duration) {
    m_FlashIntensity = std::max(m_FlashIntensity, std::clamp(intensity, 0.0f, 1.0f));
    m_FlashRemaining = std::max(m_FlashRemaining, std::max(0.0f, duration));
}
void GameplayFeedbackComponent::SlowMotion(float scale, float duration) {
    if (Actor* owner = GetOwner())
        if (Scene* scene = owner->GetScene()) {
            scene->SetTimeScale(std::clamp(scale, 0.01f, 1.0f));
            m_SlowRemaining = std::max(m_SlowRemaining, std::max(0.0f, duration));
        }
}
void GameplayFeedbackComponent::OnLateUpdate(float deltaSeconds) {
    Actor* owner = GetOwner();
    if (!owner)
        return;
    Scene* scene = owner->GetScene();
    const float scale = scene ? std::max(0.01f, scene->GetTimeScale()) : 1.0f;
    const float unscaled = deltaSeconds / scale;
    owner->GetTransform().position -= m_LastShakeOffset;
    m_LastShakeOffset = Vec3::Zero();
    if (m_ShakeRemaining > 0.0f) {
        auto noise = [&]() {
            m_NoiseState = m_NoiseState * 1664525u + 1013904223u;
            return (static_cast<float>((m_NoiseState >> 8) & 0xffffu) / 32767.5f) - 1.0f;
        };
        const float strength =
            m_ShakeAmplitude * RuntimeAccessibility::GetCameraShakeScale() * std::min(1.0f, m_ShakeRemaining * 10.0f);
        m_LastShakeOffset = {noise() * strength, noise() * strength, noise() * strength};
        owner->GetTransform().position += m_LastShakeOffset;
        m_ShakeRemaining = std::max(0.0f, m_ShakeRemaining - unscaled);
    }
    if (auto* post = owner->GetComponent<PostProcessComponent>()) {
        if (m_FlashRemaining > 0.0f) {
            if (!m_FlashCaptured) {
                m_OriginalVignette = post->GetVignette();
                m_OriginalSaturation = post->GetSaturation();
                m_FlashCaptured = true;
            }
            const float amount = m_FlashIntensity * std::min(1.0f, m_FlashRemaining * 12.0f);
            post->SetVignette(std::max(m_OriginalVignette, amount));
            post->SetSaturation(std::max(0.0f, m_OriginalSaturation - amount * 0.8f));
            m_FlashRemaining = std::max(0.0f, m_FlashRemaining - unscaled);
        } else if (m_FlashCaptured) {
            post->SetVignette(m_OriginalVignette);
            post->SetSaturation(m_OriginalSaturation);
            m_FlashCaptured = false;
        }
    }
    if (m_SlowRemaining > 0.0f) {
        m_SlowRemaining = std::max(0.0f, m_SlowRemaining - unscaled);
        if (m_SlowRemaining <= 0.0f && scene)
            scene->SetTimeScale(1.0f);
    }
}
void GameplayFeedbackComponent::OnEndPlay() {
    if (Actor* owner = GetOwner()) {
        owner->GetTransform().position -= m_LastShakeOffset;
        if (Scene* scene = owner->GetScene())
            scene->SetTimeScale(1.0f);
        if (auto* post = owner->GetComponent<PostProcessComponent>())
            if (m_FlashCaptured) {
                post->SetVignette(m_OriginalVignette);
                post->SetSaturation(m_OriginalSaturation);
            }
    }
    m_LastShakeOffset = Vec3::Zero();
    m_ShakeRemaining = m_FlashRemaining = m_SlowRemaining = 0.0f;
    m_FlashCaptured = false;
}
void GameplayFeedbackComponent::Serialize(nlohmann::json& data) const {
    data = nlohmann::json::object();
}
void GameplayFeedbackComponent::Deserialize(const nlohmann::json&) {
    OnEndPlay();
}
