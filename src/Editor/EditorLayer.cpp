#include "Editor/EditorLayer.h"

#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/ModelAsset.h"
#include "Camera/Camera.h"
#include "Core/EngineMath.h"
#include "Core/EngineTime.h"
#include "Core/Logger.h"
#include "Core/Platform.h"
#include "Math/Mat4Inverse.h"
#include "Animation/SkinnedMeshRendererComponent.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Renderer/LightComponent.h"
#include "Renderer/PostProcessComponent.h"
#include "Renderer/ShaderManager.h"
#include "Scene/Actor.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptComponent.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {
constexpr size_t kMaxLogLines = 4096;
}

#if defined(MYENGINE_ENABLE_IMGUI)
namespace {
    constexpr float kToolbarHeight      = 40.0f;
    constexpr float kOutlinerPanelWidth = 280.0f;
    constexpr float kInspectorWidth     = 320.0f;
    constexpr float kLogPanelHeight     = 220.0f;

    bool DrawVec3Editor(const char* label, Vec3& value, float speed)
    {
        float values[3] = { value.x, value.y, value.z };
        if (!ImGui::DragFloat3(label, values, speed)) {
            return false;
        }
        value = Vec3{ values[0], values[1], values[2] };
        return true;
    }

    // ImGuizmo::matrix_t is a float[4][4] union layout 閳?same row-major packing as our Mat4.
    // Do not transpose; a prior bug used col-major shuffle and broke MVP / gizmo placement.
    void Mat4CopyToFloat16(const Mat4& m, float out[16])
    {
        static_assert(sizeof(Mat4) == sizeof(float) * 16, "Mat4 must be 16 floats");
        std::memcpy(out, m.m, sizeof(float) * 16);
    }

    void Mat4CopyFromFloat16(const float in[16], Mat4& out)
    {
        std::memcpy(out.m, in, sizeof(float) * 16);
    }

    bool IsShaderCompileErrorLine(const std::string& line)
    {
        return line.find("[ShaderCompileError]") != std::string::npos ||
               line.find("[ShaderCompilerD3D11]") != std::string::npos;
    }

    // Transform::GetLocalMatrix uses row-vector TRS: S * Ry * Rx * Rz * T.
    // Non-uniform scale therefore lives in rows; split out row scale before
    // ImGuizmo orthonormalizes the model matrix for rotate gizmo input.
    void ExtractOrthonormalRAndRowScale(const Mat4& w, float rowScale[3], Mat4& outR)
    {
        for (int i = 0; i < 3; ++i) {
            const float x = w.m[i][0];
            const float y = w.m[i][1];
            const float z = w.m[i][2];
            rowScale[i]   = std::sqrt(x * x + y * y + z * z);
            if (rowScale[i] < 1e-8f) {
                rowScale[i] = 1e-8f;
            }
        }
        outR             = Mat4::Identity();
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                outR.m[i][j] = w.m[i][j] / rowScale[i];
            }
        }
        outR.m[3][3] = 1.0f;
    }

    // Inverse of Transform::GetLocalMatrix(): upper 3x3 is diag(scale)*(Ry*Rx*Rz),
    // with rotation.x/y/z = pitch/yaw/roll in degrees. ImGuizmo's
    // DecomposeMatrixToComponents uses a different Euler order 閳?using it caused rotate
    // drift and apparent coupling to the camera.
    void DecomposeLocalMatrixToTransform(const Mat4& m, Vec3& outPos, Vec3& outRotDeg, Vec3& outScale)
    {
        outPos = Vec3{ m.m[3][0], m.m[3][1], m.m[3][2] };

        auto rowLen = [&](int r) {
            const float x = m.m[r][0];
            const float y = m.m[r][1];
            const float z = m.m[r][2];
            return std::sqrt(x * x + y * y + z * z);
        };
        outScale = Vec3{ rowLen(0), rowLen(1), rowLen(2) };

        const float s0 = outScale.x > 1e-8f ? outScale.x : 1e-8f;
        const float s1 = outScale.y > 1e-8f ? outScale.y : 1e-8f;
        const float s2 = outScale.z > 1e-8f ? outScale.z : 1e-8f;

        const float r02 = m.m[0][2] / s0;
        const float r12 = m.m[1][2] / s1;
        const float r22 = m.m[2][2] / s2;
        const float r10 = m.m[1][0] / s1;
        const float r11 = m.m[1][1] / s1;
        const float r00 = m.m[0][0] / s0;
        const float r01 = m.m[0][1] / s0;

        const float sp  = std::clamp(r12, -1.0f, 1.0f);
        const float pitch = std::asin(sp);
        float       yaw   = 0.0f;
        float       roll  = 0.0f;
        if (std::fabs(std::cos(pitch)) > 1e-4f) {
            yaw  = std::atan2(-r02, r22);
            roll = std::atan2(-r10, r11);
        } else {
            yaw  = std::atan2(r01, r00);
            roll = 0.0f;
        }

        outRotDeg =
            Vec3{ pitch * kRad2Deg, yaw * kRad2Deg, roll * kRad2Deg };
    }

    void MergeUniqueSorted(std::vector<std::string>& paths)
    {
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    }

    MeshHandle ResolveEditorMeshPath(const std::string& path)
    {
        AssetManager& am = AssetManager::Get();
        MeshHandle h = am.GetByPath<MeshAsset>(path);
        if (h.IsValid()) {
            return h;
        }
        if (path == "__builtin__/Triangle") {
            return am.GetTriangleMesh();
        }
        if (path == "__builtin__/Quad") {
            return am.GetQuadMesh();
        }
        if (path == "__builtin__/Cube") {
            return am.GetCubeMesh();
        }
        return am.Load<MeshAsset>(path);
    }

    MaterialHandle ResolveEditorMaterialPath(const std::string& path)
    {
        AssetManager& am = AssetManager::Get();
        MaterialHandle h = am.GetByPath<MaterialAsset>(path);
        if (h.IsValid()) {
            return h;
        }
        if (path == "__builtin__/Default") {
            return am.GetDefaultMaterial();
        }
        return am.Load<MaterialAsset>(path);
    }

    bool DrawComponentEnabled(Component& component)
    {
        bool enabled = component.IsEnabled();
        if (!ImGui::Checkbox("Enabled", &enabled)) {
            return false;
        }
        component.SetEnabled(enabled);
        return true;
    }

    bool DrawColliderCommon(ColliderComponent& collider)
    {
        bool changed = false;

        bool trigger = collider.IsTrigger();
        if (ImGui::Checkbox("Is Trigger", &trigger)) {
            collider.SetTrigger(trigger);
            changed = true;
        }

        int layer = static_cast<int>(collider.GetLayer());
        if (ImGui::InputInt("Layer", &layer)) {
            collider.SetLayer(static_cast<uint32_t>((std::max)(1, layer)));
            changed = true;
        }

        int layerMask = static_cast<int>(collider.GetLayerMask());
        if (ImGui::InputInt("Layer Mask", &layerMask)) {
            collider.SetLayerMask(static_cast<uint32_t>(layerMask));
            changed = true;
        }

        return changed;
    }

    static const SDL_DialogFileFilter kSceneJsonFilters[] = {
        { "Scene JSON", "json" },
    };

    static const SDL_DialogFileFilter kImportAssetFilters[] = {
        { "Importable Assets", "gltf;glb;png;jpg;jpeg" },
        { "Models", "gltf;glb" },
        { "Images", "png;jpg;jpeg" },
    };

    constexpr const char kModelDragPayloadType[] = "MYENGINE_MODEL_PATH";
    constexpr const char kTextureDragPayloadType[] = "MYENGINE_TEXTURE_PATH";

    std::string LowerExtension(const std::string& path)
    {
        std::string ext = std::filesystem::path(path).extension().string();
        if (!ext.empty() && ext[0] == '.') {
            ext.erase(ext.begin());
        }
        for (char& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return ext;
    }

    bool IsModelAssetPath(const std::string& path)
    {
        const std::string ext = LowerExtension(path);
        return ext == "obj" || ext == "gltf" || ext == "glb";
    }

    bool IsTextureAssetPath(const std::string& path)
    {
        const std::string ext = LowerExtension(path);
        return ext == "png" || ext == "jpg" || ext == "jpeg" ||
               ext == "bmp" || ext == "tga" || ext == "ppm" ||
               ext == "pgm" || ext == "pam" || ext == "hdr";
    }

    bool IsMaterialAssetPath(const std::string& path)
    {
        return LowerExtension(path) == "mat";
    }

    std::filesystem::path MakeUniqueContentPath(
        const std::filesystem::path& directory,
        const std::string& stem,
        const std::string& extension)
    {
        namespace fs = std::filesystem;
        fs::path candidate = directory / (stem + extension);
        int suffix = 1;
        while (fs::exists(candidate)) {
            candidate = directory / (stem + "_" + std::to_string(suffix++) + extension);
        }
        return candidate;
    }

    bool RayIntersectYPlane(const Math::Ray& ray, float planeY, float& outT)
    {
        const float dy = ray.direction.y;
        if (std::fabs(dy) < 1e-5f) {
            return false;
        }
        outT = (planeY - ray.origin.y) / dy;
        return outT > 1e-4f;
    }

    void HelpMarker(const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                                 ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", text);
        }
    }

    bool DrawEditableInspectorJson(nlohmann::json& fields)
    {
        if (!fields.is_object()) {
            fields = nlohmann::json::object();
        }

        bool changed = false;
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            const std::string label = it.key();
            nlohmann::json& value = it.value();
            ImGui::PushID(label.c_str());
            if (value.is_boolean()) {
                bool v = value.get<bool>();
                if (ImGui::Checkbox(label.c_str(), &v)) {
                    value = v;
                    changed = true;
                }
            } else if (value.is_number()) {
                float v = value.get<float>();
                if (ImGui::DragFloat(label.c_str(), &v, 0.05f)) {
                    value = v;
                    changed = true;
                }
            } else if (value.is_string()) {
                char buffer[256] = {};
                const std::string text = value.get<std::string>();
                std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
                if (ImGui::InputText(label.c_str(), buffer, sizeof(buffer))) {
                    value = std::string(buffer);
                    changed = true;
                }
            } else {
                ImGui::TextDisabled("%s: %s", label.c_str(), value.dump().c_str());
            }
            ImGui::PopID();
        }
        return changed;
    }

    std::filesystem::path ResolveEditorContentDirectory()
    {
        namespace fs = std::filesystem;
        try {
            const fs::path cwdTry = fs::current_path() / "Content";
            if (fs::is_directory(cwdTry)) {
                return fs::weakly_canonical(cwdTry);
            }
        } catch (...) {}

        const char* base = SDL_GetBasePath();
        if (base) {
            const fs::path baseDir(base);
            SDL_free((void*)base);
            const fs::path alt = baseDir / "Content";
            if (fs::is_directory(alt)) {
                try {
                    return fs::weakly_canonical(alt);
                } catch (...) {
                    return alt.lexically_normal();
                }
            }
        }
        return {};
    }
}

