#include "Game/SceneLayer.h"
#include "Core/Logger.h"
#include "Assets/AssetManager.h"

// --------------------------------------------------------------------------
SceneLayer::SceneLayer(const std::string& layerName)
    : Layer(layerName), m_EditorScene(std::make_unique<Scene>("Untitled")) {
    m_EditorScene->SetSceneManager(&m_SceneManager);
    m_EditorScene->SetGameFlowController(&m_GameFlowController);
    m_GameFlowController.EnterBoot(nullptr);
}

// --------------------------------------------------------------------------
// Layer 生命周期
// --------------------------------------------------------------------------

void SceneLayer::OnAttach() {
    Logger::Info("[SceneLayer] '", Name(), "' attached — scene: '", m_EditorScene->GetName(), "'");
    OnSceneLoaded();
}

void SceneLayer::OnDetach() {
    OnSceneUnloaded();
    Logger::Info("[SceneLayer] '", Name(), "' detached");
}

void SceneLayer::OnUpdate(float dt) {
    if (m_PlayScene && m_SceneManager.IsLoading() && m_GameFlowController.GetState() != GameFlowState::Loading)
        m_GameFlowController.BeginLoading(m_PlayScene.get());
    std::unique_ptr<Scene> requestedScene;
    if (m_SceneManager.Process(requestedScene) && requestedScene) {
        OnSceneUnloaded();
        if (IsEditing()) {
            m_EditorScene = std::move(requestedScene);
            m_EditorScene->SetSceneManager(&m_SceneManager);
            m_EditorScene->SetGameFlowController(&m_GameFlowController);
            m_SceneFilePath = (AssetManager::Get().GetProjectRoot() / m_SceneManager.GetRequestedPath()).string();
            m_Dirty = false;
        } else {
            if (m_PlayScene)
                m_PlayScene->EndPlay();
            m_PlayScene = std::move(requestedScene);
            m_PlayScene->SetSceneManager(&m_SceneManager);
            m_PlayScene->SetGameFlowController(&m_GameFlowController);
            m_PlayScene->BeginPlay();
            m_RunState = SceneRunState::Play;
            m_GameFlowController.FinishLoading(m_PlayScene.get(), true);
        }
        OnSceneLoaded();
        AssetCacheBudget budget;
        budget.cpuHighWatermarkBytes = AssetManager::Get().GetAssetCpuBudgetBytes();
        AssetManager::Get().CollectGarbage(budget);
    }
    const SceneLoadState loadState = m_SceneManager.GetState();
    if (loadState == SceneLoadState::Failed && m_LastObservedLoadState != SceneLoadState::Failed)
        m_GameFlowController.FinishLoading(m_PlayScene.get(), false);
    m_LastObservedLoadState = loadState;
    const bool loadInProgress = m_SceneManager.IsLoading();
    if (!loadInProgress && (m_RunState == SceneRunState::Play || m_StepRequested)) {
        Scene* playScene = GetPlayScene();
        if (!playScene) {
            m_StepRequested = false;
            return;
        }
        if (m_StepRequested)
            playScene->Resume();
        playScene->OnUpdate(dt);
        if (m_StepRequested)
            playScene->Pause();
        m_StepRequested = false;
    }
}

void SceneLayer::OnEvent(Event& event) {
    if (event.type == EventType::WindowFocusLost) {
        m_WindowFocused = false;
        if (m_PauseWhenUnfocused && m_PlayScene)
            m_GameFlowController.RequestPause(GamePauseReason::WindowInactive, m_PlayScene.get());
    } else if (event.type == EventType::WindowFocusGained) {
        m_WindowFocused = true;
        if (m_PlayScene)
            m_GameFlowController.ReleasePause(GamePauseReason::WindowInactive, m_PlayScene.get());
    }
}

// --------------------------------------------------------------------------
// Scene 文件操作
// --------------------------------------------------------------------------

void SceneLayer::NewScene(const std::string& sceneName) {
    if (!IsEditing())
        StopPlay();
    OnSceneUnloaded();

    m_EditorScene = std::make_unique<Scene>(sceneName);
    m_EditorScene->SetSceneManager(&m_SceneManager);
    m_EditorScene->SetGameFlowController(&m_GameFlowController);
    m_SceneFilePath = "";
    m_Dirty = false;

    Logger::Info("[SceneLayer] New scene: '", sceneName, "'");
    OnSceneLoaded();
}

bool SceneLayer::LoadScene(const std::string& filepath) {
    if (!IsEditing())
        StopPlay();
    auto newScene = std::make_unique<Scene>();
    if (!SceneSerializer::LoadFromFile(*newScene, filepath)) {
        // 加载失败，恢复空场景
        Logger::Error("[SceneLayer] Failed to load: ", filepath);
        return false;
    }

    OnSceneUnloaded();
    m_EditorScene = std::move(newScene);
    m_EditorScene->SetSceneManager(&m_SceneManager);
    m_EditorScene->SetGameFlowController(&m_GameFlowController);
    m_SceneFilePath = filepath;
    m_Dirty = false;

    OnSceneLoaded();
    return true;
}

bool SceneLayer::SaveScene() {
    if (m_SceneFilePath.empty()) {
        Logger::Warn("[SceneLayer] SaveScene(): no file path set, use SaveSceneAs()");
        return false;
    }
    return SaveSceneAs(m_SceneFilePath);
}

