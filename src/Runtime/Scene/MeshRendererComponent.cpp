#include "Scene/MeshRendererComponent.h"
#include "Assets/AssetManager.h"

namespace {

MeshHandle ResolveMesh(const std::string& path)
{
    AssetManager& am = AssetManager::Get();
    MeshHandle    h  = am.GetByPath<MeshAsset>(path);
    if (h.IsValid()) {
        return h;
    }
    if (path == "__builtin__/Triangle") {
        return am.GetTriangleMesh();
    }
    if (path == "__builtin__/Quad") {
        return am.GetQuadMesh();
    }
    if (path == "__builtin__/Cube") {
        return am.GetCubeMesh();
    }
    return am.Load<MeshAsset>(path);
}

MaterialHandle ResolveMaterial(const std::string& path)
{
    AssetManager&  am = AssetManager::Get();
    MaterialHandle h  = am.GetByPath<MaterialAsset>(path);
    if (h.IsValid()) {
        return h;
    }
    if (path == "__builtin__/Default") {
        return am.GetDefaultMaterial();
    }
    return am.Load<MaterialAsset>(path);
}

} // namespace

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
    auto itMesh = data.find("mesh");
    if (itMesh != data.end() && itMesh->is_string()) {
        m_Mesh = ResolveMesh(itMesh->get<std::string>());
    }
    auto itMat = data.find("material");
    if (itMat != data.end() && itMat->is_string()) {
        m_Material = ResolveMaterial(itMat->get<std::string>());
    }
}
