#include "Renderer/SceneRenderCollector.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Camera/Camera.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Renderer/ParticleSystemComponent.h"
#include "Renderer/MaterialSystem.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <unordered_map>

SceneRenderCollection SceneRenderCollector::Collect(const Scene& scene, const Camera& camera,
                                                    bool staticGeometryOnly) const {
    SceneRenderCollection collection;
    MaterialSystem materialSystem;
    std::unordered_map<MaterialAsset*, BlendMode> resolvedBlendModes;

    auto addRenderItem = [&](Actor& actor, MeshAsset* mesh, const SubMesh& subMesh, uint32_t subMeshIndex,
                             MaterialAsset* material, SkinnedMeshRendererComponent* skin) {
        if (!mesh || !material)
            return;
        SceneRenderItem item;
        item.actor = &actor;
        item.mesh = mesh;
        item.subMesh = &subMesh;
        item.subMeshIndex = subMeshIndex;
        item.material = material;
        item.skin = skin;
        item.distanceSq = (actor.GetWorldPosition() - camera.GetPosition()).LengthSq();
        ++collection.submittedSubMeshes;
        BlendMode blendMode = material->GetBlendMode();
        const auto cachedBlendMode = resolvedBlendModes.find(material);
        if (cachedBlendMode != resolvedBlendModes.end()) {
            blendMode = cachedBlendMode->second;
        } else {
            if (material->HasParent() || material->GetShaderAsset().IsValid()) {
                const ResolvedMaterial resolved = materialSystem.Resolve(*material);
                if (resolved.valid)
                    blendMode = resolved.blendMode;
            }
            resolvedBlendModes.emplace(material, blendMode);
        }
        if (blendMode == BlendMode::Transparent) {
            collection.transparentItems.push_back(item);
        } else {
            collection.opaqueItems.push_back(item);
        }
    };

    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive() || (staticGeometryOnly && !actor.IsStatic()))
            return;
        if (auto* particles = actor.GetComponent<ParticleSystemComponent>()) {
            if (particles->IsEnabled() && particles->GetAliveCount() > 0) {
                MeshAsset* mesh = particles->BuildBillboardMesh(camera);
                MaterialAsset* material = particles->GetMaterial();
                if (mesh && material && !mesh->GetSubMeshes().empty())
                    addRenderItem(actor, mesh, mesh->GetSubMeshes().front(), 0, material, nullptr);
            }
            return;
        }
        if (auto* skinned = actor.GetComponent<SkinnedMeshRendererComponent>()) {
            if (!skinned->IsEnabled() || !skinned->IsValid())
                return;
            MeshAsset* mesh = skinned->GetRenderMesh();
            MaterialAsset* material = skinned->GetMaterial().Get();
            if (!mesh || !material)
                return;
            const Mat4 world = actor.GetWorldMatrix();
            if (!camera.IsVisible(TransformAABB(mesh->GetAABB(), world)))
                return;
            const auto& subMeshes = mesh->GetSubMeshes();
            for (uint32_t i = 0; i < subMeshes.size(); ++i) {
                if (!camera.IsVisible(TransformAABB(subMeshes[i].bounds, world))) {
                    ++collection.culledSubMeshes;
                    continue;
                }
                addRenderItem(actor, mesh, subMeshes[i], i, material, skinned);
            }
            return;
        }

        auto* renderer = actor.GetComponent<MeshRendererComponent>();
        if (!renderer || !renderer->IsEnabled() || !renderer->IsValid())
            return;
        MeshAsset* mesh = renderer->GetMesh().Get();
        if (!mesh)
            return;
        const Mat4 world = actor.GetWorldMatrix();
        if (!camera.IsVisible(TransformAABB(mesh->GetAABB(), world)))
            return;
        const auto& subMeshes = mesh->GetSubMeshes();
        for (uint32_t i = 0; i < subMeshes.size(); ++i) {
            const SubMesh& subMesh = subMeshes[i];
            if (!camera.IsVisible(TransformAABB(subMesh.bounds, world))) {
                ++collection.culledSubMeshes;
                continue;
            }
            MaterialHandle material = renderer->GetMaterialForSlot(subMesh.materialSlot);
            addRenderItem(actor, mesh, subMesh, i, material.Get(), nullptr);
        }
    });

    std::sort(collection.opaqueItems.begin(), collection.opaqueItems.end(),
              [](const SceneRenderItem& a, const SceneRenderItem& b) {
                  if (a.mesh != b.mesh)
                      return a.mesh < b.mesh;
                  if (a.subMeshIndex != b.subMeshIndex)
                      return a.subMeshIndex < b.subMeshIndex;
                  if (a.material != b.material)
                      return a.material < b.material;
                  return a.distanceSq < b.distanceSq;
              });
    std::sort(collection.transparentItems.begin(), collection.transparentItems.end(),
              [](const SceneRenderItem& a, const SceneRenderItem& b) { return a.distanceSq > b.distanceSq; });

    return collection;
}
