#include "Editor/EditorShaderWatchService.h"

#include "Core/Logger.h"
#include "Editor/EditorContext.h"
#include "Game/SceneRenderLayer.h"
#include "Renderer/ShaderManager.h"
#include "Renderer/ShaderCooker.h"

#include <algorithm>
#include <unordered_set>

void EditorShaderWatchService::OnAttach(EditorContext& context) {
    EditorService::OnAttach(context);
    Refresh();
}
void EditorShaderWatchService::Refresh() {
    m_Paths.clear();
    m_Times.clear();
    m_Dependents.clear();
    std::error_code error;
    std::vector<std::filesystem::path> roots = {"EngineContent/Shaders"};
    if (GetContext())
        roots.push_back(GetContext()->GetContentRoot() / "Shaders");
    for (const auto& root : roots) {
        if (!std::filesystem::is_directory(root, error))
            continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
            if (!entry.is_regular_file())
                continue;
            const auto ext = entry.path().extension();
            if (ext == ".shader" || ext == ".hlsl" || ext == ".hlsli") {
                const std::string path = entry.path().generic_string();
                m_Paths.push_back(path);
                m_Times[path] = entry.last_write_time(error);
                if (ext == ".shader") {
                    m_Dependents[path].push_back(path);
                    std::vector<std::string> dependencies;
                    const std::filesystem::path allowedRoot =
                        GetContext() && path.rfind(GetContext()->GetContentRoot().generic_string(), 0) == 0
                            ? GetContext()->GetContentRoot()
                            : root.parent_path();
                    std::string dependencyError;
                    if (ShaderCooker::CollectDependencies(entry.path(), allowedRoot, dependencies, &dependencyError))
                        for (const std::string& dependency : dependencies)
                            m_Dependents[std::filesystem::path(dependency).lexically_normal().generic_string()]
                                .push_back(path);
                }
            }
        }
    }
}
void EditorShaderWatchService::OnUpdate(float deltaSeconds) {
    if (!GetContext() || m_Paths.empty())
        return;
    m_Accumulator += deltaSeconds;
    if (m_Accumulator < 0.5f)
        return;
    m_Accumulator = 0;
    const auto previousTimes = m_Times;
    Refresh();
    std::unordered_set<std::string> shaders;
    for (const auto& [path, time] : m_Times) {
        const auto previous = previousTimes.find(path);
        if (previous == previousTimes.end() || previous->second == time)
            continue;
        const auto dependents = m_Dependents.find(path);
        if (dependents != m_Dependents.end())
            shaders.insert(dependents->second.begin(), dependents->second.end());
        Logger::Info("[Editor] Shader dependency changed: ", path);
    }
    for (const std::string& shader : shaders) {
        ShaderManager::Get().Recompile(shader);
        if (GetContext()->GetSceneLayer())
            GetContext()->GetSceneLayer()->InvalidateMaterialPreview();
    }
}
