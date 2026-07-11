#pragma once

#include "Editor/EditorService.h"

#include <filesystem>
#include <string>
#include <memory>
#include <vector>

class AssetImportService;
struct AssetDatabaseValidationReport;

class EditorImportService final : public EditorService {
public:
    EditorImportService();
    ~EditorImportService() override;
    void OnAttach(EditorContext& context) override;
    bool Import(const std::string& sourcePath);
    bool Reimport(const std::string& uuid);
    bool ReimportWithSettings(const std::string& uuid,
                              const std::string& settingsJson);
    bool EnsureShaderCache(const std::filesystem::path& sourcePath,
                           const std::string& settingsJson,
                           bool allowCompile,
                           std::filesystem::path& outArtifactPath,
                           bool& outCacheHit,
                           std::string* error = nullptr);
    size_t EnsureModelCachesForScene(const std::filesystem::path& scenePath,
                                     std::vector<std::string>* failures = nullptr);
    size_t ReimportAll(std::vector<std::string>* failures = nullptr);
    bool RefreshValidation(std::string* error = nullptr);
    const AssetDatabaseValidationReport* GetValidationReport() const;
    std::string GetValidationSummaryText() const;
    bool HasValidationIssues() const;
    static std::filesystem::path MakeUniqueContentPath(const std::filesystem::path& directory,
        const std::string& stem, const std::string& extension);
private:
    std::unique_ptr<AssetImportService> m_ImportPipeline;
};
