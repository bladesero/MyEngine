#pragma once

#include "Animation/AnimationData.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

enum class AnimatorConditionMode { Greater, Less, Equals, NotEquals, If, IfNot, Trigger };

struct AnimatorCondition {
    std::string parameter;
    AnimatorConditionMode mode = AnimatorConditionMode::If;
    float threshold = 0.0f;
};

struct AnimatorTransition {
    std::string destination;
    float duration = 0.15f;
    bool hasExitTime = false;
    float exitTime = 1.0f;
    std::vector<AnimatorCondition> conditions;
};

struct AnimatorEvent {
    float normalizedTime = 0.0f;
    std::string name;
    std::string payload;
};

struct AnimatorBlendSample {
    float threshold = 0.0f;
    AnimationClip clip;
};

struct AnimatorState {
    std::string name;
    AnimationClip clip;
    std::string blendParameter;
    std::vector<AnimatorBlendSample> blendTree;
    std::vector<AnimatorTransition> transitions;
    std::vector<AnimatorEvent> events;
    float speed = 1.0f;
};

class AnimatorController {
public:
    void SetEntryState(std::string state) { m_EntryState = std::move(state); }
    const std::string& GetEntryState() const { return m_EntryState; }
    bool AddState(AnimatorState state);
    const AnimatorState* FindState(const std::string& name) const;
    const std::vector<AnimatorState>& GetStates() const { return m_States; }

    void Serialize(nlohmann::json& data) const;
    bool Deserialize(const nlohmann::json& data);

private:
    std::string m_EntryState;
    std::vector<AnimatorState> m_States;
};