bool SceneLayer::SaveSceneAs(const std::string& filepath) {
    if (!IsEditing()) {
        Logger::Warn("[SceneLayer] Runtime scene cannot be saved; stop Play mode first");
        return false;
    }
    if (!SceneSerializer::SaveToFile(*m_EditorScene, filepath)) {
        Logger::Error("[SceneLayer] Failed to save: ", filepath);
        return false;
    }
    m_SceneFilePath = filepath;
    m_Dirty = false;
    return true;
}

bool SceneLayer::RestoreEditorSnapshot(const std::string& serializedScene, const std::string& originalFilepath) {
    if (!IsEditing())
        StopPlay();
    auto recovered = std::make_unique<Scene>();
    if (!SceneSerializer::LoadFromString(*recovered, serializedScene)) {
        Logger::Error("[SceneLayer] Failed to deserialize recovery snapshot");
        return false;
    }
    OnSceneUnloaded();
    m_EditorScene = std::move(recovered);
    m_EditorScene->SetSceneManager(&m_SceneManager);
    m_EditorScene->SetGameFlowController(&m_GameFlowController);
    m_SceneFilePath = originalFilepath;
    m_Dirty = true;
    OnSceneLoaded();
    return true;
}

Scene& SceneLayer::GetSimulationScene() {
    return m_PlayScene ? *m_PlayScene : *m_EditorScene;
}

const Scene& SceneLayer::GetSimulationScene() const {
    return m_PlayScene ? *m_PlayScene : *m_EditorScene;
}

std::unique_ptr<Scene> SceneLayer::CloneSceneFromJson(const std::string& json) const {
    auto clone = std::make_unique<Scene>();
    if (!SceneSerializer::LoadFromString(*clone, json))
        return nullptr;
    return clone;
}

bool SceneLayer::BeginPlay() {
    if (m_RunState != SceneRunState::Edit) {
        if (m_RunState == SceneRunState::Pause)
            ResumePlay();
        return true;
    }

    const std::string editSceneSnapshot = SceneSerializer::SaveToString(*m_EditorScene);
    m_EditDirtySnapshot = m_Dirty;
    m_PlayScene = CloneSceneFromJson(editSceneSnapshot);
    if (!m_PlayScene) {
        Logger::Error("[SceneLayer] Failed to clone scene for Play mode");
        return false;
    }
    m_PlayScene->SetSceneManager(&m_SceneManager);
    m_PlayScene->SetGameFlowController(&m_GameFlowController);
    m_RunState = SceneRunState::Play;
    m_PlayScene->BeginPlay();
    m_GameFlowController.EnterGameplay(m_PlayScene.get());
    if (m_PauseWhenUnfocused && !m_WindowFocused)
        m_GameFlowController.RequestPause(GamePauseReason::WindowInactive, m_PlayScene.get());
    m_StepRequested = false;
    Logger::Info("[SceneLayer] Enter Play mode");
    return true;
}

void SceneLayer::StopPlay() {
    if (m_RunState == SceneRunState::Edit)
        return;
    if (m_PlayScene)
        m_PlayScene->EndPlay();
    m_PlayScene.reset();
    m_GameFlowController.EnterBoot(nullptr);
    m_Dirty = m_EditDirtySnapshot;
    m_RunState = SceneRunState::Edit;
    m_StepRequested = false;
    Logger::Info("[SceneLayer] Return to Edit mode");
}

void SceneLayer::PausePlay() {
    if (m_RunState == SceneRunState::Play) {
        m_RunState = SceneRunState::Pause;
        m_GameFlowController.RequestPause(GamePauseReason::Editor, m_PlayScene.get());
        Logger::Info("[SceneLayer] Play mode paused");
    }
}

void SceneLayer::ResumePlay() {
    if (m_RunState == SceneRunState::Pause) {
        m_RunState = SceneRunState::Play;
        m_GameFlowController.ReleasePause(GamePauseReason::Editor, m_PlayScene.get());
        Logger::Info("[SceneLayer] Play mode resumed");
    }
}

bool SceneLayer::StepPlay() {
    if (m_RunState == SceneRunState::Edit && !BeginPlay())
        return false;
    m_RunState = SceneRunState::Pause;
    m_GameFlowController.RequestPause(GamePauseReason::Editor, m_PlayScene.get());
    m_StepRequested = true;
    return true;
}

bool SceneLayer::RequestSceneLoad(const std::string& path) {
    if (!m_SceneManager.RequestLoad(path))
        return false;
    m_LastObservedLoadState = m_SceneManager.GetState();
    if (m_PlayScene)
        m_GameFlowController.BeginLoading(m_PlayScene.get());
    return true;
}

void SceneLayer::SetPauseWhenUnfocused(bool enabled) {
    if (m_PauseWhenUnfocused == enabled)
        return;
    m_PauseWhenUnfocused = enabled;
    if (!m_PlayScene)
        return;
    if (enabled && !m_WindowFocused)
        m_GameFlowController.RequestPause(GamePauseReason::WindowInactive, m_PlayScene.get());
    else if (!enabled)
        m_GameFlowController.ReleasePause(GamePauseReason::WindowInactive, m_PlayScene.get());
}
