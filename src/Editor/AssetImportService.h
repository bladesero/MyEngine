#pragma once

#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporter.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct AssetImportReport {
    bool succeeded = false;
    bool cacheHit = false;
    AssetRecord record;
};

enum class AssetImportFault {
    None,
    AfterArtifactValidation,
    AfterArtifactPromote,
    BeforeDatabaseSave,
};

class AssetImportService {
public:
    bool OpenProject(const std::filesystem::path& projectRoot, std::string* error = nullptr);
    void RegisterImporter(std::unique_ptr<IAssetImporter> importer);
    AssetImportReport Import(const std::filesystem::path& externalSource, const std::string& settingsJson = "{}",
                             std::string* error = nullptr);
    AssetImportReport ImportSource(const std::filesystem::path& source, const std::string& settingsJson = "{}",
                                   const std::string& existingUuid = {}, std::string* error = nullptr);
    AssetImportReport ImportEngineShaderSource(const std::filesystem::path& source,
                                               const std::string& settingsJson = "{}", std::string* error = nullptr);
    AssetImportReport Reimport(const std::string& uuid, std::string* error = nullptr);
    AssetImportReport ReimportWithSettings(const std::string& uuid, const std::string& settingsJson,
                                           std::string* error = nullptr);
    size_t ReimportAll(std::vector<std::string>* failures = nullptr);

    AssetDatabase& GetDatabase() { return m_Database; }
    const AssetDatabase& GetDatabase() const { return m_Database; }
    const AssetDatabaseValidationReport& GetValidationReport() const { return m_ValidationReport; }
    bool RefreshValidation(std::string* error = nullptr);

    void SetInjectedFaultForTesting(AssetImportFault fault) { m_InjectedFault = fault; }

private:
    const IAssetImporter* FindImporter(const std::filesystem::path& path) const;
    std::string BuildCacheKey(const IAssetImporter& importer, const std::filesystem::path& source,
                              const std::string& settingsJson, std::string* error) const;

    std::filesystem::path m_ProjectRoot;
    AssetDatabase m_Database;
    AssetDatabaseValidationReport m_ValidationReport;
    std::vector<std::unique_ptr<IAssetImporter>> m_Importers;
    AssetImportFault m_InjectedFault = AssetImportFault::None;

    AssetImportReport ImportSourceInternal(const std::filesystem::path& source, const std::string& settingsJson,
                                           const std::string& existingUuid, const std::string& virtualUuid,
                                           bool writeMeta, std::string* error);
};
