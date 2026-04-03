#include "Scene/MeshRendererComponent.h"
#include "Assets/AssetManager.h"

void MeshRendererComponent::Serialize(nlohmann::json& data) const {
    Component::Serialize(data);
    if (m_Mesh.Get()) {
        data["mesh"] = m_Mesh->GetPath();
    }
    if (m_Material.Get()) {
        data["material"] = m_Material->GetPath();
    }
}

void MeshRendererComponent::Deserialize(const nlohmann::json& data) {
    Component::Deserialize(data);
    auto itMesh = data.find("mesh");
    if (itMesh != data.end() && itMesh->is_string()) {
        m_Mesh = AssetManager::Get().GetByPath<MeshAsset>(itMesh->get<std::string>());
        if (!m_Mesh.IsValid()) {
            m_Mesh = AssetManager::Get().Load<MeshAsset>(itMesh->get<std::string>());
        }
    }
    auto itMat = data.find("material");
    if (itMat != data.end() && itMat->is_string()) {
        m_Material = AssetManager::Get().GetByPath<MaterialAsset>(itMat->get<std::string>());
        if (!m_Material.IsValid()) {
            m_Material = AssetManager::Get().Load<MaterialAsset>(itMat->get<std::string>());
        }
    }
}
