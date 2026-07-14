#include "Editor/EditorProjectSettingsController.h"

#include "Core/Logger.h"
#include "Editor/EditorLayer.h"
#include "Editor/UI/EditorNotifications.h"
#include "Editor/UI/EditorViewportPolicy.h"
#include "Editor/UI/EditorWidgets.h"
#include "Game/SceneRenderLayer.h"
#include "Input/Input.h"
#include "Project/PublishTargets.h"
#include "Renderer/RenderBackendRegistry.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <cstring>
#include <vector>

namespace EditorWidgets = Editor::UI::EditorWidgets;

namespace {
RenderPath RenderPathFromProjectValue(const std::string& value) {
    return value == "deferred" ? RenderPath::Deferred : RenderPath::Forward;
}

const char* ProjectValueFromRenderPathIndex(int index) {
    return index == 1 ? "deferred" : "forward";
}

std::vector<RenderBackend> AvailableEditorBackends() {
    std::vector<RenderBackend> backends;
    for (RenderBackend backend :
         {RenderBackend::D3D11, RenderBackend::D3D12, RenderBackend::Metal, RenderBackend::Vulkan}) {
        if (IsBackendCompiled(backend))
            backends.push_back(backend);
    }
    return backends;
}

int BackendIndexFromProjectValue(const std::string& value) {
    const auto requested = ParseRenderBackend(value);
    const auto backends = AvailableEditorBackends();
    for (size_t i = 0; i < backends.size(); ++i) {
        if (requested && backends[i] == *requested)
            return static_cast<int>(i);
    }
    for (size_t i = 0; i < backends.size(); ++i) {
        if (backends[i] == kDefaultRenderBackend)
            return static_cast<int>(i);
    }
    return 0;
}
} // namespace

void EditorProjectSettingsController::DrawProjectSettingsTab(EditorLayer& layer) {
#if defined(MYENGINE_ENABLE_IMGUI)
    const auto& config = layer.m_Project.GetConfig();
    ImGui::InputText("Project name", layer.m_ProjectName.data(), layer.m_ProjectName.size());
    ImGui::InputText("Output directory", layer.m_PublishOutput.data(), layer.m_PublishOutput.size());
    ImGui::LabelText("Target", "%s", PublishTargets::kDefaultTargetLabel);
    ImGui::LabelText("Startup scene", "%s",
                     config.GetStartupScene().empty() ? "<not set>" : config.GetStartupScene().c_str());
    ImGui::TextDisabled("Use Set Startup to assign the currently saved scene.");
    if (ImGui::Button("Save")) {
        auto& editable = layer.m_Project.GetConfig();
        const ProjectConfig previous = editable;
        editable.SetName(layer.m_ProjectName.data());
        editable.GetPublishSettings().outputDirectory = layer.m_PublishOutput.data();
        editable.GetPublishSettings().target = PublishTargets::kDefaultTargetId;
        std::string error;
        if (!editable.SetInputConfigPath(layer.m_InputConfigPath.data(), &error)) {
            editable = previous;
            layer.ShowProjectResult("Invalid input config path: " + error, true);
            ImGui::EndPopup();
            return;
        }
        if (editable.Save(&error)) {
            Logger::Info("[Editor] Project settings saved");
            layer.LoadProjectInputConfig();
            layer.ShowProjectResult("Project settings saved.", false);
        } else {
            editable = previous;
            layer.ShowProjectResult("Failed to save project settings: " + error, true);
        }
    }
#else
    (void)layer;
#endif
}

