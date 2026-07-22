#include "Editor/EditorPanels.h"

#include "Camera/Camera.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImGuiBackend.h"
#include "Editor/EditorOperators.h"
#include "Game/SceneRenderLayer.h"
#include "Game/SceneViewportController.h"
#include "Game/GameViewport.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr const char kModelPayload[] = "MYENGINE_MODEL_PATH";
constexpr const char kPrefabPayload[] = "MYENGINE_PREFAB_PATH";

#if defined(MYENGINE_ENABLE_IMGUI)
struct AxisGizmoEndpoint {
    Vec3 axis;
    SceneViewDirection direction;
    const char* label;
    ImU32 color;
    ImVec2 pos;
    float depth = 0.0f;
    float radius = 8.0f;
};

ImU32 AxisColor(float r, float g, float b, float a = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32({r, g, b, a});
}

ImU32 DimAxisColor(ImU32 color) {
    const ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    return ImGui::ColorConvertFloat4ToU32({value.x * 0.55f, value.y * 0.55f, value.z * 0.55f, value.w * 0.85f});
}

ImVec2 ProjectAxisToGizmo(const Vec3& axis, const Camera& camera, const ImVec2& center, float radius) {
    const float x = axis.Dot(camera.GetRight());
    const float y = -axis.Dot(camera.GetCamUp());
    return {center.x + x * radius, center.y + y * radius};
}

bool PointInCircle(const ImVec2& point, const ImVec2& center, float radius) {
    const float dx = point.x - center.x;
    const float dy = point.y - center.y;
    return dx * dx + dy * dy <= radius * radius;
}

bool ProjectWorld(const Vec3& world, const Camera& camera, const EditorPanelRect& rect, ImVec2& screen) {
    const Vec4 clip = camera.GetViewProj().Transform(Vec4::FromVec3(world, 1.0f));
    if (clip.w <= 0.0001f)
        return false;
    const float x = clip.x / clip.w, y = clip.y / clip.w, z = clip.z / clip.w;
    if (z < 0.0f || z > 1.0f)
        return false;
    screen = {rect.x + (x * 0.5f + 0.5f) * rect.width, rect.y + (0.5f - y * 0.5f) * rect.height};
    return true;
}

void DrawNavigationOverlay(const Scene& scene, const Camera& camera, const EditorPanelRect& rect) {
    const NavigationWorld& nav = scene.GetNavigationWorld();
    if (!nav.IsBaked())
        return;
    const uint64_t total = static_cast<uint64_t>(nav.GetWidth()) * nav.GetHeight();
    const uint32_t stride = static_cast<uint32_t>(std::max<uint64_t>(1, (total + 4999) / 5000));
    const auto& settings = nav.GetSettings();
    const auto& cells = nav.GetCells();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (uint32_t z = 0; z < nav.GetHeight(); z += stride)
        for (uint32_t x = 0; x < nav.GetWidth(); x += stride) {
            const size_t index = static_cast<size_t>(z) * nav.GetWidth() + x;
            if (index >= cells.size())
                continue;
            const float size = settings.cellSize * stride;
            const Vec3 a(settings.bounds.min.x + x * settings.cellSize, settings.bounds.min.y + 0.03f,
                         settings.bounds.min.z + z * settings.cellSize),
                b = a + Vec3(size, 0, 0), c = a + Vec3(0, 0, size);
            ImVec2 sa, sb, sc;
            if (!ProjectWorld(a, camera, rect, sa))
                continue;
            const ImU32 color = cells[index] ? IM_COL32(60, 210, 110, 105) : IM_COL32(235, 70, 70, 135);
            if (ProjectWorld(b, camera, rect, sb))
                draw->AddLine(sa, sb, color, 1.0f);
            if (ProjectWorld(c, camera, rect, sc))
                draw->AddLine(sa, sc, color, 1.0f);
        }
}

