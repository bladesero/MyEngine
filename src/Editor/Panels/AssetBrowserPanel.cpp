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
#include "Editor/EditorLayout.h"
#include "Editor/EditorPanelHelpers.h"
#include "Editor/EditorProject.h"
#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
constexpr const char kModelPayload[]="MYENGINE_MODEL_PATH";
constexpr const char kTexturePayload[]="MYENGINE_TEXTURE_PATH";
constexpr const char kPrefabPayload[]="MYENGINE_PREFAB_PATH";

std::string ReadFileContent(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}

AssetBrowserPanel::AssetBrowserPanel():EditorPanel("assetBrowser","Asset Browser"){}
void AssetBrowserPanel::OnAttach(EditorContext& context){EditorPanel::OnAttach(context);if(context.GetAssetRegistry())context.GetAssetRegistry()->Refresh();}
void AssetBrowserPanel::OnUpdate(float dt){(void)dt;auto* registry=GetContext()?GetContext()->GetAssetRegistry():nullptr;if(registry)registry->WatchForChanges();}
void AssetBrowserPanel::OnImGui(){if(IsVisible())DrawContent();}

void AssetBrowserPanel::DeleteSelectedAsset() {
    auto* context = GetContext();
    if (!context) return;
    const std::string& path = context->GetSelection().GetAssetPath();
    if (path.empty()) return;
    try {
        std::string content = ReadFileContent(path);
        auto* stack = context->GetCommandStack();
        if (!stack) {
            std::filesystem::remove(path);
            context->GetAssetRegistry()->Refresh();
        } else {
            stack->ExecuteCommand(std::make_unique<DeleteAssetCommand>(path, content), *context);
            context->GetAssetRegistry()->Refresh();
        }
        context->GetSelection().Clear();
    } catch (...) {
        Logger::Warn("[Editor] Failed to delete asset: ", path);
    }
    m_PendingDelete = false;
}

void AssetBrowserPanel::DuplicateSelectedAsset() {
    auto* context = GetContext();
    if (!context) return;
    const std::string& path = context->GetSelection().GetAssetPath();
    if (path.empty()) return;
    namespace fs = std::filesystem;
    fs::path src(path);
    fs::path dst = src.parent_path() / (src.stem().string() + "_Copy" + src.extension().string());
    try {
        std::string content = ReadFileContent(path);
        auto* stack = context->GetCommandStack();
        if (!stack) {
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
            AssetManager::Get().Load<Asset>(dst.string());
            context->GetAssetRegistry()->Refresh();
        } else {
            stack->ExecuteCommand(std::make_unique<CreateAssetCommand>(dst.string(), content), *context);
            AssetManager::Get().Load<Asset>(dst.string());
            context->GetAssetRegistry()->Refresh();
        }
    } catch (...) {
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
    try {
        auto* stack = context->GetCommandStack();
        if (!stack) {
            fs::rename(src, dst);
            context->GetAssetRegistry()->Refresh();
        } else {
            stack->ExecuteCommand(std::make_unique<RenameAssetCommand>(src.string(), dst.string()), *context);
            context->GetAssetRegistry()->Refresh();
        }
        context->GetSelection().Select(EditorSelectObject::MakeAsset(dst.string()));
    } catch (...) {
        Logger::Warn("[Editor] Failed to rename asset: ", path);
    }
    m_PendingRename = false;
}

void AssetBrowserPanel::DrawContent(){
#if defined(MYENGINE_ENABLE_IMGUI)
    using namespace EditorPanelHelpers;auto* context=GetContext();auto* registry=context?context->GetAssetRegistry():nullptr;if(!registry)return;const auto* viewport=ImGui::GetMainViewport();const auto rect=EditorLayout::Compute(viewport->WorkPos.x,viewport->WorkPos.y,viewport->WorkSize.x,viewport->WorkSize.y).assetBrowser;ImGui::SetNextWindowPos({rect.x,rect.y});ImGui::SetNextWindowSize({rect.width,rect.height});ImGui::Begin("Asset Browser");

    // Toolbar: Refresh | Import | Create Material / Texture / Script
    if(ImGui::Button("Refresh"))registry->Refresh();ImGui::SameLine();
    if(ImGui::Button("Import")){if(auto* dialogs=context->GetService<EditorDialogService>())dialogs->RequestImportAsset(context->GetWindow());}ImGui::SameLine();

    if (ImGui::BeginCombo("##CreateAsset", "Create...")) {
        if (ImGui::Selectable("Material")) {
            const auto directory=context->GetContentRoot()/"Materials";
            const auto path=EditorImportService::MakeUniqueContentPath(directory,"NewMaterial",".mat");
            auto material=MaterialAsset::CreateDefault(path.stem().string());
            if(SaveMaterialAssetToFile(*material,path.string())){
                AssetManager::Get().Load<MaterialAsset>(path.string());registry->Refresh();
            }
        }
        if (ImGui::Selectable("Default Texture")) {
            const auto directory=context->GetContentRoot()/"Textures";
            std::filesystem::create_directories(directory);
            const auto path=EditorImportService::MakeUniqueContentPath(directory,"NewTexture",".tex");
            try {
                std::ofstream out(path);
                out << "{}";
                out.close();
                registry->Refresh();
            } catch (...) {}
        }
        ImGui::EndCombo();
    }

    ImGui::InputTextWithHint("##Filter","Filter...",m_Filter,sizeof(m_Filter));ImGui::Separator();

    // Context menu detection for empty area
    bool emptyRightClick = false;
    if (EditorContextMenu::DetectWindow("##AssetCtxMenu")) emptyRightClick = true;

    for(const auto& asset:registry->GetAssets()){
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

        if(ImGui::Selectable(asset.relativePath.c_str(),selected)){
            context->GetSelection().Select(
                EditorSelectObject::MakeAsset(asset.absolutePath.string()));
            if(context->GetProject())context->GetProject()->GetState().selectedAssetPath=asset.absolutePath.string();
        }

        // Right-click context menu per asset
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            context->GetSelection().Select(
                EditorSelectObject::MakeAsset(asset.absolutePath.string()));
            ImGui::OpenPopup("##AssetItemCtx");
        }
        if (ImGui::BeginPopup("##AssetItemCtx")) {
            if (ImGui::Selectable("Delete")) { m_PendingDelete = true; }
            if (ImGui::Selectable("Duplicate")) { DuplicateSelectedAsset(); }
            if (ImGui::Selectable("Rename")) {
                m_PendingRename = true;
                std::strncpy(m_RenameBuffer, asset.relativePath.c_str(), sizeof(m_RenameBuffer)-1);
                m_RenameBuffer[sizeof(m_RenameBuffer)-1] = '\0';
            }
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
        if (ImGui::Selectable("Refresh")) registry->Refresh();
        if (ImGui::Selectable("Import...")) {
            if(auto* dialogs=context->GetService<EditorDialogService>()) dialogs->RequestImportAsset(context->GetWindow());
        }
        ImGui::EndPopup();
    }

    ImGui::End();
#endif
}
