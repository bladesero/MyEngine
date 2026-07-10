#pragma once
#include "Assets/Asset.h"
#include "Navigation/NavigationWorld.h"
class NavMeshAsset final:public Asset{
public:explicit NavMeshAsset(const std::string& path):Asset(AssetType::NavMesh,path){}
    void Capture(const NavigationWorld& world);bool Apply(NavigationWorld& world)const;
    const NavigationWorld::BakeSettings& GetSettings()const{return m_Settings;}uint32_t GetWidth()const{return m_Width;}uint32_t GetHeight()const{return m_Height;}const std::vector<uint8_t>& GetCells()const{return m_Cells;}
private:NavigationWorld::BakeSettings m_Settings;uint32_t m_Width=0,m_Height=0;std::vector<uint8_t> m_Cells;
    friend std::shared_ptr<NavMeshAsset> LoadNavMeshAssetFromFile(const std::string&);friend bool SaveNavMeshAssetToFile(const NavMeshAsset&,const std::string&);
};
using NavMeshHandle=AssetHandle<NavMeshAsset>;
std::shared_ptr<NavMeshAsset> LoadNavMeshAssetFromFile(const std::string& path);bool SaveNavMeshAssetToFile(const NavMeshAsset& asset,const std::string& path);
