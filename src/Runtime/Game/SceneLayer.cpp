#include "Game/SceneLayer.h"
#include "Core/Logger.h"

// --------------------------------------------------------------------------
SceneLayer::SceneLayer(const std::string& layerName)
    : Layer(layerName)
    , m_Scene(std::make_unique<Scene>("Untitled"))
{
}

// --------------------------------------------------------------------------
// Layer 生命周期
// --------------------------------------------------------------------------

void SceneLayer::OnAttach()
{
    Logger::Info("[SceneLayer] '", Name(), "' attached — scene: '",
                 m_Scene->GetName(), "'");
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
        m_Scene->OnUpdate(dt);
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

    m_Scene          = std::make_unique<Scene>(sceneName);
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
        m_Scene = std::make_unique<Scene>("Untitled");
        Logger::Error("[SceneLayer] Failed to load: ", filepath);
        return false;
    }

    m_Scene         = std::move(newScene);
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
    if (!SceneSerializer::SaveToFile(*m_Scene, filepath)) {
        Logger::Error("[SceneLayer] Failed to save: ", filepath);
        return false;
    }
    m_SceneFilePath = filepath;
    m_Dirty         = false;
    return true;
}

bool SceneLayer::CloneSceneFromJson(const std::string& json)
{
    auto clone = std::make_unique<Scene>();
    if (!SceneSerializer::LoadFromString(*clone, json)) return false;
    OnSceneUnloaded();
    m_Scene = std::move(clone);
    OnSceneLoaded();
    return true;
}

bool SceneLayer::BeginPlay()
{
    if (m_RunState != SceneRunState::Edit) {
        if (m_RunState == SceneRunState::Pause) ResumePlay();
        return true;
    }

    m_EditSceneSnapshot = SceneSerializer::SaveToString(*m_Scene);
    m_EditDirtySnapshot = m_Dirty;
    if (!CloneSceneFromJson(m_EditSceneSnapshot)) {
        m_EditSceneSnapshot.clear();
        Logger::Error("[SceneLayer] Failed to clone scene for Play mode");
        return false;
    }
    m_RunState = SceneRunState::Play;
    m_StepRequested = false;
    Logger::Info("[SceneLayer] Enter Play mode");
    return true;
}

void SceneLayer::StopPlay()
{
    if (m_RunState == SceneRunState::Edit) return;
    const std::string snapshot = std::move(m_EditSceneSnapshot);
    m_EditSceneSnapshot.clear();
    if (!snapshot.empty() && !CloneSceneFromJson(snapshot)) {
        Logger::Error("[SceneLayer] Failed to restore Edit scene");
    }
    m_Dirty = m_EditDirtySnapshot;
    m_RunState = SceneRunState::Edit;
    m_StepRequested = false;
    Logger::Info("[SceneLayer] Return to Edit mode");
}

void SceneLayer::PausePlay()
{
    if (m_RunState == SceneRunState::Play) {
        m_RunState = SceneRunState::Pause;
        Logger::Info("[SceneLayer] Play mode paused");
    }
}

void SceneLayer::ResumePlay()
{
    if (m_RunState == SceneRunState::Pause) {
        m_RunState = SceneRunState::Play;
        Logger::Info("[SceneLayer] Play mode resumed");
    }
}

bool SceneLayer::StepPlay()
{
    if (m_RunState == SceneRunState::Edit && !BeginPlay()) return false;
    m_RunState = SceneRunState::Pause;
    m_StepRequested = true;
    return true;
}
