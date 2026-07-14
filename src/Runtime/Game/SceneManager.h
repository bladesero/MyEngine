#pragma once
#include "Scene/SceneSerializer.h"
#include "Core/TaskService.h"
#include "Renderer/GpuUploadQueue.h"
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class Scene;
class Asset;
enum class SceneLoadState {
    Idle,
    Requested,
    Reading,
    Parsing,
    Preloading,
    Instantiating,
    Ready,
    Failed,
    Cancelled,
    Uploading,
    Activating
};
using SceneLoadStage = SceneLoadState;
using SceneLoadRequestID = uint64_t;

struct SceneLoadOptions {
    double instantiateBudgetMs = 4.0;
    double gpuUploadBudgetMs = 4.0;
    uint64_t gpuUploadBudgetBytes = 32ull * 1024ull * 1024ull;
    bool waitForCriticalAssets = true;
};

class SceneManager {
public:
    ~SceneManager();
    SceneLoadRequestID RequestLoad(std::string projectRelativePath, SceneLoadOptions options = {});
    bool Process(std::unique_ptr<Scene>& loadedScene);
    void CancelLoad();
    bool RetryLastLoad();
    void DismissFailure();
    bool IsLoading() const;
    static const char* StageName(SceneLoadState state);
    SceneLoadState GetState() const { return m_State; }
    SceneLoadStage GetStage() const { return m_State; }
    float GetProgress() const { return m_Progress; }
    SceneLoadRequestID GetRequestID() const { return m_RequestID; }
    const std::string& GetRequestedPath() const { return m_RequestedPath; }
    const std::string& GetLastError() const { return m_LastError; }
    void SetPersistentValue(const std::string& key, nlohmann::json value);
    nlohmann::json GetPersistentValue(const std::string& key, const nlohmann::json& fallback = {}) const;
    const nlohmann::json& GetPersistentData() const { return m_PersistentData; }
    void ClearPersistentData() { m_PersistentData = nlohmann::json::object(); }

private:
    struct WorkerResult {
        SceneLoadRequestID requestID = 0;
        bool success = false;
        std::string source;
        SceneLoadPlan plan;
        std::string error;
    };
    static bool IsSafeScenePath(const std::string& path);
    void ReapStaleTasks();
    void ReleasePinnedDependencies();
    SceneLoadState m_State = SceneLoadState::Idle;
    std::string m_RequestedPath, m_LastError;
    nlohmann::json m_PersistentData = nlohmann::json::object();
    TaskScope m_TaskScope;
    TaskHandle<WorkerResult> m_WorkerTask;
    std::vector<TaskHandle<WorkerResult>> m_StaleTasks;
    SceneLoadRequestID m_RequestID = 0;
    SceneLoadOptions m_Options;
    float m_Progress = 0.0f;
    SceneLoadPlan m_Plan;
    std::string m_Source;
    SceneInstantiationState m_Instantiation;
    std::unique_ptr<Scene> m_Candidate;
    std::vector<TaskHandle<std::shared_ptr<Asset>>> m_PreloadTasks;
    std::vector<std::string> m_PinnedDependencies;
    bool m_PreloadStarted = false;
    bool m_DependenciesPinned = false;
    GpuUploadFence m_UploadFence = 0;
};
