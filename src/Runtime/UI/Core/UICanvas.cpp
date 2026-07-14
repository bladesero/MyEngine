#include "UI/Core/UICanvas.h"

#include "Core/Logger.h"
#include "UI/Rml/RmlContextManager.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

UICanvas::~UICanvas() {
    CloseDocument();
}

void UICanvas::SetContext(Rml::Context* context) {
    if (m_Context == context)
        return;
    CloseDocument();
    m_Context = context;
}

bool UICanvas::LoadDocument(const std::string& path) {
    m_DocumentPath = path;
    m_MemoryDocumentSource.clear();
    m_MemoryDocumentURL.clear();
    CloseDocument();
    if (!m_Context || path.empty())
        return false;

    m_Document = m_Context->LoadDocument(path);
    if (!m_Document) {
        Logger::Warn("[UI] Failed to load Rml document: ", path);
        return false;
    }
    if (m_Visible)
        m_Document->Show();
    return true;
}

bool UICanvas::LoadDocumentFromMemory(const std::string& source, const std::string& sourceURL) {
    m_MemoryDocumentSource = source;
    m_MemoryDocumentURL = sourceURL;
    CloseDocument();
    if (!m_Context || source.empty())
        return false;

    m_Document = m_Context->LoadDocumentFromMemory(source, sourceURL);
    if (!m_Document) {
        Logger::Warn("[UI] Failed to load generated Rml document: ", sourceURL);
        return false;
    }
    if (m_Visible)
        m_Document->Show();
    return true;
}

void UICanvas::CloseDocument() {
    if (m_Document) {
        if (RmlContextManager::IsContextAlive(m_Context)) {
            m_Document->Close();
        }
        m_Document = nullptr;
    }
}

bool UICanvas::Reload() {
    if (!m_MemoryDocumentSource.empty()) {
        return LoadDocumentFromMemory(m_MemoryDocumentSource, m_MemoryDocumentURL);
    }
    const std::string path = m_DocumentPath;
    return LoadDocument(path);
}

void UICanvas::SetVisible(bool visible) {
    m_Visible = visible;
    if (!m_Document)
        return;
    if (visible)
        m_Document->Show();
    else
        m_Document->Hide();
}
