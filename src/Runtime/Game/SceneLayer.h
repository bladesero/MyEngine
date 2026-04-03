#pragma once

#include "Core/Layer.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
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

class SceneLayer : public Layer {
public:
    explicit SceneLayer(const std::string& layerName = "SceneLayer");

    // ---- Layer 生命周期 -----------------------------------------------
    void OnAttach()  override;
    void OnDetach()  override;
    void OnUpdate(float dt)    override;
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

    // ---- 访问 ----------------------------------------------------------
    Scene&       GetScene()       { return *m_Scene; }
    const Scene& GetScene() const { return *m_Scene; }

    // 当前场景关联的文件路径，空字符串表示尚未保存到文件
    const std::string& GetSceneFilePath() const { return m_SceneFilePath; }
    bool               HasFilePath()      const { return !m_SceneFilePath.empty(); }

    // 场景是否有未保存的修改
    bool IsDirty() const { return m_Dirty; }
    void MarkDirty()     { m_Dirty = true; }
    void ClearDirty()    { m_Dirty = false; }

protected:
    // 子类可重写，在 Scene 加载/创建完成后触发
    virtual void OnSceneLoaded()  {}
    virtual void OnSceneUnloaded() {}

    std::unique_ptr<Scene> m_Scene;

private:
    std::string m_SceneFilePath;
    bool        m_Dirty = false;
};