ImVec2 ToPlatformWindowLocal(const ImVec2& screenPos) {
    if (const ImGuiViewport* viewport = ImGui::GetWindowViewport()) {
        return {screenPos.x - viewport->Pos.x, screenPos.y - viewport->Pos.y};
    }
    return screenPos;
}
#endif
} // namespace

void SceneViewportPanel::DropPrefab(const std::string& path, float screenX, float screenY) {
    EditorContext* context = GetContext();
    if (!context || !context->CanEditScene())
        return;
    Transform placement;
    Math::Ray ray{};
    float distance = 0.0f;
    auto* sceneViewport = context->GetSceneViewport();
    if (sceneViewport && sceneViewport->BuildRayFromScreen(screenX, screenY, ray) &&
        std::fabs(ray.direction.y) > 1e-5f && (distance = -ray.origin.y / ray.direction.y) > 0.0f)
        placement.position = ray.At(distance);
    else if (sceneViewport) {
        Camera& camera = sceneViewport->GetCamera();
        placement.position = camera.GetPosition() + camera.GetForward() * 8.0f;
    }
    if (auto* operators = context->GetOperators()) {
        operators->Prefabs().InstantiatePrefab(*context, path, 0, placement, "Drop Prefab");
    }
}

SceneViewportPanel::SceneViewportPanel(std::shared_ptr<EditorGizmoState> state)
    : EditorPanel("viewport", "Scene View"), m_State(std::move(state)) {
}

void SceneViewportPanel::OnUpdate(float) {
    EditorContext* context = GetContext();
    auto* layer = context ? context->GetSceneLayer() : nullptr;
    if (context && layer && layer->IsSceneViewportActive())
        m_LightGizmoController.Submit(*context);
}

int SceneViewportPanel::GetWindowFlags() const {
#if defined(MYENGINE_ENABLE_IMGUI)
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;
#else
    return 0;
#endif
}

void SceneViewportPanel::BeforeBegin() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
#endif
}

void SceneViewportPanel::AfterEnd() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::PopStyleVar();
#endif
}

void SceneViewportPanel::DropModel(const std::string& path, float screenX, float screenY) {
    EditorContext* context = GetContext();
    if (!context || !context->CanEditScene())
        return;
    if (auto* operators = context->GetOperators()) {
        operators->Viewport().DropModel(*context, path, screenX, screenY);
    }
}

