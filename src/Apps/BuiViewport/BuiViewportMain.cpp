#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "BuiViewportABI.h"
#include "BuiViewportInput.h"
#include "Assets/AssetManager.h"
#include "Core/Application.h"
#include "Core/FrameStats.h"
#include "Core/Logger.h"
#include "Core/PlatformEventBridge.h"
#include "Game/SceneRenderLayer.h"
#include "Project/ProjectConfig.h"
#include "Renderer/EngineShaderCatalog.h"
#include "Renderer/RenderBackendRegistry.h"
#include "Renderer/ShaderManager.h"
#include "UI/Render/UIDrawList.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace {
namespace fs = std::filesystem;
constexpr char kLabel[] = "[BUI Viewport] ";

class Kit {
public:
    ~Kit() {
        if (object_)
            api_->destroy(object_);
        if (library_)
            SDL_UnloadObject(library_);
    }

    bool Open(const fs::path& path) {
        library_ = SDL_LoadObject(path.u8string().c_str());
        if (!library_) {
            Logger::Error(kLabel, "Could not load kit: ", SDL_GetError());
            return false;
        }
        const auto getApi = reinterpret_cast<MBUIGetApi>(SDL_LoadFunction(library_, "MyEngineBuiGetApi"));
        api_ = getApi ? getApi(MYENGINE_BUI_VIEWPORT_ABI) : nullptr;
        if (!api_ || !api_->create || !api_->destroy || !api_->frame || !api_->pointer || !api_->take_reset_camera) {
            Logger::Error(kLabel, "BUI viewport kit ABI mismatch");
            return false;
        }
        object_ = api_->create();
        return object_ != nullptr;
    }

    bool Frame(int width, int height, float scale, MBUIFrame& frame) {
        return api_->frame(object_, width, height, scale, &frame) && frame.rgba;
    }
    void Pointer(int kind, int x, int y) { api_->pointer(object_, kind, x, y); }
    bool TakeReset() { return api_->take_reset_camera(object_) != 0; }

    Kit() = default;
    Kit(const Kit&) = delete;
    Kit& operator=(const Kit&) = delete;

private:
    SDL_SharedObject* library_ = nullptr;
    const MBUIApi* api_ = nullptr;
    void* object_ = nullptr;
};

class Composite {
public:
    explicit Composite(IRenderContext& rhi) : rhi_(rhi) {}

    bool UpdateChrome(const MBUIFrame& frame, int width, int height) {
        if (chrome_ && chromeRevision_ == frame.revision)
            return true;
        auto texture = rhi_.UploadTexture2D(frame.rgba, width, height);
        if (!texture)
            return false;
        auto view = CreateView(texture);
        if (!view)
            return false;
        chrome_ = std::move(view);
        chromeRevision_ = frame.revision;
        return true;
    }

    bool Record(int width, int height, const MBUIRect& viewport, const GpuTextureView* scene,
                GpuTextureView& backbuffer, GpuCommandList& commands) {
        if (!chrome_ || !backbuffer.texture || !BuiViewport::Fits(viewport, width, height))
            return false;
        if (!PreparePipeline(backbuffer.texture->desc.format))
            return false;
        if (scene && (!sceneView_ || sceneView_->texture != scene->texture)) {
            sceneView_ = CreateView(scene->texture);
            if (!sceneView_)
                return false;
        }
        if (!PrepareGeometry(width, height, viewport))
            return false;
        const float constants[] = {1.f / width, 1.f / height, 0.f, 0.f};
        auto chromeBindings = Bind(chrome_, constants);
        auto sceneBindings = scene ? Bind(sceneView_, constants) : nullptr;
        if (!chromeBindings || (scene && !sceneBindings))
            return false;

        RenderingAttachment color;
        color.view = &backbuffer;
        color.loadOp = RHILoadOp::Clear;
        color.storeOp = RHIStoreOp::Store;
        color.clearColor = {0.f, 0.f, 0.f, 1.f};
        const auto w = static_cast<uint32_t>(width), h = static_cast<uint32_t>(height);
        // Frame is already open. Its backend owns the backbuffer's Present transition.
        // The scene graph exports its offscreen composite in ShaderResource state.
        commands.BeginDebugEvent("BUI viewport composite");
        commands.BeginRendering(RenderingInfo{&color, 1, nullptr, w, h});
        commands.SetViewport(0.f, 0.f, static_cast<float>(width), static_cast<float>(height));
        commands.SetScissor(0, 0, w, h);
        commands.SetGraphicsPipeline(pipeline_.get());
        commands.BindIndexBuffer(index_.get());
        commands.BindVertexBuffer(chromeVertices_.get());
        commands.SetBindGroup(0, chromeBindings.get());
        commands.DrawIndexed(6);
        if (sceneBindings) {
            commands.BindVertexBuffer(sceneVertices_.get());
            commands.SetBindGroup(0, sceneBindings.get());
            commands.DrawIndexed(6);
        }
        commands.EndRendering();
        commands.EndDebugEvent();
        return true;
    }

private:
    std::shared_ptr<GpuTextureView> CreateView(const std::shared_ptr<GpuTexture>& texture) {
        RHITextureViewDesc desc;
        desc.usage = RHIResourceUsage::ShaderResource;
        return rhi_.CreateTextureView(texture, desc);
    }