class EditorLayer::ImGuiPlatformEventBridge : public IPlatformEventBridge {
public:
    explicit ImGuiPlatformEventBridge(IRenderContext* renderContext)
        : m_RenderContext(renderContext) {}

    void OnSDLEvent(const SDL_Event& event) override {
        if (m_RenderContext) {
            m_RenderContext->ProcessImGuiSDLEvent(event);
        }
    }

private:
    IRenderContext* m_RenderContext = nullptr;
};

static void SDLCALL EditorOpenFileDialogCallback(void* userdata, const char* const* filelist, int /*filter*/)
{
    auto* self = static_cast<EditorLayer*>(userdata);
    std::lock_guard<std::mutex> lock(self->m_FileDialogMutex);
    self->m_PendingFilePath.clear();
    if (filelist && filelist[0]) {
        self->m_PendingFilePath = filelist[0];
        self->m_PendingFileOp   = EditorLayer::PendingFileOp::OpenScene;
    } else {
        self->m_PendingFileOp = EditorLayer::PendingFileOp::None;
    }
}

static void SDLCALL EditorSaveFileDialogCallback(void* userdata, const char* const* filelist, int /*filter*/)
{
    auto* self = static_cast<EditorLayer*>(userdata);
    std::lock_guard<std::mutex> lock(self->m_FileDialogMutex);
    self->m_PendingFilePath.clear();
    if (filelist && filelist[0]) {
        self->m_PendingFilePath = filelist[0];
        self->m_PendingFileOp   = EditorLayer::PendingFileOp::SaveScene;
    } else {
        self->m_PendingFileOp = EditorLayer::PendingFileOp::None;
    }
}

static void SDLCALL EditorImportAssetDialogCallback(void* userdata, const char* const* filelist, int /*filter*/)
{
    auto* self = static_cast<EditorLayer*>(userdata);
    std::lock_guard<std::mutex> lock(self->m_FileDialogMutex);
    self->m_PendingFilePath.clear();
    if (filelist && filelist[0]) {
        self->m_PendingFilePath = filelist[0];
        self->m_PendingFileOp   = EditorLayer::PendingFileOp::ImportAsset;
    } else {
        self->m_PendingFileOp = EditorLayer::PendingFileOp::None;
    }
}
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
    m_RenderContext = m_SceneLayer->GetRenderContext();
    if (!m_RenderContext) {
        Logger::Error("[Editor] Missing render context");
        return;
    }
    if (!m_Window->GetSDLWindow()) {
        Logger::Error("[Editor] ImGui requires SDLWindow (SDL_Window*)");
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());

    if (!m_RenderContext->InitImGui(m_Window)) {
        Logger::Error("[Editor] Failed to initialize ImGui backend from render context");
        ImGui::DestroyContext();
        return;
    }

    if (m_Engine) {
        m_PlatformBridge = std::make_unique<ImGuiPlatformEventBridge>(m_RenderContext);
        m_Engine->SetPlatformEventBridge(m_PlatformBridge.get());
    }

    Logger::SetSink([this](const std::string& line) { OnLogMessage(line); });
    RefreshAssetBrowserListing();
    RefreshShaderWatchList();
    m_ImGuiReady = true;
#endif
}

void EditorLayer::OnDetach()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    Logger::SetSink({});
    if (m_Engine) {
        m_Engine->SetPlatformEventBridge(nullptr);
    }
    m_PlatformBridge.reset();
    if (m_ImGuiReady) {
        if (m_RenderContext) {
            m_RenderContext->ShutdownImGui();
        }
        ImGui::DestroyContext();
        m_ImGuiReady = false;
    }
    m_RenderContext = nullptr;
#endif
}

void EditorLayer::OnUpdate(float dt)
{
    (void)dt;
#if defined(MYENGINE_ENABLE_IMGUI)
    ProcessPendingFileDialogs();
    ValidateSelection();
    PollShaderChanges();
#endif
}

void EditorLayer::OnRender()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_SceneLayer || !m_ImGuiReady || !m_RenderContext) return;
    ValidateSelection();

    m_RenderContext->BeginImGuiFrame();

    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    ImGuizmo::AllowAxisFlip(false);

    DrawToolbar();
    DrawSceneView();
    DrawSceneOutliner();
    DrawInspector();
    DrawLogOutput();
    DrawAssetBrowser();

    ImGui::Render();

    m_RenderContext->RenderImGuiDrawData(ImGui::GetDrawData());

    m_RenderContext->EndFrame();
#endif
}

#if defined(MYENGINE_ENABLE_IMGUI)
void EditorLayer::ProcessPendingFileDialogs()
{
    std::string path;
    PendingFileOp op = PendingFileOp::None;
    {
        std::lock_guard<std::mutex> lock(m_FileDialogMutex);
        if (m_PendingFileOp == PendingFileOp::None) {
            return;
        }
        op   = m_PendingFileOp;
        path = m_PendingFilePath;
        m_PendingFileOp = PendingFileOp::None;
    }

    if (op == PendingFileOp::OpenScene) {
        if (!path.empty()) {
            if (!m_SceneLayer->LoadScene(path)) {
                Logger::Error("[Editor] Failed to open scene: ", path);
            } else {
                SelectActor(nullptr);
            }
        }
        return;
    }

    if (op == PendingFileOp::SaveScene) {
        if (!path.empty()) {
            if (!m_SceneLayer->SaveSceneAs(path)) {
                Logger::Error("[Editor] Failed to save scene: ", path);
            }
        }
        return;
    }

    if (op == PendingFileOp::ImportAsset) {
        if (!path.empty()) {
            ImportAssetToContent(path);
        }
    }
}

void EditorLayer::RequestOpenSceneDialog()
{
    if (!m_Window || !m_Window->GetSDLWindow()) return;
    SDL_ShowOpenFileDialog(
        &EditorOpenFileDialogCallback,
        this,
        m_Window->GetSDLWindow(),
        kSceneJsonFilters,
        1,
        nullptr,
        false);
}

void EditorLayer::RequestSaveSceneDialog()
{
    if (!m_Window || !m_Window->GetSDLWindow()) return;
    SDL_ShowSaveFileDialog(
        &EditorSaveFileDialogCallback,
        this,
        m_Window->GetSDLWindow(),
        kSceneJsonFilters,
        1,
        nullptr);
}

void EditorLayer::RequestImportAssetDialog()
{
    if (!m_Window || !m_Window->GetSDLWindow()) return;
    SDL_ShowOpenFileDialog(
        &EditorImportAssetDialogCallback,
        this,
        m_Window->GetSDLWindow(),
        kImportAssetFilters,
        3,
        nullptr,
        false);
}

void EditorLayer::TryPickActorFromSceneView(float screenX, float screenY)
{
    Math::Ray ray{};
    if (!m_SceneLayer->BuildRayFromScreen(screenX, screenY, ray)) {
        return;
    }

    Actor* best   = nullptr;
    float  bestT  = FLT_MAX;

    Scene& scene = m_SceneLayer->GetScene();
    scene.ForEach([&](Actor& actor) {
        auto* mr = actor.GetComponent<MeshRendererComponent>();
        if (!mr || !mr->IsValid()) {
            return;
        }
        const MeshAsset* mesh = mr->GetMesh().Get();
        if (!mesh) {
            return;
        }

        const AABB worldAABB = TransformAABB(mesh->GetAABB(), actor.GetWorldMatrix());
        float      t0 = 0.0f;
        float      t1 = 0.0f;
        if (!worldAABB.IntersectRay(ray, t0, t1)) {
            return;
        }
        if (t1 < 0.0f) {
            return;
        }

        float tHit = t0;
        if (t0 < 0.0f) {
            tHit = 0.0f;
        }

        if (tHit < bestT) {
            bestT = tHit;
            best  = &actor;
        }
    });

    SelectActor(best);
}

