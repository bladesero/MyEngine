#include "Animation/SkinnedMeshRendererComponent.h"

#include "Assets/AssetManager.h"

#include <algorithm>
#include <cmath>

namespace {

Mat4 MakeTransform(const Vec3& translation, const Quat& rotation, const Vec3& scale)
{
    return Mat4::Scale(scale) * rotation.ToMat4() * Mat4::Translation(translation);
}

nlohmann::json VecToJson(const Vec3& value)
{
    return nlohmann::json::array({ value.x, value.y, value.z });
}

nlohmann::json QuatToJson(const Quat& value)
{
    return nlohmann::json::array({ value.x, value.y, value.z, value.w });
}

Vec3 JsonToVec(const nlohmann::json& value, const Vec3& fallback)
{
    if (!value.is_array() || value.size() != 3) return fallback;
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
}

Quat JsonToQuat(const nlohmann::json& value, const Quat& fallback)
{
    if (!value.is_array() || value.size() != 4) return fallback;
    return Quat{
        value[0].get<float>(), value[1].get<float>(),
        value[2].get<float>(), value[3].get<float>()
    }.Normalized();
}

nlohmann::json MatToJson(const Mat4& value)
{
    nlohmann::json result = nlohmann::json::array();
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.push_back(value.m[row][column]);
        }
    }
    return result;
}

Mat4 JsonToMat(const nlohmann::json& value)
{
    if (!value.is_array() || value.size() != 16) return Mat4::Identity();
    Mat4 result;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.m[row][column] = value[static_cast<size_t>(row * 4 + column)].get<float>();
        }
    }
    return result;
}

} // namespace

void SkinnedMeshRendererComponent::OnUpdate(float deltaSeconds)
{
    if (m_Playing && m_Clip.duration > 0.0f) {
        m_Time += deltaSeconds;
        if (m_Clip.looping) m_Time = std::fmod(m_Time, m_Clip.duration);
        else m_Time = std::min(m_Time, m_Clip.duration);
    }
    if (m_Playing && m_BlendClip.duration > 0.0f) {
        m_BlendTime += deltaSeconds;
        if (m_BlendClip.looping) {
            m_BlendTime = std::fmod(m_BlendTime, m_BlendClip.duration);
        } else {
            m_BlendTime = std::min(m_BlendTime, m_BlendClip.duration);
        }
    }
    RebuildPose();
}

void SkinnedMeshRendererComponent::SetSourceMesh(MeshHandle mesh)
{
    m_SourceMesh = std::move(mesh);
    if (!m_SourceMesh) {
        m_DeformedMesh.reset();
        return;
    }
    m_DeformedMesh = std::make_shared<MeshAsset>(m_SourceMesh->GetPath() + "#skinned-runtime");
    m_DeformedMesh->SetName(m_SourceMesh->GetName() + "_Skinned");
    RebuildGpuMesh();
}

void SkinnedMeshRendererComponent::SetSkeleton(
    std::vector<Bone> bones, std::vector<SkinWeight> weights)
{
    m_Bones = std::move(bones);
    m_Weights = std::move(weights);
    m_SkinMatrices.assign(m_Bones.size(), Mat4::Identity());
    RebuildGpuMesh();
    RebuildPose();
}

void SkinnedMeshRendererComponent::SetAnimation(AnimationClip clip)
{
    m_Clip = std::move(clip);
    m_Time = 0.0f;
    RebuildPose();
}

void SkinnedMeshRendererComponent::SetBlendAnimation(AnimationClip clip, float weight)
{
    m_BlendClip = std::move(clip);
    m_BlendTime = 0.0f;
    SetBlendWeight(weight);
    RebuildPose();
}

void SkinnedMeshRendererComponent::ClearBlendAnimation()
{
    m_BlendClip = {};
    m_BlendTime = 0.0f;
    m_BlendWeight = 0.0f;
    RebuildPose();
}

void SkinnedMeshRendererComponent::SetBlendWeight(float weight)
{
    m_BlendWeight = std::clamp(weight, 0.0f, 1.0f);
    RebuildPose();
}

void SkinnedMeshRendererComponent::SetAnimationTime(float time)
{
    m_Time = std::max(0.0f, time);
    if (m_Clip.duration > 0.0f) {
        m_Time = m_Clip.looping ? std::fmod(m_Time, m_Clip.duration)
                                : std::min(m_Time, m_Clip.duration);
    }
    RebuildPose();
}

