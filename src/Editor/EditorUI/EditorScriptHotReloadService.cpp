#include "Editor/EditorUI/EditorScriptHotReloadService.h"

#include "Core/Logger.h"
#include "Editor/EditorUI/EditorAngelScriptDomain.h"

void EditorScriptHotReloadService::OnUpdate(float deltaSeconds)
{
    if (!m_Domain || !m_Domain->IsLoaded()) return;
    m_Accumulator += deltaSeconds;
    if (m_Accumulator < 1.0f) return;
    m_Accumulator = 0.0f;

    std::string error;
    if (!m_Domain->ReloadIfChanged(&error) && !error.empty()) {
        Logger::Warn("[EditorScript] Hot reload failed: ", error);
    }
}