void EditorLayer::SelectActor(Actor* actor)
{
    m_Selected = actor;
    m_SelectedID = actor ? actor->GetID() : 0;
    m_SelectedScene = (actor && m_SceneLayer) ? &m_SceneLayer->GetScene() : nullptr;
}

void EditorLayer::ValidateSelection()
{
    if (!m_SceneLayer || m_SelectedID == 0) {
        m_Selected = nullptr;
        m_SelectedID = 0;
        m_SelectedScene = nullptr;
        return;
    }

    const Scene* currentScene = &m_SceneLayer->GetScene();
    if (m_SelectedScene != currentScene) {
        m_Selected = nullptr;
        m_SelectedID = 0;
        m_SelectedScene = nullptr;
        return;
    }

    Actor* live = m_SceneLayer->GetScene().FindByID(m_SelectedID);
    if (!live) {
        m_Selected = nullptr;
        m_SelectedID = 0;
        m_SelectedScene = nullptr;
        return;
    }
    m_Selected = live;
}

void EditorLayer::RefreshAssetBrowserListing()
{
    namespace fs = std::filesystem;
    m_AssetBrowserRelPaths.clear();
    m_AssetBrowserRootAbs.clear();

    const fs::path root = ResolveEditorContentDirectory();
    if (root.empty() || !fs::is_directory(root)) {
        return;
    }

    try {
        m_AssetBrowserRootAbs = fs::absolute(root).lexically_normal().string();
    } catch (...) {
        m_AssetBrowserRootAbs = root.lexically_normal().string();
    }

    try {
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const fs::path rel = fs::relative(entry.path(), root);
            m_AssetBrowserRelPaths.push_back(rel.generic_string());
        }
    } catch (...) {
        // Ignore unreadable directories or permission errors.
    }

    std::sort(m_AssetBrowserRelPaths.begin(), m_AssetBrowserRelPaths.end());
}

bool EditorLayer::ImportAssetToContent(const std::string& sourcePath)
{
    namespace fs = std::filesystem;
    if (sourcePath.empty()) return false;

    fs::path source(sourcePath);
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        Logger::Warn("[Editor] Import source is not a file: ", sourcePath);
        return false;
    }

    fs::path root = !m_AssetBrowserRootAbs.empty()
        ? fs::path(m_AssetBrowserRootAbs)
        : ResolveEditorContentDirectory();
    if (root.empty()) {
        root = fs::current_path() / "Content";
    }
    fs::create_directories(root, ec);
    if (ec) {
        Logger::Error("[Editor] Failed to create Content directory: ", root.string());
        return false;
    }

    const std::string sourceString = source.string();
    const bool model = IsModelAssetPath(sourceString);
    const bool texture = IsTextureAssetPath(sourceString);
    if (!model && !texture) {
        Logger::Warn("[Editor] Unsupported import asset: ", sourcePath);
        return false;
    }

    const fs::path targetDir = root / (model ? "Models" : "Textures");
    fs::create_directories(targetDir, ec);
    if (ec) {
        Logger::Error("[Editor] Failed to create import directory: ", targetDir.string());
        return false;
    }

    const fs::path target = MakeUniqueContentPath(
        targetDir,
        source.stem().string(),
        source.extension().string());
    fs::copy_file(source, target, fs::copy_options::none, ec);
    if (ec) {
        Logger::Error("[Editor] Failed to import asset: ", ec.message());
        return false;
    }

    if (LowerExtension(sourceString) == "gltf") {
        const fs::path sourceBin = source.parent_path() / (source.stem().string() + ".bin");
        if (fs::is_regular_file(sourceBin, ec)) {
            const fs::path targetBin = target.parent_path() / sourceBin.filename();
            fs::copy_file(sourceBin, targetBin, fs::copy_options::skip_existing, ec);
            ec.clear();
        }
    }

    RefreshAssetBrowserListing();
    m_SelectedAssetAbsPath = target.lexically_normal().string();
    std::error_code relError;
    m_SelectedAssetRelPath = fs::relative(target, root, relError).generic_string();
    if (relError) {
        m_SelectedAssetRelPath = target.filename().generic_string();
    }

    if (texture) {
        AssetManager::Get().Load<TextureAsset>(m_SelectedAssetAbsPath);
    } else if (model) {
        AssetManager::Get().Load<ModelAsset>(m_SelectedAssetAbsPath);
    }
    Logger::Info("[Editor] Imported asset: ", m_SelectedAssetAbsPath);
    return true;
}

void EditorLayer::RefreshShaderWatchList()
{
    namespace fs = std::filesystem;
    m_WatchedShaders.clear();
    m_ShaderWriteTimes.clear();

    const fs::path root = fs::path("src/Runtime/Renderer/Shaders");
    if (!fs::exists(root) || !fs::is_directory(root)) {
        Logger::Warn("[Editor] Shader folder not found: ", root.generic_string());
        return;
    }

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".hlsl" && ext != ".hlsli") continue;
        const std::string path = entry.path().generic_string();
        m_WatchedShaders.push_back(path);
        m_ShaderWriteTimes[path] = fs::last_write_time(entry.path());
    }
}

void EditorLayer::PollShaderChanges()
{
    m_ShaderWatchAccumulator += Time::DeltaSeconds();
    if (m_ShaderWatchAccumulator < 0.5f) return;
    m_ShaderWatchAccumulator = 0.0f;

    namespace fs = std::filesystem;
    if (m_WatchedShaders.empty()) {
        RefreshShaderWatchList();
    }

    for (const std::string& path : m_WatchedShaders) {
        std::error_code ec;
        const fs::file_time_type t = fs::last_write_time(path, ec);
        if (ec) continue;
        auto it = m_ShaderWriteTimes.find(path);
        if (it == m_ShaderWriteTimes.end()) {
            m_ShaderWriteTimes[path] = t;
            continue;
        }
        if (it->second != t) {
            it->second = t;
            Logger::Info("[Editor] Hot-reload shader: ", path);
            ShaderManager::Get().Recompile(path);
        }
    }
}

void EditorLayer::TryCreateMeshActorFromDroppedModel(const std::string& absModelPath,
                                                     float              screenX,
                                                     float              screenY)
{
    if (!m_SceneLayer || absModelPath.empty() || !IsModelAssetPath(absModelPath)) {
        return;
    }

    ModelHandle model = AssetManager::Get().Load<ModelAsset>(absModelPath);
    if (!model || !model->GetMesh()) {
        Logger::Warn("[Editor] Failed to load model: ", absModelPath);
        return;
    }

    MeshHandle     mesh = model->GetMesh();
    MaterialHandle mat  = model->GetMaterial(0);
    if (!mat) {
        mat = AssetManager::Get().GetDefaultMaterial();
    }

    std::string stem = std::filesystem::path(absModelPath).stem().string();
    if (stem.empty()) {
        stem = "Mesh";
    }

    Actor* actor = m_SceneLayer->GetScene().CreateActor(stem);
    actor->AddComponent<MeshRendererComponent>();
    auto* mr = actor->GetComponent<MeshRendererComponent>();
    if (!mr) {
        Logger::Error("[Editor] Failed to add MeshRenderer");
        m_SceneLayer->GetScene().DestroyActor(actor);
        return;
    }
    mr->SetMesh(mesh);
    mr->SetMaterial(mat);

    Vec3 spawnPos{ 0.0f, 0.0f, 0.0f };
    Math::Ray ray{};
    if (m_SceneLayer->BuildRayFromScreen(screenX, screenY, ray)) {
        float t = 0.0f;
        if (RayIntersectYPlane(ray, 0.0f, t)) {
            spawnPos = ray.At(t);
        } else {
            Camera& cam = m_SceneLayer->GetCamera();
            spawnPos = cam.GetPosition() + cam.GetForward() * 8.0f;
        }
    } else {
        Camera& cam = m_SceneLayer->GetCamera();
        spawnPos = cam.GetPosition() + cam.GetForward() * 8.0f;
    }

    actor->GetTransform().position = spawnPos;
    m_SceneLayer->MarkDirty();
    SelectActor(actor);
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
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, kToolbarHeight));

    ImGui::Begin("##Toolbar", nullptr, flags);

    const bool editing = m_SceneLayer->IsEditing();
    if (ImGui::Button("New Scene")) {
        m_SceneLayer->NewScene("Untitled");
        SelectActor(nullptr);
    }
    ImGui::SameLine();

    if (ImGui::Button("Open Scene")) {
        RequestOpenSceneDialog();
    }
    ImGui::SameLine();

    ImGui::BeginDisabled(!editing);
    if (ImGui::Button("Save Scene")) {
        if (m_SceneLayer->HasFilePath()) {
            if (!m_SceneLayer->SaveScene()) {
                Logger::Error("[Editor] SaveScene failed");
            }
        } else {
            RequestSaveSceneDialog();
        }
    }
    ImGui::EndDisabled();
    if (!editing) {
        HelpMarker("Runtime scene cannot be saved. Stop Play mode first.");
    }
    ImGui::SameLine();

    if (ImGui::Button("Recompile All Shaders")) {
        ShaderManager::Get().RecompileAll();
    }
    ImGui::SameLine();

    const SceneRunState runState = m_SceneLayer->GetRunState();
    if (runState == SceneRunState::Edit) {
        if (ImGui::Button("Play")) {
            SelectActor(nullptr);
            m_SceneLayer->BeginPlay();
        }
    } else {
        if (ImGui::Button("Stop")) {
            SelectActor(nullptr);
            m_SceneLayer->StopPlay();
        }
        ImGui::SameLine();
        if (runState == SceneRunState::Play) {
            if (ImGui::Button("Pause")) m_SceneLayer->PausePlay();
        } else {
            if (ImGui::Button("Resume")) m_SceneLayer->ResumePlay();
        }
        ImGui::SameLine();
        if (ImGui::Button("Step")) m_SceneLayer->StepPlay();
    }
    if (m_Engine) {
        const FrameStats& stats = m_Engine->GetFrameStats();
        ImGui::SameLine();
        ImGui::Text("FPS %.1f | Frame %.2f ms | U %.2f | R %.2f",
                    stats.fps, stats.smoothedFrameMs,
                    stats.updateMs, stats.renderMs);
    }

    ImGui::End();