BoneKeyframe SkinnedMeshRendererComponent::SampleTrack(
    const BoneTrack& track, const Bone& bone, float time) const
{
    BoneKeyframe fallback;
    fallback.translation = bone.bindTranslation;
    fallback.rotation = bone.bindRotation;
    fallback.scale = bone.bindScale;
    if (track.keys.empty()) return fallback;
    if (track.keys.size() == 1 || time <= track.keys.front().time) return track.keys.front();
    if (time >= track.keys.back().time) return track.keys.back();

    for (size_t i = 0; i + 1 < track.keys.size(); ++i) {
        const BoneKeyframe& a = track.keys[i];
        const BoneKeyframe& b = track.keys[i + 1];
        if (time < a.time || time > b.time) continue;
        const float alpha = (time - a.time) / std::max(0.00001f, b.time - a.time);
        BoneKeyframe result;
        result.time = time;
        result.translation = Vec3::Lerp(a.translation, b.translation, alpha);
        result.rotation = Slerp(a.rotation, b.rotation, alpha);
        result.scale = Vec3::Lerp(a.scale, b.scale, alpha);
        return result;
    }
    return fallback;
}

BoneKeyframe SkinnedMeshRendererComponent::SampleClip(
    const AnimationClip& clip, const Bone& bone,
    size_t boneIndex, float time) const
{
    for (const BoneTrack& track : clip.tracks) {
        if (track.boneIndex == boneIndex) return SampleTrack(track, bone, time);
    }
    BoneKeyframe bindPose;
    bindPose.translation = bone.bindTranslation;
    bindPose.rotation = bone.bindRotation;
    bindPose.scale = bone.bindScale;
    return bindPose;
}

void SkinnedMeshRendererComponent::RebuildPose()
{
    if (m_Bones.empty()) return;
    std::vector<Mat4> globalPose(m_Bones.size(), Mat4::Identity());
    for (size_t i = 0; i < m_Bones.size(); ++i) {
        const Bone& bone = m_Bones[i];
        BoneKeyframe pose = SampleClip(m_Clip, bone, i, m_Time);
        if (m_BlendWeight > 0.0f && !m_BlendClip.tracks.empty()) {
            const BoneKeyframe blendPose =
                SampleClip(m_BlendClip, bone, i, m_BlendTime);
            pose.translation = Vec3::Lerp(
                pose.translation, blendPose.translation, m_BlendWeight);
            pose.rotation = Slerp(
                pose.rotation, blendPose.rotation, m_BlendWeight);
            pose.scale = Vec3::Lerp(pose.scale, blendPose.scale, m_BlendWeight);
        }
        const Mat4 local = MakeTransform(pose.translation, pose.rotation, pose.scale);
        globalPose[i] = bone.parent >= 0 && static_cast<size_t>(bone.parent) < i
            ? local * globalPose[static_cast<size_t>(bone.parent)] : local;
        m_SkinMatrices[i] = bone.inverseBind * globalPose[i];
    }
}

void SkinnedMeshRendererComponent::RebuildGpuMesh()
{
    if (!m_SourceMesh || !m_DeformedMesh) return;
    const auto& source = m_SourceMesh->GetVertices();
    std::vector<MeshVertex> vertices = source;
    if (m_Weights.size() == source.size()) {
        for (size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex) {
            for (size_t influence = 0; influence < 4; ++influence) {
                vertices[vertexIndex].boneIndices[influence] =
                    static_cast<float>(m_Weights[vertexIndex].boneIndices[influence]);
                vertices[vertexIndex].boneWeights[influence] =
                    m_Weights[vertexIndex].weights[influence];
            }
        }
    }
    m_DeformedMesh->SetGeometry(
        std::move(vertices), m_SourceMesh->GetIndices(), m_SourceMesh->GetSubMeshes());
}

void SkinnedMeshRendererComponent::Skin()
{
    RebuildPose();
}

void SkinnedMeshRendererComponent::Serialize(nlohmann::json& data) const
{
    data["mesh"] = m_SourceMesh
        ? AssetManager::Get().MakeProjectRelativePath(m_SourceMesh->GetPath()) : "";
    data["material"] = m_Material
        ? AssetManager::Get().MakeProjectRelativePath(m_Material->GetPath()) : "";
    data["playing"] = m_Playing;
    data["time"] = m_Time;
    data["clip"] = {
        { "name", m_Clip.name }, { "duration", m_Clip.duration }, { "looping", m_Clip.looping }
    };
    data["bones"] = nlohmann::json::array();
    for (const Bone& bone : m_Bones) {
        data["bones"].push_back({
            { "name", bone.name }, { "parent", bone.parent },
            { "translation", VecToJson(bone.bindTranslation) },
            { "rotation", QuatToJson(bone.bindRotation) },
            { "scale", VecToJson(bone.bindScale) },
            { "inverseBind", MatToJson(bone.inverseBind) },
        });
    }
    data["weights"] = nlohmann::json::array();
    for (const SkinWeight& weight : m_Weights) {
        data["weights"].push_back({
            { "bones", weight.boneIndices },
            { "values", weight.weights },
        });
    }
    data["tracks"] = nlohmann::json::array();
    for (const BoneTrack& track : m_Clip.tracks) {
        nlohmann::json trackJson;
        trackJson["bone"] = track.boneIndex;
        trackJson["keys"] = nlohmann::json::array();
        for (const BoneKeyframe& key : track.keys) {
            trackJson["keys"].push_back({
                { "time", key.time },
                { "translation", VecToJson(key.translation) },
                { "rotation", QuatToJson(key.rotation) },
                { "scale", VecToJson(key.scale) },
            });
        }
        data["tracks"].push_back(std::move(trackJson));
    }
}

