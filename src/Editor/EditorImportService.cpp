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
#include "Renderer/ShaderCacheService.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace {

using ImportClock = std::chrono::steady_clock;

double ElapsedImportMs(ImportClock::time_point start) {
    return std::chrono::duration<double, std::milli>(ImportClock::now() - start).count();
}

std::string LowerExtension(const std::filesystem::path& path) {
    std::string value = path.extension().string();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::filesystem::path StripAssetFragment(const std::string& value) {
    const size_t fragment = value.find('#');
    return std::filesystem::path(fragment == std::string::npos ? value : value.substr(0, fragment));
}

void CollectModelReferences(const nlohmann::json& value, std::vector<std::string>& references) {
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        const std::filesystem::path source = StripAssetFragment(text);
        const std::string extension = LowerExtension(source);
        if (extension == ".gltf" || extension == ".glb") {
            references.push_back(text);
        }
        return;
    }
    if (value.is_array()) {
        for (const auto& item : value)
            CollectModelReferences(item, references);
        return;
    }
    if (value.is_object()) {
        for (const auto& item : value.items())
            CollectModelReferences(item.value(), references);
    }
}

std::filesystem::path ResolveProjectAssetPath(const std::filesystem::path& projectRoot, const std::string& reference) {
    std::filesystem::path source = StripAssetFragment(reference);
    if (source.empty())
        return {};
    std::error_code error;
    if (!source.is_absolute()) {
        source = projectRoot / source;
    }
    return std::filesystem::absolute(source, error).lexically_normal();
}

void RecordImportEvent(EditorContext* context, const char* operation, const std::string& sourceOrUuid,
                       double durationMs, bool success, std::string details = {}) {
    std::string eventDetails = "source=" + sourceOrUuid;
    eventDetails += success ? ";success=true" : ";success=false";
    if (!details.empty()) {
        eventDetails += ";";
        eventDetails += std::move(details);
    }
    if (context) {
        if (EditorProfiler* profiler = context->GetProfiler()) {
            profiler->RecordEvent("EditorImport", operation, durationMs, std::move(eventDetails));
        }
    }
    if (!success) {
        Logger::Warn("[EditorImport] ", operation, " failed: ", sourceOrUuid);
    }
}

} // namespace

std::filesystem::path EditorImportService::MakeUniqueContentPath(const std::filesystem::path& directory,
                                                                 const std::string& stem,
                                                                 const std::string& extension) {
    std::filesystem::path result = directory / (stem + extension);
    int suffix = 1;
    while (std::filesystem::exists(result))
        result = directory / (stem + "_" + std::to_string(suffix++) + extension);
    return result;
}
EditorImportService::EditorImportService() : m_ImportPipeline(std::make_unique<AssetImportService>()) {
}

EditorImportService::~EditorImportService() {
    ShaderCacheService::Get().ClearResolver();
}

void EditorImportService::OnAttach(EditorContext& context) {
    EditorService::OnAttach(context);
    std::string error;
    if (!m_ImportPipeline->OpenProject(context.GetProjectRoot(), &error))
        Logger::Warn("[Editor] Asset import pipeline unavailable: ", error);
    ShaderCacheService::Get().SetResolver([this](const ShaderCacheRequest& request) {
        ShaderCacheResult result;
        std::string error;
        bool cacheHit = false;
        if (EnsureShaderCache(request.sourcePath, "{}", request.allowCompile, result.artifactPath, cacheHit, &error)) {
            result.succeeded = true;
            result.cacheHit = cacheHit;
        } else {
            result.diagnostic = std::move(error);
        }
        return result;
    });
}