void EditorProjectSettingsController::DrawGraphicsSettingsTab(EditorLayer& layer) {
#if defined(MYENGINE_ENABLE_IMGUI)
    const auto backends = AvailableEditorBackends();
    if (layer.m_GraphicsBackendIndex < 0 || layer.m_GraphicsBackendIndex >= static_cast<int>(backends.size())) {
        layer.m_GraphicsBackendIndex = 0;
    }
    const RenderBackend selectedBackend =
        backends.empty() ? kDefaultRenderBackend : backends[static_cast<size_t>(layer.m_GraphicsBackendIndex)];
    if (ImGui::BeginCombo("Backend", RenderBackendToLabel(selectedBackend))) {
        for (size_t i = 0; i < backends.size(); ++i) {
            const bool selected = layer.m_GraphicsBackendIndex == static_cast<int>(i);
            if (ImGui::Selectable(RenderBackendToLabel(backends[i]), selected)) {
                layer.m_GraphicsBackendIndex = static_cast<int>(i);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    static constexpr const char* kRenderPaths[] = {"Forward", "Deferred"};
    static constexpr int kRenderPathCount = 2;
    if (layer.m_RenderPathIndex >= kRenderPathCount)
        layer.m_RenderPathIndex = 0;
    ImGui::Combo("Render Path", &layer.m_RenderPathIndex, kRenderPaths, kRenderPathCount);
    const RHIBackend active = layer.m_RenderContext ? layer.m_RenderContext->GetBackend() : RHIBackend::Unknown;
    const RenderBackend activeBackend = active == RHIBackend::Vulkan  ? RenderBackend::Vulkan
                                        : active == RHIBackend::D3D12 ? RenderBackend::D3D12
                                        : active == RHIBackend::Metal ? RenderBackend::Metal
                                                                      : RenderBackend::D3D11;
    ImGui::LabelText("Active backend", "%s",
                     active == RHIBackend::Unknown ? "Unknown" : RenderBackendToLabel(activeBackend));
    const RenderPath activeRenderPath = layer.m_SceneLayer ? layer.m_SceneLayer->GetRenderPath() : RenderPath::Forward;
    ImGui::LabelText("Active render path", "%s", activeRenderPath == RenderPath::Deferred ? "Deferred" : "Forward");
    ImGui::LabelText("Apply", "%s", "backend next launch, render path immediate");
    if (ImGui::Button("Save Graphics")) {
        auto& editable = layer.m_Project.GetConfig();
        const ProjectConfig previous = editable;
        editable.GetGraphicsSettings().backend = RenderBackendToProjectValue(selectedBackend);
        editable.GetGraphicsSettings().renderPath = ProjectValueFromRenderPathIndex(layer.m_RenderPathIndex);
        std::string error;
        if (editable.Save(&error)) {
            if (layer.m_SceneLayer) {
                layer.m_SceneLayer->SetRenderPath(
                    RenderPathFromProjectValue(editable.GetGraphicsSettings().renderPath));
            }
            Logger::Info("[Editor] Graphics settings saved");
            layer.ShowProjectResult("Graphics settings saved.", false);
        } else {
            editable = previous;
            layer.ShowProjectResult("Failed to save graphics settings: " + error, true);
        }
    }
#else
    (void)layer;
#endif
}

void EditorProjectSettingsController::DrawGameplayInputSettingsTab(EditorLayer& layer) {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::InputText("Input config", layer.m_InputConfigPath.data(), layer.m_InputConfigPath.size());
    if (ImGui::Button("Create Default Input Config"))
        layer.CreateDefaultInputConfig();
    ImGui::SameLine();
    if (ImGui::Button("Reload Input Config")) {
        auto& editable = layer.m_Project.GetConfig();
        const ProjectConfig previous = editable;
        std::string error;
        if (!editable.SetInputConfigPath(layer.m_InputConfigPath.data(), &error)) {
            editable = previous;
            layer.ShowProjectResult("Invalid input config path: " + error, true);
        } else if (!editable.Save(&error)) {
            editable = previous;
            layer.ShowProjectResult("Failed to save project settings: " + error, true);
        } else {
            layer.LoadProjectInputConfig();
            layer.ShowProjectResult("Input config reloaded.", false);
        }
    }
#else
    (void)layer;
#endif
}

void EditorProjectSettingsController::DrawLayoutSettingsTab(EditorLayer& layer) {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::LabelText("Default config", "%s", layer.m_LayoutManager.GetConfigPath().string().c_str());
    if (!layer.m_LayoutManager.GetLastWarning().empty()) {
        EditorWidgets::InlineMessage(EditorWidgets::MessageType::Warning,
                                     layer.m_LayoutManager.GetLastWarning().c_str());
    }
    if (ImGui::Button("Save Current"))
        layer.SaveEditorLayout();
    ImGui::SameLine();
    if (ImGui::Button("Reset To Default"))
        layer.ResetEditorLayoutToDefault();
    ImGui::SameLine();
    if (ImGui::Button("Reveal Config Path"))
        layer.RevealEditorLayoutConfig();
    ImGui::Separator();
    ImGui::TextDisabled("Layout is stored in editor state and does not dirty the scene.");
#else
    (void)layer;
#endif
}

void EditorProjectSettingsController::DrawAppearanceSettingsTab(EditorLayer& layer) {
#if defined(MYENGINE_ENABLE_IMGUI)
    float userScale = layer.m_Workspace.GetUserUiScale();
    ImGui::Text("Platform DPI scale: %.2f", layer.m_UIScaleManager.GetPlatformScale());
    ImGui::Text("Effective UI scale: %.2f", layer.m_UIScaleManager.GetEffectiveScale());
    ImGui::LabelText("Font root", "%s", layer.m_UIScaleManager.GetFontManager().GetFontRoot().string().c_str());
    if (!layer.m_UIScaleManager.GetFontManager().GetLastWarning().empty()) {
        EditorWidgets::InlineMessage(Editor::UI::EditorNotificationType::Warning,
                                     layer.m_UIScaleManager.GetFontManager().GetLastWarning().c_str());
    }
    if (ImGui::SliderFloat("UI Scale", &userScale, Editor::UI::EditorUIScaleSettings::kMinUserScale,
                           Editor::UI::EditorUIScaleSettings::kMaxUserScale, "%.2f")) {
        layer.m_Workspace.SetUserUiScale(userScale);
        layer.m_UIScaleManager.SetUserScale(userScale);
        layer.m_UIScaleManager.MarkFontAtlasDirty();
        layer.m_ThemeManager.Apply(layer.m_UIScaleManager.GetEffectiveScale());
        std::string error;
        if (!layer.m_Workspace.Save(&error))
            Logger::Warn("[Editor] ", error);
    }

    static constexpr const char* kThemes[] = {"Dark"};
    int themeIndex = 0;
    if (ImGui::Combo("Theme", &themeIndex, kThemes, 1)) {
        layer.m_Workspace.SetEditorThemeId("dark");
        layer.m_ThemeManager.SetThemeID(layer.m_Workspace.GetEditorThemeId());
        layer.m_ThemeManager.Apply(layer.m_UIScaleManager.GetEffectiveScale());
        std::string error;
        if (!layer.m_Workspace.Save(&error))
            Logger::Warn("[Editor] ", error);
    }
    if (ImGui::Button("Reset Appearance")) {
        layer.m_Workspace.SetUserUiScale(1.0f);
        layer.m_Workspace.SetEditorThemeId("dark");
        layer.m_UIScaleManager.SetUserScale(layer.m_Workspace.GetUserUiScale());
        layer.m_UIScaleManager.MarkFontAtlasDirty();
        layer.m_ThemeManager.SetThemeID(layer.m_Workspace.GetEditorThemeId());
        layer.m_ThemeManager.Apply(layer.m_UIScaleManager.GetEffectiveScale());
        std::string error;
        if (layer.m_Workspace.Save(&error))
            layer.ShowProjectResult("Appearance reset.", false);
        else
            layer.ShowProjectResult("Failed to save appearance: " + error, true);
    }
    ImGui::Separator();
    ImGui::TextDisabled("Appearance is stored in workspace preferences and does not dirty the scene.");
#else
    (void)layer;
#endif
}

void EditorProjectSettingsController::Open(EditorLayer& layer) {
    if (!layer.m_ProjectOpen)
        return;
    const auto& config = layer.m_Project.GetConfig();
    std::strncpy(layer.m_ProjectName.data(), config.GetName().c_str(), layer.m_ProjectName.size() - 1);
    layer.m_ProjectName.back() = '\0';
    std::strncpy(layer.m_PublishOutput.data(), config.GetPublishSettings().outputDirectory.c_str(),
                 layer.m_PublishOutput.size() - 1);
    layer.m_PublishOutput.back() = '\0';
    std::strncpy(layer.m_InputConfigPath.data(), config.GetInputSettings().config.c_str(),
                 layer.m_InputConfigPath.size() - 1);
    layer.m_InputConfigPath.back() = '\0';
    layer.m_GraphicsBackendIndex = BackendIndexFromProjectValue(config.GetGraphicsSettings().backend);
    layer.m_RenderPathIndex = config.GetGraphicsSettings().renderPath == "deferred" ? 1 : 0;
    layer.m_ProjectSettingsRequested = true;
}

void EditorProjectSettingsController::Draw(EditorLayer& layer) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (layer.m_ProjectSettingsRequested) {
        ImGui::OpenPopup("Settings");
        layer.m_ProjectSettingsRequested = false;
    }
    ImGui::SetNextWindowSize({760.0f, 520.0f}, ImGuiCond_Appearing);
    Editor::UI::EditorViewportPolicy::BindNextModalToMainViewport();
    if (!ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_NoCollapse))
        return;

    if (ImGui::BeginTabBar("SettingsTabs")) {
        if (ImGui::BeginTabItem("Project")) {
            DrawProjectSettingsTab(layer);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Graphics")) {
            DrawGraphicsSettingsTab(layer);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gameplay Input")) {
            DrawGameplayInputSettingsTab(layer);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shortcuts")) {
            layer.DrawShortcutSettingsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Appearance")) {
            DrawAppearanceSettingsTab(layer);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Layout")) {
            DrawLayoutSettingsTab(layer);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
#else
    (void)layer;
#endif
}
