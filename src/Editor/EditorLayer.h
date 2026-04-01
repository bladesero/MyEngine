#pragma once

#include "Core/Layer.h"
#include "Game/SceneRenderLayer.h"
#include "Scene/SceneSerializer.h"
#include "Core/Engine.h"
#include "Core/Window.h"
#include "Core/PlatformEventBridge.h"
#include "Renderer/IRenderContext.h"
#include <memory>

// Forward-declare ImGui to avoid hard dependency when disabled.
#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

// ============================================================================
// EditorLayer  –  ImGui-based editor UI on top of runtime
//
//  - Toolbar: New / Open Scene
//  - Scene Outliner: tree view of all actors in the current scene
//
//  Assumes there is a SceneRenderLayer driving the runtime scene + camera.
// ============================================================================

class EditorLayer : public Layer {
public:
    EditorLayer(SceneRenderLayer* sceneLayer, IWindow* window, Engine* engine);

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float dt) override { (void)dt; }
    void OnEvent(Event& e) override { (void)e; }
    void OnRender() override;

private:
    void DrawToolbar();
    void DrawSceneOutliner();
    void DrawInspector();
    void DrawActorNode(Actor* actor);

    SceneRenderLayer* m_SceneLayer = nullptr;
    IRenderContext*   m_RenderContext = nullptr;
    IWindow*          m_Window     = nullptr;
    Engine*           m_Engine     = nullptr;
    Actor*            m_Selected   = nullptr;

#if defined(MYENGINE_ENABLE_IMGUI)
    class ImGuiPlatformEventBridge;
    std::unique_ptr<ImGuiPlatformEventBridge> m_PlatformBridge;
    bool m_ImGuiReady = false;
#endif
};

