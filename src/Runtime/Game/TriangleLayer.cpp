#include "TriangleLayer.h"
#include "../Core/Logger.h"
#include "../Core/EngineTime.h"
#include "../Input/Input.h"
#include "../Renderer/TriangleShader.h"

#include <SDL3/SDL_scancode.h>

// --------------------------------------------------------------------------
// Vertex layout (must match HLSL VSIn)
// --------------------------------------------------------------------------
struct Vertex {
    float x, y, z;          // POSITION
    float r, g, b, a;       // COLOR
};

static const VertexElement k_Layout[] = {
    { "POSITION", 0, VertexFormat::Float3, offsetof(Vertex, x) },
    { "COLOR",    0, VertexFormat::Float4, offsetof(Vertex, r) },
};

// --------------------------------------------------------------------------
// TriangleLayer
// --------------------------------------------------------------------------

TriangleLayer::TriangleLayer(IRenderContext* renderer, int w, int h)
    : Layer("TriangleLayer"), m_Renderer(renderer), m_VpW(w), m_VpH(h) {}

void TriangleLayer::OnAttach() {
    Logger::Info(Name(), " attached");

    // ---- Geometry ----------------------------------------------------------
    static const Vertex vertices[] = {
        {  0.0f,  0.5f, 0.0f,   1.0f, 0.2f, 0.2f, 1.0f },   // top   – red
        {  0.5f, -0.5f, 0.0f,   0.2f, 1.0f, 0.2f, 1.0f },   // right – green
        { -0.5f, -0.5f, 0.0f,   0.2f, 0.2f, 1.0f, 1.0f },   // left  – blue
    };

    m_VB = m_Renderer->CreateVertexBuffer(
        vertices, sizeof(vertices), sizeof(Vertex));

    // ---- Shader ------------------------------------------------------------
    m_Shader = m_Renderer->CreateShader(
        k_TriangleShaderSource, "VSMain", "PSMain",
        k_Layout, 2);

    // ---- Camera ------------------------------------------------------------
    m_Camera.LookAt({ 0.0f, 0.0f, -3.0f }, { 0.0f, 0.0f, 0.0f });
    m_Camera.SetPerspective(60.0f,
        static_cast<float>(m_VpW) / static_cast<float>(m_VpH),
        0.1f,
        1000.0f);
}

void TriangleLayer::OnDetach() {
    m_VB.reset();
    m_Shader.reset();
    Logger::Info(Name(), " detached");
}

void TriangleLayer::OnEvent(Event& event) {
    if (event.type == EventType::WindowResize) {
        m_VpW = event.resize.width;
        m_VpH = event.resize.height;
        if (m_VpW > 0 && m_VpH > 0) {
            m_Camera.SetAspect(
                static_cast<float>(m_VpW) / static_cast<float>(m_VpH));
            if (GpuSwapChain* swapChain = m_Renderer->GetSwapChain()) {
                swapChain->Resize(static_cast<uint32_t>(m_VpW),
                                  static_cast<uint32_t>(m_VpH));
            }
            m_Renderer->SetViewport(0, 0,
                static_cast<float>(m_VpW), static_cast<float>(m_VpH));
        }
    }
}

void TriangleLayer::OnUpdate(float dt) {
    // ---- Keyboard: WASD rotate triangle, QE dolly camera ------------------
    if (Input::IsKeyDown(SDL_SCANCODE_LEFT)  ||
        Input::IsKeyDown(SDL_SCANCODE_A))   m_Rotation -= 90.0f * dt;
    if (Input::IsKeyDown(SDL_SCANCODE_RIGHT) ||
        Input::IsKeyDown(SDL_SCANCODE_D))   m_Rotation += 90.0f * dt;

    if (Input::IsKeyDown(SDL_SCANCODE_Q))   m_Camera.Dolly( 2.0f * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_E))   m_Camera.Dolly(-2.0f * dt);

    // ---- Right-mouse orbit -------------------------------------------------
    if (Input::IsMousePressed(3))  { m_RmbDown = true; }
    if (Input::IsMouseReleased(3)) { m_RmbDown = false; }

    if (m_RmbDown) {
        int relX = Input::GetMouseRelX();
        int relY = Input::GetMouseRelY();
        if (relX != 0 || relY != 0) {
            m_Camera.Orbit(
                static_cast<float>(relX) * 0.3f,
                static_cast<float>(relY) * 0.3f);
        }
    }

    // ---- FPS log -----------------------------------------------------------
    static float acc = 0.0f;
    static int   frames = 0;
    acc += dt; ++frames;
    if (acc >= 1.0f) {
        Logger::Info("FPS=", frames,
                     "  frame=", Time::FrameCount(),
                     "  total=", Time::TotalSeconds(), "s");
        acc = 0.0f; frames = 0;
    }
}

void TriangleLayer::OnRender() {
    if (!m_Renderer || !m_VB || !m_Shader) return;
    GpuCommandList* cmd = m_Renderer->GetGraphicsCommandList();
    if (!cmd) return;

    // ---- Build MVP (row-vector: world * Model * View * Proj) ---------------
    Mat4 model = Mat4::RotationY(m_Rotation * kDeg2Rad);
    Mat4 mvp   = model * m_Camera.GetViewProj();
    // Upload row-major; HLSL interprets as transpose, mul(pos, g_MVP) then correct.

    // ---- Clear + draw ------------------------------------------------------
    m_Renderer->BeginFrame(0.12f, 0.12f, 0.18f);

    cmd->BindShader(m_Shader.get());
    cmd->BindVertexBuffer(m_VB.get());
    cmd->SetVSConstants(mvp.Data(), 64);
    cmd->Draw(3);

    m_Renderer->EndFrame();
}
