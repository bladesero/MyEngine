#pragma once

#include "Scene/ActorHandle.h"
#include "Scene/Component.h"

#include <string>
#include <unordered_set>

struct DamageEvent {
    ActorHandle source;
    ActorHandle target;
    float amount = 0.0f;
    Vec3 point = Vec3::Zero();
    Vec3 direction = Vec3::Zero();
};

class HealthComponent final : public Component {
public:
    const char* GetTypeName() const override { return "Health"; }
    void OnBeginPlay() override;
    void SetMaxHealth(float value, bool preserveRatio = false);
    float GetMaxHealth() const { return m_MaxHealth; }
    void SetHealth(float value);
    float GetHealth() const { return m_Health; }
    bool ApplyDamage(const DamageEvent& event);
    bool Heal(float amount);
    bool IsDead() const { return m_Health <= 0.0f; }
    const DamageEvent& GetLastDamage() const { return m_LastDamage; }
    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    float m_MaxHealth = 100.0f;
    float m_Health = 100.0f;
    bool m_ResetOnPlay = true;
    DamageEvent m_LastDamage;
};

class HurtboxComponent final : public Component {
public:
    const char* GetTypeName() const override { return "Hurtbox"; }
    void SetTeam(uint32_t value) { m_Team = value; }
    uint32_t GetTeam() const { return m_Team; }
    void SetDamageMultiplier(float value);
    float GetDamageMultiplier() const { return m_DamageMultiplier; }
    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    uint32_t m_Team = 0;
    float m_DamageMultiplier = 1.0f;
};

class HitboxComponent final : public Component {
public:
    const char* GetTypeName() const override { return "Hitbox"; }
    int GetExecutionOrder() const override { return 100; }
    void OnFixedUpdate(float deltaSeconds) override;
    void OnAnimationEvent(const AnimationEventData& event) override;
    void BeginAttack(float damage = -1.0f);
    void EndAttack();
    bool IsAttackActive() const { return m_AttackActive; }
    void SetDamage(float value);
    float GetDamage() const { return m_Damage; }
    void SetRadius(float value);
    float GetRadius() const { return m_Radius; }
    void SetOffset(const Vec3& value) { m_Offset = value; }
    const Vec3& GetOffset() const { return m_Offset; }
    void SetTeam(uint32_t value) { m_Team = value; }
    uint32_t GetTeam() const { return m_Team; }
    void SetLayerMask(uint32_t value) { m_LayerMask = value; }
    uint32_t GetLayerMask() const { return m_LayerMask; }
    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    float m_Damage = 10.0f;
    float m_Radius = 0.75f;
    Vec3 m_Offset = {0.0f, 1.0f, 0.75f};
    uint32_t m_Team = 0;
    uint32_t m_LayerMask = 0xffffffffu;
    bool m_AttackActive = false;
    std::unordered_set<uint64_t> m_HitActors;
};

enum class InteractionKind { Generic, Pickup, Door, Checkpoint, SceneExit };

class InteractionComponent final : public Component {
public:
    const char* GetTypeName() const override { return "Interaction"; }
    void SetKind(InteractionKind value) { m_Kind = value; }
    InteractionKind GetKind() const { return m_Kind; }
    void SetPrompt(std::string value) { m_Prompt = std::move(value); }
    const std::string& GetPrompt() const { return m_Prompt; }
    void SetRange(float value);
    float GetRange() const { return m_Range; }
    void SetSingleUse(bool value) { m_SingleUse = value; }
    bool IsSingleUse() const { return m_SingleUse; }
    void SetDestroyOnUse(bool value) { m_DestroyOnUse = value; }
    bool GetDestroyOnUse() const { return m_DestroyOnUse; }
    bool CanInteract() const { return !m_Consumed && IsEnabled(); }
    bool Interact(ActorHandle instigator);
    ActorHandle GetLastInstigator() const { return m_LastInstigator; }
    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    InteractionKind m_Kind = InteractionKind::Generic;
    std::string m_Prompt = "Interact";
    float m_Range = 2.0f;
    bool m_SingleUse = false;
    bool m_DestroyOnUse = false;
    bool m_Consumed = false;
    ActorHandle m_LastInstigator;
};

class GameplayFeedbackComponent final : public Component {
public:
    const char* GetTypeName() const override { return "GameplayFeedback"; }
    int GetExecutionOrder() const override { return 1100; }
    void OnLateUpdate(float deltaSeconds) override;
    void OnEndPlay() override;
    void Shake(float amplitude, float duration);
    void Flash(float intensity, float duration);
    void SlowMotion(float scale, float duration);
    bool IsActive() const { return m_ShakeRemaining > 0.0f || m_FlashRemaining > 0.0f || m_SlowRemaining > 0.0f; }
    const Vec3& GetAppliedShakeOffset() const { return m_LastShakeOffset; }
    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    Vec3 m_LastShakeOffset = Vec3::Zero();
    float m_ShakeAmplitude = 0.0f, m_ShakeRemaining = 0.0f, m_FlashIntensity = 0.0f, m_FlashRemaining = 0.0f,
          m_SlowRemaining = 0.0f;
    float m_OriginalVignette = 0.0f, m_OriginalSaturation = 1.0f;
    bool m_FlashCaptured = false;
    uint32_t m_NoiseState = 0x12345678u;
};
