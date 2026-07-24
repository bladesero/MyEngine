#pragma once

#include "API/RuntimeApi.h"

#include "Scene/Component.h"
#include "Assets/MeshAsset.h"
#include "Assets/MaterialAsset.h"

#include <vector>

// ==========================================================================
// MeshRendererComponent  –  可渲染的网格 + 材质
//
// 挂载到 Actor 后，场景渲染时用 Actor 的 World 矩阵与 Camera 的 ViewProj
// 绘制该网格。序列化时保存 mesh/material 路径，反序列化时通过 AssetManager 解析。
// ==========================================================================

class MYENGINE_RUNTIME_API MeshRendererComponent : public Component {
public:
    MeshHandle GetMesh() const { return m_Mesh; }
    MaterialHandle GetMaterial() const { return GetMaterialForSlot(0); }
    const std::vector<MaterialHandle>& GetMaterials() const { return m_Materials; }
    MaterialHandle GetMaterialForSlot(int slot) const;

    void SetMesh(MeshHandle h) { m_Mesh = std::move(h); }
    void SetMaterial(MaterialHandle h) { SetMaterialSlot(0, std::move(h)); }
    void SetMaterials(std::vector<MaterialHandle> materials);
    void SetMaterialSlot(size_t slot, MaterialHandle material);

    bool IsValid() const { return m_Mesh.IsValid() && GetMaterial().IsValid(); }

    const char* GetTypeName() const override { return "MeshRenderer"; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    MeshHandle m_Mesh;
    std::vector<MaterialHandle> m_Materials;
};