bool SceneViewportPanel::DrawSceneViewOverlay(EditorContext& context, SceneViewport& viewport, EditorGizmoState& state,
                                              const EditorPanelRect& rect) {
#if defined(MYENGINE_ENABLE_IMGUI)
    bool hovered = false;
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    const bool hasPlayWorld = context.GetSceneLayer() && !context.GetSceneLayer()->IsEditing();
    const bool inspectingPlay = context.IsInspectingPlayWorld();

    ImGui::SetNextWindowPos({rect.x + 10.0f, rect.y + 10.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.45f);
    if (ImGui::Begin("Scene View Overlay###scene_view_overlay_left", nullptr, flags)) {
        hovered |= ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (!hasPlayWorld)
            ImGui::BeginDisabled();
        if (ImGui::SmallButton(inspectingPlay ? "PlayWorld" : "EditorWorld")) {
            context.SetSceneViewMode(inspectingPlay ? EditorWorldViewMode::EditorWorld
                                                    : EditorWorldViewMode::PlayWorldInspect);
        }
        if (!hasPlayWorld)
            ImGui::EndDisabled();
        if (inspectingPlay) {
            ImGui::SameLine();
            ImGui::TextColored({1.0f, 0.75f, 0.25f, 1.0f}, "Read-only");
        }

        ImGui::Separator();
        if (SceneRenderLayer* sceneLayer = context.GetSceneLayer()) {
            static constexpr const char* debugViews[] = {
                "Final",           "HDR Lighting",      "HiZ Min/Max", "Motion Vectors", "SSGI",       "SSR Confidence",
                "TAA History Age", "TAA Reject Reason", "RT Shadow",   "RT AO",          "RT Diffuse", "RT Reflection"};
            int debugView = static_cast<int>(sceneLayer->GetSceneDebugView());
            ImGui::SetNextItemWidth(145.0f);
            if (ImGui::Combo("Debug View", &debugView, debugViews, static_cast<int>(std::size(debugViews))))
                sceneLayer->SetSceneDebugView(static_cast<RendererDebugView>(debugView));
            if (debugView == static_cast<int>(RendererDebugView::TAAHistoryAge))
                ImGui::TextDisabled("Red: 1  Yellow: 60  Green: 120+");
            else if (debugView == static_cast<int>(RendererDebugView::TAARejectReason))
                ImGui::TextDisabled(
                    "Green direct | Cyan rescue | Teal retained | Purple motion guard | Red depth | Magenta normal");
        }
        ImGui::Separator();
        const bool canEditSelection = context.CanEditSelection();
        if (!canEditSelection)
            ImGui::BeginDisabled();
        auto drawOperationButton = [&state](const char* label, ImGuizmo::OPERATION operation) {
            const bool active = state.operation == operation;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, {0.20f, 0.45f, 0.82f, 1.0f});
            if (ImGui::SmallButton(label))
                state.operation = operation;
            if (active)
                ImGui::PopStyleColor();
        };
        drawOperationButton("Move", ImGuizmo::TRANSLATE);
        ImGui::SameLine();
        drawOperationButton("Rotate", ImGuizmo::ROTATE);
        ImGui::SameLine();
        drawOperationButton("Scale", ImGuizmo::SCALE);
        if (!canEditSelection)
            ImGui::EndDisabled();
    }
    ImGui::End();

    ImGui::SetNextWindowPos({rect.x + rect.width - 10.0f, rect.y + 10.0f}, ImGuiCond_Always, {1.0f, 0.0f});
    ImGui::SetNextWindowBgAlpha(0.45f);
    if (ImGui::Begin("Scene View Control Gizmos###scene_view_overlay_right", nullptr, flags)) {
        hovered |= ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        constexpr float kGizmoSize = 112.0f;
        constexpr float kAxisLength = 34.0f;
        constexpr float kCenterRadius = 34.0f;
        constexpr float kEndpointRadius = 10.0f;

        const ImVec2 itemMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##scene_view_axis_gizmo", {kGizmoSize, kGizmoSize});
        const bool itemHovered = ImGui::IsItemHovered();
        const bool itemActive = ImGui::IsItemActive();
        hovered |= itemHovered || itemActive;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 center{itemMin.x + kGizmoSize * 0.5f, itemMin.y + kGizmoSize * 0.5f};
        const Camera& camera = viewport.GetCamera();
        const ImU32 red = AxisColor(0.92f, 0.16f, 0.26f);
        const ImU32 green = AxisColor(0.42f, 0.78f, 0.05f);
        const ImU32 blue = AxisColor(0.18f, 0.48f, 0.88f);
        std::array<AxisGizmoEndpoint, 6> endpoints{
            {{Vec3::Right(), SceneViewDirection::Right, "X", red},
             {-Vec3::Right(), SceneViewDirection::Left, "", DimAxisColor(red)},
             {Vec3::Up(), SceneViewDirection::Top, "Y", green},
             {-Vec3::Up(), SceneViewDirection::Bottom, "", DimAxisColor(green)},
             {Vec3::Forward(), SceneViewDirection::Back, "Z", blue},
             {-Vec3::Forward(), SceneViewDirection::Front, "", DimAxisColor(blue)}}};
        for (auto& endpoint : endpoints) {
            endpoint.pos = ProjectAxisToGizmo(endpoint.axis, camera, center, kAxisLength);
            endpoint.depth = endpoint.axis.Dot(camera.GetForward());
            endpoint.radius = endpoint.label[0] ? kEndpointRadius : kEndpointRadius * 0.82f;
        }
        std::sort(endpoints.begin(), endpoints.end(),
                  [](const AxisGizmoEndpoint& a, const AxisGizmoEndpoint& b) { return a.depth < b.depth; });

        drawList->AddCircleFilled(center, kCenterRadius, AxisColor(0.55f, 0.58f, 0.63f, 0.30f), 36);
        drawList->AddCircle(center, kCenterRadius, AxisColor(0.78f, 0.80f, 0.84f, 0.25f), 36, 1.0f);
        for (const auto& endpoint : endpoints) {
            drawList->AddLine(center, endpoint.pos, endpoint.color, 2.5f);
        }
        drawList->AddCircleFilled(center, 4.0f, AxisColor(0.82f, 0.84f, 0.88f, 0.90f), 16);
        for (const auto& endpoint : endpoints) {
            drawList->AddCircleFilled(endpoint.pos, endpoint.radius, endpoint.color, 24);
            drawList->AddCircle(endpoint.pos, endpoint.radius, AxisColor(0.04f, 0.05f, 0.06f, 0.35f), 24, 1.0f);
            if (endpoint.label[0]) {
                const ImVec2 textSize = ImGui::CalcTextSize(endpoint.label);
                drawList->AddText({endpoint.pos.x - textSize.x * 0.5f, endpoint.pos.y - textSize.y * 0.5f},
                                  AxisColor(0.05f, 0.06f, 0.08f), endpoint.label);
            }
        }

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (itemHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            for (auto it = endpoints.rbegin(); it != endpoints.rend(); ++it) {
                if (PointInCircle(mouse, it->pos, it->radius + 3.0f)) {
                    if (auto* operators = context.GetOperators()) {
                        operators->Viewport().FrameDirection(context, it->direction);
                    }
                    break;
                }
            }
        }
        if (itemActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            if (auto* operators = context.GetOperators()) {
                operators->Viewport().OrbitAroundSelection(context, -delta.x * 0.35f, -delta.y * 0.35f);
            }
        }

        if (ImGui::SmallButton(viewport.IsOrthographic() ? "Ortho" : "Persp")) {
            if (auto* operators = context.GetOperators()) {
                operators->Viewport().ToggleSceneProjection(context);
            }
        }
    }
    ImGui::End();

    return hovered;
#else
    (void)context;
    (void)viewport;
    (void)state;
    (void)rect;
    return false;
#endif
}

