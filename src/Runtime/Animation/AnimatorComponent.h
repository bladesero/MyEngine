#pragma once

#include "Animation/AnimatorController.h"
#include "Scene/Component.h"

#include <unordered_map>
#include <unordered_set>

class AnimatorComponent final : public Component {
public:
    const char* GetTypeName() const override { return "Animator"; }
    void OnBeginPlay() override;
    void OnUpdate(float deltaSeconds) override;

    AnimatorController& GetController() { return m_Controller; }
    const AnimatorController& GetController() const { return m_Controller; }
    void SetController(AnimatorController controller);
    bool Play(const std::string& stateName, float transitionSeconds = 0.0f);
    void SetFloat(const std::string& name, float value) { m_Floats[name] = value; }
    float GetFloat(const std::string& name) const;
    void SetBool(const std::string& name, bool value) { m_Bools[name] = value; }
    bool GetBool(const std::string& name) const;
    void SetTrigger(const std::string& name) { m_Triggers.insert(name); }
    const std::string& GetCurrentState() const { return m_CurrentState; }
    float GetNormalizedTime() const;
    std::vector<AnimatorEvent> ConsumeEvents();
    void SetApplyRootMotion(bool enabled) { m_ApplyRootMotion = enabled; }
    bool AppliesRootMotion() const { return m_ApplyRootMotion; }
    Vec3 ConsumeRootMotionDelta() {
        Vec3 value = m_RootMotionDelta;
        m_RootMotionDelta = Vec3::Zero();
        return value;
    }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    bool ConditionsPass(const AnimatorTransition& transition) const;
    void ApplyState(float transitionSeconds);
    void UpdateEvents(const AnimatorState& state, float previousTime, float currentTime);

    AnimatorController m_Controller;
    std::unordered_map<std::string, float> m_Floats;
    std::unordered_map<std::string, bool> m_Bools;
    std::unordered_set<std::string> m_Triggers;
    std::string m_CurrentState;
    float m_StateTime = 0.0f;
    float m_TransitionTime = 0.0f;
    float m_TransitionDuration = 0.0f;
    bool m_ApplyRootMotion = false;
    Vec3 m_RootMotionDelta = Vec3::Zero();
    std::vector<AnimatorEvent> m_PendingEvents;
};
