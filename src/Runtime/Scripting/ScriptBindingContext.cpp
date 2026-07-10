#include "Scripting/ScriptBindingContext.h"

ScriptBindingContext& ScriptBindingContext::Get()
{
    static ScriptBindingContext context;
    return context;
}

void ScriptBindingContext::ClearUIEventBridge(UIEventBridge* bridge)
{
    if (m_UIEventBridge == bridge) m_UIEventBridge = nullptr;
}

void ScriptBindingContext::ClearUISystem(UISystem* system)
{
    if (m_UISystem == system) m_UISystem = nullptr;
}
