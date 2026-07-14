#pragma once

#include "../Core/Layer.h"
#include "../Renderer/IRenderContext.h"
#include "../Camera/Camera.h"

#include <memory>

struct ShaderHandle;

// --------------------------------------------------------------------------
// TriangleLayer
//
// Renders a colour-interpolated triangle using D3D11.
// Camera can be orbited with right-mouse-drag, zoomed with scroll wheel
// (simulated via keyboard +/-).
// --------------------------------------------------------------------------
class TriangleLayer : public Layer {
public:
    explicit TriangleLayer(IRenderContext* renderer, int viewportW, int viewportH);

    void OnAttach() override;
    void OnDetach() override;
    void OnEvent(Event& event) override;
    void OnUpdate(float dt) override;
    void OnRender() override;

private:
    IRenderContext* m_Renderer = nullptr;
    int m_VpW = 1280;
    int m_VpH = 720;

    std::shared_ptr<GpuBuffer> m_VB;
    std::shared_ptr<ShaderHandle> m_ShaderHandle;
    std::shared_ptr<GpuGraphicsPipeline> m_Pipeline;

    Camera m_Camera;

    float m_Rotation = 0.0f; // degrees per second rotation of triangle

    // Mouse-orbit state
    bool m_RmbDown = false;
    int m_LastMX = 0;
    int m_LastMY = 0;
};
