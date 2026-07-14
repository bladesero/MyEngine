#include "Animation/AnimatorComponent.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Scene/Actor.h"

#include <algorithm>
#include <cmath>

void AnimatorComponent::OnBeginPlay() {
    if (m_CurrentState.empty())
        m_CurrentState = m_Controller.GetEntryState();
    ApplyState(0.0f);
}

void AnimatorComponent::SetController(AnimatorController controller) {
    m_Controller = std::move(controller);
    m_CurrentState = m_Controller.GetEntryState();
    m_StateTime = 0.0f;
    ApplyState(0.0f);
}

float AnimatorComponent::GetFloat(const std::string& name) const {
    const auto it = m_Floats.find(name);
    return it == m_Floats.end() ? 0.0f : it->second;
}

bool AnimatorComponent::GetBool(const std::string& name) const {
    const auto it = m_Bools.find(name);
    return it != m_Bools.end() && it->second;
}

float AnimatorComponent::GetNormalizedTime() const {
    const AnimatorState* state = m_Controller.FindState(m_CurrentState);
    return state && state->clip.duration > 0.0f ? m_StateTime / state->clip.duration : 0.0f;
}

bool AnimatorComponent::Play(const std::string& stateName, float transitionSeconds) {
    if (!m_Controller.FindState(stateName))
        return false;
    m_CurrentState = stateName;
    m_StateTime = 0.0f;
    m_TransitionTime = 0.0f;
    m_TransitionDuration = std::max(0.0f, transitionSeconds);
    ApplyState(std::max(0.0f, transitionSeconds));
    return true;
}

bool AnimatorComponent::ConditionsPass(const AnimatorTransition& transition) const {
    for (const AnimatorCondition& condition : transition.conditions) {
        const float value = GetFloat(condition.parameter);
        switch (condition.mode) {
        case AnimatorConditionMode::Greater:
            if (!(value > condition.threshold))
                return false;
            break;
        case AnimatorConditionMode::Less:
            if (!(value < condition.threshold))
                return false;
            break;
        case AnimatorConditionMode::Equals:
            if (std::fabs(value - condition.threshold) > 0.0001f)
                return false;
            break;
        case AnimatorConditionMode::NotEquals:
            if (std::fabs(value - condition.threshold) <= 0.0001f)
                return false;
            break;
        case AnimatorConditionMode::If:
            if (!GetBool(condition.parameter))
                return false;
            break;
        case AnimatorConditionMode::IfNot:
            if (GetBool(condition.parameter))
                return false;
            break;
        case AnimatorConditionMode::Trigger:
            if (!m_Triggers.count(condition.parameter))
                return false;
            break;
        }
    }
    return true;
}

void AnimatorComponent::ApplyState(float transitionSeconds) {
    const AnimatorState* state = m_Controller.FindState(m_CurrentState);
    auto* renderer = GetOwner() ? GetOwner()->GetComponent<SkinnedMeshRendererComponent>() : nullptr;
    if (!state || !renderer)
        return;
    if (transitionSeconds > 0.0f && !renderer->GetAnimation().tracks.empty())
        renderer->SetBlendAnimation(state->clip, 0.0f);
    else
        renderer->SetAnimation(state->clip);
    if (!state->blendTree.empty()) {
        const float parameter = GetFloat(state->blendParameter);
        auto samples = state->blendTree;
        std::sort(samples.begin(), samples.end(),
                  [](const auto& a, const auto& b) { return a.threshold < b.threshold; });
        const auto upper =
            std::lower_bound(samples.begin(), samples.end(), parameter,
                             [](const AnimatorBlendSample& sample, float value) { return sample.threshold < value; });
        if (upper == samples.begin())
            renderer->SetAnimation(upper->clip);
        else if (upper == samples.end())
            renderer->SetAnimation(samples.back().clip);
        else {
            const auto& lower = *(upper - 1);
            const float range = std::max(0.0001f, upper->threshold - lower.threshold);
            renderer->SetAnimation(lower.clip);
            renderer->SetBlendAnimation(upper->clip, (parameter - lower.threshold) / range);
        }
    }
}

void AnimatorComponent::UpdateEvents(const AnimatorState& state, float previousTime, float currentTime) {
    if (state.clip.duration <= 0.0f)
        return;
    const float previous = previousTime / state.clip.duration;
    const float current = currentTime / state.clip.duration;
    for (const AnimatorEvent& event : state.events) {
        const bool crossed = current >= previous ? event.normalizedTime > previous && event.normalizedTime <= current
                                                 : event.normalizedTime > previous || event.normalizedTime <= current;
        if (crossed) {
            m_PendingEvents.push_back(event);
            if (Actor* owner = GetOwner()) {
                const AnimationEventData dispatched{event.name, event.payload};
                owner->ForEachComponent([&](Component& component) {
                    if (&component != this && component.IsEnabled())
                        component.OnAnimationEvent(dispatched);
                });
            }
        }
    }
}

void AnimatorComponent::OnUpdate(float deltaSeconds) {
    const AnimatorState* state = m_Controller.FindState(m_CurrentState);
    if (!state) {
        Play(m_Controller.GetEntryState());
        state = m_Controller.FindState(m_CurrentState);
    }
    if (!state)
        return;
    const float previous = m_StateTime;
    m_StateTime += std::max(0.0f, deltaSeconds) * std::max(0.0f, state->speed);
    if (m_TransitionDuration > 0.0f) {
        m_TransitionTime += std::max(0.0f, deltaSeconds);
        if (auto* owner = GetOwner()) {
            if (auto* renderer = owner->GetComponent<SkinnedMeshRendererComponent>()) {
                const float weight = std::clamp(m_TransitionTime / m_TransitionDuration, 0.0f, 1.0f);
                renderer->SetBlendWeight(weight);
                if (weight >= 1.0f) {
                    renderer->SetAnimation(state->clip);
                    renderer->ClearBlendAnimation();
                    m_TransitionDuration = 0.0f;
                }
            }
        }
    }
    if (state->clip.looping && state->clip.duration > 0.0f)
        m_StateTime = std::fmod(m_StateTime, state->clip.duration);
    UpdateEvents(*state, previous, m_StateTime);
    for (const AnimatorTransition& transition : state->transitions) {
        if (transition.hasExitTime && GetNormalizedTime() < transition.exitTime)
            continue;
        if (!ConditionsPass(transition))
            continue;
        for (const AnimatorCondition& condition : transition.conditions)
            if (condition.mode == AnimatorConditionMode::Trigger)
                m_Triggers.erase(condition.parameter);
        Play(transition.destination, transition.duration);
        break;
    }
}

std::vector<AnimatorEvent> AnimatorComponent::ConsumeEvents() {
    std::vector<AnimatorEvent> result;
    result.swap(m_PendingEvents);
    return result;
}

void AnimatorComponent::Serialize(nlohmann::json& data) const {
    m_Controller.Serialize(data["controller"]);
    data["currentState"] = m_CurrentState;
    data["floats"] = m_Floats;
    data["bools"] = m_Bools;
    data["applyRootMotion"] = m_ApplyRootMotion;
}

void AnimatorComponent::Deserialize(const nlohmann::json& data) {
    if (data.contains("controller"))
        m_Controller.Deserialize(data["controller"]);
    m_CurrentState = data.value("currentState", m_Controller.GetEntryState());
    m_Floats = data.value("floats", decltype(m_Floats){});
    m_Bools = data.value("bools", decltype(m_Bools){});
    m_ApplyRootMotion = data.value("applyRootMotion", false);
    m_StateTime = 0.0f;
    ApplyState(0.0f);
}
