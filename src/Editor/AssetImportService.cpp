#include "Editor/AssetImportService.h"

#include "Assets/AssetMeta.h"
#include "Assets/AssetManager.h"
#include "Core/Sha256.h"
#include "Project/RuntimeCompatibility.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_set>

namespace {
void SetError(std::string* error, std::string value) { if (error) *error = std::move(value); }

std::string LowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

bool IsModelSourcePath(const std::filesystem::path& path)
{
    const std::string extension = LowerExtension(path);
    return extension == ".obj" || extension == ".gltf" || extension == ".glb";
}

std::filesystem::path SdfVoxelSidecarPathFor(const std::filesystem::path& path)
{
    return path.parent_path() / (path.stem().string() + ".sdfvox.xml");
}

std::string SourceKey(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::absolute(path, ec).lexically_normal().generic_string();
}

std::string ExistingSourceUuid(const std::filesystem::path& source)
{
    if (auto meta = AssetMeta::Load(source.string())) return meta->uuid;
    return {};
}

void MirrorBakedSidecarNextToSource(const AssetRecord& record)
{
    if (record.sourcePath.empty() || record.artifactPath.empty()) return;
    const std::filesystem::path sourcePath(record.sourcePath);
    const std::filesystem::path artifactPath(record.artifactPath);
    const std::filesystem::path artifactSidecar = SdfVoxelSidecarPathFor(artifactPath);
    if (!std::filesystem::is_regular_file(artifactSidecar)) return;

    const std::filesystem::path sourceSidecar = SdfVoxelSidecarPathFor(sourcePath);
    std::error_code ec;
    if (std::filesystem::equivalent(artifactSidecar, sourceSidecar, ec)) return;
    ec.clear();
    std::filesystem::copy_file(artifactSidecar, sourceSidecar,
        std::filesystem::copy_options::overwrite_existing, ec);
}

bool HasRequiredSdfVoxelSidecars(const std::filesystem::path& source,
                                 const std::filesystem::path& artifact)
{
    if (!std::filesystem::is_regular_file(SdfVoxelSidecarPathFor(source))) return false;
    return artifact.empty() ||
        std::filesystem::is_regular_file(SdfVoxelSidecarPathFor(artifact));
}

bool GltfArtifactDependenciesPresent(const std::filesystem::path& source,
                                     const std::filesystem::path& artifact)
{
    if (LowerExtension(source) != ".gltf" || artifact.empty()) return true;
    const std::filesystem::path sourceRoot = source.parent_path();
    const std::filesystem::path artifactRoot = artifact.parent_path();
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(sourceRoot, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path path = it->path();
        if (path == source) continue;
        const std::string extension = LowerExtension(path);
        if (extension == ".meta" || extension == ".sdfvox.xml") continue;
        const std::filesystem::path relative = std::filesystem::relative(path, sourceRoot, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (!std::filesystem::is_regular_file(artifactRoot / relative)) return false;
    }
    return true;
}

bool HasRequiredBakedModelArtifacts(const std::filesystem::path& source,
                                    const std::filesystem::path& artifact)
{
    return HasRequiredSdfVoxelSidecars(source, artifact) &&
        GltfArtifactDependenciesPresent(source, artifact);
}
}

bool AssetImportService::OpenProject(const std::filesystem::path& projectRoot,
                                     std::string* error) {
    m_ProjectRoot = std::filesystem::absolute(projectRoot).lexically_normal();
    std::filesystem::create_directories(m_ProjectRoot / "SourceAssets");
    std::filesystem::create_directories(m_ProjectRoot / "Library/windows-x64");
    if (m_Importers.empty()) {
        RegisterImporter(CreateModelSdfVoxelAssetImporter());
        RegisterImporter(CreatePassthroughAssetImporter());
    }
    if (!m_Database.Open(m_ProjectRoot / ".myengine/AssetDatabase.json", error)) return false;
    RefreshValidation();
    return true;
}

void AssetImportService::RegisterImporter(std::unique_ptr<IAssetImporter> importer) {
    if (importer) m_Importers.push_back(std::move(importer));
}

const IAssetImporter* AssetImportService::FindImporter(const std::filesystem::path& path) const {
    for (const auto& importer : m_Importers) if (importer->Supports(path)) return importer.get();
    return nullptr;
}

std::string AssetImportService::BuildCacheKey(const IAssetImporter& importer,
                                               const std::filesystem::path& source,
                                               const std::string& settingsJson,
                                               std::string* error) const {
    std::string hashError;
    const std::string sourceHash = Sha256::HashFile(source, &hashError);
    if (!hashError.empty()) { SetError(error, hashError); return {}; }
    Sha256 hash;
    std::string contract = sourceHash + "|" + settingsJson + "|" + importer.GetName() +
        "|" + std::to_string(importer.GetVersion()) + "|" + RuntimeCompatibility::kBuildId +
        "|windows-x64";
    if (std::string(importer.GetName()) == "model-sdf-voxel") {
        contract += "|sdfVoxel=";
        contract += m_SdfVoxelBakingEnabled ? "1" : "0";
    }
    hash.Update(contract.data(), contract.size());
    return Sha256::ToHex(hash.Final());
}

AssetImportReport AssetImportService::Import(const std::filesystem::path& externalSource,
                                              const std::string& settingsJson,
                                              std::string* error) {
    if (!std::filesystem::is_regular_file(externalSource)) {
        SetError(error, "import source is missing"); return {};
    }
    auto destination = m_ProjectRoot / "SourceAssets" / externalSource.filename();
    int suffix = 1;
    while (std::filesystem::exists(destination))
        destination = m_ProjectRoot / "SourceAssets" /
            (externalSource.stem().string() + "_" + std::to_string(suffix++) + externalSource.extension().string());
    std::error_code ec;
    std::filesystem::copy_file(externalSource, destination, ec);
    if (ec) { SetError(error, ec.message()); return {}; }
    return ImportSource(destination, settingsJson, {}, false, error);
}

AssetImportReport AssetImportService::ImportSource(const std::filesystem::path& source,
                                                    const std::string& settingsJson,
                                                    const std::string& existingUuid,
                                                    bool forceImport,
                                                    std::string* error) {
    AssetImportReport report;
    const IAssetImporter* importer = FindImporter(source);
    if (!importer) { SetError(error, "no importer supports source"); return report; }
    AssetMeta meta;
    if (!existingUuid.empty()) {
        auto loaded = AssetMeta::Load(source.string(), error);
        if (!loaded || loaded->uuid != existingUuid) return report;
        meta = *loaded;
    } else {
        meta = AssetMeta::Create(source.string());
        meta.importerVersion = importer->GetVersion();
        if (!AssetMeta::Save(meta)) { SetError(error, "failed to save metadata"); return report; }
    }
    const std::string cacheKey = BuildCacheKey(*importer, source, settingsJson, error);
    if (cacheKey.empty()) return report;
    const auto artifact = m_ProjectRoot / "Library/windows-x64" / meta.uuid /
        (cacheKey + source.extension().string());
    AssetRecord previous;
    const AssetRecord* existing = m_Database.FindByUuid(meta.uuid);
    if (existing) previous = *existing;
    if (!forceImport && existing && existing->sourceHash == cacheKey &&
        std::filesystem::is_regular_file(existing->artifactPath)) {
        report = {true, true, *existing};
        RefreshValidation();
        return report;
    }
    ImportRequest request{source, artifact, meta.uuid, settingsJson, "windows-x64",
                          m_SdfVoxelBakingEnabled};
    ImportResult result = importer->Import(request);
    AssetRecord record = existing ? *existing : AssetRecord{};
    record.uuid = meta.uuid;
    record.sourcePath = source.generic_string();
    record.artifactPath = artifact.generic_string();
    record.importer = importer->GetName();
    record.importerVersion = importer->GetVersion();
    record.settingsJson = settingsJson;
    record.sourceHash = cacheKey;
    record.dependencies = std::move(result.dependencies);
    record.diagnostics = std::move(result.diagnostics);
    record.type = std::move(result.type);
    record.state = result.succeeded ? AssetImportState::Ready : AssetImportState::Failed;
    if (result.succeeded) {
        std::string hashError;
        record.artifactHash = Sha256::HashFile(artifact, &hashError);
        if (!hashError.empty()) { SetError(error, hashError); return report; }
    } else if (existing) {
        record.artifactPath = previous.artifactPath;
        record.artifactHash = previous.artifactHash;
        if (record.diagnostics.empty()) {
            record.diagnostics.push_back({"error", "import failed; retained previous artifact"});
        }
    }
    if (!m_Database.Upsert(record, error) || !m_Database.Save(error)) return report;
    RefreshValidation();
    if (result.succeeded)
        AssetManager::Get().RegisterPersistentIdentity(record.artifactPath, record.uuid);
    report.succeeded = result.succeeded;
    report.record = std::move(record);
    return report;
}

AssetImportReport AssetImportService::Reimport(const std::string& uuid, std::string* error) {
    const AssetRecord* record = m_Database.FindByUuid(uuid);
    if (!record) { SetError(error, "asset uuid is not registered"); return {}; }
    if (!std::filesystem::is_regular_file(record->sourcePath)) {
        AssetRecord missing = *record;
        missing.state = AssetImportState::MissingSource;
        missing.diagnostics.push_back({"error", "asset source is missing"});
        m_Database.Upsert(missing);
        m_Database.Save();
        RefreshValidation();
        SetError(error, "asset source is missing");
        return {false, false, missing};
    }
    return ImportSource(record->sourcePath, record->settingsJson, uuid, false, error);
}

AssetImportReport AssetImportService::ReimportWithSettings(
    const std::string& uuid, const std::string& settingsJson, std::string* error) {
    const AssetRecord* record = m_Database.FindByUuid(uuid);
    if (!record) { SetError(error, "asset uuid is not registered"); return {}; }
    if (!std::filesystem::is_regular_file(record->sourcePath)) {
        AssetRecord missing = *record;
        missing.state = AssetImportState::MissingSource;
        missing.settingsJson = settingsJson;
        missing.diagnostics.push_back({"error", "asset source is missing"});
        m_Database.Upsert(missing);
        m_Database.Save();
        RefreshValidation();
        SetError(error, "asset source is missing");
        return {false, false, missing};
    }
    return ImportSource(record->sourcePath, settingsJson, uuid, false, error);
}

size_t AssetImportService::ReimportAll(std::vector<std::string>* failures) {
    size_t succeeded = 0;
    const auto records = m_Database.GetAll();
    for (const auto& record : records) {
        std::string error;
        if (Reimport(record.uuid, &error).succeeded) ++succeeded;
        else if (failures) failures->push_back(record.uuid + ": " + error);
    }
    RefreshValidation();
    return succeeded;
}

size_t AssetImportService::BakeSdfVoxelForImportedModels(std::vector<std::string>* failures,
                                                         bool forceRebake) {
    const bool previousSdfVoxelBaking = m_SdfVoxelBakingEnabled;
    m_SdfVoxelBakingEnabled = true;
    size_t succeeded = 0;
    std::unordered_set<std::string> visitedSources;
    auto bakeSource = [&](const std::filesystem::path& source,
                          const std::string& settingsJson,
                          const std::string& uuid,
                          const std::filesystem::path& artifact,
                          const std::string& label) {
        if (!std::filesystem::is_regular_file(source)) {
            if (failures) failures->push_back(label + ": asset source is missing");
            return;
        }
        const std::string key = SourceKey(source);
        if (!visitedSources.insert(key).second) return;
        if (!forceRebake && HasRequiredBakedModelArtifacts(source, artifact)) return;
        std::string error;
        AssetImportReport report =
            ImportSource(source, settingsJson.empty() ? "{}" : settingsJson, uuid, true, &error);
        if (report.succeeded) {
            ++succeeded;
            MirrorBakedSidecarNextToSource(report.record);
        } else if (failures) {
            failures->push_back(label + ": " + error);
        }
    };

    const auto records = m_Database.GetAll();
    for (const auto& record : records) {
        if (record.type != "model" && !IsModelSourcePath(record.sourcePath)) continue;
        bakeSource(record.sourcePath, record.settingsJson, record.uuid,
                   record.artifactPath, record.uuid);
    }

    const std::filesystem::path scanRoots[] = {
        m_ProjectRoot / "SourceAssets",
        m_ProjectRoot / "Content"
    };
    for (const std::filesystem::path& root : scanRoots) {
        if (!std::filesystem::exists(root)) continue;
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec)) continue;
            const std::filesystem::path source = it->path();
            if (!IsModelSourcePath(source)) continue;
            bakeSource(source, "{}", ExistingSourceUuid(source), {},
                       source.generic_string());
        }
    }

    m_SdfVoxelBakingEnabled = previousSdfVoxelBaking;
    RefreshValidation();
    return succeeded;
}

bool AssetImportService::RefreshValidation(std::string* error) {
    const bool passed = m_Database.ValidateAgainstProject(m_ProjectRoot, m_ValidationReport);
    if (!passed && error) *error = m_ValidationReport.Summary();
    return passed;
}
