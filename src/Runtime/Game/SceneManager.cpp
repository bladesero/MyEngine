#include "Game/SceneManager.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"
#include "Renderer/GpuUploadQueue.h"
#include <filesystem>
#include <fstream>
#include <sstream>

bool SceneManager::IsSafeScenePath(const std::string& value)
{
    std::filesystem::path path(value);
    if(value.empty()||path.is_absolute()||path.has_root_name()||path.has_root_directory())return false;
    auto it=path.begin();if(it==path.end()||*it!="Content")return false;
    for(const auto& part:path)if(part==".."||part==".")return false;
    const std::string name=path.generic_string();
    return name.size()>=11&&name.rfind(".scene.json")==name.size()-11;
}

SceneLoadRequestID SceneManager::RequestLoad(std::string path, SceneLoadOptions options)
{
    if(!IsSafeScenePath(path)){
        m_LastError="scene path must be project-relative Content/*.scene.json";
        m_State=SceneLoadState::Failed; return 0;
    }
    if(m_WorkerTask.valid()) m_StaleTasks.push_back(std::move(m_WorkerTask));
    if(m_PreloadStarted)for(const std::string& dependency:m_Plan.assetDependencies)AssetManager::Get().Unpin(dependency);
    ++m_RequestID;
    m_RequestedPath=std::filesystem::path(path).generic_string();
    m_Options=options; m_LastError.clear(); m_Progress=0.0f;
    GpuUploadBudget uploadBudget;
    uploadBudget.maxBytes=options.gpuUploadBudgetBytes;
    uploadBudget.maxMilliseconds=options.gpuUploadBudgetMs;
    GpuUploadQueue::Get().SetDefaultBudget(uploadBudget);
    m_Plan={}; m_Instantiation={}; m_Candidate.reset(); m_PreloadTasks.clear(); m_PreloadStarted=false;
    m_State=SceneLoadState::Requested;
    return m_RequestID;
}

void SceneManager::CancelLoad()
{
    if (m_State==SceneLoadState::Ready||m_State==SceneLoadState::Failed||m_State==SceneLoadState::Idle) return;
    if(m_WorkerTask.valid()) m_StaleTasks.push_back(std::move(m_WorkerTask));
    if(m_PreloadStarted)for(const std::string& dependency:m_Plan.assetDependencies)AssetManager::Get().Unpin(dependency);
    ++m_RequestID; m_Candidate.reset(); m_Plan={}; m_Instantiation={}; m_PreloadTasks.clear();m_PreloadStarted=false;
    m_State=SceneLoadState::Cancelled; m_Progress=0.0f;
}

void SceneManager::ReapStaleTasks()
{
    for(auto it=m_StaleTasks.begin();it!=m_StaleTasks.end();){
        if(it->valid()&&it->wait_for(std::chrono::seconds(0))==std::future_status::ready){it->get();it=m_StaleTasks.erase(it);}else ++it;
    }
}

bool SceneManager::Process(std::unique_ptr<Scene>& loadedScene)
{
    loadedScene.reset(); ReapStaleTasks();
    if(m_State==SceneLoadState::Requested){
        const auto absolute=AssetManager::Get().GetProjectRoot()/m_RequestedPath;
        const SceneLoadRequestID id=m_RequestID;
        m_WorkerTask=std::async(std::launch::async,[absolute,id]{
            WorkerResult result; result.requestID=id;
            std::ifstream input(absolute,std::ios::binary);
            if(!input){result.error="cannot open scene file";return result;}
            std::ostringstream stream;stream<<input.rdbuf();
            if(!input.good()&&!input.eof()){result.error="failed while reading scene file";return result;}
            result.success=SceneSerializer::BuildLoadPlan(stream.str(),result.plan,&result.error);
            return result;
        });
        m_State=SceneLoadState::Reading; m_Progress=0.05f; return false;
    }
    if(m_State==SceneLoadState::Reading){
        if(!m_WorkerTask.valid()||m_WorkerTask.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
        WorkerResult result=m_WorkerTask.get();
        if(result.requestID!=m_RequestID)return false;
        if(!result.success){m_LastError="failed to prepare scene '"+m_RequestedPath+"': "+result.error;m_State=SceneLoadState::Failed;return false;}
        m_State=SceneLoadState::Parsing; m_Plan=std::move(result.plan); m_Progress=0.25f; return false;
    }
    if(m_State==SceneLoadState::Parsing){m_State=SceneLoadState::Preloading;m_Progress=0.35f;return false;}
    if(m_State==SceneLoadState::Preloading){
        if(!m_PreloadStarted){
            for(const std::string& path:m_Plan.assetDependencies) AssetManager::Get().Pin(path);
            const auto paths=m_Plan.root.value("preloadAssets",std::vector<std::string>{});
            for(const std::string& path:paths)m_PreloadTasks.push_back(AssetManager::Get().RequestAsync(path));
            m_PreloadStarted=true;
        }
        size_t ready=0;
        for(auto& task:m_PreloadTasks)if(task.wait_for(std::chrono::seconds(0))==std::future_status::ready)++ready;
        if(ready<m_PreloadTasks.size()){
            m_Progress=0.35f+0.05f*static_cast<float>(ready)/static_cast<float>(std::max<size_t>(1,m_PreloadTasks.size()));return false;
        }
        if(m_Options.waitForCriticalAssets)for(auto& task:m_PreloadTasks)if(!task.get()){
            m_LastError="failed to preload a critical scene asset";
            for(const std::string& path:m_Plan.assetDependencies)AssetManager::Get().Unpin(path);
            m_State=SceneLoadState::Failed;return false;
        }
        m_Candidate=std::make_unique<Scene>();m_Instantiation={};
        m_State=SceneLoadState::Instantiating;m_Progress=0.4f;return false;
    }
    if(m_State!=SceneLoadState::Instantiating||!m_Candidate)return false;
    const auto start=std::chrono::steady_clock::now(); bool complete=false;
    do {
        if(!SceneSerializer::InstantiateLoadPlan(*m_Candidate,m_Plan,m_Instantiation,1,complete,&m_LastError)){
            for(const std::string& path:m_Plan.assetDependencies) AssetManager::Get().Unpin(path);
            m_Candidate.reset();m_State=SceneLoadState::Failed;return false;
        }
        const size_t total=std::max<size_t>(1,m_Plan.actors.size());
        m_Progress=0.4f+0.55f*static_cast<float>(m_Instantiation.nextActor)/static_cast<float>(total);
    } while(!complete&&std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<m_Options.instantiateBudgetMs);
    if(!complete)return false;
    for(const std::string& path:m_Plan.assetDependencies) AssetManager::Get().Unpin(path);
    loadedScene=std::move(m_Candidate);m_State=SceneLoadState::Ready;m_Progress=1.0f;return true;
}

void SceneManager::SetPersistentValue(const std::string& key,nlohmann::json value){if(!key.empty())m_PersistentData[key]=std::move(value);}
nlohmann::json SceneManager::GetPersistentValue(const std::string& key,const nlohmann::json& fallback)const{const auto it=m_PersistentData.find(key);return it==m_PersistentData.end()?fallback:*it;}
