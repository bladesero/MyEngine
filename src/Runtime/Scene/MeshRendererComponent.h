#pragma once

#include "Scene/Component.h"
#include "Assets/MeshAsset.h"
#include "Assets/MaterialAsset.h"

// ==========================================================================
// MeshRendererComponent  –  可渲染的网格 + 材质
//
// 挂载到 Actor 后，场景渲染时用 Actor 的 World 矩阵与 Camera 的 ViewProj
// 绘制该网格。序列化时保存 mesh/material 路径，反序列化时通过 AssetManager 解析。
// ==========================================================================

class MeshRendererComponent : public Component {
public:
    MeshHandle    GetMesh()    const { return m_Mesh; }
    MaterialHandle GetMaterial() const { return m_Material; }

    void SetMesh(MeshHandle h)       { m_Mesh = std::move(h); }
    void SetMaterial(MaterialHandle h) { m_Material = std::move(h); }

    bool IsValid() const {
        return m_Mesh.IsValid() && m_Material.IsValid();
    }

    const char* GetTypeName() const override { return "MeshRenderer"; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    MeshHandle     m_Mesh;
    MaterialHandle m_Material;
};
