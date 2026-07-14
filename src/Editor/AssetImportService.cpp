#include "Editor/AssetImportService.h"

#include "Assets/AssetMeta.h"
#include "Assets/AssetManager.h"
#include "Core/Sha256.h"
#include "Project/RuntimeCompatibility.h"
#include "Renderer/ShaderCooker.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace {
void SetError(std::string* error, std::string value) {
    if (error)
        *error = std::move(value);
}

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

bool StartsWithDataUri(const std::string& uri) {
    return uri.size() >= 5 && std::tolower(static_cast<unsigned char>(uri[0])) == 'd' &&
           std::tolower(static_cast<unsigned char>(uri[1])) == 'a' &&
           std::tolower(static_cast<unsigned char>(uri[2])) == 't' &&
           std::tolower(static_cast<unsigned char>(uri[3])) == 'a' && uri[4] == ':';
}

std::string DecodeUriPath(std::string_view uri) {
    std::string decoded;
    decoded.reserve(uri.size());
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            unsigned int value = 0;
            std::istringstream stream(std::string(uri.substr(i + 1, 2)));
            stream >> std::hex >> value;
            if (!stream.fail()) {
                decoded.push_back(static_cast<char>(value));
                i += 2;
                continue;
            }
        }
        decoded.push_back(uri[i] == '/' ? std::filesystem::path::preferred_separator : uri[i]);
    }
    return decoded;
}

std::vector<std::filesystem::path> CollectGltfExternalDependencies(const std::filesystem::path& source) {
    std::vector<std::filesystem::path> dependencies;
    if (LowerExtension(source) != ".gltf")
        return dependencies;

    try {
        std::ifstream input(source);
        nlohmann::json gltf;
        input >> gltf;
        auto appendUri = [&](const nlohmann::json& value) {
            if (!value.is_object() || !value.contains("uri") || !value["uri"].is_string()) {
                return;
            }
            const std::string uri = value["uri"].get<std::string>();
            if (uri.empty() || StartsWithDataUri(uri))
                return;
            dependencies.push_back((source.parent_path() / DecodeUriPath(uri)).lexically_normal());
        };
        for (const auto& buffer : gltf.value("buffers", nlohmann::json::array())) {
            appendUri(buffer);
        }
        for (const auto& image : gltf.value("images", nlohmann::json::array())) {
            appendUri(image);
        }
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    } catch (...) {
        dependencies.clear();
    }
    return dependencies;
}

std::filesystem::path FindShaderContentRoot(std::filesystem::path sourcePath) {
    std::error_code error;
    sourcePath = std::filesystem::absolute(std::move(sourcePath), error).lexically_normal();
    std::filesystem::path cursor = sourcePath.parent_path();
    while (!cursor.empty()) {
        const std::string name = cursor.filename().string();
        if (name == "Content" || name == "EngineContent")
            return cursor;
        const auto parent = cursor.parent_path();
        if (parent == cursor)
            break;
        cursor = parent;
    }
    return sourcePath.parent_path();
}

std::string StableVirtualShaderUuid(const std::filesystem::path& source) {
    Sha256 hash;
    const std::string key = "engine-shader:" + std::filesystem::absolute(source).lexically_normal().generic_string();
    hash.Update(key.data(), key.size());
    return "engine-shader-" + Sha256::ToHex(hash.Final()).substr(0, 32);
}
} // namespace

bool AssetImportService::OpenProject(const std::filesystem::path& projectRoot, std::string* error) {
    m_ProjectRoot = std::filesystem::absolute(projectRoot).lexically_normal();
    std::filesystem::create_directories(m_ProjectRoot / "SourceAssets");
    std::filesystem::create_directories(m_ProjectRoot / "Library/windows-x64");
    if (m_Importers.empty()) {
        RegisterImporter(CreateGltfModelAssetImporter());
        RegisterImporter(CreateShaderAssetImporter());
        RegisterImporter(CreatePassthroughAssetImporter());
    }
    if (!m_Database.Open(m_ProjectRoot / ".myengine/AssetDatabase.json", error))
        return false;
    RefreshValidation();
    return true;
}

void AssetImportService::RegisterImporter(std::unique_ptr<IAssetImporter> importer) {
    if (importer)
        m_Importers.push_back(std::move(importer));
}

const IAssetImporter* AssetImportService::FindImporter(const std::filesystem::path& path) const {
    for (const auto& importer : m_Importers)
        if (importer->Supports(path))
            return importer.get();
    return nullptr;
}