    std::shared_ptr<GpuBindGroup> Bind(const std::shared_ptr<GpuTextureView>& view, const float (&constants)[4]) {
        auto group = rhi_.CreateBindGroup(shader_);
        if (!group || !group->SetConstants("UIScreenConstants", constants, sizeof(constants)) ||
            !group->SetTexture("u_Texture", view) || !group->SetSampler("u_Sampler", sampler_) || !group->Validate())
            return nullptr;
        return group;
    }

    bool PreparePipeline(RHIFormat format) {
        if (pipeline_ && format_ == format)
            return true;
        const VertexElement layout[] = {
            {"POSITION", 0, VertexFormat::Float2, offsetof(UIVertex, x)},
            {"TEXCOORD", 0, VertexFormat::Float2, offsetof(UIVertex, u)},
            {"COLOR", 0, VertexFormat::Float4, offsetof(UIVertex, r)},
        };
        auto handle = ShaderManager::Get().GetOrCreate(EngineShaders::kScreenUI, layout, 3);
        if (!handle || !handle->shader)
            return false;
        shader_ = handle->shader;
        GraphicsPipelineDesc desc;
        desc.shader = shader_;
        desc.colorFormats = {format};
        desc.depthStencil.depthTestEnable = false;
        desc.depthStencil.depthWriteEnable = false;
        desc.rasterizer.cullMode = RHICullMode::None;
        desc.blend.attachments[0].blendEnable = false;
        pipeline_ = rhi_.CreateGraphicsPipeline(desc);
        RHISamplerDesc sampler;
        sampler.filter = RHIFilter::Linear;
        sampler.addressU = sampler.addressV = sampler.addressW = RHIAddressMode::Clamp;
        sampler_ = rhi_.CreateSampler(sampler);
        format_ = format;
        if (!sampler_)
            pipeline_.reset();
        return pipeline_ != nullptr;
    }

    std::shared_ptr<GpuBuffer> Quad(const MBUIRect& bounds) {
        const auto x = static_cast<float>(bounds.x), y = static_cast<float>(bounds.y);
        const auto right = static_cast<float>(bounds.x + bounds.width);
        const auto bottom = static_cast<float>(bounds.y + bounds.height);
        const UIVertex vertices[] = {{x, y, 0, 0}, {right, y, 1, 0}, {right, bottom, 1, 1}, {x, bottom, 0, 1}};
        return rhi_.CreateVertexBuffer(vertices, sizeof(vertices), sizeof(UIVertex));
    }

    bool PrepareGeometry(int width, int height, const MBUIRect& r) {
        if (!index_) {
            const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
            index_ = rhi_.CreateIndexBuffer(indices, sizeof(indices));
        }
        if (!chromeVertices_ || width_ != width || height_ != height) {
            chromeVertices_ = Quad({0, 0, width, height});
            width_ = width;
            height_ = height;
        }
        if (!sceneVertices_ || rect_.x != r.x || rect_.y != r.y || rect_.width != r.width || rect_.height != r.height) {
            sceneVertices_ = Quad(r);
            rect_ = r;
        }
        return index_ && chromeVertices_ && sceneVertices_;
    }

    IRenderContext& rhi_;
    std::shared_ptr<GpuTextureView> chrome_, sceneView_;
    std::shared_ptr<GpuBuffer> index_, chromeVertices_, sceneVertices_;
    std::shared_ptr<GpuShader> shader_;
    std::shared_ptr<GpuGraphicsPipeline> pipeline_;
    std::shared_ptr<GpuSampler> sampler_;
    RHIFormat format_{};
    uint64_t chromeRevision_ = 0;
    int width_ = 0, height_ = 0;
    MBUIRect rect_{};
};

