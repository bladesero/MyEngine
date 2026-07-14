#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Animation/AnimationData.h"
#include "Scene/Component.h"

#include <string>
#include <vector>

class SkinnedMeshRendererComponent final : public Component {
public:
    const char* GetTypeName() const override { return "SkinnedMeshRenderer"; }
    void OnUpdate(float deltaSeconds) override;

    void SetSourceMesh(MeshHandle mesh);
    MeshHandle GetSourceMesh() const { return m_SourceMesh; }
    MeshAsset* GetRenderMesh() const { return m_DeformedMesh.get(); }

    void SetMaterial(MaterialHandle material) { m_Material = std::move(material); }
    MaterialHandle GetMaterial() const { return m_Material; }
    bool IsValid() const { return m_SourceMesh.IsValid() && m_Material.IsValid() && m_DeformedMesh != nullptr; }

    void SetSkeleton(std::vector<Bone> bones, std::vector<SkinWeight> weights);
    void SetAnimation(AnimationClip clip);
    void SetBlendAnimation(AnimationClip clip, float weight);
    void ClearBlendAnimation();
    void SetBlendWeight(float weight);
    float GetBlendWeight() const { return m_BlendWeight; }
    void SetPlaying(bool playing) { m_Playing = playing; }
    bool IsPlaying() const { return m_Playing; }
    float GetAnimationTime() const { return m_Time; }
    void SetAnimationTime(float time);

    const std::vector<Bone>& GetBones() const { return m_Bones; }
    const std::vector<SkinWeight>& GetWeights() const { return m_Weights; }
    const AnimationClip& GetAnimation() const { return m_Clip; }
    const std::vector<Mat4>& GetSkinMatrices() const { return m_SkinMatrices; }
    bool UsesGpuSkinning() const {
        return !m_SkinMatrices.empty() && m_SkinMatrices.size() <= 128 &&
               m_Weights.size() == (m_SourceMesh ? m_SourceMesh->VertexCount() : 0);
    }

    // Compatibility entrypoint. GPU skinning keeps source vertices unchanged.
    void Skin();

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    BoneKeyframe SampleTrack(const BoneTrack& track, const Bone& bone, float time) const;
    BoneKeyframe SampleClip(const AnimationClip& clip, const Bone& bone, size_t boneIndex, float time) const;
    void RebuildPose();
    void RebuildGpuMesh();

    MeshHandle m_SourceMesh;
    MaterialHandle m_Material;
    std::shared_ptr<MeshAsset> m_DeformedMesh;
    std::vector<Bone> m_Bones;
    std::vector<SkinWeight> m_Weights;
    std::vector<Mat4> m_SkinMatrices;
    AnimationClip m_Clip;
    AnimationClip m_BlendClip;
    float m_Time = 0.0f;
    float m_BlendTime = 0.0f;
    float m_BlendWeight = 0.0f;
    bool m_Playing = true;
};