bool EditorImportService::EnsureShaderCache(const std::filesystem::path& sourcePath, const std::string& settingsJson,
                                            bool allowCompile, std::filesystem::path& outArtifactPath,
                                            bool& outCacheHit, std::string* error) {
    if (!m_ImportPipeline) {
        if (error)
            *error = "asset import pipeline is unavailable";
        return false;
    }

    const std::filesystem::path resolved = AssetManager::Get().ResolvePath(sourcePath.string());
    const AssetRecord* existing = m_ImportPipeline->GetDatabase().FindBySourcePath(resolved.generic_string());
    if (!existing) {
        std::error_code ec;
        const std::filesystem::path relative =
            std::filesystem::relative(resolved, AssetManager::Get().GetProjectRoot(), ec);
        if (!ec && !relative.empty() && !relative.is_absolute()) {
            existing = m_ImportPipeline->GetDatabase().FindBySourcePath(relative.generic_string());
        }
    }
    if (existing && existing->state == AssetImportState::Ready && existing->type == "shader" &&
        std::filesystem::is_regular_file(existing->artifactPath)) {
        outArtifactPath = existing->artifactPath;
        outCacheHit = true;
        return true;
    }
    if (!allowCompile) {
        if (error)
            *error = "shader cache artifact is missing and compilation is disabled";
        return false;
    }

    AssetImportReport report;
    const std::string projectRelative = AssetManager::Get().MakeProjectRelativePath(resolved.string());
    if (projectRelative.rfind("Content/Engine/", 0) == 0 || projectRelative.rfind("Content\\Engine\\", 0) == 0) {
        report = m_ImportPipeline->ImportEngineShaderSource(resolved, settingsJson, error);
    } else {
        const std::string uuid = existing ? existing->uuid : std::string{};
        report = m_ImportPipeline->ImportSource(resolved, settingsJson, uuid, error);
    }
    if (!report.succeeded)
        return false;
    AssetManager::Get().RegisterPersistentIdentity(report.record.artifactPath, report.record.uuid);
    outArtifactPath = report.record.artifactPath;
    outCacheHit = report.cacheHit;
    return true;
}

bool EditorImportService::Import(const std::string& sourcePath) {
    const auto start = ImportClock::now();
    EditorContext* context = GetContext();
    if (!context)
        return false;
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path source(sourcePath);
    if (!fs::is_regular_file(source, error)) {
        RecordImportEvent(context, "Import", sourcePath, ElapsedImportMs(start), false);
        return false;
    }
    const std::string extension = LowerExtension(source);
    const bool model = extension == ".obj" || extension == ".gltf" || extension == ".glb";
    const bool texture = extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
                         extension == ".tga" || extension == ".hdr";
    if (!model && !texture) {
        Logger::Warn("[Editor] Unsupported import: ", sourcePath);
        RecordImportEvent(context, "Import", sourcePath, ElapsedImportMs(start), false, "unsupported=true");
        return false;
    }
    std::string importError;
    const AssetImportReport report = m_ImportPipeline->Import(source, "{}", &importError);
    if (!report.succeeded) {
        Logger::Warn("[Editor] Import failed: ", importError);
        RecordImportEvent(context, "Import", sourcePath, ElapsedImportMs(start), false, "error=" + importError);
        return false;
    }
    AssetManager::Get().RegisterPersistentIdentity(report.record.artifactPath, report.record.uuid);
    if (model)
        AssetManager::Get().Load<ModelAsset>(report.record.artifactPath);
    else
        AssetManager::Get().Load<TextureAsset>(report.record.artifactPath);
    if (context->GetAssetRegistry())
        context->GetAssetRegistry()->Refresh();
    Logger::Info("[Editor] Imported asset: ", report.record.sourcePath);
    RecordImportEvent(context, "Import", report.record.sourcePath, ElapsedImportMs(start), true,
                      "artifact=" + report.record.artifactPath + ";uuid=" + report.record.uuid);
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
        RecordImportEvent(context, "Reimport", uuid, ElapsedImportMs(start), false, "error=" + error);
        return false;
    }
    if (context && context->GetAssetRegistry()) {
        context->GetAssetRegistry()->Refresh();
    }
    Logger::Info("[Editor] Reimported asset: ", report.record.sourcePath);
    RecordImportEvent(context, "Reimport", report.record.sourcePath, ElapsedImportMs(start), true,
                      "uuid=" + report.record.uuid);
    return true;
}

