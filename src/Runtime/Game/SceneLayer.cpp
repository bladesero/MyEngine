#include "Game/SceneLayer.h"
#include "Core/Logger.h"

// --------------------------------------------------------------------------
SceneLayer::SceneLayer(const std::string& layerName)
    : Layer(layerName)
    , m_EditorScene(std::make_unique<Scene>("Untitled"))
{
}

// --------------------------------------------------------------------------
// Layer 生命周期
// --------------------------------------------------------------------------

void SceneLayer::OnAttach()
{
    Logger::Info("[SceneLayer] '", Name(), "' attached — scene: '",
                 m_EditorScene->GetName(), "'");
    OnSceneLoaded();
}

void SceneLayer::OnDetach()
{
    OnSceneUnloaded();
    Logger::Info("[SceneLayer] '", Name(), "' detached");
}

void SceneLayer::OnUpdate(float dt)
{
    if (m_RunState == SceneRunState::Play || m_StepRequested) {
        Scene* playScene = GetPlayScene();
        if (!playScene) {
            m_StepRequested = false;
            return;
        }
        if (m_StepRequested) playScene->Resume();
        playScene->OnUpdate(dt);
        if (m_StepRequested) playScene->Pause();
        m_StepRequested = false;
    }
}

void SceneLayer::OnEvent(Event& event)
{
    (void)event;
    // 默认不消费事件；子类可重写并设置 event.handled = true
}

// --------------------------------------------------------------------------
// Scene 文件操作
// --------------------------------------------------------------------------

void SceneLayer::NewScene(const std::string& sceneName)
{
    if (!IsEditing()) StopPlay();
    OnSceneUnloaded();

    m_EditorScene    = std::make_unique<Scene>(sceneName);
    m_SceneFilePath  = "";
    m_Dirty          = false;

    Logger::Info("[SceneLayer] New scene: '", sceneName, "'");
    OnSceneLoaded();
}

bool SceneLayer::LoadScene(const std::string& filepath)
{
    if (!IsEditing()) StopPlay();
    OnSceneUnloaded();

    auto newScene = std::make_unique<Scene>();
    if (!SceneSerializer::LoadFromFile(*newScene, filepath)) {
        // 加载失败，恢复空场景
        m_EditorScene = std::make_unique<Scene>("Untitled");
        Logger::Error("[SceneLayer] Failed to load: ", filepath);
        return false;
    }

    m_EditorScene   = std::move(newScene);
    m_SceneFilePath = filepath;
    m_Dirty         = false;

    OnSceneLoaded();
    return true;
}

bool SceneLayer::SaveScene()
{
    if (m_SceneFilePath.empty()) {
        Logger::Warn("[SceneLayer] SaveScene(): no file path set, use SaveSceneAs()");
        return false;
    }
    return SaveSceneAs(m_SceneFilePath);
}

bool SceneLayer::SaveSceneAs(const std::string& filepath)
{
    if (!IsEditing()) {
        Logger::Warn("[SceneLayer] Runtime scene cannot be saved; stop Play mode first");
        return false;
    }
    if (!SceneSerializer::SaveToFile(*m_EditorScene, filepath)) {
        Logger::Error("[SceneLayer] Failed to save: ", filepath);
        return false;
    }
    m_SceneFilePath = filepath;
    m_Dirty         = false;
    return true;
}

Scene& SceneLayer::GetSimulationScene()
{
    return m_PlayScene ? *m_PlayScene : *m_EditorScene;
}

const Scene& SceneLayer::GetSimulationScene() const
{
    return m_PlayScene ? *m_PlayScene : *m_EditorScene;
}

std::unique_ptr<Scene> SceneLayer::CloneSceneFromJson(const std::string& json) const
{
    auto clone = std::make_unique<Scene>();
    if (!SceneSerializer::LoadFromString(*clone, json)) return nullptr;
    return clone;
}

bool SceneLayer::BeginPlay()
{
    if (m_RunState != SceneRunState::Edit) {
        if (m_RunState == SceneRunState::Pause) ResumePlay();
        return true;
    }

    const std::string editSceneSnapshot = SceneSerializer::SaveToString(*m_EditorScene);
    m_EditDirtySnapshot = m_Dirty;
    m_PlayScene = CloneSceneFromJson(editSceneSnapshot);
    if (!m_PlayScene) {
        Logger::Error("[SceneLayer] Failed to clone scene for Play mode");
        return false;
    }
    m_RunState = SceneRunState::Play;
    m_PlayScene->BeginPlay();
    m_StepRequested = false;
    Logger::Info("[SceneLayer] Enter Play mode");
    return true;
}

void SceneLayer::StopPlay()
{
    if (m_RunState == SceneRunState::Edit) return;
    if (m_PlayScene) m_PlayScene->EndPlay();
    m_PlayScene.reset();
    m_Dirty = m_EditDirtySnapshot;
    m_RunState = SceneRunState::Edit;
    m_StepRequested = false;
    Logger::Info("[SceneLayer] Return to Edit mode");
}

void SceneLayer::PausePlay()
{
    if (m_RunState == SceneRunState::Play) {
        m_RunState = SceneRunState::Pause;
        if (m_PlayScene) m_PlayScene->Pause();
        Logger::Info("[SceneLayer] Play mode paused");
    }
}

void SceneLayer::ResumePlay()
{
    if (m_RunState == SceneRunState::Pause) {
        m_RunState = SceneRunState::Play;
        if (m_PlayScene) m_PlayScene->Resume();
        Logger::Info("[SceneLayer] Play mode resumed");
    }
}

bool SceneLayer::StepPlay()
{
    if (m_RunState == SceneRunState::Edit && !BeginPlay()) return false;
    m_RunState = SceneRunState::Pause;
    if (m_PlayScene) m_PlayScene->Pause();
    m_StepRequested = true;
    return true;
}
