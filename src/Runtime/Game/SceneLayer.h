#pragma once

#include "Core/Layer.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Game/SceneManager.h"
#include "Game/GameFlowController.h"
#include <memory>
#include <string>

// ==========================================================================
// SceneLayer  –  持有并驱动一个 Scene 的 Layer
//
// 职责：
//   - 在 OnUpdate 中调用 scene.OnUpdate(dt)
//   - 在 OnRender 中遍历场景（留给子类重写实现具体渲染）
//   - 提供 LoadScene / SaveScene / NewScene 接口
//   - 追踪当前打开的场景文件路径（m_SceneFilePath）
// ==========================================================================

enum class SceneRunState {
    Edit,
    Play,
    Pause,
};

class SceneLayer : public Layer {
public:
    explicit SceneLayer(const std::string& layerName = "SceneLayer");

    // ---- Layer 生命周期 -----------------------------------------------
    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float dt) override;
    void OnEvent(Event& event) override;

    // OnRender 留给子类实现（此基类不做任何渲染）
    void OnRender() override {}

    // ---- Scene 文件操作 -----------------------------------------------

    // 创建空白新场景（丢弃当前内容）
    void NewScene(const std::string& sceneName = "Untitled");

    // 从文件加载场景，成功返回 true
    bool LoadScene(const std::string& filepath);

    // 保存到当前打开的文件，若未关联文件则返回 false
    bool SaveScene();

    // 另存为指定路径
    bool SaveSceneAs(const std::string& filepath);

    // Atomically replaces the editor scene from a recovery snapshot. The
    // recovered scene remains dirty so the user must explicitly save it.
    bool RestoreEditorSnapshot(const std::string& serializedScene, const std::string& originalFilepath);

    // ---- 访问 ----------------------------------------------------------
    Scene& GetScene() { return GetSimulationScene(); }
    const Scene& GetScene() const { return GetSimulationScene(); }
    Scene& GetEditorScene() { return *m_EditorScene; }
    const Scene& GetEditorScene() const { return *m_EditorScene; }
    Scene* GetPlayScene() { return m_PlayScene.get(); }
    const Scene* GetPlayScene() const { return m_PlayScene.get(); }
    Scene& GetSimulationScene();
    const Scene& GetSimulationScene() const;
    bool HasPlayWorld() const { return m_PlayScene != nullptr; }

    // 当前场景关联的文件路径，空字符串表示尚未保存到文件
    const std::string& GetSceneFilePath() const { return m_SceneFilePath; }
    bool HasFilePath() const { return !m_SceneFilePath.empty(); }

    // 场景是否有未保存的修改
    bool IsDirty() const { return m_Dirty; }
    void MarkDirty() { m_Dirty = true; }
    void ClearDirty() { m_Dirty = false; }

    SceneRunState GetRunState() const { return m_RunState; }
    bool IsEditing() const { return m_RunState == SceneRunState::Edit; }
    bool IsPlaying() const { return m_RunState == SceneRunState::Play; }
    bool IsPaused() const { return m_RunState == SceneRunState::Pause; }

    bool BeginPlay();
    void StopPlay();
    void PausePlay();
    void ResumePlay();
    bool StepPlay();
    SceneManager& GetSceneManager() { return m_SceneManager; }
    const SceneManager& GetSceneManager() const { return m_SceneManager; }
    GameFlowController& GetGameFlowController() { return m_GameFlowController; }
    const GameFlowController& GetGameFlowController() const { return m_GameFlowController; }
    bool RequestSceneLoad(const std::string& path);
    void SetPauseWhenUnfocused(bool enabled);
    bool GetPauseWhenUnfocused() const { return m_PauseWhenUnfocused; }
    bool IsWindowFocused() const { return m_WindowFocused; }

protected:
    // 子类可重写，在 Scene 加载/创建完成后触发
    virtual void OnSceneLoaded() {}
    virtual void OnSceneUnloaded() {}

    std::unique_ptr<Scene> m_EditorScene;
    std::unique_ptr<Scene> m_PlayScene;

private:
    std::unique_ptr<Scene> CloneSceneFromJson(const std::string& json) const;

    std::string m_SceneFilePath;
    bool m_Dirty = false;
    SceneRunState m_RunState = SceneRunState::Edit;
    bool m_EditDirtySnapshot = false;
    bool m_StepRequested = false;
    SceneManager m_SceneManager;
    GameFlowController m_GameFlowController;
    SceneLoadState m_LastObservedLoadState = SceneLoadState::Idle;
    bool m_PauseWhenUnfocused = false;
    bool m_WindowFocused = true;
};
