#pragma once
#include <future>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
class Scene;
enum class SceneLoadState { Idle, Requested, Loading, Ready, Failed };
class SceneManager {
public:
    bool RequestLoad(std::string projectRelativePath);bool Process(std::unique_ptr<Scene>& loadedScene);
    SceneLoadState GetState()const{return m_State;}const std::string& GetRequestedPath()const{return m_RequestedPath;}const std::string& GetLastError()const{return m_LastError;}
    void SetPersistentValue(const std::string& key,nlohmann::json value);nlohmann::json GetPersistentValue(const std::string& key,const nlohmann::json& fallback={})const;const nlohmann::json& GetPersistentData()const{return m_PersistentData;}void ClearPersistentData(){m_PersistentData=nlohmann::json::object();}
private:
    struct ReadResult { bool success=false;std::string source;std::string error; };
    static bool IsSafeScenePath(const std::string& path);
    SceneLoadState m_State=SceneLoadState::Idle;
    std::string m_RequestedPath,m_LastError;
    nlohmann::json m_PersistentData=nlohmann::json::object();
    std::future<ReadResult> m_ReadTask;
};
