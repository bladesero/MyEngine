#include "Editor/EditorShaderWatchService.h"

#include "Core/Logger.h"
#include "Renderer/ShaderManager.h"

void EditorShaderWatchService::OnAttach(EditorContext& context) { EditorService::OnAttach(context); Refresh(); }
void EditorShaderWatchService::Refresh() {
    m_Paths.clear(); m_Times.clear(); const std::filesystem::path root="src/Runtime/Renderer/Shaders"; std::error_code error;
    if(!std::filesystem::is_directory(root,error)) return;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root,error)) {
        if(!entry.is_regular_file()) continue; const auto ext=entry.path().extension();
        if(ext==".hlsl"||ext==".hlsli") { const std::string path=entry.path().generic_string(); m_Paths.push_back(path); m_Times[path]=entry.last_write_time(error); }
    }
}
void EditorShaderWatchService::OnUpdate(float deltaSeconds) {
    m_Accumulator+=deltaSeconds; if(m_Accumulator<0.5f) return; m_Accumulator=0;
    std::error_code error;
    for(const std::string& path:m_Paths) { const auto time=std::filesystem::last_write_time(path,error); if(error) continue;
        auto it=m_Times.find(path); if(it!=m_Times.end()&&it->second!=time) { it->second=time; ShaderManager::Get().Recompile(path); Logger::Info("[Editor] Shader changed: ",path); }}
}
