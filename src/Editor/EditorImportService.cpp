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
#include "Editor/EditorProfiler.h"

#include <algorithm>
#include <chrono>
#include <cctype>

namespace {

using ImportClock = std::chrono::steady_clock;

double ElapsedImportMs(ImportClock::time_point start)
{
    return std::chrono::duration<double, std::milli>(
        ImportClock::now() - start).count();
}

std::string LowerExtension(const std::filesystem::path& path) {
    std::string value=path.extension().string(); std::transform(value.begin(),value.end(),value.begin(),
        [](unsigned char c){return static_cast<char>(std::tolower(c));}); return value; }

void RecordImportEvent(EditorContext* context, const char* operation,
                       const std::string& sourceOrUuid, double durationMs,
                       bool success, std::string details = {})
{
    std::string eventDetails = "source=" + sourceOrUuid;
    eventDetails += success ? ";success=true" : ";success=false";
    if (!details.empty()) {
        eventDetails += ";";
        eventDetails += std::move(details);
    }
    if (context) {
        if (EditorProfiler* profiler = context->GetProfiler()) {
            profiler->RecordEvent("EditorImport", operation, durationMs,
                                  std::move(eventDetails));
        }
    }
    if (!success) {
        Logger::Warn("[EditorImport] ", operation, " failed: ", sourceOrUuid);
    }
}

}

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
    const auto start = ImportClock::now();
    EditorContext* context=GetContext(); if(!context) return false;
    namespace fs=std::filesystem; std::error_code error; const fs::path source(sourcePath);
    if(!fs::is_regular_file(source,error)) {
        RecordImportEvent(context, "Import", sourcePath, ElapsedImportMs(start), false);
        return false;
    }
    const std::string extension=LowerExtension(source);
    const bool model=extension==".obj"||extension==".gltf"||extension==".glb";
    const bool texture=extension==".png"||extension==".jpg"||extension==".jpeg"||extension==".bmp"||extension==".tga"||extension==".hdr";
    if(!model&&!texture) {
        Logger::Warn("[Editor] Unsupported import: ",sourcePath);
        RecordImportEvent(context, "Import", sourcePath, ElapsedImportMs(start),
                          false, "unsupported=true");
        return false;
    }
    std::string importError;
    const AssetImportReport report = m_ImportPipeline->Import(source, "{}", &importError);
    if (!report.succeeded) {
        Logger::Warn("[Editor] Import failed: ", importError);
        RecordImportEvent(context, "Import", sourcePath, ElapsedImportMs(start),
                          false, "error=" + importError);
        return false;
    }
    AssetManager::Get().RegisterPersistentIdentity(report.record.artifactPath,
                                                   report.record.uuid);
    if(model) AssetManager::Get().Load<ModelAsset>(report.record.artifactPath);
    else AssetManager::Get().Load<TextureAsset>(report.record.artifactPath);
    if(context->GetAssetRegistry()) context->GetAssetRegistry()->Refresh();
    Logger::Info("[Editor] Imported asset: ",report.record.sourcePath);
    RecordImportEvent(context, "Import", report.record.sourcePath,
                      ElapsedImportMs(start), true,
                      "artifact=" + report.record.artifactPath +
                      ";uuid=" + report.record.uuid);
    return true;
}

bool EditorImportService::Reimport(const std::string& uuid) {
    const auto start = ImportClock::now();
    EditorContext* context = GetContext();
    if (uuid.empty() || !m_ImportPipeline) {
        RecordImportEvent(context, "Reimport", uuid, ElapsedImportMs(start), false);
        return false;
    }
    std::string error;
    const AssetImportReport report = m_ImportPipeline->Reimport(uuid, &error);
    if (!report.succeeded) {
        Logger::Warn("[Editor] Reimport failed: ", error);
        RecordImportEvent(context, "Reimport", uuid, ElapsedImportMs(start),
                          false, "error=" + error);
        return false;
    }
    if (context && context->GetAssetRegistry()) {
        context->GetAssetRegistry()->Refresh();
    }
    Logger::Info("[Editor] Reimported asset: ", report.record.sourcePath);
    RecordImportEvent(context, "Reimport", report.record.sourcePath,
                      ElapsedImportMs(start), true,
                      "uuid=" + report.record.uuid);
    return true;
}

bool EditorImportService::ReimportWithSettings(
    const std::string& uuid, const std::string& settingsJson) {
    const auto start = ImportClock::now();
    EditorContext* context = GetContext();
    if (uuid.empty() || !m_ImportPipeline) {
        RecordImportEvent(context, "Reimport With Settings", uuid,
                          ElapsedImportMs(start), false);
        return false;
    }
    std::string error;
    const AssetImportReport report =
        m_ImportPipeline->ReimportWithSettings(uuid, settingsJson, &error);
    if (!report.succeeded) {
        Logger::Warn("[Editor] Reimport with settings failed: ", error);
        RecordImportEvent(context, "Reimport With Settings", uuid,
                          ElapsedImportMs(start), false,
                          "error=" + error);
        return false;
    }
    AssetManager::Get().RegisterPersistentIdentity(report.record.artifactPath,
                                                   report.record.uuid);
    if (context && context->GetAssetRegistry()) {
        context->GetAssetRegistry()->Refresh();
    }
    Logger::Info("[Editor] Updated import settings: ", report.record.sourcePath);
    RecordImportEvent(context, "Reimport With Settings", report.record.sourcePath,
                      ElapsedImportMs(start), true,
                      "uuid=" + report.record.uuid);
    return true;
}

size_t EditorImportService::ReimportAll(std::vector<std::string>* failures) {
    const auto start = ImportClock::now();
    EditorContext* context = GetContext();
    if (!m_ImportPipeline) {
        RecordImportEvent(context, "Reimport All", "*", ElapsedImportMs(start), false);
        return 0;
    }
    const size_t succeeded = m_ImportPipeline->ReimportAll(failures);
    if (context && context->GetAssetRegistry()) {
        context->GetAssetRegistry()->Refresh();
    }
    const size_t failureCount = failures ? failures->size() : 0;
    RecordImportEvent(context, "Reimport All", "*", ElapsedImportMs(start),
                      failureCount == 0, "succeeded=" + std::to_string(succeeded) +
                      ";failures=" + std::to_string(failureCount));
    return succeeded;
}

bool EditorImportService::RefreshValidation(std::string* error) {
    const auto start = ImportClock::now();
    EditorContext* context = GetContext();
    if (!m_ImportPipeline) {
        RecordImportEvent(context, "Validate", "*", ElapsedImportMs(start), false);
        if (error) *error = "asset import pipeline is unavailable";
        return false;
    }
    const bool passed = m_ImportPipeline->RefreshValidation(error);
    const auto* report = GetValidationReport();
    const size_t issueCount = report ? report->issues.size() : 0;
    RecordImportEvent(context, "Validate", "*", ElapsedImportMs(start), passed,
                      "issues=" + std::to_string(issueCount));
    return passed;
}

const AssetDatabaseValidationReport* EditorImportService::GetValidationReport() const {
    return m_ImportPipeline ? &m_ImportPipeline->GetValidationReport() : nullptr;
}

std::string EditorImportService::GetValidationSummaryText() const {
    const AssetDatabaseValidationReport* report = GetValidationReport();
    if (!report) return "asset validation unavailable";
    return report->Summary();
}

bool EditorImportService::HasValidationIssues() const {
    const AssetDatabaseValidationReport* report = GetValidationReport();
    return report && !report->Passed();
}
