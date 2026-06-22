#include "UI/Core/UICanvas.h"

#include "Core/Logger.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

UICanvas::~UICanvas()
{
    CloseDocument();
}

void UICanvas::SetContext(Rml::Context* context)
{
    if (m_Context == context) return;
    CloseDocument();
    m_Context = context;
}

bool UICanvas::LoadDocument(const std::string& path)
{
    m_DocumentPath = path;
    CloseDocument();
    if (!m_Context || path.empty()) return false;

    m_Document = m_Context->LoadDocument(path);
    if (!m_Document) {
        Logger::Warn("[UI] Failed to load Rml document: ", path);
        return false;
    }
    if (m_Visible) m_Document->Show();
    return true;
}

void UICanvas::CloseDocument()
{
    if (m_Document) {
        m_Document->Close();
        m_Document = nullptr;
    }
}

bool UICanvas::Reload()
{
    const std::string path = m_DocumentPath;
    return LoadDocument(path);
}

void UICanvas::SetVisible(bool visible)
{
    m_Visible = visible;
    if (!m_Document) return;
    if (visible) m_Document->Show();
    else m_Document->Hide();
}
