#pragma once

#include "Scene/Component.h"
#include "Scene/ActorHandle.h"

#include <vector>

enum class EnemyState { Idle, Patrol, Chase, Attack, Stagger, Dead };

class EnemyAIComponent final : public Component {
public:
    const char* GetTypeName() const override { return "EnemyAI"; }
    int GetExecutionOrder() const override { return 50; }
    void OnUpdate(float deltaSeconds) override;
    void SetTarget(ActorHandle target);
    ActorHandle GetTarget() const { return m_Target; }
    EnemyState GetState() const { return m_State; }
    void Stagger(float duration = 0.25f);
    void SetDetectionRange(float value);
    float GetDetectionRange() const { return m_DetectionRange; }
    void SetFieldOfViewDegrees(float value);
    float GetFieldOfViewDegrees() const { return m_FieldOfViewDegrees; }
    void SetAttackRange(float value);
    float GetAttackRange() const { return m_AttackRange; }
    void SetAttackDamage(float value);
    float GetAttackDamage() const { return m_AttackDamage; }
    void AddPatrolPoint(const Vec3& point) { m_PatrolPoints.push_back(point); }
    void ClearPatrolPoints() { m_PatrolPoints.clear(); }
    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    bool CanSeeTarget() const;
    void ChangeState(EnemyState state);
    ActorHandle m_Target;
    uint64_t m_TargetActorID = 0;
    EnemyState m_State = EnemyState::Idle;
    std::vector<Vec3> m_PatrolPoints;
    size_t m_PatrolIndex = 0;
    float m_DetectionRange = 10.0f, m_FieldOfViewDegrees = 120.0f, m_AttackRange = 1.5f, m_AttackDamage = 10.0f,
          m_AttackCooldown = 1.0f, m_AttackTimer = 0.0f, m_StateTimer = 0.0f;
};
