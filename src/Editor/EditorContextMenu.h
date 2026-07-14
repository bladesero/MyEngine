#pragma once

#include <cstdint>
#include <functional>

// Describes what was right-clicked so context-menu handlers can decide what
// items to contribute.
struct ContextMenuContext {
    enum class Target { None, Actor, Asset, Component, LogEntry };
    Target target = Target::None;
    void* pointer = nullptr; // Actor*, Asset*, etc.
    uint64_t id = 0;
};

// RAII wrapper around ImGui::BeginPopup / EndPopup with built-in menu-item
// rendering and static right-click detection helpers.
//
// Typical usage in a panel's DrawContent:
//
//   EditorContextMenu::DetectItem("##Ctx");
//   EditorContextMenu menu("##Ctx");
//   if (menu.IsOpen()) {
//       ContextMenuContext ctx{Target::Actor, ptr, id};
//       ShowContextMenu(ctx, menu);
//   }  // ~EditorContextMenu calls EndPopup
//
// For empty-area right-click:
//
//   EditorContextMenu::DetectWindow("##Ctx");
//   EditorContextMenu menu("##Ctx");
//   if (menu.IsOpen()) { ... }
//
class EditorContextMenu {
public:
    explicit EditorContextMenu(const char* id);
    ~EditorContextMenu();

    bool IsOpen() const { return m_Open; }

    // --- Menu items -------------------------------------------------------
    // Render a clickable item.  When clicked the callback fires and the
    // popup is automatically closed.
    bool AddAction(const char* label, std::function<void()> callback, bool enabled = true);

    // Render a horizontal separator.
    void AddSeparator();

    // Explicitly close the popup (CloseCurrentPopup + EndPopup).
    void Close();

    // --- Static detection helpers -----------------------------------------
    // Call after rendering an item; detects right-click on that item and
    // calls OpenPopup.  Returns true when the popup was opened.
    static bool DetectItem(const char* id);

    // Call inside a window; detects right-click on empty window area
    // (not blocked by an active popup) and calls OpenPopup.
    static bool DetectWindow(const char* id);

    // Raw OpenPopup for external triggers.
    static void OpenPopup(const char* id);

private:
    bool m_Open = false;
};