class ViewportLayer final : public SceneRenderLayer, public IPlatformEventBridge {
public:
    ViewportLayer(IRenderContext& rhi, IWindow& window, Engine& engine, const fs::path& scene,
                  const ProjectGraphicsSettings& graphics, Kit& kit, unsigned smokeFrames)
        : SceneRenderLayer(&rhi, window.GetPixelWidth(), window.GetPixelHeight()), window_(window), engine_(engine),
          kit_(kit), composite_(rhi), smokeFrames_(smokeFrames) {
        SetPresentEnabled(false);
        SetRenderPath(graphics.renderPath == "deferred" ? RenderPath::Deferred : RenderPath::Forward);
        SetDeviceProfile(graphics.deviceProfile);
        SetHardwareRayTracingEnabled(graphics.hardwareRayTracing);
        SetViewportInputEnabled(false); // scene controller never reads global input for this app
        scenePath_ = scene.string();
    }

    void OnAttach() override {
        SceneRenderLayer::OnAttach();
        ready_ = LoadScene(scenePath_);
    }
    bool IsReady() const { return ready_; }
    bool SmokePassed() const { return smokePassed_; }
    void OnDetach() override {
        CancelInput();
        SceneRenderLayer::OnDetach();
    }

    void OnUpdate(float dt) override {
        auto& camera = GetCamera();
        const auto delta = cameraInput_.Take();
        const float speed = 6.f * dt;
        if (kit_.TakeReset()) {
            CancelInput();
            camera.LookAt({0.f, 0.f, -4.f}, {0.f, 0.f, 0.f});
        } else {
            if (delta.yaw || delta.pitch)
                camera.Rotate(-delta.yaw * .25f, -delta.pitch * .25f);
            if (delta.forward || delta.dolly)
                camera.MoveForward(delta.forward * speed + delta.dolly * .6f);
            if (delta.right)
                camera.MoveRight(delta.right * speed);
            if (delta.up)
                camera.MoveUp(delta.up * speed);
        }
        SceneRenderLayer::OnUpdate(dt);
    }

