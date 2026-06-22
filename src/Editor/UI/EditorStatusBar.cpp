#include "Editor/UI/EditorStatusBar.h"

#include "Core/Engine.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorProject.h"
#include "Editor/UI/EditorIcons.h"
#include "Editor/UI/EditorTheme.h"
#include "Editor/UI/EditorWidgets.h"
#include "Renderer/IRenderContext.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <filesystem>

namespace Editor::UI {

float EditorStatusBar::Draw(EditorContext& context,
                            const EditorProject* project,
                            IRenderContext* renderContext,
                            Engine* engine,
                            float effectiveScale)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = ScaleToken(EditorStyleTokens{}.statusBarHeight, effectiveScale);
    ImGui::SetNextWindowPos({viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - height});
    ImGui::SetNextWindowSize({viewport->WorkSize.x, height});
    ImGui::SetNextWindowViewport(viewport->ID);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        {ScaleToken(8.0f, effectiveScale), ScaleToken(3.0f, effectiveScale)});
    if (ImGui::Begin("Editor Status Bar", nullptr, flags)) {
        const FrameStats emptyStats{};
        const FrameStats& stats = engine ? engine->GetFrameStats() : emptyStats;
        if (EditorWidgets::SvgIcon(context, EditorIcons::EngineEditor,
                                   ScaleToken(14.0f, effectiveScale))) {
            ImGui::SameLine();
        }
        ImGui::Text("Ready | Selected: %s | %s | %.1f FPS / %.2f ms | Project: %s",
                    FormatSelectedText(context).c_str(),
                    FormatBackendText(renderContext).c_str(),
                    stats.fps,
                    stats.smoothedFrameMs,
                    FormatProjectText(project).c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    return height;
#else
    (void)context;
    (void)project;
    (void)renderContext;
    (void)engine;
    (void)effectiveScale;
    return 0.0f;
#endif
}

std::string EditorStatusBar::FormatSelectedText(const EditorContext& context)
{
    const auto& selection = context.GetSelection();
    if (selection.HasActor()) {
        if (Scene* scene = context.GetInspectorScene()) {
            if (Actor* actor = selection.ResolveActor(*scene)) return actor->GetName();
        }
        return "Actor";
    }
    if (selection.HasAsset()) {
        const std::filesystem::path path(selection.GetAssetPath());
        return path.filename().string();
    }
    return "None";
}

std::string EditorStatusBar::FormatBackendText(IRenderContext* renderContext)
{
    if (!renderContext) return "Unknown";
    return FormatBackendText(renderContext->GetBackend());
}

std::string EditorStatusBar::FormatBackendText(RHIBackend backend)
{
    switch (backend) {
        case RHIBackend::D3D11: return "D3D11";
        case RHIBackend::D3D12: return "D3D12";
        case RHIBackend::Metal: return "Metal";
        default: return "Unknown";
    }
}

std::string EditorStatusBar::FormatProjectText(const EditorProject* project)
{
    if (!project) return "None";
    const std::string& name = project->GetConfig().GetName();
    return name.empty() ? "Unnamed" : name;
}

} // namespace Editor::UI