#endif
}

void EditorLayer::DrawActorNode(Actor* actor)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!actor) return;

    const bool selected = (m_SelectedID != 0 && m_SelectedID == actor->GetID());
    const bool hasChildren = !actor->GetChildren().empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanFullWidth;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected)     flags |= ImGuiTreeNodeFlags_Selected;

    const bool open = ImGui::TreeNodeEx((void*)actor, flags, "%s", actor->GetName().c_str());
    if (ImGui::IsItemClicked()) {
        SelectActor(actor);
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
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float outlinerH = vp->WorkSize.y - kToolbarHeight - kLogPanelHeight;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kOutlinerPanelWidth, outlinerH), ImGuiCond_Always);

    ImGui::Begin("Scene Outliner");

    const bool editing = m_SceneLayer->IsEditing();
    if (!editing) {
        ImGui::TextDisabled("Runtime scene is read-only here. Stop Play mode to edit.");
    }

    ImGui::BeginDisabled(!editing);
    if (ImGui::Button("Create Actor")) {
        m_SceneLayer->GetScene().CreateActor("Actor");
        m_SceneLayer->MarkDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Selected")) {
        if (m_Selected) {
            Actor* victim = m_Selected;
            SelectActor(nullptr);
            m_SceneLayer->GetScene().DestroyActor(victim);
            m_SceneLayer->MarkDirty();
        }
    }
    ImGui::EndDisabled();

    Scene& scene = m_SceneLayer->GetScene();
    auto   roots = scene.GetRootActors();

    for (Actor* actor : roots) {
        DrawActorNode(actor);
    }

    ImGui::End();
#endif
}

