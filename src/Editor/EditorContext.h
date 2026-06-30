#pragma once

#include "Editor/EditorSelection.h"
#include "Renderer/IRenderContext.h"

#include <filesystem>
#include <typeindex>
#include <unordered_map>

class EditorAssetRegistry;
class EditorActionRegistry;
class EditorAngelScriptDomain;
class EditorCommandStack;
class EditorImGuiBackend;
class EditorOperators;
class EditorProject;
class EditorProfiler;
class EditorService;
class EditorShortcutMap;
class Engine;
class IWindow;
class Scene;
class SceneLayer;
class SceneRenderLayer;
class GameViewport;
class SceneViewport;

enum class EditorWorldViewMode : uint8_t {
    EditorWorld,
    PlayWorldInspect,
};

class EditorContext {
public:
    EditorContext() = default;
    explicit EditorContext(Scene* scene);
    EditorContext(SceneRenderLayer* sceneLayer, IRenderContext* renderContext,
                  IWindow* window, Engine* engine);

    // Transitional access while editor call sites move to narrower runtime services.
    SceneRenderLayer* GetSceneLayer() const { return m_SceneLayer; }
    SceneLayer* GetSceneLayerBase() const;
    SceneViewport* GetSceneViewport() const;
    GameViewport* GetGameViewport() const;
    IRenderContext* GetRenderContext() const { return m_RenderContext; }
    IWindow* GetWindow() const { return m_Window; }
    Engine* GetEngine() const { return m_Engine; }
    Scene* GetEditorScene() const;
    Scene* GetPlayScene() const;
    Scene* GetSimulationScene() const;
    EditorWorldViewMode GetSceneViewMode() const { return m_SceneViewMode; }
    void SetSceneViewMode(EditorWorldViewMode mode);
    void RefreshSceneViewMode();
    Scene* GetSceneViewScene() const;
    Scene* GetInspectorScene() const;
    bool CanEditScene() const;
    bool CanEditSelection() const;
    bool IsInspectingPlayWorld() const;
    Scene* GetScene() const;
    bool IsEditing() const;
    void MarkSceneDirty() const;

    EditorSelection& GetSelection() { return m_Selection; }
    const EditorSelection& GetSelection() const { return m_Selection; }
    void SetCommandStack(EditorCommandStack* value) { m_CommandStack = value; }
    EditorCommandStack* GetCommandStack() const { return m_CommandStack; }
    void SetAssetRegistry(EditorAssetRegistry* value) { m_AssetRegistry = value; }
    EditorAssetRegistry* GetAssetRegistry() const { return m_AssetRegistry; }
    void SetProject(EditorProject* value) { m_Project = value; }
    EditorProject* GetProject() const { return m_Project; }
    void SetProfiler(EditorProfiler* value) { m_Profiler = value; }
    EditorProfiler* GetProfiler() const { return m_Profiler; }
    void SetActionRegistry(EditorActionRegistry* value) { m_ActionRegistry = value; }
    EditorActionRegistry* GetActionRegistry() const { return m_ActionRegistry; }
    void SetShortcutMap(EditorShortcutMap* value) { m_ShortcutMap = value; }
    EditorShortcutMap* GetShortcutMap() const { return m_ShortcutMap; }
    void SetImGuiBackend(EditorImGuiBackend* backend) { m_ImGuiBackend = backend; }
    EditorImGuiBackend* GetImGuiBackend() const { return m_ImGuiBackend; }
    void SetEditorScriptDomain(EditorAngelScriptDomain* value) { m_EditorScriptDomain = value; }
    EditorAngelScriptDomain* GetEditorScriptDomain() const { return m_EditorScriptDomain; }
    void SetOperators(EditorOperators* value) { m_Operators = value; }
    EditorOperators* GetOperators() const { return m_Operators; }
    void SetProjectRoot(std::filesystem::path root);
    const std::filesystem::path& GetProjectRoot() const { return m_ProjectRoot; }
    const std::filesystem::path& GetContentRoot() const { return m_ContentRoot; }

    template <typename T> void RegisterService(T& service) {
        m_Services[std::type_index(typeid(T))] = &service;
    }
    template <typename T> T* GetService() const {
        const auto it = m_Services.find(std::type_index(typeid(T)));
        return it == m_Services.end() ? nullptr : static_cast<T*>(it->second);
    }
    void ClearServices() { m_Services.clear(); }

private:
    SceneRenderLayer* m_SceneLayer = nullptr;
    Scene* m_SceneOverride = nullptr;
    IRenderContext* m_RenderContext = nullptr;
    IWindow* m_Window = nullptr;
    Engine* m_Engine = nullptr;
    EditorCommandStack* m_CommandStack = nullptr;
    EditorAssetRegistry* m_AssetRegistry = nullptr;
    EditorProject* m_Project = nullptr;
    EditorProfiler* m_Profiler = nullptr;
    EditorActionRegistry* m_ActionRegistry = nullptr;
    EditorShortcutMap* m_ShortcutMap = nullptr;
    EditorImGuiBackend* m_ImGuiBackend = nullptr;
    EditorAngelScriptDomain* m_EditorScriptDomain = nullptr;
    EditorOperators* m_Operators = nullptr;
    EditorWorldViewMode m_SceneViewMode = EditorWorldViewMode::EditorWorld;
    std::unordered_map<std::type_index, EditorService*> m_Services;
    std::filesystem::path m_ProjectRoot;
    std::filesystem::path m_ContentRoot;
    EditorSelection m_Selection;
};
