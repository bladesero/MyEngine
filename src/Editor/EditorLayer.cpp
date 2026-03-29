#include "Editor/EditorLayer.h"

#include "Scene/Scene.h"
#include "Scene/Actor.h"
#include "Core/Logger.h"
#include "Core/Platform.h"

#ifdef MYENGINE_PLATFORM_WINDOWS
#  include "Renderer/D3D11Context.h"
#  include "Renderer/D3D12Context.h"
#endif

#ifdef MYENGINE_PLATFORM_MACOS
#  include "Renderer/MetalContext.h"
#endif

#if defined(MYENGINE_ENABLE_IMGUI)
#  include <backends/imgui_impl_sdl3.h>

#  ifdef MYENGINE_PLATFORM_WINDOWS
#    include <backends/imgui_impl_dx11.h>
#    include <backends/imgui_impl_dx12.h>
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <commdlg.h>
#  endif

#  ifdef MYENGINE_PLATFORM_MACOS
// imgui_impl_metal.h uses __OBJC__ guards; the ObjC API is only exposed when
// compiled as Objective-C++ (.mm).  On macOS CMake sets LANGUAGE OBJCXX for
// this file so the imports below are valid.
#    import <Metal/Metal.h>
#    include <backends/imgui_impl_metal.h>
#  endif
#endif

#if defined(MYENGINE_ENABLE_IMGUI)
class EditorLayer::ImGuiPlatformEventBridge : public IPlatformEventBridge {
public:
    void OnSDLEvent(const SDL_Event& event) override {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
};
#endif

EditorLayer::EditorLayer(SceneRenderLayer* sceneLayer, IWindow* window, Engine* engine)
    : Layer("EditorLayer")
    , m_SceneLayer(sceneLayer)
    , m_Window(window)
    , m_Engine(engine)
{}

void EditorLayer::OnAttach()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Window || !m_SceneLayer) return;
    IRenderContext* rc = m_SceneLayer->GetRenderContext();
    if (!m_Window->GetSDLWindow()) {
        Logger::Error("[Editor] ImGui requires SDLWindow (SDL_Window*)");
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Hook raw SDL events for ImGui through platform bridge.
    if (m_Engine) {
        m_PlatformBridge = std::make_unique<ImGuiPlatformEventBridge>();
        m_Engine->SetPlatformEventBridge(m_PlatformBridge.get());
    }

    // Choose renderer backend depending on current IRenderContext.
#ifdef MYENGINE_PLATFORM_WINDOWS
    ImGui_ImplSDL3_InitForD3D(m_Window->GetSDLWindow());
    if (auto* d3d = dynamic_cast<D3D11Context*>(rc)) {
        m_ImGuiBackend = ImGuiBackendType::DX11;
        ImGui_ImplDX11_Init(d3d->GetDevice(), d3d->GetDeviceContext());
    }
    else if (auto* d3d12 = dynamic_cast<D3D12Context*>(rc)) {
        m_ImGuiBackend = ImGuiBackendType::DX12;
        ImGui_ImplDX12_Init(
            d3d12->GetDevice(),
            d3d12->GetNumFramesInFlight(),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            d3d12->GetSrvDescriptorHeap(),
            d3d12->GetFontSrvCpuHandle(),
            d3d12->GetFontSrvGpuHandle());
    }
    else {
        Logger::Error("[Editor] ImGui requires D3D11Context or D3D12Context");
        ImGui::DestroyContext();
        return;
    }
#elif defined(MYENGINE_PLATFORM_MACOS)
    ImGui_ImplSDL3_InitForMetal(m_Window->GetSDLWindow());
    if (auto* metal = dynamic_cast<MetalContext*>(rc)) {
        m_ImGuiBackend = ImGuiBackendType::Metal;
        auto* device = (__bridge id<MTLDevice>)metal->GetDevice();
        ImGui_ImplMetal_Init(device);
    }
    else {
        Logger::Error("[Editor] ImGui requires MetalContext on macOS");
        ImGui::DestroyContext();
        return;
    }
#else
    Logger::Error("[Editor] No supported ImGui renderer backend on this platform");
    ImGui::DestroyContext();
    (void)rc;
    return;
#endif

    m_ImGuiReady = true;
#endif
}

void EditorLayer::OnDetach()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_Engine) {
        m_Engine->SetPlatformEventBridge(nullptr);
    }
    m_PlatformBridge.reset();
    if (m_ImGuiReady) {
#ifdef MYENGINE_PLATFORM_WINDOWS
        if (m_ImGuiBackend == ImGuiBackendType::DX11) {
            ImGui_ImplDX11_Shutdown();
        }
        else {
            ImGui_ImplDX12_Shutdown();
        }
#elif defined(MYENGINE_PLATFORM_MACOS)
        ImGui_ImplMetal_Shutdown();
#endif
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        m_ImGuiReady = false;
    }
#endif
}

void EditorLayer::OnRender()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_SceneLayer || !m_ImGuiReady) return;
    IRenderContext* rc = m_SceneLayer->GetRenderContext();

