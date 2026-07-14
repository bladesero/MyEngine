#include "Game/SceneManager.h"
#include "Assets/AssetManager.h"
#include "Core/RuntimeFileSystem.h"
#include "Scene/Scene.h"
#include "Renderer/GpuUploadQueue.h"
#include <filesystem>

SceneManager::~SceneManager()
{
    if(m_WorkerTask.Valid())m_WorkerTask.Cancel();
    for(auto& task:m_StaleTasks)task.Cancel();
    ReleasePinnedDependencies();
    m_TaskScope.CancelAndWait();
}

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
    if(m_WorkerTask.Valid()){m_WorkerTask.Cancel();m_StaleTasks.push_back(std::move(m_WorkerTask));}
    ReleasePinnedDependencies();
    ++m_RequestID;
    m_RequestedPath=std::filesystem::path(path).generic_string();
    m_Options=options; m_LastError.clear(); m_Progress=0.0f;
    GpuUploadBudget uploadBudget;
    uploadBudget.maxBytes=options.gpuUploadBudgetBytes;
    uploadBudget.maxMilliseconds=options.gpuUploadBudgetMs;
    GpuUploadQueue::Get().SetDefaultBudget(uploadBudget);
    m_Plan={}; m_Source.clear(); m_Instantiation={}; m_Candidate.reset();
    m_PreloadTasks.clear(); m_PinnedDependencies.clear(); m_PreloadStarted=false; m_UploadFence=0;
    m_State=SceneLoadState::Requested;
    return m_RequestID;
}

void SceneManager::CancelLoad()
{
    if (m_State==SceneLoadState::Ready||m_State==SceneLoadState::Failed||m_State==SceneLoadState::Idle) return;
    if(m_WorkerTask.Valid()){m_WorkerTask.Cancel();m_StaleTasks.push_back(std::move(m_WorkerTask));}
    ReleasePinnedDependencies();
    ++m_RequestID; m_Candidate.reset(); m_Plan={}; m_Source.clear(); m_Instantiation={};
    m_PreloadTasks.clear();m_PinnedDependencies.clear();m_PreloadStarted=false;m_UploadFence=0;
    m_State=SceneLoadState::Cancelled; m_Progress=0.0f;
}

bool SceneManager::RetryLastLoad()
{
    if(m_RequestedPath.empty()||m_State!=SceneLoadState::Failed)return false;
    const std::string path=m_RequestedPath;
    const SceneLoadOptions options=m_Options;
    return RequestLoad(path,options)!=0;
}

void SceneManager::DismissFailure()
{
    if(m_State!=SceneLoadState::Failed)return;
    m_State=SceneLoadState::Idle;
    m_LastError.clear();
    m_Progress=0.0f;
}

bool SceneManager::IsLoading() const
{
    return m_State==SceneLoadState::Requested||m_State==SceneLoadState::Reading||
        m_State==SceneLoadState::Parsing||m_State==SceneLoadState::Preloading||
        m_State==SceneLoadState::Instantiating||m_State==SceneLoadState::Uploading||
        m_State==SceneLoadState::Activating;
}

const char* SceneManager::StageName(SceneLoadState state)
{
    switch(state){
    case SceneLoadState::Idle:return "Idle";
    case SceneLoadState::Requested:return "Preparing";
    case SceneLoadState::Reading:return "Reading scene";
    case SceneLoadState::Parsing:return "Parsing scene";
    case SceneLoadState::Preloading:return "Loading assets";
    case SceneLoadState::Instantiating:return "Building world";
    case SceneLoadState::Uploading:return "Uploading resources";
    case SceneLoadState::Activating:return "Activating world";
    case SceneLoadState::Ready:return "Ready";
    case SceneLoadState::Failed:return "Failed";
    case SceneLoadState::Cancelled:return "Cancelled";
    }
    return "Unknown";
}

void SceneManager::ReleasePinnedDependencies()
{
    if(!m_DependenciesPinned)return;
    for(const std::string& dependency:m_PinnedDependencies)
        AssetManager::Get().Unpin(dependency);
    m_PinnedDependencies.clear();
    m_DependenciesPinned=false;
}

void SceneManager::ReapStaleTasks()
{
    for(auto it=m_StaleTasks.begin();it!=m_StaleTasks.end();){
        if(it->Valid()&&it->IsReady()){
            try{(void)it->Get();}catch(...){}
            it=m_StaleTasks.erase(it);
        }else ++it;
    }
    m_TaskScope.ReapCompleted();
}

