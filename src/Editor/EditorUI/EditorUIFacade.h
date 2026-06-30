#pragma once

#include <string>

class EditorContext;

namespace Editor::Scripting {

void SetActiveEditorContext(EditorContext* context);
EditorContext* GetActiveEditorContext();
void SetActiveEditorPanelID(const std::string& panelID);
void ClearActiveEditorPanelID();

void RegisterEditorUIFacade(void* scriptEngine);
void RegisterEditorContextBindings(void* scriptEngine);

bool DrawViewportImage(EditorContext& context, const std::string& which);

} // namespace Editor::Scripting