std::string AssetImportService::BuildCacheKey(const IAssetImporter& importer, const std::filesystem::path& source,
                                              const std::string& settingsJson, std::string* error) const {
    std::string hashError;
    const std::string sourceHash = Sha256::HashFile(source, &hashError);
    if (!hashError.empty()) {
        SetError(error, hashError);
        return {};
    }
    Sha256 hash;
    std::string dependencyContract;
    if (std::string(importer.GetName()) == "shader") {
        const std::vector<ShaderBackend> backends = ShaderCooker::BackendsForTargetPlatform("windows-x64");
        std::vector<std::string> dependencies;
        const std::string shaderKey = ShaderCooker::BuildCacheKey(source, FindShaderContentRoot(source), backends,
                                                                  "windows-x64", settingsJson, &dependencies, error);
        if (shaderKey.empty())
            return {};
        dependencyContract += "|shader:";
        dependencyContract += shaderKey;
        for (const auto& dependency : dependencies) {
            dependencyContract += "|dep:";
            dependencyContract += dependency;
        }
    }
    for (const auto& dependency : CollectGltfExternalDependencies(source)) {
        dependencyContract += "|dep:";
        dependencyContract += dependency.generic_string();
        dependencyContract += "=";
        if (std::filesystem::is_regular_file(dependency)) {
            const std::string dependencyHash = Sha256::HashFile(dependency, &hashError);
            if (!hashError.empty()) {
                SetError(error, hashError);
                return {};
            }
            dependencyContract += dependencyHash;
        } else {
            dependencyContract += "missing";
        }
    }
    const std::string contract = sourceHash + dependencyContract + "|" + settingsJson + "|" + importer.GetName() + "|" +
                                 std::to_string(importer.GetVersion()) + "|" + RuntimeCompatibility::kBuildId +
                                 "|windows-x64";
    hash.Update(contract.data(), contract.size());
    return Sha256::ToHex(hash.Final());
}

AssetImportReport AssetImportService::Import(const std::filesystem::path& externalSource,
                                             const std::string& settingsJson, std::string* error) {
    if (!std::filesystem::is_regular_file(externalSource)) {
        SetError(error, "import source is missing");
        return {};
    }
    auto destination = m_ProjectRoot / "SourceAssets" / externalSource.filename();
    int suffix = 1;
    while (std::filesystem::exists(destination))
        destination =
            m_ProjectRoot / "SourceAssets" /
            (externalSource.stem().string() + "_" + std::to_string(suffix++) + externalSource.extension().string());
    std::error_code ec;
    std::filesystem::copy_file(externalSource, destination, ec);
    if (ec) {
        SetError(error, ec.message());
        return {};
    }
    return ImportSource(destination, settingsJson, {}, error);
}

AssetImportReport AssetImportService::ImportSource(const std::filesystem::path& source, const std::string& settingsJson,
                                                   const std::string& existingUuid, std::string* error) {
    return ImportSourceInternal(source, settingsJson, existingUuid, {}, true, error);
}

AssetImportReport AssetImportService::ImportEngineShaderSource(const std::filesystem::path& source,
                                                               const std::string& settingsJson, std::string* error) {
    return ImportSourceInternal(source, settingsJson, {}, StableVirtualShaderUuid(source), false, error);
}