bool SceneManager::Process(std::unique_ptr<Scene>& loadedScene)
{
    loadedScene.reset(); ReapStaleTasks();
    if(m_State==SceneLoadState::Requested){
        const auto absolute=AssetManager::Get().GetProjectRoot()/m_RequestedPath;
        const SceneLoadRequestID id=m_RequestID;
        m_WorkerTask=TaskService::Get().Submit(
            m_TaskScope,{"scene.read",TaskPriority::High},[absolute,id](CancellationToken token){
            WorkerResult result; result.requestID=id;
            token.ThrowIfCancellationRequested();
            if(!RuntimeFileSystem::Get().ReadText(absolute.string(),result.source,&result.error)){
                if(result.error.empty())result.error="cannot open scene file";
                return result;
            }
            token.ThrowIfCancellationRequested();
            result.success=true;
            return result;
        });
        m_State=SceneLoadState::Reading; m_Progress=0.05f; return false;
    }
    if(m_State==SceneLoadState::Reading){
        if(!m_WorkerTask.Valid()||!m_WorkerTask.IsReady())return false;
        WorkerResult result;
        try{result=m_WorkerTask.Get();}
        catch(const TaskCancelled&){m_State=SceneLoadState::Cancelled;return false;}
        catch(const std::exception& exception){m_LastError="scene read task failed: "+std::string(exception.what());m_State=SceneLoadState::Failed;return false;}
        m_WorkerTask={};
        if(result.requestID!=m_RequestID)return false;
        if(!result.success){m_LastError="failed to read scene '"+m_RequestedPath+"': "+result.error;m_State=SceneLoadState::Failed;return false;}
        m_Source=std::move(result.source);
        const SceneLoadRequestID id=m_RequestID;
        m_WorkerTask=TaskService::Get().Submit(
            m_TaskScope,{"scene.parse",TaskPriority::High},[source=std::move(m_Source),id](CancellationToken token)mutable{
            WorkerResult parsed;parsed.requestID=id;
            token.ThrowIfCancellationRequested();
            parsed.success=SceneSerializer::BuildLoadPlan(source,parsed.plan,&parsed.error);
            token.ThrowIfCancellationRequested();
            return parsed;
        });
        m_State=SceneLoadState::Parsing; m_Progress=0.15f; return false;
    }
    if(m_State==SceneLoadState::Parsing){
        if(!m_WorkerTask.Valid()||!m_WorkerTask.IsReady())return false;
        WorkerResult result;
        try{result=m_WorkerTask.Get();}
        catch(const TaskCancelled&){m_State=SceneLoadState::Cancelled;return false;}
        catch(const std::exception& exception){m_LastError="scene parse task failed: "+std::string(exception.what());m_State=SceneLoadState::Failed;return false;}
        m_WorkerTask={};
        if(result.requestID!=m_RequestID)return false;
        if(!result.success){m_LastError="failed to parse scene '"+m_RequestedPath+"': "+result.error;m_State=SceneLoadState::Failed;return false;}
        m_Plan=std::move(result.plan);m_State=SceneLoadState::Preloading;m_Progress=0.3f;return false;
    }
    if(m_State==SceneLoadState::Preloading){
        if(!m_PreloadStarted){
            m_PinnedDependencies=m_Plan.assetDependencies;
            for(const std::string& path:m_PinnedDependencies) AssetManager::Get().Pin(path);
            m_DependenciesPinned=true;
            const auto paths=m_Plan.root.value("preloadAssets",std::vector<std::string>{});
            for(const std::string& path:paths)m_PreloadTasks.push_back(AssetManager::Get().RequestAsync(path));
            m_PreloadStarted=true;
        }
        size_t ready=0;
        for(auto& task:m_PreloadTasks)if(task.IsReady())++ready;
        if(ready<m_PreloadTasks.size()){
            m_Progress=0.3f+0.15f*static_cast<float>(ready)/static_cast<float>(std::max<size_t>(1,m_PreloadTasks.size()));return false;
        }
        if(m_Options.waitForCriticalAssets)for(auto& task:m_PreloadTasks)try{
            if(!task.Get()){
                m_LastError="failed to preload a critical scene asset";
                ReleasePinnedDependencies();m_State=SceneLoadState::Failed;return false;
            }
        }catch(const std::exception& exception){
            m_LastError="critical scene asset task failed: "+std::string(exception.what());
            ReleasePinnedDependencies();m_State=SceneLoadState::Failed;return false;
        }
        m_Candidate=std::make_unique<Scene>();m_Instantiation={};
        m_State=SceneLoadState::Instantiating;m_Progress=0.45f;return false;
    }
    if(m_State==SceneLoadState::Uploading){
        if(!GpuUploadQueue::Get().IsFenceComplete(m_UploadFence))return false;
        m_State=SceneLoadState::Activating;m_Progress=0.98f;return false;
    }
    if(m_State==SceneLoadState::Activating){
        m_Candidate->AdoptAssetPins(std::move(m_PinnedDependencies));
        m_DependenciesPinned=false;
        loadedScene=std::move(m_Candidate);m_State=SceneLoadState::Ready;m_Progress=1.0f;return true;
    }
    if(m_State!=SceneLoadState::Instantiating||!m_Candidate)return false;
    const auto start=std::chrono::steady_clock::now(); bool complete=false;
    do {
        if(!SceneSerializer::InstantiateLoadPlan(*m_Candidate,m_Plan,m_Instantiation,1,complete,&m_LastError)){
            ReleasePinnedDependencies();
            m_Candidate.reset();m_State=SceneLoadState::Failed;return false;
        }
        const size_t total=std::max<size_t>(1,m_Plan.actors.size());
        m_Progress=0.45f+0.4f*static_cast<float>(m_Instantiation.nextActor)/static_cast<float>(total);
    } while(!complete&&std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<m_Options.instantiateBudgetMs);
    if(!complete)return false;
    m_UploadFence=GpuUploadQueue::Get().CaptureFence();
    m_State=SceneLoadState::Uploading;m_Progress=0.9f;return false;
}

void SceneManager::SetPersistentValue(const std::string& key,nlohmann::json value){if(!key.empty())m_PersistentData[key]=std::move(value);}
nlohmann::json SceneManager::GetPersistentValue(const std::string& key,const nlohmann::json& fallback)const{const auto it=m_PersistentData.find(key);return it==m_PersistentData.end()?fallback:*it;}
