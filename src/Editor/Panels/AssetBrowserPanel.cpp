#include "Editor/EditorPanels.h"

#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"
#include "Assets/ModelAsset.h"
#include "Assets/TextureAsset.h"
#include "Core/Logger.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorDialogService.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorLayout.h"
#include "Editor/EditorPanelHelpers.h"
#include "Editor/EditorProject.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace {constexpr const char kModelPayload[]="MYENGINE_MODEL_PATH";constexpr const char kTexturePayload[]="MYENGINE_TEXTURE_PATH";}
AssetBrowserPanel::AssetBrowserPanel():EditorPanel("assetBrowser","Asset Browser"){}
void AssetBrowserPanel::OnAttach(EditorContext& context){EditorPanel::OnAttach(context);if(context.GetAssetRegistry())context.GetAssetRegistry()->Refresh();}
void AssetBrowserPanel::OnUpdate(float dt){(void)dt;auto* registry=GetContext()?GetContext()->GetAssetRegistry():nullptr;if(registry)registry->WatchForChanges();}
void AssetBrowserPanel::OnImGui(){if(IsVisible())DrawContent();}
void AssetBrowserPanel::DrawContent(){
#if defined(MYENGINE_ENABLE_IMGUI)
    using namespace EditorPanelHelpers;auto* context=GetContext();auto* registry=context?context->GetAssetRegistry():nullptr;if(!registry)return;const auto* viewport=ImGui::GetMainViewport();const auto rect=EditorLayout::Compute(viewport->WorkPos.x,viewport->WorkPos.y,viewport->WorkSize.x,viewport->WorkSize.y).assetBrowser;ImGui::SetNextWindowPos({rect.x,rect.y});ImGui::SetNextWindowSize({rect.width,rect.height});ImGui::Begin("Asset Browser");
    if(ImGui::Button("Refresh"))registry->Refresh();ImGui::SameLine();if(ImGui::Button("Import")){if(auto* dialogs=context->GetService<EditorDialogService>())dialogs->RequestImportAsset(context->GetWindow());}ImGui::SameLine();
    if(ImGui::Button("Create Material")){const auto directory=context->GetContentRoot()/"Materials";const auto path=EditorImportService::MakeUniqueContentPath(directory,"NewMaterial",".mat");auto material=MaterialAsset::CreateDefault(path.stem().string());if(SaveMaterialAssetToFile(*material,path.string())){AssetManager::Get().Load<MaterialAsset>(path.string());registry->Refresh();}}
    ImGui::InputTextWithHint("##Filter","Filter...",m_Filter,sizeof(m_Filter));ImGui::Separator();
    for(const auto& asset:registry->GetAssets()){if(m_Filter[0]&&asset.relativePath.find(m_Filter)==std::string::npos)continue;const bool selected=context->GetSelection().GetAssetPath()==asset.absolutePath.string();if(ImGui::Selectable(asset.relativePath.c_str(),selected)){context->GetSelection().SelectAssetPath(asset.absolutePath.string());if(context->GetProject())context->GetProject()->GetState().selectedAssetPath=asset.absolutePath.string();}
        if(asset.type==EditorAssetType::Model&&ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)){const std::string path=asset.absolutePath.string();ImGui::SetDragDropPayload(kModelPayload,path.c_str(),path.size()+1);ImGui::TextUnformatted(asset.relativePath.c_str());ImGui::EndDragDropSource();}
        if(asset.type==EditorAssetType::Texture&&ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)){const std::string path=asset.absolutePath.string();ImGui::SetDragDropPayload(kTexturePayload,path.c_str(),path.size()+1);ImGui::TextUnformatted(asset.relativePath.c_str());ImGui::EndDragDropSource();}}
    ImGui::End();
#endif
}
