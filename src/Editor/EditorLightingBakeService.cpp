#include "Editor/EditorLightingBakeService.h"

#include "Assets/AssetManager.h"
#include "Assets/LightingProbeAsset.h"
#include "Core/Logger.h"
#include "Editor/EditorContext.h"
#include "Game/SceneRenderLayer.h"
#include "Renderer/IRenderContext.h"
#include "Scene/Scene.h"

#include <cctype>
#include <filesystem>

namespace {
std::string SanitizeAssetName(std::string name) {
    for (char& character : name) {
        const auto value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '_' && character != '-')
            character = '_';
    }
    return name.empty() ? "Scene" : name;
}
} // namespace

bool EditorLightingBakeService::RequestBake(Scene& scene) {
    if (m_PendingScene || m_ActiveScene)
        return false;
    m_PendingScene = &scene;
    m_LastResult = {};
    Logger::Info("[LightingBake] queued GPU reflection probe bake for ", scene.GetName());
    return true;
}

void EditorLightingBakeService::OnUpdate(float deltaSeconds) {
    (void)deltaSeconds;
    EditorContext* context = GetContext();
    if (!context || !m_PendingScene)
        return;
    m_ActiveScene = m_PendingScene;
    m_PendingScene = nullptr;
    m_LastResult = ExecuteBake(*context, *m_ActiveScene);
    m_ActiveScene = nullptr;
}

void EditorLightingBakeService::OnDetach() {
    m_PendingScene = nullptr;
    m_ActiveScene = nullptr;
    EditorService::OnDetach();
}

ProbeBakeResult EditorLightingBakeService::ExecuteBake(EditorContext& context, Scene& scene) const {
    ProbeBakeResult result;
    IRenderContext* renderContext = context.GetRenderContext();
    if (!renderContext ||
        (renderContext->GetBackend() != RHIBackend::D3D11 && renderContext->GetBackend() != RHIBackend::D3D12)) {
        result.error = "lighting probes can be baked only with the D3D11 or D3D12 Editor backend";
        Logger::Error("[LightingBake] ", result.error);
        return result;
    }
    const std::filesystem::path relative =
        std::filesystem::path("Content") / "Lighting" / (SanitizeAssetName(scene.GetName()) + ".lightprobes");
    const std::filesystem::path absolute = AssetManager::Get().GetProjectRoot() / relative;
    LightingProbeAsset asset(absolute.string());
    ProbeBakeRenderer baker(renderContext, renderContext, renderContext);
    result = baker.Bake(scene, asset, [](const ProbeBakeProgress& progress) {
        if (progress.completedSteps == 0 || progress.completedSteps == progress.totalSteps)
            Logger::Info("[LightingBake] ", progress.stage, " ", progress.completedSteps, "/", progress.totalSteps);
    });
    if (!result.succeeded) {
        if (!result.cancelled)
            Logger::Error("[LightingBake] failed: ", result.error);
        return result;
    }
    std::string error;
    if (!SaveLightingProbeAssetToFile(asset, absolute.string(), &error)) {
        result.succeeded = false;
        result.error = error;
        Logger::Error("[LightingBake] failed to save asset: ", error);
        return result;
    }
    scene.SetLightingProbeAssetPath(relative.generic_string());
    AssetManager::Get().Unload(relative.generic_string());
    if (auto* layer = context.GetSceneLayer())
        layer->MarkDirty();
    Logger::Info("[LightingBake] baked ", result.reflectionProbeCount, " reflection probes, ", result.shVolumeCount,
                 " SH volumes and ", result.shSampleCount, " SH samples to ", relative.generic_string());
    return result;
}

bool EditorLightingBakeService::IsBakeCurrent(const Scene& scene) const {
    if (IsBakePending(scene))
        return false;
    if (scene.GetLightingProbeAssetPath().empty())
        return false;
    auto asset = AssetManager::Get().Load<LightingProbeAsset>(scene.GetLightingProbeAssetPath());
    return asset.IsValid() && asset->GetDependencyHash() == ProbeBakeRenderer::ComputeDependencyHash(scene);
}
