#include "Assets/NavMeshAsset.h"
#include "Core/RuntimeFileSystem.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
void NavMeshAsset::Capture(const NavigationWorld& world) {
    m_Settings = world.GetSettings();
    m_Width = world.GetWidth();
    m_Height = world.GetHeight();
    m_Cells = world.GetCells();
    SetState(AssetState::Ready);
}
bool NavMeshAsset::Apply(NavigationWorld& world) const {
    return world.SetBakedData(m_Settings, m_Width, m_Height, m_Cells);
}
bool SaveNavMeshAssetToFile(const NavMeshAsset& a, const std::string& path) {
    try {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        nlohmann::json runs = nlohmann::json::array();
        for (size_t i = 0; i < a.m_Cells.size();) {
            size_t end = i + 1;
            while (end < a.m_Cells.size() && a.m_Cells[end] == a.m_Cells[i])
                ++end;
            runs.push_back({a.m_Cells[i], end - i});
            i = end;
        }
        nlohmann::json j = {
            {"version", 1},
            {"boundsMin", {a.m_Settings.bounds.min.x, a.m_Settings.bounds.min.y, a.m_Settings.bounds.min.z}},
            {"boundsMax", {a.m_Settings.bounds.max.x, a.m_Settings.bounds.max.y, a.m_Settings.bounds.max.z}},
            {"cellSize", a.m_Settings.cellSize},
            {"agentRadius", a.m_Settings.agentRadius},
            {"width", a.m_Width},
            {"height", a.m_Height},
            {"cellsRle", runs}};
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << j.dump();
        return static_cast<bool>(out);
    } catch (...) {
        return false;
    }
}
std::shared_ptr<NavMeshAsset> LoadNavMeshAssetFromFile(const std::string& path) {
    try {
        std::string text;
        if (!RuntimeFileSystem::Get().ReadText(path, text))
            return {};
        nlohmann::json j = nlohmann::json::parse(text);
        if (j.value("version", 0) != 1)
            return {};
        auto asset = std::make_shared<NavMeshAsset>(path);
        auto min = j.value("boundsMin", nlohmann::json::array()), max = j.value("boundsMax", nlohmann::json::array());
        if (min.size() != 3 || max.size() != 3)
            return {};
        asset->m_Settings.bounds = {{min[0].get<float>(), min[1].get<float>(), min[2].get<float>()},
                                    {max[0].get<float>(), max[1].get<float>(), max[2].get<float>()}};
        asset->m_Settings.cellSize = j.value("cellSize", 0.5f);
        asset->m_Settings.agentRadius = j.value("agentRadius", 0.4f);
        asset->m_Width = j.value("width", 0u);
        asset->m_Height = j.value("height", 0u);
        if (j.contains("cellsRle")) {
            for (const auto& run : j["cellsRle"]) {
                if (!run.is_array() || run.size() != 2)
                    return {};
                const uint8_t value = run[0].get<uint8_t>();
                const size_t count = run[1].get<size_t>();
                if (count > 4000000 || asset->m_Cells.size() + count > 4000000)
                    return {};
                asset->m_Cells.insert(asset->m_Cells.end(), count, value);
            }
        } else
            asset->m_Cells = j.value("cells", std::vector<uint8_t>{});
        if (asset->m_Cells.size() != static_cast<size_t>(asset->m_Width) * asset->m_Height)
            return {};
        asset->SetState(AssetState::Ready);
        return asset;
    } catch (...) {
        return {};
    }
}
