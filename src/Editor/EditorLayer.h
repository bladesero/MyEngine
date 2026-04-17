#pragma once

#include "Core/Layer.h"
#include "Game/SceneRenderLayer.h"
#include "Scene/SceneSerializer.h"
#include "Core/Engine.h"
#include "Core/Window.h"
#include "Core/PlatformEventBridge.h"
#include "Renderer/IRenderContext.h"
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#include <ImGuizmo.h>
#endif

// ============================================================================
// EditorLayer  –  ImGui-based editor UI on top of runtime
//
//  - Toolbar: New / Open / Save Scene (SDL3 file dialogs)
//  - Scene Outliner: tree view, create/delete actors
//  - Scene View: viewport rect, picking, ImGuizmo
//  - Inspector: Transform + MeshRenderer mesh/material
//
//  Assumes there is a SceneRenderLayer driving the runtime scene + camera.
// ============================================================================

class EditorLayer : public Layer {
public:
    EditorLayer(SceneRenderLayer* sceneLayer, IWindow* window, Engine* engine);

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float dt) override;
    void OnEvent(Event& e) override { (void)e; }
    void OnRender() override;

private:
    void DrawToolbar();
    void DrawSceneOutliner();
    void DrawSceneView();
    void DrawInspector();
    void DrawLogOutput();
    void DrawAssetBrowser();
    void DrawActorNode(Actor* actor);
    void OnLogMessage(const std::string& line);

#if defined(MYENGINE_ENABLE_IMGUI)
    friend void EditorOpenFileDialogCallback(void* userdata, const char* const* filelist, int filter);
    friend void EditorSaveFileDialogCallback(void* userdata, const char* const* filelist, int filter);

    void ProcessPendingFileDialogs();
    void RefreshAssetBrowserListing();
    void TryCreateMeshActorFromDroppedObj(const std::string& absObjPath, float screenX, float screenY);
    void RequestOpenSceneDialog();
    void RequestSaveSceneDialog();
    void TryPickActorFromSceneView(float screenX, float screenY);
    void DrawMeshMaterialInspector(Actor* actor);
    void RefreshShaderWatchList();
    void PollShaderChanges();

    enum class PendingFileOp : uint8_t {
        None,
        OpenScene,
        SaveScene,
    };

    std::mutex        m_FileDialogMutex;
    std::string       m_PendingFilePath;
    PendingFileOp     m_PendingFileOp = PendingFileOp::None;

    ImGuizmo::OPERATION m_GizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      m_GizmoMode      = ImGuizmo::LOCAL;
#endif

    SceneRenderLayer* m_SceneLayer = nullptr;
    IRenderContext*   m_RenderContext = nullptr;
    IWindow*          m_Window     = nullptr;
    Engine*           m_Engine     = nullptr;
    Actor*            m_Selected   = nullptr;
    std::deque<std::string> m_LogLines;
    std::mutex        m_LogMutex;
    bool              m_LogAutoScroll = true;
    bool              m_LogScrollToBottom = false;

#if defined(MYENGINE_ENABLE_IMGUI)
    class ImGuiPlatformEventBridge;
    std::unique_ptr<ImGuiPlatformEventBridge> m_PlatformBridge;
    bool m_ImGuiReady = false;

    std::vector<std::string> m_AssetBrowserRelPaths;
    std::string              m_AssetBrowserRootAbs;
    std::vector<std::string> m_WatchedShaders;
    std::unordered_map<std::string, std::filesystem::file_time_type> m_ShaderWriteTimes;
    float m_ShaderWatchAccumulator = 0.0f;
#endif
};