void SkinnedMeshRendererComponent::Deserialize(const nlohmann::json& data)
{
    const std::string meshPath = data.value("mesh", std::string{});
    const std::string materialPath = data.value("material", std::string{});
    if (!meshPath.empty()) {
        MeshHandle mesh = AssetManager::Get().GetByPath<MeshAsset>(meshPath);
        if (!mesh && meshPath.rfind("__builtin__/", 0) == 0) {
            if (meshPath.find("Cube") != std::string::npos) mesh = AssetManager::Get().GetCubeMesh();
            else if (meshPath.find("Quad") != std::string::npos) mesh = AssetManager::Get().GetQuadMesh();
        }
        SetSourceMesh(mesh);
    }
    if (!materialPath.empty()) {
        MaterialHandle material = AssetManager::Get().GetByPath<MaterialAsset>(materialPath);
        if (!material && materialPath.rfind("__builtin__/", 0) == 0) {
            material = AssetManager::Get().GetDefaultMaterial();
        }
        SetMaterial(material);
    }

    std::vector<Bone> bones;
    if (data.contains("bones")) {
        for (const auto& value : data["bones"]) {
            Bone bone;
            bone.name = value.value("name", std::string{});
            bone.parent = value.value("parent", -1);
            if (value.contains("translation")) bone.bindTranslation = JsonToVec(value["translation"], Vec3::Zero());
            if (value.contains("rotation")) bone.bindRotation = JsonToQuat(value["rotation"], Quat::Identity());
            if (value.contains("scale")) bone.bindScale = JsonToVec(value["scale"], Vec3::One());
            if (value.contains("inverseBind")) bone.inverseBind = JsonToMat(value["inverseBind"]);
            bones.push_back(bone);
        }
    }
    std::vector<SkinWeight> weights;
    if (data.contains("weights")) {
        for (const auto& value : data["weights"]) {
            SkinWeight weight;
            if (value.contains("bones")) {
                for (size_t i = 0; i < 4 && i < value["bones"].size(); ++i) {
                    weight.boneIndices[i] = value["bones"][i].get<uint16_t>();
                }
            }
            if (value.contains("values")) {
                for (size_t i = 0; i < 4 && i < value["values"].size(); ++i) {
                    weight.weights[i] = value["values"][i].get<float>();
                }
            }
            weights.push_back(weight);
        }
    }
    if (!bones.empty() && m_SourceMesh) {
        if (weights.size() != m_SourceMesh->VertexCount()) {
            weights.assign(m_SourceMesh->VertexCount(), SkinWeight{});
        }
        SetSkeleton(std::move(bones), std::move(weights));
    }

    AnimationClip clip;
    if (data.contains("clip")) {
        const auto& clipJson = data["clip"];
        clip.name = clipJson.value("name", std::string{});
        clip.duration = clipJson.value("duration", 0.0f);
        clip.looping = clipJson.value("looping", true);
    }
    if (data.contains("tracks")) {
        for (const auto& trackJson : data["tracks"]) {
            BoneTrack track;
            track.boneIndex = trackJson.value("bone", uint16_t(0));
            if (trackJson.contains("keys")) {
                for (const auto& keyJson : trackJson["keys"]) {
                    BoneKeyframe key;
                    key.time = keyJson.value("time", 0.0f);
                    if (keyJson.contains("translation")) key.translation = JsonToVec(keyJson["translation"], Vec3::Zero());
                    if (keyJson.contains("rotation")) key.rotation = JsonToQuat(keyJson["rotation"], Quat::Identity());
                    if (keyJson.contains("scale")) key.scale = JsonToVec(keyJson["scale"], Vec3::One());
                    track.keys.push_back(key);
                }
            }
            clip.tracks.push_back(std::move(track));
        }
    }
    SetAnimation(std::move(clip));
    m_Playing = data.value("playing", true);
    SetAnimationTime(data.value("time", 0.0f));
}
