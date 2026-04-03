#include "Editor/EditorLayer.h"

#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/ModelAsset.h"
#include "Camera/Camera.h"
#include "Core/EngineMath.h"
#include "Core/Logger.h"
#include "Core/Platform.h"
#include "Math/Mat4Inverse.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
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

    // ImGuizmo::matrix_t is a float[4][4] union layout — same row-major packing as our Mat4.
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

    // Transform::GetLocalMatrix builds upper 3x3 as (Ry*Rx*Rz) * diag(sx,sy,sz) with
    // M_ij = R_ij * s_j. ImGuizmo OrthoNormalize() normalizes each ROW of the model matrix,
    // which breaks R when scales differ — rotation rings track the view and feel "camera
    // locked". Split out orthonormal R and per-column scale for rotate gizmo input.
    void ExtractOrthonormalRAndColumnScale(const Mat4& w, float colScale[3], Mat4& outR)
    {
        for (int j = 0; j < 3; ++j) {
            const float x = w.m[0][j];
            const float y = w.m[1][j];
            const float z = w.m[2][j];
            colScale[j]   = std::sqrt(x * x + y * y + z * z);
            if (colScale[j] < 1e-8f) {
                colScale[j] = 1e-8f;
            }
        }
        outR             = Mat4::Identity();
        for (int j = 0; j < 3; ++j) {
            for (int i = 0; i < 3; ++i) {
                outR.m[i][j] = w.m[i][j] / colScale[j];
            }
        }
        outR.m[3][3] = 1.0f;
    }

    // Inverse of Transform::GetLocalMatrix(): upper 3x3 is (Ry*Rx*Rz)*diag(scale),
    // with rotation.x/y/z = pitch/yaw/roll in degrees. ImGuizmo's
    // DecomposeMatrixToComponents uses a different Euler order — using it caused rotate
    // drift and apparent coupling to the camera.
    void DecomposeLocalMatrixToTransform(const Mat4& m, Vec3& outPos, Vec3& outRotDeg, Vec3& outScale)
    {
        outPos = Vec3{ m.m[3][0], m.m[3][1], m.m[3][2] };

        auto colLen = [&](int c) {
            const float x = m.m[0][c];
            const float y = m.m[1][c];
            const float z = m.m[2][c];
            return std::sqrt(x * x + y * y + z * z);
        };
        outScale = Vec3{ colLen(0), colLen(1), colLen(2) };

        const float s0 = outScale.x > 1e-8f ? outScale.x : 1e-8f;
        const float s1 = outScale.y > 1e-8f ? outScale.y : 1e-8f;
        const float s2 = outScale.z > 1e-8f ? outScale.z : 1e-8f;

        const float r02 = m.m[0][2] / s2;
        const float r12 = m.m[1][2] / s2;
        const float r22 = m.m[2][2] / s2;
        const float r10 = m.m[1][0] / s0;
        const float r11 = m.m[1][1] / s1;
        const float r00 = m.m[0][0] / s0;
        const float r01 = m.m[0][1] / s1;

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

    static const SDL_DialogFileFilter kSceneJsonFilters[] = {
        { "Scene JSON", "json" },
    };

    constexpr const char kObjDragPayloadType[] = "MYENGINE_OBJ_PATH";

    bool PathEndsWithObj(const std::string& path)
    {
        if (path.size() < 4) {
            return false;
        }
        const size_t i = path.size() - 4;
        const char a = path[i];
        const char b = path[i + 1];
        const char c = path[i + 2];
        const char d = path[i + 3];
        return a == '.' &&
               (b == 'o' || b == 'O') &&
               (c == 'b' || c == 'B') &&
               (d == 'j' || d == 'J');
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
#endif
}

void EditorLayer::OnRender()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_SceneLayer || !m_ImGuiReady || !m_RenderContext) return;

    m_RenderContext->BeginImGuiFrame();

    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

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
                m_Selected = nullptr;
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

    m_Selected = best;
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