    void OnSDLEvent(const SDL_Event& e) override {
        auto* window = window_.GetSDLWindow();
        const auto id = SDL_GetWindowID(window);
        switch (e.type) {
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
            if (e.window.windowID == id) {
                viewport_ = {};
                CancelInput();
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (e.motion.windowID == id) {
                cameraInput_.Motion(e.motion.xrel, e.motion.yrel);
                if (!cameraInput_.Looking())
                    kit_.Pointer(MBUI_MOVE, X(e.motion.x), Y(e.motion.y));
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.windowID == id) {
                const bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                const int x = X(e.button.x), y = Y(e.button.y);
                if (e.button.button == SDL_BUTTON_RIGHT) {
                    if (down && cameraInput_.BeginLook(BuiViewport::Contains(viewport_, x, y))) {
                        kit_.Pointer(MBUI_CANCEL, 0, 0);
                        SDL_CaptureMouse(true);
                    }
                    if (!down && cameraInput_.Looking())
                        StopLook();
                } else if (e.button.button == SDL_BUTTON_LEFT && !cameraInput_.Looking()) {
                    kit_.Pointer(down ? MBUI_LEFT_DOWN : MBUI_LEFT_UP, x, y);
                }
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (e.wheel.windowID == id) {
                const float sign = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
                cameraInput_.Wheel(BuiViewport::Contains(viewport_, X(e.wheel.mouse_x), Y(e.wheel.mouse_y)),
                                   sign * e.wheel.y);
            }
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (e.key.windowID == id && !e.key.repeat) {
                if (auto key = MapKey(e.key.scancode))
                    cameraInput_.SetKey(*key, e.type == SDL_EVENT_KEY_DOWN);
            }
            break;
        }
    }

    void OnRender() override {
        if (quit_ || (SDL_GetWindowFlags(window_.GetSDLWindow()) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)))
            return;
        const int width = window_.GetPixelWidth(), height = window_.GetPixelHeight();
        if (width <= 0 || height <= 0)
            return;
        const auto scale = SDL_GetWindowDisplayScale(window_.GetSDLWindow());
        MBUIFrame ui{};
        if (!kit_.Frame(width, height, scale > 0.f ? scale : 1.f, ui) ||
            !BuiViewport::Fits(ui.viewport, width, height)) {
            viewport_ = {};
            CancelInput();
            // A very small window can legitimately have no room for the scene.
            return;
        }
        if (!composite_.UpdateChrome(ui, width, height)) {
            Fail("BUI raster upload failed");
            return;
        }

        viewport_ = ui.viewport;
        BeginViewportActivityFrame();
        SetSceneViewportActive(true);
        CommitViewportActivityFrame();
        GetSceneViewport()->SetViewportRect(viewport_.x, viewport_.y, viewport_.width, viewport_.height);
        const auto closuresBefore = frameClosures_;
        bool recorded = false;
        {
            // The same coordinator is used by SceneRenderLayer and the renderer.
            // It closes the only graphics frame, including failure paths.
            BuiViewport::FinishFrame finish([this] {
                GetFrameCoordinator().EndFrame();
                ++frameClosures_;
            });
            SceneRenderLayer::OnRender();
            auto* rhi = GetRenderContext();
            auto* back = rhi->GetCurrentBackBufferView();
            auto* scene = GetSceneColorView();
            auto* commands = rhi->GetGraphicsCommandList();
            // The renderer can publish a texture before the first graph actually
            // executes (e.g. shader prewarm). Keep showing chrome, not uninitialized pixels.
            if (!scene || !scene->texture || scene->texture->desc.width != static_cast<uint32_t>(viewport_.width) ||
                scene->texture->desc.height != static_cast<uint32_t>(viewport_.height) ||
                FrameStatsProvider::GetRendererStats().fullscreenDrawCalls == 0)
                scene = nullptr;
            if (back && back->texture && back->texture->desc.width == static_cast<uint32_t>(width) &&
                back->texture->desc.height == static_cast<uint32_t>(height) && commands) {
                if (!composite_.Record(width, height, viewport_, scene, *back, *commands)) {
                    Fail("Could not record scene/BUI composite");
                    return;
                }
                recorded = scene != nullptr;
            }
        }
        if (!recorded)
            return;
        const auto sceneFrameCloses = frameClosures_ - closuresBefore;
        if (sceneFrameCloses != 1) {
            Fail("Did not observe one coordinator close for a scene composite");
            return;
        }
        sceneFrameClosures_ += sceneFrameCloses;
        if (++compositeFrames_ == smokeFrames_ && smokeFrames_) {
            Logger::Info(kLabel, "scene_composites=", compositeFrames_,
                         " coordinator_closes_for_scene_frames=", sceneFrameClosures_,
                         " all_coordinator_closes=", frameClosures_);
            smokePassed_ = true;
            engine_.SetExitCode(0);
            quit_ = true;
            engine_.RequestQuit();
        }
    }

private:
    static std::optional<BuiViewport::Key> MapKey(SDL_Scancode scancode) {
        using BuiViewport::Key;
        switch (scancode) {
        case SDL_SCANCODE_W:
            return Key::Forward;
        case SDL_SCANCODE_S:
            return Key::Back;
        case SDL_SCANCODE_A:
            return Key::Left;
        case SDL_SCANCODE_D:
            return Key::Right;
        case SDL_SCANCODE_Q:
            return Key::Up;
        case SDL_SCANCODE_E:
            return Key::Down;
        default:
            return std::nullopt;
        }
    }

    int X(float x) const { return BuiViewport::ToPixel(x, window_.GetWidth(), window_.GetPixelWidth()); }
    int Y(float y) const { return BuiViewport::ToPixel(y, window_.GetHeight(), window_.GetPixelHeight()); }
    void StopLook() {
        if (cameraInput_.Looking())
            SDL_CaptureMouse(false);
        cameraInput_.EndLook();
    }
    void CancelInput() {
        StopLook();
        cameraInput_.Cancel();
        kit_.Pointer(MBUI_CANCEL, 0, 0);
    }
    void Fail(const char* why) {
        Logger::Error(kLabel, why);
        engine_.SetExitCode(3);
        quit_ = true;
        engine_.RequestQuit();
    }

    IWindow& window_;
    Engine& engine_;
    Kit& kit_;
    Composite composite_;
    BuiViewport::Input cameraInput_;
    MBUIRect viewport_{};
    std::string scenePath_;
    unsigned smokeFrames_ = 0;
    uint64_t compositeFrames_ = 0, frameClosures_ = 0, sceneFrameClosures_ = 0;
    bool ready_ = false, quit_ = false, smokePassed_ = false;
};

class BuiApp final : public Application {
public:
    BuiApp(ApplicationConfig config, ProjectConfig project, fs::path scene, fs::path kit, unsigned smoke)
        : Application(config), backend_(config.backend), project_(std::move(project)), scene_(std::move(scene)),
          kitPath_(std::move(kit)), smoke_(smoke) {}

protected:
    bool OnInit() override {
        if (!kit_.Open(kitPath_))
            return false;
        if (const char* base = SDL_GetBasePath()) {
            const auto content = fs::u8path(base) / "EngineContent";
            std::error_code ec;
            if (fs::is_directory(content, ec))
                AssetManager::Get().SetEngineContentRoot(content);
        }
        AssetManager::Get().SetProjectRoot(project_.GetRoot());
        ShaderManager::Get().SetShaderCacheMode(ShaderCacheMode::EditorOnDemandCompile);
        rhi_ = CreateRenderContext(backend_);
        if (!rhi_ || !rhi_->Init(&GetWindow()))
            return false;
        GetEngine().SetFatalHealthCheck([this]() -> std::optional<std::string> {
            return rhi_->IsDeviceLost() ? std::optional<std::string>("BUI viewport: RHI device lost") : std::nullopt;
        });
        view_ =
            new ViewportLayer(*rhi_, GetWindow(), GetEngine(), scene_, project_.GetGraphicsSettings(), kit_, smoke_);
        PushLayer(view_);
        if (!view_->IsReady()) {
            Logger::Error(kLabel, "Could not load scene ", scene_.u8string());
            return false;
        }
        GetEngine().SetPlatformEventBridge(view_);
        return true;
    }
    void OnBeforeLayersCleared() override {
        GetEngine().SetPlatformEventBridge(nullptr);
        if (smoke_ && (!view_ || !view_->SmokePassed())) {
            Logger::Error(kLabel, "Smoke did not complete; no platform Present result is asserted");
            GetEngine().SetExitCode(3);
        }
        view_ = nullptr;
    }
    void OnShutdown() override {
        if (rhi_)
            rhi_->Shutdown();
    }

private:
    RenderBackend backend_;
    ProjectConfig project_;
    fs::path scene_, kitPath_;
    unsigned smoke_;
    Kit kit_;
    std::unique_ptr<IRenderContext> rhi_;
    ViewportLayer* view_ = nullptr;
};

int Run(int argc, char** argv) {
    fs::path projectRoot = fs::current_path(), kit;
    std::string sceneArgument, backendArgument;
    unsigned smoke = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help") {
            SDL_Log("MyEngineBuiViewport --kit <absolute-library> [--project <path>] [--scene <project-relative>] "
                    "[--backend metal|d3d11|d3d12|vulkan] [--smoke-frames N]");
            return 0;
        }
        if (i + 1 == argc)
            return 2;
        const std::string value = argv[++i];
        if (option == "--kit")
            kit = fs::u8path(value);
        else if (option == "--project")
            projectRoot = fs::u8path(value);
        else if (option == "--scene")
            sceneArgument = value;
        else if (option == "--backend")
            backendArgument = value;
        else if (option == "--smoke-frames") {
            if (value.empty() || value.size() > 3 || value.find_first_not_of("0123456789") != std::string::npos)
                return 2;
            smoke = static_cast<unsigned>(std::stoi(value));
            if (smoke == 0)
                return 2;
        } else {
            return 2;
        }
    }
    std::error_code ec;
    if (!kit.is_absolute() || !fs::is_regular_file(kit, ec)) {
        Logger::Error(kLabel, "Specify the built BUI viewport kit with --kit <absolute-library>");
        return 2;
    }
    ProjectConfig project;
    fs::path scene;
    std::string error;
    if (!project.Open(projectRoot, false, &error) ||
        !(sceneArgument.empty() ? project.ResolveStartupScene(scene, &error)
                                : project.ResolveScenePath(sceneArgument, scene, true, &error))) {
        Logger::Error(kLabel, error);
        return 2;
    }
    ApplicationConfig config;
    config.window.title = "MyEngine / Native BUI Viewport";
    config.window.width = 1280;
    config.window.height = 800;
    config.engine.appName = "MyEngineBuiViewport";
    const auto backend =
        ParseRenderBackend(backendArgument.empty() ? project.GetGraphicsSettings().backend : backendArgument);
    if (backend && IsBackendCompiled(*backend))
        config.backend = *backend;
    else if (!backendArgument.empty()) {
        Logger::Error(kLabel, "Backend not available: ", backendArgument);
        return 2;
    }
    if (smoke)
        config.engine.autoQuitAfterSeconds = 120.f;
    BuiApp app(config, std::move(project), std::move(scene), std::move(kit), smoke);
    return app.Run();
}

} // namespace

int main(int argc, char** argv) {
    return SDL_RunApp(argc, argv, Run, nullptr);
}
