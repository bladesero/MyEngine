#pragma once

#include "Editor/EditorService.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class EditorShaderWatchService final : public EditorService {
public:
    void OnAttach(EditorContext& context) override;
    void OnUpdate(float deltaSeconds) override;
    void Refresh();
private:
    std::vector<std::string> m_Paths;
    std::unordered_map<std::string,std::filesystem::file_time_type> m_Times;
    float m_Accumulator=0;
};
