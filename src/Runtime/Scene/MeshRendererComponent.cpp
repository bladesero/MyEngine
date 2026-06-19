#include "Scene/MeshRendererComponent.h"
#include "Assets/AssetManager.h"

void MeshRendererComponent::Serialize(nlohmann::json& data) const {
    Component::Serialize(data);
    if (m_Mesh.Get()) {
        data["mesh"] = AssetManager::Get().MakeProjectRelativePath(m_Mesh->GetPath());
    }
    if (m_Material.Get()) {
        data["material"] = AssetManager::Get().MakeProjectRelativePath(m_Material->GetPath());
    }
}

void MeshRendererComponent::Deserialize(const nlohmann::json& data) {
    Component::Deserialize(data);
    std::string meshPath;
    auto itMesh = data.find("mesh");
    if (itMesh != data.end() && itMesh->is_string()) {
        meshPath = itMesh->get<std::string>();
        m_Mesh = AssetManager::Get().ResolveMeshReference(meshPath);
    }
    auto itMat = data.find("material");
    if (itMat != data.end() && itMat->is_string()) {
        m_Material = AssetManager::Get().ResolveMaterialReference(
            itMat->get<std::string>(), meshPath);
    }
}