void EditorLayer::DrawSceneView()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float centerX = vp->WorkPos.x + kOutlinerPanelWidth;
    const float centerW = vp->WorkSize.x - kOutlinerPanelWidth - kInspectorWidth;
    const float sceneY = vp->WorkPos.y + kToolbarHeight;
    const float sceneH = vp->WorkSize.y - kToolbarHeight - kLogPanelHeight;

    if (centerW <= 1.0f || sceneH <= 1.0f) {
        m_SceneLayer->SetViewportInputEnabled(false);
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(centerX, sceneY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(centerW, sceneH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (!ImGui::Begin("Scene View", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleVar();
        m_SceneLayer->SetViewportInputEnabled(false);
        return;
    }

    const ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
    const ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
    const ImVec2 winPos = ImGui::GetWindowPos();
    const float viewportX = winPos.x + contentMin.x;
    const float viewportY = winPos.y + contentMin.y;
    const float viewportW = contentMax.x - contentMin.x;
    const float viewportH = contentMax.y - contentMin.y;

    if (viewportW > 1.0f && viewportH > 1.0f) {
        m_SceneLayer->SetEditorViewportRect(
            static_cast<int>(viewportX),
            static_cast<int>(viewportY),
            static_cast<int>(viewportW),
            static_cast<int>(viewportH));
    }

    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    m_SceneLayer->SetViewportInputEnabled(hovered);

    if (hovered) {
        ImGuiIO& ioKeys = ImGui::GetIO();
        if (!ioKeys.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) {
                m_GizmoOperation = ImGuizmo::TRANSLATE;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E)) {
                m_GizmoOperation = ImGuizmo::ROTATE;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                m_GizmoOperation = ImGuizmo::SCALE;
            }
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(viewportX, viewportY));
    if (void* sceneTexture = m_SceneLayer->GetSceneColorTextureHandle()) {
        ImGui::Image(
            reinterpret_cast<ImTextureID>(sceneTexture),
            ImVec2(viewportW, viewportH),
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f));
    } else {
        ImGui::Dummy(ImVec2(viewportW, viewportH));
    }
    const bool editing = m_SceneLayer->IsEditing();
    if (editing && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kModelDragPayloadType)) {
            if (payload->Data && payload->DataSize > 0) {
                const char* path = static_cast<const char*>(payload->Data);
                const ImVec2 mousePos = ImGui::GetMousePos();
                TryCreateMeshActorFromDroppedModel(path, mousePos.x, mousePos.y);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Manipulate() must run before mouse picking so IsOver()/IsUsing() match this frame's gizmo.
    // Gizmo is placed at mesh AABB center in world space (falls back to actor pivot when no mesh).
    if (editing && m_Selected && viewportW > 1.0f && viewportH > 1.0f) {
        Camera& cam = m_SceneLayer->GetCamera();
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(viewportX, viewportY, viewportW, viewportH);
        ImGuizmo::SetOrthographic(cam.GetProjectionMode() == ProjectionMode::Orthographic);

        float viewM[16];
        float projM[16];
        // Keep the same view-projection as MainPass, while adapting inverse-view
        // camera direction to ImGuizmo's right-handed expectation. For row vectors:
        // (view * zFlip) * (zFlip * proj) == view * proj.
        Mat4 zFlip = Mat4::Identity();
        zFlip.m[2][2] = -1.0f;
        const Mat4 gizmoView = cam.GetView() * zFlip;
        const Mat4 gizmoProj = zFlip * cam.GetProj();
        Mat4CopyToFloat16(gizmoView, viewM);
        Mat4CopyToFloat16(gizmoProj, projM);

        Mat4 world      = m_Selected->GetWorldMatrix();
        Vec3 pivotModel = Vec3::Zero();
        Vec3 pivotWorld = world.TransformPoint(Vec3::Zero());
        if (auto* mrPick = m_Selected->GetComponent<MeshRendererComponent>()) {
            if (mrPick->IsValid()) {
                if (const MeshAsset* meshAsset = mrPick->GetMesh().Get()) {
                    pivotModel = meshAsset->GetAABB().Center();
                    pivotWorld = world.TransformPoint(pivotModel);
                }
            }
        }

        float        rotateRowScale[3] = { 1.0f, 1.0f, 1.0f };
        Mat4         gizmoLinear       = world;
        const bool   rotateGizmo       = (m_GizmoOperation == ImGuizmo::ROTATE);
        if (rotateGizmo) {
            Mat4 R{};
            ExtractOrthonormalRAndRowScale(world, rotateRowScale, R);
            gizmoLinear = R;
        }

        Mat4 gizmoMat = gizmoLinear;
        gizmoMat.m[3][0] = pivotWorld.x;
        gizmoMat.m[3][1] = pivotWorld.y;
        gizmoMat.m[3][2] = pivotWorld.z;

        float matrix[16];
        Mat4CopyToFloat16(gizmoMat, matrix);

        const bool manipulated = ImGuizmo::Manipulate(
            viewM,
            projM,
            m_GizmoOperation,
            m_GizmoMode,
            matrix,
            nullptr,
            nullptr);

        if (manipulated) {
            Mat4 gizmoNew{};
            Mat4CopyFromFloat16(matrix, gizmoNew);

            Mat4 oriented = gizmoNew;
            if (rotateGizmo) {
                for (int i = 0; i < 3; ++i) {
                    for (int j = 0; j < 3; ++j) {
                        oriented.m[i][j] = gizmoNew.m[i][j] * rotateRowScale[i];
                    }
                }
            }

            Mat4 linearOnly = oriented;
            linearOnly.m[3][0] = linearOnly.m[3][1] = linearOnly.m[3][2] = 0.0f;
            linearOnly.m[3][3]                                               = 1.0f;
            const Vec3 tCenter(gizmoNew.m[3][0], gizmoNew.m[3][1], gizmoNew.m[3][2]);
            const Vec3 pivotScaled = linearOnly.TransformPoint(pivotModel);
            const Vec3 tOrigin     = tCenter - pivotScaled;

            Mat4 worldNew     = oriented;
            worldNew.m[3][0]  = tOrigin.x;
            worldNew.m[3][1]  = tOrigin.y;
            worldNew.m[3][2]  = tOrigin.z;
            worldNew.m[3][3]  = 1.0f;

            Mat4 localNew = worldNew;
            if (Actor* parent = m_Selected->GetParent()) {
                Mat4 invParent{};
                if (Mat4Invert(parent->GetWorldMatrix(), invParent)) {
                    localNew = worldNew * invParent;
                }
            }

            Vec3 tr;
            Vec3 rot;
            Vec3 sc;
            DecomposeLocalMatrixToTransform(localNew, tr, rot, sc);
            Transform& t = m_Selected->GetTransform();
            t.position = tr;
            t.rotation = rot;
            t.scale    = sc;
            m_SceneLayer->MarkDirty();
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
        TryPickActorFromSceneView(io.MousePos.x, io.MousePos.y);
    }

    ImGui::End();
    ImGui::PopStyleVar();
#endif
}

#if defined(MYENGINE_ENABLE_IMGUI)
void EditorLayer::DrawAddComponentInspector(Actor* actor)
{
    if (!actor) return;

    ImGui::Separator();
    ImGui::TextUnformatted("Add Component");

    const char* preview = "Select component...";
    if (ImGui::BeginCombo("##AddComponent", preview)) {
        const std::vector<std::string> types = ComponentRegistry::Get().GetRegisteredTypes();
        for (const std::string& type : types) {
            const bool alreadyAdded = actor->HasComponentType(type);
            if (alreadyAdded) {
                ImGui::BeginDisabled();
            }

            const std::string label = alreadyAdded ? (type + " (added)") : type;
            if (ImGui::Selectable(label.c_str(), false) && !alreadyAdded) {
                Component* component = ComponentRegistry::Get().Create(type, *actor);
                if (component) {
                    if (auto* meshRenderer = actor->GetComponent<MeshRendererComponent>()) {
                        if (!meshRenderer->GetMesh().IsValid()) {
                            meshRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
                        }
                        if (!meshRenderer->GetMaterial().IsValid()) {
                            meshRenderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
                        }
                    }
                    if (auto* skinned = actor->GetComponent<SkinnedMeshRendererComponent>()) {
                        if (!skinned->GetMaterial().IsValid()) {
                            skinned->SetMaterial(AssetManager::Get().GetDefaultMaterial());
                        }
                    }
                    m_SceneLayer->MarkDirty();
                }
            }

            if (alreadyAdded) {
                ImGui::EndDisabled();
            }
        }
        ImGui::EndCombo();
    }
}

void EditorLayer::DrawPhysicsInspector(Actor* actor)
{
    if (!actor) return;

    if (auto* rb = actor->GetComponent<RigidBodyComponent>()) {
        ImGui::Separator();
        ImGui::PushID("RigidBody");
        ImGui::TextUnformatted("RigidBody");
        bool changed = DrawComponentEnabled(*rb);

        const char* bodyTypes[] = { "Static", "Dynamic" };
        int bodyType = rb->GetBodyType() == BodyType::Static ? 0 : 1;
        if (ImGui::Combo("Body Type", &bodyType, bodyTypes, 2)) {
            rb->SetBodyType(bodyType == 0 ? BodyType::Static : BodyType::Dynamic);
            changed = true;
        }

        float mass = rb->GetMass();
        if (ImGui::DragFloat("Mass", &mass, 0.05f, 0.0001f, 100000.0f)) {
            rb->SetMass(mass);
            changed = true;
        }

        Vec3 velocity = rb->GetVelocity();
        if (DrawVec3Editor("Velocity", velocity, 0.05f)) {
            rb->SetVelocity(velocity);
            changed = true;
        }

        float restitution = rb->GetRestitution();
        if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f)) {
            rb->SetRestitution(restitution);
            changed = true;
        }

        float damping = rb->GetLinearDamping();
        if (ImGui::DragFloat("Linear Damping", &damping, 0.01f, 0.0f, 100.0f)) {
            rb->SetLinearDamping(damping);
            changed = true;
        }

        float friction = rb->GetFriction();
        if (ImGui::SliderFloat("Friction", &friction, 0.0f, 2.0f)) {
            rb->SetFriction(friction);
            changed = true;
        }

        bool useGravity = rb->UsesGravity();
        if (ImGui::Checkbox("Use Gravity", &useGravity)) {
            rb->SetUseGravity(useGravity);
            changed = true;
        }

        ImGui::Text("Sleeping: %s", rb->IsSleeping() ? "yes" : "no");
        if (changed) m_SceneLayer->MarkDirty();
        if (ImGui::Button("Remove RigidBody")) {
            actor->RemoveComponent<RigidBodyComponent>();
            m_SceneLayer->MarkDirty();
            ImGui::PopID();
            return;
        }
        ImGui::PopID();
    }

    if (auto* box = actor->GetComponent<BoxColliderComponent>()) {
        ImGui::Separator();
        ImGui::PushID("BoxCollider");
        ImGui::TextUnformatted("BoxCollider");
        bool changed = DrawComponentEnabled(*box);
        changed |= DrawColliderCommon(*box);
        Vec3 halfExtents = box->GetHalfExtents();
        if (DrawVec3Editor("Half Extents", halfExtents, 0.02f)) {
            box->SetHalfExtents(halfExtents);
            changed = true;
        }
        if (changed) m_SceneLayer->MarkDirty();
        if (ImGui::Button("Remove BoxCollider")) {
            actor->RemoveComponent<BoxColliderComponent>();
            m_SceneLayer->MarkDirty();
            ImGui::PopID();
            return;
        }
        ImGui::PopID();
    }

    if (auto* sphere = actor->GetComponent<SphereColliderComponent>()) {
        ImGui::Separator();
        ImGui::PushID("SphereCollider");
        ImGui::TextUnformatted("SphereCollider");
        bool changed = DrawComponentEnabled(*sphere);
        changed |= DrawColliderCommon(*sphere);
        float radius = sphere->GetRadius();
        if (ImGui::DragFloat("Radius", &radius, 0.02f, 0.001f, 100000.0f)) {
            sphere->SetRadius(radius);
            changed = true;
        }
        if (changed) m_SceneLayer->MarkDirty();
        if (ImGui::Button("Remove SphereCollider")) {
            actor->RemoveComponent<SphereColliderComponent>();
            m_SceneLayer->MarkDirty();
            ImGui::PopID();
            return;
        }
        ImGui::PopID();
    }

    if (auto* capsule = actor->GetComponent<CapsuleColliderComponent>()) {
        ImGui::Separator();
        ImGui::PushID("CapsuleCollider");
        ImGui::TextUnformatted("CapsuleCollider");
        bool changed = DrawComponentEnabled(*capsule);
        changed |= DrawColliderCommon(*capsule);
        float radius = capsule->GetRadius();
        if (ImGui::DragFloat("Radius", &radius, 0.02f, 0.001f, 100000.0f)) {
            capsule->SetRadius(radius);
            changed = true;
        }
        float halfHeight = capsule->GetHalfHeight();
        if (ImGui::DragFloat("Half Height", &halfHeight, 0.02f, 0.0f, 100000.0f)) {
            capsule->SetHalfHeight(halfHeight);
            changed = true;
        }
        if (changed) m_SceneLayer->MarkDirty();
        if (ImGui::Button("Remove CapsuleCollider")) {
            actor->RemoveComponent<CapsuleColliderComponent>();
            m_SceneLayer->MarkDirty();
            ImGui::PopID();
            return;
        }
        ImGui::PopID();
    }

    if (auto* controller = actor->GetComponent<CharacterControllerComponent>()) {
        ImGui::Separator();
        ImGui::PushID("CharacterController");
        ImGui::TextUnformatted("CharacterController");
        bool changed = DrawComponentEnabled(*controller);
        Vec3 velocity = controller->GetVelocity();
        if (DrawVec3Editor("Velocity", velocity, 0.05f)) {
            controller->Move(velocity);
            changed = true;
        }
        bool useGravity = controller->UsesGravity();
        if (ImGui::Checkbox("Use Gravity", &useGravity)) {
            controller->SetUseGravity(useGravity);
            changed = true;
        }
        float stepOffset = controller->GetStepOffset();
        if (ImGui::DragFloat("Step Offset", &stepOffset, 0.01f, 0.0f, 1000.0f)) {
            controller->SetStepOffset(stepOffset);
            changed = true;
        }
        ImGui::Text("Grounded: %s", controller->IsGrounded() ? "yes" : "no");
        if (changed) m_SceneLayer->MarkDirty();
        if (ImGui::Button("Remove CharacterController")) {
            actor->RemoveComponent<CharacterControllerComponent>();
            m_SceneLayer->MarkDirty();
            ImGui::PopID();
            return;
        }
        ImGui::PopID();
    }
}

void EditorLayer::DrawSkinnedMeshInspector(Actor* actor)
{
    if (!actor) return;

    auto* skinned = actor->GetComponent<SkinnedMeshRendererComponent>();
    if (!skinned) return;

    ImGui::Separator();
    ImGui::PushID("SkinnedMeshRenderer");
    ImGui::TextUnformatted("Skinned Mesh Renderer");
    bool changed = DrawComponentEnabled(*skinned);

    std::vector<std::string> meshPaths = {
        "__builtin__/Triangle",
        "__builtin__/Quad",
        "__builtin__/Cube",
    };
    {
        auto extra = AssetManager::Get().GetCachedPathsByType(AssetType::Mesh);
        meshPaths.insert(meshPaths.end(), extra.begin(), extra.end());
        MergeUniqueSorted(meshPaths);
    }

    MeshHandle sourceMesh = skinned->GetSourceMesh();
    const std::string meshCurrent = sourceMesh.IsValid() ? sourceMesh->GetPath() : "";
    const char* meshPreview = meshCurrent.empty() ? "(none)" : meshCurrent.c_str();
    if (ImGui::BeginCombo("Source Mesh", meshPreview)) {
        for (const std::string& p : meshPaths) {
            const bool selected = (p == meshCurrent);
            if (ImGui::Selectable(p.c_str(), selected)) {
                MeshHandle h = ResolveEditorMeshPath(p);
                if (h.IsValid()) {
                    skinned->SetSourceMesh(h);
                    changed = true;
                } else {
                    Logger::Warn("[Editor] Failed to resolve skinned mesh source: ", p);
                }
            }
        }
        ImGui::EndCombo();
    }

    std::vector<std::string> matPaths = { "__builtin__/Default" };
    {
        auto extra = AssetManager::Get().GetCachedPathsByType(AssetType::Material);
        matPaths.insert(matPaths.end(), extra.begin(), extra.end());
        MergeUniqueSorted(matPaths);
    }

    MaterialHandle mat = skinned->GetMaterial();
    const std::string matCurrent = mat.IsValid() ? mat->GetPath() : "";
    const char* matPreview = matCurrent.empty() ? "(none)" : matCurrent.c_str();
    if (ImGui::BeginCombo("Material", matPreview)) {
        for (const std::string& p : matPaths) {
            const bool selected = (p == matCurrent);
            if (ImGui::Selectable(p.c_str(), selected)) {
                MaterialHandle mh = ResolveEditorMaterialPath(p);
                if (mh.IsValid()) {
                    skinned->SetMaterial(mh);
                    changed = true;
                } else {
                    Logger::Warn("[Editor] Failed to resolve skinned material: ", p);
                }
            }
        }
        ImGui::EndCombo();
    }

    if (DrawMaterialAssetInspector(skinned->GetMaterial())) {
        changed = true;
    }

    bool playing = skinned->IsPlaying();
    if (ImGui::Checkbox("Playing", &playing)) {
        skinned->SetPlaying(playing);
        changed = true;
    }

    float time = skinned->GetAnimationTime();
    if (ImGui::DragFloat("Animation Time", &time, 0.01f, 0.0f, 100000.0f)) {
        skinned->SetAnimationTime(time);
        changed = true;
    }

    float blendWeight = skinned->GetBlendWeight();
    if (ImGui::SliderFloat("Blend Weight", &blendWeight, 0.0f, 1.0f)) {
        skinned->SetBlendWeight(blendWeight);
        changed = true;
    }

    ImGui::Text("Bones: %zu", skinned->GetBones().size());
    ImGui::Text("Weights: %zu", skinned->GetWeights().size());
    ImGui::Text("Skin Matrices: %zu", skinned->GetSkinMatrices().size());
    ImGui::Text("GPU Skinning: %s", skinned->UsesGpuSkinning() ? "yes" : "no");

    if (changed) m_SceneLayer->MarkDirty();
    if (ImGui::Button("Remove SkinnedMeshRenderer")) {
        actor->RemoveComponent<SkinnedMeshRendererComponent>();
        m_SceneLayer->MarkDirty();
        ImGui::PopID();
        return;
    }
    ImGui::PopID();
}

void EditorLayer::DrawLightInspector(Actor* actor)
{
    if (!actor) return;

    auto* light = actor->GetComponent<LightComponent>();
    if (!light) return;

    ImGui::Separator();
    ImGui::PushID("Light");
    ImGui::TextUnformatted("Light");
    bool changed = DrawComponentEnabled(*light);

    const char* lightTypes[] = { "Directional", "Point", "Spot" };
    int type = 0;
    if (light->GetLightType() == LightType::Point) {
        type = 1;
    } else if (light->GetLightType() == LightType::Spot) {
        type = 2;
    }
    if (ImGui::Combo("Type", &type, lightTypes, 3)) {
        light->SetLightType(type == 0 ? LightType::Directional :
            (type == 1 ? LightType::Point : LightType::Spot));
        changed = true;
    }

    Vec3 color = light->GetColor();
    float colorValues[3] = { color.x, color.y, color.z };
    if (ImGui::ColorEdit3("Color", colorValues)) {
        light->SetColor({ colorValues[0], colorValues[1], colorValues[2] });
        changed = true;
    }

    float intensity = light->GetIntensity();
    if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 1000.0f)) {
        light->SetIntensity(intensity);
        changed = true;
    }

    if (light->GetLightType() == LightType::Directional) {
        Vec3 direction = light->GetDirection();
        if (DrawVec3Editor("Direction", direction, 0.02f)) {
            light->SetDirection(direction);
            changed = true;
        }
        bool castShadows = light->CastsShadows();
        if (ImGui::Checkbox("Cast Shadows", &castShadows)) {
            light->SetCastShadows(castShadows);
            changed = true;
        }
    } else {
        float range = light->GetRange();
        if (ImGui::DragFloat("Range", &range, 0.05f, 0.01f, 10000.0f)) {
            light->SetRange(range);
            changed = true;
        }
        if (light->GetLightType() == LightType::Spot) {
            Vec3 direction = light->GetDirection();
            if (DrawVec3Editor("Direction", direction, 0.02f)) {
                light->SetDirection(direction);
                changed = true;
            }

            float innerCone = light->GetInnerConeAngle();
            if (ImGui::DragFloat("Inner Cone", &innerCone, 0.25f, 0.0f, 89.0f)) {
                light->SetInnerConeAngle(innerCone);
                changed = true;
            }

            float outerCone = light->GetOuterConeAngle();
            if (ImGui::DragFloat("Outer Cone", &outerCone, 0.25f, 0.0f, 89.0f)) {
                light->SetOuterConeAngle(outerCone);
                changed = true;
            }
        }
        bool castShadows = light->CastsShadows();
        if (ImGui::Checkbox("Cast Shadows", &castShadows)) {
            light->SetCastShadows(castShadows);
            changed = true;
        }
        ImGui::TextDisabled(light->GetLightType() == LightType::Point
            ? "Point light position uses the actor transform."
            : "Spot light position uses the actor transform.");
    }

    if (changed) m_SceneLayer->MarkDirty();
    if (ImGui::Button("Remove Light")) {
        actor->RemoveComponent<LightComponent>();
        m_SceneLayer->MarkDirty();
        ImGui::PopID();
        return;
    }
    ImGui::PopID();
}

