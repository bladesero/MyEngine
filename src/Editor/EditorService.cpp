#include "Editor/EditorService.h"

#include "Editor/EditorContext.h"

void EditorServiceCollection::AttachAll(EditorContext& context)
{
    for (Entry& entry : m_Entries) {
        entry.registerService(context);
        entry.service->OnAttach(context);
    }
}

void EditorServiceCollection::UpdateAll(float deltaSeconds)
{
    for (Entry& entry : m_Entries) entry.service->OnUpdate(deltaSeconds);
}

void EditorServiceCollection::DetachAll(EditorContext& context)
{
    for (auto it = m_Entries.rbegin(); it != m_Entries.rend(); ++it) {
        it->service->OnDetach();
    }
    context.ClearServices();
    m_Entries.clear();
}
