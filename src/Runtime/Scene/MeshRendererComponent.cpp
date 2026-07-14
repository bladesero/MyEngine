#include "Scene/MeshRendererComponent.h"
#include "Assets/AssetManager.h"

namespace {

bool IsEmbeddedBuiltinMaterialPath(const std::string& path) {
    return path.rfind("__builtin__/", 0) == 0 && path != "__builtin__/Default";
}

} // namespace

MaterialHandle MeshRendererComponent::GetMaterialForSlot(int slot) const {
    if (slot >= 0) {
        const size_t index = static_cast<size_t>(slot);
        if (index < m_Materials.size() && m_Materials[index].IsValid()) {
            return m_Materials[index];
        }
    }
    if (!m_Materials.empty() && m_Materials[0].IsValid()) {
        return m_Materials[0];
    }
    return AssetManager::Get().GetDefaultMaterial();
}

void MeshRendererComponent::SetMaterials(std::vector<MaterialHandle> materials) {
    m_Materials = std::move(materials);
}

void MeshRendererComponent::SetMaterialSlot(size_t slot, MaterialHandle material) {
    if (slot >= m_Materials.size()) {
        m_Materials.resize(slot + 1);
    }
    m_Materials[slot] = std::move(material);
}

void MeshRendererComponent::Serialize(nlohmann::json& data) const {
    Component::Serialize(data);
    if (m_Mesh.Get()) {
        data["mesh"] = AssetManager::Get().MakeProjectRelativePath(m_Mesh->GetPath());
    }
    if (!m_Materials.empty()) {
        data["materials"] = nlohmann::json::array();
        for (const MaterialHandle& material : m_Materials) {
            data["materials"].push_back(
                material.Get() ? AssetManager::Get().MakeProjectRelativePath(material->GetPath()) : std::string{});
        }
    }
    MaterialHandle slot0 = !m_Materials.empty() ? m_Materials[0] : MaterialHandle{};
    if (slot0.Get()) {
        const std::string materialPath = AssetManager::Get().MakeProjectRelativePath(slot0->GetPath());
        data["material"] = materialPath;
        if (IsEmbeddedBuiltinMaterialPath(materialPath)) {
            SerializeMaterialAssetForScene(*slot0.Get(), data["materialData"]);
        }
    }
}

void MeshRendererComponent::Deserialize(const nlohmann::json& data) {
    Component::Deserialize(data);
    m_Materials.clear();
    std::string meshPath;
    auto itMesh = data.find("mesh");
    if (itMesh != data.end() && itMesh->is_string()) {
        meshPath = itMesh->get<std::string>();
        m_Mesh = AssetManager::Get().ResolveMeshReference(meshPath);
    }
    auto itMaterials = data.find("materials");
    if (itMaterials != data.end() && itMaterials->is_array()) {
        m_Materials.reserve(itMaterials->size());
        for (const auto& entry : *itMaterials) {
            if (entry.is_string()) {
                m_Materials.push_back(AssetManager::Get().ResolveMaterialReference(entry.get<std::string>(), meshPath));
            } else {
                m_Materials.push_back({});
            }
        }
    }
    auto itMat = data.find("material");
    const auto materialData = data.find("materialData");
    const bool hasEmbeddedMaterial = materialData != data.end() && materialData->is_object();
    if (!m_Materials.empty() && hasEmbeddedMaterial) {
        std::string materialPath;
        if (itMat != data.end() && itMat->is_string()) {
            materialPath = itMat->get<std::string>();
        } else if (itMaterials != data.end() && itMaterials->is_array() && !itMaterials->empty() &&
                   (*itMaterials)[0].is_string()) {
            materialPath = (*itMaterials)[0].get<std::string>();
        }
        const bool slot0Resolved = m_Materials[0].IsValid();
        if (!slot0Resolved && IsEmbeddedBuiltinMaterialPath(materialPath)) {
            auto material = LoadMaterialAssetFromScene(*materialData, materialPath);
            if (material) {
                m_Materials[0] = AssetManager::Get().Register(std::move(material));
            }
        }
    }
    if (m_Materials.empty() && itMat != data.end() && itMat->is_string()) {
        const std::string materialPath = itMat->get<std::string>();
        if (hasEmbeddedMaterial && IsEmbeddedBuiltinMaterialPath(materialPath)) {
            auto material = LoadMaterialAssetFromScene(*materialData, materialPath);
            if (material) {
                SetMaterial(AssetManager::Get().Register(std::move(material)));
                return;
            }
        }
        SetMaterial(AssetManager::Get().ResolveMaterialReference(materialPath, meshPath));
        const bool slot0Resolved = !m_Materials.empty() && m_Materials[0].IsValid();
        if (!slot0Resolved && hasEmbeddedMaterial) {
            auto material = LoadMaterialAssetFromScene(
                *materialData, materialPath.empty() ? std::string("__builtin__/SceneMaterial") : materialPath);
            if (material) {
                SetMaterial(AssetManager::Get().Register(std::move(material)));
            }
        }
    }
}