void SceneViewportPanel::DrawContent() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (TryDrawScriptedBody("viewport"))
        return;

    EditorContext* context = GetContext();
    if (!context || !m_State)
        return;
    auto* sceneViewport = context->GetSceneViewport();
    if (!sceneViewport)
        return;

    const ImVec2 imageMin = ImGui::GetCursorScreenPos();
    const ImVec2 imageSize = ImGui::GetContentRegionAvail();
    if (imageSize.x <= 1.0f || imageSize.y <= 1.0f) {
        sceneViewport->SetInputEnabled(false);
        return;
    }
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    const EditorPanelRect imageRect{imageMin.x, imageMin.y, imageSize.x, imageSize.y};

    if (auto* layer = context->GetSceneLayer()) {
        layer->SetSceneViewportActive(true);
    }
    sceneViewport->SetViewportRect(static_cast<int>(imageMin.x), static_cast<int>(imageMin.y),
                                   static_cast<int>(imageSize.x), static_cast<int>(imageSize.y));
    sceneViewport->SetInputEnabled(hovered);

    bool drewImage = false;
    GpuTextureView* outputView = sceneViewport->GetOutputView();
    if (outputView) {
        void* texture = nullptr;
        if (auto* backend = context->GetImGuiBackend())
            texture = backend->GetTextureId(outputView);
        if (texture) {
            ImGui::Image(reinterpret_cast<ImTextureID>(texture), imageSize);
            drewImage = true;
        }
    }
    if (!drewImage) {
        ImGui::Dummy(imageSize);
    }
    if (Scene* scene = context->GetInspectorScene())
        DrawNavigationOverlay(*scene, sceneViewport->GetCamera(), imageRect);

    if (context->CanEditScene() && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kModelPayload)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            DropModel(static_cast<const char*>(payload->Data), mouse.x, mouse.y);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPrefabPayload)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            DropPrefab(static_cast<const char*>(payload->Data), mouse.x, mouse.y);
        }
        ImGui::EndDragDropTarget();
    }

    const bool overlayBlocksViewportInput = DrawSceneViewOverlay(*context, *sceneViewport, *m_State, imageRect);

    Scene* inspectionScene = context->GetInspectorScene();
    Actor* actor = inspectionScene ? context->GetSelection().ResolveActor(*inspectionScene) : nullptr;
    if (!overlayBlocksViewportInput && context->CanEditSelection() && actor) {
        m_GizmoController.DrawAndApply(*context, *actor, imageRect, *m_State);
    } else {
        m_GizmoController.FinishInteraction(*context);
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !overlayBlocksViewportInput && !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        m_PickingController.Pick(*context, mouse.x, mouse.y);
    }
