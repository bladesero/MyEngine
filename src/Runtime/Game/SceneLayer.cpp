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
    m_Scene->OnUpdate(dt);
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
    OnSceneUnloaded();

    m_Scene          = std::make_unique<Scene>(sceneName);
    m_SceneFilePath  = "";
    m_Dirty          = false;

    Logger::Info("[SceneLayer] New scene: '", sceneName, "'");
    OnSceneLoaded();
}

bool SceneLayer::LoadScene(const std::string& filepath)
{
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
    if (!SceneSerializer::SaveToFile(*m_Scene, filepath)) {
        Logger::Error("[SceneLayer] Failed to save: ", filepath);
        return false;
    }
    m_SceneFilePath = filepath;
    m_Dirty         = false;
    return true;
}
