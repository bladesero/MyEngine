#include "Animation/AnimatorController.h"

#include <algorithm>

namespace {
const char* ModeName(AnimatorConditionMode mode) {
    switch (mode) {
    case AnimatorConditionMode::Greater:
        return "Greater";
    case AnimatorConditionMode::Less:
        return "Less";
    case AnimatorConditionMode::Equals:
        return "Equals";
    case AnimatorConditionMode::NotEquals:
        return "NotEquals";
    case AnimatorConditionMode::IfNot:
        return "IfNot";
    case AnimatorConditionMode::Trigger:
        return "Trigger";
    default:
        return "If";
    }
}

AnimatorConditionMode ParseMode(const std::string& value) {
    if (value == "Greater")
        return AnimatorConditionMode::Greater;
    if (value == "Less")
        return AnimatorConditionMode::Less;
    if (value == "Equals")
        return AnimatorConditionMode::Equals;
    if (value == "NotEquals")
        return AnimatorConditionMode::NotEquals;
    if (value == "IfNot")
        return AnimatorConditionMode::IfNot;
    if (value == "Trigger")
        return AnimatorConditionMode::Trigger;
    return AnimatorConditionMode::If;
}

nlohmann::json ClipToJson(const AnimationClip& clip) {
    nlohmann::json result = {{"name", clip.name},
                             {"duration", clip.duration},
                             {"looping", clip.looping},
                             {"tracks", nlohmann::json::array()}};
    for (const BoneTrack& track : clip.tracks) {
        nlohmann::json value = {{"bone", track.boneIndex}, {"keys", nlohmann::json::array()}};
        for (const BoneKeyframe& key : track.keys)
            value["keys"].push_back({{"time", key.time},
                                     {"translation", {key.translation.x, key.translation.y, key.translation.z}},
                                     {"rotation", {key.rotation.x, key.rotation.y, key.rotation.z, key.rotation.w}},
                                     {"scale", {key.scale.x, key.scale.y, key.scale.z}}});
        result["tracks"].push_back(std::move(value));
    }
    return result;
}

AnimationClip ClipFromJson(const nlohmann::json& json) {
    AnimationClip clip;
    clip.name = json.value("name", std::string{});
    clip.duration = std::max(0.0f, json.value("duration", 0.0f));
    clip.looping = json.value("looping", true);
    for (const auto& trackValue : json.value("tracks", nlohmann::json::array())) {
        BoneTrack track;
        track.boneIndex = trackValue.value("bone", uint16_t(0));
        for (const auto& keyValue : trackValue.value("keys", nlohmann::json::array())) {
            BoneKeyframe key;
            key.time = keyValue.value("time", 0.0f);
            const auto translation = keyValue.value("translation", nlohmann::json::array());
            const auto rotation = keyValue.value("rotation", nlohmann::json::array());
            const auto scale = keyValue.value("scale", nlohmann::json::array());
            if (translation.size() == 3)
                key.translation = {translation[0].get<float>(), translation[1].get<float>(),
                                   translation[2].get<float>()};
            if (rotation.size() == 4)
                key.rotation = Quat{rotation[0].get<float>(), rotation[1].get<float>(), rotation[2].get<float>(),
                                    rotation[3].get<float>()}
                                   .Normalized();
            if (scale.size() == 3)
                key.scale = {scale[0].get<float>(), scale[1].get<float>(), scale[2].get<float>()};
            track.keys.push_back(key);
        }
        clip.tracks.push_back(std::move(track));
    }
    return clip;
}
} // namespace

bool AnimatorController::AddState(AnimatorState state) {
    if (state.name.empty() || FindState(state.name))
        return false;
    if (m_EntryState.empty())
        m_EntryState = state.name;
    m_States.push_back(std::move(state));
    return true;
}

const AnimatorState* AnimatorController::FindState(const std::string& name) const {
    const auto it =
        std::find_if(m_States.begin(), m_States.end(), [&](const AnimatorState& state) { return state.name == name; });
    return it == m_States.end() ? nullptr : &*it;
}

void AnimatorController::Serialize(nlohmann::json& data) const {
    data = {{"entryState", m_EntryState}, {"states", nlohmann::json::array()}};
    for (const AnimatorState& state : m_States) {
        nlohmann::json value = {{"name", state.name},
                                {"clip", ClipToJson(state.clip)},
                                {"blendParameter", state.blendParameter},
                                {"speed", state.speed},
                                {"blendTree", nlohmann::json::array()},
                                {"events", nlohmann::json::array()},
                                {"transitions", nlohmann::json::array()}};
        for (const auto& sample : state.blendTree)
            value["blendTree"].push_back({{"threshold", sample.threshold}, {"clip", ClipToJson(sample.clip)}});
        for (const auto& event : state.events)
            value["events"].push_back(
                {{"time", event.normalizedTime}, {"name", event.name}, {"payload", event.payload}});
        for (const auto& transition : state.transitions) {
            nlohmann::json transitionJson = {{"destination", transition.destination},
                                             {"duration", transition.duration},
                                             {"hasExitTime", transition.hasExitTime},
                                             {"exitTime", transition.exitTime},
                                             {"conditions", nlohmann::json::array()}};
            for (const auto& condition : transition.conditions)
                transitionJson["conditions"].push_back({{"parameter", condition.parameter},
                                                        {"mode", ModeName(condition.mode)},
                                                        {"threshold", condition.threshold}});
            value["transitions"].push_back(std::move(transitionJson));
        }
        data["states"].push_back(std::move(value));
    }
}

bool AnimatorController::Deserialize(const nlohmann::json& data) {
    if (!data.is_object() || !data.contains("states") || !data["states"].is_array())
        return false;
    std::vector<AnimatorState> states;
    for (const auto& value : data["states"]) {
        AnimatorState state;
        state.name = value.value("name", std::string{});
        if (state.name.empty())
            return false;
        if (value.contains("clip"))
            state.clip = ClipFromJson(value["clip"]);
        state.blendParameter = value.value("blendParameter", std::string{});
        state.speed = value.value("speed", 1.0f);
        for (const auto& sample : value.value("blendTree", nlohmann::json::array()))
            state.blendTree.push_back(
                {sample.value("threshold", 0.0f), ClipFromJson(sample.value("clip", nlohmann::json::object()))});
        for (const auto& event : value.value("events", nlohmann::json::array()))
            state.events.push_back({std::clamp(event.value("time", 0.0f), 0.0f, 1.0f),
                                    event.value("name", std::string{}), event.value("payload", std::string{})});
        for (const auto& transitionValue : value.value("transitions", nlohmann::json::array())) {
            AnimatorTransition transition;
            transition.destination = transitionValue.value("destination", std::string{});
            transition.duration = std::max(0.0f, transitionValue.value("duration", 0.15f));
            transition.hasExitTime = transitionValue.value("hasExitTime", false);
            transition.exitTime = transitionValue.value("exitTime", 1.0f);
            for (const auto& conditionValue : transitionValue.value("conditions", nlohmann::json::array()))
                transition.conditions.push_back({conditionValue.value("parameter", std::string{}),
                                                 ParseMode(conditionValue.value("mode", std::string("If"))),
                                                 conditionValue.value("threshold", 0.0f)});
            state.transitions.push_back(std::move(transition));
        }
        states.push_back(std::move(state));
    }
    m_States = std::move(states);
    m_EntryState = data.value("entryState", m_States.empty() ? std::string{} : m_States.front().name);
    return m_EntryState.empty() || FindState(m_EntryState) != nullptr;
}
