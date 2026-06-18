#pragma once

#include "Editor/EditorService.h"

#include <filesystem>
#include <string>

class EditorImportService final : public EditorService {
public:
    bool Import(const std::string& sourcePath);
    static std::filesystem::path MakeUniqueContentPath(const std::filesystem::path& directory,
        const std::string& stem, const std::string& extension);
};