void EditorLayer::TryCreateMeshActorFromDroppedObj(const std::string& absObjPath,
                                                   float                 screenX,
                                                   float                 screenY)
{
    if (!m_SceneLayer || absObjPath.empty() || !PathEndsWithObj(absObjPath)) {
        return;
    }

    ModelHandle model = AssetManager::Get().Load<ModelAsset>(absObjPath);
    if (!model || !model->GetMesh()) {
        Logger::Warn("[Editor] Failed to load model: ", absObjPath);
        return;
    }

    MeshHandle     mesh = model->GetMesh();
    MaterialHandle mat  = model->GetMaterial(0);
    if (!mat) {
        mat = AssetManager::Get().GetDefaultMaterial();
    }

    std::string stem = std::filesystem::path(absObjPath).stem().string();
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
    m_Selected = actor;
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

    if (ImGui::Button("New Scene")) {
        m_SceneLayer->NewScene("Untitled");
        m_Selected = nullptr;
    }
    ImGui::SameLine();

    if (ImGui::Button("Open Scene")) {
        RequestOpenSceneDialog();
    }
    ImGui::SameLine();

    if (ImGui::Button("Save Scene")) {
        if (m_SceneLayer->HasFilePath()) {
            if (!m_SceneLayer->SaveScene()) {
                Logger::Error("[Editor] SaveScene failed");
            }
        } else {
            RequestSaveSceneDialog();
        }
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
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float outlinerH = vp->WorkSize.y - kToolbarHeight - kLogPanelHeight;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kOutlinerPanelWidth, outlinerH), ImGuiCond_Always);

    ImGui::Begin("Scene Outliner");

    if (ImGui::Button("Create Actor")) {
        m_SceneLayer->GetScene().CreateActor("Actor");
        m_SceneLayer->MarkDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Selected")) {
        if (m_Selected) {
            Actor* victim = m_Selected;
            m_Selected    = nullptr;
            m_SceneLayer->GetScene().DestroyActor(victim);
            m_SceneLayer->MarkDirty();
        }
    }

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
                             ImGuiWindowFlags_NoScrollWithMouse;

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
    ImGui::Dummy(ImVec2(viewportW, viewportH));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjDragPayloadType)) {
            if (payload->Data && payload->DataSize > 0) {
                const char* path = static_cast<const char*>(payload->Data);
                const ImVec2 mousePos = ImGui::GetMousePos();
                TryCreateMeshActorFromDroppedObj(path, mousePos.x, mousePos.y);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Manipulate() must run before mouse picking so IsOver()/IsUsing() match this frame's gizmo.
    // Gizmo is placed at mesh AABB center in world space (falls back to actor pivot when no mesh).
    if (m_Selected && viewportW > 1.0f && viewportH > 1.0f) {
        Camera& cam = m_SceneLayer->GetCamera();
        ImGuizmo::SetRect(viewportX, viewportY, viewportW, viewportH);
        ImGuizmo::SetOrthographic(cam.GetProjectionMode() == ProjectionMode::Orthographic);

        float viewM[16];
        float projM[16];
        // Must use the same View and Proj as MainPass (world * view * proj). A separate
        // "GL style" projection tweak makes ImGuizmo's MVP disagree with the scene and the
        // gizmo slides on screen when the camera moves.
        Mat4CopyToFloat16(cam.GetView(), viewM);
        Mat4CopyToFloat16(cam.GetProj(), projM);

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

        float        rotateColScale[3] = { 1.0f, 1.0f, 1.0f };
        Mat4         gizmoLinear       = world;
        const bool   rotateGizmo       = (m_GizmoOperation == ImGuizmo::ROTATE);
        if (rotateGizmo) {
            Mat4 R{};
            ExtractOrthonormalRAndColumnScale(world, rotateColScale, R);
            gizmoLinear = R;
        }

        Mat4 gizmoMat = gizmoLinear;
        gizmoMat.m[3][0] = pivotWorld.x;
        gizmoMat.m[3][1] = pivotWorld.y;
        gizmoMat.m[3][2] = pivotWorld.z;

        float matrix[16];
        Mat4CopyToFloat16(gizmoMat, matrix);

        ImGuizmo::Manipulate(
            viewM,
            projM,
            m_GizmoOperation,
            m_GizmoMode,
            matrix,
            nullptr,
            nullptr);

        if (ImGuizmo::IsUsing()) {
            Mat4 gizmoNew{};
            Mat4CopyFromFloat16(matrix, gizmoNew);

            Mat4 oriented = gizmoNew;
            if (rotateGizmo) {
                for (int j = 0; j < 3; ++j) {
                    for (int i = 0; i < 3; ++i) {
                        oriented.m[i][j] = gizmoNew.m[i][j] * rotateColScale[j];
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
                    localNew = invParent * worldNew;
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
                MeshHandle h = AssetManager::Get().GetByPath<MeshAsset>(p);
                if (!h.IsValid()) {
                    h = AssetManager::Get().Load<MeshAsset>(p);
                }
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
                MaterialHandle mh = AssetManager::Get().GetByPath<MaterialAsset>(p);
                if (!mh.IsValid()) {
                    mh = AssetManager::Get().Load<MaterialAsset>(p);
                }
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

    if (ImGui::Button("Remove Mesh Renderer")) {
        actor->RemoveComponent<MeshRendererComponent>();
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

    ImGui::Text("Actor: %s", m_Selected->GetName().c_str());
    ImGui::Text("ID: %llu", static_cast<unsigned long long>(m_Selected->GetID()));
    ImGui::Separator();
    ImGui::TextUnformatted("Transform");

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
        ImGui::TextUnformatted(line.c_str());
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
    const char* filter = s_Filter;
    const bool useFilter = (filter[0] != '\0');

    ImGui::BeginChild("##AssetBrowserScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const std::string& rel : m_AssetBrowserRelPaths) {
        if (useFilter) {
            if (rel.find(filter) == std::string::npos) {
                continue;
            }
        }
        const bool isObj = PathEndsWithObj(rel);
        if (ImGui::Selectable(rel.c_str())) {
            const std::string full =
                (std::filesystem::path(m_AssetBrowserRootAbs) / rel).lexically_normal().string();
            Logger::Info("[Editor] Asset path: ", full);
        }
        if (isObj && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const std::string full =
                (std::filesystem::path(m_AssetBrowserRootAbs) / rel).lexically_normal().string();
            ImGui::SetDragDropPayload(kObjDragPayloadType, full.c_str(), full.size() + 1);
            ImGui::TextUnformatted("OBJ → Scene");
            ImGui::EndDragDropSource();
        }
    }
    ImGui::EndChild();

    ImGui::End();
#endif
}
