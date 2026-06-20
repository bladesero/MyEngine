#pragma once

#include "Editor/EditorContextMenu.h" // ContextMenuContext

#include <functional>
#include <string>
#include <vector>

class EditorContext;
class EditorContextMenu;

using ContextMenuHandler = std::function<void(
    const ContextMenuContext& ctx, EditorContextMenu& menu)>;

class EditorPanel {
public:
    EditorPanel(std::string id, std::string title);
    virtual ~EditorPanel() = default;
    virtual void OnAttach(EditorContext& context) { m_Context = &context; }
    virtual void OnDetach() { m_Context = nullptr; }
    virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
    virtual void OnImGui();
    const std::string& GetID() const { return m_ID; }
    const std::string& GetTitle() const { return m_Title; }
    bool IsVisible() const { return m_Visible; }
    void SetVisible(bool value) { m_Visible = value; }

    // Register a handler that contributes items when a context menu opens.
    void RegisterContextMenuHandler(ContextMenuHandler handler);

protected:
    virtual void DrawContent() = 0;
    EditorContext* GetContext() const { return m_Context; }

    // Call inside a context-menu popup (after BeginPopup returned true).
    // Iterates all registered handlers and lets them build the menu.
    void ShowContextMenu(const ContextMenuContext& ctx,
                         EditorContextMenu& menu);

private:
    std::string m_ID, m_Title;
    bool m_Visible = true;
    EditorContext* m_Context = nullptr;
    std::vector<ContextMenuHandler> m_ContextMenuHandlers;
};
