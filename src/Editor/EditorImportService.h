#pragma once

#include "Editor/EditorService.h"

#include <filesystem>
#include <string>
#include <memory>

class AssetImportService;

class EditorImportService final : public EditorService {
public:
    EditorImportService();
    ~EditorImportService() override;
    void OnAttach(EditorContext& context) override;
    bool Import(const std::string& sourcePath);
    static std::filesystem::path MakeUniqueContentPath(const std::filesystem::path& directory,
        const std::string& stem, const std::string& extension);
private:
    std::unique_ptr<AssetImportService> m_ImportPipeline;
};