void EditorLayer::DrawPostProcessInspector(Actor* actor)
{
    if (!actor) return;

    auto* post = actor->GetComponent<PostProcessComponent>();
    if (!post) return;

    ImGui::Separator();
    ImGui::PushID("PostProcess");
    ImGui::TextUnformatted("Post Process");
    bool changed = DrawComponentEnabled(*post);

    bool toneMapping = post->IsToneMappingEnabled();
    if (ImGui::Checkbox("Tone Mapping", &toneMapping)) {
        post->SetToneMappingEnabled(toneMapping);
        changed = true;
    }

    float exposure = post->GetExposure();
    if (ImGui::DragFloat("Exposure", &exposure, 0.02f, 0.0f, 16.0f)) {
        post->SetExposure(exposure);
        changed = true;
    }

    float gamma = post->GetGamma();
    if (ImGui::DragFloat("Gamma", &gamma, 0.01f, 0.1f, 8.0f)) {
        post->SetGamma(gamma);
        changed = true;
    }

    float vignette = post->GetVignette();
    if (ImGui::SliderFloat("Vignette", &vignette, 0.0f, 1.0f)) {
        post->SetVignette(vignette);
        changed = true;
    }

    float saturation = post->GetSaturation();
    if (ImGui::DragFloat("Saturation", &saturation, 0.02f, 0.0f, 4.0f)) {
        post->SetSaturation(saturation);
        changed = true;
    }

    float contrast = post->GetContrast();
    if (ImGui::DragFloat("Contrast", &contrast, 0.02f, 0.0f, 4.0f)) {
        post->SetContrast(contrast);
        changed = true;
    }

    float aa = post->GetAntiAliasingStrength();
    if (ImGui::SliderFloat("Shader AA Strength", &aa, 0.0f, 1.0f)) {
        post->SetAntiAliasingStrength(aa);
        changed = true;
    }
    ImGui::TextDisabled("FXAA applied in fullscreen post-process pass.");

    float ssaoRadius = post->GetSSAORadius();
    if (ImGui::DragFloat("SSAO Radius", &ssaoRadius, 0.02f, 0.01f, 10.0f)) {
        post->SetSSAORadius(ssaoRadius);
        changed = true;
    }

    float ssaoBias = post->GetSSAOBias();
    if (ImGui::DragFloat("SSAO Bias", &ssaoBias, 0.001f, 0.0f, 0.5f)) {
        post->SetSSAOBias(ssaoBias);
        changed = true;
    }

    float ssaoPower = post->GetSSAOPower();
    if (ImGui::DragFloat("SSAO Power", &ssaoPower, 0.02f, 0.1f, 8.0f)) {
        post->SetSSAOPower(ssaoPower);
        changed = true;
    }

    float ssaoIntensity = post->GetSSAOIntensity();
    if (ImGui::DragFloat("SSAO Intensity", &ssaoIntensity, 0.02f, 0.0f, 4.0f)) {
        post->SetSSAOIntensity(ssaoIntensity);
        changed = true;
    }

    if (changed) m_SceneLayer->MarkDirty();
    if (ImGui::Button("Remove PostProcess")) {
        actor->RemoveComponent<PostProcessComponent>();
        m_SceneLayer->MarkDirty();
        ImGui::PopID();
        return;
    }
    ImGui::PopID();
}

