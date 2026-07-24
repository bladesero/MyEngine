#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"
#include "Assets/Asset.h"
#include "Assets/NavMeshData.h"

#include <cstdint>
#include <vector>

class NavMeshAsset final : public Asset {
public:
    explicit NavMeshAsset(const std::string& path) : Asset(AssetType::NavMesh, path) {}
    void SetBakedData(const NavMeshBakeSettings& settings, uint32_t width, uint32_t height,
                      std::vector<uint8_t> cells) {
        m_Settings = settings;
        m_Width = width;
        m_Height = height;
        m_Cells = std::move(cells);
        SetState(AssetState::Ready);
    }
    const NavMeshBakeSettings& GetSettings() const { return m_Settings; }
    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }
    const std::vector<uint8_t>& GetCells() const { return m_Cells; }

private:
    NavMeshBakeSettings m_Settings;
    uint32_t m_Width = 0, m_Height = 0;
    std::vector<uint8_t> m_Cells;
    friend MYENGINE_RUNTIME_API std::shared_ptr<NavMeshAsset>
    LoadNavMeshAssetFromFile(const std::string&);
    friend MYENGINE_RUNTIME_API bool SaveNavMeshAssetToFile(const NavMeshAsset&,
                                                            const std::string&);
};
using NavMeshHandle = AssetHandle<NavMeshAsset>;
MYENGINE_RUNTIME_API std::shared_ptr<NavMeshAsset> LoadNavMeshAssetFromFile(const std::string& path);
MYENGINE_RUNTIME_API bool SaveNavMeshAssetToFile(const NavMeshAsset& asset, const std::string& path);
