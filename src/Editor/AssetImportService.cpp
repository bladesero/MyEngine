#include "Editor/AssetImportService.h"

#include "Assets/AssetMeta.h"
#include "Assets/AssetManager.h"
#include "Core/Sha256.h"
#include "Project/RuntimeCompatibility.h"

#include <fstream>

namespace {
void SetError(std::string* error, std::string value) { if (error) *error = std::move(value); }
}

bool AssetImportService::OpenProject(const std::filesystem::path& projectRoot,
                                     std::string* error) {
    m_ProjectRoot = std::filesystem::absolute(projectRoot).lexically_normal();
    std::filesystem::create_directories(m_ProjectRoot / "SourceAssets");
    std::filesystem::create_directories(m_ProjectRoot / "Library/windows-x64");
    if (m_Importers.empty()) RegisterImporter(CreatePassthroughAssetImporter());
    return m_Database.Open(m_ProjectRoot / ".myengine/AssetDatabase.json", error);
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
    const std::string contract = sourceHash + "|" + settingsJson + "|" + importer.GetName() +
        "|" + std::to_string(importer.GetVersion()) + "|" + RuntimeCompatibility::kBuildId +
        "|windows-x64";
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
    return ImportSource(destination, settingsJson, {}, error);
}

AssetImportReport AssetImportService::ImportSource(const std::filesystem::path& source,
                                                    const std::string& settingsJson,
                                                    const std::string& existingUuid,
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
    if (existing && existing->sourceHash == cacheKey &&
        std::filesystem::is_regular_file(existing->artifactPath)) {
        report = {true, true, *existing}; return report;
    }
    ImportRequest request{source, artifact, meta.uuid, settingsJson, "windows-x64"};
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
    }
    if (!m_Database.Upsert(record, error) || !m_Database.Save(error)) return report;
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
        m_Database.Upsert(missing);
        m_Database.Save();
        SetError(error, "asset source is missing"); return {};
    }
    return ImportSource(record->sourcePath, record->settingsJson, uuid, error);
}

size_t AssetImportService::ReimportAll(std::vector<std::string>* failures) {
    size_t succeeded = 0;
    const auto records = m_Database.GetAll();
    for (const auto& record : records) {
        std::string error;
        if (Reimport(record.uuid, &error).succeeded) ++succeeded;
        else if (failures) failures->push_back(record.uuid + ": " + error);
    }
    return succeeded;
}
