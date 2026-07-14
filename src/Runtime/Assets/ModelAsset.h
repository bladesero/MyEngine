#pragma once

#include "Assets/Asset.h"
#include "Assets/MeshAsset.h"
#include "Assets/MaterialAsset.h"
#include "Core/EngineMath.h"
#include "Animation/AnimationData.h"
#include <vector>
#include <string>

// ==========================================================================
// ModelAsset  –  模型资产
//
// 一个模型 = 一个 MeshAsset（含若干 SubMesh）+ 若干 MaterialAsset（按槽位索引）。
// 同时保存初始节点变换，用于 Actor 首次实例化时设定 Transform。
// ==========================================================================

// --------------------------------------------------------------------------
// ModelNode  –  模型内部的层级节点（用于蒙皮/多节点导入）
// --------------------------------------------------------------------------
struct ModelNode {
    std::string name;
    Mat4 localTransform = Mat4::Identity();
    MeshHandle mesh;           // 可为空（只有 transform 的骨骼节点）
    int materialSlot = 0;      // 索引到 ModelAsset::m_Materials
    std::vector<int> children; // 子节点索引
};

// --------------------------------------------------------------------------
// ModelAsset
// --------------------------------------------------------------------------
class ModelAsset : public Asset {
public:
    explicit ModelAsset(const std::string& path) : Asset(AssetType::Model, path) {}

    // ---- 构建模型 ----------------------------------------------------------
    void SetMesh(MeshHandle mesh) {
        m_Mesh = std::move(mesh);
        TryMarkReady();
    }

    void SetMaterials(std::vector<MaterialHandle> materials) {
        m_Materials = std::move(materials);
        TryMarkReady();
    }

    void AddMaterial(MaterialHandle mat) {
        m_Materials.push_back(std::move(mat));
        TryMarkReady();
    }

    // 设置完整节点树（可选，用于多节点模型）
    void SetNodes(std::vector<ModelNode> nodes, int rootIndex = 0) {
        m_Nodes = std::move(nodes);
        m_RootIndex = rootIndex;
    }

    // ---- 访问器 -----------------------------------------------------------
    const MeshHandle& GetMesh() const { return m_Mesh; }
    MeshAsset* GetMeshPtr() const { return m_Mesh.Get(); }

    const std::vector<MaterialHandle>& GetMaterials() const { return m_Materials; }
    MaterialHandle GetMaterial(int slot) const {
        if (slot >= 0 && slot < static_cast<int>(m_Materials.size()))
            return m_Materials[static_cast<size_t>(slot)];
        return MaterialHandle{};
    }
    int MaterialCount() const { return static_cast<int>(m_Materials.size()); }

    const std::vector<ModelNode>& GetNodes() const { return m_Nodes; }
    int GetRootIndex() const { return m_RootIndex; }
    bool HasNodes() const { return !m_Nodes.empty(); }

    void SetSkin(std::vector<Bone> bones, std::vector<SkinWeight> weights) {
        m_Bones = std::move(bones);
        m_SkinWeights = std::move(weights);
    }
    void SetAnimations(std::vector<AnimationClip> clips) { m_Animations = std::move(clips); }
    const std::vector<Bone>& GetBones() const { return m_Bones; }
    const std::vector<SkinWeight>& GetSkinWeights() const { return m_SkinWeights; }
    const std::vector<AnimationClip>& GetAnimations() const { return m_Animations; }
    bool HasSkin() const { return !m_Bones.empty() && !m_SkinWeights.empty(); }

    bool ReloadFrom(const Asset& source) override {
        const auto* model = dynamic_cast<const ModelAsset*>(&source);
        if (!model)
            return false;
        m_Mesh = model->m_Mesh;
        m_Materials = model->m_Materials;
        m_Nodes = model->m_Nodes;
        m_RootIndex = model->m_RootIndex;
        m_Bones = model->m_Bones;
        m_SkinWeights = model->m_SkinWeights;
        m_Animations = model->m_Animations;
        SetState(AssetState::Ready);
        return true;
    }

    // ---- 工厂：从已有 mesh + material 快速构建 ----------------------------
    static std::shared_ptr<ModelAsset> Create(const std::string& name, MeshHandle mesh, MaterialHandle material = {}) {
        auto model = std::make_shared<ModelAsset>("__builtin__/" + name);
        model->SetName(name);
        model->SetMesh(std::move(mesh));
        if (material)
            model->AddMaterial(std::move(material));
        return model;
    }

private:
    void TryMarkReady() {
        // 至少需要有效 Mesh 才能标为 Ready
        if (m_Mesh && m_Mesh->IsReady()) {
            SetState(AssetState::Ready);
        }
    }

    MeshHandle m_Mesh;
    std::vector<MaterialHandle> m_Materials;
    std::vector<ModelNode> m_Nodes;
    int m_RootIndex = 0;
    std::vector<Bone> m_Bones;
    std::vector<SkinWeight> m_SkinWeights;
    std::vector<AnimationClip> m_Animations;
};

using ModelHandle = AssetHandle<ModelAsset>;
