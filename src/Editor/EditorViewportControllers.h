#pragma once

#include "Editor/EditorLayout.h"
#include "Scene/Transform.h"

#include <cstdint>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#include <ImGuizmo.h>
#endif

class Actor;
class EditorContext;

struct EditorGizmoState {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE mode = ImGuizmo::LOCAL;
#endif
};

class EditorPickingController {
public:
    void Pick(EditorContext& context, float screenX, float screenY) const;
};

class EditorGizmoController {
public:
    static bool ComputeLocalMatrix(const Mat4& world, const Mat4* parentWorld, Mat4& local);
    void DrawAndApply(EditorContext& context, Actor& actor, const EditorPanelRect& viewportRect,
                      const EditorGizmoState& state);
    void FinishInteraction(EditorContext& context);

private:
    bool Commit(EditorContext& context);

    uint64_t m_ActiveActorID = 0;
    Transform m_InitialTransform;
    bool m_WasUsing = false;
};
