#include "Editor/EditorNavigationBakeService.h"

#include "Assets/AssetManager.h"
#include "Editor/EditorContext.h"
#include "Game/SceneRenderLayer.h"
#include "Assets/NavMeshAsset.h"
#include "Navigation/NavigationWorld.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace {
std::string SanitizeNavigationAssetName(std::string name)
{
    for (char& ch : name) {
        const auto value = static_cast<unsigned char>(ch);
        if (!std::isalnum(value) && ch != '_' && ch != '-') ch = '_';
    }
    return name.empty() ? "Scene" : name;
}

std::vector<AABB> CollectStaticNavigationObstacles(Scene& scene)
{
    std::vector<AABB> obstacles;
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsStatic()) return;
        const Vec3 position = actor.GetWorldPosition();
        const Vec3 halfScale = actor.GetTransform().scale * 0.5f;
        obstacles.push_back({position - halfScale, position + halfScale});
    });
    return obstacles;
}
}

bool EditorNavigationBakeService::Bake(EditorContext& context, Scene& scene) const
{
    static constexpr AABB kDefaultBounds{{-50.0f, 0.0f, -50.0f},
                                         {50.0f, 5.0f, 50.0f}};
    const std::vector<AABB> obstacles = CollectStaticNavigationObstacles(scene);
    if (!scene.GetNavigationWorld().Bake({kDefaultBounds, 0.5f, 0.4f}, obstacles)) {
        return false;
    }

    const std::filesystem::path relative =
        std::filesystem::path("Content") / "Navigation" /
        (SanitizeNavigationAssetName(scene.GetName()) + ".navmesh");
    const std::filesystem::path absolute =
        AssetManager::Get().GetProjectRoot() / relative;
    NavMeshAsset asset(absolute.string());
    asset.Capture(scene.GetNavigationWorld());
    if (!SaveNavMeshAssetToFile(asset, absolute.string())) return false;

    scene.SetNavMeshAssetPath(relative.generic_string());
    if (auto* layer = context.GetSceneLayer()) layer->MarkDirty();
    return true;
}