bool EditorLayer::DrawMaterialAssetInspector(MaterialHandle material)
{
    if (!material.IsValid()) {
        return false;
    }

    bool changed = false;
    MaterialAsset& mat = *material.Get();

    ImGui::Separator();
    ImGui::PushID(&mat);
    ImGui::TextUnformatted("Material Properties");
    ImGui::TextWrapped("%s", mat.GetPath().c_str());

    const char* blendModes[] = { "Opaque", "AlphaTest", "Transparent" };
    int blendMode = static_cast<int>(mat.GetBlendMode());
    if (ImGui::Combo("Blend Mode", &blendMode, blendModes, 3)) {
        mat.SetBlendMode(static_cast<BlendMode>(std::clamp(blendMode, 0, 2)));
        changed = true;
    }

    bool twoSided = mat.IsTwoSided();
    if (ImGui::Checkbox("Two Sided", &twoSided)) {
        mat.SetTwoSided(twoSided);
        changed = true;
    }

    bool wireframe = mat.IsWireframe();
    if (ImGui::Checkbox("Wireframe", &wireframe)) {
        mat.SetWireframe(wireframe);
        changed = true;
    }

    float alphaThreshold = mat.GetAlphaThreshold();
    if (ImGui::SliderFloat("Alpha Threshold", &alphaThreshold, 0.0f, 1.0f)) {
        mat.SetAlphaThreshold(alphaThreshold);
        changed = true;
    }

    MaterialParam baseColorParam = mat.GetParam(
        "BaseColor",
        MaterialParam::FromVec4(1.0f, 1.0f, 1.0f, 1.0f));
    float baseColor[4] = {
        baseColorParam.data[0],
        baseColorParam.data[1],
        baseColorParam.data[2],
        baseColorParam.data[3],
    };
    if (ImGui::ColorEdit4("Base Color", baseColor)) {
        mat.SetParam("BaseColor", MaterialParam::FromVec4(
            baseColor[0], baseColor[1], baseColor[2], baseColor[3]));
        changed = true;
    }

    float metallic = mat.GetFloat("Metallic", 0.0f);
    if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
        mat.SetParam("Metallic", MaterialParam::FromFloat(metallic));
        changed = true;
    }

    float roughness = mat.GetFloat("Roughness", 0.5f);
    if (ImGui::SliderFloat("Roughness", &roughness, 0.04f, 1.0f)) {
        mat.SetParam("Roughness", MaterialParam::FromFloat(roughness));
        changed = true;
    }

    float ao = mat.GetFloat("AmbientOcclusion", 1.0f);
    if (ImGui::SliderFloat("Ambient Occlusion", &ao, 0.0f, 1.0f)) {
        mat.SetParam("AmbientOcclusion", MaterialParam::FromFloat(ao));
        changed = true;
    }

    Vec3 emissiveColor = mat.GetColor("Emissive", Vec3::Zero());
    float emissiveArr[3] = { emissiveColor.x, emissiveColor.y, emissiveColor.z };
    if (ImGui::ColorEdit3("Emissive", emissiveArr)) {
        mat.SetParam("Emissive", MaterialParam::FromVec4(
            emissiveArr[0], emissiveArr[1], emissiveArr[2], 1.0f));
        changed = true;
    }

    auto drawTextureSlot = [&](const char* label, const char* slot) {
        TextureHandle texture = mat.GetTexture(slot);
        const std::string current = texture.IsValid() ? texture->GetPath() : std::string("(none)");
        ImGui::Text("%s: %s", label, current.c_str());
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kTextureDragPayloadType)) {
                if (payload->Data && payload->DataSize > 0) {
                    const char* path = static_cast<const char*>(payload->Data);
                    TextureHandle loaded = AssetManager::Get().GetByPath<TextureAsset>(path);
                    if (!loaded.IsValid()) {
                        loaded = AssetManager::Get().Load<TextureAsset>(path);
                    }
                    if (loaded.IsValid()) {
                        mat.SetTexture(slot, loaded);
                        changed = true;
                    } else {
                        Logger::Warn("[Editor] Failed to assign texture: ", path);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    };

    ImGui::TextDisabled("Drag textures from Asset Browser onto a slot.");
    drawTextureSlot("BaseColor Map", "BaseColorMap");
    drawTextureSlot("Normal Map", "NormalMap");
    drawTextureSlot("Metallic/Roughness Map", "MetallicRoughnessMap");
    drawTextureSlot("Occlusion Map", "OcclusionMap");
    drawTextureSlot("Emissive Map", "EmissiveMap");

    const bool isMatFile = IsMaterialAssetPath(mat.GetPath());
    ImGui::BeginDisabled(!isMatFile);
    if (ImGui::Button("Save Material")) {
        if (SaveMaterialAssetToFile(mat, mat.GetPath())) {
            Logger::Info("[Editor] Saved material: ", mat.GetPath());
        }
    }
    ImGui::EndDisabled();
    if (!isMatFile) {
        ImGui::TextDisabled("Create a .mat asset to persist material edits.");
    }

    ImGui::PopID();
    return changed;
}

void EditorLayer::DrawMeshMaterialInspector(Actor* actor)
{
    if (!actor) return;

    auto* mr = actor->GetComponent<MeshRendererComponent>();
    if (!mr) {
        if (ImGui::Button("Add Mesh Renderer")) {
            actor->AddComponent<MeshRendererComponent>();
            mr = actor->GetComponent<MeshRendererComponent>();
            if (mr) {
                mr->SetMesh(AssetManager::Get().GetCubeMesh());
                mr->SetMaterial(AssetManager::Get().GetDefaultMaterial());
                m_SceneLayer->MarkDirty();
            }
        }
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Mesh Renderer");

    std::vector<std::string> meshPaths = {
        "__builtin__/Triangle",
        "__builtin__/Quad",
        "__builtin__/Cube",
    };
    {
        auto extra = AssetManager::Get().GetCachedPathsByType(AssetType::Mesh);
        meshPaths.insert(meshPaths.end(), extra.begin(), extra.end());
        MergeUniqueSorted(meshPaths);
    }

    MeshHandle mesh = mr->GetMesh();
    const std::string meshCurrent = mesh.IsValid() ? mesh->GetPath() : "";

    const char* preview = meshCurrent.empty() ? "(none)" : meshCurrent.c_str();
    if (ImGui::BeginCombo("Mesh", preview)) {
        for (const std::string& p : meshPaths) {
            const bool selected = (p == meshCurrent);
            if (ImGui::Selectable(p.c_str(), selected)) {
                MeshHandle h = ResolveEditorMeshPath(p);
                if (h.IsValid()) {
                    mr->SetMesh(h);
                    m_SceneLayer->MarkDirty();
                } else {
                    Logger::Warn("[Editor] Failed to resolve mesh: ", p);
                }
            }
        }
        ImGui::EndCombo();
    }

    std::vector<std::string> matPaths = { "__builtin__/Default" };
    {
        auto extra = AssetManager::Get().GetCachedPathsByType(AssetType::Material);
        matPaths.insert(matPaths.end(), extra.begin(), extra.end());
        MergeUniqueSorted(matPaths);
    }

    MaterialHandle mat = mr->GetMaterial();
    const std::string matCurrent = mat.IsValid() ? mat->GetPath() : "";

    const char* matPreview = matCurrent.empty() ? "(none)" : matCurrent.c_str();
    if (ImGui::BeginCombo("Material", matPreview)) {
        for (const std::string& p : matPaths) {
            const bool selected = (p == matCurrent);
            if (ImGui::Selectable(p.c_str(), selected)) {
                MaterialHandle mh = ResolveEditorMaterialPath(p);
                if (mh.IsValid()) {
                    mr->SetMaterial(mh);
                    m_SceneLayer->MarkDirty();
                } else {
                    Logger::Warn("[Editor] Failed to resolve material: ", p);
                }
            }
        }
        ImGui::EndCombo();
    }

    if (DrawMaterialAssetInspector(mr->GetMaterial())) {
        m_SceneLayer->MarkDirty();
    }

    if (ImGui::Button("Remove Mesh Renderer")) {
        actor->RemoveComponent<MeshRendererComponent>();
        m_SceneLayer->MarkDirty();
    }
}
#endif

#if defined(MYENGINE_ENABLE_IMGUI)
void EditorLayer::DrawScriptInspector(Actor* actor)
{
    if (!actor) return;

    auto* script = actor->GetComponent<ScriptComponent>();
    ImGui::Separator();
    ImGui::TextUnformatted("Script");
    if (!script) {
        if (ImGui::Button("Add Script")) {
            actor->AddComponent<ScriptComponent>();
            m_SceneLayer->MarkDirty();
        }
        return;
    }

    ImGui::Text("Status: %s", script->IsCompiled() ? "Compiled" : "Error");
    const std::string& path = script->GetScriptPath();
    ImGui::TextWrapped("Path: %s", path.empty() ? "(inline)" : path.c_str());

    const std::string& error = script->GetLastError();
    if (!error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.42f, 0.38f, 1.0f));
        ImGui::TextWrapped("%s", error.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::TextUnformatted("Inspector Fields");
    nlohmann::json fields = script->GetInspectorFields();
    if (fields.empty()) {
        ImGui::TextDisabled("No fields. Define Inspector = { ... } in Lua.");
    } else if (DrawEditableInspectorJson(fields)) {
        script->SetInspectorFields(std::move(fields));
        m_SceneLayer->MarkDirty();
    }

    if (ImGui::Button("Remove Script")) {
        actor->RemoveComponent<ScriptComponent>();
        m_SceneLayer->MarkDirty();
    }
}
#endif

void EditorLayer::DrawInspector()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x - kInspectorWidth, vp->WorkPos.y + kToolbarHeight),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kInspectorWidth, vp->WorkSize.y - kToolbarHeight), ImGuiCond_Always);

    ImGui::Begin("Inspector");

    if (!m_Selected) {
        ImGui::TextDisabled("Select an actor to edit its transform.");
        ImGui::End();
        return;
    }

    const bool editing = m_SceneLayer->IsEditing();
    if (!editing) {
        ImGui::TextDisabled("Runtime scene is read-only here. Stop Play mode to edit.");
    }

    ImGui::Text("Actor: %s", m_Selected->GetName().c_str());
    ImGui::Text("ID: %llu", static_cast<unsigned long long>(m_Selected->GetID()));
    ImGui::Separator();
    ImGui::TextUnformatted("Transform");

    ImGui::BeginDisabled(!editing);
    Transform& transform = m_Selected->GetTransform();
    bool changed = false;
    changed |= DrawVec3Editor("Position", transform.position, 0.05f);
    changed |= DrawVec3Editor("Rotation", transform.rotation, 0.2f);
    changed |= DrawVec3Editor("Scale", transform.scale, 0.05f);

    if (changed) {
        m_SceneLayer->MarkDirty();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Gizmo");
    if (ImGui::RadioButton("Translate", m_GizmoOperation == ImGuizmo::TRANSLATE)) {
        m_GizmoOperation = ImGuizmo::TRANSLATE;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", m_GizmoOperation == ImGuizmo::ROTATE)) {
        m_GizmoOperation = ImGuizmo::ROTATE;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", m_GizmoOperation == ImGuizmo::SCALE)) {
        m_GizmoOperation = ImGuizmo::SCALE;
    }

    if (ImGui::RadioButton("Local", m_GizmoMode == ImGuizmo::LOCAL)) {
        m_GizmoMode = ImGuizmo::LOCAL;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("World", m_GizmoMode == ImGuizmo::WORLD)) {
        m_GizmoMode = ImGuizmo::WORLD;
    }

    DrawMeshMaterialInspector(m_Selected);
    DrawSkinnedMeshInspector(m_Selected);
    DrawPhysicsInspector(m_Selected);
    DrawLightInspector(m_Selected);
    DrawPostProcessInspector(m_Selected);
    DrawScriptInspector(m_Selected);
    DrawAddComponentInspector(m_Selected);
    ImGui::EndDisabled();

    ImGui::End();
#endif
}

