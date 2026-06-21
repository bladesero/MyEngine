#pragma once

#include "Editor/EditorService.h"

#include <filesystem>
#include <string>

class EditorLuaScriptService final : public EditorService {
public:
    ~EditorLuaScriptService() override;

    bool RunSource(const std::string& source, const std::string& chunkName,
                   std::string* error = nullptr);
    bool RunFile(const std::filesystem::path& path, std::string* error = nullptr);
};
