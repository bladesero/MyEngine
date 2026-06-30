#include "Editor/EditorPanels.h"

#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"
#include "Assets/ModelAsset.h"
#include "Assets/TextureAsset.h"
#include "Core/Logger.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorContextMenu.h"
#include "Editor/EditorDialogService.h"
#include "Editor/EditorDragDrop.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorOperators.h"
#include "Editor/EditorPanelHelpers.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorUI/EditorAngelScriptDomain.h"
#include "Editor/UI/EditorIcons.h"
#include "Editor/UI/EditorWidgets.h"
#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

namespace {
constexpr const char kModelPayload[]="MYENGINE_MODEL_PATH";
constexpr const char kTexturePayload[]="MYENGINE_TEXTURE_PATH";
constexpr const char kPrefabPayload[]="MYENGINE_PREFAB_PATH";
namespace EditorIcons = Editor::UI::EditorIcons;
namespace EditorWidgets = Editor::UI::EditorWidgets;

std::string ReadFileContent(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

const char* IconForAssetType(EditorAssetType type)
{
    switch (type) {
        case EditorAssetType::Model: return EditorIcons::Mesh;
        case EditorAssetType::Texture: return EditorIcons::Texture;
        case EditorAssetType::Material: return EditorIcons::Material;
        case EditorAssetType::Scene: return EditorIcons::Scene;
        case EditorAssetType::Prefab: return EditorIcons::Prefab;
        case EditorAssetType::Script: return EditorIcons::Script;
        case EditorAssetType::Shader: return EditorIcons::Shader;
        case EditorAssetType::Audio: return EditorIcons::Audio;
        case EditorAssetType::UI: return EditorIcons::Input;
        default: return EditorIcons::Asset;
    }
}

const char* ImportStateText(AssetImportState state)
{
    switch (state) {
        case AssetImportState::Importing: return "importing";
        case AssetImportState::Failed: return "failed";
        case AssetImportState::Stale: return "stale";
        case AssetImportState::MissingSource: return "missing source";
        default: return "ready";
    }
}

const char* AssetTypeName(EditorAssetType type)
{
    switch (type) {
        case EditorAssetType::Model: return "Model";
        case EditorAssetType::Texture: return "Texture";
        case EditorAssetType::Material: return "Material";
        case EditorAssetType::Scene: return "Scene";
        case EditorAssetType::Prefab: return "Prefab";
        case EditorAssetType::Script: return "Script";
        case EditorAssetType::Shader: return "Shader";
        case EditorAssetType::Audio: return "Audio";
        case EditorAssetType::UI: return "UI";
        default: return "Unknown";
    }
}

void DrawScriptAssetContextMenu(EditorContext& context, EditorAssetType type)
{
    EditorAngelScriptDomain* domain = context.GetEditorScriptDomain();
    if (!domain || !domain->IsLoaded() ||
        !domain->GetConfig().enableContextMenuExtensions) {
        return;
    }
    const std::string typeName = AssetTypeName(type);
    for (const auto& extension : domain->GetRegistry().GetAssetContextMenus()) {
        if (extension.targetType != "*" && extension.targetType != "Any" &&
            extension.targetType != typeName) {
            continue;
        }
        std::string error;
        const std::string stateKey = "assetContext:" + typeName;
        if (!domain->ExecuteExtension(extension.callback, stateKey, context, &error) &&
            !error.empty()) {
            Logger::Warn("[EditorScript] Asset context menu failed for ",
                         typeName, ": ", error);
        }
    }
}

bool StartsWithPath(const std::string& value, const std::string& prefix)
{
    return value == prefix ||
        (value.size() > prefix.size() && value.compare(0, prefix.size(), prefix) == 0 &&
         value[prefix.size()] == '/');
}

bool IsDirectChildFolder(const std::string& parent, const std::string& child)
{
    if (!StartsWithPath(child, parent) || child == parent) return false;
    const std::string suffix = child.substr(parent.size() + 1);
    return suffix.find('/') == std::string::npos;
}
}

AssetBrowserPanel::AssetBrowserPanel():EditorPanel("assetBrowser","Asset Browser"){}
void AssetBrowserPanel::OnAttach(EditorContext& context){EditorPanel::OnAttach(context);if(context.GetAssetRegistry())context.GetAssetRegistry()->Refresh();}
void AssetBrowserPanel::OnUpdate(float dt){
    auto* context = GetContext();
    auto* operators = context ? context->GetOperators() : nullptr;
    if (operators) operators->Assets().WatchIfDue(*context, dt, m_WatchAccumulator);
}

void AssetBrowserPanel::DeleteSelectedAsset() {
    auto* context = GetContext();
    if (!context) return;
    const std::string& path = context->GetSelection().GetAssetPath();
    if (path.empty()) return;
    auto* operators = context->GetOperators();
    if (!operators || !operators->Assets().DeleteAsset(*context, path)) {
        Logger::Warn("[Editor] Failed to delete asset: ", path);
    }
    m_PendingDelete = false;
}

void AssetBrowserPanel::DuplicateSelectedAsset() {
    auto* context = GetContext();
    if (!context) return;
    const std::string& path = context->GetSelection().GetAssetPath();
    if (path.empty()) return;
    auto* operators = context->GetOperators();
    if (!operators || !operators->Assets().DuplicateAsset(*context, path)) {
        Logger::Warn("[Editor] Failed to duplicate asset: ", path);
    }
}

void AssetBrowserPanel::RenameSelectedAsset() {
    auto* context = GetContext();
    if (!context) return;
    const std::string& path = context->GetSelection().GetAssetPath();
    if (path.empty()) { m_PendingRename = false; return; }
    std::string newName(m_RenameBuffer);
    if (newName.empty()) { m_PendingRename = false; return; }
    namespace fs = std::filesystem;
    fs::path src(path);
    fs::path dst = src.parent_path() / newName;
    if (src == dst) { m_PendingRename = false; return; }
    auto* operators = context->GetOperators();
    if (!operators || !operators->Assets().RenameAsset(*context, path, dst.string())) {
        Logger::Warn("[Editor] Failed to rename asset: ", path);
    }
    m_PendingRename = false;
}

void AssetBrowserPanel::EnsureSelectedFolder() {
    auto* context = GetContext();
    auto* registry = context ? context->GetAssetRegistry() : nullptr;
    if (!registry) return;
    for (const auto& folder : registry->GetFolders()) {
        if (folder.relativePath == m_SelectedFolder) return;
    }
    m_SelectedFolder = "Content";
}

std::filesystem::path AssetBrowserPanel::CurrentContentDirectory(const char* fallback) const {
    auto* context = GetContext();
    if (!context) return {};
    if (StartsWithPath(m_SelectedFolder, "Content")) {
        const std::string suffix = m_SelectedFolder == "Content"
            ? std::string{}
            : m_SelectedFolder.substr(std::string("Content/").size());
        return suffix.empty()
            ? context->GetContentRoot()
            : context->GetContentRoot() / std::filesystem::path(suffix);
    }
    return context->GetContentRoot() / fallback;
}

void AssetBrowserPanel::DrawContent(){
#if defined(MYENGINE_ENABLE_IMGUI)
    if (TryDrawScriptedBody("assetBrowser")) return;

    using namespace EditorPanelHelpers;auto* context=GetContext();auto* registry=context?context->GetAssetRegistry():nullptr;if(!registry)return;
    EnsureSelectedFolder();

    // Toolbar: Refresh | Import | Create Material / Texture / Script
    auto* operators = context->GetOperators();
    if(EditorWidgets::IconButton(*context, "RefreshAssets", EditorIcons::Refresh, "Refresh")){if(operators)operators->Assets().Refresh(*context);else registry->Refresh();EnsureSelectedFolder();}ImGui::SameLine();
    if(EditorWidgets::IconButton(*context, "ImportAsset", EditorIcons::Asset, "Import")){if(auto* dialogs=context->GetService<EditorDialogService>())dialogs->RequestImportAsset(context->GetWindow());}ImGui::SameLine();
    if(EditorWidgets::IconButton(*context, "ReimportAllAssets", EditorIcons::Refresh, "Reimport All")){
        if(auto* importer=context->GetService<EditorImportService>()){std::vector<std::string> failures;importer->ReimportAll(&failures);if(!failures.empty())Logger::Warn("[Editor] Reimport failures: ",failures.front());if(operators)operators->Assets().Refresh(*context);else registry->Refresh();EnsureSelectedFolder();}
    }ImGui::SameLine();

    if (ImGui::BeginCombo("##CreateAsset", "Create...")) {
        if (ImGui::Selectable("Material")) {
            const auto directory=CurrentContentDirectory("Materials");
            std::filesystem::create_directories(directory);
            const auto path=EditorImportService::MakeUniqueContentPath(directory,"NewMaterial",".mat");
            auto material=MaterialAsset::CreateDefault(path.stem().string());
            if(SaveMaterialAssetToFile(*material,path.string())){
                AssetManager::Get().Load<MaterialAsset>(path.string());registry->Refresh();EnsureSelectedFolder();
            }
        }
        if (ImGui::Selectable("Default Texture")) {
            const auto directory=CurrentContentDirectory("Textures");
            std::filesystem::create_directories(directory);
            const auto path=EditorImportService::MakeUniqueContentPath(directory,"NewTexture",".tex");
            try {
                std::ofstream out(path);
                out << "{}";
                out.close();
                registry->Refresh();EnsureSelectedFolder();
            } catch (...) {}
        }
        ImGui::EndCombo();
    }

    EditorWidgets::SvgIcon(*context, EditorIcons::Search, 14.0f);
    ImGui::SameLine();
    ImGui::InputTextWithHint("##Filter","Filter...",m_Filter,sizeof(m_Filter));ImGui::Separator();

    if (ImGui::BeginChild("##AssetFolderTree", ImVec2(220.0f, 0), true)) {
        const auto& folders = registry->GetFolders();
        std::function<void(const EditorAssetFolderInfo&)> drawFolder =
            [&](const EditorAssetFolderInfo& folder) {
                bool hasChildren = false;
                for (const auto& child : folders) {
                    if (IsDirectChildFolder(folder.relativePath, child.relativePath)) {
                        hasChildren = true;
                        break;
                    }
                }
                ImGui::PushID(folder.relativePath.c_str());
                EditorWidgets::SvgIcon(*context, EditorIcons::Folder, 14.0f);
                ImGui::SameLine();
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                    ImGuiTreeNodeFlags_SpanAvailWidth;
                if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (folder.relativePath == m_SelectedFolder) flags |= ImGuiTreeNodeFlags_Selected;
                const std::string label = folder.displayName + " (" + std::to_string(folder.assetCount) + ")";
                const bool open = ImGui::TreeNodeEx("##folder", flags, "%s", label.c_str());
                if (ImGui::IsItemClicked()) m_SelectedFolder = folder.relativePath;
                if (hasChildren && open) {
                    for (const auto& child : folders) {
                        if (IsDirectChildFolder(folder.relativePath, child.relativePath)) drawFolder(child);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            };
        for (const auto& folder : folders) {
            if (folder.relativePath == "Content" || folder.relativePath == "SourceAssets")
                drawFolder(folder);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();

    // Context menu detection for empty area
    bool emptyRightClick = false;
    if (EditorContextMenu::DetectWindow("##AssetCtxMenu")) emptyRightClick = true;

    for(const auto& asset:registry->GetAssetsInFolder(m_SelectedFolder,true)){
        if(m_Filter[0]&&asset.relativePath.find(m_Filter)==std::string::npos)continue;
        const bool selected=context->GetSelection().GetAssetPath()==asset.absolutePath.string();

        // Rename inline editing
        if (m_PendingRename && selected) {
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                                  ImGuiInputTextFlags_EnterReturnsTrue)) {
                RenameSelectedAsset();
            }
            if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_PendingRename = false;
            }
            continue;
        }

        // Delete confirmation
        if (m_PendingDelete && selected) {
            ImGui::TextUnformatted("Delete this asset?");
            if (ImGui::Button("Yes")) DeleteSelectedAsset();
            ImGui::SameLine();
            if (ImGui::Button("No")) m_PendingDelete = false;
            continue;
        }

        EditorWidgets::SvgIcon(*context, IconForAssetType(asset.type), 14.0f);
        ImGui::SameLine();
        std::string label=asset.relativePath;
        if(asset.imported)label+=" ["+std::string(ImportStateText(asset.importState))+"]";
        if(!asset.diagnostics.empty())label+=" !";
        if(ImGui::Selectable(label.c_str(),selected)){
            if(operators)operators->Selection().SelectAsset(*context,asset.absolutePath.string());
        }
        if (ImGui::IsItemHovered() && (asset.imported || !asset.diagnostics.empty())) {
            ImGui::BeginTooltip();
            if (asset.imported) {
                ImGui::Text("UUID: %s", asset.uuid.c_str());
                ImGui::Text("State: %s", ImportStateText(asset.importState));
                if (!asset.artifactPath.empty()) ImGui::Text("Artifact: %s", asset.artifactPath.string().c_str());
            }
            for (const auto& diagnostic : asset.diagnostics) {
                ImGui::Text("%s: %s", diagnostic.severity.c_str(), diagnostic.message.c_str());
            }
            ImGui::EndTooltip();
        }

        // Right-click context menu per asset
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if(operators)operators->Selection().SelectAsset(*context,asset.absolutePath.string());
            ImGui::OpenPopup("##AssetItemCtx");
        }
        if (ImGui::BeginPopup("##AssetItemCtx")) {
            if (asset.imported && ImGui::Selectable("Reimport")) {
                if(operators)operators->Assets().Reimport(*context,asset.uuid);
                else if(auto* importer=context->GetService<EditorImportService>())importer->Reimport(asset.uuid);
                EnsureSelectedFolder();
            }
            if (ImGui::Selectable("Delete")) { m_PendingDelete = true; }
            if (ImGui::Selectable("Duplicate")) { DuplicateSelectedAsset(); }
            if (ImGui::Selectable("Rename")) {
                m_PendingRename = true;
                std::strncpy(m_RenameBuffer, asset.relativePath.c_str(), sizeof(m_RenameBuffer)-1);
                m_RenameBuffer[sizeof(m_RenameBuffer)-1] = '\0';
            }
            DrawScriptAssetContextMenu(*context, asset.type);
            ImGui::EndPopup();
        }

        // Drag source for supported types
        if(asset.type==EditorAssetType::Model){AssetDragDropSource(kModelPayload,asset.absolutePath.string(),asset.relativePath).Draw();}
        if(asset.type==EditorAssetType::Texture){AssetDragDropSource(kTexturePayload,asset.absolutePath.string(),asset.relativePath).Draw();}
        if(asset.type==EditorAssetType::Prefab){AssetDragDropSource(kPrefabPayload,asset.absolutePath.string(),asset.relativePath).Draw();}
    }

    // Empty-area context menu
    if (emptyRightClick) {
        ImGui::OpenPopup("##AssetCtxMenu");
    }
    if (ImGui::BeginPopup("##AssetCtxMenu")) {
        if (ImGui::Selectable("Refresh")) { if(operators)operators->Assets().Refresh(*context);else registry->Refresh(); EnsureSelectedFolder(); }
        if (ImGui::Selectable("Import...")) {
            if(auto* dialogs=context->GetService<EditorDialogService>()) dialogs->RequestImportAsset(context->GetWindow());
        }
        DrawScriptAssetContextMenu(*context, EditorAssetType::Unknown);
        ImGui::EndPopup();
    }
    ImGui::EndGroup();

#endif
}
