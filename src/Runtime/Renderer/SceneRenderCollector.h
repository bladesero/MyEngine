#pragma once

#include "API/RuntimeApi.h"

#include <cstdint>
#include <vector>

class Actor;
class Camera;
class MaterialAsset;
class MeshAsset;
class Scene;
class SkinnedMeshRendererComponent;
struct SubMesh;

struct SceneRenderItem {
    Actor* actor = nullptr;
    MeshAsset* mesh = nullptr;
    const SubMesh* subMesh = nullptr;
    uint32_t subMeshIndex = 0;
    MaterialAsset* material = nullptr;
    SkinnedMeshRendererComponent* skin = nullptr;
    float distanceSq = 0.0f;
};

struct SceneRenderCollection {
    std::vector<SceneRenderItem> opaqueItems;
    std::vector<SceneRenderItem> transparentItems;
    uint32_t submittedSubMeshes = 0;
    uint32_t culledSubMeshes = 0;
};

class MYENGINE_RUNTIME_API SceneRenderCollector {
public:
    SceneRenderCollection Collect(const Scene& scene, const Camera& camera, bool staticGeometryOnly = false) const;
};
