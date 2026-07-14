#include "Editor/EditorOperatorShared.h"

bool EditorViewportOperator::SetSceneViewportRect(EditorContext& context, const EditorRect& rect, bool hovered) const {
    SceneViewport* viewport = context.GetSceneViewport();
    if (!viewport || rect.width <= 1.0f || rect.height <= 1.0f)
        return false;
    viewport->SetViewportRect(static_cast<int>(rect.x), static_cast<int>(rect.y), static_cast<int>(rect.width),
                              static_cast<int>(rect.height));
    viewport->SetInputEnabled(hovered);
    if (SceneRenderLayer* layer = context.GetSceneLayer())
        layer->SetSceneViewportActive(true);
    return true;
}

bool EditorViewportOperator::SetGameViewportRect(EditorContext& context, const EditorRect& rect) const {
    GameViewport* viewport = context.GetGameViewport();
    if (!viewport || rect.width <= 1.0f || rect.height <= 1.0f)
        return false;
    viewport->SetViewportRect(static_cast<int>(rect.x), static_cast<int>(rect.y), static_cast<int>(rect.width),
                              static_cast<int>(rect.height));
    if (SceneRenderLayer* layer = context.GetSceneLayer())
        layer->SetGameViewportActive(true);
    return true;
}

bool EditorViewportOperator::FrameSelected(EditorContext& context) const {
    Vec3 target{};
    float radius = 1.0f;
    if (!ResolveViewportFrameTarget(context, target, radius))
        return false;
    return FrameTarget(context, target, radius);
}

bool EditorViewportOperator::FrameTarget(EditorContext& context, const Vec3& target, float radius) const {
    SceneViewport* viewport = context.GetSceneViewport();
    if (!viewport)
        return false;
    viewport->FrameTarget(target, radius);
    return true;
}

bool EditorViewportOperator::FrameDirection(EditorContext& context, SceneViewDirection direction) const {
    SceneViewport* viewport = context.GetSceneViewport();
    if (!viewport)
        return false;
    Vec3 target{};
    float radius = 1.0f;
    if (!ResolveViewportFrameTarget(context, target, radius)) {
        target = Vec3::Zero();
        radius = 1.0f;
    }
    viewport->FrameDirection(direction, target, (std::max)(10.0f, radius * 4.0f));
    return true;
}

bool EditorViewportOperator::OrbitAroundSelection(EditorContext& context, float yawDegrees, float pitchDegrees) const {
    SceneViewport* viewport = context.GetSceneViewport();
    if (!viewport)
        return false;
    Vec3 target{};
    float radius = 1.0f;
    if (!ResolveViewportFrameTarget(context, target, radius))
        target = Vec3::Zero();
    viewport->OrbitAroundFocus(target, yawDegrees, pitchDegrees);
    return true;
}

bool EditorViewportOperator::ToggleSceneProjection(EditorContext& context) const {
    SceneViewport* viewport = context.GetSceneViewport();
    if (!viewport)
        return false;
    viewport->ToggleProjectionMode();
    return true;
}

bool EditorViewportOperator::DropModel(EditorContext& context, const std::string& path, float screenX,
                                       float screenY) const {
    if (!context.CanEditScene() || path.empty())
        return false;

    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack)
        return false;

    ModelHandle model = AssetManager::Get().Load<ModelAsset>(path);
    if (!model || !model->GetMesh()) {
        Logger::Warn("[Editor] Failed to load model: ", path);
        return false;
    }

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t previousSelection = context.GetSelection().GetActorID();

    std::string actorName = std::filesystem::path(path).stem().string();
    Actor* actor = scene->CreateActor(actorName.empty() ? "Mesh" : actorName);
    if (!actor)
        return false;

    auto* renderer = actor->AddComponent<MeshRendererComponent>();
    if (!renderer)
        return false;
    renderer->SetMesh(model->GetMesh());
    renderer->SetMaterials(model->GetMaterials());
    if (renderer->GetMaterials().empty()) {
        renderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
    }

    Math::Ray ray{};
    float distance = 0.0f;
    SceneViewport* sceneViewport = context.GetSceneViewport();
    if (sceneViewport && sceneViewport->BuildRayFromScreen(screenX, screenY, ray) &&
        std::fabs(ray.direction.y) > 1e-5f && (distance = -ray.origin.y / ray.direction.y) > 0.0f) {
        actor->GetTransform().position = ray.At(distance);
    } else if (sceneViewport) {
        Camera& camera = sceneViewport->GetCamera();
        actor->GetTransform().position = camera.GetPosition() + camera.GetForward() * 8.0f;
    }

    const uint64_t actorID = actor->GetID();
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand("Drop Model", before, after, previousSelection, actorID), context);
}
