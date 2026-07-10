#include "Game/SceneManager.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
bool SceneManager::IsSafeScenePath(const std::string& value){std::filesystem::path path(value);if(value.empty()||path.is_absolute()||path.has_root_name()||path.has_root_directory())return false;auto it=path.begin();if(it==path.end()||*it!="Content")return false;for(const auto& part:path)if(part==".."||part==".")return false;const std::string name=path.generic_string();return name.size()>=11&&name.rfind(".scene.json")==name.size()-11;}
bool SceneManager::RequestLoad(std::string path){if(!IsSafeScenePath(path)){m_LastError="scene path must be project-relative Content/*.scene.json";m_State=SceneLoadState::Failed;return false;}if(m_State==SceneLoadState::Loading){m_LastError="a scene load is already in progress";return false;}m_RequestedPath=std::filesystem::path(path).generic_string();m_LastError.clear();m_State=SceneLoadState::Requested;return true;}
bool SceneManager::Process(std::unique_ptr<Scene>& loadedScene)
{
    loadedScene.reset();
    if(m_State==SceneLoadState::Requested){
        const std::filesystem::path absolute=AssetManager::Get().GetProjectRoot()/m_RequestedPath;
        m_ReadTask=std::async(std::launch::async,[absolute]{ReadResult result;std::ifstream input(absolute,std::ios::binary);if(!input){result.error="cannot open scene file";return result;}std::ostringstream stream;stream<<input.rdbuf();if(!input.good()&&!input.eof()){result.error="failed while reading scene file";return result;}result.source=stream.str();result.success=true;return result;});
        m_State=SceneLoadState::Loading;
        return false;
    }
    if(m_State!=SceneLoadState::Loading||!m_ReadTask.valid()||m_ReadTask.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
    ReadResult result=m_ReadTask.get();
    if(!result.success){m_LastError="failed to load scene '"+m_RequestedPath+"': "+result.error;m_State=SceneLoadState::Failed;return false;}
    auto candidate=std::make_unique<Scene>();
    if(!SceneSerializer::LoadFromString(*candidate,result.source)){m_LastError="failed to deserialize scene: "+m_RequestedPath;m_State=SceneLoadState::Failed;return false;}
    loadedScene=std::move(candidate);m_State=SceneLoadState::Ready;return true;
}
void SceneManager::SetPersistentValue(const std::string& key,nlohmann::json value){if(!key.empty())m_PersistentData[key]=std::move(value);}nlohmann::json SceneManager::GetPersistentValue(const std::string& key,const nlohmann::json& fallback)const{const auto it=m_PersistentData.find(key);return it==m_PersistentData.end()?fallback:*it;}
