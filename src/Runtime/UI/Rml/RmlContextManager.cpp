#include "UI/Rml/RmlContextManager.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>

#include <unordered_set>

namespace {

std::unordered_set<Rml::Context*> g_AliveContexts;

} // namespace

bool RmlContextManager::Create(const std::string& name, int width, int height) {
    Destroy();
    m_Name = name;
    m_Context = Rml::CreateContext(name, Rml::Vector2i(width, height));
    if (m_Context)
        g_AliveContexts.insert(m_Context);
    return m_Context != nullptr;
}

void RmlContextManager::Destroy() {
    if (!m_Context)
        return;
    g_AliveContexts.erase(m_Context);
    Rml::RemoveContext(m_Name);
    m_Context = nullptr;
    m_Name.clear();
}

void RmlContextManager::Resize(int width, int height) {
    if (!m_Context || width <= 0 || height <= 0)
        return;
    m_Context->SetDimensions({width, height});
}

bool RmlContextManager::IsContextAlive(Rml::Context* context) {
    return context && g_AliveContexts.count(context) != 0;
}