#ifdef MYENGINE_PLATFORM_WINDOWS
    if (m_ImGuiBackend == ImGuiBackendType::DX11) {
        auto* d3d11 = dynamic_cast<D3D11Context*>(rc);
        if (!d3d11) return;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplSDL3_NewFrame();
    }
    else {
        auto* d3d12 = dynamic_cast<D3D12Context*>(rc);
        if (!d3d12) return;
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplSDL3_NewFrame();
    }
#elif defined(MYENGINE_PLATFORM_MACOS)
    {
        auto* metal = dynamic_cast<MetalContext*>(rc);
        if (!metal) return;
        auto* rpd = (__bridge MTLRenderPassDescriptor*)metal->GetRenderPassDescriptor();
        ImGui_ImplMetal_NewFrame(rpd);
        ImGui_ImplSDL3_NewFrame();
    }
#endif

    ImGui::NewFrame();

    DrawToolbar();
    DrawSceneOutliner();

    ImGui::Render();

#ifdef MYENGINE_PLATFORM_WINDOWS
    if (m_ImGuiBackend == ImGuiBackendType::DX11) {
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    else {
        auto* d3d12 = dynamic_cast<D3D12Context*>(rc);
        if (!d3d12) return;
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3d12->GetCommandList());
    }
#elif defined(MYENGINE_PLATFORM_MACOS)
    {
        auto* metal = dynamic_cast<MetalContext*>(rc);
        if (!metal) return;
        auto* cmdBuf = (__bridge id<MTLCommandBuffer>)metal->GetCommandBuffer();
        auto* enc    = (__bridge id<MTLRenderCommandEncoder>)metal->GetCommandEncoder();
        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmdBuf, enc);
    }
#endif

    // Present AFTER UI overlay (SceneRenderLayer rendered with present disabled).
    rc->EndFrame();
#endif
}

#if defined(MYENGINE_ENABLE_IMGUI) && defined(MYENGINE_PLATFORM_WINDOWS)
static std::string OpenSceneFileDialogWin32()
{
    char fileName[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = nullptr;
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = "Scene (*.json)\0*.json\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(fileName);
    }
    return {};
}

static std::string SaveSceneFileDialogWin32()
{
    char fileName[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = nullptr;
    ofn.lpstrFile    = fileName;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrFilter  = "Scene (*.json)\0*.json\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt  = "json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn) == TRUE) {
        return std::string(fileName);
    }
    return {};
}
#endif

void EditorLayer::DrawToolbar()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 40.0f));

    ImGui::Begin("##Toolbar", nullptr, flags);

    if (ImGui::Button("New Scene")) {
        m_SceneLayer->NewScene("Untitled");
        m_Selected = nullptr;
    }
    ImGui::SameLine();

    if (ImGui::Button("Open Scene")) {
#if defined(MYENGINE_PLATFORM_WINDOWS)
        std::string path = OpenSceneFileDialogWin32();
        if (!path.empty()) {
            if (!SceneSerializer::LoadFromFile(m_SceneLayer->GetScene(), path)) {
                Logger::Error("[Editor] Failed to open scene: ", path);
            } else {
                m_Selected = nullptr;
            }
        }
#else
        const char* path = "scene.json";
        if (!SceneSerializer::LoadFromFile(m_SceneLayer->GetScene(), path)) {
            Logger::Error("[Editor] Failed to open scene: ", path);
        } else {
            m_Selected = nullptr;
        }
#endif
    }
    ImGui::SameLine();

    if (ImGui::Button("Save Scene")) {
#if defined(_WIN32)
        std::string path = SaveSceneFileDialogWin32();
        if (!path.empty()) {
            if (!SceneSerializer::SaveToFile(m_SceneLayer->GetScene(), path)) {
                Logger::Error("[Editor] Failed to save scene: ", path);
            }
        }
#else
        const char* path = "scene.json";
        if (!SceneSerializer::SaveToFile(m_SceneLayer->GetScene(), path)) {
            Logger::Error("[Editor] Failed to save scene: ", path);
        }
#endif
    }

    ImGui::End();
#endif
}

void EditorLayer::DrawActorNode(Actor* actor)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!actor) return;

    const bool selected = (m_Selected == actor);
    const bool hasChildren = !actor->GetChildren().empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanFullWidth;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected)     flags |= ImGuiTreeNodeFlags_Selected;

    const bool open = ImGui::TreeNodeEx((void*)actor, flags, "%s", actor->GetName().c_str());
    if (ImGui::IsItemClicked()) {
        m_Selected = actor;
    }

    if (open && hasChildren) {
        for (Actor* child : actor->GetChildren()) {
            DrawActorNode(child);
        }
        ImGui::TreePop();
    }
#endif
}

void EditorLayer::DrawSceneOutliner()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::Begin("Scene Outliner");

    Scene& scene = m_SceneLayer->GetScene();
    auto   roots = scene.GetRootActors();

    for (Actor* actor : roots) {
        DrawActorNode(actor);
    }

    ImGui::End();
#endif
}