AssetImportReport AssetImportService::ImportSourceInternal(const std::filesystem::path& source,
                                                           const std::string& settingsJson,
                                                           const std::string& existingUuid,
                                                           const std::string& virtualUuid, bool writeMeta,
                                                           std::string* error) {
    AssetImportReport report;
    const IAssetImporter* importer = FindImporter(source);
    if (!importer) {
        SetError(error, "no importer supports source");
        return report;
    }
    AssetMeta meta;
    if (!virtualUuid.empty()) {
        meta.uuid = virtualUuid;
        meta.importerVersion = importer->GetVersion();
    } else if (!existingUuid.empty()) {
        auto loaded = AssetMeta::Load(source.string(), error);
        if (!loaded || loaded->uuid != existingUuid)
            return report;
        meta = *loaded;
    } else {
        meta = AssetMeta::Create(source.string());
        meta.importerVersion = importer->GetVersion();
    }
    const std::string cacheKey = BuildCacheKey(*importer, source, settingsJson, error);
    if (cacheKey.empty())
        return report;
    const auto artifact =
        m_ProjectRoot / "Library/windows-x64" / meta.uuid / (cacheKey + importer->GetArtifactExtension(source));
    AssetRecord previous;
    const AssetRecord* existing = m_Database.FindByUuid(meta.uuid);
    if (existing)
        previous = *existing;
    if (existing && existing->sourceHash == cacheKey && std::filesystem::is_regular_file(existing->artifactPath)) {
        report = {true, true, *existing};
        RefreshValidation();
        return report;
    }
    const auto stagingArtifact = std::filesystem::path(artifact.string() + ".import-staging");
    std::error_code fileError;
    std::filesystem::remove(stagingArtifact, fileError);
    ImportRequest request{source, stagingArtifact, meta.uuid, settingsJson, "windows-x64"};
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
    record.state = AssetImportState::Ready;
    if (!result.succeeded || !std::filesystem::is_regular_file(stagingArtifact)) {
        std::filesystem::remove(stagingArtifact, fileError);
        SetError(error, "import failed; retained previous ready artifact and record");
        report.record = existing ? previous : record;
        return report;
    }
    std::string hashError;
    record.artifactHash = Sha256::HashFile(stagingArtifact, &hashError);
    if (!hashError.empty() || record.artifactHash.empty()) {
        std::filesystem::remove(stagingArtifact, fileError);
        SetError(error, hashError.empty() ? "staged artifact hash is empty" : hashError);
        return report;
    }
    for (const auto& dependency : record.dependencies) {
        const bool shaderSource = record.type == "shader" && std::filesystem::is_regular_file(dependency);
        if (!dependency.empty() && !shaderSource && !m_Database.FindByUuid(dependency)) {
            std::filesystem::remove(stagingArtifact, fileError);
            SetError(error, "staged artifact has unresolved dependency: " + dependency);
            return report;
        }
    }
    if (m_InjectedFault == AssetImportFault::AfterArtifactValidation) {
        std::filesystem::remove(stagingArtifact, fileError);
        SetError(error, "injected import failure after artifact validation");
        return report;
    }

    std::filesystem::create_directories(artifact.parent_path(), fileError);
    const auto artifactBackup = std::filesystem::path(artifact.string() + ".import-backup");
    std::filesystem::remove(artifactBackup, fileError);
    const bool replacedArtifact = std::filesystem::is_regular_file(artifact);
    if (replacedArtifact) {
        std::filesystem::rename(artifact, artifactBackup, fileError);
        if (fileError) {
            SetError(error, fileError.message());
            return report;
        }
    }
    std::filesystem::rename(stagingArtifact, artifact, fileError);
    if (fileError) {
        if (replacedArtifact) {
            std::error_code ignored;
            std::filesystem::rename(artifactBackup, artifact, ignored);
        }
        SetError(error, "failed to promote staged artifact: " + fileError.message());
        return report;
    }
    // Artifact promotion happens before database/meta persistence. Any later failure must restore both the previous
    // artifact and registry record so readers never observe a mixed import generation.
    auto rollback = [&]() {
        std::error_code ignored;
        std::filesystem::remove(artifact, ignored);
        if (replacedArtifact)
            std::filesystem::rename(artifactBackup, artifact, ignored);
        if (existing)
            m_Database.Upsert(previous);
        else
            m_Database.Remove(meta.uuid);
        m_Database.Save();
    };
    if (m_InjectedFault == AssetImportFault::AfterArtifactPromote) {
        rollback();
        SetError(error, "injected import failure after artifact promote");
        return report;
    }
    if (!m_Database.Upsert(record, error)) {
        rollback();
        return report;
    }
    if (m_InjectedFault == AssetImportFault::BeforeDatabaseSave || !m_Database.Save(error)) {
        rollback();
        if (m_InjectedFault == AssetImportFault::BeforeDatabaseSave)
            SetError(error, "injected import failure before database save");
        return report;
    }
    if (writeMeta && !AssetMeta::Save(meta)) {
        rollback();
        SetError(error, "failed to save metadata");
        return report;
    }
    std::filesystem::remove(artifactBackup, fileError);
    RefreshValidation();
    if (result.succeeded)
        AssetManager::Get().RegisterPersistentIdentity(record.artifactPath, record.uuid);
    report.succeeded = result.succeeded;
    report.record = std::move(record);
    return report;
}

AssetImportReport AssetImportService::Reimport(const std::string& uuid, std::string* error) {
    const AssetRecord* record = m_Database.FindByUuid(uuid);
    if (!record) {
        SetError(error, "asset uuid is not registered");
        return {};
    }
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
    return ImportSource(record->sourcePath, record->settingsJson, uuid, error);
}

AssetImportReport AssetImportService::ReimportWithSettings(const std::string& uuid, const std::string& settingsJson,
                                                           std::string* error) {
    const AssetRecord* record = m_Database.FindByUuid(uuid);
    if (!record) {
        SetError(error, "asset uuid is not registered");
        return {};
    }
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
    return ImportSource(record->sourcePath, settingsJson, uuid, error);
}

size_t AssetImportService::ReimportAll(std::vector<std::string>* failures) {
    size_t succeeded = 0;
    const auto records = m_Database.GetAll();
    for (const auto& record : records) {
        std::string error;
        if (Reimport(record.uuid, &error).succeeded)
            ++succeeded;
        else if (failures)
            failures->push_back(record.uuid + ": " + error);
    }
    RefreshValidation();
    return succeeded;
}

bool AssetImportService::RefreshValidation(std::string* error) {
    const bool passed = m_Database.ValidateAgainstProject(m_ProjectRoot, m_ValidationReport);
    if (!passed && error)
        *error = m_ValidationReport.Summary();
    return passed;
}
