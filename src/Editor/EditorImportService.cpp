#include "Editor/EditorImportService.h"
#include "Editor/AssetImportService.h"

#include "Assets/AssetManager.h"
#include "Assets/AssetMeta.h"
#include "Assets/MaterialAsset.h"
#include "Assets/ModelAsset.h"
#include "Assets/TextureAsset.h"
#include "Core/Logger.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorContext.h"

#include <algorithm>
#include <cctype>

namespace { std::string LowerExtension(const std::filesystem::path& path) {
    std::string value=path.extension().string(); std::transform(value.begin(),value.end(),value.begin(),
        [](unsigned char c){return static_cast<char>(std::tolower(c));}); return value; } }

std::filesystem::path EditorImportService::MakeUniqueContentPath(const std::filesystem::path& directory,
    const std::string& stem, const std::string& extension) {
    std::filesystem::path result=directory/(stem+extension); int suffix=1;
    while(std::filesystem::exists(result)) result=directory/(stem+"_"+std::to_string(suffix++)+extension);
    return result;
}
EditorImportService::EditorImportService()
    : m_ImportPipeline(std::make_unique<AssetImportService>()) {}

EditorImportService::~EditorImportService() = default;

void EditorImportService::OnAttach(EditorContext& context) {
    EditorService::OnAttach(context);
    std::string error;
    if (!m_ImportPipeline->OpenProject(context.GetProjectRoot(), &error))
        Logger::Warn("[Editor] Asset import pipeline unavailable: ", error);
}

bool EditorImportService::Import(const std::string& sourcePath) {
    EditorContext* context=GetContext(); if(!context) return false;
    namespace fs=std::filesystem; std::error_code error; const fs::path source(sourcePath);
    if(!fs::is_regular_file(source,error)) return false;
    const std::string extension=LowerExtension(source);
    const bool model=extension==".obj"||extension==".gltf"||extension==".glb";
    const bool texture=extension==".png"||extension==".jpg"||extension==".jpeg"||extension==".bmp"||extension==".tga"||extension==".hdr";
    if(!model&&!texture) { Logger::Warn("[Editor] Unsupported import: ",sourcePath); return false; }
    std::string importError;
    const AssetImportReport report = m_ImportPipeline->Import(source, "{}", &importError);
    if (!report.succeeded) {
        Logger::Warn("[Editor] Import failed: ", importError);
        return false;
    }
    AssetManager::Get().RegisterPersistentIdentity(report.record.artifactPath,
                                                   report.record.uuid);
    if(model) AssetManager::Get().Load<ModelAsset>(report.record.artifactPath);
    else AssetManager::Get().Load<TextureAsset>(report.record.artifactPath);
    if(context->GetAssetRegistry()) context->GetAssetRegistry()->Refresh();
    Logger::Info("[Editor] Imported asset: ",report.record.sourcePath); return true;
}