bool EditorImportService::ReimportWithSettings(const std::string& uuid, const std::string& settingsJson) {
    const auto start = ImportClock::now();
    EditorContext* context = GetContext();
    if (uuid.empty() || !m_ImportPipeline) {
        RecordImportEvent(context, "Reimport With Settings", uuid, ElapsedImportMs(start), false);
        return false;
    }
    std::string error;
    const AssetImportReport report = m_ImportPipeline->ReimportWithSettings(uuid, settingsJson, &error);
    if (!report.succeeded) {
        Logger::Warn("[Editor] Reimport with settings failed: ", error);
        RecordImportEvent(context, "Reimport With Settings", uuid, ElapsedImportMs(start), false, "error=" + error);
        return false;
    }
    AssetManager::Get().RegisterPersistentIdentity(report.record.artifactPath, report.record.uuid);
    if (context && context->GetAssetRegistry()) {
        context->GetAssetRegistry()->Refresh();
    }
    Logger::Info("[Editor] Updated import settings: ", report.record.sourcePath);
    RecordImportEvent(context, "Reimport With Settings", report.record.sourcePath, ElapsedImportMs(start), true,
                      "uuid=" + report.record.uuid);
    return true;
}

size_t EditorImportService::EnsureModelCachesForScene(const std::filesystem::path& scenePath,
                                                      std::vector<std::string>* failures) {
    const auto start = ImportClock::now();
    EditorContext* context = GetContext();
    if (!context || !m_ImportPipeline || !std::filesystem::is_regular_file(scenePath)) {
        RecordImportEvent(context, "Warm Model Cache", scenePath.string(), ElapsedImportMs(start), false);
        return 0;
    }

    std::vector<std::string> references;
    try {
        std::ifstream input(scenePath);
        nlohmann::json sceneJson;
        input >> sceneJson;
        CollectModelReferences(sceneJson, references);
    } catch (const std::exception& exception) {
        if (failures)
            failures->push_back(scenePath.string() + ": " + exception.what());
        RecordImportEvent(context, "Warm Model Cache", scenePath.string(), ElapsedImportMs(start), false,
                          "error=" + std::string(exception.what()));
        return 0;
    }

    std::unordered_set<std::string> visited;
    size_t warmed = 0;
    for (const std::string& reference : references) {
        const std::filesystem::path source = ResolveProjectAssetPath(context->GetProjectRoot(), reference);
        if (source.empty() || !std::filesystem::is_regular_file(source))
            continue;
        const std::string sourceKey = source.generic_string();
        if (!visited.insert(sourceKey).second)
            continue;

        std::string error;
        const AssetRecord* existing = m_ImportPipeline->GetDatabase().FindBySourcePath(sourceKey);
        const std::string uuid = existing ? existing->uuid : std::string{};
        AssetImportReport report = m_ImportPipeline->ImportSource(source, "{}", uuid, &error);
        if (!report.succeeded) {
            if (failures)
                failures->push_back(sourceKey + ": " + error);
            continue;
        }
        if (std::filesystem::path(report.record.artifactPath).extension() == ".modelbin") {
            AssetManager::Get().RegisterPersistentIdentity(report.record.artifactPath, report.record.uuid);
            ++warmed;
        }
    }

    RecordImportEvent(context, "Warm Model Cache", scenePath.string(), ElapsedImportMs(start),
                      failures ? failures->empty() : true, "models=" + std::to_string(warmed));
    return warmed;
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
    RecordImportEvent(context, "Reimport All", "*", ElapsedImportMs(start), failureCount == 0,
                      "succeeded=" + std::to_string(succeeded) + ";failures=" + std::to_string(failureCount));
    return succeeded;
}

bool EditorImportService::RefreshValidation(std::string* error) {
    const auto start = ImportClock::now();
    EditorContext* context = GetContext();
    if (!m_ImportPipeline) {
        RecordImportEvent(context, "Validate", "*", ElapsedImportMs(start), false);
        if (error)
            *error = "asset import pipeline is unavailable";
        return false;
    }
    const bool passed = m_ImportPipeline->RefreshValidation(error);
    const auto* report = GetValidationReport();
    const size_t issueCount = report ? report->issues.size() : 0;
    RecordImportEvent(context, "Validate", "*", ElapsedImportMs(start), passed, "issues=" + std::to_string(issueCount));
    return passed;
}

const AssetDatabaseValidationReport* EditorImportService::GetValidationReport() const {
    return m_ImportPipeline ? &m_ImportPipeline->GetValidationReport() : nullptr;
}

std::string EditorImportService::GetValidationSummaryText() const {
    const AssetDatabaseValidationReport* report = GetValidationReport();
    if (!report)
        return "asset validation unavailable";
    return report->Summary();
}

bool EditorImportService::HasValidationIssues() const {
    const AssetDatabaseValidationReport* report = GetValidationReport();
    return report && !report->Passed();
}