void EditorLayer::DrawLogOutput()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float centerX = vp->WorkPos.x + kOutlinerPanelWidth;
    const float centerW = vp->WorkSize.x - kOutlinerPanelWidth - kInspectorWidth;
    const float logY = vp->WorkPos.y + vp->WorkSize.y - kLogPanelHeight;

    if (centerW <= 1.0f) return;

    ImGui::SetNextWindowPos(ImVec2(centerX, logY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(centerW, kLogPanelHeight), ImGuiCond_Always);
    ImGui::Begin("Log Output");

    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(m_LogMutex);
        m_LogLines.clear();
        m_LogScrollToBottom = false;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_LogAutoScroll);
    ImGui::Separator();

    std::vector<std::string> logs;
    bool scrollToBottom = false;
    {
        std::lock_guard<std::mutex> lock(m_LogMutex);
        logs.assign(m_LogLines.begin(), m_LogLines.end());
        scrollToBottom = m_LogScrollToBottom;
        m_LogScrollToBottom = false;
    }

    ImGui::BeginChild("##LogScrollRegion", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const std::string& line : logs) {
        const bool shaderErr = IsShaderCompileErrorLine(line);
        if (shaderErr) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.42f, 0.38f, 1.0f));
        }
        ImGui::TextUnformatted(line.c_str());
        if (shaderErr) {
            ImGui::PopStyleColor();
        }
    }
    if (m_LogAutoScroll && scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::End();
#endif
}

void EditorLayer::OnLogMessage(const std::string& line)
{
    std::lock_guard<std::mutex> lock(m_LogMutex);
    if (m_LogLines.size() >= kMaxLogLines) {
        m_LogLines.pop_front();
    }
    m_LogLines.push_back(line);
    m_LogScrollToBottom = true;
}

void EditorLayer::DrawAssetBrowser()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float logY = vp->WorkPos.y + vp->WorkSize.y - kLogPanelHeight;

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, logY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kOutlinerPanelWidth, kLogPanelHeight), ImGuiCond_Always);
    ImGui::Begin("Asset Browser");

    if (ImGui::Button("Refresh")) {
        RefreshAssetBrowserListing();
    }

    ImGui::SameLine();
    if (ImGui::Button("Import")) {
        RequestImportAssetDialog();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(m_AssetBrowserRootAbs.empty());
    if (ImGui::Button("Create Material")) {
        namespace fs = std::filesystem;
        const fs::path materialDir = fs::path(m_AssetBrowserRootAbs) / "Materials";
        const fs::path materialPath = MakeUniqueContentPath(materialDir, "NewMaterial", ".mat");
        auto material = MaterialAsset::CreateDefault(materialPath.stem().string());
        material->SetParam("AmbientOcclusion", MaterialParam::FromFloat(1.0f));
        if (SaveMaterialAssetToFile(*material, materialPath.string())) {
            AssetManager::Get().Load<MaterialAsset>(materialPath.string());
            RefreshAssetBrowserListing();
            Logger::Info("[Editor] Created material: ", materialPath.string());
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    static char s_Filter[128] = {};
    ImGui::InputTextWithHint("##AssetFilter", "Filter...", s_Filter, sizeof(s_Filter));

    ImGui::Separator();

    if (m_AssetBrowserRootAbs.empty()) {
        ImGui::TextDisabled("No Content folder found.");
        ImGui::TextDisabled("Expected ./Content or next to executable.");
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("%s", m_AssetBrowserRootAbs.c_str());
    if (!m_SelectedAssetAbsPath.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("Selected: %s", m_SelectedAssetRelPath.c_str());
        if (IsTextureAssetPath(m_SelectedAssetAbsPath)) {
            TextureHandle texture = AssetManager::Get().GetByPath<TextureAsset>(m_SelectedAssetAbsPath);
            if (!texture.IsValid()) {
                texture = AssetManager::Get().Load<TextureAsset>(m_SelectedAssetAbsPath);
            }
            if (texture.IsValid()) {
                ImGui::Text("Texture: %dx%d mips=%d",
                    texture->GetWidth(), texture->GetHeight(), texture->GetMipLevels());
            }
        } else if (IsModelAssetPath(m_SelectedAssetAbsPath)) {
            ModelHandle model = AssetManager::Get().GetByPath<ModelAsset>(m_SelectedAssetAbsPath);
            if (!model.IsValid()) {
                model = AssetManager::Get().Load<ModelAsset>(m_SelectedAssetAbsPath);
            }
            if (model.IsValid() && model->GetMesh()) {
                ImGui::Text("Model: vertices=%zu indices=%zu materials=%d",
                    model->GetMesh()->VertexCount(),
                    model->GetMesh()->IndexCount(),
                    model->MaterialCount());
            }
        } else if (IsMaterialAssetPath(m_SelectedAssetAbsPath)) {
            MaterialHandle material = AssetManager::Get().GetByPath<MaterialAsset>(m_SelectedAssetAbsPath);
            if (!material.IsValid()) {
                material = AssetManager::Get().Load<MaterialAsset>(m_SelectedAssetAbsPath);
            }
            if (material.IsValid()) {
                ImGui::Text("Material: params=%zu textures=%zu",
                    material->GetParams().size(),
                    material->GetTextures().size());
            }
        }
        ImGui::Separator();
    }
    const char* filter = s_Filter;
    const bool useFilter = (filter[0] != '\0');

    ImGui::BeginChild("##AssetBrowserScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const std::string& rel : m_AssetBrowserRelPaths) {
        if (useFilter) {
            if (rel.find(filter) == std::string::npos) {
                continue;
            }
        }
        const bool isModel = IsModelAssetPath(rel);
        const bool isTexture = IsTextureAssetPath(rel);
        const bool isMaterial = IsMaterialAssetPath(rel);
        if (ImGui::Selectable(rel.c_str())) {
            const std::string full =
                (std::filesystem::path(m_AssetBrowserRootAbs) / rel).lexically_normal().string();
            m_SelectedAssetRelPath = rel;
            m_SelectedAssetAbsPath = full;
            Logger::Info("[Editor] Asset path: ", full);
            if (isMaterial) {
                AssetManager::Get().Load<MaterialAsset>(full);
            } else if (isTexture) {
                AssetManager::Get().Load<TextureAsset>(full);
            } else if (isModel) {
                AssetManager::Get().Load<ModelAsset>(full);
            }
        }
        if (isModel && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const std::string full =
                (std::filesystem::path(m_AssetBrowserRootAbs) / rel).lexically_normal().string();
            ImGui::SetDragDropPayload(kModelDragPayloadType, full.c_str(), full.size() + 1);
            ImGui::TextUnformatted("OBJ 閳?Scene");
            ImGui::EndDragDropSource();
        }
        if (isTexture && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const std::string full =
                (std::filesystem::path(m_AssetBrowserRootAbs) / rel).lexically_normal().string();
            ImGui::SetDragDropPayload(kTextureDragPayloadType, full.c_str(), full.size() + 1);
            ImGui::Text("Texture -> Material: %s", rel.c_str());
            ImGui::EndDragDropSource();
        }
    }
    ImGui::EndChild();

    ImGui::End();
#endif
}
