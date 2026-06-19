#include "Scene/MeshRendererComponent.h"
#include "Assets/AssetManager.h"

namespace {

bool IsEmbeddedBuiltinMaterialPath(const std::string& path)
{
    return path.rfind("__builtin__/", 0) == 0 && path != "__builtin__/Default";
}

}

void MeshRendererComponent::Serialize(nlohmann::json& data) const {
    Component::Serialize(data);
    if (m_Mesh.Get()) {
        data["mesh"] = AssetManager::Get().MakeProjectRelativePath(m_Mesh->GetPath());
    }
    if (m_Material.Get()) {
        const std::string materialPath =
            AssetManager::Get().MakeProjectRelativePath(m_Material->GetPath());
        data["material"] = materialPath;
        if (IsEmbeddedBuiltinMaterialPath(materialPath)) {
            SerializeMaterialAssetForScene(*m_Material.Get(), data["materialData"]);
        }
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
        const std::string materialPath = itMat->get<std::string>();
        const auto materialData = data.find("materialData");
        const bool hasEmbeddedMaterial =
            materialData != data.end() && materialData->is_object();
        if (hasEmbeddedMaterial && IsEmbeddedBuiltinMaterialPath(materialPath)) {
            auto material = LoadMaterialAssetFromScene(*materialData, materialPath);
            if (material) {
                m_Material = AssetManager::Get().Register(std::move(material));
                return;
            }
        }
        m_Material = AssetManager::Get().ResolveMaterialReference(materialPath, meshPath);
        if (!m_Material.IsValid() && hasEmbeddedMaterial) {
            auto material = LoadMaterialAssetFromScene(*materialData,
                materialPath.empty() ? std::string("__builtin__/SceneMaterial") : materialPath);
            if (material) {
                m_Material = AssetManager::Get().Register(std::move(material));
            }
        }
    }
}
