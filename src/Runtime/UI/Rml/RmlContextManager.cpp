#include "UI/Rml/RmlContextManager.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>

bool RmlContextManager::Create(const std::string& name, int width, int height)
{
    Destroy();
    m_Name = name;
    m_Context = Rml::CreateContext(name, Rml::Vector2i(width, height));
    return m_Context != nullptr;
}

void RmlContextManager::Destroy()
{
    if (!m_Context) return;
    Rml::RemoveContext(m_Name);
    m_Context = nullptr;
    m_Name.clear();
}

void RmlContextManager::Resize(int width, int height)
{
    if (!m_Context || width <= 0 || height <= 0) return;
    m_Context->SetDimensions({width, height});
}