#endif
}

GameViewportPanel::GameViewportPanel() : EditorPanel("gameViewport", "Game View") {
}

int GameViewportPanel::GetWindowFlags() const {
#if defined(MYENGINE_ENABLE_IMGUI)
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;
#else
    return 0;
#endif
}

void GameViewportPanel::BeforeBegin() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
#endif
}

void GameViewportPanel::AfterEnd() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::PopStyleVar();
#endif
}

void GameViewportPanel::DrawContent() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (TryDrawScriptedBody("gameViewport"))
        return;

    EditorContext* context = GetContext();
    if (!context)
        return;
    auto* gameViewport = context->GetGameViewport();
    if (!gameViewport)
        return;

    const ImVec2 imageMin = ImGui::GetCursorScreenPos();
    const ImVec2 imageSize = ImGui::GetContentRegionAvail();
    if (imageSize.x <= 1.0f || imageSize.y <= 1.0f) {
        gameViewport->SetInputEnabled(false);
        return;
    }
    const ImVec2 imageMinLocal = ToPlatformWindowLocal(imageMin);
    if (auto* layer = context->GetSceneLayer()) {
        layer->SetGameViewportActive(true);
    }
    gameViewport->SetViewportRect(static_cast<int>(imageMinLocal.x), static_cast<int>(imageMinLocal.y),
                                  static_cast<int>(imageSize.x), static_cast<int>(imageSize.y));
    gameViewport->SetInputEnabled(false);
    if (auto* layer = context->GetSceneLayer()) {
        UIInputViewport viewport;
        viewport.x = static_cast<int>(imageMinLocal.x);
        viewport.y = static_cast<int>(imageMinLocal.y);
        viewport.width = static_cast<int>(imageSize.x);
        viewport.height = static_cast<int>(imageSize.y);
        viewport.enabled = true;
        viewport.hovered = true;
        layer->SetUIInputViewport(viewport);
    }

    bool drewImage = false;
    if (GpuTextureView* view = gameViewport->GetOutputView()) {
        void* texture = nullptr;
        if (auto* backend = context->GetImGuiBackend())
            texture = backend->GetTextureId(view);
        if (texture) {
            ImGui::Image(reinterpret_cast<ImTextureID>(texture), imageSize);
            drewImage = true;
        }
    }
    if (!drewImage) {
        ImGui::Dummy(imageSize);
    }
    if (!gameViewport->HasMainCamera()) {
        ImGui::SetCursorScreenPos({imageMin.x + 12.0f, imageMin.y + 12.0f});
        ImGui::TextColored({1.0f, 0.75f, 0.25f, 1.0f}, "No Main Camera Component");
    }
#endif
}
