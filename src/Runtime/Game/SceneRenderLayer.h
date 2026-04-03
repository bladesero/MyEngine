#pragma once

#include "Game/SceneLayer.h"
#include "Renderer/IRenderContext.h"
#include "Renderer/Renderer.h"
#include "Camera/Camera.h"
#include "Math/Ray.h"
#include <memory>

// ==========================================================================
// SceneRenderLayer  –  SceneLayer + D3D11 场景渲染
//
// 构造时传入 IRenderContext 与视口尺寸；OnRender 遍历场景中带
// MeshRendererComponent 的 Actor，上传 Mesh 到 GPU（若未上传），
// 使用材质或默认 Mesh 着色器绘制（MVP + BaseColor）。
// OnSceneLoaded 时若场景为空则创建一个带立方体 + 默认材质的演示 Actor。
// 视口输入：飞行相机 — WASD 平移，Q/E 上下，按住右键拖动转向（手柄左摇杆移动、右摇杆转向）。
// ==========================================================================

class SceneRenderLayer : public SceneLayer {
public:
    SceneRenderLayer(IRenderContext* context, int viewportWidth, int viewportHeight);

    void OnAttach()  override;
    void OnDetach()   override;
    void OnUpdate(float dt) override;
    void OnEvent(Event& event) override;
    void OnRender()  override;

    // Editor overlay needs to present after UI. When false, this layer will render
    // the 3D scene but will not call Present (EndFrame).
    void SetPresentEnabled(bool enabled) { m_PresentEnabled = enabled; }
    void SetEditorViewportRect(int x, int y, int width, int height);
    void SetViewportInputEnabled(bool enabled);

    IRenderContext* GetRenderContext() const { return m_RenderContext; }

    Camera&       GetCamera()       { return m_Camera; }
    const Camera& GetCamera() const { return m_Camera; }

    void GetViewportRect(int& outX, int& outY, int& outW, int& outH) const;

    // Window-space pixel coordinates (same space as SDL / ImGui::GetIO().MousePos).
    bool BuildRayFromScreen(float screenX, float screenY, Math::Ray& outRay) const;

protected:
    void OnSceneLoaded() override;

private:
    IRenderContext*       m_RenderContext = nullptr;
    Renderer              m_Renderer;
    int                   m_VpX = 0;
    int                   m_VpY = 0;
    int                   m_VpW = 0;
    int                   m_VpH = 0;
    Camera                m_Camera;
    std::shared_ptr<GpuShader> m_DefaultMeshShader;
    bool                  m_RmbDown = false;
    bool                  m_UseEditorViewport = false;
    bool                  m_ViewportInputEnabled = true;
    bool                  m_PresentEnabled = true;
};
